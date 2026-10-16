'use strict';
// Real offline browser coverage of imported desktop OCR evidence. The Web
// editor intentionally has no OCR engine; no model or network request is used.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const F=require('../src/feedback.js');
const {downloadText,readExportJson}=require('./export-test-utils.cjs');
const web=path.resolve(__dirname,'..'),output=path.join(__dirname,'output/recognized-text');
const {chromium}=require(process.env.EDITHERE_PLAYWRIGHT_MODULE||path.resolve(web,'../../EditHere宣传/动画/node_modules/playwright-core'));
const executablePath=process.env.EDITHERE_BROWSER_PATH||'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe';
const rectangle=(x1,y1,x2,y2)=>({x1,y1,x2,y2});
async function main(){
  fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({executablePath,headless:true});
  const context=await browser.newContext({offline:true,acceptDownloads:true,viewport:{width:1100,height:800}});
  const page=await context.newPage(),errors=[],requests=[],checks=[];
  page.on('pageerror',error=>errors.push(String(error)));page.on('dialog',dialog=>dialog.accept());
  context.on('request',request=>{if(/^https?:/.test(request.url()))requests.push(request.url());});
  async function load(value){await page.locator('#file-input').setInputFiles({name:'ocr-feedback.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(value))});await page.waitForFunction(()=>document.body.dataset.busy!=='true');}
  async function exported(){await page.locator('#export').click();const value=await readExportJson(page);await page.locator('[data-close="export-dialog"]').click();return value;}
  async function toggle(value){await page.locator('#export').click();await page.locator('#include-recognized-text').setChecked(value);const result=await readExportJson(page);await page.locator('[data-close="export-dialog"]').click();return result;}
  async function edit(index,text){await page.locator('[data-edit-recognized]').nth(index).click();await page.locator('#recognized-text').fill(text);await page.locator('#recognized-save').click();await page.locator('#recognized-dialog').waitFor({state:'hidden'});}
  const recognized=value=>value.objects.filter(object=>object.recognizedText).map(object=>object.recognizedText);
  try{
    await page.goto(pathToFileURL(path.join(web,'EditHere.html')).href);
    const image=await page.evaluate(()=>{const c=document.createElement('canvas');c.width=400;c.height=200;const p=c.getContext('2d');p.fillStyle='#fff';p.fillRect(0,0,400,200);p.fillStyle='#222';p.font='24px sans-serif';p.fillText('Save',35,55);return c.toDataURL('image/png');});
    const source=rectangle(20,20,100,70),destination=rectangle(180,20,260,70);
    const current=F.exportImage({image,movements:[{source:rectangle(20,100,100,150),destination:rectangle(180,100,260,150)}],notes:[
      {source,text:'第一个按钮变蓝',recognizedText:{text:'Save <script>alert(1)</script>',engine:'windows-ocr',language:'en-US',confidence:.95}},
      {source,text:'第二个独立意见',recognizedText:{text:'Save copy',engine:'apple-vision'}},
      {source:rectangle(180,100,260,150),movementSource:rectangle(20,100,100,150),text:'',recognizedText:{text:'Cancel',engine:'windows-ocr'}}
    ]},true,'2026-10-16T08:30:00Z');
    await load(current);await page.locator('#workspace').waitFor({state:'visible'});
    if(await page.locator('body').getAttribute('data-inspector-open')!=='true')await page.locator('#inspect-toggle').click();
    assert.equal(await page.locator('.recognized-text').count(),3);assert.equal(await page.locator('.recognized-text script').count(),0);
    assert.deepEqual(recognized(await exported()),recognized(current));
    assert.equal((await exported()).objects.flatMap(object=>object.annotations).length,2);
    await page.screenshot({path:path.join(output,'imported-recognized-text.png')});
    checks.push('Imported region OCR and a movement-only OCR result remain escaped screenshot data, with no additional annotation');
    await page.locator('.note-card').first().click();await page.locator('#stage').focus();await page.keyboard.press('ArrowRight');
    assert.equal(recognized(await exported()).length,2,'moving an annotation target invalidates OCR from its previous pixels');
    await page.locator('#undo').click();assert.deepEqual(recognized(await exported()),recognized(current));
    checks.push('Moving an ordinary annotation clears stale screenshot evidence and undo restores the original region and text');
    await edit(0,'Save corrected');
    let value=await exported(),edited=recognized(value).find(text=>text.text==='Save corrected');
    assert.equal(edited.edited,true);assert.equal(edited.language,'en-US');assert.equal(Object.hasOwn(edited,'confidence'),false);
    assert(recognized(value).some(text=>text.text==='Save copy'),'independent notes sharing bounds keep independent evidence');
    assert.deepEqual(value.objects.flatMap(object=>object.annotations).map(annotation=>annotation.text),['第一个按钮变蓝','第二个独立意见']);
    await page.locator('#undo').click();assert(recognized(await exported()).some(text=>text.confidence===.95));
    await page.locator('#redo').click();assert(recognized(await exported()).some(text=>text.text==='Save corrected'));
    checks.push('Editing OCR retains annotation text, clears stale confidence, affects only its target, and supports undo/redo');
    await page.locator('[data-delete-recognized]').first().click();
    assert.equal(recognized(await exported()).length,2);
    const saved=JSON.parse(await downloadText(page,'#save-project'));
    assert(saved.document.notes.some(note=>note.recognizedText?.text===''&&note.recognizedText.edited===true));
    await load(saved);assert.equal(recognized(await exported()).length,2);
    checks.push('Deleting OCR stores an explicit project marker and stays deleted after saving and reopening');
    await page.evaluate(value=>EditHereWeb.loadAgentFeedback({image:value.image,name:'restored-feedback',feedback:value}),current);
    assert.deepEqual(await page.evaluate(()=>EditHereWeb.feedback()),current,'restored receipt keeps exact original snapshot');
    const disabled=await toggle(false);
    assert.equal(recognized(disabled).length,0);assert.equal(recognized(await page.evaluate(()=>EditHereWeb.feedback())).length,0);
    assert.equal((await page.evaluate(()=>EditHereWeb.project())).document.notes.filter(note=>note.recognizedText).length,3);
    assert.deepEqual(await toggle(true),current);
    await edit(0,'Save updated after recovery');assert(recognized(await exported()).some(text=>text.text==='Save updated after recovery'));
    checks.push('Recovered Agent snapshots obey the export switch, retain editable project OCR, and invalidate on edits');
    await toggle(false);await page.reload();assert.equal(await page.locator('#include-recognized-text').isChecked(),false);await load(current);assert.equal(recognized(await exported()).length,0);await toggle(true);
    checks.push('The export preference persists across reload and applies to newly opened feedback');
    const video=F.exportVideo({source:'offline-source.mp4',durationMs:1000,frames:[{id:'f1',timestampUs:100000,width:400,height:200,image,movements:[],notes:[{source:destination,text:'视频按钮变蓝',recognizedText:{text:'Continue',engine:'windows-ocr'}}]}]},true,'2026-10-16T08:30:00Z');
    await load(video);value=await exported();assert.deepEqual(recognized(value),recognized(video));assert.deepEqual(value.objects[0].recognizedText,value.frames[0].feedback.objects[0].recognizedText);
    const videoSaved=JSON.parse(await downloadText(page,'#save-project'));await load(videoSaved);assert.deepEqual(recognized(await exported()),recognized(video));
    const videoDisabled=await toggle(false);assert.equal(recognized(videoDisabled).length,0);assert(videoDisabled.frames.every(frame=>recognized(frame.feedback).length===0));
    await toggle(true);const before=await page.evaluate(()=>EditHereWeb.feedback());
    for(const mutate of [v=>v.objects[0].recognizedText.text='wrong evidence',v=>delete v.objects[0].recognizedText]){const invalid=structuredClone(video);mutate(invalid);await load(invalid);assert.deepEqual(await page.evaluate(()=>EditHereWeb.feedback()),before);}
    checks.push('Video project recovery preserves OCR, disabled export removes root and frame copies, and mismatched index evidence is rejected');
    const {$schema,schemaVersion,tool,exportedAt,...legacy}=current;await load(legacy);assert.deepEqual(await page.evaluate(()=>EditHereWeb.feedback()),before,'unversioned OCR is rejected before replacing work');
    for(const object of legacy.objects)delete object.recognizedText;await load(legacy);assert.equal(recognized(await exported()).length,0);
    checks.push('Legacy feedback remains readable while new OCR metadata requires feedback-minimal-2');
    assert.deepEqual(errors,[]);assert.deepEqual(requests,[]);await page.screenshot({path:path.join(output,'recognized-text.png')});
    fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({checks,errors,requests},null,2));console.log(checks.map(check=>'PASS '+check).join('\n'));
  }finally{await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
