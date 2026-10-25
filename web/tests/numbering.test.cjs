'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const L=require('../src/layout.js');
const {downloadText,readExportJson}=require('./export-test-utils.cjs');
const web=path.resolve(__dirname,'..'),output=path.join(__dirname,'output/numbering');
const {chromium}=require(process.env.EDITHERE_PLAYWRIGHT_MODULE||path.resolve(web,'../../EditHere宣传/动画/node_modules/playwright-core'));
const executablePath=process.env.EDITHERE_BROWSER_PATH||'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe';
const rect=(x1,y1,x2,y2)=>({x1,y1,x2,y2});
async function main(){
  fs.mkdirSync(output,{recursive:true});
  const browser=await chromium.launch({executablePath,headless:true});
  const context=await browser.newContext({offline:true,acceptDownloads:true,viewport:{width:1100,height:800}});
  const page=await context.newPage(),errors=[],checks=[];
  page.on('pageerror',error=>errors.push(String(error)));page.on('dialog',dialog=>dialog.accept());
  async function load(value,name='numbering.json'){
    await page.locator('#file-input').setInputFiles({name,mimeType:'application/json',buffer:Buffer.from(JSON.stringify(value))});
    await page.waitForFunction(()=>document.body.dataset.busy!=='true');
  }
  async function exported(){await page.locator('#export').click();const value=await readExportJson(page);await page.locator('[data-close="export-dialog"]').click();return value;}
  const cards=()=>page.locator('.note-card').evaluateAll(elements=>elements.map(element=>({id:element.dataset.note,number:Number(element.querySelector('.note-number').textContent),text:element.querySelector('p').textContent,empty:element.classList.contains('movement-empty')})));
  try{
    await page.goto(pathToFileURL(path.join(web,'EditHere.html')).href);await page.evaluate(()=>document.querySelector('#embed-image').checked=true);
    const image=await page.evaluate(()=>{const c=document.createElement('canvas');c.width=400;c.height=200;c.getContext('2d').fillRect(0,0,400,200);return c.toDataURL('image/png');});
    const a=rect(20,20,60,50),b=rect(70,20,100,50),toA=rect(120,20,160,50),toB=rect(200,20,230,50),layout=L.create(400,200);
    const aId=L.addRegion(layout,a),bId=L.addRegion(layout,b);L.transform(layout,aId,toA);L.transform(layout,bId,toB);
    layout.movements=[{groupId:aId,source:a,destination:toA},{groupId:bId,source:b,destination:toB}];
    const notes=[{id:'global',source:null,text:'第一条全局'},{id:'point',source:rect(5,5,5,5),text:'第二条点'},
      {id:'empty',source:toA,text:'',groupId:aId,movementSource:a},
      {id:'region',source:rect(300,120,340,160),text:'第四条区域'},
      {id:'movement',source:toB,text:'第五条移动',groupId:bId,movementSource:b}];
    const project={schemaVersion:'edithere-web-project-1',tool:'EditHere Web',kind:'image',name:'numbering.png',width:400,height:200,document:{id:'numbering',timestampUs:null,image,layout,notes}};
    await load(project);await page.locator('#workspace').waitFor({state:'visible'});
    if(await page.locator('body').getAttribute('data-inspector-open')!=='true')await page.locator('#inspect-toggle').click();
    const original=await exported(),sidebar=await cards(),pins=await page.locator('#pins .pin').evaluateAll(elements=>Object.fromEntries(elements.map(element=>[element.dataset.note,Number(element.textContent)])));
    assert.deepEqual(original.objects.map(object=>object.id),['o1','o2','o3','o4','o5']);
    assert.deepEqual(original.objects.flatMap(object=>object.annotations).map(({number,text})=>[number,text]),[[1,'第一条全局'],[2,'第二条点'],[4,'第四条区域'],[5,'第五条移动']]);
    for(const note of notes.filter(note=>note.text)){
      const card=sidebar.find(card=>card.id===note.id),annotation=original.objects.flatMap(object=>object.annotations).find(annotation=>annotation.text===note.text);
      assert.equal(annotation.number,card.number,'JSON and sidebar identify '+note.text);
      if(note.source!==null)assert.equal(annotation.number,pins[note.id],'JSON and image pin identify '+note.text);
    }
    assert.equal(pins.empty,3);assert(!original.objects.flatMap(object=>object.annotations).some(annotation=>annotation.number===3));
    checks.push('Image pins, sidebar and JSON preserve creation order and share numbers, including a blank movement slot');
    const saved=JSON.parse(await downloadText(page,'#save-project'));await load(saved);assert.deepEqual((await exported()).objects,original.objects);
    checks.push('Saving and reopening a Web project preserves the original numbering order');
    await load(original);assert.deepEqual((await cards()).filter(card=>!card.empty).map(card=>card.text),['第一条全局','第二条点','第四条区域','第五条移动']);
    const reordered=await exported();assert.deepEqual(reordered.objects,original.objects);
    checks.push('Feedback import restores annotation order and retains the empty movement number slot');
    const before=await page.evaluate(()=>EditHereWeb.feedback());
    const duplicateId=JSON.parse(JSON.stringify(original));duplicateId.objects[1].id=duplicateId.objects[0].id;
    const duplicateNumber=JSON.parse(JSON.stringify(original));duplicateNumber.objects[0].annotations[0].number=5;
    for(const invalid of [duplicateId,duplicateNumber]){await load(invalid);assert.deepEqual(await page.evaluate(()=>EditHereWeb.feedback()),before);}
    checks.push('Duplicate object IDs and annotation numbers reject import without replacing existing work');
    const old=JSON.parse(JSON.stringify(original));for(const object of old.objects){delete object.id;object.annotations=object.annotations.map(annotation=>annotation.text);}
    await load(old);assert.equal((await exported()).objects.flatMap(object=>object.annotations).length,4);
    const legacy={annotationSpace:'result',image,changes:[{from:a,to:toA}],annotations:[{text:'旧全局先写'},{point:{x:5,y:5},text:'旧点其次'},{change:0,text:'旧移动最后'}]};
    await load(legacy);assert.deepEqual((await cards()).map(card=>card.text),['旧全局先写','旧点其次','旧移动最后']);
    checks.push('J01 v2 string annotations and older action feedback remain importable');
    assert.deepEqual(errors,[]);await page.screenshot({path:path.join(output,'numbered-feedback.png')});
    fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({checks,errors},null,2));console.log(checks.map(check=>'PASS '+check).join('\n'));
  }finally{await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
