#!/usr/bin/env node
// EditHere DSH bridge: a loopback-only annotation session for DeepSeek Harness.
// The browser edits a group of screenshots; this server stores the pages, waits
// for an explicit submission, then writes one compact JSON package plus only
// the annotated page images (source + numbered/arrowed preview).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';

const require = createRequire(import.meta.url);
const feedbackFormat = require('../web/src/feedback.js');

const HERE = path.dirname(fileURLToPath(import.meta.url));
const HTML_FILE = path.join(HERE, '..', 'web', 'EditHere.dsh.html');
const SERVICE = 'edithere-dsh';
const API = '/__edithere_dsh';
const MAX_BODY = 128 * 1024 * 1024;
const MAX_IMAGE = 48 * 1024 * 1024;
const DEFAULT_PORT = 4782;
const MIME = {png:'png', jpeg:'jpg', webp:'webp', gif:'gif', bmp:'bmp'};

function fail(message, status = 400) { const error = new Error(message); error.status = status; throw error; }
function nowIso() { return new Date().toISOString(); }
function digest(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function resolveDataDir() {
  return path.resolve(process.env.EDITHERE_DSH_DATA_DIR || path.join(os.homedir(), '.edithere', 'dsh'));
}
function json(res, status, body, head = false) {
  const text = JSON.stringify(body);
  res.writeHead(status, {'Content-Type':'application/json; charset=utf-8', 'Cache-Control':'no-store',
    'X-Content-Type-Options':'nosniff', 'Referrer-Policy':'no-referrer', 'Content-Length':Buffer.byteLength(text)});
  res.end(head ? undefined : text);
}
function readBody(req, limit = MAX_BODY) {
  return new Promise((resolve, reject) => {
    let bytes = 0, chunks = [], failed = false;
    req.on('data', chunk => {
      bytes += chunk.length;
      if (bytes > limit) { if (!failed) { const error = new Error('Request body is too large.'); error.status = 413; reject(error); } failed = true; chunks = []; }
      else if (!failed) chunks.push(chunk);
    });
    req.on('end', () => {
      if (failed) return;
      try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}')); }
      catch { const error = new Error('Invalid JSON body.'); error.status = 400; reject(error); }
    });
    req.on('error', reject);
  });
}
function mimeOf(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length < 12) return null;
  if (buffer.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) return 'png';
  if (buffer[0] === 255 && buffer[1] === 216 && buffer[2] === 255) return 'jpeg';
  if (buffer.toString('ascii', 0, 4) === 'RIFF' && buffer.toString('ascii', 8, 12) === 'WEBP') return 'webp';
  if (['GIF87a', 'GIF89a'].includes(buffer.toString('ascii', 0, 6))) return 'gif';
  if (buffer.toString('ascii', 0, 2) === 'BM') return 'bmp';
  return null;
}
function decodeDataUrl(value) {
  if(value&&typeof value==='object'&&value.mode==='embedded')value=value.dataUrl;
  if (typeof value !== 'string' || value.length > MAX_IMAGE * 2) fail('A PNG data URL is required for each annotated page.');
  const match = /^data:image\/(png|jpeg|webp|gif|bmp);base64,([A-Za-z0-9+/]+={0,2})$/.exec(value);
  if (!match || match[2].length % 4 !== 0) fail('Unsupported or malformed image data URL.');
  const buffer = Buffer.from(match[2], 'base64');
  if (buffer.toString('base64') !== match[2]) fail('Image base64 is not canonical.');
  if (buffer.length > MAX_IMAGE) fail('Page image exceeds 48 MiB.');
  if (!mimeOf(buffer)) fail('Image signature does not match a supported image.');
  return {buffer, mime:match[1], dataUrl:value};
}
function safeSlug(name) {
  return String(name || 'page').trim().replace(/[\\/:*?"<>|\u0000-\u001f]/g, '-')
    .replace(/\s+/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '').slice(0, 60) || 'page';
}
function pageFileName(index, name, mime, annotated = false) {
  return 'page-' + (index + 1) + '-' + safeSlug(name) + (annotated ? '-annotated' : '') + '.' + (MIME[mime] || 'png');
}
function writeFileSynced(file, data, flag = 'w') {
  fs.mkdirSync(path.dirname(file), {recursive:true});
  const descriptor = fs.openSync(file, flag, 0o600);
  try { fs.writeFileSync(descriptor, data); fs.fsyncSync(descriptor); }
  finally { fs.closeSync(descriptor); }
}
function publishExclusive(output, serialized) {
  const directory = path.dirname(output);
  fs.mkdirSync(directory, {recursive:true});
  const temp = path.join(directory, '.' + path.basename(output) + '.' + crypto.randomBytes(6).toString('hex') + '.tmp');
  try {
    writeFileSynced(temp, serialized);
    try { fs.linkSync(temp, output); }
    catch (error) {
      if (!['EPERM','ENOTSUP','EOPNOTSUPP','EXDEV'].includes(error.code)) throw error;
      fs.copyFileSync(temp, output, fs.constants.COPYFILE_EXCL);
    }
  } finally { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } }
}
function absoluteFile(value, label) {
  if (typeof value !== 'string' || !path.isAbsolute(value)) fail(label + ' must be an absolute path.');
  return path.resolve(value);
}
function imageFromPath(file, label = 'image') {
  const resolved = absoluteFile(file, label);
  let stat;
  try { stat = fs.statSync(resolved); } catch { fail(label + ' does not exist: ' + resolved, 400); }
  if (!stat.isFile()) fail(label + ' is not a file: ' + resolved);
  if (stat.size < 12 || stat.size > MAX_IMAGE) fail(label + ' is empty or exceeds 48 MiB.');
  const buffer = fs.readFileSync(resolved);
  const mime = mimeOf(buffer);
  if (!mime) fail(label + ' is not a PNG, JPEG, WebP, GIF or BMP image.');
  return {buffer, mime, name:path.basename(resolved, path.extname(resolved))};
}
function parseImageInput(value) {
  if (!value || typeof value !== 'object') fail('Invalid image input.');
  if (value.path !== undefined) return imageFromPath(value.path, 'image path');
  if (value.dataUrl !== undefined) {
    const decoded = decodeDataUrl(value.dataUrl);
    const name = typeof value.name === 'string' && value.name.trim() ? value.name.trim().slice(0, 120) : '页面';
    return {...decoded, name, ...(value.id !== undefined ? {id:value.id} : {})};
  }
  fail('Each image needs a path or a dataUrl.');
}

class Store {
  constructor({directory = resolveDataDir(), key} = {}) {
    this.directory = path.resolve(directory);
    this.sessions = path.join(this.directory, 'sessions');
    fs.mkdirSync(this.sessions, {recursive:true});
    this.key = key || this.ensureKey();
    this.watchers = new Map();
  }
  ensureKey() {
    const file = path.join(this.directory, 'server-key');
    if (!fs.existsSync(file)) writeFileSynced(file, crypto.randomBytes(32).toString('hex'));
    const key = fs.readFileSync(file, 'utf8').trim();
    if (!/^[a-f0-9]{64}$/.test(key)) fail('The stored server key is invalid.', 500);
    return key;
  }
  authenticate(authorization) {
    const supplied = typeof authorization === 'string' && authorization.startsWith('Bearer ') ? authorization.slice(7) : '';
    if (!/^[a-f0-9]{64}$/.test(supplied) || !crypto.timingSafeEqual(Buffer.from(supplied), Buffer.from(this.key))) fail('Local Agent authentication required.', 401);
  }
  sessionDirectory(id) { return path.join(this.sessions, id); }
  sessionFile(id) { return path.join(this.sessions, id + '.json'); }
  load(id) {
    if (typeof id !== 'string' || !/^[a-f0-9]{32}$/.test(id)) fail('Invalid session ID.');
    let session;
    try { session = JSON.parse(fs.readFileSync(this.sessionFile(id), 'utf8')); }
    catch (error) { if (error.code === 'ENOENT') fail('Session not found.', 404); throw error; }
    if (!session || session.id !== id || !/^[a-f0-9]{64}$/.test(session.token || '') ||
        typeof session.output !== 'string' || !path.isAbsolute(session.output) ||
        !['pending','completed','cancelled','expired'].includes(session.state) ||
        !Number.isFinite(session.expiresAt) || !Array.isArray(session.pages) || !Array.isArray(session.groupNotes)) fail('Invalid persisted session.', 500);
    if (session.state === 'pending' && Date.now() >= session.expiresAt) {
      session.state = 'expired';
      this.save(session);
    }
    return session;
  }
  save(session) {
    const file = this.sessionFile(session.id);
    const temp = file + '.' + crypto.randomBytes(6).toString('hex') + '.tmp';
    try { writeFileSynced(temp, JSON.stringify(session)); fs.renameSync(temp, file); }
    finally { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } }
  }
  byToken(token) {
    if (typeof token !== 'string' || !/^[a-f0-9]{64}$/.test(token)) fail('Session not found.', 404);
    const file = fs.readdirSync(this.sessions).find(name => /^[a-f0-9]{32}\.json$/.test(name) && (() => {
      try { return JSON.parse(fs.readFileSync(path.join(this.sessions, name), 'utf8')).token === token; } catch { return false; }
    })());
    if (!file) fail('Session not found.', 404);
    return this.load(file.slice(0, -5));
  }
  create({name, output, timeoutSeconds = 3600, images = []}) {
    if (typeof name !== 'string' || !name.trim() || name.length > 200) fail('A session name is required (max 200 characters).');
    const target = absoluteFile(output, 'output');
    if (fs.existsSync(target)) fail('Output already exists; choose a unique path.', 409);
    if (!Number.isInteger(timeoutSeconds) || timeoutSeconds < 60 || timeoutSeconds > 86400) fail('Timeout must be 60–86400 seconds.');
    if (!Array.isArray(images) || images.length > 64) fail('At most 64 images can be attached at once.');
    const id = crypto.randomBytes(16).toString('hex');
    const directory = this.sessionDirectory(id);
    fs.mkdirSync(path.join(directory, 'pages'), {recursive:true});
    fs.mkdirSync(path.join(directory, 'inbox'), {recursive:true});
    const session = {id, token:crypto.randomBytes(32).toString('hex'), state:'pending', name:name.trim(),
      output:target, createdAt:nowIso(), updatedAt:nowIso(), expiresAt:Date.now() + timeoutSeconds * 1000,
      revision:0, pages:[], groupNotes:[], completedAt:null, sha256:null, receipt:null};
    for (const input of images) this.addPage(session, parseImageInput(input), {save:false});
    this.save(session);
    return session;
  }
  addPage(session, image, {save = true, origin = 'agent'} = {}) {
    if (session.state !== 'pending') fail('This session is ' + session.state + '.', 409);
    if (session.pages.length >= 64) fail('An image set supports at most 64 pages.');
    const mime = image.mime || mimeOf(image.buffer);
    if (!mime) fail('Unsupported image.');
    const id = image.id || ('p-' + crypto.randomBytes(6).toString('hex'));
    if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$/.test(id) || session.pages.some(page => page.id === id)) fail('Invalid or duplicate page ID.');
    const relative = 'pages/' + id + '.' + MIME[mime];
    writeFileSynced(path.join(this.sessionDirectory(session.id), relative), image.buffer);
    session.revision++;
    const page = {id, name:String(image.name || ('页面 ' + (session.pages.length + 1))).trim().slice(0, 120) || ('页面 ' + (session.pages.length + 1)),
      file:relative, revision:session.revision, origin, createdAt:nowIso()};
    session.pages.push(page);
    session.updatedAt = nowIso();
    if (save) this.save(session);
    return page;
  }
  publicPage(session, page) {
    const file = path.join(this.sessionDirectory(session.id), page.file);
    const buffer = fs.readFileSync(file);
    const mime = mimeOf(buffer) || 'png';
    return {id:page.id, name:page.name, revision:page.revision, dataUrl:'data:image/' + mime + ';base64,' + buffer.toString('base64')};
  }
  payload(session, {since = null} = {}) {
    const pages = since === null ? session.pages : session.pages.filter(page => page.revision > since);
    return {service:SERVICE, sessionId:session.id, state:session.state, name:session.name,
      output:session.output, inbox:path.join(this.sessionDirectory(session.id), 'inbox'),
      expiresAt:session.expiresAt, revision:session.revision,
      pages:pages.map(page => this.publicPage(session, page)),
      ...(since === null ? {draft:this.readDraft(session), draftSavedAt:session.draftSavedAt || null} : {}),
      groupNotes:session.groupNotes};
  }
  readDraft(session) {
    const file = path.join(this.sessionDirectory(session.id), 'draft.json');
    try {
      if (fs.statSync(file).size > 96 * 1024 * 1024) return null;
      return JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch { return null; }
  }
  saveDraft(session, project) {
    if (session.state !== 'pending') fail('This session is ' + session.state + '.', 409);
    const kind = project && typeof project === 'object' && !Array.isArray(project) ? project.kind : undefined;
    const validSet = kind === 'set' && Array.isArray(project.pages) && project.pages.length >= 1 && project.pages.length <= 64 &&
      Array.isArray(project.groupNotes) && project.groupNotes.length <= 1000;
    const validImage = kind === 'image' && project.document && typeof project.document === 'object' &&
      typeof project.document.image === 'string' && /^data:image\//.test(project.document.image);
    if (!project || typeof project !== 'object' || Array.isArray(project) ||
        project.schemaVersion !== 'edithere-web-project-1' || (!validSet && !validImage)) fail('Invalid EditHere draft.');
    let serialized;
    try { serialized = JSON.stringify(project); } catch { fail('Draft must be serializable JSON.'); }
    if (Buffer.byteLength(serialized) > 96 * 1024 * 1024) fail('Draft is too large.');
    const file = path.join(this.sessionDirectory(session.id), 'draft.json');
    const temp = file + '.' + crypto.randomBytes(6).toString('hex') + '.tmp';
    try { writeFileSynced(temp, serialized); fs.renameSync(temp, file); }
    finally { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } }
    session.draftSavedAt = nowIso();
    session.updatedAt = session.draftSavedAt;
    this.save(session);
    return session.draftSavedAt;
  }
  deleteDraft(session) {
    try { fs.unlinkSync(path.join(this.sessionDirectory(session.id), 'draft.json')); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  receipt(session) {
    const base = {tool:'EditHere DSH', sessionId:session.id, state:session.state, name:session.name,
      output:session.output, inbox:path.join(this.sessionDirectory(session.id), 'inbox'),
      expiresAt:session.expiresAt, pageCount:session.state === 'completed' ? (session.receipt?.pageCount ?? session.pages.length) : session.pages.length,
      draftSavedAt:session.draftSavedAt || null};
    if (session.state !== 'completed') return base;
    return {...base, completedAt:session.completedAt, sha256:session.sha256, summary:session.receipt?.summary || null,
      pages:(session.receipt?.pages || []).slice()};
  }
  watchInbox(id, notify) {
    if (this.watchers.has(id)) return;
    const sizes = new Map();
    const timer = setInterval(() => {
      let session;
      try { session = this.load(id); } catch { clearInterval(timer); this.watchers.delete(id); return; }
      if (session.state !== 'pending') { clearInterval(timer); this.watchers.delete(id); return; }
      const inbox = path.join(this.sessionDirectory(id), 'inbox');
      let names;
      try { names = fs.readdirSync(inbox); } catch { return; }
      for (const name of names) {
        const file = path.join(inbox, name);
        let stat;
        try { stat = fs.statSync(file); } catch { continue; }
        if (!stat.isFile() || stat.size < 12 || stat.size > MAX_IMAGE) continue;
        if (!/\.(png|jpe?g|webp|gif|bmp)$/i.test(name)) continue;
        const previous = sizes.get(file);
        sizes.set(file, stat.size);
        if (previous !== stat.size || Date.now() - stat.mtimeMs < 450) continue;
        try {
          const image = imageFromPath(file, 'inbox image');
          image.name = path.basename(name, path.extname(name)).replace(/^\d+[-_]/, '');
          const page = this.addPage(session, image, {save:false, origin:'inbox'});
          session.updatedAt = nowIso();
          this.save(session);
          const target = path.join(inbox, 'imported');
          fs.mkdirSync(target, {recursive:true});
          fs.renameSync(file, path.join(target, Date.now() + '-' + path.basename(name)));
          sizes.delete(file);
          notify(session, page);
        } catch {
          const rejected = path.join(inbox, 'rejected');
          try { fs.mkdirSync(rejected, {recursive:true}); fs.renameSync(file, path.join(rejected, Date.now() + '-' + path.basename(name))); } catch { /* ignore */ }
        }
      }
    }, 1200);
    timer.unref?.();
    this.watchers.set(id, timer);
  }
  stopInbox(id) {
    const timer = this.watchers.get(id);
    if (timer) clearInterval(timer);
    this.watchers.delete(id);
    if (this.watchers.size === 0) { /* no shared timer to stop */ }
  }
  cancel(session) {
    if (session.state === 'completed') fail('Completed feedback cannot be cancelled.', 409);
    if (session.state === 'pending') { session.state = 'cancelled'; session.updatedAt = nowIso(); this.save(session); }
    this.stopInbox(session.id);
    return session;
  }
  submit(session, body) {
    if (session.state !== 'pending') fail('This session is ' + session.state + '.', 409);
    if (!body || typeof body !== 'object') fail('Feedback is required.');
    let normalized;
    try { normalized = feedbackFormat.normalizeSet(body.feedback); }
    catch (error) { fail('Invalid image set feedback: ' + error.message); }
    if (normalized.set.name.length > 200) fail('The feedback set name is too long.');
    if (!Array.isArray(body.annotated)) fail('Annotated page images are required.');
    const annotated = new Map();
    for (const item of body.annotated) {
      if (!item || typeof item.id !== 'string' || !/^[A-Za-z0-9_.-]{1,128}$/.test(item.id)) fail('Invalid annotated page ID.');
      if (annotated.has(item.id)) fail('Duplicate annotated page ID.');
      const decoded = decodeDataUrl(item.dataUrl);
      annotated.set(item.id, decoded);
    }
    const output = session.output;
    if (fs.existsSync(output)) fail('Output already exists; feedback was not overwritten.', 409);
    const directory = path.dirname(output);
    const imagesDirectory = path.join(directory, 'images');
    fs.mkdirSync(imagesDirectory, {recursive:true});
    const final = JSON.parse(JSON.stringify(normalized));
    const pageIndex = new Map(final.pageIndex.map(page => [page.id, page]));
    const receiptPages = [];
    for (const [offset, page] of final.pages.entries()) {
      const source = decodeDataUrl(page.feedback.image);
      const known=session.pages.find(item=>item.id===page.id);
      if(known&&digest(source.buffer)!==digest(fs.readFileSync(path.join(this.sessionDirectory(session.id),known.file))))fail('Submitted page does not match the reviewed original.');
      const annotatedImage = annotated.get(page.id);
      if (!annotatedImage) fail('Missing annotated image for page "' + page.name + '".');
      const sourceName = pageFileName(offset, page.name, source.mime, false);
      const annotatedName = pageFileName(offset, page.name, annotatedImage.mime, true);
      writeFileSynced(path.join(imagesDirectory, sourceName), source.buffer);
      writeFileSynced(path.join(imagesDirectory, annotatedName), annotatedImage.buffer);
      if(page.feedback.image&&typeof page.feedback.image==='object'){const {dataUrl,...descriptor}=page.feedback.image;page.feedback.image={...descriptor,mode:'none'};}
      else delete page.feedback.image;
      page.imageFile = 'images/' + sourceName;
      page.annotatedImage = 'images/' + annotatedName;
      const indexEntry = pageIndex.get(page.id);
      if (indexEntry) { indexEntry.imageFile = page.imageFile; indexEntry.annotatedImage = page.annotatedImage; }
      receiptPages.push({id:page.id, name:page.name, index:page.index,
        sourceImage:'images/' + sourceName, annotatedImage:'images/' + annotatedName,
        sourcePath:path.join(imagesDirectory, sourceName), annotatedPath:path.join(imagesDirectory, annotatedName),
        width:page.width, height:page.height});
    }
    const finalPageIds = new Set(final.pages.map(page => page.id));
    for (const [id] of annotated) if (!finalPageIds.has(id)) fail('Annotated image has no matching feedback page: ' + id);
    final.dsh = {sessionId:session.id, name:session.name, output:path.resolve(output),
      instruction:'Only pages in pages[] are annotated. Read each annotatedImage and sourceImage; pageIndex lists context pages without images. Apply the feedback to the product and report which files changed.'};
    const serialized = JSON.stringify(final, null, 2) + '\n';
    publishExclusive(output, serialized);
    session.state = 'completed';
    session.completedAt = nowIso();
    session.updatedAt = session.completedAt;
    session.sha256 = digest(serialized);
    session.receipt = {pageCount:final.set.pageCount, summary:feedbackFormat.summary(final, serialized), pages:receiptPages};
    this.save(session);
    this.deleteDraft(session);
    this.stopInbox(session.id);
    return session;
  }
}

function sameOrigin(req, port) {
  if (req.headers['sec-fetch-site'] === 'cross-site') return false;
  const origin = req.headers.origin;
  if (!origin) return true;
  return origin === 'http://127.0.0.1:' + port || origin === 'http://localhost:' + port;
}
function hostAllowed(req, port) {
  const host = String(req.headers.host || '');
  return host === '127.0.0.1:' + port || host === 'localhost:' + port || host === '[::1]:' + port;
}

export async function startServer({port = Number(process.env.EDITHERE_DSH_PORT) || DEFAULT_PORT, directory = resolveDataDir()} = {}) {
  if (!fs.existsSync(HTML_FILE)) throw new Error('EditHere.dsh.html is missing; run node web/build.cjs first.');
  const html = fs.readFileSync(HTML_FILE, 'utf8').replace("connect-src 'none'", "connect-src 'self'");
  const htmlSha = crypto.createHash('sha256').update(fs.readFileSync(HTML_FILE)).digest('hex');
  const store = new Store({directory});
  const clients = new Set();
  function broadcast(session, event, data) {
    for (const client of clients) {
      if (client.sessionId !== session.id) continue;
      try { client.res.write('event: ' + event + '\ndata: ' + JSON.stringify(data) + '\n\n'); }
      catch { /* the close handler removes it */ }
    }
  }
  function notifyPage(session, page) { broadcast(session, 'page', store.publicPage(session, page)); }
  function ensureWatch(session) {
    if (session.state === 'pending') store.watchInbox(session.id, (current, page) => notifyPage(current, page));
  }
  const server = http.createServer(async (req, res) => {
    const actualPort = server.address()?.port || port;
    const currentOrigin = 'http://127.0.0.1:' + actualPort;
    if (!hostAllowed(req, actualPort)) { json(res, 403, {error:'Only loopback Host headers are allowed.'}); return; }
    let url;
    try { url = new URL(req.url, currentOrigin); }
    catch { json(res, 400, {error:'Invalid request URL.'}); return; }
    try {
      const pathname = url.pathname;
      if (pathname === API + '/health' && req.method === 'GET') {
        json(res, 200, {service:SERVICE, pid:process.pid, dataDir:store.directory, html:HTML_FILE, htmlSha,
          version:1, now:nowIso()});
        return;
      }
      if (pathname.startsWith(API + '/')) {
        if (req.method === 'POST' && !/^application\/json(?:\s*;|$)/i.test(req.headers['content-type'] || '')) { json(res, 415, {error:'application/json is required.'}); return; }
        if (pathname === API + '/session' && req.method === 'GET') {
          const session = store.byToken(url.searchParams.get('token'));
          if (session.state !== 'pending') { json(res, 200, {service:SERVICE, sessionId:session.id, state:session.state, name:session.name, output:session.output, expiresAt:session.expiresAt, revision:session.revision, pages:[], groupNotes:session.groupNotes}); return; }
          const since = url.searchParams.get('since');
          const payload = store.payload(session, {since:since === null ? null : Number(since)});
          ensureWatch(session);
          json(res, 200, payload);
          return;
        }
        if (pathname === API + '/events' && req.method === 'GET') {
          if (!sameOrigin(req, actualPort)) { json(res, 403, {error:'Same-origin requests are required.'}); return; }
          const session = store.byToken(url.searchParams.get('token'));
          if (session.state !== 'pending') { json(res, 410, {error:'This session is ' + session.state + '.'}); return; }
          res.writeHead(200, {'Content-Type':'text/event-stream; charset=utf-8', 'Cache-Control':'no-store',
            'Connection':'keep-alive', 'X-Accel-Buffering':'no'});
          res.write('retry: 2000\n\n');
          const client = {sessionId:session.id, res, timer:null};
          clients.add(client);
          res.write('event: hello\ndata: ' + JSON.stringify({revision:session.revision, state:session.state}) + '\n\n');
          client.timer = setInterval(() => { try { res.write(': ping\n\n'); } catch { /* close */ } }, 15000);
          client.timer.unref?.();
          req.on('close', () => { clearInterval(client.timer); clients.delete(client); });
          ensureWatch(session);
          return;
        }
        if (pathname === API + '/draft' && req.method === 'POST') {
          if (!sameOrigin(req, actualPort) || req.headers.origin !== currentOrigin) { json(res, 403, {error:'Same-origin browser draft saving is required.'}); return; }
          const session = store.byToken(url.searchParams.get('token'));
          const body = await readBody(req);
          const savedAt = store.saveDraft(session, body.project);
          json(res, 200, {state:session.state, draftSavedAt:savedAt});
          return;
        }
        if (pathname === API + '/feedback' && req.method === 'POST') {
          if (!sameOrigin(req, actualPort) || req.headers.origin !== currentOrigin) { json(res, 403, {error:'Same-origin browser submission is required.'}); return; }
          const session = store.byToken(url.searchParams.get('token'));
          const body = await readBody(req);
          const completed = store.submit(session, body);
          broadcast(completed, 'state', {state:completed.state, output:completed.output, sha256:completed.sha256});
          json(res, 200, store.receipt(completed));
          return;
        }
        if (pathname === API + '/cancel' && req.method === 'POST') {
          if (!sameOrigin(req, actualPort) || req.headers.origin !== currentOrigin) { json(res, 403, {error:'Same-origin browser cancellation is required.'}); return; }
          const session = store.byToken(url.searchParams.get('token'));
          await readBody(req);
          store.cancel(session);
          broadcast(session, 'state', {state:session.state, output:session.output});
          json(res, 200, store.receipt(session));
          return;
        }
        store.authenticate(req.headers.authorization);
        if (pathname === API + '/sessions' && req.method === 'POST') {
          const body = await readBody(req);
          const session = store.create({name:body.name, output:body.output, timeoutSeconds:body.timeoutSeconds, images:body.images || []});
          ensureWatch(session);
          json(res, 201, {service:SERVICE, sessionId:session.id, token:session.token,
            url:'http://127.0.0.1:' + actualPort + '/?dsh=' + session.token,
            output:session.output, inbox:path.join(store.sessionDirectory(session.id), 'inbox'),
            expiresAt:session.expiresAt, pageCount:session.pages.length});
          return;
        }
        const match = /^\/__edithere_dsh\/sessions\/([a-f0-9]{32})(\/pages|\/cancel)?$/.exec(pathname);
        if (match) {
          const session = store.load(match[1]);
          if (!match[2] && req.method === 'GET') { json(res, 200, store.receipt(session)); return; }
          if (match[2] === '/pages' && req.method === 'POST') {
            const body = await readBody(req);
            const images = Array.isArray(body.images) ? body.images : body.image ? [body.image] : [];
            if (!images.length) fail('At least one image is required.');
            if (session.pages.length + images.length > 64) fail('An image set supports at most 64 pages.');
            const added = [];
            for (const input of images) added.push(store.addPage(session, parseImageInput(input), {save:false}));
            store.save(session);
            ensureWatch(session);
            for (const page of added) notifyPage(session, page);
            json(res, 201, {service:SERVICE, sessionId:session.id, added:added.map(page => ({id:page.id, name:page.name, revision:page.revision})), pageCount:session.pages.length});
            return;
          }
          if (match[2] === '/cancel' && req.method === 'POST') {
            await readBody(req);
            store.cancel(session);
            broadcast(session, 'state', {state:session.state, output:session.output});
            json(res, 200, store.receipt(session));
            return;
          }
        }
        json(res, 404, {error:'Agent endpoint not found.'});
        return;
      }
      if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405, {Allow:'GET, HEAD'}); res.end(); return; }
      if (pathname !== '/' && pathname !== '/EditHere.dsh.html') { res.writeHead(404); res.end(); return; }
      let body = html;
      if (/^[a-f0-9]{64}$/.test(url.searchParams.get('dsh') || '')) {
        try { store.byToken(url.searchParams.get('dsh')); }
        catch (error) { json(res, error.status || 500, {error:error.message}); return; }
      }
      res.writeHead(200, {'Content-Type':'text/html; charset=utf-8', 'Cache-Control':'no-store',
        'X-Content-Type-Options':'nosniff', 'Referrer-Policy':'no-referrer'});
      res.end(req.method === 'HEAD' ? undefined : body);
    } catch (error) {
      if (res.writableEnded || res.headersSent) { try { res.end(); } catch { /* already closed */ } return; }
      json(res, error.status || 500, {error:error.message});
    }
  });
  server.requestTimeout = 120000;
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => { server.off('error', reject); resolve(); });
  });
  return {server, store, port:server.address().port};
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const portArg = process.argv.indexOf('--port');
  const port = portArg >= 0 ? Number(process.argv[portArg + 1]) : Number(process.env.EDITHERE_DSH_PORT) || DEFAULT_PORT;
  startServer({port}).then(({port}) => {
    console.log('EditHere DSH bridge listening on http://127.0.0.1:' + port);
  }).catch(error => { console.error('EditHere DSH bridge:', error.message); process.exitCode = 1; });
}
