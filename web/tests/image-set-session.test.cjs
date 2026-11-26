'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {VERSION,MAX_PAGES,readManifest,writeManifest,upsertPage,upsertPages,snapshotManifest,bundledProject} = require('../image-set.cjs');
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

test('a transient manifest lock preserves the previous review until atomic replacement succeeds',context=>{
  const {manifest,dir}=fixture(context),before=fs.readFileSync(manifest,'utf8');
  const next=readManifest(manifest);next.name='Next review';
  const rename=fs.renameSync;let locks=2;
  context.after(()=>{fs.renameSync=rename;});
  fs.renameSync=(from,to)=>{
    if(to===manifest && locks-->0){
      assert.equal(fs.readFileSync(manifest,'utf8'),before);
      throw Object.assign(new Error('temporary sharing lock'),{code:'EPERM'});
    }
    return rename(from,to);
  };
  writeManifest(manifest,next);
  assert.equal(readManifest(manifest).name,'Next review');
  assert.deepEqual(fs.readdirSync(dir).filter(name=>name.endsWith('.tmp')),[]);
  const committed=fs.readFileSync(manifest,'utf8');
  fs.renameSync=()=>{throw Object.assign(new Error('persistent sharing lock'),{code:'EBUSY'});};
  assert.throws(()=>writeManifest(manifest,{...next,name:'Must not commit'}),/persistent sharing lock/);
  assert.equal(fs.readFileSync(manifest,'utf8'),committed);
  assert.deepEqual(fs.readdirSync(dir).filter(name=>name.endsWith('.tmp')),[]);
});
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

for (const [label,relative] of [
  ['desktop','skills/edithere'],['web','web/skills/edithere-web'],
  ['connector','connector/skills/edithere'],['trae','trae-plugin/skills/edithere'],
  ['dsh','dsh/skill/edithere-dsh']
]) test(`the ${label} skill maintains review rounds outside the source repository`,context=>{
  const {dir,image}=fixture(context),portable=path.join(dir,'standalone-skill');
  fs.cpSync(path.resolve(__dirname,'../..',relative),portable,{recursive:true});
  const cli=path.join(portable,'scripts/image-set.mjs'),manifest=path.join(dir,'review.json');
  const run=(...args)=>JSON.parse(execFileSync(process.execPath,[cli,...args],{cwd:portable,encoding:'utf8',windowsHide:true}));
  run('create',manifest,'--name','Review rounds');
  run('upsert',manifest,'--id','home','--name','Home','--image',image);
  run('upsert',manifest,'--id','settings.ai','--name','AI settings','--image',image);
  const first=run('list',manifest),roundOne=path.join(dir,'round-one.json');
  run('bundle',manifest,'--output',roundOne);
  const firstProject=JSON.parse(fs.readFileSync(roundOne,'utf8'));
  const replacement=path.join(dir,'settings-v2.png');
  fs.copyFileSync(path.resolve(__dirname,'../../tests/fixtures/ocr-small-url.png'),replacement);
  run('upsert',manifest,'--id','settings.ai','--name','AI preferences','--image',replacement);
  const second=run('list',manifest),roundTwo=path.join(dir,'round-two.json');
  assert.deepEqual(second.pages.map(page=>page.id),['home','settings.ai']);
  assert.deepEqual(second.pages[0],first.pages[0]);
  assert.notEqual(second.pages[1].sha256,first.pages[1].sha256);
  assert.equal(run('bundle',manifest,'--output',roundTwo).pageCount,2);
  const secondProject=JSON.parse(fs.readFileSync(roundTwo,'utf8'));
  assert.equal(secondProject.pages[0].image,firstProject.pages[0].image);
  assert.notEqual(secondProject.pages[1].image,firstProject.pages[1].image);
  assert.deepEqual(JSON.parse(fs.readFileSync(roundOne,'utf8')),firstProject,'the reviewed round remains intact');
  assert.deepEqual(run('remove',manifest,'--id','settings.ai').pages.map(page=>page.id),['home']);
  assert(fs.existsSync(replacement),'removing a page reference preserves its screenshot');
});

test('upsertPages and upsert-batch update multiple pages atomically with compact stdout and strict validation',context=>{
  const {dir,image}=fixture(context),cli=path.resolve(__dirname,'../../scripts/image-set.mjs');
  const secondImg=path.join(dir,'play.png');
  fs.copyFileSync(path.resolve(__dirname,'../../tests/fixtures/ocr-small-url.png'),secondImg);
  const run=(...args)=>JSON.parse(execFileSync(process.execPath,[cli,...args],{encoding:'utf8',windowsHide:true}));

  const batchManifest=path.join(dir,'batch.json');
  assert.throws(()=>upsertPages(batchManifest,[{id:'home',name:'Home',imagePath:image}]),/exist/i);
  assert.throws(()=>upsertPages(batchManifest,[{id:'a',imagePath:image},{id:'a',imagePath:secondImg}],{createName:'Batch'}),/Duplicate page id/i);

  const created=run('upsert-batch',batchManifest,'--name','Game Batch','--page',`menu=${image}=Main Menu`,'--page',`play=${secondImg}=Gameplay`);
  assert.equal(created.ok,true);
  assert.equal(created.name,'Game Batch');
  assert.equal(created.pageCount,2);
  assert.equal(created.updatedCount,2);
  assert.deepEqual(created.pages.map(p=>p.id),['menu','play']);
  assert.equal(created.pages[0].sha256,undefined,'compact CLI stdout omits sha256 hashes');

  const onDisk=readManifest(batchManifest);
  assert.equal(onDisk.pages.length,2);
  assert.match(onDisk.pages[0].sha256,/^[a-f0-9]{64}$/);
  assert.match(onDisk.pages[1].sha256,/^[a-f0-9]{64}$/);

  const specPath=path.join(dir,'pages-spec.json');
  fs.writeFileSync(specPath,JSON.stringify({pages:[{id:'menu',image:secondImg},{id:'shop',name:'Item Shop',image:image}]}));
  const updated=run('sync',batchManifest,'--pages-json',specPath);
  assert.equal(updated.pageCount,3);
  assert.equal(updated.updatedCount,2);
  assert.deepEqual(updated.pages.map(p=>p.id),['menu','play','shop']);
  assert.equal(updated.pages[0].name,'Main Menu','updating without name preserves existing page name');

  const stdinRes=JSON.parse(execFileSync(process.execPath,[cli,'upsert-batch',batchManifest,'--pages-json','-','--full'],{
    input:JSON.stringify([{id:'play',name:'Battle HUD',image}]),
    encoding:'utf8',
    windowsHide:true,
  }));
  assert.equal(stdinRes.pages[1].name,'Battle HUD');
  assert.match(stdinRes.pages[1].sha256,/^[a-f0-9]{64}$/,'--full returns full manifest hashes');

  const scanDir=path.join(dir,'shots');
  fs.mkdirSync(scanDir,{recursive:true});
  fs.copyFileSync(image,path.join(scanDir,'02_Combat HUD.png'));
  fs.copyFileSync(secondImg,path.join(scanDir,'01_Menu!1.png'));
  fs.copyFileSync(image,path.join(scanDir,'01_Menu@1.png'));
  fs.writeFileSync(path.join(scanDir,'notes.txt'),'ignored');
  const dirManifest=path.join(dir,'from-dir.json');
  const fromDir=run('upsert-batch',dirManifest,'--name','Scanned','--from-dir',scanDir);
  assert.deepEqual(fromDir.pages.map(p=>p.id),['01_Menu-1','01_Menu-1-2','02_Combat-HUD']);
  assert.equal(fromDir.pages[2].name,'02_Combat HUD');

  assert.throws(()=>execFileSync(process.execPath,[cli,'upsert-batch',path.join(dir,'missing.json'),'--page',`x=${image}`],{encoding:'utf8',stdio:'pipe',windowsHide:true}));

  const tooMany=Array.from({length:MAX_PAGES+1},(_,i)=>({id:`p${i}`,imagePath:image}));
  assert.throws(()=>upsertPages(path.join(dir,'overflow.json'),tooMany,{createName:'TooBig'}),/Invalid EditHere image set manifest/i);

  const portable=path.join(dir,'portable-skill');
  fs.cpSync(path.resolve(__dirname,'../../skills/edithere'),portable,{recursive:true});
  const portableCli=path.join(portable,'scripts/image-set.mjs');
  const portableManifest=path.join(dir,'portable-batch.json');
  const portableOut=JSON.parse(execFileSync(process.execPath,[portableCli,'upsert-batch',portableManifest,'--name','Portable','--page',`one=${image}=One`,'--page',`two=${secondImg}=Two`],{cwd:portable,encoding:'utf8',windowsHide:true}));
  assert.equal(portableOut.ok,true);
  assert.equal(portableOut.pageCount,2);
});
