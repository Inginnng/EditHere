/**
 * Shared auto-routing runtime used by both `cli/edithere.mjs` and `cli/unified-mcp.mjs`.
 * Automatically selects Desktop `edithere-cli` when installed, or falls back cleanly
 * to the bundled Web Agent (`web/agent.cjs`) without modifying any existing binaries.
 */
import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { inspectDesktopCli, resolveDesktopCli, runDesktopCliSync } from './desktop-locator.mjs';
import { startInboxWatcher } from './inbox-watcher.mjs';
import { capturePlanWithBrowser, launchCaptureBrowser } from '../scripts/capture-web.mjs';

const require = createRequire(import.meta.url);
const imageSetHelpers = require('../web/image-set.cjs');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WEB_AGENT_CLI = path.join(ROOT, 'web', 'agent.cjs');

const desktopSessions = new Map();

let warmBrowser = null;
let warmBrowserLaunching = null;
let warmIdleTimer = null;

function warmIdleTimeoutMs(env = process.env) {
  const raw = Number(env?.EDITHERE_BROWSER_IDLE_MS ?? 60000);
  if (!Number.isFinite(raw)) return 60000;
  return Math.max(100, Math.min(300000, Math.round(raw)));
}

function clearWarmIdleTimer() {
  if (warmIdleTimer) {
    clearTimeout(warmIdleTimer);
    warmIdleTimer = null;
  }
}

function scheduleWarmBrowserIdleClose(idleMs) {
  clearWarmIdleTimer();
  warmIdleTimer = setTimeout(() => {
    void closeWarmBrowser();
  }, idleMs);
  warmIdleTimer.unref?.();
}

async function acquireWarmBrowser() {
  clearWarmIdleTimer();
  if (warmBrowser && (typeof warmBrowser.isConnected !== 'function' || warmBrowser.isConnected())) {
    return warmBrowser;
  }
  warmBrowser = null;
  if (!warmBrowserLaunching) {
    warmBrowserLaunching = launchCaptureBrowser({ headless: true })
      .then((browser) => {
        warmBrowser = browser;
        browser.once?.('disconnected', () => {
          if (warmBrowser === browser) {
            warmBrowser = null;
            clearWarmIdleTimer();
          }
        });
        return browser;
      })
      .finally(() => {
        warmBrowserLaunching = null;
      });
  }
  return warmBrowserLaunching;
}

export async function closeWarmBrowser() {
  clearWarmIdleTimer();
  if (warmBrowserLaunching) {
    try {
      await warmBrowserLaunching;
    } catch {
      /* ignore launch error on teardown */
    }
  }
  const browser = warmBrowser;
  warmBrowser = null;
  if (browser) {
    await browser.close().catch(() => {});
  }
}

export function getWarmBrowserState() {
  return {
    connected: Boolean(warmBrowser && (typeof warmBrowser.isConnected !== 'function' || warmBrowser.isConnected())),
    idleTimerActive: Boolean(warmIdleTimer),
  };
}

function makeTempFile(subDir, prefix, ext) {
  const dir = path.join(os.tmpdir(), subDir);
  fs.mkdirSync(dir, { recursive: true });
  const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 14);
  const rand = crypto.randomBytes(4).toString('hex');
  return path.join(dir, `${prefix}-${stamp}-${rand}${ext}`);
}

function spawnTrackedDesktopSession(desktopPath, cliArgs, { inputPath = null, outputPath, watcher = null, env = process.env } = {}) {
  const sessionId = crypto.randomBytes(16).toString('hex');
  const child = spawn(desktopPath, cliArgs, {
    env,
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
  });
  const record = {
    sessionId,
    transport: 'desktop',
    inputPath,
    outputPath,
    startedAt: Date.now(),
    state: 'pending',
    exitCode: null,
    stdout: '',
    stderr: '',
    receipt: null,
    watcher,
    child,
  };
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', (chunk) => { record.stdout += chunk; });
  child.stderr.on('data', (chunk) => { record.stderr += chunk; });
  child.once('error', (err) => {
    if (record.watcher) {
      record.watcher.stop();
      record.watcher = null;
    }
    record.exitCode = -1;
    record.stderr += String(err?.message || err);
    record.state = 'failed';
  });
  child.once('close', (code) => {
    record.exitCode = code;
    if (record.watcher) {
      record.watcher.stop();
      record.watcher = null;
    }
    const line = record.stdout.trim().split(/\r?\n/).pop();
    if (line && line.startsWith('{')) {
      try {
        record.receipt = JSON.parse(line);
      } catch {
        record.receipt = null;
      }
    }
    if (code === 0 && fs.existsSync(outputPath)) {
      record.state = 'completed';
    } else if (code === 6) {
      record.state = 'cancelled';
    } else if (code === 7) {
      record.state = 'expired';
    } else {
      record.state = 'failed';
    }
  });
  desktopSessions.set(sessionId, record);
  return {
    ok: true,
    transport: 'desktop',
    sessionId,
    state: 'pending',
    outputPath,
  };
}

/**
 * Determine effective transport (`desktop` | `web`) given requested mode.
 */
export function resolveTransport({ mode = 'auto', env = process.env } = {}) {
  if (!['auto', 'desktop', 'web'].includes(mode)) {
    throw new Error(`Invalid mode "${mode}". Expected auto, desktop, or web.`);
  }
  const desktop = resolveDesktopCli({ env });
  if (mode === 'desktop') {
    if (!desktop.found || !desktop.path) {
      throw new Error(desktop.error || 'Desktop edithere-cli requested but not found.');
    }
    return { transport: 'desktop', desktop };
  }
  if (mode === 'auto' && desktop.found && desktop.path) {
    return { transport: 'desktop', desktop };
  }
  return { transport: 'web', desktop };
}

/**
 * Query status for the selected or auto-detected transport without launching a GUI window.
 */
export function queryStatus({ mode = 'auto', env = process.env } = {}) {
  const { transport } = resolveTransport({ mode, env });
  if (transport === 'desktop') {
    const info = inspectDesktopCli({ env });
    return {
      ok: true,
      transport: 'desktop',
      running: info.running,
      version: info.version,
      supportsVideo: info.supportsVideo,
      supportsImageSet: info.supportsImageSet,
      cliPath: info.path,
      source: info.source,
      statusProbe: info.statusProbe,
    };
  }
  const webReady = fs.existsSync(WEB_AGENT_CLI) && fs.existsSync(path.join(ROOT, 'web', 'EditHere.html'));
  const port = Number(env.EDITHERE_WEB_PORT || 7330);
  return {
    ok: webReady,
    transport: 'web',
    webReady,
    agentCli: WEB_AGENT_CLI,
    port: Number.isInteger(port) ? port : 7330,
  };
}

function runWebAgent(args, { env = process.env, timeoutMs = 20000 } = {}) {
  const res = spawnSync(process.execPath, [WEB_AGENT_CLI, ...args], {
    cwd: ROOT,
    env,
    encoding: 'utf8',
    timeout: timeoutMs,
    windowsHide: true,
  });
  const stdout = String(res.stdout || '').trim();
  const stderr = String(res.stderr || '').trim();
  if (res.status !== 0) {
    let errObj = null;
    try {
      errObj = JSON.parse(stderr || stdout);
    } catch {
      errObj = null;
    }
    throw new Error(errObj?.error || stderr || stdout || `web/agent.cjs exited with code ${res.status}`);
  }
  return JSON.parse(stdout);
}

function isJsonManifest(filePath) {
  return path.extname(filePath).toLowerCase() === '.json';
}

function buildDesktopAnnotateArgs({ inputPath, outputPath, pageContext, timeoutSeconds }) {
  const args = ['annotate', inputPath, '--output', outputPath, '--timeout', String(timeoutSeconds)];
  if (pageContext) {
    args.push('--page-context', pageContext);
  }
  return args;
}

/**
 * Run blocking CLI annotation (`edithere annotate`), routing Desktop or Web Agent automatically.
 */
export function runBlockingAnnotate({
  inputPath,
  outputPath,
  mode = 'auto',
  pageContext = null,
  timeoutSeconds = 1800,
  inboxDir = null,
  env = process.env,
} = {}) {
  const absInput = path.resolve(inputPath);
  const absOutput = path.resolve(outputPath);
  if (!fs.existsSync(absInput)) throw new Error(`Input file does not exist: ${absInput}`);
  if (fs.existsSync(absOutput)) throw new Error(`Output file already exists: ${absOutput}`);

  const { transport, desktop } = resolveTransport({ mode, env });
  const watcher = inboxDir && isJsonManifest(absInput)
    ? startInboxWatcher({ inboxDir, manifestPath: absInput })
    : null;

  try {
    if (transport === 'desktop') {
      const cliArgs = buildDesktopAnnotateArgs({ inputPath: absInput, outputPath: absOutput, pageContext, timeoutSeconds });
      const res = spawnSync(desktop.path, cliArgs, {
        env,
        encoding: 'utf8',
        windowsHide: true,
      });
      const stdout = String(res.stdout || '').trim();
      const stderr = String(res.stderr || '').trim();
      let json = null;
      try {
        json = stdout ? JSON.parse(stdout.split('\n').pop()) : null;
      } catch {
        json = null;
      }
      return {
        ok: res.status === 0 && json?.ok === true,
        transport: 'desktop',
        exitCode: res.status,
        receipt: json,
        stderr: stderr || undefined,
      };
    }

    const args = isJsonManifest(absInput)
      ? ['start', '--project', absInput, '--output', absOutput, '--timeout', String(timeoutSeconds)]
      : ['start', '--image', absInput, '--output', absOutput, '--timeout', String(timeoutSeconds)];
    if (!isJsonManifest(absInput) && pageContext) {
      args.push('--page-context', pageContext);
    }
    const started = runWebAgent(args, { env });
    return {
      ok: true,
      transport: 'web',
      state: 'pending',
      ...started,
    };
  } finally {
    if (watcher) watcher.stop();
  }
}

/**
 * Start an asynchronous annotation session (Desktop child process or Web Agent session).
 */
export function startAsyncSession({
  inputPath,
  outputPath = null,
  mode = 'auto',
  pageContext = null,
  timeoutSeconds = 1800,
  inboxDir = null,
  env = process.env,
} = {}) {
  const absInput = path.resolve(inputPath);
  const absOutput = outputPath ? path.resolve(outputPath) : makeTempFile('edithere-feedback', 'annotate', '.json');
  if (!fs.existsSync(absInput)) throw new Error(`Input file does not exist: ${absInput}`);
  if (fs.existsSync(absOutput)) throw new Error(`Output file already exists: ${absOutput}`);

  const { transport, desktop } = resolveTransport({ mode, env });
  const watcher = inboxDir && isJsonManifest(absInput)
    ? startInboxWatcher({ inboxDir, manifestPath: absInput })
    : null;

  if (transport === 'desktop') {
    const cliArgs = buildDesktopAnnotateArgs({ inputPath: absInput, outputPath: absOutput, pageContext, timeoutSeconds });
    return spawnTrackedDesktopSession(desktop.path, cliArgs, {
      inputPath: absInput,
      outputPath: absOutput,
      watcher,
      env,
    });
  }

  try {
    const args = isJsonManifest(absInput)
      ? ['start', '--project', absInput, '--output', absOutput, '--timeout', String(timeoutSeconds)]
      : ['start', '--image', absInput, '--output', absOutput, '--timeout', String(timeoutSeconds)];
    if (!isJsonManifest(absInput) && pageContext) {
      args.push('--page-context', pageContext);
    }
    const started = runWebAgent(args, { env });
    return {
      ok: true,
      transport: 'web',
      state: 'pending',
      ...started,
    };
  } catch (error) {
    if (watcher) watcher.stop();
    throw error;
  }
}

/**
 * Poll a previously started annotation session by `sessionId`.
 */
export function pollAsyncSession(sessionId, { env = process.env } = {}) {
  const local = desktopSessions.get(sessionId);
  if (local) {
    const elapsedSeconds = Math.round((Date.now() - local.startedAt) / 1000);
    if (local.state === 'pending') {
      return {
        ok: true,
        transport: 'desktop',
        sessionId,
        state: 'pending',
        elapsedSeconds,
        outputPath: local.outputPath,
      };
    }
    let feedbackSummary = null;
    if (local.state === 'completed' && fs.existsSync(local.outputPath)) {
      const parsed = JSON.parse(fs.readFileSync(local.outputPath, 'utf8'));
      feedbackSummary = {
        schemaVersion: parsed.schemaVersion || null,
        objectsCount: Array.isArray(parsed.objects) ? parsed.objects.length : undefined,
        pagesCount: Array.isArray(parsed.pages) ? parsed.pages.length : undefined,
      };
    }
    return {
      ok: local.state === 'completed',
      transport: 'desktop',
      sessionId,
      state: local.state,
      exitCode: local.exitCode,
      outputPath: local.outputPath,
      feedbackSummary,
      receipt: local.receipt || undefined,
      error: local.state === 'failed' ? (local.receipt?.error || local.stderr || undefined) : undefined,
    };
  }

  const polled = runWebAgent(['poll', '--session', sessionId], { env });
  return {
    ok: true,
    transport: 'web',
    ...polled,
  };
}

/**
 * List visible desktop windows (excluding EditHere) via `edithere-cli windows`.
 */
export function listDesktopWindows({ env = process.env } = {}) {
  const desktop = resolveDesktopCli({ env });
  if (!desktop.found || !desktop.path) {
    throw new Error(desktop.error || 'Desktop edithere-cli not found.');
  }
  const res = runDesktopCliSync(desktop.path, ['windows'], { timeoutMs: 8000 });
  return {
    ok: res.exitCode === 0 && res.json?.ok === true,
    transport: 'desktop',
    exitCode: res.exitCode,
    windows: res.json?.windows || [],
    receipt: res.json,
    error: res.json?.error || res.error || (res.stderr || undefined),
  };
}

function normalizeWebViewport(raw) {
  if (!raw) return { width: 1440, height: 900 };
  if (typeof raw === 'string') {
    const match = /^(\d{2,5})x(\d{2,5})$/i.exec(raw.trim());
    if (!match) throw new Error(`Invalid viewport "${raw}". Expected WxH.`);
    return { width: Number(match[1]), height: Number(match[2]) };
  }
  if (typeof raw === 'object' && Number.isInteger(raw.width) && Number.isInteger(raw.height)) {
    return { width: raw.width, height: raw.height };
  }
  throw new Error('Invalid viewport format.');
}

/**
 * Single-call fast capture router across `interactive | screen | window | web`,
 * with optional direct handoff into a non-blocking annotation session.
 */
export async function runFastCapture({
  source = 'interactive',
  window = null,
  windowId = null,
  url = null,
  urls = null,
  viewport = null,
  dpr = 2,
  theme = 'light',
  fullPage = false,
  selector = null,
  waitSelector = null,
  waitMs = 0,
  concurrency = 3,
  timeoutMs = 30000,
  annotate = false,
  outputPath = null,
  imagePath = null,
  pageContext = null,
  timeoutSeconds = 1800,
  mode = 'auto',
  env = process.env,
} = {}) {
  if (!['interactive', 'screen', 'window', 'web'].includes(source)) {
    throw new Error(`Invalid capture source "${source}". Expected interactive, screen, window, or web.`);
  }

  if (source === 'web') {
    const targetUrls = Array.isArray(urls) && urls.length > 0 ? urls : url ? [url] : [];
    if (targetUrls.length === 0) {
      throw new Error('Web capture requires `url` or non-empty `urls`.');
    }
    const vp = normalizeWebViewport(viewport);
    const clampedConcurrency = Math.min(3, Math.max(1, Number(concurrency) || 3));
    const batchDir = targetUrls.length > 1 || annotate
      ? makeTempFile('edithere-captures', 'web-batch', '')
      : null;
    if (batchDir) fs.mkdirSync(batchDir, { recursive: true });

    const jobs = targetUrls.map((targetUrl, idx) => {
      const targetOutput = targetUrls.length === 1
        ? path.resolve(
            imagePath || (!annotate && outputPath ? outputPath : makeTempFile('edithere-captures', 'web', '.png'))
          )
        : path.join(batchDir, `page-${idx + 1}.png`);
      return {
        id: `page-${idx + 1}`,
        name: targetUrl,
        url: targetUrl,
        output: targetOutput,
        viewport: vp,
        dpr: dpr ?? 2,
        theme: theme || 'light',
        fullPage: Boolean(fullPage),
        selector: selector || null,
        waitSelector: waitSelector || null,
        waitMs: waitMs ?? 0,
        timeout: timeoutMs ?? 30000,
      };
    });

    const browser = await acquireWarmBrowser();
    let captures;
    try {
      captures = await capturePlanWithBrowser(browser, jobs, { concurrency: clampedConcurrency });
    } finally {
      scheduleWarmBrowserIdleClose(warmIdleTimeoutMs(env));
    }

    if (!annotate) {
      return {
        ok: true,
        transport: 'web',
        source: 'web',
        count: captures.length,
        outputPath: captures.length === 1 ? captures[0].output : undefined,
        width: captures.length === 1 ? captures[0].width : undefined,
        height: captures.length === 1 ? captures[0].height : undefined,
        captures,
      };
    }

    let annotateInput = captures[0].output;
    if (captures.length > 1 || (Array.isArray(urls) && urls.length > 0)) {
      const manifestPath = path.join(batchDir, 'project.json');
      imageSetHelpers.writeManifest(
        manifestPath,
        { schemaVersion: imageSetHelpers.VERSION, name: pageContext || 'Web Capture', pages: [] },
        { create: true }
      );
      for (const item of captures) {
        imageSetHelpers.upsertPage(manifestPath, {
          id: item.id,
          name: item.name,
          imagePath: item.output,
        });
      }
      annotateInput = manifestPath;
    }

    const session = startAsyncSession({
      inputPath: annotateInput,
      outputPath: outputPath || null,
      mode,
      pageContext: pageContext || (captures.length === 1 ? url : null),
      timeoutSeconds,
      env,
    });
    return {
      ...session,
      source: 'web',
      captures,
      captureOutputPath: captures.length === 1 ? captures[0].output : annotateInput,
    };
  }

  const desktop = resolveDesktopCli({ env });
  if (!desktop.found || !desktop.path) {
    throw new Error(desktop.error || 'Desktop edithere-cli requested but not found.');
  }
  if (window && windowId) {
    throw new Error('`window` and `windowId` are mutually exclusive.');
  }
  if (source === 'interactive' && (window || windowId)) {
    throw new Error('Cannot combine source="interactive" with `window` or `windowId`.');
  }
  if (source === 'window' && !window && !windowId) {
    throw new Error('source="window" requires `window` or `windowId`.');
  }

  if (source === 'interactive' && !annotate && !outputPath) {
    const res = runDesktopCliSync(desktop.path, ['capture'], { timeoutMs: 30000 });
    return {
      ok: res.exitCode === 0 && res.json?.ok === true,
      transport: 'desktop',
      source: 'interactive',
      exitCode: res.exitCode,
      receipt: res.json,
      error: res.json?.error || res.error || (res.stderr || undefined),
    };
  }

  const sourceArgs = source === 'screen'
    ? ['--screen']
    : source === 'window'
      ? (windowId ? ['--window-id', String(windowId)] : ['--window', String(window)])
      : [];

  if (!annotate) {
    const absOutput = outputPath ? path.resolve(outputPath) : makeTempFile('edithere-captures', 'capture', '.png');
    const cliArgs = ['capture', '--output', absOutput, ...sourceArgs];
    const res = runDesktopCliSync(desktop.path, cliArgs, { timeoutMs: 30000 });
    return {
      ok: res.exitCode === 0 && res.json?.ok === true,
      transport: 'desktop',
      source,
      exitCode: res.exitCode,
      outputPath: res.json?.output || absOutput,
      width: res.json?.width,
      height: res.json?.height,
      windowTitle: res.json?.windowTitle,
      appName: res.json?.appName,
      receipt: res.json,
      candidates: res.json?.candidates,
      error: res.json?.error || res.error || (res.stderr || undefined),
    };
  }

  const absOutput = outputPath ? path.resolve(outputPath) : makeTempFile('edithere-feedback', 'capture-annotate', '.json');
  if (fs.existsSync(absOutput)) {
    throw new Error(`Output file already exists: ${absOutput}`);
  }
  const cliArgs = ['capture', '--annotate', '--output', absOutput, '--timeout', String(timeoutSeconds), ...sourceArgs];
  if (pageContext) {
    cliArgs.push('--page-context', pageContext);
  }
  return spawnTrackedDesktopSession(desktop.path, cliArgs, {
    inputPath: null,
    outputPath: absOutput,
    env,
  });
}

export async function shutdownManagedSessions() {
  for (const session of desktopSessions.values()) {
    if (session.watcher) session.watcher.stop();
    if (session.state === 'pending' && session.child && !session.child.killed) {
      session.child.kill();
    }
  }
  desktopSessions.clear();
  await closeWarmBrowser();
}
