/**
 * Shared auto-routing runtime used by both `cli/edithere.mjs` and `cli/unified-mcp.mjs`.
 * Automatically selects Desktop `edithere-cli` when installed, or falls back cleanly
 * to the bundled Web Agent (`web/agent.cjs`) without modifying any existing binaries.
 */
import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { inspectDesktopCli, resolveDesktopCli } from './desktop-locator.mjs';
import { startInboxWatcher } from './inbox-watcher.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WEB_AGENT_CLI = path.join(ROOT, 'web', 'agent.cjs');

const desktopSessions = new Map();

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

  if (transport === 'desktop') {
    const sessionId = crypto.randomBytes(16).toString('hex');
    const cliArgs = buildDesktopAnnotateArgs({ inputPath: absInput, outputPath: absOutput, pageContext, timeoutSeconds });
    const child = spawn(desktop.path, cliArgs, {
      env,
      stdio: ['ignore', 'pipe', 'pipe'],
      windowsHide: true,
    });
    const record = {
      sessionId,
      transport: 'desktop',
      inputPath: absInput,
      outputPath: absOutput,
      startedAt: Date.now(),
      state: 'pending',
      exitCode: null,
      stdout: '',
      stderr: '',
      watcher,
      child,
    };
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', (chunk) => { record.stdout += chunk; });
    child.stderr.on('data', (chunk) => { record.stderr += chunk; });
    child.once('close', (code) => {
      record.exitCode = code;
      if (record.watcher) {
        record.watcher.stop();
        record.watcher = null;
      }
      if (code === 0 && fs.existsSync(absOutput)) {
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
      outputPath: absOutput,
    };
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
    };
  }

  const polled = runWebAgent(['poll', '--session', sessionId], { env });
  return {
    ok: true,
    transport: 'web',
    ...polled,
  };
}

export function shutdownManagedSessions() {
  for (const session of desktopSessions.values()) {
    if (session.watcher) session.watcher.stop();
    if (session.state === 'pending' && session.child && !session.child.killed) {
      session.child.kill();
    }
  }
  desktopSessions.clear();
}
