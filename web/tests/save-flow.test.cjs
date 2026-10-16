'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const {downloadText}=require('./export-test-utils.cjs');
const web=path.resolve(__dirname,'..'),output=path.join(__dirname,'output/save-flow');
const {chromium}=require(path.resolve(web,'../../EditHere宣传/动画/node_modules/playwright-core'));
const executablePath=process.env.EDITHERE_BROWSER_PATH||'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe';
async function main(){
  fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({executablePath,headless:true});
  const context=await browser.newContext({offline:true,acceptDownloads:true,viewport:{width:480,height:720}});
  const page=await context.newPage(),errors=[],downloads=[];page.on('pageerror',error=>errors.push(error.stack));page.on('dialog',dialog=>dialog.accept());page.on('download',download=>downloads.push(download.suggestedFilename()));
  // Isolate clipboard and preferences from the user's actual browser.
  await page.addInitScript(()=>Object.defineProperty(navigator,'clipboard',{value:{writeText:async text=>{if(window.rejectCopy)throw Error('denied');window.copiedJSON=text;}}}));
  const url=pathToFileURL(path.join(web,'EditHere.html')).href;
  async function note(){await page.locator('#demo').click();await page.locator('#inspect-toggle').click();await page.locator('#global').click();await page.locator('#note-text').fill('游戏主页更清晰 ✨');await page.locator('#note-form [type="submit"]').click();}
  async function menu(){await page.locator('#save-options-toggle').click();await page.locator('#save-options').waitFor({state:'visible'});}
  async function viewJson(){
    await menu();await page.locator('#save-settings').click();
    assert(await page.locator('#export-dialog').isHidden(),'selecting view mode must not open the viewer');
    assert.equal(await page.locator('#save-json').getAttribute('aria-label'),'查看 JSON 文件');
    await page.locator('#save-json').click();await page.locator('#export-dialog').waitFor({state:'visible'});
  }
  async function utility(){if(await page.locator('#utility-toggle').isVisible()&&await page.locator('#utility-toggle').getAttribute('aria-expanded')!=='true')await page.locator('#utility-toggle').click();}
  try{
    await page.goto(url);await page.evaluate(()=>localStorage.clear());await page.reload();
    assert(await page.locator('#save-json').isDisabled());assert.equal(await page.locator('#offline,#actual-size').count(),0);
    await note();await menu();await page.locator('#save-options [data-save-mode="file"]').click();
    assert.equal(downloads.length,0,'selecting file mode must not download');
    const initial=await downloadText(page,'#save-json');
    assert.equal(initial,await page.evaluate(()=>EditHereFeedback.serialize(EditHereWeb.feedback())));
    assert(JSON.parse(initial).image.startsWith('data:image/png;base64,'));
    const imageFeedback=JSON.parse(initial);
    assert.equal(imageFeedback.schemaVersion,'feedback-minimal-2');
    assert.equal(imageFeedback.$schema,'urn:edithere:feedback-minimal:2');
    assert.deepEqual(imageFeedback.tool,{name:'EditHere',version:await page.locator('meta[name="edithere-version"]').getAttribute('content')});
    assert.match(imageFeedback.exportedAt,/^\d{4}-\d{2}-\d{2}T.*Z$/);
    await menu();await page.locator('#save-options [data-save-mode="content"]').click();
    assert.equal(await page.evaluate(()=>window.copiedJSON),undefined,'selecting copy mode must not copy');assert.equal(downloads.length,1);
    assert.equal(await page.locator('#save-json').evaluate(el=>el===document.activeElement),true);
    await page.locator('#save-json').click();
    assert.equal(await page.evaluate(()=>window.copiedJSON),initial);assert(await page.locator('#save-options').isHidden());
    assert.equal(await page.locator('#save-json').getAttribute('aria-label'),'复制 JSON 内容');
    await page.reload();assert.equal(await page.locator('body').getAttribute('data-save-mode'),'content');await note();
    await viewJson();await page.locator('#embed').uncheck();
    assert((await page.locator('#export-hint').textContent()).includes('不能单独恢复'));
    const light=await downloadText(page);assert(!Object.hasOwn(JSON.parse(light),'image'));assert(Buffer.byteLength(light)<Buffer.byteLength(initial)/10);
    await page.locator('#copy-json').click();assert.equal(await page.evaluate(()=>window.copiedJSON),light);
    await page.locator('[data-close="export-dialog"]').click();
    await page.reload();assert.equal(await page.locator('#embed').isChecked(),false);assert.equal(await page.locator('body').getAttribute('data-save-mode'),'content');await note();
    await page.evaluate(()=>{window.rejectCopy=true;});await page.locator('#save-json').click();
    await page.locator('#export-dialog').waitFor({state:'visible'});
    assert.equal(JSON.parse(await page.locator('#json-preview').inputValue()).objects[0].annotations[0].text,'游戏主页更清晰 ✨');
    assert((await page.locator('#export-hint').textContent()).includes('剪贴板不可用'));
    await page.locator('[data-close="export-dialog"]').click();
    await menu();const downloadsBefore=downloads.length;await page.locator('#save-options [data-save-mode="project"]').click();
    assert.equal(downloads.length,downloadsBefore,'selecting project mode must not download');
    const pending=page.waitForEvent('download');await page.locator('#save-json').click();
    const projectDownload=await pending;assert(projectDownload.suggestedFilename().endsWith('.edithere-web.json'));
    const stream=await projectDownload.createReadStream(),chunks=[];for await(const chunk of stream)chunks.push(chunk);
    assert.equal(JSON.parse(Buffer.concat(chunks)).schemaVersion,'edithere-web-project-1');assert.equal(await page.locator('body').getAttribute('data-save-mode'),'project');
    await utility();assert.equal(await page.locator('#save-project use').getAttribute('href'),'#i-save');
    await page.locator('#help').click();
    for(const id of ['edit','share','recover']){await page.locator('#help-tab-'+id).click();assert(await page.locator('#help-panel-'+id).isVisible());}
    assert((await page.locator('#help-panel-recover').innerText()).includes('重试发送'));
    await page.evaluate(()=>{document.body.dataset.agentState='completed';document.body.dataset.messageState='failed';});
    await page.waitForFunction(()=>document.querySelector('#help-session-status').textContent.includes('重试发送'));
    await page.evaluate(()=>{document.body.dataset.messageState='unsupported';});
    await page.waitForFunction(()=>document.querySelector('#help-session-status').textContent.includes('复制反馈路径'));
    await page.evaluate(()=>{delete document.body.dataset.messageState;document.body.dataset.agentState='pending';});
    await page.waitForFunction(()=>document.querySelector('#help-session-status').textContent.includes('无法自动保存'));
    await page.evaluate(()=>{delete document.body.dataset.agentState;});
    await page.locator('#help-tab-recover').press('Home');assert.equal(await page.locator('#help-tab-edit').getAttribute('aria-selected'),'true');
    await page.screenshot({path:path.join(output,'tutorial-480.png')});await page.locator('[data-close="help-dialog"]').click();
    await page.locator('#toast').waitFor({state:'hidden'});
    await menu();await page.locator('#save-settings').click();
    await page.reload();assert.equal(await page.locator('body').getAttribute('data-save-mode'),'view');assert(await page.locator('#export-dialog').isHidden());await note();
    for(const [width,height] of [[320,390],[375,720],[480,720],[600,900],[900,390]]){
      await page.setViewportSize({width,height});await menu();
      const popup=await page.locator('#save-options').boundingBox();assert(popup.x>=0&&popup.x+popup.width<=width&&popup.y+popup.height<=height,'save menu stays inside '+width+'x'+height);
      await page.screenshot({path:path.join(output,'save-menu-'+width+'x'+height+'.png')});
      await page.locator('#save-options').press('Escape');
      await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
      const row=await page.locator('.header').evaluate(header=>{const r=header.getBoundingClientRect();return {y:r.y,height:r.height,width:innerWidth,groups:Array.from(header.children).map(el=>{const b=el.getBoundingClientRect();return {y:b.y,height:b.height,right:b.right};})};});
      assert(row.groups.every(g=>Math.abs(g.y-row.y)<1&&Math.abs(g.height-row.height)<1&&g.right<=width),'new save control keeps one header row');
      await utility();await page.locator('#help').click();await page.locator('#help-tab-recover').click();
      const help=await page.locator('#help-dialog').boundingBox();assert(help.x>=0&&help.y>=0&&help.x+help.width<=width&&help.y+help.height<=height);
      assert(await page.locator('#help-dialog').evaluate(el=>el.scrollWidth<=el.clientWidth));
      await page.screenshot({path:path.join(output,'help-'+width+'x'+height+'.png')});await page.locator('[data-close="help-dialog"]').click();
      await page.screenshot({path:path.join(output,'save-'+width+'x'+height+'.png')});
    }
    // Reopened video projects use the same export path, even without source
    // playback. Copying an unchanged snapshot must retain its exportedAt value.
    const videoProject=JSON.parse(Buffer.concat(chunks));videoProject.kind='video';videoProject.name='游戏片段.webm';
    videoProject.video={source:videoProject.name,durationMs:1000,width:videoProject.width,height:videoProject.height};
    videoProject.frames=[{...videoProject.document,timestampUs:0}];delete videoProject.document;
    await page.setViewportSize({width:480,height:720});await page.evaluate(()=>{window.rejectCopy=false;});
    await page.locator('#file-input').setInputFiles({name:'video.edithere-web.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(videoProject))});
    await page.waitForFunction(()=>document.querySelector('#media-meta').textContent.includes('视频'));
    await viewJson();await page.locator('#embed').check();
    const videoJSON=await downloadText(page);await page.locator('#copy-json').click();
    assert.equal(await page.evaluate(()=>window.copiedJSON),videoJSON,'video delivery retains the exact same snapshot');
    await page.locator('[data-close="export-dialog"]').click();
    if(await page.locator('body').getAttribute('data-inspector-open')!=='true')await page.locator('#inspect-toggle').click();
    await page.locator('#global').click();await page.locator('#note-text').fill('视频意见更新');await page.locator('#note-form [type="submit"]').click();
    await page.locator('#save-json').click();const updated=JSON.parse(await page.evaluate(()=>window.copiedJSON));
    assert.equal(updated.objects.length,2);assert.equal(updated.frames[0].feedback.objects.length,2);assert(updated.frames[0].feedback.image.startsWith('data:image/png;base64,'));
    assert.deepEqual(errors,[]);console.log('PASS Image/video file and copy bytes match; editing refreshes exports; preferences survive reload; no-image JSON is smaller; clipboard denial preserves full JSON; project and illustrated help remain usable at narrow/short sizes.');
  }catch(error){
    console.error('Save-flow state:',await page.evaluate(()=>({errors:document.querySelector('#toast')?.textContent,saveMode:document.body.dataset.saveMode,saveDisabled:document.querySelector('#save-json')?.disabled})),errors.slice(0,2));
    await page.screenshot({path:path.join(output,'failure.png')});throw error;
  }finally{await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
