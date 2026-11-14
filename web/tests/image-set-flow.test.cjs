'use strict';
// Real browser acceptance: project screenshots -> per-page feedback -> replace
// one screen -> second review, plus the offline file/bundle path.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {once}=require('node:events');
const {pathToFileURL}=require('node:url');
const root=path.resolve(process.env.EDITHERE_IMAGE_SET_ROOT||path.join(__dirname,'../..'));
const {snapshotManifest,writeManifest,upsertPage,bundledProject,VERSION}=require(path.join(root,'web/image-set.cjs'));
const {createPreviewServer}=require(path.join(root,'web/preview-server.cjs'));
const {chromium}=require(process.env.EDITHERE_PLAYWRIGHT_MODULE||path.resolve(root,'../EditHere宣传/动画/node_modules/playwright-core'));
const executablePath=process.env.EDITHERE_BROWSER_PATH||'C:/Program Files/Google/Chrome/Application/chrome.exe';
const output=path.join(root,'web/tests/output/image-set-flow',Date.now().toString());
async function main(){
  fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({executablePath,headless:true});
  const context=await browser.newContext({viewport:{width:1280,height:850},acceptDownloads:true});
  const source=await context.newPage();
  const images=[];
  for(const [id,title,color]of [['home','Project overview','#426ce9'],['settings.preferences','Settings','#277b75']]){
    await source.setContent(`<style>body{margin:0;background:#f1f4fa;color:#263449;font:18px system-ui}aside{position:absolute;width:190px;height:100%;background:white;padding:24px;box-sizing:border-box}main{margin-left:220px;padding:36px}h1{font-size:32px}.card{background:white;border-radius:16px;padding:28px;margin:24px 0}button{background:${color};color:white;padding:14px 24px;border:0;border-radius:9px;font:inherit}</style><aside><b>Project Studio</b><p>Overview</p><p>Settings</p><p>Activity</p></aside><main><h1>${title}</h1><p>Review the working application</p><div class=card><h2>${title==='Settings'?'Workspace preferences':'Recent projects'}</h2><p>Shared results are ready for annotation.</p><button>${title==='Settings'?'Save preferences':'Open project'}</button></div></main>`);
    const file=path.join(output,id+'.png');await source.screenshot({path:file});images.push(file);
  }
  const manifest=path.join(output,'project.json');writeManifest(manifest,{schemaVersion:VERSION,name:'Project Studio review',pages:[]},{create:true});
  upsertPage(manifest,{id:'home',name:'Home',imagePath:images[0]});upsertPage(manifest,{id:'settings.preferences',name:'Settings',imagePath:images[1]});
  const {server,sessions}=createPreviewServer({port:0,directory:path.join(output,'sessions')});server.listen(0,'127.0.0.1');await once(server,'listening');sessions.origin='http://127.0.0.1:'+server.address().port;
  const errors=[];const page=await context.newPage();page.on('pageerror',error=>errors.push(String(error)));
  async function loadReview(filename){const created=sessions.create({...snapshotManifest(manifest),output:path.join(output,filename)});await page.goto(created.url);await page.waitForFunction(()=>document.body.dataset.agentState==='pending'&&document.querySelectorAll('[data-page]').length===2);return sessions.byId(created.sessionId);}
  async function editSettings(){await page.locator('[data-page="settings.preferences"]').click();if(await page.locator('body').getAttribute('data-inspector-open')!=='true')await page.locator('#inspect-toggle').click();await page.locator('#global').click();await page.locator('#note-text').fill('Move the save button closer to the title');await page.locator('#note-form [type="submit"]').click();await page.locator('#note-dialog').waitFor({state:'hidden'});}
  try{
    let session=await loadReview('feedback-round1.json');await editSettings();
    await page.screenshot({path:path.join(output,'web-set-review.png')});
    await page.locator('#export').click();await page.waitForFunction(()=>document.body.dataset.agentState==='completed');
    const feedback=JSON.parse(fs.readFileSync(session.output,'utf8'));
    assert.equal(feedback.pages.length,1);assert.equal(feedback.pages[0].id,'settings.preferences');assert.equal(feedback.pageIndex[0].annotated,false);assert.equal(feedback.pageIndex[0].imageFile,undefined);
    assert.equal(JSON.stringify(sessions.status(sessions.byId(session.id))).includes('base64'),false);
    await source.setContent('<body style="background:#e8f3ef;font:28px system-ui;padding:60px"><h1>Settings updated</h1><button>Save preferences</button></body>');
    const nextImage=path.join(output,'settings-v2.png');await source.screenshot({path:nextImage});upsertPage(manifest,{id:'settings.preferences',imagePath:nextImage});
    const next=snapshotManifest(manifest);assert.equal(next.pages.length,2);assert.equal(next.pages[0].image,snapshotManifest(manifest).pages[0].image);
    session=await loadReview('feedback-round2.json');assert.equal(await page.locator('#count').textContent(),'0');
    await page.locator('#export').click();await page.waitForFunction(()=>document.body.dataset.agentState==='completed');
    assert.equal(JSON.parse(fs.readFileSync(session.output,'utf8')).pages.length,0,'completing a clean review does not invent feedback or attach untouched images');
    const offline=await context.newPage();offline.on('pageerror',error=>errors.push(String(error)));offline.on('dialog',dialog=>dialog.accept());
    const projectFile=path.join(output,'offline-project.json');fs.writeFileSync(projectFile,JSON.stringify(bundledProject(manifest)));
    await offline.goto(sessions.origin+'/EditHere.html');await offline.locator('#file-input').setInputFiles(projectFile);await offline.locator('[data-page="settings.preferences"]').click();
    if(await offline.locator('body').getAttribute('data-inspector-open')!=='true')await offline.locator('#inspect-toggle').click();
    await offline.locator('#global').click();await offline.locator('#note-text').fill('Keep the updated settings layout');await offline.locator('#note-form [type="submit"]').click();await offline.locator('#note-dialog').waitFor({state:'hidden'});
    await offline.locator('#export').click();const downloadPromise=offline.waitForEvent('download');await offline.locator('#download-json').click();const download=await downloadPromise,downloadPath=path.join(output,download.suggestedFilename());await download.saveAs(downloadPath);
    let value;
    if(downloadPath.endsWith('.zip')){const B=require(path.join(root,'web/src/bundle.js'));const entries=B.unzip(new Uint8Array(fs.readFileSync(downloadPath)));const json=[...entries].find(([name])=>name.endsWith('.json'));value=JSON.parse(new TextDecoder().decode(json[1]));assert.equal([...entries.keys()].filter(name=>/\.(png|jpe?g)$/.test(name)).length,1);}
    else value=JSON.parse(fs.readFileSync(downloadPath,'utf8'));
    assert.equal(value.pages.length,1);assert.equal(value.pages[0].id,'settings.preferences');
    await offline.screenshot({path:path.join(output,'offline-set-export.png')});
    const {startServer}=await import(pathToFileURL(path.join(root,'dsh/server.mjs')).href);
    const dsh=await startServer({port:0,directory:path.join(output,'dsh-sessions')});
    const dshPage=await context.newPage();dshPage.on('pageerror',error=>errors.push(String(error)));
    try{
      const api='http://127.0.0.1:'+dsh.port,body={name:'Project Studio DSH',output:path.join(output,'dsh-feedback.json'),timeoutSeconds:600,images:snapshotManifest(manifest).pages.map(page=>({id:page.id,name:page.name,dataUrl:page.image}))};
      const response=await fetch(api+'/__edithere_dsh/sessions',{method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer '+dsh.store.key},body:JSON.stringify(body)});assert.equal(response.status,201);const created=await response.json();
      await dshPage.goto(created.url);await dshPage.waitForFunction(()=>document.body.dataset.dshState==='pending');await dshPage.locator('[data-page="settings.preferences"]').click();
      if(await dshPage.locator('body').getAttribute('data-inspector-open')!=='true')await dshPage.locator('#inspect-toggle').click();
      await dshPage.locator('#global').click();await dshPage.locator('#note-text').fill('Keep the settings title visible');await dshPage.locator('#note-form [type="submit"]').click();await dshPage.locator('#note-dialog').waitFor({state:'hidden'});
      await dshPage.locator('#export').click();await dshPage.waitForFunction(()=>document.body.dataset.dshState==='completed');
      const saved=JSON.parse(fs.readFileSync(body.output,'utf8'));assert.equal(saved.pages.length,1);assert.equal(saved.pages[0].id,'settings.preferences');assert.equal(saved.pageIndex[0].imageFile,undefined);
      assert(fs.existsSync(path.resolve(path.dirname(body.output),saved.pages[0].imageFile)));assert(fs.existsSync(path.resolve(path.dirname(body.output),saved.pages[0].annotatedImage)));assert.equal(JSON.stringify(saved).includes('base64'),false);
    }finally{await dshPage.close();await new Promise(resolve=>dsh.server.close(resolve));}
    assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'acceptance.json'),JSON.stringify({ok:true,root,manifest,firstFeedback:'feedback-round1.json',secondFeedback:'feedback-round2.json',offlineExport:downloadPath},null,2));
    console.log(JSON.stringify({ok:true,root,output,checks:['annotated page only','no image payload in receipt','stable page replacement','empty second review','offline round trip','no browser errors']}));
  }finally{await context.close();await browser.close();await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
