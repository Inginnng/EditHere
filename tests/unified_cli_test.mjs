import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runDoctor } from '../cli/doctor.mjs';
import { runInit } from '../cli/init.mjs';
import { sanitizePageId, startInboxWatcher } from '../cli/inbox-watcher.mjs';
import { manifestToUnifiedProject, unifiedProjectToManifest, validateUnifiedProject } from '../cli/project-format.mjs';

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
    for (const expected of ['edithere_doctor', 'edithere_status', 'edithere_annotate_start', 'edithere_annotate_poll', 'edithere_convert_project']) {
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
