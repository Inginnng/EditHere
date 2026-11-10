'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {VERSION,readManifest,writeManifest,upsertPage,snapshotManifest,bundledProject} = require('../image-set.cjs');
const {AgentSessions,readSessionStatus,hydratedDraft} = require('../agent-session.cjs');
const F = require('../src/feedback.js');
const PNG='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';
function fixture(context) {
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'edithere-image-set-'));
  context.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
  const manifest=path.join(dir,'project.json'),image=path.join(dir,'home.png');
  fs.writeFileSync(image,Buffer.from(PNG.split(',')[1],'base64'));
  writeManifest(manifest,{schemaVersion:VERSION,name:'Desktop project',pages:[]},{create:true});
  upsertPage(manifest,{id:'home',name:'Home',imagePath:image});
  upsertPage(manifest,{id:'settings',name:'Settings',imagePath:image});
  return {dir,manifest,image};
}
function feedback(snapshot) {
  return F.exportSet({name:snapshot.name,pageIndex:snapshot.pages.map(page=>({id:page.id,name:page.name,width:1,height:1})),pages:[{id:'settings',name:'Settings',width:1,height:1,feedback:F.exportImage({image:PNG,imageFile:'page-2-Settings.png',notes:[{source:null,text:'Improve the settings title'}],movements:[]},'embedded')}],groupNotes:[{text:'Use consistent spacing'}]});
}
test('AI upsert preserves page IDs, order and untouched screenshots; bundle retains all pages',context=>{
  const {manifest,image}=fixture(context),before=readManifest(manifest);
  const replacement=path.join(path.dirname(image),'settings-v2.png');fs.copyFileSync(image,replacement);
  upsertPage(manifest,{id:'settings',name:'Preferences',imagePath:replacement});
  const after=readManifest(manifest);
  assert.equal(after.pages.length,2);assert.deepEqual(after.pages[0],before.pages[0]);assert.equal(after.pages[1].id,'settings');assert.equal(after.pages[1].name,'Preferences');
  const project=bundledProject(manifest);assert.equal(project.kind,'set');assert.deepEqual(project.pages.map(page=>page.id),['home','settings']);assert.equal(project.pages[1].width,1);
  assert.throws(()=>writeManifest(manifest,before,{create:true}),/exist/i);
  fs.appendFileSync(replacement,'changed');assert.throws(()=>snapshotManifest(manifest),/changed/i);
});
test('review snapshots, drafts and receipts survive restart and only annotated images return',context=>{
  const {dir,manifest,image}=fixture(context),snapshot=snapshotManifest(manifest),directory=path.join(dir,'sessions'),output=path.join(dir,'feedback.json');
  const sessions=new AgentSessions({directory,credentials:false});
  const created=sessions.create({...snapshot,output});let session=sessions.byId(created.sessionId);
  assert.deepEqual(sessions.browser(session.token).pages.map(page=>page.id),['home','settings']);
  const project=bundledProject(manifest);project.pages[1].notes=[{id:'n1',source:null,text:'Improve the settings title'}];project.groupNotes=[{id:'g1',text:'Use consistent spacing'}];
  sessions.saveDraft(session.token,project);
  const restarted=new AgentSessions({directory,credentials:false});session=restarted.byId(session.id);
  assert.equal(session.draft.pages[0].image,undefined);assert.equal(hydratedDraft(session).pages[1].image,PNG);
  fs.appendFileSync(image,'new-page-content');
  assert.equal(session.pages[0].image,PNG,'an active review keeps the reviewed bytes');
  const value=feedback(snapshot),receipt=restarted.submit(session.token,value);
  assert.equal(receipt.state,'completed');assert.equal(JSON.stringify(receipt).includes('base64'),false);
  const saved=JSON.parse(fs.readFileSync(output,'utf8'));assert.equal(saved.pages.length,1);assert.equal(saved.pages[0].id,'settings');assert.equal(saved.pageIndex[0].annotated,false);assert.equal(saved.pageIndex[0].imageFile,undefined);
  assert.deepEqual(readSessionStatus({directory,sessionId:session.id}),receipt);
  assert.deepEqual(restarted.submit(session.token,value),receipt);
  const malicious=structuredClone(value);malicious.pages[0].feedback.image=PNG.replace('ScLbt','ScLbu');assert.throws(()=>restarted.submit(session.token,malicious),/original image/i);
});
test('cancellation and whole-project comments never attach unannotated images',context=>{
  const {dir,manifest}=fixture(context),snapshot=snapshotManifest(manifest),sessions=new AgentSessions({directory:path.join(dir,'sessions'),credentials:false});
  const created=sessions.create({...snapshot,output:path.join(dir,'cancelled.json')});const session=sessions.byId(created.sessionId);
  sessions.cancel(session);assert.equal(fs.existsSync(session.output),false);
  const another=sessions.create({...snapshot,output:path.join(dir,'group-only.json')});const pending=sessions.byId(another.sessionId);
  const value=F.exportSet({name:snapshot.name,pageIndex:snapshot.pages.map(page=>({id:page.id,name:page.name,width:1,height:1})),pages:[],groupNotes:[{text:'Use dark mode'}]});
  const receipt=sessions.submit(pending.token,value);assert.equal(receipt.feedback.pages.length,0);assert.equal(JSON.stringify(receipt).includes('base64'),false);
});
test('manifest CLI can create, replace, bundle and remove a page using its stable ID',context=>{
  const {dir,image}=fixture(context),manifest=path.join(dir,'cli.json'),cli=path.resolve(__dirname,'../../scripts/image-set.mjs');
  const run=(...args)=>JSON.parse(execFileSync(process.execPath,[cli,...args],{encoding:'utf8',windowsHide:true}));
  assert.equal(run('create',manifest,'--name','CLI project').ok,true);
  run('upsert',manifest,'--id','home','--image',image);run('upsert',manifest,'--id','home','--image',image,'--name','Home v2');
  assert.equal(run('list',manifest).pages.length,1);
  const output=path.join(dir,'offline.json');assert.equal(run('bundle',manifest,'--output',output).pageCount,1);assert.equal(JSON.parse(fs.readFileSync(output)).kind,'set');
  assert.equal(run('remove',manifest,'--id','home').pages.length,0);
});
