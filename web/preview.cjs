'use strict';
// Start or reuse the local preview, then verify that it serves this checkout.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const html = path.join(__dirname, 'EditHere.html');
const healthUrl = 'http://127.0.0.1:7330/__edithere_preview_health';
const previewUrl = 'http://127.0.0.1:7330/EditHere.html';
function health() {
  return new Promise(resolve => {
    const request = http.get(healthUrl, response => {
      let body = '';
      response.setEncoding('utf8');
      response.on('data', chunk => { body += chunk; if (body.length > 8192) request.destroy(); });
      response.on('end', () => { try { resolve(response.statusCode === 200 ? JSON.parse(body) : null); } catch { resolve(null); } });
      response.on('error', () => resolve(null));
    });
    request.setTimeout(1000, () => { request.destroy(); resolve(null); });
    request.on('error', () => resolve(null));
  });
}
function verify(state, requireBridge = false) {
  if (state.service !== 'edithere-web-preview' || path.resolve(state.file || '') !== html) throw new Error('Port 7330 is serving another project. Stop that service before starting this preview.');
  const hash = crypto.createHash('sha256').update(fs.readFileSync(html)).digest('hex');
  if (state.sha256 !== hash) throw new Error('The preview response does not match the current EditHere.html.');
  if (requireBridge && (state.apiVersion !== 1 || state.agentBridge !== true)) throw new Error('The existing preview (PID ' + state.pid + ') predates the Agent bridge. Stop that verified EditHere preview process and run node web/preview.cjs again.');
}
async function ensurePreview({requireBridge = false} = {}) {
  if (!fs.existsSync(html)) throw new Error('EditHere.html is missing. Run node web/build.cjs first.');
  const existing = await health();
  if (existing) { verify(existing, requireBridge); return existing; }
  const logs = path.join(__dirname, 'tests/output/preview');
  fs.mkdirSync(logs, {recursive:true});
  const out = fs.openSync(path.join(logs, 'server.log'), 'a');
  const err = fs.openSync(path.join(logs, 'server-error.log'), 'a');
  const child = spawn(process.execPath, [path.join(__dirname, 'preview-server.cjs')], {cwd:__dirname,detached:true,windowsHide:true,stdio:['ignore',out,err]});
  fs.closeSync(out); fs.closeSync(err);
  let failure;
  child.on('error', error => { failure = error; });
  child.unref();
  for (let attempt = 0; attempt < 12; attempt++) {
    const state = await health();
    if (state) { verify(state, requireBridge); fs.writeFileSync(path.join(logs, 'server.pid'), String(state.pid)); return state; }
    if (failure || child.exitCode !== null) break;
    await new Promise(resolve => setTimeout(resolve, 250));
  }
  throw failure || new Error('Preview did not start. Check ' + path.join(logs, 'server-error.log') + '; another service may occupy port 7330.');
}
if (require.main === module) ensurePreview().then(state => {
  if (process.argv.includes('--json')) console.log(JSON.stringify({...state, url:previewUrl}));
  else console.log('EditHere preview is ready (PID ' + state.pid + '): ' + previewUrl);
}).catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = {ensurePreview, health, verify, previewUrl};
