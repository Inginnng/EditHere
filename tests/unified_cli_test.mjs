import test from 'node:test';
import assert from 'node:assert/strict';
import { execFile, execFileSync, spawn, spawnSync } from 'node:child_process';
import { promisify } from 'node:util';
const execFileAsync = promisify(execFile);
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { clearDesktopProbeCache, getDesktopCapabilities } from '../cli/desktop-locator.mjs';
import { runDoctor } from '../cli/doctor.mjs';
import { runInit } from '../cli/init.mjs';
import { sanitizePageId, startInboxWatcher } from '../cli/inbox-watcher.mjs';
import { manifestToUnifiedProject, unifiedProjectToManifest, validateUnifiedProject } from '../cli/project-format.mjs';
import { closeWarmBrowser, getWarmBrowserState, runFastCapture } from '../cli/router.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const FIXTURE_PNG = path.join(ROOT, 'tests', 'fixtures', 'ocr-small-url.png');
const UNIFIED_CLI = path.join(ROOT, 'cli', 'edithere.mjs');
const UNIFIED_MCP = path.join(ROOT, 'cli', 'unified-mcp.mjs');

test('doctor returns structured runtime, web, playwright, and host readiness checks', async () => {
  const report = await runDoctor({ cwd: ROOT, probePort: false, probeDesktop: false });
  assert.equal(report.ok, true);
  assert.equal(report.node.compatOk, true);
  assert.equal(report.web.ready, true);
  assert.equal(report.summary.activeRoute, 'web');
  assert.ok(Array.isArray(report.hosts));
  assert.ok(report.hosts.some((h) => h.id === 'codex'));
  assert.ok(Array.isArray(report.recommendations));

  const cliRes = spawnSync(process.execPath, [UNIFIED_CLI, 'doctor', '--json'], {
    cwd: ROOT,
    encoding: 'utf8',
    windowsHide: true,
  });
  assert.equal(cliRes.status, 0, cliRes.stderr);
  const parsed = JSON.parse(cliRes.stdout);
  assert.equal(parsed.ok, true);
  assert.equal(typeof parsed.summary.activeRoute, 'string');
});

test('init supports dry-run, installs canonical English skill cleanly, and respects --force', (t) => {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-init-test-'));
  t.after(() => fs.rmSync(tempRoot, { recursive: true, force: true }));

  const dest = path.join(tempRoot, 'codex', 'skills', 'edithere');
  const dry = runInit({ destination: dest, dryRun: true, env: {} });
  assert.equal(dry.ok, true);
  assert.equal(dry.results[0].state, 'would-install');
  assert.equal(fs.existsSync(dest), false);

  const first = runInit({ destination: dest, env: {} });
  assert.equal(first.ok, true);
  assert.equal(first.results[0].state, 'installed');
  assert.equal(fs.existsSync(path.join(dest, 'SKILL.md')), true);
  assert.equal(fs.existsSync(path.join(dest, 'references', 'layout-translation.md')), true);
  const localNote = fs.readFileSync(path.join(dest, 'references', 'local-installation.md'), 'utf8');
  assert.ok(localNote.includes(UNIFIED_MCP));
  assert.deepEqual(first.mcpConfig.mcpServers.edithere, {
    command: process.execPath,
    args: [UNIFIED_MCP],
  });

  const second = runInit({ destination: dest, env: {} });
  assert.equal(second.results[0].state, 'skipped-exists');

  const forced = runInit({ destination: dest, force: true, env: {} });
  assert.equal(forced.results[0].state, 'updated');
  assert.equal(typeof forced.skillVersion, 'string');
  assert.equal(fs.existsSync(path.join(dest, '.edithere-skill.json')), true);
});

test('init --update handles versioned, legacy, path-stale, up-to-date and newer skills without downgrading', (t) => {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-init-version-test-'));
  t.after(() => fs.rmSync(tempRoot, { recursive: true, force: true }));

  const home = path.join(tempRoot, 'home');
  const codexSkill = path.join(home, '.codex', 'skills', 'edithere');
  const claudeSkill = path.join(home, '.claude', 'skills', 'edithere');
  const cursorSkill = path.join(home, '.cursor', 'skills', 'edithere');
  const traeSkill = path.join(home, '.trae', 'skills', 'edithere');
  const agentsDir = path.join(home, '.agents');

  // 1. Codex: up-to-date skillVersion even if appVersion was older (0.14.0 -> 0.14.1 asymmetric bugfix)
  runInit({ destination: codexSkill, env: {} });
  const codexMetaPath = path.join(codexSkill, '.edithere-skill.json');
  const codexMeta = JSON.parse(fs.readFileSync(codexMetaPath, 'utf8'));
  const installedVer = codexMeta.skillVersion;
  codexMeta.appVersion = '0.14.0';
  fs.writeFileSync(codexMetaPath, JSON.stringify(codexMeta, null, 2));

  // 2. Claude: outdated skillVersion
  runInit({ destination: claudeSkill, env: {} });
  const claudeMetaPath = path.join(claudeSkill, '.edithere-skill.json');
  const claudeMeta = JSON.parse(fs.readFileSync(claudeMetaPath, 'utf8'));
  claudeMeta.skillVersion = '0.9.0';
  fs.writeFileSync(claudeMetaPath, JSON.stringify(claudeMeta, null, 2));
  fs.writeFileSync(path.join(claudeSkill, 'obsolete-file.txt'), 'remove on atomic replace');

  // 3. Cursor: legacy (no .edithere-skill.json)
  fs.mkdirSync(cursorSkill, { recursive: true });
  fs.writeFileSync(path.join(cursorSkill, 'SKILL.md'), 'legacy skill');

  // 4. Trae: newer (9.0.0) must never auto-downgrade
  runInit({ destination: traeSkill, env: {} });
  const traeMetaPath = path.join(traeSkill, '.edithere-skill.json');
  const traeMeta = JSON.parse(fs.readFileSync(traeMetaPath, 'utf8'));
  traeMeta.skillVersion = '9.0.0';
  fs.writeFileSync(traeMetaPath, JSON.stringify(traeMeta, null, 2));

  // 5. Agents host present without skill -> --update skips not-installed
  fs.mkdirSync(agentsDir, { recursive: true });

  const updateRes = runInit({ host: 'auto', update: true, homeDir: home, env: {} });
  const stateByHost = Object.fromEntries(updateRes.results.map((r) => [r.host, r]));
  assert.equal(stateByHost.codex.state, 'up-to-date');
  assert.equal(stateByHost.claude.state, 'updated');
  assert.equal(stateByHost.claude.fromVersion, '0.9.0');
  assert.equal(fs.existsSync(path.join(claudeSkill, 'obsolete-file.txt')), false);
  assert.equal(stateByHost.cursor.state, 'updated');
  assert.equal(stateByHost.trae.state, 'skipped-newer');
  assert.equal(JSON.parse(fs.readFileSync(traeMetaPath, 'utf8')).skillVersion, '9.0.0');
  assert.equal(stateByHost.agents.state, 'skipped-not-installed');

  // Same skillVersion with broken recorded MCP/CLI path triggers path-stale refresh on update
  codexMeta.skillVersion = installedVer;
  codexMeta.unifiedMcpPath = path.join(tempRoot, 'missing-old', 'unified-mcp.mjs');
  fs.writeFileSync(codexMetaPath, JSON.stringify(codexMeta, null, 2));
  const staleRes = runInit({ destination: codexSkill, update: true, env: {} });
  assert.equal(staleRes.results[0].state, 'updated');
});

test('doctor reports bundledSkillVersion and per-host skillStatus', async (t) => {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-doctor-skill-test-'));
  t.after(() => fs.rmSync(tempRoot, { recursive: true, force: true }));

  const home = path.join(tempRoot, 'home');
  const codexSkill = path.join(home, '.codex', 'skills', 'edithere');
  fs.mkdirSync(codexSkill, { recursive: true });
  fs.writeFileSync(path.join(codexSkill, 'SKILL.md'), 'old');
  fs.writeFileSync(path.join(codexSkill, '.edithere-skill.json'), JSON.stringify({ skillVersion: '0.1.0' }));

  const report = await runDoctor({ cwd: ROOT, homeDir: home, env: {}, probePort: false, probeDesktop: false });
  assert.match(report.summary.bundledSkillVersion, /^\d+\.\d+\.\d+$/);
  const codex = report.hosts.find((h) => h.id === 'codex');
  assert.equal(codex.skillStatus, 'outdated');
  assert.equal(codex.skillVersion, '0.1.0');
  assert.ok(report.recommendations.some((r) => r.includes('init --update')));
});

test('inbox-watcher ingests dropped PNGs, handles CJK filenames safely, and deduplicates', (t) => {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-inbox-test-'));
  t.after(() => fs.rmSync(tempRoot, { recursive: true, force: true }));

  const inboxDir = path.join(tempRoot, 'inbox');
  const manifestPath = path.join(tempRoot, 'results', 'project.json');
  const events = [];
  const watcher = startInboxWatcher({
    inboxDir,
    manifestPath,
    manifestName: 'Game HUD',
    pollIntervalMs: 0,
    onAdded: (ev) => events.push(ev),
  });
  t.after(() => watcher.stop());

  assert.match(sanitizePageId('inventory.png'), /^inventory$/);
  assert.match(sanitizePageId('物品栏.png'), /^page-[a-f0-9]{8}$/);

  fs.copyFileSync(FIXTURE_PNG, path.join(inboxDir, '物品栏.png'));
  fs.writeFileSync(path.join(inboxDir, 'ignore.txt'), 'not an image');

  const added1 = watcher.scanOnce({ forceImmediate: true });
  assert.equal(added1.length, 1);
  assert.equal(added1[0].name, '物品栏');
  assert.equal(watcher.seenCount(), 1);

  // Second scan with unchanged bytes produces no duplicate upsert
  const added2 = watcher.scanOnce({ forceImmediate: true });
  assert.equal(added2.length, 0);

  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  assert.equal(manifest.pages.length, 1);
  assert.equal(manifest.pages[0].name, '物品栏');
  assert.match(manifest.pages[0].id, /^page-[a-f0-9]{8}$/);
});

test('project-format converts manifests <-> edithere-project-2 and supports legacy web projects', (t) => {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-format-test-'));
  t.after(() => fs.rmSync(tempRoot, { recursive: true, force: true }));

  const manifestPath = path.join(tempRoot, 'src-gallery', 'project.json');
  const imageSetCli = path.join(ROOT, 'scripts', 'image-set.mjs');
  execFileSync(process.execPath, [imageSetCli, 'create', manifestPath, '--name', 'HUD Set'], { windowsHide: true });
  execFileSync(process.execPath, [imageSetCli, 'upsert', manifestPath, '--id', 'inventory', '--name', 'Inventory', '--image', FIXTURE_PNG], { windowsHide: true });

  const unifiedPath = path.join(tempRoot, 'portable.edithere.json');
  const conv1 = JSON.parse(execFileSync(process.execPath, [UNIFIED_CLI, 'convert', 'to-unified', manifestPath, '--output', unifiedPath], {
    encoding: 'utf8',
    windowsHide: true,
  }));
  assert.equal(conv1.ok, true);
  assert.equal(conv1.schemaVersion, 'edithere-project-2');

  const unified = JSON.parse(fs.readFileSync(unifiedPath, 'utf8'));
  validateUnifiedProject(unified);
  assert.equal(unified.kind, 'single');
  assert.equal(unified.pages[0].id, 'inventory');

  // Corrupt SHA-256 must fail validation
  const bad = structuredClone(unified);
  bad.pages[0].image.sha256 = '0'.repeat(64);
  assert.throws(() => validateUnifiedProject(bad), /SHA-256 mismatch/);

  // Round-trip back to manifest + PNG files
  const extractedDir = path.join(tempRoot, 'extracted');
  const conv2 = JSON.parse(execFileSync(process.execPath, [UNIFIED_CLI, 'convert', 'to-manifest', unifiedPath, '--out-dir', extractedDir], {
    encoding: 'utf8',
    windowsHide: true,
  }));
  assert.equal(conv2.ok, true);
  assert.equal(conv2.pageCount, 1);
  assert.equal(fs.existsSync(path.join(extractedDir, 'images', 'inventory.png')), true);

  // Also test legacy edithere-web-project-1 extraction
  const legacyPath = path.join(tempRoot, 'legacy-web.json');
  fs.writeFileSync(legacyPath, JSON.stringify({
    schemaVersion: 'edithere-web-project-1',
    name: 'Legacy Gallery',
    pages: [{ id: 'home', name: 'Home', image: unified.pages[0].image.dataUrl }],
  }), 'utf8');
  const legacyRes = unifiedProjectToManifest(legacyPath, path.join(tempRoot, 'legacy-extracted'));
  assert.equal(legacyRes.ok, true);
  assert.equal(fs.existsSync(path.join(tempRoot, 'legacy-extracted', 'images', 'home.png')), true);
});

test('unified CLI status and unified MCP stdio server handle initialize, tools/list, and tools/call', async () => {
  const statusOut = JSON.parse(execFileSync(process.execPath, [UNIFIED_CLI, 'status', '--mode', 'web'], {
    cwd: ROOT,
    encoding: 'utf8',
    windowsHide: true,
  }));
  assert.equal(statusOut.ok, true);
  assert.equal(statusOut.transport, 'web');

  const child = spawn(process.execPath, [UNIFIED_MCP], {
    cwd: ROOT,
    stdio: ['pipe', 'pipe', 'pipe'],
    windowsHide: true,
  });

  const pending = new Map();
  let buffer = '';
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', (chunk) => {
    buffer += chunk;
    let idx;
    while ((idx = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, idx).trim();
      buffer = buffer.slice(idx + 1);
      if (!line) continue;
      const msg = JSON.parse(line);
      if (msg.id !== undefined && pending.has(msg.id)) {
        pending.get(msg.id)(msg);
        pending.delete(msg.id);
      }
    }
  });

  const send = (obj) => child.stdin.write(JSON.stringify(obj) + '\n');
  const request = (id, method, params) => new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`Timeout waiting for ${method}`)), 8000);
    pending.set(id, (msg) => {
      clearTimeout(timer);
      resolve(msg);
    });
    send({ jsonrpc: '2.0', id, method, params });
  });

  try {
    const init = await request(1, 'initialize', { protocolVersion: '2025-06-18', capabilities: {} });
    assert.equal(init.result.serverInfo.name, 'edithere-unified');
    send({ jsonrpc: '2.0', method: 'notifications/initialized' });

    const list = await request(2, 'tools/list', {});
    const names = list.result.tools.map((t) => t.name);
    for (const expected of [
      'edithere_doctor',
      'edithere_status',
      'edithere_windows',
      'edithere_capture',
      'edithere_annotate_start',
      'edithere_annotate_poll',
      'edithere_convert_project',
    ]) {
      assert.ok(names.includes(expected), `Missing unified MCP tool ${expected}`);
    }

    const docCall = await request(3, 'tools/call', { name: 'edithere_doctor', arguments: {} });
    assert.equal(docCall.result.isError, false);
    const docPayload = JSON.parse(docCall.result.content[0].text);
    assert.equal(docPayload.ok, true);

    const statusCall = await request(4, 'tools/call', { name: 'edithere_status', arguments: { mode: 'web' } });
    assert.equal(statusCall.result.isError, false);
    assert.equal(JSON.parse(statusCall.result.content[0].text).transport, 'web');
  } finally {
    child.stdin.end();
    child.kill();
  }
});

test('desktop-locator caches --version and --help probes by path/mtime and clears cleanly', () => {
  clearDesktopProbeCache();
  const env = { ...process.env, EDITHERE_CLI: process.execPath };
  const first = getDesktopCapabilities({ env });
  assert.equal(first.found, true);
  assert.equal(first.cacheHit, false);
  assert.ok(typeof first.version === 'string' && /^\d+\.\d+\.\d+$/.test(first.version));

  const second = getDesktopCapabilities({ env });
  assert.equal(second.found, true);
  assert.equal(second.cacheHit, true);
  assert.equal(second.version, first.version);

  clearDesktopProbeCache();
  const third = getDesktopCapabilities({ env });
  assert.equal(third.cacheHit, false);
});

test('capture-web and warm browser pool settle fast on pages with open SSE streams and evict on idle', async (t) => {
  const localPw = 'e:\\Project\\EditHere总包\\EditHere宣传\\动画\\node_modules\\playwright-core\\index.js';
  const prevPwEnv = process.env.EDITHERE_PLAYWRIGHT_MODULE;
  const prevBrowserEnv = process.env.EDITHERE_BROWSER_PATH;
  const prevIdleEnv = process.env.EDITHERE_BROWSER_IDLE_MS;

  if (!prevPwEnv && fs.existsSync(localPw)) {
    process.env.EDITHERE_PLAYWRIGHT_MODULE = localPw;
  }

  // Verify Playwright availability before running live headless Chrome SSE assertions.
  let canLaunchWeb = false;
  try {
    const { launchCaptureBrowser } = await import('../scripts/capture-web.mjs');
    const b = await launchCaptureBrowser({ headless: true });
    await b.close();
    canLaunchWeb = true;
  } catch (err) {
    if (err?.code === 'playwright_not_found' || /playwright/i.test(String(err?.message))) {
      t.skip(`Skipping live web capture benchmark: ${err.message}`);
      return;
    }
    throw err;
  }
  assert.equal(canLaunchWeb, true);

  const sseConnections = new Set();
  const server = http.createServer((req, res) => {
    const urlPath = (req.url || '/').split('?')[0];
    if (urlPath === '/sse') {
      res.writeHead(200, {
        'Content-Type': 'text/event-stream',
        'Cache-Control': 'no-cache',
        Connection: 'keep-alive',
      });
      res.write(': connected\n\n');
      const iv = setInterval(() => res.write('data: {"tick":1}\n\n'), 150);
      sseConnections.add(res);
      req.on('close', () => {
        clearInterval(iv);
        sseConnections.delete(res);
      });
      return;
    }
    if (urlPath === '/api/data') {
      setTimeout(() => {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      }, 40);
      return;
    }
    const label = urlPath.replace('/', '') || 'home';
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html><html><body style="margin:0;background:#101520;color:#eef2ff;font:24px sans-serif">
      <main id="ready" style="padding:32px">SSE Page: ${label}</main>
      <script>
        fetch('/api/data').then(() => {
          const es = new EventSource('/sse');
          window.__es = es;
        });
      </script>
    </body></html>`);
  });

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  const baseUrl = `http://127.0.0.1:${port}`;
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-fast-web-'));

  t.after(async () => {
    await closeWarmBrowser();
    for (const r of sseConnections) r.destroy();
    sseConnections.clear();
    server.close();
    fs.rmSync(outDir, { recursive: true, force: true });
    if (prevPwEnv === undefined) delete process.env.EDITHERE_PLAYWRIGHT_MODULE;
    else process.env.EDITHERE_PLAYWRIGHT_MODULE = prevPwEnv;
    if (prevBrowserEnv === undefined) delete process.env.EDITHERE_BROWSER_PATH;
    else process.env.EDITHERE_BROWSER_PATH = prevBrowserEnv;
    if (prevIdleEnv === undefined) delete process.env.EDITHERE_BROWSER_IDLE_MS;
    else process.env.EDITHERE_BROWSER_IDLE_MS = prevIdleEnv;
  });

  const startedAt = Date.now();
  const batchRes = await runFastCapture({
    source: 'web',
    urls: [`${baseUrl}/one`, `${baseUrl}/two`, `${baseUrl}/three`],
    viewport: '1440x900',
    dpr: 2,
    concurrency: 3,
    waitMs: 0,
    env: { ...process.env, EDITHERE_BROWSER_IDLE_MS: '350' },
  });
  const elapsedMs = Date.now() - startedAt;

  assert.equal(batchRes.ok, true);
  assert.equal(batchRes.count, 3);
  assert.ok(elapsedMs < 8000, `Expected 3 concurrent SSE pages well under 3x5s stall, took ${elapsedMs}ms`);
  const { readPngSize } = await import('../scripts/capture-web.mjs');
  for (const item of batchRes.captures) {
    assert.equal(fs.existsSync(item.output), true);
    const sz = readPngSize(item.output);
    assert.equal(sz.width, 2880);
    assert.equal(sz.height, 1800);
  }
  // Pool stays briefly warm immediately after completion, then auto-closes after idle TTL.
  assert.equal(getWarmBrowserState().connected, true);
  await new Promise((r) => setTimeout(r, 600));
  assert.equal(getWarmBrowserState().connected, false);

  // Verify standalone capture-web.mjs CLI with --wait-selector & --wait-ms 0 on open SSE page
  // Use async execFile so the in-process HTTP server event loop continues serving /sse.
  const singleOut = path.join(outDir, 'standalone.png');
  const cliStarted = Date.now();
  const { stdout: cliStdout } = await execFileAsync(
    process.execPath,
    [
      path.join(ROOT, 'scripts', 'capture-web.mjs'),
      '--url', `${baseUrl}/single`,
      '--output', singleOut,
      '--wait-selector', '#ready',
      '--wait-ms', '0',
    ],
    { cwd: ROOT, env: process.env, encoding: 'utf8', timeout: 15000, windowsHide: true }
  );
  const cliElapsed = Date.now() - cliStarted;
  const cliJson = JSON.parse(cliStdout.trim());
  assert.equal(cliJson.ok, true);
  assert.ok(cliElapsed < 8000, `Standalone SSE capture took ${cliElapsed}ms`);
  assert.equal(fs.existsSync(singleOut), true);
});
