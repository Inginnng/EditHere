import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import sessionModule from '../agent-session.cjs';

const require = createRequire(new URL('../mcp/package.json',import.meta.url));
const {Client} = require('@modelcontextprotocol/sdk/client/index.js');
const {StdioClientTransport} = require('@modelcontextprotocol/sdk/client/stdio.js');
const {RESOURCE_MIME_TYPE,EXTENSION_ID} = require('@modelcontextprotocol/ext-apps/server');
const {readSessionStatus} = sessionModule;
const webDirectory = fileURLToPath(new URL('../',import.meta.url));
const image = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';
const feedback = {annotationSpace:'result',image,objects:[{source:null,movements:[],annotations:['主页开始按钮更醒目']}]};
function fixture() {
  const directory=path.join(webDirectory,'tests/output/mcp-server',crypto.randomUUID());
  fs.mkdirSync(directory,{recursive:true});
  const imagePath=path.join(directory,'游戏主页.png'),outputPath=path.join(directory,'feedback.json'),dataDirectory=path.join(directory,'sessions');
  fs.writeFileSync(imagePath,Buffer.from(image.split(',')[1],'base64'));
  return {directory,imagePath,outputPath,dataDirectory};
}
async function connect(context,paths,{apps=true}={}) {
  const client=new Client({name:'EditHere official SDK test host',version:'1.0.0'},{capabilities:apps ? {extensions:{[EXTENSION_ID]:{mimeTypes:[RESOURCE_MIME_TYPE]}}} : {}});
  const transport=new StdioClientTransport({command:process.execPath,args:[process.env.EDITHERE_MCP_TEST_ENTRY || path.join(webDirectory,'mcp/server.mjs')],
    env:{...process.env,EDITHERE_WEB_DATA_DIR:paths.dataDirectory},stderr:'pipe'});
  let stderr='';transport.stderr.on('data',chunk=>{stderr+=chunk.toString();});
  await client.connect(transport);
  context.after(async()=>{await client.close(); assert.equal(stderr,'','MCP stderr: '+stderr);});
  return client;
}
const call=(client,name,args)=>client.callTool({name,arguments:args});
function draft(name='游戏主页') {
  return {schemaVersion:'edithere-web-project-1',tool:'EditHere Web',savedAt:new Date().toISOString(),kind:'image',name,width:1,height:1,
    document:{id:'frame-1',timestampUs:null,image,notes:[{id:'n-1',source:null,text:'增加游戏说明'}],layout:null}};
}

test('official stdio negotiation lists MCP Apps resources, UI-only mutation tools and strict self-contained HTML',async context=>{
  const paths=fixture(),client=await connect(context,paths);
  const {tools}=await client.listTools();
  const open=tools.find(tool=>tool.name==='edithere_web_open');
  assert.equal(open._meta.ui.resourceUri,'ui://edithere/annotation.html');
  assert.deepEqual(open._meta.ui.visibility,['model']);
  for (const name of ['edithere_web_submit','edithere_web_draft','edithere_web_cancel','edithere_web_image']) assert.deepEqual(tools.find(tool=>tool.name===name)._meta.ui.visibility,['app']);
  const resource=(await client.listResources()).resources[0];
  assert.equal(resource.mimeType,RESOURCE_MIME_TYPE);assert.equal(resource._meta.ui.prefersBorder,false);
  const html=(await client.readResource({uri:resource.uri})).contents[0];
  assert.equal(html.mimeType,RESOURCE_MIME_TYPE);assert.match(html.text,/EditHere/);
  assert.deepEqual(html._meta.ui.csp.connectDomains,[]);
  const policy=/<meta\s+http-equiv="Content-Security-Policy"\s+content="([^"]+)"/.exec(html.text)?.[1];
  assert.match(policy,/connect-src 'none'/);
  assert.equal(fs.existsSync(path.join(paths.dataDirectory,'server-key')),false);
});

test('a host without MCP Apps gets an explicit fallback and no orphan annotation session',async context=>{
  const paths=fixture(),client=await connect(context,paths,{apps:false});
  const opened=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath});
  assert.equal(opened.structuredContent.state,'unsupported');assert.equal(opened.structuredContent.uiSupport,'unsupported');
  assert.deepEqual(opened.structuredContent.fallback.args.slice(-4),['--image',paths.imagePath,'--output',paths.outputPath]);
  assert.equal(opened._meta,undefined);assert.equal(fs.readdirSync(paths.dataDirectory).filter(name=>name.endsWith('.json')).length,0);
  assert.equal(fs.existsSync(paths.outputPath),false);
});

test('native feedback saves once, stays readable after MCP restart and is kept out of image-heavy model context',async context=>{
  const paths=fixture(),client=await connect(context,paths);
  const opened=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath,name:'游戏主页'});
  const privateData=opened._meta.edithere,{sessionId,token}=privateData;
  assert.equal(privateData.image,image);assert.equal(opened.structuredContent.state,'pending');
  assert.equal(JSON.stringify(opened.structuredContent).includes('base64'),false);assert.equal(opened.content[0].text.includes(token),false);
  const first=await call(client,'edithere_web_submit',{sessionId,token,feedback});
  assert.equal(first.structuredContent.state,'completed');assert.equal(first.structuredContent.outputPath,paths.outputPath);
  assert.deepEqual(first.structuredContent.feedback,{annotationSpace:'result',objects:feedback.objects});
  assert.deepEqual(JSON.parse(fs.readFileSync(paths.outputPath,'utf8')),feedback);
  assert.deepEqual((await call(client,'edithere_web_submit',{sessionId,token,feedback})).structuredContent,first.structuredContent);
  assert.equal((await call(client,'edithere_web_submit',{sessionId,token,feedback:{...feedback,objects:[]}})).isError,true);
  await client.close();
  const restarted=await connect(context,paths);
  assert.deepEqual((await call(restarted,'edithere_web_read',{sessionId})).structuredContent,first.structuredContent);
  const reopened=await call(restarted,'edithere_web_open',{sessionId});
  assert.equal(reopened._meta.edithere.image,image);assert.deepEqual(reopened._meta.edithere.receipt,first.structuredContent);
  assert.equal(readSessionStatus({directory:paths.dataDirectory,sessionId}).state,'completed');
});

test('versioned image metadata reaches the saved file and MCP read receipt unchanged',async context=>{
  const paths=fixture(),client=await connect(context,paths);
  const opened=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath}),{sessionId,token}=opened._meta.edithere;
  const metadata={$schema:'urn:edithere:feedback-minimal:2',schemaVersion:'feedback-minimal-2',tool:{name:'EditHere',version:'0.11.1'},exportedAt:'2026-10-16T16:30:00+08:00'};
  const changes={...metadata,annotationSpace:'result',objects:feedback.objects};
  assert.equal((await call(client,'edithere_web_submit',{sessionId,token,feedback:{...changes,schemaVersion:'feedback-minimal-99'}})).isError,true);
  const submitted=await call(client,'edithere_web_submit',{sessionId,token,feedback:changes});
  assert.equal(submitted.structuredContent.state,'completed');
  assert.deepEqual(submitted.structuredContent.feedback,changes);
  assert.deepEqual(JSON.parse(fs.readFileSync(paths.outputPath,'utf8')),{...changes,image});
  await client.close();
  const restarted=await connect(context,paths);
  assert.deepEqual((await call(restarted,'edithere_web_read',{sessionId})).structuredContent.feedback,changes);
  assert.equal(JSON.stringify(submitted.structuredContent).includes('base64'),false);
});

test('pending draft resumes with the original image after the host closes the stdio connection',async context=>{
  const paths=fixture(),client=await connect(context,paths);
  const opened=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath,name:'游戏主页'}),{sessionId,token}=opened._meta.edithere;
  const project=draft();
  const saved=await call(client,'edithere_web_draft',{sessionId,token,project});
  assert.equal(saved.structuredContent.state,'pending');assert.ok(saved.structuredContent.draftSavedAt);
  await client.close();
  assert.equal(fs.existsSync(paths.outputPath),false);
  const restarted=await connect(context,paths),reopened=await call(restarted,'edithere_web_open',{sessionId});
  assert.deepEqual({...reopened._meta.edithere.draft,document:{...reopened._meta.edithere.draft.document,image}},project);assert.equal(reopened._meta.edithere.token,token);
  assert.equal(reopened._meta.edithere.draft.document.image,undefined);
  const cancelled=await call(restarted,'edithere_web_cancel',{sessionId,token});
  assert.equal(cancelled.structuredContent.state,'cancelled');
  assert.equal((await call(restarted,'edithere_web_submit',{sessionId,token,feedback})).isError,true);
  assert.equal(fs.existsSync(paths.outputPath),false);
});

test('large images cross the default 10 MiB SDK transport in private chunks and resume without duplicating image data',async context=>{
  const paths=fixture();
  const original=Buffer.from(image.split(',')[1],'base64');
  const largeImageBytes=Buffer.concat([original,Buffer.alloc(9*1024*1024)]);
  fs.writeFileSync(paths.imagePath,largeImageBytes);
  const dataURL='data:image/png;base64,'+largeImageBytes.toString('base64');
  assert.ok(dataURL.length>10*1024*1024);
  const client=await connect(context,paths),opened=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath,name:'游戏主页'});
  const {sessionId,token,imageInfo}=opened._meta.edithere;
  assert.equal(opened._meta.edithere.image,undefined);assert.equal(imageInfo.length,dataURL.length);
  let reconstructed='',offset=0;
  while(offset<imageInfo.length){
    const chunk=await call(client,'edithere_web_image',{sessionId,token,offset});
    assert.equal(chunk.structuredContent.offset,offset);
    assert.equal(chunk.structuredContent.totalLength,imageInfo.length);
    assert.ok(chunk._meta.edithereImageChunk.length<=256*1024);
    assert.equal(chunk.content[0].text.includes('base64'),false);
    reconstructed+=chunk._meta.edithereImageChunk;offset=chunk.structuredContent.nextOffset;
  }
  assert.equal(reconstructed,dataURL);
  assert.equal((await call(client,'edithere_web_image',{sessionId,token,offset:dataURL.length+1})).isError,true);
  const project=draft();delete project.document.image;
  assert.equal((await call(client,'edithere_web_draft',{sessionId,token,project})).structuredContent.state,'pending');
  await client.close();
  const restarted=await connect(context,paths),reopened=await call(restarted,'edithere_web_open',{sessionId});
  assert.equal(reopened._meta.edithere.image,undefined);assert.equal(reopened._meta.edithere.imageInfo.length,dataURL.length);
  assert.equal(reopened._meta.edithere.draft.document.image,undefined);
  const changes={...feedback};delete changes.image;
  const submitted=await call(restarted,'edithere_web_submit',{sessionId,token,feedback:changes});
  assert.equal(submitted.structuredContent.state,'completed');
  assert.equal(JSON.parse(fs.readFileSync(paths.outputPath,'utf8')).image,dataURL);
});

test('separate MCP connections reload saved state and cannot overwrite another connection\'s completed feedback',async context=>{
  const paths=fixture(),one=await connect(context,paths),two=await connect(context,paths);
  const opened=await call(one,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath,name:'游戏主页'}),{sessionId,token}=opened._meta.edithere;
  assert.equal((await call(two,'edithere_web_open',{sessionId})).structuredContent.state,'pending');
  await call(one,'edithere_web_submit',{sessionId,token,feedback});
  const lateDraft=await call(two,'edithere_web_draft',{sessionId,token,project:draft()});
  assert.equal(lateDraft.isError,true);assert.match(lateDraft.content[0].text,/completed/);
  assert.equal((await call(two,'edithere_web_read',{sessionId})).structuredContent.state,'completed');
  assert.equal((await call(two,'edithere_web_cancel',{sessionId,token})).isError,true);
  assert.deepEqual(JSON.parse(fs.readFileSync(paths.outputPath,'utf8')),feedback);
});

test('wrong-session tokens, arbitrary draft fields and ambiguous resume inputs are rejected',async context=>{
  const paths=fixture(),client=await connect(context,paths);
  const first=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:paths.outputPath,name:'游戏主页'});
  const second=await call(client,'edithere_web_open',{imagePath:paths.imagePath,outputPath:path.join(paths.directory,'second.json')});
  const {sessionId,token}=first._meta.edithere;
  assert.equal((await call(client,'edithere_web_submit',{sessionId,token:second._meta.edithere.token,feedback})).isError,true);
  assert.equal((await call(client,'edithere_web_submit',{sessionId,token,feedback:{...feedback,image:image+'different'}})).isError,true);
  const wrongImage=draft();wrongImage.document.image=image+'different';
  assert.equal((await call(client,'edithere_web_draft',{sessionId,token,project:wrongImage})).isError,true);
  assert.equal((await call(client,'edithere_web_draft',{sessionId,token,project:{...draft(),unexpected:'not-project'}})).isError,true);
  assert.equal((await call(client,'edithere_web_open',{sessionId,imagePath:paths.imagePath})).isError,true);
  assert.equal((await call(client,'edithere_web_open',{imagePath:'relative.png'})).isError,true);
  assert.equal(fs.existsSync(paths.outputPath),false);
});
