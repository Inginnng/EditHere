import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Node itself is an executable CLI fixture. No GUI, shell scripts, installed
// EditHere binary, or third-party test package is required.
process.env.EDITHERE_CLI = process.execPath;
const connectorUrl = new URL('../connector/mcp-server.mjs', import.meta.url);
const { runCli, cliResultToContent, statusContent } = await import(connectorUrl);

test('CLI output preserves a multibyte character split between stream chunks', async () => {
  const script = `
    const bytes = Buffer.from(JSON.stringify({ok: true, message: '中文路径'}) + '\\n');
    const split = bytes.indexOf(Buffer.from('中')) + 1;
    process.stdout.write(bytes.subarray(0, split));
    setTimeout(() => process.stdout.write(bytes.subarray(split)), 100);
  `;
  const result = await runCli(['-e', script]);
  assert.equal(result.exitCode, 0);
  assert.equal(result.json.message, '中文路径');
  assert.equal(cliResultToContent(result).isError, false);
});

test('a CLI crash after printing success is reported as failure', async () => {
  const result = await runCli(['-e', "console.log(JSON.stringify({ok:true,running:true})); process.exitCode=9"]);
  assert.equal(result.exitCode, 9);
  assert.equal(result.json.ok, true);
  assert.equal(cliResultToContent(result).isError, true);
  assert.equal(statusContent(result).isError, true);
});

test('a timed-out CLI cannot turn earlier success output into success', async () => {
  const result = await runCli(['-e', "console.log(JSON.stringify({ok:true})); setInterval(()=>{},1000)"], { timeoutMs: 1000 });
  assert.equal(result.timedOut, true);
  assert.equal(result.json.ok, true);
  assert.equal(cliResultToContent(result).isError, true);
});

test('excessive child-process output is bounded and reported as failure', async () => {
  const result = await runCli(['-e', "process.stdout.write('x'.repeat(2*1024*1024)); setInterval(()=>{},1000)"], { timeoutMs: 3000 });
  assert.equal(result.error?.code, 'output_limit');
  assert.ok(result.stdout.length <= 1024 * 1024);
  assert.equal(cliResultToContent(result).isError, true);
});

test('closing MCP stdin terminates an outstanding CLI child', { timeout: 5000 }, async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-connector-test-'));
  const pidFile = path.join(directory, 'child.pid');
  const runner = path.join(directory, 'runner.mjs');
  fs.writeFileSync(runner, `
    import { runCli, main } from ${JSON.stringify(connectorUrl.href)};
    runCli(['-e', ${JSON.stringify(`require('fs').writeFileSync(${JSON.stringify(pidFile)}, String(process.pid)); setInterval(()=>{},1000)`)}]);
    await main();
  `);
  const server = spawn(process.execPath, [runner], { stdio: ['pipe', 'pipe', 'pipe'] });
  let childPid;
  try {
    const started = Date.now();
    while (!fs.existsSync(pidFile) && Date.now() - started < 2000) {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    assert.ok(fs.existsSync(pidFile), 'the test CLI child must have started');
    childPid = Number(fs.readFileSync(pidFile, 'utf8'));
    const closed = once(server, 'close');
    server.stdin.end();
    const [code] = await Promise.race([
      closed,
      new Promise((_, reject) => setTimeout(() => reject(new Error('connector did not exit after stdin EOF')), 2000).unref()),
    ]);
    assert.equal(code, 0);
    assert.throws(() => process.kill(childPid, 0), { code: 'ESRCH' });
  } finally {
    server.kill('SIGKILL');
    if (childPid) { try { process.kill(childPid, 'SIGKILL'); } catch { /* already gone */ } }
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('MCP reports malformed JSON and remains available for the next request', { timeout: 5000 }, async () => {
  const server = spawn(process.execPath, [fileURLToPath(connectorUrl)], { stdio: ['pipe', 'pipe', 'pipe'] });
  server.stdout.setEncoding('utf8');
  let output = '';
  server.stdout.on('data', (chunk) => { output += chunk; });
  try {
    server.stdin.write('{not-json}\n');
    server.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: 7, method: 'ping' }) + '\n');
    const started = Date.now();
    while (!output.includes('"id":7') && Date.now() - started < 2000) {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    const replies = output.trim().split('\n').filter(Boolean).map((line) => JSON.parse(line));
    assert.deepEqual(replies.map((reply) => reply.id), [null, 7]);
    assert.equal(replies[0].error.code, -32700);
    assert.deepEqual(replies[1].result, {});
  } finally {
    server.kill('SIGKILL');
  }
});
