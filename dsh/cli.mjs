#!/usr/bin/env node
// Command-line entry for the EditHere DSH annotation loop.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import os from 'node:os';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const {snapshotManifest} = createRequire(import.meta.url)('../web/image-set.cjs');

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SERVER = path.join(HERE, 'server.mjs');
const SKILL = path.join(HERE, 'skill', 'edithere-dsh');
const SERVICE = 'edithere-dsh';
const API = '/__edithere_dsh';
const DEFAULT_PORT = Number(process.env.EDITHERE_DSH_PORT) || 4782;
const DATA_DIR = path.resolve(process.env.EDITHERE_DSH_DATA_DIR || path.join(os.homedir(), '.edithere', 'dsh'));
const DSH_HOME = path.resolve(process.env.DSH_HOME || path.join(os.homedir(), '.dsh'));
const KEY_FILE = path.join(DATA_DIR, 'server-key');

function usage() {
  return `EditHere DSH — visual page annotation for DeepSeek Harness

  node cli.mjs start  --name "桌面程序页面" [--image <png> ...] [--output <json>] [--timeout 3600] [--open]
  node cli.mjs start  --project <set.json> [--output <json>] [--open]
  node cli.mjs add    --session <id> --image <png> [--image <png> ...]
  node cli.mjs wait   --session <id> [--wait 50]
  node cli.mjs status --session <id>
  node cli.mjs cancel --session <id>
  node cli.mjs serve  [--port 4782]
  node cli.mjs install-skill [--project | --dest <directory>]
  node cli.mjs capture --output <png> [--all] [--window <title>]

The server always listens on 127.0.0.1 only. Feedback is written only after the
user presses “完成并返回 AI” in the browser.`;
}
function parseArgs(argv) {
  const command = argv[0];
  const opts = {images:[], _:[]};
  for (let index = 1; index < argv.length; index++) {
    const arg = argv[index];
    if (!arg.startsWith('--')) { opts._.push(arg); continue; }
    const key = arg.slice(2);
    const next = argv[index + 1];
    if (next === undefined || next.startsWith('--')) { opts[key] = true; continue; }
    index++;
    if (key === 'image') opts.images.push(next); else opts[key] = next;
  }
  return {command, opts};
}
function portOf(opts) {
  const value = opts.port === undefined ? DEFAULT_PORT : Number(opts.port);
  if (!Number.isInteger(value) || value < 1024 || value > 65535) throw new Error('--port must be 1024–65535.');
  return value;
}
function request({port, endpoint, method = 'GET', body, auth = false}) {
  const headers = {};
  let content;
  if (body !== undefined) {
    content = JSON.stringify(body);
    headers['Content-Type'] = 'application/json';
    headers['Content-Length'] = Buffer.byteLength(content);
  }
  if (auth) {
    if (!fs.existsSync(KEY_FILE)) throw new Error('The EditHere DSH server has not created its local key yet. Run start first.');
    headers.Authorization = 'Bearer ' + fs.readFileSync(KEY_FILE, 'utf8').trim();
  }
  return new Promise((resolve, reject) => {
    const req = http.request({host:'127.0.0.1', port, path:endpoint, method, headers}, res => {
      let bytes = 0, chunks = [];
      res.on('data', chunk => {
        bytes += chunk.length;
        if (bytes > 160 * 1024 * 1024) { req.destroy(new Error('Response is too large.')); return; }
        chunks.push(chunk);
      });
      res.on('end', () => {
        try {
          const data = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
          if (res.statusCode < 200 || res.statusCode >= 300) throw new Error(data.error || 'HTTP ' + res.statusCode);
          resolve(data);
        } catch (error) { reject(error); }
      });
    });
    req.setTimeout(30000, () => req.destroy(new Error('The local EditHere DSH server did not respond.')));
    req.on('error', reject);
    req.end(content);
  });
}
function health(port) {
  return new Promise(resolve => {
    const req = http.get({host:'127.0.0.1', port, path:API + '/health', timeout:1200}, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', chunk => { body += chunk; if (body.length > 8192) req.destroy(); });
      res.on('end', () => { try { resolve(res.statusCode === 200 ? JSON.parse(body) : null); } catch { resolve(null); } });
      res.on('error', () => resolve(null));
    });
    req.on('error', () => resolve(null));
    req.on('timeout', () => { req.destroy(); resolve(null); });
  });
}
async function ensureServer(port) {
  const running = await health(port);
  if (running) {
    if (running.service !== SERVICE) throw new Error('Port ' + port + ' is used by another service.');
    if (path.resolve(running.dataDir || '') !== DATA_DIR) throw new Error('Port ' + port + ' is serving a different EditHere DSH data directory.');
    return running;
  }
  const logs = path.join(DATA_DIR, 'logs');
  fs.mkdirSync(logs, {recursive:true});
  const out = fs.openSync(path.join(logs, 'server.log'), 'a');
  const err = fs.openSync(path.join(logs, 'server-error.log'), 'a');
  const child = spawn(process.execPath, [SERVER, '--port', String(port)], {
    cwd:HERE, detached:true, windowsHide:true, stdio:['ignore', out, err]
  });
  child.unref();
  fs.closeSync(out); fs.closeSync(err);
  let failure;
  child.once('error', error => { failure = error; });
  for (let attempt = 0; attempt < 40; attempt++) {
    const state = await health(port);
    if (state) { if (state.service !== SERVICE) throw new Error('Port ' + port + ' is used by another service.'); return state; }
    if (failure || child.exitCode !== null) break;
    await new Promise(resolve => setTimeout(resolve, 250));
  }
  throw failure || new Error('The EditHere DSH server did not start. See ' + path.join(logs, 'server-error.log'));
}
function outputPath(opts) {
  if (opts.output !== undefined) {
    if (typeof opts.output !== 'string') throw new Error('--output needs a path.');
    return path.resolve(opts.output);
  }
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  return path.join(process.cwd(), '.edithere', 'dsh', 'feedback-' + stamp + '.json');
}
function imageInputs(opts) {
  if (opts.project) {
    if (typeof opts.project !== 'string' || opts.images.length) throw new Error('Use --project or --image.');
    return snapshotManifest(opts.project).pages.map(page => ({id:page.id,name:page.name,dataUrl:page.image}));
  }
  if (!opts.images.length) return [];
  return opts.images.map(file => {
    if (typeof file !== 'string') throw new Error('--image needs a path.');
    const resolved = path.resolve(file);
    if (!fs.existsSync(resolved)) throw new Error('Image does not exist: ' + resolved);
    return {path:resolved};
  });
}
function openUrl(url) {
  if (process.platform === 'win32') spawn('cmd.exe', ['/c', 'start', '', url], {detached:true, stdio:'ignore', windowsHide:true}).unref();
  else if (process.platform === 'darwin') spawn('open', [url], {detached:true, stdio:'ignore'}).unref();
  else spawn('xdg-open', [url], {detached:true, stdio:'ignore'}).unref();
}
function print(value) { process.stdout.write(JSON.stringify(value, null, 2) + '\n'); }

async function start(opts) {
  const port = portOf(opts);
  if (typeof opts.name !== 'string' || !opts.name.trim()) opts.name = opts.project ? snapshotManifest(opts.project).name : '组图';
  if (opts.timeout !== undefined && (!Number.isInteger(Number(opts.timeout)) || Number(opts.timeout) < 60)) throw new Error('--timeout must be at least 60 seconds.');
  await ensureServer(port);
  const body = {name:opts.name, output:outputPath(opts), timeoutSeconds:opts.timeout === undefined ? 3600 : Number(opts.timeout), images:imageInputs(opts)};
  const session = await request({port, endpoint:API + '/sessions', method:'POST', body, auth:true});
  const result = {...session, cli:path.join(HERE, 'cli.mjs'),
    next:'Open url in the browser, annotate one or more pages, then run: node ' + path.join(HERE, 'cli.mjs') + ' wait --session ' + session.sessionId};
  if (opts.open) openUrl(session.url);
  print(result);
  return result;
}
async function add(opts) {
  const port = portOf(opts);
  if (typeof opts.session !== 'string' || !/^[a-f0-9]{32}$/.test(opts.session)) throw new Error('--session must be the sessionId returned by start.');
  const images = imageInputs(opts);
  if (!images.length) throw new Error('At least one --image is required.');
  await ensureServer(port);
  const result = await request({port, endpoint:API + '/sessions/' + opts.session + '/pages', method:'POST', body:{images}, auth:true});
  print(result);
  return result;
}
async function status(opts) {
  const port = portOf(opts);
  if (typeof opts.session !== 'string' || !/^[a-f0-9]{32}$/.test(opts.session)) throw new Error('--session must be the sessionId returned by start.');
  await ensureServer(port);
  return request({port, endpoint:API + '/sessions/' + opts.session, auth:true});
}
async function wait(opts) {
  const port = portOf(opts);
  if (typeof opts.session !== 'string' || !/^[a-f0-9]{32}$/.test(opts.session)) throw new Error('--session must be the sessionId returned by start.');
  const seconds = opts.wait === undefined ? 50 : Number(opts.wait);
  if (!Number.isFinite(seconds) || seconds < 0 || seconds > 55) throw new Error('--wait must be between 0 and 55 seconds.');
  await ensureServer(port);
  const deadline = Date.now() + seconds * 1000;
  let receipt;
  do {
    receipt = await request({port, endpoint:API + '/sessions/' + opts.session, auth:true});
    if (receipt.state !== 'pending' || Date.now() >= deadline) break;
    await new Promise(resolve => setTimeout(resolve, 500));
  } while (Date.now() <= deadline);
  print(receipt);
  return receipt;
}
async function cancel(opts) {
  const port = portOf(opts);
  if (typeof opts.session !== 'string' || !/^[a-f0-9]{32}$/.test(opts.session)) throw new Error('--session must be the sessionId returned by start.');
  await ensureServer(port);
  const result = await request({port, endpoint:API + '/sessions/' + opts.session + '/cancel', method:'POST', body:{}, auth:true});
  print(result);
  return result;
}
async function serve(opts) {
  const {startServer} = await import('./server.mjs');
  const {port} = await startServer({port:portOf(opts)});
  print({service:SERVICE, url:'http://127.0.0.1:' + port + '/', dataDir:DATA_DIR});
  return new Promise(() => {});
}
function installSkill(opts) {
  if (!fs.existsSync(path.join(SKILL, 'SKILL.md'))) throw new Error('The bundled edithere-dsh skill is missing.');
  let destination;
  if (opts.dest !== undefined) destination = path.resolve(opts.dest);
  else if (opts.project) destination = path.join(process.cwd(), '.dsh', 'skills', 'edithere-dsh');
  else destination = path.join(DSH_HOME, 'skills', 'edithere-dsh');
  fs.mkdirSync(path.dirname(destination), {recursive:true});
  fs.cpSync(SKILL, destination, {recursive:true, force:true});
  const local = [
    '# Local paths for edithere-dsh',
    '',
    'CLI: ' + path.join(HERE, 'cli.mjs'),
    'Server: ' + SERVER,
    'Data: ' + DATA_DIR,
    'Default URL: http://127.0.0.1:' + DEFAULT_PORT + '/',
    '',
    'Example:',
    '  node "' + path.join(HERE, 'cli.mjs') + '" start --name "桌面程序页面" --image "C:\\shots\\home.png" --image "C:\\shots\\settings.png" --open',
    ''
  ].join('\n');
  fs.writeFileSync(path.join(destination, 'LOCAL.md'), local);
  const result = {installed:true, destination, cli:path.join(HERE, 'cli.mjs'), local:path.join(destination, 'LOCAL.md')};
  print(result);
  return result;
}
async function capture(opts) {
  if (process.platform !== 'win32') throw new Error('capture currently supports Windows only; use your screenshot tool and --image instead.');
  if (typeof opts.output !== 'string') throw new Error('--output is required.');
  const output = path.resolve(opts.output);
  fs.mkdirSync(path.dirname(output), {recursive:true});
  const script = path.join(HERE, 'capture.ps1');
  const args = ['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',script,'-Path',output];
  if (opts.all) args.push('-All');
  if (typeof opts.window === 'string') args.push('-WindowTitle', opts.window);
  await new Promise((resolve, reject) => {
    const child = spawn('powershell.exe', args, {windowsHide:true, stdio:['ignore','inherit','inherit']});
    child.once('error', reject);
    child.once('exit', code => code === 0 ? resolve() : reject(new Error('Screenshot capture failed with exit code ' + code)));
  });
  const result = {captured:true, path:output};
  print(result);
  return result;
}

const {command, opts} = parseArgs(process.argv.slice(2));
try {
  if (!command || command === 'help' || command === '--help' || command === '-h') { console.log(usage()); process.exitCode = command ? 0 : 1; }
  else if (command === 'start') await start(opts);
  else if (command === 'add') await add(opts);
  else if (command === 'wait') await wait(opts);
  else if (command === 'status') print(await status(opts));
  else if (command === 'cancel') await cancel(opts);
  else if (command === 'serve') await serve(opts);
  else if (command === 'install-skill') installSkill(opts);
  else if (command === 'capture') await capture(opts);
  else { console.error(usage()); process.exitCode = 1; }
} catch (error) {
  console.error(JSON.stringify({error:error.message}));
  process.exitCode = 1;
}
