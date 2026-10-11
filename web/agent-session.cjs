'use strict';
// Local, explicit-submission bridge. Browser tokens can only access their own
// supplied image; creation and status queries require the server's local key.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const feedbackFormat = require('./src/feedback.js');

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
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).sort().join(',') !== 'annotationSpace,image,objects') fail('Expected native image feedback: annotationSpace, image and objects.');
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
  return {annotationSpace:'result', image, objects};
}

class AgentSessions {
  constructor({directory, origin = 'http://127.0.0.1:7330', now = Date.now}) {
    this.directory = path.resolve(directory); this.origin = origin; this.now = now;
    fs.mkdirSync(this.directory, {recursive:true});
    this.key = randomToken();
    fs.writeFileSync(path.join(this.directory, 'server-key'), this.key, {mode:0o600});
    this.sessions = new Map();
    for (const name of fs.readdirSync(this.directory).filter(name => /^[a-f0-9]{32}\.json$/.test(name))) {
      try {
        const session = JSON.parse(fs.readFileSync(path.join(this.directory, name), 'utf8'));
        if (session.id + '.json' !== name || !/^[a-f0-9]{64}$/.test(session.token) || !path.isAbsolute(session.output)) continue;
        if (session.state === 'submitting') {
          try {
            if (digest(fs.readFileSync(session.output)) === session.sha256) { session.state = 'completed'; session.completedAt ||= new Date(this.now()).toISOString(); }
            else { session.state = 'pending'; delete session.sha256; delete session.result; }
          } catch { session.state = 'pending'; delete session.sha256; delete session.result; }
        }
        this.sessions.set(session.id, session); this.persist(session); this.expire(session);
      } catch { /* A broken unrelated metadata file must not expose a session. */ }
    }
  }
  persist(session) {
    const file = path.join(this.directory, session.id + '.json');
    const temp = file + '.' + randomToken() + '.tmp';
    fs.writeFileSync(temp, JSON.stringify(session), {mode:0o600});
    fs.renameSync(temp, file);
  }
  expire(session) {
    if (session.state === 'pending' && this.now() >= session.expiresAt) { session.state = 'expired'; this.persist(session); }
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
  byId(id) { const session = this.sessions.get(id); if (!session) fail('Session not found.', 404); return this.expire(session); }
  byToken(token) { const session = [...this.sessions.values()].find(session => session.token === token); if (!session) fail('Session not found.', 404); return this.expire(session); }
  status(session) {
    this.expire(session);
    return {tool:'EditHere', sessionId:session.id, state:session.state, name:session.name, inputPath:session.inputPath, output:session.output, expiresAt:session.expiresAt,
      ...(session.state === 'completed' ? {completedAt:session.completedAt, sha256:session.sha256, feedback:session.result} : {})};
  }
  browser(token) {
    const session = this.byToken(token);
    return {sessionId:session.id, name:session.name, state:session.state, expiresAt:session.expiresAt,
      ...(session.state === 'pending' ? {image:session.image} : {})};
  }
  submit(token, input) {
    const session = this.byToken(token);
    const feedback = validateFeedback(input, session.image);
    const serialized = feedbackFormat.serialize(feedback), sha256 = digest(serialized);
    if (session.state === 'completed') {
      if (session.sha256 !== sha256) fail('This session already received different feedback.', 409);
      return this.status(session);
    }
    if (session.state !== 'pending') fail('This session is ' + session.state + '.', 409);
    session.state = 'submitting'; session.sha256 = sha256;
    session.result = {annotationSpace:feedback.annotationSpace, objects:feedback.objects};
    this.persist(session);
    try {
      fs.mkdirSync(path.dirname(session.output), {recursive:true});
      fs.writeFileSync(session.output, serialized, {flag:'wx', mode:0o600});
    } catch (error) {
      session.state = 'pending'; delete session.sha256; delete session.result; this.persist(session);
      fail(error.code === 'EEXIST' ? 'Output already exists; feedback was not overwritten.' : 'Could not save feedback: ' + error.message, error.code === 'EEXIST' ? 409 : 500);
    }
    session.state = 'completed'; session.completedAt = new Date(this.now()).toISOString(); this.persist(session);
    return this.status(session);
  }
  cancel(session) {
    this.expire(session);
    if (session.state === 'completed') fail('Completed feedback cannot be cancelled.', 409);
    if (session.state === 'pending') { session.state = 'cancelled'; this.persist(session); }
    return this.status(session);
  }
}
module.exports = {AgentSessions, API_VERSION, MAX_BODY, imageData, validateFeedback};
