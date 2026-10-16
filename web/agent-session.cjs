'use strict';
// Local, explicit-submission bridge. Browser tokens can only access their own
// supplied image; creation and status queries require the server's local key.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const {execFileSync} = require('node:child_process');
const feedbackFormat = require('./src/feedback.js');
const layoutFormat = require('./src/layout.js');

const API_VERSION = 1;
const MAX_BODY = 96 * 1024 * 1024;
const randomToken = () => crypto.randomBytes(32).toString('hex');
const digest = value => crypto.createHash('sha256').update(value).digest('hex');
function fail(message, status = 400) { const error = new Error(message); error.status = status; throw error; }
function imageData(value) {
  if (typeof value !== 'string' || value.length > MAX_BODY - 1024) fail('An image data URL is required.');
  const match = /^data:image\/(png|jpeg|webp|gif|bmp);base64,([A-Za-z0-9+/]+={0,2})$/.exec(value);
  if (!match || match[2].length % 4 !== 0) fail('Unsupported or malformed image data URL.');
  const bytes = Buffer.from(match[2], 'base64');
  if (bytes.toString('base64') !== match[2]) fail('Image base64 is not canonical.');
  const png = bytes.length >= 24 && bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]));
  const jpeg = bytes.length >= 4 && bytes[0] === 255 && bytes[1] === 216 && bytes[2] === 255;
  const webp = bytes.length >= 16 && bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP';
  const gif = bytes.length >= 10 && ['GIF87a','GIF89a'].includes(bytes.toString('ascii', 0, 6));
  const bmp = bytes.length >= 26 && bytes.toString('ascii', 0, 2) === 'BM';
  if (!({png,jpeg,webp,gif,bmp})[match[1]]) fail('Image signature does not match its MIME type.');
  return value;
}
function rectangle(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).sort().join(',') !== 'x1,x2,y1,y2') fail('Invalid feedback rectangle.');
  const out = {};
  for (const key of ['x1','y1','x2','y2']) {
    if (!Number.isFinite(value[key]) || value[key] < 0 || value[key] > 100000) fail('Invalid feedback coordinate.');
    out[key] = value[key];
  }
  if (out.x2 < out.x1 || out.y2 < out.y1) fail('Invalid feedback rectangle bounds.');
  return out;
}
function validateFeedback(value, image) {
  boundedJSON(value, MAX_BODY, 'Feedback');
  try { value = feedbackFormat.normalizeImage(value); } catch (error) { fail(error.message); }
  if (!Object.hasOwn(value, 'image')) fail('Expected native image feedback with the original image.');
  if (value.annotationSpace !== 'result' || value.image !== image) fail('Feedback must include this session\'s original image.');
  if (!Array.isArray(value.objects) || value.objects.length > 1000) fail('Invalid feedback objects.');
  const objects = value.objects.map(object => {
    if (!object || typeof object !== 'object' || Array.isArray(object) || Object.keys(object).sort().join(',') !== 'annotations,movements,source') fail('Invalid feedback object fields.');
    const source = object.source === null ? null : rectangle(object.source);
    if (!Array.isArray(object.movements) || object.movements.length > 1000 || !Array.isArray(object.annotations) || object.annotations.length > 1000) fail('Invalid movement or annotation list.');
    if (source === null && object.movements.length) fail('A global annotation cannot move.');
    const movements = object.movements.map(movement => {
      if (!movement || typeof movement !== 'object' || Array.isArray(movement) || Object.keys(movement).join(',') !== 'to') fail('Invalid movement destination.');
      const to = rectangle(movement.to);
      if (source.x1 === source.x2 || source.y1 === source.y2 || to.x1 === to.x2 || to.y1 === to.y2) fail('Movement rectangles must have positive dimensions.');
      return {to};
    });
    const annotations = object.annotations.map(text => {
      if (typeof text !== 'string' || !text.trim() || text.length > 10000) fail('Annotations must contain nonempty text.');
      return text;
    });
    if (!movements.length && !annotations.length) fail('Empty feedback objects are not valid.');
    return {source, movements, annotations};
  });
  return {...feedbackFormat.imageMetadata(value), annotationSpace:'result', image, objects};
}
function feedbackReceipt(feedback) {
  const {image, ...result} = feedback;
  return result;
}

function defaultSessionDirectory() {
  return path.resolve(process.env.EDITHERE_WEB_DATA_DIR || path.join(os.homedir(), '.edithere', 'web-sessions'));
}
function boundedJSON(value, limit, name) {
  let serialized;
  try { serialized = JSON.stringify(value); } catch { fail(name + ' must be serializable JSON.'); }
  if (!serialized || Buffer.byteLength(serialized) > limit) fail(name + ' is too large.');
  return serialized;
}
function allowedKeys(value, keys, name) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).some(key => !keys.includes(key))) fail('Invalid ' + name + ' fields.');
}
function validateDraft(value, session) {
  const serialized = boundedJSON(value, MAX_BODY, 'Draft');
  allowedKeys(value, ['schemaVersion','tool','savedAt','kind','name','width','height','document'], 'draft');
  if (value.schemaVersion !== 'edithere-web-project-1' || value.tool !== 'EditHere Web' || value.kind !== 'image' || value.name !== session.name || typeof value.savedAt !== 'string' || !Number.isFinite(Date.parse(value.savedAt))) fail('Invalid image draft project.');
  const {width, height, document:documentValue} = value;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width > 32767 || height > 32767 || width * height > 32000000) fail('Invalid draft dimensions.');
  allowedKeys(documentValue, ['id','timestampUs','image','notes','layout'], 'draft document');
  if (documentValue.image !== session.image || typeof documentValue.id !== 'string' || !documentValue.id || documentValue.id.length > 256 || /[\\/:*?"<>|\u0000-\u001f]/.test(documentValue.id) || documentValue.timestampUs !== null) fail('Draft must contain this session\'s original image.');
  if (!Array.isArray(documentValue.notes) || documentValue.notes.length > 1000) fail('Invalid draft annotation list.');
  const layout = documentValue.layout;
  if (layout !== null) {
    allowedKeys(layout, ['width','height','pieces','groups','movements'], 'draft layout');
    for (const piece of layout.pieces || []) allowedKeys(piece, ['id','source','to'], 'draft piece');
    for (const group of layout.groups || []) allowedKeys(group, ['id','label','pieces','originalBounds'], 'draft group');
    for (const movement of layout.movements || []) allowedKeys(movement, ['groupId','source','destination','sources'], 'draft movement');
    try { layoutFormat.validate(layout, width, height); } catch (error) { fail('Invalid draft layout: ' + error.message); }
  }
  const groups = new Set(layout?.groups.map(group => group.id) || []), ids = new Set();
  function draftRectangle(rect, allowNull = true) {
    if (rect === null && allowNull) return;
    const valid = rectangle(rect);
    if (valid.x2 > width || valid.y2 > height) fail('Draft annotation is outside the image.');
  }
  for (const note of documentValue.notes) {
    allowedKeys(note, ['id','source','text','groupId','groupIds','movementSource','movementSources'], 'draft annotation');
    if (typeof note.id !== 'string' || !note.id || note.id.length > 256 || ids.has(note.id) || typeof note.text !== 'string' || note.text.length > 10000 || !note.text.trim() && !note.movementSource) fail('Invalid draft annotation.');
    ids.add(note.id); draftRectangle(note.source);
    if (note.groupId && !groups.has(note.groupId)) fail('Draft annotation references a missing component.');
    if (note.groupIds !== undefined && (!Array.isArray(note.groupIds) || !note.groupIds.length || note.groupIds.length > 512 || new Set(note.groupIds).size !== note.groupIds.length || note.groupIds.some(id => !groups.has(id)))) fail('Invalid draft component references.');
    if (note.movementSource) {
      if (!layout || note.source === null) fail('Draft movement requires a layout and destination.');
      draftRectangle(note.movementSource, false);
    }
    if (note.movementSources !== undefined) {
      if (!note.movementSource || !Array.isArray(note.movementSources) || !note.movementSources.length || note.movementSources.length > 4096) fail('Invalid draft movement slices.');
      note.movementSources.forEach(region => draftRectangle(region, false));
    }
  }
  const draft = JSON.parse(serialized);
  // The immutable image lives once in the session record, not again per save.
  delete draft.document.image;
  return draft;
}
function hydratedDraft(session) {
  return session.draft ? {...session.draft, document:{...session.draft.document, image:session.image}} : undefined;
}
function flushDirectory(directory) {
  let descriptor;
  try { descriptor = fs.openSync(directory, 'r'); fs.fsyncSync(descriptor); }
  catch (error) { if (!['EISDIR','EPERM','EACCES','EINVAL','ENOTSUP'].includes(error.code)) throw error; }
  finally { if (descriptor !== undefined) fs.closeSync(descriptor); }
}
function writeSynced(file, serialized) {
  const descriptor = fs.openSync(file, 'wx', 0o600);
  try { fs.writeFileSync(descriptor, serialized); fs.fsyncSync(descriptor); }
  finally { fs.closeSync(descriptor); }
}
function atomicOutput(output, serialized, id) {
  const directory = path.dirname(output);
  fs.mkdirSync(directory, {recursive:true});
  const temp = path.join(directory, '.' + path.basename(output) + '.' + id + '.' + randomToken() + '.tmp');
  try {
    writeSynced(temp, serialized);
    // Publishing a fully flushed file with link is atomic and never replaces an
    // existing destination. rename would silently overwrite it on POSIX.
    try { fs.linkSync(temp, output); }
    catch (error) {
      if (process.platform !== 'win32' || !['EPERM','ENOTSUP','EOPNOTSUPP'].includes(error.code)) throw error;
      // Some Windows sandboxes deny creating hard links while ordinary moves
      // are allowed. The two-argument .NET Move is atomic on the same volume
      // and fails if the destination exists. Paths are data in the environment,
      // never interpolated into PowerShell code, and the helper stays hidden.
      const command = 'try { [System.IO.File]::Move($env:EDITHERE_PENDING_FILE, $env:EDITHERE_RESULT_FILE); exit 0 } catch { if ([System.IO.File]::Exists($env:EDITHERE_RESULT_FILE)) { exit 17 }; [Console]::Error.WriteLine($_.Exception.Message); exit 1 }';
      try {
        execFileSync('powershell.exe', ['-NoLogo','-NoProfile','-NonInteractive','-WindowStyle','Hidden','-Command',command], {
          windowsHide:true, timeout:10000, stdio:'pipe', env:{...process.env, EDITHERE_PENDING_FILE:temp, EDITHERE_RESULT_FILE:output}
        });
      } catch (moveError) {
        if (moveError.status === 17) moveError.code = 'EEXIST';
        throw moveError;
      }
    }
    flushDirectory(directory);
  } finally { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } }
}
function persistedSession(directory, id) {
  if (typeof id !== 'string' || !/^[a-f0-9]{32}$/.test(id)) fail('Invalid session ID.');
  const file = path.join(path.resolve(directory), id + '.json');
  let session;
  try {
    if (fs.statSync(file).size > MAX_BODY * 2 + 16384) fail('Session record is too large.');
    session = JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) { if (error.code === 'ENOENT') fail('Session not found.', 404); throw error; }
  if (session.id !== id || !/^[a-f0-9]{64}$/.test(session.token) || typeof session.output !== 'string' || !path.isAbsolute(session.output) || typeof session.inputPath !== 'string' || !path.isAbsolute(session.inputPath) || !['pending','submitting','completed','cancelled','expired'].includes(session.state) || !Number.isFinite(session.expiresAt)) fail('Invalid persisted session.');
  imageData(session.image);
  if (session.state === 'submitting') {
    try {
      const serialized = fs.readFileSync(session.output);
      if (digest(serialized) !== session.sha256) throw new Error('Unmatched result.');
      const feedback = validateFeedback(JSON.parse(serialized), session.image);
      session.result = feedbackReceipt(feedback);
      session.state = 'completed'; session.completedAt ||= new Date().toISOString();
    } catch { session.state = 'pending'; delete session.sha256; delete session.result; }
  }
  return session;
}
function receipt(session, now = Date.now()) {
  const state = session.state === 'pending' && now >= session.expiresAt ? 'expired' : session.state;
  return {tool:'EditHere', sessionId:session.id, state, name:session.name, inputPath:session.inputPath, output:session.output, expiresAt:session.expiresAt,
    ...(state === 'completed' ? {completedAt:session.completedAt, sha256:session.sha256, feedback:session.result} : {})};
}
function readSessionStatus({directory = defaultSessionDirectory(), sessionId}) {
  const session = persistedSession(directory, sessionId);
  if (session.state === 'completed') {
    let output;
    try { output = fs.readFileSync(session.output); } catch { fail('Completed feedback file is missing or unreadable.', 409); }
    if (digest(output) !== session.sha256) fail('Completed feedback file was changed after submission.', 409);
    const feedback = validateFeedback(JSON.parse(output), session.image);
    session.result = feedbackReceipt(feedback);
  }
  return receipt(session);
}

class AgentSessions {
  constructor({directory = defaultSessionDirectory(), origin = 'http://127.0.0.1:7330', now = Date.now, credentials = true} = {}) {
    this.directory = path.resolve(directory); this.origin = origin; this.now = now;
    fs.mkdirSync(this.directory, {recursive:true});
    if (credentials) {
      this.key = randomToken();
      fs.writeFileSync(path.join(this.directory, 'server-key'), this.key, {mode:0o600});
    }
    this.sessions = new Map();
    // A persistent user directory may contain many historical screenshots.
    // Load only the requested session rather than retaining all image buffers
    // on every MCP/preview startup. byId performs restart recovery on demand.
  }
  persist(session) {
    const file = path.join(this.directory, session.id + '.json');
    const temp = file + '.' + randomToken() + '.tmp';
    try { writeSynced(temp, JSON.stringify(session)); fs.renameSync(temp, file); flushDirectory(this.directory); }
    finally { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } }
  }
  expire(session) {
    if (session.state === 'pending' && this.now() >= session.expiresAt) session.state = 'expired';
    return session;
  }
  create(input) {
    if (!input || typeof input !== 'object') fail('Session input is required.');
    const image = imageData(input.image);
    if (typeof input.inputPath !== 'string' || !path.isAbsolute(input.inputPath) || typeof input.output !== 'string' || !path.isAbsolute(input.output)) fail('Input and output paths must be absolute.');
    const output = path.resolve(input.output);
    if (fs.existsSync(output)) fail('Output already exists; choose a unique path.', 409);
    const timeout = input.timeoutSeconds ?? 900;
    if (!Number.isInteger(timeout) || timeout < 30 || timeout > 86400) fail('Timeout must be 30–86400 seconds.');
    const name = input.name ?? path.basename(input.inputPath);
    if (typeof name !== 'string' || !name.trim() || name.length > 200) fail('Invalid image name.');
    const session = {id:crypto.randomBytes(16).toString('hex'), token:randomToken(), state:'pending', image, name,
      inputPath:path.resolve(input.inputPath), output, createdAt:new Date(this.now()).toISOString(), expiresAt:this.now() + timeout * 1000};
    this.sessions.set(session.id, session); this.persist(session);
    return {sessionId:session.id, url:this.origin + '/EditHere.html?agent=' + session.token, output, expiresAt:session.expiresAt};
  }
  byId(id) { const session = persistedSession(this.directory, id); this.sessions.set(id, session); return this.expire(session); }
  byToken(token) {
    if (typeof token !== 'string' || !/^[a-f0-9]{64}$/.test(token)) fail('Session not found.', 404);
    let session = [...this.sessions.values()].find(session => session.token === token);
    if (!session) {
      for (const name of fs.readdirSync(this.directory).filter(name => /^[a-f0-9]{32}\.json$/.test(name))) {
        try { const candidate = persistedSession(this.directory, name.slice(0, -5)); if (candidate.token === token) { session = candidate; this.sessions.set(candidate.id, candidate); break; } } catch { /* unrelated record */ }
      }
    }
    if (!session) fail('Session not found.', 404);
    const latest = this.byId(session.id);
    if (latest.token !== token) fail('Session not found.', 404);
    return latest;
  }
  status(session) {
    this.expire(session);
    return receipt(session, this.now());
  }
  browser(token) {
    const session = this.byToken(token);
    return {sessionId:session.id, name:session.name, state:session.state, expiresAt:session.expiresAt,
      ...(session.state === 'pending' ? {image:session.image} : {})};
  }
  submit(token, input) {
    const id = this.byToken(token).id;
    return this.locked(id, () => this.submitLocked(token, input));
  }
  submitLocked(token, input) {
    const session = this.byToken(token);
    const feedback = validateFeedback(input, session.image);
    const serialized = feedbackFormat.serialize(feedback), sha256 = digest(serialized);
    if (session.state === 'completed') {
      if (session.sha256 !== sha256) fail('This session already received different feedback.', 409);
      return this.status(session);
    }
    if (session.state !== 'pending') fail('This session is ' + session.state + '.', 409);
    session.state = 'submitting'; session.sha256 = sha256;
    session.result = feedbackReceipt(feedback);
    this.persist(session);
    try {
      atomicOutput(session.output, serialized, session.id);
    } catch (error) {
      // A failure after publishing (for example a lost directory flush ACK)
      // must keep the journal recoverable instead of orphaning the saved file.
      try {
        if (digest(fs.readFileSync(session.output)) === sha256) {
          session.state = 'completed'; session.completedAt = new Date(this.now()).toISOString(); this.persist(session);
          return this.status(session);
        }
      } catch { /* No matching committed output. Keep the existing file intact. */ }
      session.state = 'pending'; delete session.sha256; delete session.result; this.persist(session);
      fail(error.code === 'EEXIST' ? 'Output already exists; feedback was not overwritten.' : 'Could not save feedback: ' + error.message, error.code === 'EEXIST' ? 409 : 500);
    }
    session.state = 'completed'; session.completedAt = new Date(this.now()).toISOString(); this.persist(session);
    return this.status(session);
  }
  cancel(session) {
    return this.locked(session.id, () => this.cancelLocked(this.byId(session.id)));
  }
  cancelLocked(session) {
    this.expire(session);
    if (session.state === 'completed') fail('Completed feedback cannot be cancelled.', 409);
    if (session.state === 'pending') { session.state = 'cancelled'; this.persist(session); }
    return this.status(session);
  }
  saveDraft(token, project) {
    const id = this.byToken(token).id;
    return this.locked(id, () => {
      const session = this.byToken(token);
      if (session.state !== 'pending') fail('This session is ' + session.state + '.', 409);
      session.draft = validateDraft(project, session);
      session.draftSavedAt = new Date(this.now()).toISOString(); this.persist(session);
      return {sessionId:session.id, state:session.state, draftSavedAt:session.draftSavedAt};
    });
  }
  locked(id, operation) {
    const file = path.join(this.directory, id + '.lock'), owner = {pid:process.pid, token:randomToken()};
    let descriptor;
    for (let attempt = 0; attempt < 2; attempt++) {
      try { descriptor = fs.openSync(file, 'wx', 0o600); break; }
      catch (error) {
        if (error.code !== 'EEXIST') throw error;
        let stale = false;
        try {
          const holder = JSON.parse(fs.readFileSync(file, 'utf8'));
          if (!Number.isInteger(holder.pid) || holder.pid < 1) throw new Error('Invalid lock owner.');
          try { process.kill(holder.pid, 0); } catch (error) { stale = error.code === 'ESRCH'; }
        } catch { try { stale = Date.now() - fs.statSync(file).mtimeMs > 30000; } catch { stale = true; } }
        if (!stale || attempt) fail('This session is being saved. Retry the same action shortly.', 409);
        try { fs.unlinkSync(file); } catch (error) { if (error.code !== 'ENOENT') throw error; }
      }
    }
    try { fs.writeFileSync(descriptor, JSON.stringify(owner)); fs.closeSync(descriptor); descriptor = undefined; return operation(); }
    finally {
      if (descriptor !== undefined) fs.closeSync(descriptor);
      try { if (JSON.parse(fs.readFileSync(file, 'utf8')).token === owner.token) fs.unlinkSync(file); } catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
  }
}
module.exports = {AgentSessions, API_VERSION, MAX_BODY, imageData, validateFeedback, validateDraft, hydratedDraft, defaultSessionDirectory, readSessionStatus};
