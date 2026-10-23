/* The official MCP Apps transport, following Cowart's save-then-message flow.
 * This file is bundled only into EditHere.mcp.html; the offline editor has no
 * dependency on a host, listener, or SDK. Session credentials stay in closure. */
import { App } from '@modelcontextprotocol/ext-apps';

const editor = globalThis.EditHereWeb;
const $ = id => document.getElementById(id);
const submit = $('export'), cancel = $('open');
const originalExport = submit.onclick, originalOpen = cancel.onclick;
// MCP Apps commonly omit allow-forms from their iframe sandbox. Annotation
// save is a local editing action, so dispatch our existing editor handler
// directly rather than attempting a browser form submission or navigation.
function saveLocalNote(event) {
  event.preventDefault();
  const form=$('note-form');
  if(form.reportValidity())form.onsubmit(event);
}
$('note-form').querySelector('button[type="submit"]').addEventListener('click',saveLocalNote);
$('note-dialog').addEventListener('keydown',event=>{
  if((event.ctrlKey||event.metaKey)&&event.key==='Enter'){
    event.stopImmediatePropagation();saveLocalNote(event);
  }
},true);
const bridge = new App({name:'EditHere',version:document.querySelector('meta[name="edithere-version"]').content}, {availableDisplayModes:['inline','fullscreen']}, {autoResize:false});
const lifecycle = new AbortController();
let connectPromise, session = null, receipt = null, snapshot = null;
let connected = false, alive = true, working = false, draftTimer;
let draftTail = Promise.resolve(), openTail = Promise.resolve(), lastDraftRevision = -1;
let expansionRequested = false;

async function expandForEditing() {
  if(expansionRequested||!alive)return;
  expansionRequested=true;
  const context=bridge.getHostContext();
  document.body.dataset.mcpDisplayMode=context?.displayMode||'inline';
  if(context?.displayMode==='fullscreen')return;
  if(!context?.availableDisplayModes?.includes('fullscreen')){
    document.body.dataset.mcpDisplayRequest='unavailable';return;
  }
  document.body.dataset.mcpDisplayRequest='requesting';
  try{
    // The host chooses the actual container for its expanded MCP Apps surface.
    // Request once per view so a later user collapse is always respected.
    const result=await bridge.requestDisplayMode({mode:'fullscreen'},{timeout:10000,signal:lifecycle.signal});
    if(!alive)return;
    document.body.dataset.mcpDisplayMode=result.mode;
    document.body.dataset.mcpDisplayRequest=result.mode==='fullscreen'?'accepted':'declined';
  }catch{
    if(alive)document.body.dataset.mcpDisplayRequest='failed';
  }
}

function label(button, text) {button.querySelector('span').textContent=text;}
function toolValue(result) {
  // Cowart also supports hosts that wrap a native result in one JSON text block.
  for(let depth=0;depth<3&&!result?.structuredContent&&!result?._meta?.edithere;depth++){
    if(result?.content?.length!==1||result.content[0].type!=='text')break;
    try{result=JSON.parse(result.content[0].text);}catch{break;}
  }
  if(result?.isError)throw new Error(result.content?.find(item=>item.type==='text')?.text||'批注会话未完成');
  return {value:result?.structuredContent||{},privateData:result?._meta?.edithere};
}
function supportsTools() {return connected&&!!bridge.getHostCapabilities()?.serverTools;}
function supportsMessages() {return connected&&!!bridge.getHostCapabilities()?.message?.text;}
async function callResult(name, args) {
  await connectPromise;
  if(!alive||!supportsTools())throw new Error('宿主消息桥不可用；编辑保留在页面中，可保存项目或导出反馈');
  return bridge.callServerTool({name,arguments:args},{timeout:30000,signal:lifecycle.signal});
}
async function call(name, args) {return toolValue(await callResult(name,args)).value;}
async function sessionImage(value) {
  const maximum=96*1024*1024;
  if(!supportsTools()&&typeof value.image==='string'){
    if(value.image.length>maximum)throw new Error('会话图片过大');
    return value.image;
  }
  // Fetch the immutable image directly through the app-only bridge. Agent
  // tool results may transform large strings before forwarding them to the UI;
  // accepting an inline encoding there can leave a connected but empty editor.
  let length=value.imageInfo?.length;
  if(length!==undefined&&(!Number.isSafeInteger(length)||length<1||length>maximum))throw new Error('会话图片信息无效');
  const chunks=[];let offset=0;
  document.body.dataset.imageState='loading';
  do{
    const result=await callResult('edithere_web_image',{sessionId:value.sessionId,token:value.token,offset});
    const info=toolValue(result).value, chunk=result?._meta?.edithereImageChunk;
    length??=info.totalLength;
    if(!Number.isSafeInteger(length)||length<1||length>maximum)throw new Error('会话图片信息无效');
    if(typeof chunk!=='string'||chunk.length>1024*1024||info.offset!==offset||info.totalLength!==length||
        !Number.isSafeInteger(info.nextOffset)||info.nextOffset<=offset||info.nextOffset>length||chunk.length!==info.nextOffset-offset)
      throw new Error('会话图片分块不完整，请重新打开会话');
    chunks.push(chunk);offset=info.nextOffset;
    document.body.dataset.imageReceived=String(offset);
    document.body.dataset.imageLength=String(length);
  }while(offset<length);
  document.body.dataset.imageState='received';
  return chunks.join('');
}
function setBusy(value) {working=value;submit.disabled=value;cancel.disabled=value;}
function unlockOffline() {
  editor.releaseAgent();$('workspace').inert=false;delete document.body.dataset.agentLocked;
  cancel.onclick=originalOpen;cancel.querySelector('use').setAttribute('href','#i-open');label(cancel,'打开');
  cancel.title='打开图片、视频或项目 (Ctrl / ⌘ + O)';cancel.setAttribute('aria-label','打开图片、视频或项目');cancel.disabled=false;
  submit.onclick=originalExport;label(submit,'导出反馈');submit.title='导出结构化反馈 (Ctrl / ⌘ + E)';submit.setAttribute('aria-label','导出反馈');submit.disabled=false;
}
function stop(reason) {
  if(!alive)return;
  alive=false;clearTimeout(draftTimer);lifecycle.abort();unsubscribe();
  document.body.dataset.mcpState='stopped';
  // Teardown is not a successful draft save. No new tool or chat request is
  // issued after the host stops this view; only acknowledged data is on disk.
  working=false;
  if(receipt){document.body.dataset.messageState='stopped';manualReceipt();}
  else{unlockOffline();editor.notify(reason||'批注连接已停止，已保存草稿可在重新打开会话后恢复');}
}
function persistDraft() {
  draftTail=draftTail.catch(()=>{}).then(async()=>{
    if(!alive||!session||receipt||session.state!=='pending'||!supportsTools())return;
    const revision=editor.projectRevision();
    if(revision===lastDraftRevision)return;
    const project=editor.project();
    // The immutable original is already persisted by this session. Removing
    // its encoding keeps autosaves under the host's tool message size limit.
    delete project.document.image;
    const value=await call('edithere_web_draft',{sessionId:session.sessionId,token:session.token,project});
    if(value.state!=='pending')throw new Error('草稿会话已结束');
    if(!alive)return;
    lastDraftRevision=revision;editor.markAgentDraftSaved(revision);
    document.body.dataset.draftState=revision===editor.projectRevision()?'saved':'unsaved';
  });
  return draftTail;
}
const unsubscribe=editor.onProjectChange(()=>{
  if(!alive||!session||session.state!=='pending'||receipt||!supportsTools())return;
  document.body.dataset.draftState='unsaved';clearTimeout(draftTimer);
  draftTimer=setTimeout(()=>{
    persistDraft().catch(()=>{if(alive){document.body.dataset.draftState='error';editor.notify('草稿暂未保存，编辑保留在页面；点击反馈可重试保存');}});
  },350);
});
function messageText() {
  return 'EditHere 批注已保存。请读取下面的结构化反馈，结合本次对话的用户要求继续处理。\n'+
    JSON.stringify({sessionId:session.sessionId,outputPath:receipt.outputPath,sha256:receipt.sha256})+'\n'+
    JSON.stringify(receipt.feedback||{annotationSpace:'result',objects:[]});
}
function manualReceipt() {
  label(submit,'已保存');submit.disabled=false;submit.onclick=()=>{
    originalExport();editor.notify('反馈已保存。请回到对话说“继续”，或让 AI 读取：'+receipt.outputPath);
  };
  submit.title='反馈已保存；宿主未接收对话消息，可导出或回到对话继续';submit.setAttribute('aria-label',submit.title);
  cancel.disabled=false;label(cancel,'复制路径');cancel.querySelector('use').setAttribute('href','#i-copy');
  cancel.title='复制已保存反馈的路径';cancel.setAttribute('aria-label',cancel.title);
  cancel.onclick=async()=>{
    try{await navigator.clipboard.writeText(receipt.outputPath);editor.notify('路径已复制，回到对话让 AI 读取反馈');}
    catch{editor.notify('反馈路径：'+receipt.outputPath);}
  };
}
async function sendSavedFeedback() {
  if(working||!alive||!receipt)return;
  if(!supportsMessages()){
    document.body.dataset.messageState='unsupported';manualReceipt();
    editor.notify('反馈已保存；宿主不支持自动回传消息，请回到对话说“继续”');return;
  }
  setBusy(true);label(submit,'发送中');document.body.dataset.messageState='sending';
  try{
    const result=await bridge.sendMessage({role:'user',content:[{type:'text',text:messageText()}]},{timeout:10000,signal:lifecycle.signal});
    if(result?.isError)throw new Error('宿主拒绝了这条反馈消息');
    if(!alive)return;
    document.body.dataset.messageState='accepted';label(submit,'已发送');
    submit.title='反馈已保存，宿主已接收消息';submit.setAttribute('aria-label',submit.title);submit.disabled=true;cancel.disabled=true;
    editor.notify('反馈已保存，宿主已接收消息。可回到对话查看');
  }catch(error){
    if(!alive)return;
    document.body.dataset.messageState='failed';manualReceipt();label(submit,'重试发送');submit.onclick=sendSavedFeedback;
    submit.title='反馈已保存；消息未确认送达，重试只发送消息';submit.setAttribute('aria-label',submit.title);
    editor.notify('反馈已保存，消息未确认送达。可重试发送，或回到对话说“继续”');
  }finally{working=false;}
}
async function complete() {
  if(working||!alive||!session||receipt)return;
  if($('note-dialog').open){editor.notify('请先保存或关闭正在编辑的批注');return;}
  setBusy(true);label(submit,'保存中');clearTimeout(draftTimer);
  $('workspace').inert=true;document.body.dataset.agentLocked='true';
  try{
    snapshot ||= editor.feedback();
    // A draft failure must not prevent the final, idempotent feedback save.
    await persistDraft().catch(()=>{});
    const {image:originalImage,...feedback}=snapshot;
    receipt=await call('edithere_web_submit',{sessionId:session.sessionId,token:session.token,feedback});
    if(receipt.state!=='completed'){receipt=null;throw new Error('反馈尚未保存，请重试');}
    if(!alive)return;
    editor.completedAgent();document.body.dataset.messageState='unsent';label(submit,'已保存');
  }catch(error){
    if(alive){label(submit,'重试保存');submit.disabled=false;cancel.disabled=false;editor.notify(error.message||'保存暂时不可用，当前反馈仍保留在页面');}
    return;
  }finally{working=false;}
  await sendSavedFeedback();
}
async function cancelSession() {
  if(working||!alive||receipt||!editor.canCancel())return;
  setBusy(true);clearTimeout(draftTimer);$('workspace').inert=true;
  try{
    await draftTail.catch(()=>{});
    const value=await call('edithere_web_cancel',{sessionId:session.sessionId,token:session.token});
    if(!['cancelled','expired'].includes(value.state))throw new Error('会话已经完成，请回到对话读取反馈');
    session.state=value.state;document.body.dataset.agentState=value.state;unlockOffline();
    editor.notify('批注会话已取消，当前编辑仍可保存项目或导出');
  }catch(error){if(alive){submit.disabled=false;cancel.disabled=false;if(!snapshot)$('workspace').inert=false;editor.notify(error.message);}}
  finally{working=false;}
}
async function openResult(result) {
  const {value,privateData}=toolValue(result);
  if(!privateData)return;
  await connectPromise;
  if(!alive)return;
  if(!/^[a-f0-9]{32}$/i.test(privateData.sessionId||'')||!/^[a-f0-9]{64}$/i.test(privateData.token||''))throw new Error('宿主未传入完整的批注会话，请让 AI 重新打开');
  if(session){
    if(session.sessionId!==privateData.sessionId)editor.notify('当前面板已有批注会话，请为另一张图打开新面板');
    return;
  }
  void expandForEditing();
  const image=await sessionImage(privateData);
  if(!alive)return;
  session={...privateData,image,state:value.state||privateData.state};receipt=privateData.receipt||null;
  editor.bindAgent();
  if(receipt?.state==='completed'&&receipt.feedback){
    await editor.loadAgentFeedback({image:session.image,name:session.name,feedback:{...receipt.feedback,image:session.image}});
  }else if(session.draft){
    const draft=session.draft;
    if(draft.document?.image!==undefined&&draft.document.image!==image)throw new Error('批注草稿与会话原图不匹配');
    await editor.loadAgentProject({image,name:session.name,project:{...draft,document:{...draft.document,image}}});
    lastDraftRevision=editor.projectRevision();
  }else await editor.loadAgentImage(session);
  document.body.dataset.agentState=session.state;document.body.dataset.mcpState='ready';
  document.body.dataset.agentSession=session.sessionId;
  document.body.dataset.imageState='ready';
  $('drop-card').disabled=true;$('demo').disabled=true;
  document.querySelector('.local-badge').title='通过宿主消息桥保存本机批注；反馈与对话消息分别确认';
  if(receipt?.state==='completed'){
    editor.completedAgent();manualReceipt();
    if(supportsMessages()){label(submit,'发送反馈');submit.onclick=sendSavedFeedback;submit.title='反馈已保存；点击将这份反馈发送到当前对话';submit.setAttribute('aria-label',submit.title);}
    editor.notify('已恢复保存的反馈，可重新发送到对话');return;
  }
  if(session.state!=='pending'||!supportsTools()){
    unlockOffline();editor.notify(session.state==='pending'?'宿主不支持反馈保存，请导出 JSON 后回到对话':'会话已结束，已恢复内容仍可导出');return;
  }
  label(submit,'反馈');submit.title='保存批注并发送到当前对话 (Ctrl / ⌘ + E)';submit.setAttribute('aria-label',submit.title);submit.onclick=complete;submit.disabled=false;
  label(cancel,'取消');cancel.querySelector('use').setAttribute('href','#i-close');cancel.title='取消这次批注会话';cancel.setAttribute('aria-label',cancel.title);cancel.onclick=cancelSession;cancel.disabled=false;
}

bridge.ontoolinput=()=>{if(!session)document.body.dataset.agentState='loading';};
bridge.ontoolresult=result=>{
  openTail=openTail.then(()=>openResult(result)).catch(error=>{
    if(alive){document.body.dataset.agentState='error';document.body.dataset.imageState='error';unlockOffline();editor.notify(error.message||'批注素材未能打开');}
  });
};
bridge.ontoolcancelled=()=>stop('宿主已取消本次调用；当前编辑仍可保存项目或导出');
bridge.onteardown=async()=>{stop();return {};};
bridge.onhostcontextchanged=context=>{
  if(context.theme==='dark'||context.theme==='light')document.documentElement.dataset.theme=context.theme;
  if(context.displayMode)document.body.dataset.mcpDisplayMode=context.displayMode;
};
bridge.onerror=()=>{document.body.dataset.mcpState='error';};

if(window.parent===window){
  document.body.dataset.mcpState='standalone';
  editor.notify('这是 MCP 面板文件。直接打开时可离线批注和导出；回传需在支持 MCP Apps 的 Agent 中使用');
}else{
  document.body.dataset.mcpState='connecting';
  connectPromise=bridge.connect(undefined,{timeout:10000,signal:lifecycle.signal}).then(()=>{
    connected=true;document.body.dataset.mcpState='connected';
    const context=bridge.getHostContext();if(context?.theme)bridge.onhostcontextchanged(context);
    if(!supportsTools())editor.notify('宿主不支持 MCP 批注保存，可导出反馈后回到对话');
  }).catch(()=>{
    if(alive){document.body.dataset.mcpState='unsupported';unlockOffline();editor.notify('宿主未建立 MCP Apps 消息桥，可导出反馈后回到对话');}
  });
}
