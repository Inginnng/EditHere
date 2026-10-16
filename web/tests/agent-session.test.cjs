'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const http = require('node:http');
const {spawnSync} = require('node:child_process');
const {AgentSessions, readSessionStatus, hydratedDraft} = require('../agent-session.cjs');
const {createPreviewServer} = require('../preview-server.cjs');
const format = require('../src/feedback.js');
const image = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';
const rectangle = {x1:0,y1:0,x2:1,y2:1};
const feedback = {annotationSpace:'result',image,objects:[
  {source:rectangle,movements:[{to:rectangle}],annotations:[]},
  {source:null,movements:[],annotations:['游戏主页：开始按钮更醒目']}
]};
function fixture() {
  const directory = path.join(__dirname, 'output/agent-unit', crypto.randomUUID());
  return {directory, output:path.join(directory,'feedback.json'), inputPath:path.join(directory,'home.png')};
}
function create(store, paths, extras = {}) { return store.create({image,name:'游戏主页',inputPath:paths.inputPath,output:paths.output,...extras}); }
const token = session => new URL(session.url).searchParams.get('agent');

test('explicit submit writes native complete JSON; receipt omits image; retry is idempotent', () => {
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  assert.equal(store.status(store.byId(session.sessionId)).state, 'pending');
  assert.equal(fs.existsSync(paths.output), false);
  assert.equal(store.browser(token(session)).image, image);
  const receipt = store.submit(token(session), feedback);
  assert.equal(receipt.state, 'completed');
  assert.equal(receipt.feedback.image, undefined);
  assert.deepEqual(receipt.feedback.objects, feedback.objects);
  assert.equal(fs.readFileSync(paths.output, 'utf8'), format.serialize(feedback));
  assert.equal(receipt.sha256, crypto.createHash('sha256').update(fs.readFileSync(paths.output)).digest('hex'));
  assert.deepEqual(store.submit(token(session), feedback), receipt);
  assert.throws(() => store.submit(token(session), {...feedback,objects:[]}), /different feedback/);
});

test('versioned metadata survives submission, restart, disk verification and crash recovery', () => {
  const metadata = {$schema:'urn:edithere:feedback-minimal:2', schemaVersion:'feedback-minimal-2',
    tool:{name:'EditHere', version:'0.11.1'}, exportedAt:'2026-10-16T16:30:00+08:00'};
  const current = {...metadata, ...feedback};
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  const receipt = store.submit(token(session), current);
  assert.deepEqual(receipt.feedback, {...metadata, annotationSpace:'result', objects:feedback.objects});
  assert.deepEqual(JSON.parse(fs.readFileSync(paths.output, 'utf8')), current);
  assert.deepEqual(readSessionStatus({directory:paths.directory,sessionId:session.sessionId}), receipt);
  const raw = store.byId(session.sessionId); raw.state = 'submitting'; raw.result = {objects:[]}; store.persist(raw);
  const restarted = new AgentSessions(paths);
  assert.deepEqual(restarted.status(restarted.byId(session.sessionId)), receipt);
  assert.deepEqual(restarted.submit(token(session), current), receipt);
});

test('a declared image version cannot omit or replace its metadata', () => {
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  const current = {...feedback, $schema:'urn:edithere:feedback-minimal:2', schemaVersion:'feedback-minimal-2',
    tool:{name:'EditHere',version:'0.11.1'}, exportedAt:'2026-10-16T08:30:00Z'};
  for (const invalid of [
    {...current,schemaVersion:'feedback-minimal-3'}, {...current,$schema:null},
    {...current,tool:{name:'Another tool',version:'0.11.1'}}, {...current,exportedAt:'2026-10-16T08:30:00'}
  ]) assert.throws(() => store.submit(token(session), invalid));
  assert.equal(fs.existsSync(paths.output), false);
  assert.equal(store.byId(session.sessionId).state, 'pending');
});

test('feedback rejects a replaced image, empty text, invalid coordinates and incompatible fields', () => {
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  for (const invalid of [
    {...feedback,image:image.replace('iVBOR','xVBOR')},
    {...feedback,annotationSpace:'original'},
    {...feedback,schemaVersion:'invented'},
    {...feedback,objects:[{source:null,movements:[],annotations:[' ']}]},
    {...feedback,objects:[{source:{...rectangle,x1:NaN},movements:[],annotations:['test']}]},
    {...feedback,objects:[{source:null,movements:[{to:rectangle}],annotations:[]}]},
    {...feedback,objects:[{source:rectangle,movements:[],annotations:[],id:'web-only'}]}
  ]) assert.throws(() => store.submit(token(session), invalid));
  assert.equal(store.byId(session.sessionId).state, 'pending');
  assert.equal(fs.existsSync(paths.output), false);
});

test('cancel and timeout never create a result or pretend to complete', () => {
  let now = 1700000000000;
  const paths = fixture(), store = new AgentSessions({...paths,now:()=>now}), session = create(store, paths, {timeoutSeconds:30});
  now += 30000;
  const expired = store.status(store.byId(session.sessionId));
  assert.equal(expired.state, 'expired'); assert.equal(expired.feedback, undefined);
  assert.throws(() => store.submit(token(session), feedback), /expired/);
  const secondPaths = {...paths,output:path.join(paths.directory,'cancelled.json')}, second = create(store, secondPaths);
  assert.equal(store.cancel(store.byId(second.sessionId)).state, 'cancelled');
  assert.throws(() => store.submit(token(second), feedback), /cancelled/);
  assert.equal(fs.existsSync(paths.output), false); assert.equal(fs.existsSync(secondPaths.output), false);
});

test('restart restores pending sessions, original tokens and completed receipts', () => {
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  const oldKey = store.key;
  const restarted = new AgentSessions(paths);
  assert.notEqual(restarted.key, oldKey);
  assert.equal(restarted.browser(token(session)).image, image);
  const receipt = restarted.submit(token(session), feedback);
  const again = new AgentSessions(paths);
  assert.deepEqual(again.status(again.byId(session.sessionId)), receipt);
  assert.deepEqual(again.submit(token(session), feedback), receipt);
});

test('a crash after output write recovers only the recorded matching feedback', () => {
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  const raw = store.byId(session.sessionId), serialized = format.serialize(feedback);
  raw.state = 'submitting'; raw.sha256 = crypto.createHash('sha256').update(serialized).digest('hex');
  raw.result = {annotationSpace:'result',objects:feedback.objects}; store.persist(raw);
  fs.writeFileSync(paths.output, serialized);
  const restarted = new AgentSessions(paths);
  assert.equal(restarted.status(restarted.byId(session.sessionId)).state, 'completed');
  assert.deepEqual(restarted.status(restarted.byId(session.sessionId)).feedback.objects, feedback.objects);
});

test('existing output is never overwritten, including a file created after session start', () => {
  const paths = fixture(), store = new AgentSessions(paths), session = create(store, paths);
  fs.writeFileSync(paths.output, 'another unique result');
  assert.throws(() => store.submit(token(session), feedback), /not overwritten/);
  assert.equal(fs.readFileSync(paths.output, 'utf8'), 'another unique result');
  assert.equal(store.byId(session.sessionId).state, 'pending');
  assert.throws(() => create(store, paths), /already exists/);
});

test('HTTP bridge requires Agent authentication and same-origin browser submission', async context => {
  const paths = fixture(), {server,sessions} = createPreviewServer({port:0,directory:paths.directory});
  await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
  context.after(() => new Promise(resolve => server.close(resolve)));
  const origin = 'http://127.0.0.1:' + server.address().port;
  const body = {image,name:'游戏主页',inputPath:paths.inputPath,output:paths.output};
  const headers = {'Content-Type':'application/json',Authorization:'Bearer ' + sessions.key};
  assert.equal((await fetch(origin + '/__edithere_agent/sessions',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})).status,401);
  assert.equal((await fetch(origin + '/__edithere_agent/sessions',{method:'POST',headers:{...headers,Origin:'https://untrusted.example'},body:JSON.stringify(body)})).status,403);
  const forgedHost = await new Promise((resolve,reject) => {
    const request = http.get(origin + '/__edithere_preview_health',{headers:{Host:'untrusted.example'}},response => { response.resume(); resolve(response.statusCode); });
    request.on('error',reject);
  });
  assert.equal(forgedHost,403);
  const response = await fetch(origin + '/__edithere_agent/sessions',{method:'POST',headers,body:JSON.stringify(body)});
  assert.equal(response.status,201); const session = await response.json();
  assert.equal(new URL(session.url).origin,origin);
  assert.equal((await fetch(origin + '/__edithere_agent/sessions/' + session.sessionId)).status,401);
  const input = await (await fetch(origin + '/__edithere_agent/session?token=' + token(session))).json();
  assert.equal(input.image,image); assert.equal(input.output,undefined);
  const submitURL = origin + '/__edithere_agent/feedback?token=' + token(session);
  assert.equal((await fetch(submitURL,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(feedback)})).status,403);
  assert.equal((await fetch(submitURL,{method:'POST',headers:{'Content-Type':'text/plain',Origin:origin},body:JSON.stringify(feedback)})).status,415);
  const completed = await fetch(submitURL,{method:'POST',headers:{'Content-Type':'application/json',Origin:origin},body:JSON.stringify(feedback)});
  assert.equal(completed.status,200); assert.equal((await completed.json()).state,'completed');
  const receipt = await (await fetch(origin + '/__edithere_agent/sessions/' + session.sessionId,{headers:{Authorization:'Bearer ' + sessions.key}})).json();
  assert.equal(receipt.feedback.image,undefined); assert.equal(receipt.state,'completed');
  assert.equal((await fetch(origin + '/__edithere_agent/session?token=' + '0'.repeat(64))).status,404);
  assert.equal((await fetch(origin + '/__edithere_agent/file?path=' + encodeURIComponent(paths.inputPath))).status,401);
});

test('only a valid session HTML gains same-origin networking; ordinary offline preview stays strict', async context => {
  const paths = fixture(), {server,sessions} = createPreviewServer({port:0,directory:paths.directory});
  await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
  context.after(() => new Promise(resolve => server.close(resolve)));
  const origin = 'http://127.0.0.1:' + server.address().port;
  const ordinary = await (await fetch(origin + '/EditHere.html')).text();
  const policy = html => /<meta\s+http-equiv="Content-Security-Policy"\s+content="([^"]+)"/.exec(html)?.[1];
  assert.match(policy(ordinary),/connect-src 'none'/);
  sessions.origin = origin; const session = create(sessions,paths);
  const page = await (await fetch(session.url)).text();
  assert.match(policy(page),/connect-src 'self'/); assert.doesNotMatch(policy(page),/connect-src 'none'/);
  assert.equal((await fetch(origin + '/EditHere.html?agent=' + '0'.repeat(64))).status,404);
  const health = await (await fetch(origin + '/__edithere_preview_health')).json();
  assert.equal(health.apiVersion,1); assert.equal(health.agentBridge,true);
  assert.equal(health.key,undefined); assert.equal(health.token,undefined);
  assert.equal(health.sha256,crypto.createHash('sha256').update(fs.readFileSync(path.join(__dirname,'../EditHere.html'))).digest('hex'));
});

function draftProject(name = '游戏主页') {
  return {schemaVersion:'edithere-web-project-1',tool:'EditHere Web',savedAt:new Date().toISOString(),kind:'image',name,width:1,height:1,
    document:{id:'frame-1',timestampUs:null,image,notes:[{id:'note-1',source:null,text:'增加说明'}],layout:null}};
}
test('drafts restore without duplicating the image and do not overwrite a completed session from another process', () => {
  const paths = fixture(), first = new AgentSessions({...paths,credentials:false}), opened = create(first,paths);
  const second = new AgentSessions({...paths,credentials:false});
  assert.equal(fs.existsSync(path.join(paths.directory,'server-key')),false);
  first.saveDraft(token(opened),draftProject());
  const resumed = second.byId(opened.sessionId);
  assert.deepEqual(hydratedDraft(resumed),draftProjectWithDate(resumed.draft.savedAt));
  assert.equal(resumed.draft.document.image,undefined);
  second.submit(token(opened),feedback);
  assert.throws(() => first.saveDraft(token(opened),draftProject()),/completed/);
  assert.equal(first.byId(opened.sessionId).state,'completed');
  assert.equal(readSessionStatus({directory:paths.directory,sessionId:opened.sessionId}).state,'completed');
  assert.equal(fs.existsSync(path.join(paths.directory,'server-key')),false);
});
function draftProjectWithDate(savedAt) {return {...draftProject(),savedAt};}

test('draft validation rejects another image, unknown fields and invalid layout references', () => {
  const paths=fixture(),store=new AgentSessions(paths),opened=create(store,paths),project=draftProject();
  for (const invalid of [
    {...project,unexpected:'field'},
    {...project,document:{...project.document,image:image+'invalid'}},
    {...project,document:{...project.document,notes:[{id:'note-1',source:null,text:'test',groupId:'missing'}]}},
    {...project,document:{...project.document,layout:{width:1,height:1,pieces:[],groups:[]}}}
  ]) assert.throws(()=>store.saveDraft(token(opened),invalid));
  assert.equal(store.byId(opened.sessionId).draft,undefined);
  assert.equal(fs.existsSync(paths.output),false);
});

test('reading a completed receipt checks the file and needs neither HTTP nor server credentials', () => {
  const paths=fixture(),store=new AgentSessions(paths),opened=create(store,paths);
  store.submit(token(opened),feedback);
  const key=fs.readFileSync(path.join(paths.directory,'server-key'),'utf8');
  const stat=fs.statSync(path.join(paths.directory,opened.sessionId+'.json'));
  const saved=readSessionStatus({directory:paths.directory,sessionId:opened.sessionId});
  assert.equal(saved.state,'completed');assert.deepEqual(saved.feedback.objects,feedback.objects);
  assert.equal(fs.readFileSync(path.join(paths.directory,'server-key'),'utf8'),key);
  assert.equal(fs.statSync(path.join(paths.directory,opened.sessionId+'.json')).mtimeMs,stat.mtimeMs);
  fs.writeFileSync(paths.output,'external edit');
  assert.throws(()=>readSessionStatus({directory:paths.directory,sessionId:opened.sessionId}),/changed after submission/);
});

test('a crashed save lock recovers, while a live writer is respected', () => {
  const paths=fixture(),store=new AgentSessions(paths),opened=create(store,paths);
  const file=path.join(paths.directory,opened.sessionId+'.lock');
  fs.writeFileSync(file,JSON.stringify({pid:process.pid,token:'live-writer'}));
  assert.throws(()=>store.saveDraft(token(opened),draftProject()),/being saved/);
  assert.equal(JSON.parse(fs.readFileSync(file,'utf8')).token,'live-writer');
  const exited=spawnSync(process.execPath,['-e',''],{windowsHide:true,stdio:'pipe'});
  assert.equal(exited.status,0);
  fs.writeFileSync(file,JSON.stringify({pid:exited.pid,token:'crashed-writer'}));
  assert.equal(store.saveDraft(token(opened),draftProject()).state,'pending');
  assert.equal(fs.existsSync(file),false);
  assert.equal(fs.existsSync(paths.output),false);
});

test('persistent startup and token lookup retain only requested historical sessions', () => {
  const paths=fixture(),store=new AgentSessions({...paths,credentials:false});
  const one=create(store,paths),two=create(store,{...paths,output:path.join(paths.directory,'second-result.json')});
  const restarted=new AgentSessions({...paths,credentials:false});
  assert.equal(restarted.sessions.size,0);
  assert.equal(restarted.browser(token(one)).image,image);
  assert.deepEqual([...restarted.sessions.keys()],[one.sessionId]);
  assert.equal(restarted.byId(two.sessionId).id,two.sessionId);
  assert.equal(restarted.sessions.size,2);
  assert.equal(fs.existsSync(path.join(paths.directory,'server-key')),false);
});
