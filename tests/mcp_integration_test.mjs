import assert from 'node:assert/strict';
import { execFileSync, spawn } from 'node:child_process';
import crypto, { once } from 'node:events';
import nodeCrypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { writeManifest, upsertPage } = require('../web/image-set.cjs');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PRODUCT_VERSION = fs.readFileSync(path.join(ROOT, 'CMakeLists.txt'), 'utf8')
  .match(/project\(\s*EditHere\s+VERSION\s+(\d+\.\d+\.\d+)\b/)?.[1];
assert.ok(PRODUCT_VERSION, 'CMake must declare the EditHere product version');
const FIXTURE_PNG_PATH = path.join(ROOT, 'tests', 'fixtures', 'ocr-small-url.png');
const FIXTURE_PNG_BYTES = fs.readFileSync(FIXTURE_PNG_PATH);
const FIXTURE_PNG_SHA256 = nodeCrypto.createHash('sha256').update(FIXTURE_PNG_BYTES).digest('hex');

function findRealDesktopCli() {
  const candidates = [
    process.env.EDITHERE_CLI,
    path.join(ROOT, 'build-b2', process.platform === 'win32' ? 'edithere-cli.exe' : 'edithere-cli'),
  ].filter(Boolean);
  for (const candidate of candidates) {
    const resolved = path.resolve(candidate);
    if (fs.existsSync(resolved)) return resolved;
  }
  return null;
}

const REAL_CLI = findRealDesktopCli();

function makeTempDir(prefix = 'edithere-mcp-e2e-') {
  return fs.mkdtempSync(path.join(os.tmpdir(), prefix));
}

function createJsonRpcClient(command, args, { cwd = ROOT, env = process.env, timeoutMs = 8000 } = {}) {
  const child = spawn(command, args, {
    cwd,
    env,
    stdio: ['pipe', 'pipe', 'pipe'],
  });

  let stdoutBuffer = '';
  let stderrBuffer = '';
  let nextId = 0;
  const pending = new Map();

  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');

  child.stdout.on('data', (chunk) => {
    stdoutBuffer += chunk;
    let newline;
    while ((newline = stdoutBuffer.indexOf('\n')) >= 0) {
      const line = stdoutBuffer.slice(0, newline).trim();
      stdoutBuffer = stdoutBuffer.slice(newline + 1);
      if (!line) continue;
      let parsed;
      try {
        parsed = JSON.parse(line);
      } catch {
        continue;
      }
      const key = parsed.id ?? '__null__';
      const entry = pending.get(key);
      if (entry) {
        clearTimeout(entry.timer);
        pending.delete(key);
        entry.resolve(parsed);
      }
    }
  });

  child.stderr.on('data', (chunk) => {
    stderrBuffer += chunk;
  });

  child.on('exit', (code) => {
    for (const [key, entry] of pending.entries()) {
      clearTimeout(entry.timer);
      pending.delete(key);
      entry.reject(new Error(`MCP server exited early (code=${code}): ${stderrBuffer}`));
    }
  });

  function sendRaw(line) {
    child.stdin.write(line + '\n');
  }

  function request(method, params, { nullId = false } = {}) {
    const id = nullId ? null : ++nextId;
    const key = id ?? '__null__';
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(key);
        reject(new Error(`JSON-RPC request "${method}" timed out after ${timeoutMs}ms. stderr=${stderrBuffer}`));
      }, timeoutMs);
      pending.set(key, { resolve, reject, timer });
      child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, ...(params !== undefined ? { params } : {}) }) + '\n');
    });
  }

  function requestExpectErrorResponseForMalformedJson(rawText) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete('__null__');
        reject(new Error(`Timed out waiting for -32700 parse error response. stderr=${stderrBuffer}`));
      }, timeoutMs);
      pending.set('__null__', { resolve, reject, timer });
      sendRaw(rawText);
    });
  }

  function notify(method, params) {
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method, ...(params !== undefined ? { params } : {}) }) + '\n');
  }

  async function close() {
    for (const entry of pending.values()) clearTimeout(entry.timer);
    pending.clear();
    try {
      child.stdin.end();
    } catch {
      // Ignore already-closed stdin.
    }
    const closed = once(child, 'close');
    const forceTimer = setTimeout(() => {
      if (child.exitCode === null) child.kill('SIGKILL');
    }, 1000);
    await closed;
    clearTimeout(forceTimer);
  }

  return {
    child,
    get stderr() {
      return stderrBuffer;
    },
    request,
    requestExpectErrorResponseForMalformedJson,
    notify,
    close,
  };
}

function buildValidV3Project(dir, fileName = 'capture.png') {
  const imagePath = path.join(dir, fileName);
  fs.writeFileSync(imagePath, FIXTURE_PNG_BYTES);
  const now = '2026-11-24T12:00:00.000Z';
  const project = {
    schemaVersion: '3.0.0',
    tool: 'EditHere',
    exportedAt: now,
    annotationSpace: 'result',
    capture: {
      id: 'cap-e2e-1',
      createdAt: now,
      source: 'file',
      title: 'Test Screen',
      imageFile: fileName,
      width: 380,
      height: 19,
      coordinateSpace: 'image-pixels',
      origin: 'top-left',
      rectangleConvention: 'top-left-inclusive-bottom-right-exclusive',
      screenBounds: null,
      sha256: FIXTURE_PNG_SHA256,
      pngBase64: FIXTURE_PNG_BYTES.toString('base64'),
      fileName,
      windowTitle: 'EditHere Test Fixture',
      appName: 'EditHere',
      page: 'MCP集成测试页',
      pageSource: 'agent',
    },
    annotations: [
      {
        id: 'ann-1',
        number: 1,
        kind: 'rectangle',
        point: null,
        rectangle: { x1: 8, y1: 2, x2: 120, y2: 17 },
        comment: '统一边框线宽与圆角规格',
        target: {
          source: 'manual',
          label: 'Toolbar Box',
          controlType: null,
          automationId: null,
          method: 'manual',
          originalScreenBounds: null,
          clipped: false,
        },
        createdAt: now,
        updatedAt: now,
      },
    ],
    layout: null,
  };
  const projectPath = path.join(dir, 'sample.edithere');
  fs.writeFileSync(projectPath, JSON.stringify(project, null, 2), 'utf8');
  return { projectPath, imagePath };
}

test('Suite 1: Unified MCP server (cli/unified-mcp.mjs) full stdio JSON-RPC lifecycle', async () => {
  const tmpDir = makeTempDir('edithere-unified-mcp-');
  const env = {
    ...process.env,
    ...(REAL_CLI ? { EDITHERE_CLI: REAL_CLI } : {}),
  };
  const client = createJsonRpcClient(process.execPath, [path.join(ROOT, 'cli', 'unified-mcp.mjs')], { env });

  try {
    const init = await client.request('initialize', {
      protocolVersion: '2025-06-18',
      capabilities: {},
      clientInfo: { name: 'mcp-integration-test', version: PRODUCT_VERSION },
    });
    assert.equal(init.result.protocolVersion, '2025-06-18');
    assert.equal(init.result.serverInfo.name, 'edithere-unified');
    assert.equal(init.result.serverInfo.version, PRODUCT_VERSION);

    client.notify('notifications/initialized', {});
    const ping = await client.request('ping');
    assert.deepEqual(ping.result, {});

    const listed = await client.request('tools/list');
    const names = listed.result.tools.map((tool) => tool.name);
    assert.deepEqual(names, [
      'edithere_doctor',
      'edithere_status',
      'edithere_windows',
      'edithere_capture',
      'edithere_annotate_start',
      'edithere_annotate_poll',
      'edithere_convert_project',
    ]);

    const parseError = await client.requestExpectErrorResponseForMalformedJson('{not-json');
    assert.equal(parseError.error.code, -32700);

    const unknownMethod = await client.request('nonexistent/method', {});
    assert.equal(unknownMethod.error.code, -32601);

    const unknownTool = await client.request('tools/call', { name: 'edithere_missing_tool', arguments: {} });
    assert.equal(unknownTool.error.code, -32602);

    const doctorResp = await client.request('tools/call', { name: 'edithere_doctor', arguments: {} });
    assert.equal(doctorResp.result.isError, false);
    const doctor = JSON.parse(doctorResp.result.content[0].text);
    assert.equal(doctor.ok, true);
    assert.equal(doctor.web.ready, true);
    assert.equal(doctor.web.files.mcpBundle, true);
    if (REAL_CLI) {
      assert.equal(doctor.desktop.found, true);
      assert.equal(doctor.desktop.version, PRODUCT_VERSION);
    }

    const webStatus = await client.request('tools/call', {
      name: 'edithere_status',
      arguments: { mode: 'web' },
    });
    assert.equal(webStatus.result.isError, false);
    assert.equal(JSON.parse(webStatus.result.content[0].text).transport, 'web');

    if (REAL_CLI) {
      const desktopStatus = await client.request('tools/call', {
        name: 'edithere_status',
        arguments: { mode: 'desktop' },
      });
      assert.equal(desktopStatus.result.isError, false);
      const desktopJson = JSON.parse(desktopStatus.result.content[0].text);
      assert.equal(desktopJson.ok, true);
      assert.equal(desktopJson.transport, 'desktop');
      assert.equal(typeof desktopJson.running, 'boolean');
      if (desktopJson.running) assert.match(desktopJson.version, /^\d+\.\d+\.\d+$/);
      else assert.equal(desktopJson.version, PRODUCT_VERSION);

      const autoStatus = await client.request('tools/call', {
        name: 'edithere_status',
        arguments: { mode: 'auto' },
      });
      assert.equal(JSON.parse(autoStatus.result.content[0].text).transport, 'desktop');
    }

    // Roundtrip image-set manifest -> unified project -> image-set manifest
    const setDir = path.join(tmpDir, 'gallery');
    const pagesDir = path.join(setDir, 'pages');
    fs.mkdirSync(pagesDir, { recursive: true });
    const imgA = path.join(pagesDir, 'page-a.png');
    const imgB = path.join(pagesDir, 'page-b.png');
    fs.writeFileSync(imgA, FIXTURE_PNG_BYTES);
    fs.writeFileSync(imgB, FIXTURE_PNG_BYTES);
    const manifestPath = path.join(setDir, 'project.json');
    writeManifest(manifestPath, { schemaVersion: 'edithere-image-set-1', name: 'Unified Roundtrip', pages: [] }, { create: true });
    upsertPage(manifestPath, { id: 'home', name: 'Home', imagePath: imgA });
    upsertPage(manifestPath, { id: 'detail', name: 'Detail', imagePath: imgB });

    const unifiedPath = path.join(tmpDir, 'unified.edithere-project.json');
    const toUnified = await client.request('tools/call', {
      name: 'edithere_convert_project',
      arguments: { direction: 'to-unified', inputPath: manifestPath, outputPath: unifiedPath },
    });
    assert.equal(toUnified.result.isError, false, toUnified.result.content[0].text);
    const toUnifiedJson = JSON.parse(toUnified.result.content[0].text);
    assert.equal(toUnifiedJson.schemaVersion, 'edithere-project-2');
    assert.equal(toUnifiedJson.pageCount, 2);

    const restoredDir = path.join(tmpDir, 'restored');
    const toManifest = await client.request('tools/call', {
      name: 'edithere_convert_project',
      arguments: { direction: 'to-manifest', inputPath: unifiedPath, outputPath: restoredDir },
    });
    assert.equal(toManifest.result.isError, false, toManifest.result.content[0].text);
    const restored = JSON.parse(fs.readFileSync(path.join(restoredDir, 'project.json'), 'utf8'));
    assert.equal(restored.schemaVersion, 'edithere-image-set-1');
    assert.equal(restored.pages.length, 2);

    const pollMissing = await client.request('tools/call', {
      name: 'edithere_annotate_poll',
      arguments: { sessionId: '0123456789abcdef0123456789abcdef' },
    });
    assert.equal(pollMissing.result.isError, true);
    assert.ok(pollMissing.result.content[0].text.length > 0);
    // Server must remain healthy after returning a tool error.
    assert.deepEqual((await client.request('ping')).result, {});
  } finally {
    await client.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test('Suite 2: Connector MCP server (connector/mcp-server.mjs) against real edithere-cli.exe', async (t) => {
  if (!REAL_CLI) {
    t.skip('Compiled build-b2/edithere-cli.exe not found yet');
    return;
  }

  const tmpDir = makeTempDir('edithere-connector-e2e-');
  const client = createJsonRpcClient(process.execPath, [path.join(ROOT, 'connector', 'mcp-server.mjs')], {
    cwd: ROOT,
    env: { ...process.env, EDITHERE_CLI: REAL_CLI },
  });

  try {
    const init = await client.request('initialize', {
      protocolVersion: '2025-06-18',
      capabilities: {},
      clientInfo: { name: 'mcp-integration-test', version: PRODUCT_VERSION },
    });
    assert.ok(init.result.serverInfo.name);
    assert.equal(init.result.serverInfo.version, '0.2.2');
    client.notify('notifications/initialized', {});

    const toolsResp = await client.request('tools/list');
    const toolNames = toolsResp.result.tools.map((tool) => tool.name);
    assert.equal(toolNames.length, 8);
    assert.deepEqual(toolNames, [
      'edithere_status',
      'edithere_open',
      'edithere_capture',
      'edithere_windows',
      'edithere_annotate',
      'edithere_annotate_start',
      'edithere_annotate_poll',
      'edithere_export',
    ]);
    for (const tool of toolsResp.result.tools) {
      assert.equal(tool.description.includes('改这里'), false, `Tool ${tool.name} description leaked old brand suffix`);
    }

    const statusResp = await client.request('tools/call', { name: 'edithere_status', arguments: {} });
    assert.equal(statusResp.result.isError, false);
    const status = JSON.parse(statusResp.result.content[0].text);
    assert.equal(status.ok, true);
    assert.equal(typeof status.running, 'boolean');
    if (status.running) assert.match(status.version, /^\d+\.\d+\.\d+$/);
    else assert.equal(status.version, PRODUCT_VERSION);
    assert.equal(path.basename(status.connector.cli), path.basename(REAL_CLI));

    // Headless export from a schema-valid v3 .edithere project (file + preview + includeImage)
    const { projectPath } = buildValidV3Project(tmpDir);
    const outFileDir = path.join(tmpDir, 'export-file');
    const exportFileResp = await client.request('tools/call', {
      name: 'edithere_export',
      arguments: {
        imagePath: projectPath,
        outputDir: outFileDir,
        imageMode: 'file',
        preview: true,
        includeImage: true,
      },
    });
    assert.equal(exportFileResp.result.isError, false, exportFileResp.result.content[0].text);
    const fileImages = exportFileResp.result.content.filter((item) => item.type === 'image');
    assert.equal(fileImages.length, 1);
    const feedbackFiles = fs.readdirSync(outFileDir).filter((name) => name.endsWith('.json'));
    assert.equal(feedbackFiles.length, 1);
    const exportedJson = JSON.parse(fs.readFileSync(path.join(outFileDir, feedbackFiles[0]), 'utf8'));
    assert.equal(exportedJson.schemaVersion, 'feedback-minimal-4');
    assert.equal(exportedJson.tool.name, 'EditHere');
    assert.equal(exportedJson.tool.version, PRODUCT_VERSION);
    assert.equal(exportedJson.capture.page, 'MCP集成测试页');

    // Embedded imageMode export
    const outEmbeddedDir = path.join(tmpDir, 'export-embedded');
    const exportEmbeddedResp = await client.request('tools/call', {
      name: 'edithere_export',
      arguments: {
        imagePath: projectPath,
        outputDir: outEmbeddedDir,
        imageMode: 'embedded',
        includeImage: true,
      },
    });
    assert.equal(exportEmbeddedResp.result.isError, false, exportEmbeddedResp.result.content[0].text);
    const embeddedJsonFile = fs.readdirSync(outEmbeddedDir).find((name) => name.endsWith('.json'));
    const embeddedJson = JSON.parse(fs.readFileSync(path.join(outEmbeddedDir, embeddedJsonFile), 'utf8'));
    assert.equal(embeddedJson.image.mode, 'embedded');
    assert.match(embeddedJson.image.dataUrl, /^data:image\/png;base64,/);

    // Input validation checks before spawning CLI
    const missingPathErr = await client.request('tools/call', { name: 'edithere_open', arguments: {} });
    assert.equal(missingPathErr.result.isError, true);
    const blankContextErr = await client.request('tools/call', {
      name: 'edithere_open',
      arguments: { imagePath: path.join(tmpDir, 'capture.png'), pageContext: '   ' },
    });
    assert.equal(blankContextErr.error.code, -32602);
    const conflictingCaptureErr = await client.request('tools/call', {
      name: 'edithere_capture',
      arguments: { source: 'window', window: 'App', windowId: '12345' },
    });
    assert.equal(conflictingCaptureErr.error.code, -32602);
  } finally {
    await client.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test('Suite 3: Web MCP server (web/mcp/server.bundle.cjs) resource, draft and feedback lifecycle', async () => {
  const tmpDir = makeTempDir('edithere-web-mcp-');
  const dataDir = path.join(tmpDir, 'web-sessions');
  const imgPath = path.join(tmpDir, 'screen.png');
  const outputPath = path.join(tmpDir, 'out', 'feedback.json');
  fs.writeFileSync(imgPath, FIXTURE_PNG_BYTES);

  const client = createJsonRpcClient(process.execPath, [path.join(ROOT, 'web', 'mcp', 'server.bundle.cjs')], {
    cwd: ROOT,
    env: { ...process.env, EDITHERE_WEB_DATA_DIR: dataDir },
  });

  try {
    const init = await client.request('initialize', {
      protocolVersion: '2025-06-18',
      capabilities: {
        extensions: {
          'io.modelcontextprotocol/ui': { mimeTypes: ['text/html;profile=mcp-app'] },
        },
      },
      clientInfo: { name: 'mcp-integration-test', version: PRODUCT_VERSION },
    });
    assert.equal(init.result.serverInfo.name, 'edithere-web');
    client.notify('notifications/initialized', {});

    const toolsResp = await client.request('tools/list');
    const toolNames = toolsResp.result.tools.map((tool) => tool.name);
    assert.ok(toolNames.includes('edithere_web_open'));
    assert.ok(toolNames.includes('edithere_web_draft'));
    assert.ok(toolNames.includes('edithere_web_submit'));
    assert.ok(toolNames.includes('edithere_web_read'));

    const resList = await client.request('resources/list', {});
    assert.ok(resList.result.resources.some((r) => r.uri === 'ui://edithere/annotation.html'));
    const resRead = await client.request('resources/read', { uri: 'ui://edithere/annotation.html' });
    const htmlText = resRead.result.contents[0].text;
    assert.ok(htmlText.includes('<title>EditHere</title>'), 'Web MCP UI title must be EditHere');

    const opened = await client.request('tools/call', {
      name: 'edithere_web_open',
      arguments: {
        imagePath: imgPath,
        outputPath,
        name: '集成测试页',
        pageContext: '主工作台',
      },
    });
    assert.equal(opened.result.isError, undefined);
    const meta = opened.result._meta.edithere;
    assert.match(meta.sessionId, /^[a-f0-9]{32}$/);
    assert.match(meta.token, /^[a-f0-9]{64}$/);
    assert.equal(meta.capture.page, '主工作台');

    const drafted = await client.request('tools/call', {
      name: 'edithere_web_draft',
      arguments: {
        sessionId: meta.sessionId,
        token: meta.token,
        project: {
          schemaVersion: 'edithere-web-project-1',
          tool: 'EditHere Web',
          savedAt: '2026-11-24T12:00:00.000Z',
          kind: 'image',
          name: '集成测试页',
          width: 380,
          height: 19,
          document: {
            id: 'page-1',
            timestampUs: null,
            notes: [
              {
                id: 'n-1',
                source: { x1: 8, y1: 2, x2: 120, y2: 17 },
                text: '统一间距',
              },
            ],
            layout: null,
          },
        },
      },
    });
    assert.equal(drafted.result.isError, undefined);

    const submitted = await client.request('tools/call', {
      name: 'edithere_web_submit',
      arguments: {
        sessionId: meta.sessionId,
        token: meta.token,
        feedback: {
          annotationSpace: 'result',
          objects: [
            {
              source: { x1: 8, y1: 2, x2: 120, y2: 17 },
              movements: [],
              annotations: ['统一间距'],
            },
          ],
        },
      },
    });
    assert.equal(submitted.result.isError, undefined);
    assert.equal(submitted.result.structuredContent.state, 'completed');
    assert.equal(fs.existsSync(outputPath), true);
    const savedFeedback = JSON.parse(fs.readFileSync(outputPath, 'utf8'));
    assert.equal(savedFeedback.schemaVersion, 'feedback-minimal-4');
    assert.equal(savedFeedback.capture.page, '主工作台');

    const readBack = await client.request('tools/call', {
      name: 'edithere_web_read',
      arguments: { sessionId: meta.sessionId },
    });
    assert.equal(readBack.result.structuredContent.state, 'completed');

    const lateCancel = await client.request('tools/call', {
      name: 'edithere_web_cancel',
      arguments: { sessionId: meta.sessionId, token: meta.token },
    });
    assert.equal(lateCancel.result.isError, true);
  } finally {
    await client.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test('Suite 4: Unified CLI init and B2 doctor unified-mcp path resolution', () => {
  const tmpDir = makeTempDir('edithere-init-e2e-');
  try {
    const skillDest = path.join(tmpDir, 'edithere-skill');
    const stdout = execFileSync(
      process.execPath,
      [path.join(ROOT, 'cli', 'edithere.mjs'), 'init', '--destination', skillDest, '--force', '--json'],
      {
        cwd: ROOT,
        env: { ...process.env, ...(REAL_CLI ? { EDITHERE_CLI: REAL_CLI } : {}) },
        encoding: 'utf8',
      }
    );
    const initResult = JSON.parse(stdout);
    assert.equal(initResult.ok, true);
    assert.equal(fs.existsSync(path.join(skillDest, 'SKILL.md')), true);
    const installNote = fs.readFileSync(path.join(skillDest, 'references', 'local-installation.md'), 'utf8');
    assert.ok(installNote.includes('unified-mcp.mjs'));
    if (REAL_CLI) {
      assert.ok(installNote.includes(REAL_CLI));
      // Mirror B2Window::buildDoctorSnapshotJson() path check cleanly from build-b2/
      const appDir = path.dirname(REAL_CLI);
      const packagedMcp = path.resolve(appDir, 'cli/unified-mcp.mjs');
      const devMcp = path.resolve(appDir, '../cli/unified-mcp.mjs');
      assert.equal(fs.existsSync(packagedMcp) || fs.existsSync(devMcp), true, 'Expected unified-mcp.mjs reachable from CLI directory');
    }

  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test('HTML and connector metadata use the EditHere brand', () => {
  const connectorMeta = JSON.parse(fs.readFileSync(path.join(ROOT, 'connector', 'connector-meta.json'), 'utf8'));
  assert.equal(connectorMeta.name, 'EditHere');
  assert.equal(connectorMeta.name_zh, 'EditHere');
  assert.equal(connectorMeta.name_en, 'EditHere');

  const oldBrand = [];
  const webBundles = ['web/EditHere.html', 'web/EditHere.dsh.html', 'web/EditHere.mcp.html'];
  const htmlFiles = ['frontend/index.html', ...webBundles];
  // Native builds can run without the optional B2 frontend bundle.
  const builtFrontend = 'frontend/dist/index.html';
  if (fs.existsSync(path.join(ROOT, builtFrontend))) htmlFiles.push(builtFrontend);
  for (const relative of htmlFiles) {
    const html = fs.readFileSync(path.join(ROOT, relative), 'utf8');
    assert.ok(html.includes('<title>EditHere</title>'), `${relative} must have <title>EditHere</title>`);
    if (html.includes('改这里')) oldBrand.push(relative);
    if (webBundles.includes(relative)) {
      assert.match(html, /class="brand surface" aria-label="EditHere"/, `${relative} must use the accessible EditHere name`);
    }
  }
  assert.deepEqual(oldBrand, [], 'Built HTML must not contain old 改这里 brand text');
});
