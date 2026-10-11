'use strict';
// Read-only localhost preview. The editor itself remains a standalone HTML.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const html = path.join(__dirname, 'EditHere.html');
const server = http.createServer((req, res) => {
  let pathname;
  try { pathname = new URL(req.url, 'http://127.0.0.1:7330').pathname; }
  catch { res.writeHead(400); res.end(); return; }
  if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405, {Allow:'GET, HEAD'}); res.end(); return; }
  if (!['/', '/EditHere.html', '/__edithere_preview_health'].includes(pathname)) { res.writeHead(404); res.end(); return; }
  fs.readFile(html, (error, data) => {
    const health = pathname === '/__edithere_preview_health';
    const body = error ? 'EditHere.html is missing; run node web/build.cjs.' : health
      ? JSON.stringify({service:'edithere-web-preview',pid:process.pid,file:html,sha256:crypto.createHash('sha256').update(data).digest('hex')}) : data;
    res.writeHead(error ? 503 : 200, {'Content-Type':health ? 'application/json; charset=utf-8' : 'text/html; charset=utf-8', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff'});
    res.end(req.method === 'HEAD' ? undefined : body);
  });
});
server.on('error', error => { console.error('EditHere preview:', error.message); process.exitCode = 1; });
server.listen(7330, '127.0.0.1', () => console.log('EditHere preview: http://127.0.0.1:7330/EditHere.html'));
