// End-to-end reference-host test: official AppBridge + real iframe App SDK +
// official MCP Client over stdio. The HTTP listener here serves only the test host.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {build} from '../mcp/node_modules/esbuild/lib/main.js';
import {Client} from '../mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/index.js';
import {StdioClientTransport} from '../mcp/node_modules/@modelcontextprotocol/sdk/dist/esm/client/stdio.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.dirname(here), root = path.dirname(web);
const require = createRequire(import.meta.url);
const first = list => list.filter(Boolean).find(file => fs.existsSync(file));
const playwright = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe']);
const imagePath = first([process.env.EDITHERE_TEST_IMAGE, path.resolve(root, '../EditHere宣传/原图/游戏修改前.png')]);
assert(playwright && executablePath && imagePath, 'Set EDITHERE_PLAYWRIGHT_MODULE, EDITHERE_BROWSER_PATH and EDITHERE_TEST_IMAGE.');
const output = fs.mkdtempSync(path.join(fs.mkdirSync(path.join(here, 'output/mcp-flow'), {recursive:true}) || path.join(here, 'output/mcp-flow'), 'run-'));
const data = path.join(output, 'sessions'), outputPath = path.join(output, 'feedback.json');
const {chromium} = require(playwright);
let client, transport, stage='initialize';
async function connect() {
  client = new Client({name:'EditHere test client', version:'1.0.0'}, {capabilities:{extensions:{'io.modelcontextprotocol/ui':{mimeTypes:['text/html;profile=mcp-app']}}}});
  transport = new StdioClientTransport({command:process.execPath,args:[path.join(web, 'mcp/server.bundle.cjs')],env:{...process.env,EDITHERE_WEB_DATA_DIR:data},stderr:'pipe'});
  await client.connect(transport);
}
const appBridge = await build({stdin:{contents:`
  import {AppBridge,PostMessageTransport} from '@modelcontextprotocol/ext-apps/app-bridge';
  window.installView=async ({html,result,arguments:args,messageSupported=true,allowExpansion=true})=>{
    if(window.bridge){await window.bridge.teardownResource({});await window.bridge.close();}
    const old=document.querySelector('iframe');if(old)old.remove();
    const frame=document.createElement('iframe');frame.style='width:100%;height:100%;border:0';
    frame.setAttribute('sandbox','allow-scripts allow-same-origin allow-downloads');document.body.appendChild(frame);
    window.messages=[];window.calls=[];window.callErrors=[];window.displayRequests=[];window.rejectMessage=false;window.loseSaveReply=false;
    window.holdNextDraftReply=false;window.releaseDraftReply=null;window.draftReplyReleased=false;
    const capabilities={serverTools:{},...(messageSupported?{message:{text:{}}}:{})};
    const bridge=window.bridge=new AppBridge(null,{name:'Official SDK reference host',version:'1.0.0'},capabilities,
      {hostContext:{displayMode:'inline',availableDisplayModes:['inline','fullscreen']}});
    bridge.onrequestdisplaymode=async ({mode})=>{
      window.displayRequests.push(mode);
      const actual=allowExpansion?mode:'inline';
      bridge.setHostContext({displayMode:actual,availableDisplayModes:['inline','fullscreen']});
      return {mode:actual};
    };
    bridge.oncalltool=async params=>{window.calls.push(params.name);const response=await window.hostCallTool(params);
      if(response.isError)window.callErrors.push({name:params.name,content:response.content});
      if(params.name==='edithere_web_draft'&&window.holdNextDraftReply){window.holdNextDraftReply=false;
        await new Promise(resolve=>window.releaseDraftReply=resolve);window.draftReplyReleased=true;}
      if(params.name==='edithere_web_submit'&&window.loseSaveReply){window.loseSaveReply=false;throw new Error('Simulated lost save ACK');}return response;};
    bridge.onmessage=async params=>{if(window.rejectMessage)return {isError:true};window.messages.push(params);return {};};
    bridge.oninitialized=async()=>{await bridge.sendToolInput({arguments:args});await bridge.sendToolResult(result);};
    await bridge.connect(new PostMessageTransport(frame.contentWindow,frame.contentWindow));frame.srcdoc=html;
  };
`,resolveDir:path.join(web,'mcp'),sourcefile:'reference-host.js'},bundle:true,write:false,platform:'browser',format:'iife',target:'es2022'});
const hostHTML = '<!doctype html><style>html,body{height:100%;margin:0;overflow:hidden}</style><script>' + appBridge.outputFiles[0].text.replace(/<\/script/gi,'<\\/script') + '</script>';
const server = http.createServer((req,res)=>{res.setHeader('Content-Type','text/html; charset=utf-8');res.end(hostHTML);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser = await chromium.launch({executablePath,headless:true});
const page = await browser.newPage({viewport:{width:560,height:720},acceptDownloads:true});
const errors=[],checks=[];page.on('pageerror',error=>errors.push(error.message));page.on('dialog',dialog=>dialog.accept());
await page.exposeFunction('hostCallTool', async params=>{
  try{return await client.callTool(params);}
  catch(error){return {isError:true,content:[{type:'text',text:error.message}]};}
});
const hash = file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
async function open(args,{damageInlineImage=false}={}) {
  const result=await client.callTool({name:'edithere_web_open',arguments:args});
  assert(!result.isError, JSON.stringify(result.content));
  const resource=await client.readResource({uri:'ui://edithere/annotation.html'});
  const uiResult=structuredClone(result);
  if(damageInlineImage){
    assert.equal(typeof uiResult._meta.edithere.image,'string');
    uiResult._meta.edithere.image='data:image/png;base64,broken';
  }
  await page.evaluate(value=>window.installView(value),{html:resource.contents[0].text,result:uiResult,arguments:args});
  const frame=page.frameLocator('iframe');
  await frame.locator('#workspace').waitFor({state:'visible'});
  return {frame,result};
}
async function globalNote(frame,text) {
  if(await frame.locator('body').getAttribute('data-inspector-open')!=='true')await frame.locator('#inspect-toggle').click();
  await frame.locator('#global').click();await frame.locator('#note-text').fill(text);
  await frame.locator('#note-form [type="submit"]').click();
  await frame.locator('#note-dialog').waitFor({state:'hidden'});
}
async function movementNote(frame) {
  const dims=await frame.locator('#canvas').evaluate(canvas=>({width:canvas.width,height:canvas.height}));
  const at=async(x,y)=>frame.locator('#stage').evaluate((stage,point)=>{const rect=stage.getBoundingClientRect(),canvas=document.querySelector('#canvas');return {x:rect.x+point.x*rect.width/canvas.width,y:rect.y+point.y*rect.height/canvas.height};},{x,y});
  await frame.locator('#explode').click();
  await frame.locator('[data-mode="cut"]').waitFor({state:'visible'});
  await frame.locator('[data-mode="cut"]').click();
  const start=await at(dims.width*.65,dims.height*.35),end=await at(dims.width*.8,dims.height*.5);
  await page.mouse.move(start.x,start.y);await page.mouse.down();await page.mouse.move(end.x,end.y,{steps:8});await page.mouse.up();
  await frame.locator('#geometry-toggle:enabled').waitFor();await frame.locator('#geometry-toggle').click();
  const oldX=Number(await frame.locator('#gx').inputValue());
  await frame.locator('#gx').fill(String(oldX+30));await frame.locator('#apply-geometry').click();
  const note=await frame.locator('.note-card.movement-empty').getAttribute('data-note');assert(note);
  await frame.locator(`[data-edit="${note}"]`).click();await frame.locator('#note-text').fill('这个组件右移，并保留位移箭头批注');
  await frame.locator('#note-text').press('Control+Enter');await frame.locator('#note-dialog').waitFor({state:'hidden'});
  const feedback=await frame.locator('#canvas').evaluate(()=>EditHereWeb.feedback());
  assert(feedback.objects.some(object=>object.movements.length&&object.annotations.some(annotation => annotation.text === '这个组件右移，并保留位移箭头批注')));
}
try {
  stage='first open';
  await connect();await page.goto('http://127.0.0.1:'+server.address().port);
  const tools=await client.listTools();
  assert.equal(tools.tools.find(t=>t.name==='edithere_web_open')._meta.ui.resourceUri,'ui://edithere/annotation.html');
  let view=await open({imagePath,outputPath,name:'游戏主页 · MCP 链路测试'},{damageInlineImage:true});
  const sessionId=view.result.structuredContent.sessionId;
  await view.frame.locator('body[data-mcp-display-request="accepted"]').waitFor();
  await view.frame.locator('body[data-image-state="ready"]').waitFor();
  assert.equal(await view.frame.locator('#canvas').evaluate(()=>EditHereWeb.project().document.image),view.result._meta.edithere.image);
  assert((await page.evaluate(()=>window.calls)).includes('edithere_web_image'));
  checks.push('Original image loads through app-only chunks even when the agent result contains a damaged image encoding');
  for(const width of [480,375,320,560]){
    await page.setViewportSize({width,height:720});
    await view.frame.locator('body').evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    const header=await view.frame.locator('.header').evaluate(element=>{
      const rect=element.getBoundingClientRect();
      return {height:rect.height,y:rect.y,width:innerWidth,islands:Array.from(element.children).map(island=>{
        const r=island.getBoundingClientRect();return {y:r.y,height:r.height,right:r.right};
      })};
    });
    assert(header.islands.every(island=>Math.abs(island.y-header.y)<1&&Math.abs(island.height-header.height)<1&&island.right<=header.width),'MCP header must remain one aligned row at '+width+'px');
    if(width===480)assert(await view.frame.locator('#export span').isVisible(),'Feedback text remains visible at a typical narrow Agent panel width');
  }
  checks.push('MCP feedback and zoom controls share one row at 320–560px; the feedback label remains readable at 480px');
  assert.deepEqual(await page.evaluate(()=>window.displayRequests),['fullscreen']);
  await page.evaluate(()=>window.bridge.setHostContext({displayMode:'inline',availableDisplayModes:['inline','fullscreen']}));
  await view.frame.locator('body[data-mcp-display-mode="inline"]').waitFor();
  assert.deepEqual(await page.evaluate(()=>window.displayRequests),['fullscreen']);
  checks.push('One expanded-display request through the official bridge; user collapse is respected');
  assert(!fs.existsSync(outputPath));
  assert.equal(await view.frame.locator('#canvas').evaluate(c=>c.width),await view.frame.locator('#canvas').evaluate(c=>EditHereWeb.project().width));
  stage='stale draft ACK';
  await page.evaluate(()=>window.holdNextDraftReply=true);
  await globalNote(view.frame,'游戏主页：开始按钮初稿');
  await page.waitForFunction(()=>typeof window.releaseDraftReply==='function');
  const oldRevision=await view.frame.locator('#canvas').evaluate(()=>EditHereWeb.projectRevision());
  await view.frame.locator('.note-card [data-edit]').click();
  await view.frame.locator('#note-text').fill('游戏主页：让开始按钮更醒目');
  await view.frame.locator('#note-text').press('Control+Enter');
  await view.frame.locator('#note-dialog').waitFor({state:'hidden'});
  assert(await view.frame.locator('#canvas').evaluate(()=>EditHereWeb.projectRevision())>oldRevision);
  await page.evaluate(()=>window.releaseDraftReply());
  await page.waitForFunction(()=>window.draftReplyReleased);
  await view.frame.locator('body').evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))));
  assert.equal(await view.frame.locator('body').getAttribute('data-draft-state'),'unsaved','a stale save ACK must not mark a newer editing revision as saved');
  checks.push('A delayed draft ACK leaves a newer editing revision unsaved until its own save completes');
  await movementNote(view.frame);
  // Wait for a real persisted draft, then replace the MCP process and iframe.
  const savedDraftContainsEdits=()=>{
    const notes=JSON.parse(fs.readFileSync(path.join(data,sessionId+'.json'),'utf8')).draft?.document?.notes||[];
    return ['游戏主页：让开始按钮更醒目','这个组件右移，并保留位移箭头批注'].every(text=>notes.some(note=>note.text===text));
  };
  const deadline=Date.now()+10000;
  while(Date.now()<deadline&&!savedDraftContainsEdits())await new Promise(r=>setTimeout(r,100));
  assert(savedDraftContainsEdits(),'both latest annotations must reach disk before testing a process restart');
  stage='restart after draft';
  await page.waitForFunction(()=>document.querySelector('iframe')?.contentDocument?.body.dataset.draftState==='saved');
  await page.evaluate(()=>window.bridge.teardownResource({}));
  await client.close();await connect();view=await open({sessionId});
  assert((await view.frame.locator('.note-card p').allTextContents()).includes('游戏主页：让开始按钮更醒目'));
  assert((await view.frame.locator('.note-card p').allTextContents()).includes('这个组件右移，并保留位移箭头批注'));
  checks.push('Screenshot input and persisted draft restore through a new stdio process and iframe');

  const expected=await view.frame.locator('#canvas').evaluate(()=>EditHereWeb.feedback());
  stage='lost save ACK';
  await page.evaluate(()=>{window.loseSaveReply=true;window.rejectMessage=true;});
  await view.frame.locator('#export').click();
  await view.frame.locator('#export:enabled').waitFor();
  assert(fs.existsSync(outputPath));const savedHash=hash(outputPath);
  assert.deepEqual(JSON.parse(fs.readFileSync(outputPath,'utf8')),expected);
  await view.frame.locator('#export').click();
  await view.frame.locator('#export:enabled').waitFor();
  assert.equal(hash(outputPath),savedHash);
  assert.equal(await page.evaluate(()=>window.messages.length),0);
  assert((await view.frame.locator('#toast').textContent()).includes('保存'));
  const submitsBefore=await page.evaluate(()=>window.calls.filter(name=>name==='edithere_web_submit').length);
  await page.evaluate(()=>window.rejectMessage=false);
  stage='retry chat message';
  await view.frame.locator('#export').click();
  await page.waitForFunction(()=>window.messages.length===1);
  assert.equal(await page.evaluate(()=>window.calls.filter(name=>name==='edithere_web_submit').length),submitsBefore);
  assert.equal(hash(outputPath),savedHash);
  const message=await page.evaluate(()=>window.messages[0]);
  assert.equal(message.role,'user');assert(message.content[0].text.includes(sessionId));
  assert(!JSON.stringify(message).includes('data:image/'));
  const receipt=await client.callTool({name:'edithere_web_read',arguments:{sessionId}});
  assert.equal(receipt.structuredContent.state,'completed');assert.equal(receipt.structuredContent.sha256,savedHash);
  checks.push('Lost save reply retries idempotently; rejected chat delivery retries without submitting again');

  view=await open({sessionId});
  stage='no message capability';
  // UI capability fallback: a host without ui/message still preserves feedback.
  const reopenResult=await client.callTool({name:'edithere_web_open',arguments:{sessionId}});
  const resource=await client.readResource({uri:'ui://edithere/annotation.html'});
  await page.evaluate(value=>window.installView(value),{html:resource.contents[0].text,result:reopenResult,arguments:{sessionId},messageSupported:false,allowExpansion:false});
  view.frame=page.frameLocator('iframe');await view.frame.locator('#workspace').waitFor({state:'visible'});
  await view.frame.locator('body[data-mcp-display-request="declined"]').waitFor();
  assert.equal(await view.frame.locator('body').getAttribute('data-mcp-display-mode'),'inline');
  assert.deepEqual(await page.evaluate(()=>window.displayRequests),['fullscreen']);
  checks.push('Host refusal leaves a usable inline editor without repeat expansion requests');
  assert((await view.frame.locator('#toast').textContent()).includes('保存'));
  assert.equal(hash(outputPath),savedHash);
  checks.push('Completed feedback reopens with a manual fallback when the host has no message capability');

  // Saved feedback can be backed up manually even when chat delivery is unsupported.
  assert.equal(await view.frame.locator('#offline').count(),0);
  assert.equal(await view.frame.locator('#actual-size').count(),0);
  if(await view.frame.locator('body').getAttribute('data-save-mode')!=='file'){
    await view.frame.locator('#save-options-toggle').click();
    await view.frame.locator('#save-options [data-save-mode="file"]').click();
  }
  const downloadPromise=page.waitForEvent('download');await view.frame.locator('#save-json').click();
  const download=await downloadPromise,stream=await download.createReadStream(),chunks=[];
  for await(const chunk of stream)chunks.push(chunk);
  const bytes=Buffer.concat(chunks),entries=bytes.readUInt32LE(0)===0x04034b50?require('../src/bundle.js').unzip(bytes):null,backup=entries?Buffer.from([...entries].find(([name])=>name.endsWith('.json'))[1]).toString('utf8'):bytes.toString('utf8');
  assert.equal(await download.failure(),null);
  const backupValue=JSON.parse(backup);assert.deepEqual({...backupValue,image:null},{...expected,image:null},'manual JSON backup preserves saved feedback');assert.equal(backupValue.image.sha256,expected.image.sha256);assert(entries?.has(backupValue.image.file));
  assert.equal(hash(outputPath),savedHash);
  checks.push('Manual JSON backup preserves completed native feedback without resubmitting or changing the receipt');
  assert.deepEqual(errors,[]);
  await page.screenshot({path:path.join(output,'mcp-reference-host-560x720.png')});
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({passed:checks.length,checks,sha256:savedHash},null,2));
  console.log(checks.map(value=>'PASS '+value).join('\n'));
} catch(error){
  console.error('MCP flow failed at: '+stage);
  console.error(JSON.stringify(await page.evaluate(()=>{
    const body=document.querySelector('iframe')?.contentDocument?.body;
    return {state:body?.dataset,toast:body?.querySelector('#toast')?.textContent,calls:window.calls,callErrors:window.callErrors};
  }).catch(()=>({diagnostic:'The host page navigated before failure inspection.'}))));
  throw error;
}
finally {await client?.close();await browser.close();await new Promise(resolve=>server.close(resolve));}
