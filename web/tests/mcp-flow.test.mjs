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
  window.installView=async ({html,result,arguments:args,messageSupported=true})=>{
    if(window.bridge){await window.bridge.teardownResource({});await window.bridge.close();}
    const old=document.querySelector('iframe');if(old)old.remove();
    const frame=document.createElement('iframe');frame.style='width:100%;height:100%;border:0';
    frame.setAttribute('sandbox','allow-scripts allow-same-origin allow-downloads');document.body.appendChild(frame);
    window.messages=[];window.calls=[];window.rejectMessage=false;window.loseSaveReply=false;
    const capabilities={serverTools:{},...(messageSupported?{message:{text:{}}}:{})};
    const bridge=window.bridge=new AppBridge(null,{name:'Official SDK reference host',version:'1.0.0'},capabilities);
    bridge.oncalltool=async params=>{window.calls.push(params.name);const response=await window.hostCallTool(params);
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
async function open(args) {
  const result=await client.callTool({name:'edithere_web_open',arguments:args});
  assert(!result.isError, JSON.stringify(result.content));
  const resource=await client.readResource({uri:'ui://edithere/annotation.html'});
  await page.evaluate(value=>window.installView(value),{html:resource.contents[0].text,result,arguments:args});
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
  assert(feedback.objects.some(object=>object.movements.length&&object.annotations.includes('这个组件右移，并保留位移箭头批注')));
}
try {
  stage='first open';
  await connect();await page.goto('http://127.0.0.1:'+server.address().port);
  const tools=await client.listTools();
  assert.equal(tools.tools.find(t=>t.name==='edithere_web_open')._meta.ui.resourceUri,'ui://edithere/annotation.html');
  let view=await open({imagePath,outputPath,name:'游戏主页 · MCP 链路测试'});
  const sessionId=view.result.structuredContent.sessionId;
  assert(!fs.existsSync(outputPath));
  assert.equal(await view.frame.locator('#canvas').evaluate(c=>c.width),await view.frame.locator('#canvas').evaluate(c=>EditHereWeb.project().width));
  await globalNote(view.frame,'游戏主页：让开始按钮更醒目');
  await movementNote(view.frame);
  // Wait for a real persisted draft, then replace the MCP process and iframe.
  const deadline=Date.now()+10000;
  while(Date.now()<deadline){const value=JSON.parse(fs.readFileSync(path.join(data,sessionId+'.json'),'utf8'));if(value.draft?.document?.notes?.length)break;await new Promise(r=>setTimeout(r,100));}
  assert(JSON.parse(fs.readFileSync(path.join(data,sessionId+'.json'),'utf8')).draft.document.notes.length);
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
  await page.evaluate(value=>window.installView(value),{html:resource.contents[0].text,result:reopenResult,arguments:{sessionId},messageSupported:false});
  view.frame=page.frameLocator('iframe');await view.frame.locator('#workspace').waitFor({state:'visible'});
  assert((await view.frame.locator('#toast').textContent()).includes('保存'));
  assert.equal(hash(outputPath),savedHash);
  checks.push('Completed feedback reopens with a manual fallback when the host has no message capability');

  // The in-app download is still an ordinary self-contained offline HTML file.
  if(await view.frame.locator('#utility-toggle').isVisible())await view.frame.locator('#utility-toggle').click();
  const downloadPromise=page.waitForEvent('download');await view.frame.locator('#offline').click();
  const download=await downloadPromise,stream=await download.createReadStream(),chunks=[];
  for await(const chunk of stream)chunks.push(chunk);
  const offline=Buffer.concat(chunks).toString('utf8');
  assert.doesNotMatch(offline,/<script\b[^>]*\bdata-edithere-mcp-bridge\b/);assert(offline.includes("connect-src 'none'"));
  checks.push('Offline download removes the host bridge and requires no service or npm');
  assert.deepEqual(errors,[]);
  await page.screenshot({path:path.join(output,'mcp-reference-host-560x720.png')});
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({passed:checks.length,checks,sha256:savedHash},null,2));
  console.log(checks.map(value=>'PASS '+value).join('\n'));
} catch(error){console.error('MCP flow failed at: '+stage);throw error;}
finally {await client?.close();await browser.close();await new Promise(resolve=>server.close(resolve));}
