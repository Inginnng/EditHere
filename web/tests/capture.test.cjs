'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const F=require('../src/feedback.js'),{AgentSessions,readSessionStatus,hydratedDraft}=require('../agent-session.cjs'),{options}=require('../agent.cjs');
const image=require('./image-fixture.cjs').pngData(12,8);
const capture={source:'file',fileName:'游戏主页.png',capturedAt:'2026-10-17T09:10:00+08:00',page:'游戏主页面',pageSource:'agent'};
const document={image,width:12,height:8,movements:[],notes:[{source:null,text:'按钮加大'}]};
function fixture(extras={}){const directory=path.join(__dirname,'output/capture',crypto.randomUUID()),output=path.join(directory,'result.json'),store=new AgentSessions({directory,now:()=>Date.parse(capture.capturedAt)}),created=store.create({image,inputPath:path.join(directory,'original.png'),output,...extras});return {directory,output,store,created,token:new URL(created.url).searchParams.get('agent')};}

test('capture describes imports without requiring page identification, paths or a new schema version',()=>{
  for(const source of ['screen','file','clipboard','demo','scroll','history','pin','drop','video'])assert.deepEqual(F.validateCapture({source,capturedAt:capture.capturedAt}),{source,capturedAt:capture.capturedAt});
  const complete={...capture,windowTitle:'',appName:'游戏',fileName:'C:标题'};assert.equal(F.validateCapture(complete),complete);
  assert.equal(F.validateCapture({...capture,page:' 游戏主页 '}).page,' 游戏主页 ');
  for(const mode of ['file','embedded','none']){const value=F.exportImage({...document,capture},mode);assert.equal(value.schemaVersion,'feedback-minimal-3');assert.deepEqual(value.capture,capture);assert.deepEqual(F.normalizeImage(value).capture,capture);assert.deepEqual(JSON.parse(F.preview(value)).capture,capture);}
  assert.equal(F.normalizeImage(F.exportImage(document)).capture,undefined);assert.deepEqual(F.normalizeImage({objects:[]}),{annotationSpace:'result',objects:[]});assert.throws(()=>F.normalizeImage({objects:[],capture}),/requires feedback-minimal-2/);
});

test('capture rejects malformed dates, filenames, page attribution and unknown fields',()=>{
  const invalid=[null,[],{}, {...capture,source:'ai'}, {...capture,extra:true}, {...capture,capturedAt:'2026-02-30T00:00:00Z'}, {...capture,capturedAt:'2026-10-17T24:00:00Z'}, {...capture,capturedAt:'2026-10-17T09:10:00'}, {...capture,fileName:'../image.png'}, {...capture,fileName:'C:\\image.png'}, {...capture,fileName:'\0'}, {...capture,fileName:''}, {...capture,fileName:'x'.repeat(1025)}, {...capture,page:' '}, {...capture,page:'x'.repeat(501)}, {...capture,pageSource:'recognized'}, {...capture,page:undefined}, {...capture,pageSource:undefined}, {...capture,appName:'x'.repeat(1001)}];
  for(const value of invalid)assert.throws(()=>F.validateCapture(value),undefined,JSON.stringify(value));
  for(const value of ['', ' ',null,12,'x'.repeat(501)])assert.throws(()=>F.validatePageContext(value));
  assert.equal(F.validatePageContext(' 游戏主页面 '),'游戏主页面');
  assert(F.timestampWithZone('2026-10-17T09:10:00.123456789+08:00'));assert(!F.timestampWithZone('2026-10-17T09:10:00.1234567890+08:00'));
});

test('video export retains project defaults and each frame independently',()=>{
  const other={...capture,page:'游戏暂停页面'},frames=[{...document,id:'frame-a',timestampUs:0,capture},{...document,id:'frame-b',timestampUs:1000000,capture:other}];
  const value=F.exportVideo({source:'game.mp4',durationMs:2000,capture,frames});assert.deepEqual(value.capture,capture);assert.deepEqual(value.frames.map(frame=>frame.feedback.capture),[capture,other]);
});

test('Agent context stays separate from the image name and persists through drafts, submit, restart and recovery',()=>{
  const run=fixture({name:'批注任务',pageContext:' 游戏主页面 '});const session=run.store.byId(run.created.sessionId),expected={source:'file',fileName:'original.png',capturedAt:new Date(Date.parse(capture.capturedAt)).toISOString(),page:'游戏主页面',pageSource:'agent'};
  assert.deepEqual(session.capture,expected);assert.equal(session.name,'批注任务');assert.deepEqual(run.store.browser(run.token).capture,expected);
  const project={schemaVersion:'edithere-web-project-1',tool:'EditHere Web',savedAt:capture.capturedAt,kind:'image',name:session.name,width:12,height:8,document:{id:'image-1',timestampUs:null,image,notes:[{id:'note-1',source:null,text:'按钮加大'}],layout:null}};
  run.store.saveDraft(run.token,project);const resumed=new AgentSessions({directory:run.directory}).byId(session.id);assert.deepEqual(hydratedDraft(resumed).document.capture,expected);assert.equal(resumed.draft.document.image,undefined);
  const feedback=F.exportImage(document,'none'),receipt=run.store.submit(run.token,feedback);assert.deepEqual(receipt.capture,expected);assert.deepEqual(receipt.feedback.capture,expected);assert.deepEqual(JSON.parse(fs.readFileSync(run.output,'utf8')).capture,expected);assert.deepEqual(readSessionStatus({directory:run.directory,sessionId:session.id}),receipt);
  const raw=run.store.byId(session.id);raw.state='submitting';run.store.persist(raw);assert.deepEqual(new AgentSessions({directory:run.directory}).status(run.store.byId(session.id)),receipt);
});

test('a submitted page cannot silently replace immutable session context',()=>{
  const run=fixture({pageContext:'游戏主页面'}),session=run.store.byId(run.created.sessionId),feedback=F.exportImage({...document,capture:{...session.capture,page:'账户页'}},'none');
  assert.throws(()=>run.store.submit(run.token,feedback),/Capture context must match/);assert.equal(fs.existsSync(run.output),false);
  const project={schemaVersion:'edithere-web-project-1',tool:'EditHere Web',savedAt:capture.capturedAt,kind:'image',name:session.name,width:12,height:8,document:{id:'a',timestampUs:null,image,notes:[],layout:null,capture:feedback.capture}};
  assert.throws(()=>run.store.saveDraft(run.token,project),/Capture context must match/);
});

test('sessions created before capture metadata remain readable and submittable',()=>{
  const run=fixture(),session=run.store.byId(run.created.sessionId);delete session.capture;run.store.persist(session);const legacy={annotationSpace:'result',image,objects:[{source:null,movements:[],annotations:['按钮加大']}]};
  const receipt=run.store.submit(run.token,legacy);assert.equal(receipt.capture,undefined);assert.equal(receipt.feedback.capture,undefined);assert.deepEqual(JSON.parse(fs.readFileSync(run.output,'utf8')),legacy);assert.deepEqual(readSessionStatus({directory:run.directory,sessionId:session.id}),receipt);
});

test('CLI accepts the page option as a separate data argument',()=>{
  assert.equal(options(['start','--page-context','游戏主页面'])['page-context'],'游戏主页面');
  assert.throws(()=>options(['start','--page-context','a','--page-context','b']),/duplicate/);
});
