'use strict';
// Optional localhost preview and explicit Agent feedback submission bridge.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {AgentSessions, API_VERSION, MAX_BODY, defaultSessionDirectory} = require('./agent-session.cjs');
const html = path.join(__dirname, 'EditHere.html');
const sessionDirectory = defaultSessionDirectory();
function configuredPort() {
  const port = Number(process.env.EDITHERE_WEB_PORT ?? 7330);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('EDITHERE_WEB_PORT must be an integer between 1 and 65535.');
  return port;
}
function json(res, status, body, head = false) {
  res.writeHead(status, {'Content-Type':'application/json; charset=utf-8', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff', 'Referrer-Policy':'no-referrer'});
  res.end(head ? undefined : JSON.stringify(body));
}
function readBody(req) {
  return new Promise((resolve, reject) => {
    let bytes = 0, chunks = [], failed = false;
    req.on('data', chunk => {
      bytes += chunk.length;
      if (bytes > MAX_BODY) { if (!failed) { const error = new Error('Request body too large.'); error.status = 413; reject(error); } failed = true; chunks = []; }
      else if (!failed) chunks.push(chunk);
    });
    req.on('end', () => { if (!failed) { try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); } catch { const error = new Error('Invalid JSON body.'); error.status = 400; reject(error); } } });
    req.on('error', reject);
  });
}
function createPreviewServer({port = 7330, directory = sessionDirectory} = {}) {
  const origin = 'http://127.0.0.1:' + port;
  const sessions = new AgentSessions({directory, origin});
  const server = http.createServer(async (req, res) => {
    const actualPort = server.address()?.port || port;
    const currentOrigin = 'http://127.0.0.1:' + actualPort;
    sessions.origin = currentOrigin;
    // Exact Host/Origin checks protect the local writer against DNS rebinding
    // and cross-site form/fetch requests. This server deliberately has no CORS.
    if (req.headers.host !== '127.0.0.1:' + actualPort || req.headers.origin && req.headers.origin !== currentOrigin || req.headers['sec-fetch-site'] === 'cross-site') {
      json(res, 403, {error:'Only same-origin loopback requests are allowed.'}); return;
    }
    let url;
    try { url = new URL(req.url, currentOrigin); }
    catch { json(res, 400, {error:'Invalid request URL.'}); return; }
    try {
      const pathname = url.pathname;
      if (pathname.startsWith('/__edithere_agent/')) {
        if (req.method === 'POST' && !/^application\/json(?:\s*;|$)/i.test(req.headers['content-type'] || '')) { json(res, 415, {error:'application/json is required.'}); return; }
        if (pathname === '/__edithere_agent/session' && req.method === 'GET') { json(res, 200, sessions.browser(url.searchParams.get('token'))); return; }
        if (pathname === '/__edithere_agent/feedback' && req.method === 'POST') {
          if (req.headers.origin !== currentOrigin) { json(res, 403, {error:'Same-origin browser submission is required.'}); return; }
          const body=await readBody(req);json(res, 200, sessions.submit(url.searchParams.get('token'),body?.feedback||body,body?.feedback?body.previewDataUrl:undefined)); return;
        }
        if (pathname === '/__edithere_agent/cancel' && req.method === 'POST') {
          if (req.headers.origin !== currentOrigin) { json(res, 403, {error:'Same-origin browser cancellation is required.'}); return; }
          await readBody(req); json(res, 200, sessions.cancel(sessions.byToken(url.searchParams.get('token')))); return;
        }
        const authorization = req.headers.authorization || '';
        const suppliedKey = authorization.startsWith('Bearer ') ? authorization.slice(7) : '';
        if (!/^[a-f0-9]{64}$/.test(suppliedKey) || !crypto.timingSafeEqual(Buffer.from(suppliedKey), Buffer.from(sessions.key))) { json(res, 401, {error:'Local Agent authentication required.'}); return; }
        if (pathname === '/__edithere_agent/sessions' && req.method === 'POST') { json(res, 201, sessions.create(await readBody(req))); return; }
        const match = /^\/__edithere_agent\/sessions\/([a-f0-9]{32})(\/cancel)?$/.exec(pathname);
        if (match && !match[2] && req.method === 'GET') { json(res, 200, sessions.status(sessions.byId(match[1]))); return; }
        if (match && match[2] && req.method === 'POST') { await readBody(req); json(res, 200, sessions.cancel(sessions.byId(match[1]))); return; }
        json(res, 404, {error:'Agent endpoint not found.'}); return;
      }
      if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405, {Allow:'GET, HEAD'}); res.end(); return; }
      if (!['/', '/EditHere.html', '/__edithere_preview_health'].includes(pathname)) { res.writeHead(404); res.end(); return; }
      fs.readFile(html, (error, data) => {
        const health = pathname === '/__edithere_preview_health';
        if (error) { json(res, 503, {error:'EditHere.html is missing; run node web/build.cjs.'}, req.method === 'HEAD'); return; }
        if (health) { json(res, 200, {service:'edithere-web-preview',pid:process.pid,file:html,sha256:crypto.createHash('sha256').update(data).digest('hex'),apiVersion:API_VERSION,agentBridge:true,sessionDirectory:sessions.directory}, req.method === 'HEAD'); return; }
        let body = data;
        if (/^[a-f0-9]{64}$/.test(url.searchParams.get('agent') || '')) {
          try { sessions.byToken(url.searchParams.get('agent')); }
          catch (error) { json(res, error.status || 500, {error:error.message}); return; }
          // Only an authenticated session page receives same-origin networking.
          // The published offline bundle and ordinary preview remain connect-none.
          body = data.toString('utf8').replace("connect-src 'none'", "connect-src 'self'");
        }
        res.writeHead(200, {'Content-Type':'text/html; charset=utf-8', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff', 'Referrer-Policy':'no-referrer'});
        res.end(req.method === 'HEAD' ? undefined : body);
      });
    } catch (error) { if (!res.writableEnded) json(res, error.status || 500, {error:error.message}); }
  });
  server.requestTimeout = 30000;
  return {server, sessions};
}
if (require.main === module) {
  const port = configuredPort();
  const {server} = createPreviewServer({port});
  server.on('error', error => { console.error('EditHere preview:', error.message); process.exitCode = 1; });
  server.listen(port, '127.0.0.1', () => console.log('EditHere preview and Agent bridge: http://127.0.0.1:' + port + '/EditHere.html'));
}
module.exports = {createPreviewServer, sessionDirectory, configuredPort};
