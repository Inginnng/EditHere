import './style.css';
import { zipSync, strToU8 } from 'fflate';
import { Editor, coordinates } from './editor.js';
import { icon, toolButton, escapeHtml } from './icons.js';
import { makeExport, History, renumber, now, clone } from './model.js';
import { fromPng, fromImageFile, importFiles, captureScreen, cropDocument, disposeDocument, download, previewBlob } from './images.js';
import { chooseCrop } from './crop.js';

const app=document.querySelector('#app');
app.innerHTML='<header class="app-header"><a class="brand" href="./" aria-label="Help2Design 首页"><span class="brand-icon">'+icon('capture')+'</span><span>Help2Design</span><small>Web</small></a><div class="header-actions"><span class="local-label">图片仅在本地处理</span><button class="button" data-action="open">'+icon('image')+'打开图片</button><button class="button primary" data-action="capture">'+icon('capture')+'截取屏幕</button></div></header><main class="workspace"><section class="empty-state"><div class="drop-icon">'+icon('image')+'</div><h1>把修改意见，留在图片上。</h1><p>拖入或粘贴图片，即可开始批注</p><div class="empty-actions"><button class="button primary" data-action="capture">'+icon('capture')+'截取屏幕</button><button class="button" data-action="open">'+icon('upload')+'打开图片</button></div><button class="text-button" data-action="example">用示例试一下</button><small>支持 PNG、JPG、WebP 和已有 JSON 项目</small></section><div class="viewport"></div><div class="drop-overlay">松开以打开图片或项目</div></main><footer class="dock-area" hidden><div class="hint" role="status">单击批注 · 拖动框选 · 滚轮切换大小</div><div class="tool-dock" role="toolbar" aria-label="批注工具">'+toolButton('point','点标注 (P)','point')+toolButton('rect','框选 (R)','rect')+toolButton('smart','智能选块 (B)','smart')+toolButton('select','调整与移动 (V)','select')+'<span class="divider"></span>'+toolButton('undo','撤销','undo')+toolButton('redo','重做','redo')+'<span class="divider"></span>'+toolButton('minus','缩小','zoom-out')+'<button class="zoom-value" data-action="fit" aria-label="适应图片">100%</button>'+toolButton('plus','放大','zoom-in')+toolButton('sidebar','显示或隐藏批注','notes')+toolButton('more','更多操作','more')+'<span class="divider"></span><button class="button primary export-button" data-action="export">'+icon('export')+'导出 JSON</button></div></footer><input id="file-input" type="file" accept="image/png,image/jpeg,image/webp,image/bmp,image/gif,image/avif,.json" multiple hidden/><div class="toast" role="status" aria-live="polite" hidden></div><div class="busy-indicator" role="status" hidden>正在处理…</div>';
const input=app.querySelector('#file-input'),empty=app.querySelector('.empty-state'),dock=app.querySelector('.dock-area'),hint=app.querySelector('.hint');
const history=new History();let current=null,busy=false,toastTimer,menu=null;
const editor=new Editor(app.querySelector('.viewport'),{
 edit:editNote,change:(id,n)=>{history.push(current.notes);current.notes=current.notes.map(item=>item.id===id?n:item);changed();},
 hint:message=>hint.textContent=message,zoom:z=>app.querySelector('.zoom-value').textContent=Math.round(z*100)+'%',action,context:showContext,
});
const worker=new Worker(new URL('./detection.worker.js',import.meta.url),{type:'module'});let requestId=0;const pending=new Map();
worker.onmessage=({data})=>{const task=pending.get(data.id);if(task){pending.delete(data.id);data.error?task.reject(new Error(data.error)):task.resolve(data.candidates);}};
worker.onerror=()=>{for(const task of pending.values())task.reject(new Error('自动识别暂不可用，仍可手动标注。'));pending.clear();};
function detect(doc){
 if(doc.detection)return doc.detection;
 doc.detection=new Promise((resolve,reject)=>{
  const scale=Math.min(1,1000/Math.max(doc.info.width,doc.info.height)),canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(doc.info.width*scale));canvas.height=Math.max(1,Math.round(doc.info.height*scale));
  const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.fillStyle='white';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.drawImage(doc.image,0,0,canvas.width,canvas.height);
  const pixels=ctx.getImageData(0,0,canvas.width,canvas.height),id=++requestId;pending.set(id,{resolve,reject});
  worker.postMessage({id,buffer:pixels.data.buffer,width:canvas.width,height:canvas.height,originalWidth:doc.info.width,originalHeight:doc.info.height},[pixels.data.buffer]);
 });return doc.detection;
}
function notify(message){const box=app.querySelector('.toast');box.textContent=message;box.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>box.hidden=true,4200);}
async function run(fn){if(busy)return;busy=true;app.querySelector('.busy-indicator').hidden=false;try{return await fn();}catch(error){if(error.name!=='NotAllowedError'&&error.name!=='AbortError')notify(error.message||'操作未完成，请重试。');else notify('已取消截图，当前批注已保留。');}finally{busy=false;app.querySelector('.busy-indicator').hidden=true;}}
function showDocument(doc){
 const previous=current;current=doc;history.clear();editor.setDocument(doc);disposeDocument(previous);empty.hidden=true;dock.hidden=false;app.classList.add('has-document');updateControls();hint.textContent='正在识别区域… · 可直接拖动框选';
 detect(doc).then(candidates=>{doc.candidates=candidates;if(current===doc){editor.renderLayers();hint.textContent='滚轮 ↑ 更大 ↓ 更小 · 单击批注 · 拖动框选';app.querySelector('.image-meta').title=doc.info.title+' · '+candidates.length+' 个候选区域';}}).catch(error=>{if(current===doc)hint.textContent=error.message;});
}
function updateControls(){
 app.querySelector('[data-action=undo]').disabled=!history.undoStack.length;app.querySelector('[data-action=redo]').disabled=!history.redoStack.length;
 for(const mode of ['point','rect','smart','select']){const b=app.querySelector('.tool-dock [data-action='+mode+']');b.classList.toggle('active',editor.mode===mode);b.setAttribute('aria-pressed',String(editor.mode===mode));}
 app.querySelector('.tool-dock [data-action=notes]').classList.toggle('active',editor.notesVisible);
}
function changed(){current.dirty=true;renumber(current.notes);editor.refresh();updateControls();}
function ask(title,description,confirmText='继续'){
 return new Promise(resolve=>{
  const dialog=document.createElement('dialog');dialog.className='confirm-dialog';dialog.innerHTML='<h2>'+escapeHtml(title)+'</h2><p>'+escapeHtml(description)+'</p><div class="dialog-actions"><button class="button" data-answer="cancel">取消</button><button class="button primary" data-answer="ok">'+escapeHtml(confirmText)+'</button></div>';
  document.body.append(dialog);dialog.showModal();
  const finish=answer=>{dialog.close();dialog.remove();resolve(answer);};
  dialog.addEventListener('cancel',e=>{e.preventDefault();finish(false);});
  dialog.addEventListener('click',e=>{const a=e.target.closest('[data-answer]')?.dataset.answer;if(a)finish(a==='ok');});
 });
}
async function confirmReplace(){return !current?.dirty||await ask('替换当前图片？','当前批注尚未保存。取消后可先在“更多操作”中保存项目。','替换图片');}
async function editNote(note,isNew,event){
 if(isNew&&current.notes.length>=1000){notify('最多支持 1000 条批注。');return;}
 const dialog=document.createElement('dialog');dialog.className='note-dialog';const number=isNew?current.notes.length+1:note.number;
 dialog.innerHTML='<form><header><h2>批注 '+number+'</h2><span>'+escapeHtml(coordinates(note))+'</span></header><label class="sr-only" for="note-text">修改意见</label><textarea id="note-text" name="comment" placeholder="希望这里怎么改？" rows="4" maxlength="10000" required></textarea><div class="note-error" role="alert"></div><footer><small>⌘ / Ctrl + Enter 保存</small><div><button class="button" type="button" data-cancel>取消</button><button class="button primary" type="submit">保存批注</button></div></footer></form>';
 document.body.append(dialog);dialog.querySelector('textarea').value=note.comment;
 if(innerWidth>600&&event?.clientX){dialog.style.margin='0';dialog.style.left=Math.max(16,Math.min(innerWidth-376,event.clientX+22))+'px';dialog.style.top=Math.max(16,Math.min(innerHeight-306,event.clientY+18))+'px';}
 dialog.showModal();dialog.querySelector('textarea').focus();dialog.querySelector('textarea').setSelectionRange(note.comment.length,note.comment.length);
 const close=()=>{dialog.close();dialog.remove();editor.surface.focus({preventScroll:true});};
 dialog.querySelector('[data-cancel]').onclick=close;dialog.addEventListener('cancel',e=>{e.preventDefault();close();});
 const save=()=>{const comment=dialog.querySelector('textarea').value.trim();if(!comment){dialog.querySelector('.note-error').textContent='请写下修改意见。';return;}history.push(current.notes);const value={...clone(note),comment,updatedAt:now()};if(isNew)current.notes.push(value);else current.notes=current.notes.map(n=>n.id===value.id?value:n);editor.selected=value.id;editor.toggleNotes(true);changed();close();};
 dialog.querySelector('form').onsubmit=e=>{e.preventDefault();save();};dialog.addEventListener('keydown',e=>{if((e.ctrlKey||e.metaKey)&&e.key==='Enter'){e.preventDefault();save();}});
}
function exportDialog(){
 if(!current)return;const doc=current,dialog=document.createElement('dialog');dialog.className='export-dialog';
 dialog.innerHTML='<header><div><h2>导出批注</h2><p>'+doc.notes.length+' 条批注 · '+doc.info.width+' × '+doc.info.height+' 原图像素</p></div><button class="icon-button" type="button" aria-label="关闭导出">'+icon('close')+'</button></header><label class="embed-option"><input type="checkbox"/> 包含原图数据，可独立导入还原</label><textarea class="json-preview" readonly aria-label="标准化 JSON" spellcheck="false"></textarea><p class="export-hint">把 JSON 和原图一起交给 AI，即可定位每条修改意见。</p><footer><button class="button" data-export="bundle">保存 JSON 与图片</button><button class="button primary" data-export="copy">'+icon('copy')+'复制 JSON</button></footer>';
 document.body.append(dialog);const text=dialog.querySelector('textarea'),checkbox=dialog.querySelector('input');const refresh=()=>{text.value=JSON.stringify(makeExport(doc,checkbox.checked),null,2);};refresh();checkbox.onchange=refresh;
 const close=()=>{dialog.close();dialog.remove();};dialog.querySelector('header button').onclick=close;dialog.addEventListener('cancel',e=>{e.preventDefault();close();});
 dialog.querySelector('[data-export=copy]').onclick=async()=>{try{await navigator.clipboard.writeText(text.value);dialog.querySelector('.export-hint').textContent='JSON 已复制。';}catch{text.focus();text.select();dialog.querySelector('.export-hint').textContent='请按 Ctrl+C / ⌘C 复制选中的 JSON。';}};
 dialog.querySelector('[data-export=bundle]').onclick=()=>run(async()=>{const preview=await previewBlob(doc),data=makeExport(doc,checkbox.checked),previewName=doc.info.imageFile.toLowerCase()==='preview.png'?'annotations-preview.png':'preview.png',archive=zipSync({[doc.info.imageFile]:doc.png,'feedback.json':strToU8(JSON.stringify(data,null,2)),[previewName]:new Uint8Array(await preview.arrayBuffer())},{level:3});download('help2design-'+doc.info.id.slice(0,8)+'.zip',new Blob([archive],{type:'application/zip'}));dialog.querySelector('.export-hint').textContent='已导出 ZIP：JSON、原图和带批注的预览图。';});
 dialog.showModal();
}
function closeMenu(){menu?.remove();menu=null;}
function showContext(e){
 closeMenu();if(!current)return;menu=document.createElement('div');menu.className='context-menu';menu.setAttribute('role','menu');
 const items=[['crop','重新裁剪'],['save-project','保存项目'],['copy-image','复制图片'],['save-image','保存原图'],['save-preview','保存批注预览'],['export','导出 JSON'],['candidates',editor.showCandidates?'隐藏候选框':'显示候选框'],['fit','适应图片'],['help','快捷键']];
 menu.innerHTML=items.map(([a,title])=>'<button role="menuitem" data-action="'+a+'">'+title+'</button>').join('');
 document.body.append(menu);menu.style.left=Math.max(8,Math.min(innerWidth-208,e.clientX||innerWidth/2))+'px';menu.style.top=Math.max(8,Math.min(innerHeight-menu.offsetHeight-12,e.clientY||innerHeight-350))+'px';
 menu.onclick=event=>{const a=event.target.closest('[data-action]')?.dataset.action;if(a){closeMenu();action(a,event);}};
 menu.addEventListener('keydown',event=>{const buttons=[...menu.querySelectorAll('button')],i=buttons.indexOf(document.activeElement);if(event.key==='ArrowDown'||event.key==='ArrowUp'){event.preventDefault();buttons[(i+(event.key==='ArrowDown'?1:-1)+buttons.length)%buttons.length].focus();}if(event.key==='Escape')closeMenu();});menu.querySelector('button').focus();
}
function saveProject(){const data=makeExport(current,true);download('review-'+current.info.id.slice(0,8)+'.json',new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));current.dirty=false;notify('项目已下载，包含原图和全部批注。');}
function help(){
 const dialog=document.createElement('dialog');dialog.className='help-dialog';dialog.innerHTML='<h2>鼠标与快捷键</h2><dl><dt>滚轮 ↑ / ↓</dt><dd>智能模式选择更大 / 更小的块</dd><dt>Ctrl / ⌘ + 滚轮</dt><dd>缩放图片</dd><dt>空格 + 拖动</dt><dd>移动图片与批注栏</dd><dt>P / R / B / V</dt><dd>点标注 / 框选 / 智能 / 调整</dd><dt>Ctrl / ⌘ + Z</dt><dd>撤销；加 Shift 重做</dd><dt>Ctrl / ⌘ + S</dt><dd>保存项目</dd><dt>Ctrl / ⌘ + 0</dt><dd>适应图片</dd><dt>Delete</dt><dd>删除选中的批注</dd></dl><button class="button primary">知道了</button>';
 document.body.append(dialog);dialog.querySelector('button').onclick=()=>{dialog.close();dialog.remove();};dialog.addEventListener('close',()=>dialog.remove());dialog.showModal();
}
function action(name,event={}){
 if(['point','rect','smart','select'].includes(name)){editor.setMode(name);hint.textContent=name==='smart'?'滚轮 ↑ 更大 ↓ 更小 · 单击批注 · 拖动框选':'单击或拖动批注 · 空格拖动图片';updateControls();return;}
 if(name==='open'){input.click();return;}
 if(name==='example'){run(async()=>{if(!await confirmReplace())return;const response=await fetch(import.meta.env.BASE_URL+'example.png');if(!response.ok)throw new Error('示例图片无法读取。');showDocument(await fromPng(new Uint8Array(await response.arrayBuffer()),'demo','示例产品页面'));});return;}
 if(name==='capture'){run(async()=>{if(!await confirmReplace())return;const doc=await captureScreen();try{const r=await chooseCrop(doc,{detect});if(r)showDocument(await cropDocument(doc,r));}finally{disposeDocument(doc);}});return;}
 if(!current)return;
 if(name==='crop')run(async()=>{if(current.notes.length&&!await ask('重新裁剪图片？','裁剪会建立一张新图片。现有批注不会跟随，请先保存项目。','继续裁剪'))return;const r=await chooseCrop(current,{detect});if(r)showDocument(await cropDocument(current,r));});
 else if(name==='undo'){current.notes=history.undo(current.notes);changed();}
 else if(name==='redo'){current.notes=history.redo(current.notes);changed();}
 else if(name==='fit')editor.fit();
 else if(name==='zoom-in')editor.setZoom(editor.zoom*1.2);
 else if(name==='zoom-out')editor.setZoom(editor.zoom/1.2);
 else if(name==='notes'){editor.toggleNotes();updateControls();}
 else if(name==='more')showContext(event);
 else if(name==='export')exportDialog();
 else if(name==='save-project')saveProject();
 else if(name==='copy-image')run(async()=>{if(!navigator.clipboard?.write||!window.ClipboardItem)throw new Error('当前浏览器不支持复制图片，请使用“保存原图”。');try{await navigator.clipboard.write([new ClipboardItem({'image/png':new Blob([current.png],{type:'image/png'})})]);notify('图片已复制。');}catch{throw new Error('复制图片未完成，请使用“保存原图”。');}});
 else if(name==='save-image')download(current.info.imageFile,new Blob([current.png],{type:'image/png'}));
 else if(name==='save-preview')run(async()=>download('preview.png',await previewBlob(current)));
 else if(name==='candidates'){editor.showCandidates=!editor.showCandidates;editor.renderLayers();}
 else if(name==='help')help();
 else if(name==='edit-note'){const id=event.target.closest('[data-id]')?.dataset.id,n=current.notes.find(n=>n.id===id);if(n)editNote(n,false,event);}
 else if(name==='delete-note'||name==='delete'){const id=name==='delete'?editor.selected:event.target.closest('[data-id]')?.dataset.id;if(!id)return;history.push(current.notes);current.notes=current.notes.filter(n=>n.id!==id);editor.selected=null;changed();}
 else if(name==='clear')run(async()=>{if(!await confirmReplace())return;disposeDocument(current);current=null;editor.clear();empty.hidden=false;dock.hidden=true;app.classList.remove('has-document');});
}
app.addEventListener('click',e=>{if(e.target.closest('.review-window'))return;const a=e.target.closest('[data-action]')?.dataset.action;if(a)action(a,e);});
document.addEventListener('pointerdown',e=>{if(menu&&!menu.contains(e.target))closeMenu();});
input.onchange=()=>run(async()=>{const files=[...input.files];input.value='';if(!files.length||!await confirmReplace())return;showDocument(await importFiles(files));});
document.addEventListener('paste',e=>{if(e.target.closest('input,textarea,[contenteditable]'))return;const file=[...e.clipboardData.items].find(item=>item.kind==='file'&&item.type.startsWith('image/'))?.getAsFile();if(file){e.preventDefault();run(async()=>{if(await confirmReplace())showDocument(await fromImageFile(file,'clipboard'));});}});
let dragDepth=0;
document.addEventListener('dragenter',e=>{if(!e.dataTransfer.types.includes('Files'))return;e.preventDefault();dragDepth++;app.classList.add('drag-over');});
document.addEventListener('dragover',e=>{if(e.dataTransfer.types.includes('Files'))e.preventDefault();});
document.addEventListener('dragleave',()=>{if(--dragDepth<=0){dragDepth=0;app.classList.remove('drag-over');}});
document.addEventListener('drop',e=>{e.preventDefault();dragDepth=0;app.classList.remove('drag-over');const files=[...e.dataTransfer.files];if(files.length)run(async()=>{if(await confirmReplace())showDocument(await importFiles(files));});});
document.addEventListener('keydown',e=>{
 if(document.querySelector('dialog[open]')||e.target.closest('textarea,input,[contenteditable]'))return;
 if(e.key===' '){editor.space=true;if(current)e.preventDefault();}
 const mod=e.ctrlKey||e.metaKey,key=e.key.toLowerCase();
 if(mod&&key==='o'){e.preventDefault();action('open');return;}
 if(mod&&current&&key==='c'&&!window.getSelection()?.toString()){e.preventDefault();action('copy-image');return;}
 if(mod&&current){const actions={s:'save-project',e:'export','0':'fit',z:e.shiftKey?'redo':'undo',y:'redo'};if(actions[key]){e.preventDefault();action(actions[key],e);return;}}
 if(mod||e.altKey||!current)return;
 const modes={p:'point',r:'rect',b:'smart',v:'select'};
 if(modes[key]){e.preventDefault();action(modes[key]);}
 if(e.key==='Delete'||e.key==='Backspace'){e.preventDefault();action('delete');}
 if(e.key==='Escape'){if(menu)closeMenu();else{editor.cancel();editor.selected=null;editor.renderLayers();}}
 if(e.key==='Tab'&&editor.mode==='smart'&&document.activeElement===editor.surface&&editor.picker.current){e.preventDefault();editor.picker.step(e.shiftKey?-1:1);editor.renderLayers();editor.hint();}
});
document.addEventListener('keyup',e=>{if(e.key===' ')editor.space=false;});
window.addEventListener('blur',()=>{editor.space=false;editor.cancel();});
window.addEventListener('beforeunload',e=>{if(current?.dirty){e.preventDefault();e.returnValue='';}});
updateControls();
