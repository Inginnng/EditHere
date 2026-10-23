'use strict';
// A zero-dependency CLI for the local EditHere annotation round trip.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {ensurePreview} = require('./preview.cjs');
const {sessionDirectory} = require('./preview-server.cjs');
const {imageData, MAX_BODY, readSessionStatus} = require('./agent-session.cjs');
const legacyDirectory = path.join(__dirname, 'tests/output/agent');
function persistedStatus(sessionId) {
  for (const directory of [...new Set([sessionDirectory, legacyDirectory])]) {
    if (fs.existsSync(path.join(directory, sessionId + '.json'))) return readSessionStatus({directory, sessionId});
  }
  throw new Error('Session not found in persistent storage.');
}
function options(args) {
  const result = {command:args[0]};
  for (let index = 1; index < args.length; index += 2) {
    const name = args[index];
    if (!/^--(image|output|name|timeout|session|wait)$/.test(name) || args[index + 1] === undefined || Object.hasOwn(result, name.slice(2))) throw new Error('Invalid or duplicate argument: ' + name);
    result[name.slice(2)] = args[index + 1];
  }
  return result;
}
function request(method, endpoint, body) {
  const key = fs.readFileSync(path.join(sessionDirectory, 'server-key'), 'utf8');
  return new Promise((resolve, reject) => {
    const content = body === undefined ? undefined : JSON.stringify(body);
    const req = http.request('http://127.0.0.1:7330' + endpoint, {method, headers:{Authorization:'Bearer ' + key, ...(content ? {'Content-Type':'application/json','Content-Length':Buffer.byteLength(content)} : {})}}, res => {
      let bytes = 0, chunks = [];
      res.on('data', chunk => { bytes += chunk.length; if (bytes > MAX_BODY) req.destroy(new Error('Response exceeds the allowed limit.')); else chunks.push(chunk); });
      res.on('end', () => { try { const data = JSON.parse(Buffer.concat(chunks).toString('utf8')); if (res.statusCode < 200 || res.statusCode >= 300) throw new Error(data.error || 'HTTP ' + res.statusCode); resolve(data); } catch (error) { reject(error); } });
      res.on('error', reject);
    });
    req.setTimeout(20000, () => req.destroy(new Error('Local EditHere request timed out.')));
    req.on('error', reject); req.end(content);
  });
}
async function run(args) {
  const opts = options(args);
  if (!['start','poll','wait','cancel'].includes(opts.command)) throw new Error('Usage: node web/agent.cjs start --image ABS --output ABS [--name NAME] [--timeout 900]; wait|poll|cancel --session ID [--wait 55]');
  if (opts.command === 'start') {
    if (!opts.image || !path.isAbsolute(opts.image) || !opts.output || !path.isAbsolute(opts.output)) throw new Error('--image and --output must be absolute paths.');
    if (opts.session || opts.wait) throw new Error('start does not accept --session or --wait.');
    const inputPath = path.resolve(opts.image);
    if (fs.statSync(inputPath).size > 64 * 1024 * 1024) throw new Error('Input image exceeds 64 MiB.');
    const bytes = fs.readFileSync(inputPath);
    const extension = path.extname(inputPath).toLowerCase();
    const mime = {'.png':'png','.jpg':'jpeg','.jpeg':'jpeg','.webp':'webp','.gif':'gif','.bmp':'bmp'}[extension];
    if (!mime) throw new Error('Supported input images: PNG, JPEG, WebP, GIF and BMP.');
    const image = imageData('data:image/' + mime + ';base64,' + bytes.toString('base64'));
    await ensurePreview({requireBridge:true});
    return request('POST', '/__edithere_agent/sessions', {image,inputPath,output:path.resolve(opts.output),name:opts.name || path.basename(inputPath),timeoutSeconds:opts.timeout === undefined ? 900 : Number(opts.timeout)});
  }
  if (!/^[a-f0-9]{32}$/.test(opts.session || '')) throw new Error('--session must be the sessionId returned by start.');
  if (opts.image || opts.output || opts.name || opts.timeout) throw new Error('Only --session and optional --wait are accepted for this command.');
  const endpoint = '/__edithere_agent/sessions/' + opts.session;
  if (opts.command === 'cancel') {
    await ensurePreview({requireBridge:true});
    return request('POST', endpoint + '/cancel', {});
  }
  const seconds = opts.command === 'wait' ? Number(opts.wait ?? 55) : 0;
  if (!Number.isFinite(seconds) || seconds < 0 || seconds > 55) throw new Error('--wait must be between 0 and 55 seconds.');
  const deadline = Date.now() + seconds * 1000;
  let status;
  do {
    // Reading a saved receipt never launches a service or depends on port 7330.
    status = persistedStatus(opts.session);
    if (status.state !== 'pending' || Date.now() >= deadline) return status;
    await new Promise(resolve => setTimeout(resolve, Math.min(500, Math.max(0, deadline - Date.now()))));
  } while (Date.now() <= deadline);
  return status;
}
if (require.main === module) run(process.argv.slice(2)).then(result => console.log(JSON.stringify(result))).catch(error => { console.error(JSON.stringify({error:error.message})); process.exitCode = 1; });
module.exports = {run, options, request, persistedStatus};
