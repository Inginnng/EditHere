/* EditHere Web. Offline editing with an optional local Agent session. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id), L = globalThis.EditHereLayout, F = globalThis.EditHereFeedback;
  const clone = value => JSON.parse(JSON.stringify(value));
  const uid = () => 'id-' + (globalThis.crypto?.randomUUID?.() || Date.now().toString(36) + Math.random().toString(36).slice(2));
  const esc = text => String(text).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const clamp = (v, low, high) => Math.max(low, Math.min(high, v));
  const rect = (a,b) => ({x1:Math.min(a.x,b.x), y1:Math.min(a.y,b.y), x2:Math.max(a.x,b.x), y2:Math.max(a.y,b.y)});
  const same = (a,b) => JSON.stringify(a) === JSON.stringify(b);
  const contains = (r,p) => p.x >= r.x1 && p.x < r.x2 && p.y >= r.y1 && p.y < r.y2;
  const timeText = seconds => {const ms=Math.max(0,Math.round((seconds||0)*1000));return String(Math.floor(ms/60000)).padStart(2,'0')+':'+String(Math.floor(ms/1000)%60).padStart(2,'0')+'.'+String(ms%1000).padStart(3,'0');};
  const video = $('video'), canvas = $('canvas'), stage = $('stage'), ctx = canvas.getContext('2d');
  let session = null, doc = null, mode = 'smart', selected = null, selectedNote = null, zoom = 1, fitMode = true;
  let agentBound = false, projectRevision = 0;
  const projectListeners = new Set();
  const selectedGroups = new Set();
  let drag = null, hover = null, hoverIndex = 0, draftNote = null, busy = false, dirty = false, toastTimer, mediaURL = null;
  let explodeEnabled = false, explosionPreview = null;
  let panX = 0, panY = 0, pan = null, spaceDown = false, spaceUsed = false, annotationsVisible = true, hoveredMovement = null;
  let suppressPinClickUntil = 0, componentWheel = null, exportData = null, exportText = '', exportCache = null, feedbackStamp = null;
  let seeking = false, presentedTime = null, frameCallback = null, linkSignature = null, detectionId = 0;
  const histories = new WeakMap(), detections = new WeakMap(), pendingDetections = new Map();
  let worker;
  try {
    const url = URL.createObjectURL(new Blob([$('detector-code').textContent + '\nself.onmessage=e=>{const d=e.data;try{self.postMessage({id:d.id,regions:EditHereDetector.detectRegions(new Uint8ClampedArray(d.buffer),d.w,d.h,d.ow,d.oh)})}catch(error){self.postMessage({id:d.id,error:error.message})}};'], {type:'text/javascript'}));
    worker = new Worker(url); URL.revokeObjectURL(url);
    worker.onmessage = ({data}) => {const job=pendingDetections.get(data.id);if(job){pendingDetections.delete(data.id);data.error?job.reject(new Error(data.error)):job.resolve(data.regions);}};
    worker.onerror = () => {for(const job of pendingDetections.values())job.reject(new Error('自动选块不可用，仍可手动框选'));pendingDetections.clear();worker.terminate();worker=null;};
  } catch { worker = null; }
  function notify(text) {$('toast').textContent=text;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,4300);}
  async function run(fn) {
    if(busy)return;
    busy=true;document.body.dataset.busy='true';
    const controls=[...document.querySelectorAll('.app,.header-actions')];controls.forEach(element=>element.inert=true);
    updateTime();
    try{return await fn();}
    catch(error){notify(error.message||'操作未完成');}
    finally{busy=false;delete document.body.dataset.busy;controls.forEach(element=>element.inert=false);updateTime();}
  }
  function sizeOK(w,h) {if(!Number.isInteger(w)||!Number.isInteger(h)||w<1||h<1||w>32767||h>32767||w*h>32000000)throw new Error('素材尺寸过大或无效，最多支持 3200 万像素');}
  function makeCanvas(w,h) {sizeOK(w,h);const c=document.createElement('canvas');c.width=w;c.height=h;return c;}
  async function decode(data) {if(typeof data!=='string'||!/^data:image\/(png|jpeg|webp|bmp|gif|avif);base64,/i.test(data))throw new Error('项目必须包含本地内嵌图片');const image=new Image();image.src=data;await image.decode();sizeOK(image.naturalWidth,image.naturalHeight);return image;}
  function makeDoc(image,data,timestampUs=null,id=uid()) {return {id,image,data,timestampUs,notes:[],layout:null};}
  function history(d=doc) {if(!histories.has(d))histories.set(d,{undo:[],redo:[]});return histories.get(d);}
  const snapshot = d => ({notes:clone(d.notes),layout:clone(d.layout)});
  function remember(before=snapshot(doc)) {const h=history();h.undo.push(before);if(h.undo.length>40)h.undo.shift();h.redo=[];componentWheel=null;}
  function hasContent(d) {return d.notes.length>0||rawMovements(d).some(m=>!equalRect(m.source,m.destination));}
  function changed() {dirty=true;projectRevision++;for(const listener of projectListeners){try{listener(projectRevision);}catch{ /* Transport errors must not interrupt editing. */ }}}
  function commit() {if(session.kind==='video'&&hasContent(doc)&&!session.frames.includes(doc)){if(session.frames.length>=1000)throw new Error('最多支持 1000 个批注画面');session.frames.push(doc);session.frames.sort((a,b)=>a.timestampUs-b.timestampUs);}changed();refresh();}
  function viewportFit() {
    if(!session)return;
    const v=$('viewport'),origin=v.getBoundingClientRect(),gap=v.clientWidth>=900?12:8;
    const bounds={x1:gap,y1:gap,x2:Math.max(gap+1,v.clientWidth-gap),y2:Math.max(gap+1,v.clientHeight-gap)};
    const blockers=[];
    for(const selector of ['.header','.media-bar','.view-controls','.tools','.status-bar','#video-controls','.sidebar','.utility-dock']){
      const element=document.querySelector(selector);if(!element||element.hidden)continue;
      const style=getComputedStyle(element),r=element.getBoundingClientRect();
      if(style.display==='none'||style.visibility==='hidden'||Number(style.opacity)===0||r.width<=0||r.height<=0)continue;
      const b={x1:Math.max(bounds.x1,r.left-origin.left-gap),y1:Math.max(bounds.y1,r.top-origin.top-gap),x2:Math.min(bounds.x2,r.right-origin.left+gap),y2:Math.min(bounds.y2,r.bottom-origin.top+gap)};
      if(b.x2>b.x1&&b.y2>b.y1)blockers.push({...b,selector});
    }
    // Sweep the chrome's horizontal edges. For each span, merge occupied
    // vertical intervals, then fit the image in every remaining rectangle.
    // This also finds space beside top controls on a short landscape viewport.
    const edges=[...new Set([bounds.x1,bounds.x2,...blockers.flatMap(b=>[b.x1,b.x2])])].sort((a,b)=>a-b);
    let best=null;
    function consider(x1,x2,y1,y2){const w=x2-x1,h=y2-y1;if(w<=0||h<=0)return;const scale=Math.min(1,w/session.width,h/session.height),area=w*h;if(!best||scale>best.scale+1e-9||Math.abs(scale-best.scale)<=1e-9&&area>best.area)best={x1,x2,y1,y2,scale,area};}
    const compact=v.clientWidth<900||v.clientWidth/v.clientHeight<=1.2;
    if(compact&&v.clientHeight>430){
      const top=Math.max(bounds.y1,...blockers.filter(b=>['.header','.media-bar','.view-controls'].includes(b.selector)||b.selector==='.utility-dock'&&b.y1<v.clientHeight/2).map(b=>b.y2));
      const bottom=Math.min(bounds.y2,...blockers.filter(b=>['.sidebar','.tools','#video-controls'].includes(b.selector)||b.selector==='.utility-dock'&&b.y1>=v.clientHeight/2).map(b=>b.y1));
      consider(bounds.x1,bounds.x2,top,bottom);
    }else for(let i=0;i<edges.length-1;i++)for(let j=i+1;j<edges.length;j++){
      const x1=edges[i],x2=edges[j],occupied=blockers.filter(b=>b.x1<x2-1e-7&&b.x2>x1+1e-7).sort((a,b)=>a.y1-b.y1);
      let y=bounds.y1;
      for(const b of occupied){if(b.y1>y)consider(x1,x2,y,b.y1);y=Math.max(y,b.y2);}
      if(y<bounds.y2)consider(x1,x2,y,bounds.y2);
    }
    if(!best){notify('展开的面板占满了画布，收起修改意见面板即可适应画面');return;}
    zoom=Math.max(.00001,best.scale);
    panX=best.x1+(best.x2-best.x1-session.width*zoom)/2;panY=best.y1+(best.y2-best.y1-session.height*zoom)/2;
    fitMode=true;resizeStage();
  }
  function resizeStage() {if(!session)return;stage.style.width=session.width*zoom+'px';stage.style.height=session.height*zoom+'px';stage.style.left=panX+'px';stage.style.top=panY+'px';$('fit').textContent=(zoom<.1?(zoom*100).toFixed(1):Math.round(zoom*100))+'%';render();}
  function setZoom(value,anchor=null) {
    if(!session)return;
    const v=$('viewport'),r=v.getBoundingClientRect(),at=anchor?{x:anchor.clientX-r.left,y:anchor.clientY-r.top}:{x:v.clientWidth/2,y:v.clientHeight/2};
    const next=clamp(value,.00001,16),factor=next/zoom;
    panX=at.x-(at.x-panX)*factor;panY=at.y-(at.y-panY)*factor;zoom=next;fitMode=false;resizeStage();
  }
  function changeMode(value) {if(drag)cancelDrag();mode=value;hover=null;hoveredMovement=null;selectedNote=null;document.querySelectorAll('[data-mode]').forEach(b=>{b.classList.toggle('active',b.dataset.mode===mode);b.setAttribute('aria-pressed',String(b.dataset.mode===mode));});stage.style.cursor=spaceDown||mode==='pan'?'grab':mode==='select'?'default':'crosshair';$('hint').textContent=({select:doc?.layout?'拖动或八向缩放 · Ctrl / ⌘ 多选 · 组件上滚轮缩放':'拖动批注调整位置 · 点击编号编辑文字',smart:'悬停选块 · 滚轮切换大小 · 拖动手动框选',point:'点一下，精确指向一个像素',rect:'拖出范围，写下修改意见',cut:'拖出组件范围，再拖动或缩放',pan:'拖动自由画布 · 滚轮缩放 · 0 适应画面'})[mode];render();}
  function activate() {annotationsVisible=true;syncAnnotationVisibility();endExplosion();explodeEnabled=!!doc?.layout;$('empty').hidden=true;$('workspace').hidden=false;$('save-project').disabled=false;$('save-json').disabled=false;$('export').disabled=false;$('media-name').textContent=session.name;$('media-meta').textContent=session.width+' × '+session.height+' · '+(session.kind==='video'?'视频':'图片');canvas.width=session.width;canvas.height=session.height;$('overlay').setAttribute('viewBox',`0 0 ${session.width} ${session.height}`);$('video-controls').hidden=session.kind!=='video';selected=null;selectedNote=null;hover=null;dirty=false;changeMode('smart');refresh();viewportFit();if(doc?.notes.length)globalThis.EditHereChrome?.revealNote(null);}
  function releaseVideo() {if(frameCallback!==null&&video.cancelVideoFrameCallback)video.cancelVideoFrameCallback(frameCallback);frameCallback=null;video.pause();video.removeAttribute('src');video.load();if(mediaURL)URL.revokeObjectURL(mediaURL);mediaURL=null;presentedTime=null;linkSignature=null;seeking=false;}
  function replaceOK() {if(agentBound){notify('当前素材由 AI 批注会话传入；完成或取消后再打开其他素材');return false;}return !dirty||confirm('当前编辑还没有保存。要替换素材吗？可先取消并保存项目。');}
  async function openImage(file) {const url=URL.createObjectURL(file);try{const image=new Image();image.src=url;await image.decode();sizeOK(image.naturalWidth,image.naturalHeight);const c=makeCanvas(image.naturalWidth,image.naturalHeight);c.getContext('2d').drawImage(image,0,0);const data=c.toDataURL('image/png'),decoded=await decode(data);releaseVideo();session={kind:'image',name:file.name||'粘贴的图片',width:image.naturalWidth,height:image.naturalHeight,document:makeDoc(decoded,data)};doc=session.document;activate();detect(doc);}finally{URL.revokeObjectURL(url);}}
  function waitEvent(target,event,timeout=15000) {return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{clean();reject(new Error('读取素材超时，请换一个浏览器支持的文件'));},timeout);const good=()=>{clean();resolve();},bad=()=>{clean();reject(new Error('浏览器无法解码这个视频，请使用 H.264 MP4 或 WebM'));};const clean=()=>{clearTimeout(timer);target.removeEventListener(event,good);target.removeEventListener('error',bad);};target.addEventListener(event,good,{once:true});target.addEventListener('error',bad,{once:true});});}
  function trackFrames() {if(!video.requestVideoFrameCallback)return;frameCallback=video.requestVideoFrameCallback((now,info)=>{presentedTime=info.mediaTime;trackFrames();});}
  async function prepareVideo(file) {const probe=document.createElement('video'),url=URL.createObjectURL(file);probe.preload='auto';try{const ready=waitEvent(probe,'loadeddata');probe.src=url;await ready;sizeOK(probe.videoWidth,probe.videoHeight);if(!Number.isFinite(probe.duration))throw new Error('视频时长无效');return {url,width:probe.videoWidth,height:probe.videoHeight,durationMs:Math.round(probe.duration*1000)};}catch(error){URL.revokeObjectURL(url);throw error;}finally{probe.removeAttribute('src');probe.load();}}
  async function attachVideo(file,prepared) {mediaURL=prepared.url;const ready=waitEvent(video,'loadeddata');video.src=mediaURL;await ready;linkSignature={name:file.name,size:file.size,lastModified:file.lastModified};trackFrames();}
  async function openVideo(file) {const prepared=await prepareVideo(file);releaseVideo();session={kind:'video',name:file.name,width:prepared.width,height:prepared.height,video:{source:file.name,durationMs:prepared.durationMs,width:prepared.width,height:prepared.height},signature:{name:file.name,size:file.size,lastModified:file.lastModified},frames:[]};doc=null;await attachVideo(file,prepared);activate();updateTime();}
  function liveFrame() {endExplosion();explodeEnabled=false;doc=null;selected=null;selectedNote=null;hover=null;canvas.hidden=true;video.hidden=false;render();renderNotes();renderGeometry();renderFrames();}
  function ensureDoc() {
    if(!session)throw new Error('请先打开素材');
    if(doc)return doc;
    if(!mediaURL)throw new Error('请重新选择源视频，才能在新的时间点批注');
    if(seeking||video.readyState<2||video.requestVideoFrameCallback&&presentedTime===null)throw new Error('正在定位画面，请稍后批注');
    if(!video.paused)throw new Error('请先暂停视频，再开始批注');
    const timestampUs=Math.round((presentedTime??video.currentTime)*1000000);
    const existing=session.frames.find(f=>f.timestampUs===timestampUs);
    if(existing){doc=existing;}else{const c=makeCanvas(session.width,session.height);c.getContext('2d').drawImage(video,0,0);const data=c.toDataURL('image/png');const image=new Image();image.src=data;doc=makeDoc(image,data,timestampUs,'t-'+timestampUs);image.decode().then(()=>{if(doc?.image===image)render();}).catch(()=>{});}
    canvas.hidden=false;video.hidden=true;detect(doc);refresh();return doc;
  }
  async function seekTo(seconds) {if(!mediaURL){notify('已保存的批注帧可查看；继续播放需要重新选择源视频');return;}video.pause();liveFrame();const target=clamp(seconds,0,video.duration);if(Math.abs(video.currentTime-target)<.00001){updateTime();return;}const ready=waitEvent(video,'seeked');video.currentTime=target;await ready;updateTime();}
  function updateTime() {if(!session||session.kind!=='video')return;const duration=mediaURL?video.duration:session.video.durationMs/1000;const current=doc?doc.timestampUs/1000000:video.currentTime;$('timeline').max=Number.isFinite(duration)?duration:1;$('timeline').value=current||0;$('timeline').disabled=!mediaURL||busy;$('time').textContent=timeText(current)+' / '+timeText(duration);$('play').querySelector('use').setAttribute('href',video.paused?'#i-play':'#i-pause');$('play').setAttribute('aria-label',video.paused?'播放视频':'暂停视频');$('play').disabled=!mediaURL||busy;$('back').disabled=!mediaURL||busy;$('forward').disabled=!mediaURL||busy;$('frame-hint').hidden=!!doc||!video.paused;$('frame-hint').textContent=mediaURL?'已暂停 · 添加批注时保存这一帧':'源视频未关联 · 选择源视频后继续批注';}
  async function selectFrame(id) {const target=session.frames.find(f=>f.id===id);if(!target)return;video.pause();endExplosion();doc=target;explodeEnabled=!!doc.layout;selected=null;selectedNote=null;hover=null;canvas.hidden=false;video.hidden=true;refresh();if(mode==='cut'&&!doc.layout)changeMode('smart');detect(doc);updateTime();}
  function detect(d) {if(detections.has(d))return detections.get(d);const promise=(async()=>{await d.image.decode();const ratio=Math.min(1,1000/Math.max(session.width,session.height)),w=Math.max(1,Math.round(session.width*ratio)),h=Math.max(1,Math.round(session.height*ratio));const c=makeCanvas(w,h),context=c.getContext('2d',{willReadFrequently:true});context.fillStyle='white';context.fillRect(0,0,w,h);context.drawImage(d.image,0,0,w,h);const pixels=context.getImageData(0,0,w,h);let regions;if(worker){regions=await new Promise((resolve,reject)=>{const id=++detectionId;pendingDetections.set(id,{resolve,reject});worker.postMessage({id,buffer:pixels.data.buffer,w,h,ow:d.image.naturalWidth,oh:d.image.naturalHeight},[pixels.data.buffer]);});}else regions=EditHereDetector.detectRegions(pixels.data,w,h,d.image.naturalWidth,d.image.naturalHeight);d.candidates=regions;if(doc===d)render();return regions;})().catch(error=>{d.candidates=[];if(doc===d)notify(error.message);return [];});detections.set(d,promise);return promise;}
  function point(event) {const r=stage.getBoundingClientRect();return {x:clamp((event.clientX-r.left)/zoom,0,session.width),y:clamp((event.clientY-r.top)/zoom,0,session.height)};}
  function candidateAt(p) {const choices=doc?.layout?L.hit(doc.layout,p.x,p.y):(doc?.candidates||[]).filter(c=>contains(c.bounds,p)).sort((a,b)=>(a.bounds.x2-a.bounds.x1)*(a.bounds.y2-a.bounds.y1)-(b.bounds.x2-b.bounds.x1)*(b.bounds.y2-b.bounds.y1));return choices[clamp(hoverIndex,0,Math.max(0,choices.length-1))]?.bounds||null;}
  function visualRect(r,stroke,fill='none',dash='') {return `<rect x="${r.x1}" y="${r.y1}" width="${r.x2-r.x1}" height="${r.y2-r.y1}" stroke="${stroke}" fill="${fill}" stroke-width="1.5" vector-effect="non-scaling-stroke" ${dash?'stroke-dasharray="'+dash+'"':''}/>`;}
  const equalRect = (a,b) => !!a&&!!b&&['x1','y1','x2','y2'].every(k=>Math.abs(a[k]-b[k])<1e-7);
  const coversRect = (a,b) => a.x1<=b.x1+1e-7&&a.y1<=b.y1+1e-7&&a.x2>=b.x2-1e-7&&a.y2>=b.y2-1e-7;
  const areaOf = r => (r.x2-r.x1)*(r.y2-r.y1);
  const unionRect = (a,b) => a?{x1:Math.min(a.x1,b.x1),y1:Math.min(a.y1,b.y1),x2:Math.max(a.x2,b.x2),y2:Math.max(a.y2,b.y2)}:clone(b);
  function rawMovements(d=doc) {return d?.layout?L.trackedMovements(d.layout):[];}
  function movementMarkers(d=doc) {
    return rawMovements(d).filter(m=>!equalRect(m.source,m.destination)).map((m,i)=>{
      const exact=d.notes.find(n=>n.movementSource&&equalRect(n.movementSource,m.source));
      const note=exact||d.notes.filter(n=>n.movementSource&&coversRect(m.source,n.movementSource)).sort((a,b)=>areaOf(a.movementSource)-areaOf(b.movementSource))[0];
      return {...m,id:'move-'+(m.groupId||JSON.stringify(m.source)),noteId:note?.id||null,index:i};
    });
  }
  function movementDestination(source,d=doc) {
    const tracked=rawMovements(d).find(m=>equalRect(m.source,source));if(tracked)return tracked.destination;
    if(!d?.layout)return null;let shared=null,covered=0;
    for(const piece of d.layout.pieces){const overlap={x1:Math.max(source.x1,piece.source.x1),y1:Math.max(source.y1,piece.source.y1),x2:Math.min(source.x2,piece.source.x2),y2:Math.min(source.y2,piece.source.y2)};if(overlap.x2<=overlap.x1||overlap.y2<=overlap.y1)continue;
      const sx=(piece.to.x2-piece.to.x1)/(piece.source.x2-piece.source.x1),sy=(piece.to.y2-piece.to.y1)/(piece.source.y2-piece.source.y1),t={sx,sy,tx:piece.to.x1-piece.source.x1*sx,ty:piece.to.y1-piece.source.y1*sy};
      if(shared&&Object.keys(t).some(k=>Math.abs(t[k]-shared[k])>1e-7))return null;shared=t;covered+=areaOf(overlap);}
    if(!shared||Math.abs(covered-areaOf(source))>Math.max(1,areaOf(source))*1e-7)return null;
    return {x1:source.x1*shared.sx+shared.tx,y1:source.y1*shared.sy+shared.ty,x2:source.x2*shared.sx+shared.tx,y2:source.y2*shared.sy+shared.ty};
  }
  function markerLine(m) {
    const a=m.source,b=m.destination;let start={x:(a.x1+a.x2)/2,y:(a.y1+a.y2)/2},end={x:(b.x1+b.x2)/2,y:(b.y1+b.y2)/2};
    if(Math.hypot(end.x-start.x,end.y-start.y)<.01){const first=Math.hypot(b.x1-a.x1,b.y1-a.y1),last=Math.hypot(b.x2-a.x2,b.y2-a.y2);start=first>=last?{x:a.x1,y:a.y1}:{x:a.x2,y:a.y2};end=first>=last?{x:b.x1,y:b.y1}:{x:b.x2,y:b.y2};}
    return {start,end};
  }
  function markerAnchor(m,scale=zoom) {const {start,end}=markerLine(m),dx=(end.x-start.x)*scale,dy=(end.y-start.y)*scale,length=Math.hypot(dx,dy);let x=(start.x+end.x)/2*scale,y=(start.y+end.y)/2*scale;if(length>0&&length<40){const shift=20,lengthSafe=Math.max(length,.001);x-=dy/lengthSafe*shift;y+=dx/lengthSafe*shift;}return {x:clamp(x,14,Math.max(14,session.width*scale-14)),y:clamp(y,14,Math.max(14,session.height*scale-14))};}
  function hitMovement(p) {if(!annotationsVisible||!doc?.layout)return null;const markers=movementMarkers();for(let i=markers.length-1;i>=0;i--){const m=markers[i],{start,end}=markerLine(m),dx=end.x-start.x,dy=end.y-start.y,length=dx*dx+dy*dy,t=length?clamp(((p.x-start.x)*dx+(p.y-start.y)*dy)/length,0,1):0;if(Math.hypot(p.x-start.x-dx*t,p.y-start.y-dy*t)*zoom<8)return m;}return null;}
  function componentNoteBounds(n,d=doc) {return !d?.layout?null:(n.groupIds||[n.groupId]).filter(Boolean).map(id=>L.bounds(d.layout,id)).filter(Boolean).reduce((r,b)=>unionRect(r,b),null);}
  const movementPlaceholder = '位置变化，点击可写下想法...';
  function noteEntries() {if(!doc)return [];const out=doc.notes.map(n=>({...n,kind:n.movementSource?'位移意见':n.source===null?'全局意见':n.groupId||n.groupIds?'组件意见':n.source.x1===n.source.x2?'点批注':'框批注',...(n.movementSource?{movement:true,destination:movementDestination(n.movementSource)||n.source}:{})}));for(const m of movementMarkers())if(!m.noteId)out.push({...m,text:'',kind:'布局调整',movement:true,draft:true});return out;}
  function noteRect(n) {return n.movementSource?movementDestination(n.movementSource)||n.source:componentNoteBounds(n)||n.source;}
  function render() {
    if(!session)return;
    if(doc){canvas.hidden=false;video.hidden=true;ctx.clearRect(0,0,canvas.width,canvas.height);if(doc.image.complete&&doc.image.naturalWidth){if(doc.layout)L.paint(ctx,doc.image,doc.layout);else ctx.drawImage(doc.image,0,0);}}
    else{canvas.hidden=true;video.hidden=false;}
    let svg='';
    if(explodeEnabled&&doc?.layout&&mode==='cut'){for(const id of selectionIds()){const b=L.bounds(doc.layout,id);if(b)svg+=visualRect(b,'#346df1');}}
    if(hover&&!drag)svg+=visualRect(hover,'#346df1','#346df112');
    const entries=noteEntries(),markers=movementMarkers();
    if(annotationsVisible){
      for(const m of markers){const {start,end}=markerLine(m),dx=end.x-start.x,dy=end.y-start.y,length=Math.hypot(dx,dy);if(length<.0001)continue;const active=hoveredMovement===m.id||selectedNote===(m.noteId||m.id),head=Math.min(8/zoom,length*.35),ux=dx/length,uy=dy/length,tail1={x:end.x-ux*head-uy*head*.5,y:end.y-uy*head+ux*head*.5},tail2={x:end.x-ux*head+uy*head*.5,y:end.y-uy*head-ux*head*.5};
        if(active)svg+=visualRect(m.source,'#346df1','none','4 3')+visualRect(m.destination,'#346df1','#346df108','4 3');
        svg+=`<g class="movement-arrow ${active?'active':''}" data-movement="${esc(m.id)}" data-source="${esc(JSON.stringify(m.source))}" data-destination="${esc(JSON.stringify(m.destination))}"><path d="M${start.x},${start.y} L${end.x},${end.y}" stroke="white" stroke-opacity=".8" stroke-width="${active?4:3}" vector-effect="non-scaling-stroke"/><path class="movement-line" data-movement="${esc(m.id)}" data-source="${esc(JSON.stringify(m.source))}" data-destination="${esc(JSON.stringify(m.destination))}" d="M${start.x},${start.y} L${end.x},${end.y}" stroke="${active?'#2456c7':'#346df1'}" stroke-width="${active?1.8:1.2}" vector-effect="non-scaling-stroke" style="pointer-events:stroke;cursor:pointer"/><path d="M${tail1.x},${tail1.y} L${end.x},${end.y} L${tail2.x},${tail2.y}" fill="none" stroke="#346df1" stroke-width="1.5" vector-effect="non-scaling-stroke"/><circle cx="${start.x}" cy="${start.y}" r="${2.5/zoom}" fill="white" stroke="#346df1" vector-effect="non-scaling-stroke"/></g>`;
      }
      for(const n of entries){if(n.movement)continue;const r=noteRect(n);if(r&&r.x2>r.x1)svg+=visualRect(r,n.id===selectedNote?'#2456c7':'#346df1',n.id===selectedNote?'#346df110':'none');}
    }
    if(drag?.type==='draw')svg+=visualRect(rect(drag.start,drag.current),'#346df1','#346df110','4 3');
    if(mode==='select'&&explodeEnabled&&selected&&doc?.layout){const b=selectionBounds();if(b){svg+=visualRect(b,'#346df1');for(const [handle,p] of Object.entries(resizeHandles(b)))svg+=`<rect data-handle="${handle}" x="${p.x-4/zoom}" y="${p.y-4/zoom}" width="${8/zoom}" height="${8/zoom}" fill="white" stroke="#346df1" vector-effect="non-scaling-stroke" style="pointer-events:all;cursor:${handle}-resize"/>`;}}
    const adjusted=mode==='select'&&doc?.notes.find(n=>n.id===selectedNote&&!n.movementSource),adjustedBounds=adjusted&&noteRect(adjusted);
    if(adjustedBounds&&adjustedBounds.x2>adjustedBounds.x1){svg+=visualRect(adjustedBounds,'#346df1','#346df108');for(const [handle,p] of Object.entries(resizeHandles(adjustedBounds)))svg+=`<rect data-note-handle="${handle}" x="${p.x-4/zoom}" y="${p.y-4/zoom}" width="${8/zoom}" height="${8/zoom}" fill="white" stroke="#346df1" vector-effect="non-scaling-stroke" style="pointer-events:all;cursor:${handle}-resize"/>`;}
    $('overlay').innerHTML=svg;
    $('pins').innerHTML=annotationsVisible?entries.map((n,i)=>{if(n.draft)return '';const r=noteRect(n),marker=markers.find(m=>m.noteId===n.id),at=marker?markerAnchor(marker):r?{x:r.x1*zoom,y:r.y1*zoom}:null,label=n.text||(n.movement?movementPlaceholder:'');return at?`<button class="pin ${n.id===selectedNote?'selected':''} ${marker?'movement-pin':''}" data-note="${esc(n.id)}" style="left:${at.x}px;top:${at.y}px" aria-label="批注 ${i+1}：${esc(label)}" title="${esc(label)}">${i+1}</button>`:'';}).join(''):'';
  }
  function renderNotes() {
    const entries=noteEntries();$('count').textContent=entries.length;globalThis.EditHereChrome?.refreshCount(entries.length);
    $('notes').innerHTML=entries.length?entries.map((n,i)=>`<article class="note-card ${n.id===selectedNote?'selected':''} ${n.movement&&!n.text.trim()?'movement-empty':''}" data-note="${esc(n.id)}" tabindex="0" title="${esc(n.movement?locationText(n.movementSource||n.source)+' → '+locationText(n.destination):locationText(noteRect(n)))}"><div class="note-top"><span class="note-number">${i+1}</span><span class="note-kind">${n.kind}</span><div class="note-actions"><button data-edit="${esc(n.id)}" aria-label="${n.draft?'添加位移意见':'编辑批注 '+(i+1)}" title="${n.draft?'添加位移意见':'编辑批注'}"><svg class="icon" aria-hidden="true"><use href="#i-edit"/></svg></button>${n.draft?'':`<button data-delete="${esc(n.id)}" aria-label="删除批注 ${i+1}" title="删除批注"><svg class="icon" aria-hidden="true"><use href="#i-trash"/></svg></button>`}</div></div><p>${esc(n.text||(n.movement?movementPlaceholder:''))}</p></article>`).join(''):'<div class="note-empty">点选画面，添加意见</div>';
    $('preview').disabled=!doc;
  }
  function locationText(source) {if(source===null)return '整个画面';return source.x1===source.x2?`点 (${Math.round(source.x1)}, ${Math.round(source.y1)})`:`(${Math.round(source.x1)}, ${Math.round(source.y1)}) · ${Math.round(source.x2-source.x1)} × ${Math.round(source.y2-source.y1)}`;}
  function renderGeometry() {const b=explodeEnabled?selectionBounds():null;globalThis.EditHereChrome?.syncGeometry(b?doc.id+':'+selectionIds().join(','):null);if(b){$('gx').value=Math.round(b.x1);$('gy').value=Math.round(b.y1);$('gw').value=Math.round(b.x2-b.x1);$('gh').value=Math.round(b.y2-b.y1);const title=document.querySelector('.geometry-head strong');if(title)title.textContent=selectionIds().length>1?selectionIds().length+' 个组件':'组件布局';}}
  function renderFrames() {if(session?.kind!=='video')return;$('frame-list').innerHTML=session.frames.filter(hasContent).map(f=>{const count=f.notes.length+movementMarkers(f).filter(m=>!m.noteId).length;return `<button data-frame="${esc(f.id)}" class="${f===doc?'active':''}" title="${count} 条意见">${timeText(f.timestampUs/1000000)} · ${count}</button>`;}).join('');}
  function refresh() {if(!doc?.layout)explodeEnabled=false;if(!selected)selectedGroups.clear();render();renderNotes();renderGeometry();renderFrames();const h=doc?history():null;$('undo').disabled=!h?.undo.length;$('redo').disabled=!h?.redo.length;$('explode').classList.toggle('active',explodeEnabled);$('explode').setAttribute('aria-pressed',String(explodeEnabled));$('explode').title=explodeEnabled?'收起组件模式（保留修改）':'大爆炸 · 拆成可移动的组件';document.querySelector('[data-mode="cut"]').hidden=!explodeEnabled;if($('reset-component'))$('reset-component').disabled=!selected||!doc?.layout;if(session?.kind==='video')updateTime();}
  function editNote(source=null,id=null,groupId=null,movementSource=null,groupIds=null) {if(!session)return;try{ensureDoc();}catch(error){notify(error.message);return;}const existing=id?doc.notes.find(n=>n.id===id):null;if(id&&!existing)return;draftNote=existing?clone(existing):{id:uid(),source:clone(source),text:'',...(groupId?{groupId}:{}),...(groupIds?{groupIds:clone(groupIds)}:{}),...(movementSource?{movementSource:clone(movementSource)}:{})};$('note-location').textContent=(session.kind==='video'?timeText(doc.timestampUs/1000000)+' · ':'')+(draftNote.groupIds?draftNote.groupIds.length+' 个组件 · ':'')+(draftNote.movementSource?locationText(draftNote.movementSource)+' → ':'')+locationText(noteRect(draftNote));$('note-title').textContent=existing?'编辑修改意见':movementSource?'说明这次位移或缩放':'写下修改意见';$('note-text').value=draftNote.text;$('note-text').required=!draftNote.movementSource;$('note-text').setCustomValidity('');$('note-dialog').showModal();$('note-text').focus();}
  function editMovement(marker) {if(!marker)return;if(marker.noteId){selectedNote=marker.noteId;refresh();editNote(null,marker.noteId);if(marker.groupId&&equalRect(draftNote.movementSource,marker.source))draftNote.groupId=marker.groupId;}else{editNote(marker.destination,null,marker.groupId,marker.source);if(marker.sources)draftNote.movementSources=clone(marker.sources);}}
  function editEntry(id) {const marker=movementMarkers().find(m=>m.id===id);if(marker)editMovement(marker);else editNote(null,id);}
  $('note-form').onsubmit=event=>{event.preventDefault();const text=$('note-text').value.trim();if(!text&&!draftNote.movementSource){$('note-text').setCustomValidity('请写下修改意见');$('note-text').reportValidity();return;}if(!doc.notes.some(n=>n.id===draftNote.id)&&doc.notes.length>=1000){notify('每个画面最多支持 1000 条批注');return;}remember();draftNote.text=text;const index=doc.notes.findIndex(n=>n.id===draftNote.id);if(index>=0)doc.notes[index]=draftNote;else doc.notes.push(draftNote);selectedNote=draftNote.id;$('note-dialog').close();commit();globalThis.EditHereChrome?.revealNote(selectedNote);};
  $('note-text').oninput=()=>$('note-text').setCustomValidity('');
  $('note-dialog').addEventListener('keydown',event=>{if((event.ctrlKey||event.metaKey)&&event.key==='Enter'){event.preventDefault();$('note-form').requestSubmit();}});
  document.querySelectorAll('[data-close]').forEach(button=>button.onclick=()=>$(button.dataset.close).close());
  function sourceBounds(layout,groupId) {return L.originalBounds(layout,groupId);}
  function selectionIds(layout=doc?.layout) {if(!layout||!selected)return [];const ids=selectedGroups.size>1&&selectedGroups.has(selected)?[...selectedGroups]:[selected];return ids.filter(id=>layout.groups.some(g=>g.id===id));}
  function selectionBounds(layout=doc?.layout) {return selectionIds(layout).map(id=>L.bounds(layout,id)).filter(Boolean).reduce((r,b)=>unionRect(r,b),null);}
  function chooseGroup(id,toggle=false) {if(toggle){if(selected&&!selectedGroups.size)selectedGroups.add(selected);if(id){if(selectedGroups.has(id))selectedGroups.delete(id);else selectedGroups.add(id);}selected=[...selectedGroups].at(-1)||null;}else{selectedGroups.clear();if(id)selectedGroups.add(id);selected=id;}selectedNote=null;componentWheel=null;}
  function remapMovementNotes(before,previous) {
    doc.notes=L.remapNotes(before.notes,before.layout,doc.layout);
    for(const n of doc.notes){if(n.groupIds){const bounds=componentNoteBounds(n);if(bounds)n.source=clone(bounds);}if(n.movementSource){
      const old=previous.find(m=>m.groupId&&equalRect(m.source,n.movementSource)),groupId=n.groupId||old?.groupId;
      const movement=groupId&&doc.layout.movements.find(m=>m.groupId===groupId);
      if(movement){n.groupId=groupId;n.movementSource=clone(movement.source);n.source=clone(movement.destination);}
      else{const to=movementDestination(n.movementSource);if(to)n.source=clone(to);}
    }}
  }
  function synchronizeMovementNotes(pending=[]) {
    // Desktop REG-110: a move occupies its place in notes immediately, even
    // without text. Returning home removes only the empty movement note.
    const active=rawMovements().filter(m=>!equalRect(m.source,m.destination));
    doc.notes=doc.notes.filter(n=>!n.movementSource||n.text.trim()||active.some(m=>equalRect(m.source,n.movementSource)));
    for(const marker of movementMarkers())if(!marker.noteId){
      if(doc.notes.length>=1000)continue;
      // Pointer previews rebuild from the drag snapshot. Reuse the first
      // preview's identity so release, editing and undo keep the same note.
      const old=pending.find(n=>n.movementSource&&equalRect(n.movementSource,marker.source));
      doc.notes.push({id:old?.id||uid(),source:clone(marker.destination),text:'',movementSource:clone(marker.source),...(marker.groupId?{groupId:marker.groupId}:{}),...(marker.sources?{movementSources:clone(marker.sources)}:{})});
    }
  }
  function applyTransform(destination,before=snapshot(doc)) {
    const ids=selectionIds(before.layout),original=selectionBounds(before.layout);if(!original)return;
    const pending=doc.notes;
    doc.layout=clone(before.layout);
    if(ids.length===1)L.transform(doc.layout,ids[0],destination);
    else {
      const width=clamp(destination.x2-destination.x1,1,doc.layout.width),height=clamp(destination.y2-destination.y1,1,doc.layout.height),x=clamp(destination.x1,0,doc.layout.width-width),y=clamp(destination.y1,0,doc.layout.height-height),sx=width/(original.x2-original.x1),sy=height/(original.y2-original.y1);
      for(const id of ids){const b=L.bounds(before.layout,id);L.transform(doc.layout,id,{x1:x+(b.x1-original.x1)*sx,y1:y+(b.y1-original.y1)*sy,x2:x+(b.x2-original.x1)*sx,y2:y+(b.y2-original.y1)*sy});}
    }
    const previous=rawMovements({layout:before.layout});
    remapMovementNotes(before,previous);synchronizeMovementNotes(pending);
  }
  function resetComponent() {
    if(!selected||!doc?.layout)return;
    const before=snapshot(doc),previous=rawMovements({layout:before.layout});doc.layout=clone(before.layout);
    const ids=selectionIds(doc.layout),members=new Set(doc.layout.groups.filter(g=>ids.includes(g.id)).flatMap(g=>g.pieces));
    // Restore each surviving source slice exactly. Fitting the remaining
    // bounds into the parent's former frame would stretch or move its pixels.
    for(const piece of doc.layout.pieces)if(members.has(piece.id))piece.to=clone(piece.source);
    doc.layout.movements=clone(previous);
    for(const movement of doc.layout.movements){const group=doc.layout.groups.find(g=>g.id===movement.groupId),included=group?group.pieces.every(id=>members.has(id)):doc.layout.pieces.filter(p=>Math.min(p.source.x2,movement.source.x2)>Math.max(p.source.x1,movement.source.x1)&&Math.min(p.source.y2,movement.source.y2)>Math.max(p.source.y1,movement.source.y1)).every(p=>members.has(p.id));if(included)movement.destination=clone(movement.source);}
    remapMovementNotes(before,previous);synchronizeMovementNotes();
    if(!same(before,snapshot(doc))){remember(before);commit();}else refresh();
  }
  function cancelDrag() {if(drag?.before){doc.layout=drag.before.layout;doc.notes=drag.before.notes;}drag=null;render();renderGeometry();}
  function resizeHandles(b) {const x=(b.x1+b.x2)/2,y=(b.y1+b.y2)/2;return {nw:{x:b.x1,y:b.y1},n:{x,y:b.y1},ne:{x:b.x2,y:b.y1},e:{x:b.x2,y},se:{x:b.x2,y:b.y2},s:{x,y:b.y2},sw:{x:b.x1,y:b.y2},w:{x:b.x1,y}};}
  function resizeDestination(d,p) {
    const b=d.original,h=d.handle||'se',r=clone(b);if(h.includes('w'))r.x1=clamp(p.x,0,b.x2-1);if(h.includes('e'))r.x2=clamp(p.x,b.x1+1,session.width);if(h.includes('n'))r.y1=clamp(p.y,0,b.y2-1);if(h.includes('s'))r.y2=clamp(p.y,b.y1+1,session.height);
    if($('lock-ratio').checked){const ratio=(b.x2-b.x1)/(b.y2-b.y1),horizontal=/[ew]/.test(h),vertical=/[ns]/.test(h);let width=r.x2-r.x1,height=r.y2-r.y1;
      if(horizontal&&vertical){if(Math.abs(width-(b.x2-b.x1))>=Math.abs(height-(b.y2-b.y1))*ratio)height=width/ratio;else width=height*ratio;}
      else if(horizontal)height=width/ratio;else width=height*ratio;
      width=Math.min(width,session.width);height=Math.min(height,session.height);width=Math.min(width,height*ratio);height=width/ratio;
      if(h.includes('w'))r.x1=b.x2-width;else if(h.includes('e'))r.x2=b.x1+width;else{r.x1=(b.x1+b.x2-width)/2;r.x2=r.x1+width;}
      if(h.includes('n'))r.y1=b.y2-height;else if(h.includes('s'))r.y2=b.y1+height;else{r.y1=(b.y1+b.y2-height)/2;r.y2=r.y1+height;}
    }return r;
  }
  function ordinaryNoteAt(p) {if(!annotationsVisible||!doc)return null;return [...doc.notes].reverse().find(n=>{if(n.source===null||n.movementSource)return false;const r=noteRect(n);return r.x1===r.x2?Math.hypot((p.x-r.x1)*zoom,(p.y-r.y1)*zoom)<10:contains(r,p);})||null;}
  function startNoteDrag(note,p,event,handle=null,fromPin=false) {selected=null;selectedGroups.clear();selectedNote=note.id;drag={type:handle?'note-resize':'note-move',noteId:note.id,handle,fromPin,start:p,current:p,original:clone(noteRect(note)),before:snapshot(doc)};event.preventDefault();stage.setPointerCapture(event.pointerId);refresh();}
  function applyNoteDrag(d,p) {doc.notes=clone(d.before.notes);const note=doc.notes.find(n=>n.id===d.noteId);if(!note)return;const r=d.original;
    if(d.type==='note-resize'){const to=resizeDestination(d,p),x1=clamp(Math.round(to.x1),0,session.width-1),y1=clamp(Math.round(to.y1),0,session.height-1);note.source={x1,y1,x2:clamp(Math.round(to.x2),x1+1,session.width),y2:clamp(Math.round(to.y2),y1+1,session.height)};}
    else if(r.x1===r.x2){const x=clamp(Math.round(r.x1+p.x-d.start.x),0,session.width-1),y=clamp(Math.round(r.y1+p.y-d.start.y),0,session.height-1);note.source={x1:x,y1:y,x2:x,y2:y};}
    else{const width=r.x2-r.x1,height=r.y2-r.y1,x=clamp(r.x1+Math.round(p.x-d.start.x),0,session.width-width),y=clamp(r.y1+Math.round(p.y-d.start.y),0,session.height-height);note.source={x1:x,y1:y,x2:x+width,y2:y+height};}
    if(!equalRect(note.source,r)){delete note.groupId;delete note.groupIds;}
  }
  const viewport=$('viewport');
  function startPan(event) {if(pan||drag||!session||busy)return;event.preventDefault();if(spaceDown)spaceUsed=true;pan={pointerId:event.pointerId,x:event.clientX,y:event.clientY,panX,panY};fitMode=false;viewport.setPointerCapture(event.pointerId);stage.style.cursor='grabbing';viewport.style.cursor='grabbing';}
  viewport.addEventListener('pointerdown',event=>{if(event.button===1||event.button===0&&(spaceDown||mode==='pan'||event.target===viewport))startPan(event);});
  viewport.addEventListener('pointermove',event=>{if(!pan||pan.pointerId!==event.pointerId)return;panX=pan.panX+event.clientX-pan.x;panY=pan.panY+event.clientY-pan.y;resizeStage();});
  function finishPan(event) {if(!pan||event&&event.pointerId!==pan.pointerId)return;const pointerId=pan.pointerId;pan=null;if(viewport.hasPointerCapture(pointerId))viewport.releasePointerCapture(pointerId);viewport.style.cursor='';stage.style.cursor=spaceDown||mode==='pan'?'grab':mode==='select'?'default':'crosshair';}
  viewport.addEventListener('pointerup',finishPan);viewport.addEventListener('pointercancel',finishPan);viewport.addEventListener('lostpointercapture',finishPan);
  viewport.addEventListener('wheel',event=>{if(!session||event.target.closest('#stage'))return;event.preventDefault();if(!drag&&!pan)setZoom(zoom*Math.exp(-event.deltaY*.002),event);},{passive:false});
  viewport.addEventListener('contextmenu',event=>{if(session){event.preventDefault();cancelDrag();finishPan();selectedNote=null;hover=null;hoveredMovement=null;refresh();}});
  stage.addEventListener('pointerdown',event=>{
    if(!session||event.button!==0||busy)return;
    if(spaceDown||mode==='pan'){startPan(event);return;}
    if(session.kind==='video'&&!video.paused){video.pause();updateTime();notify('已暂停，再点一下或框选即可批注');return;}
    try{ensureDoc();}catch(error){notify(error.message);return;}
    stage.focus({preventScroll:true});const p=point(event);event.preventDefault();
    const badgeId=event.target.closest('[data-note]')?.dataset.note;
    if(badgeId){const n=doc.notes.find(n=>n.id===badgeId);if(mode==='select'&&n&&!n.movementSource&&n.source&&n.source.x1===n.source.x2)startNoteDrag(n,p,event,null,true);return;}
    const multi=event.ctrlKey||event.metaKey,adjusted=mode==='select'&&doc.notes.find(n=>n.id===selectedNote&&!n.movementSource&&n.source),noteBounds=adjusted&&noteRect(adjusted);
    const noteHandle=noteBounds&&noteBounds.x2>noteBounds.x1?(event.target.dataset.noteHandle||Object.entries(resizeHandles(noteBounds)).find(([,q])=>Math.hypot((p.x-q.x)*zoom,(p.y-q.y)*zoom)<10)?.[0]):null;
    if(noteHandle&&!multi){startNoteDrag(adjusted,p,event,noteHandle);return;}
    const b=selected&&doc.layout?selectionBounds():null,handle=b&&mode==='select'?(event.target.dataset.handle||Object.entries(resizeHandles(b)).find(([,q])=>Math.hypot((p.x-q.x)*zoom,(p.y-q.y)*zoom)<10)?.[0]):null;
    if(mode==='select'&&explodeEnabled&&multi&&doc.layout){const hits=L.hit(doc.layout,p.x,p.y),picked=hits[clamp(hoverIndex,0,Math.max(0,hits.length-1))]?.id||null;chooseGroup(picked,true);refresh();return;}
    const movement=!handle&&hitMovement(p);if(movement){editMovement(movement);return;}
    if(mode==='select'&&!handle){const note=ordinaryNoteAt(p);if(note){startNoteDrag(note,p,event);return;}}
    if(mode==='select'&&explodeEnabled&&doc.layout){const resize=!!handle;const hits=L.hit(doc.layout,p.x,p.y),picked=hits[clamp(hoverIndex,0,Math.max(0,hits.length-1))]?.id||null;const keepSelected=b&&contains(b,p);if(!resize&&!keepSelected)selected=picked;
      if(!resize&&!keepSelected)chooseGroup(picked);
      const area=selectionBounds();drag=area?{type:resize?'resize':'move',handle,start:p,original:clone(area),before:snapshot(doc),current:p,clickSelection:keepSelected&&!resize?picked:null}:null;selectedNote=null;renderGeometry();
    }else if(mode==='select'){selectedNote=null;drag=null;}
    else drag={type:'draw',start:p,current:p,candidate:mode==='smart'?candidateAt(p):null};
    if(drag)stage.setPointerCapture(event.pointerId);render();
  });
  stage.addEventListener('pointermove',event=>{if(!session||pan)return;const p=point(event);$('coords').textContent=`${Math.round(p.x)}, ${Math.round(p.y)} px`;if(drag){drag.current=p;try{if(drag.type==='move'){const a=drag.original,dx=p.x-drag.start.x,dy=p.y-drag.start.y;applyTransform({x1:a.x1+dx,y1:a.y1+dy,x2:a.x2+dx,y2:a.y2+dy},drag.before);}else if(drag.type==='resize')applyTransform(resizeDestination(drag,p),drag.before);else if(drag.type==='note-move'||drag.type==='note-resize')applyNoteDrag(drag,p);}catch(error){cancelDrag();notify(error.message);return;}render();renderGeometry();}else{hoveredMovement=hitMovement(p)?.id||null;stage.style.cursor=spaceDown||mode==='pan'?'grab':hoveredMovement?'pointer':mode==='select'&&ordinaryNoteAt(p)?'move':mode==='select'?'default':'crosshair';if(!hoveredMovement&&mode==='smart')hover=candidateAt(p);else if(!hoveredMovement&&mode==='select'&&explodeEnabled&&doc?.layout)hover=L.hit(doc.layout,p.x,p.y)[clamp(hoverIndex,0,Math.max(0,L.hit(doc.layout,p.x,p.y).length-1))]?.bounds||null;else hover=null;render();}});
  stage.addEventListener('pointerup',event=>{if(!drag)return;const d=drag;drag=null;if(stage.hasPointerCapture(event.pointerId))stage.releasePointerCapture(event.pointerId);if(d.type==='note-move'||d.type==='note-resize'){const distance=Math.hypot(d.current.x-d.start.x,d.current.y-d.start.y)*zoom;if(d.fromPin)suppressPinClickUntil=Date.now()+100;if(distance<3){doc.layout=d.before.layout;doc.notes=d.before.notes;refresh();if(d.fromPin)editNote(null,d.noteId);}else if(!same(d.before,snapshot(doc))){remember(d.before);commit();}else refresh();return;}if(d.type==='move'||d.type==='resize'){if(d.type==='move'&&Math.hypot(d.current.x-d.start.x,d.current.y-d.start.y)*zoom<3){doc.layout=d.before.layout;doc.notes=d.before.notes;if(d.clickSelection)selected=d.clickSelection;refresh();}else if(!same(d.before,snapshot(doc))){remember(d.before);commit();}else refresh();return;}let area=rect(d.start,d.current);area=Object.fromEntries(Object.entries(area).map(([k,v])=>[k,Math.round(v)]));const distance=Math.hypot(d.current.x-d.start.x,d.current.y-d.start.y)*zoom;
    if(mode==='cut'){if(area.x2-area.x1<2||area.y2-area.y1<2){render();return;}try{const before=snapshot(doc),groupId=L.addRegion(doc.layout,area);if(!groupId){notify('这个范围里没有可移动的像素');return;}chooseGroup(groupId);remember(before);changed();changeMode('select');refresh();}catch(error){notify(error.message);}return;}
    if(mode==='point'||mode==='smart'&&distance<5&&!d.candidate){const x=clamp(Math.round(d.start.x),0,session.width-1),y=clamp(Math.round(d.start.y),0,session.height-1);editNote({x1:x,y1:y,x2:x,y2:y});}else if(mode==='smart'&&distance<5&&d.candidate)editNote(d.candidate);else if(area.x2>area.x1&&area.y2>area.y1)editNote(area);render();
  });
  stage.addEventListener('pointercancel',cancelDrag);stage.addEventListener('lostpointercapture',()=>{if(drag)cancelDrag();});stage.addEventListener('pointerleave',()=>{if(!drag){hover=null;hoverIndex=0;hoveredMovement=null;render();}});
  stage.addEventListener('wheel',event=>{
    if(!session)return;event.preventDefault();if(drag||pan)return;
    const p=point(event),bounds=mode==='select'&&selectionBounds();
    if(bounds&&contains(bounds,p)&&!event.ctrlKey&&!event.metaKey&&!event.shiftKey&&!event.altKey){
      const before=snapshot(doc),factor=clamp(Math.exp(-event.deltaY*.0015),.2,5),width=bounds.x2-bounds.x1,height=bounds.y2-bounds.y1,scale=clamp(factor,Math.max(1/width,1/height),Math.min(session.width/width,session.height/height)),cx=(bounds.x1+bounds.x2)/2,cy=(bounds.y1+bounds.y2)/2,key=doc.id+':'+selectionIds().join(',');
      try{applyTransform({x1:cx-width*scale/2,y1:cy-height*scale/2,x2:cx+width*scale/2,y2:cy+height*scale/2},before);if(!same(before,snapshot(doc))){const now=Date.now();if(!componentWheel||componentWheel.key!==key||now-componentWheel.at>250)remember(before);componentWheel={key,at:now};commit();}}
      catch(error){doc.layout=before.layout;doc.notes=before.notes;notify(error.message);refresh();}return;
    }
    if((mode==='smart'||mode==='select'&&explodeEnabled&&doc?.layout)&&!event.ctrlKey&&!event.metaKey&&!event.altKey){hoverIndex=Math.max(0,hoverIndex+(event.deltaY<0?1:-1));if(mode==='smart')hover=candidateAt(p);else{const hits=L.hit(doc.layout,p.x,p.y);hoverIndex=clamp(hoverIndex,0,Math.max(0,hits.length-1));hover=hits[hoverIndex]?.bounds||null;}render();}
    else setZoom(zoom*Math.exp(-event.deltaY*.002),event);
  },{passive:false});
  $('pins').onclick=event=>{if(Date.now()<suppressPinClickUntil)return;const id=event.target.closest('[data-note]')?.dataset.note;if(!id)return;selectedNote=id;refresh();editEntry(id);};
  $('notes').onclick=event=>{const del=event.target.closest('[data-delete]')?.dataset.delete,edit=event.target.closest('[data-edit]')?.dataset.edit,id=event.target.closest('[data-note]')?.dataset.note;if(del){remember();doc.notes=doc.notes.filter(n=>n.id!==del);selectedNote=null;commit();}else if(edit)editEntry(edit);else if(id){selectedNote=id;refresh();if(noteEntries().find(n=>n.id===id)?.movement)editEntry(id);}};
  $('notes').addEventListener('keydown',event=>{if(event.key==='Enter'){const id=event.target.closest('[data-note]')?.dataset.note;if(id){event.preventDefault();editEntry(id);}}});
  document.querySelectorAll('[data-mode]').forEach(button=>button.onclick=()=>changeMode(button.dataset.mode));
  $('global').onclick=()=>editNote(null);
  function endExplosion() {
    if(explosionPreview){explosionPreview.getAnimations({subtree:true}).forEach(animation=>animation.cancel());explosionPreview.remove();explosionPreview=null;}
  }
  function animateExplosion() {
    endExplosion();
    if(matchMedia('(prefers-reduced-motion: reduce)').matches)return;
    // Match the desktop ExplosionWave: a reflection gathers at the upper-right
    // corner, then a soft diagonal ribbon crosses the viewport in 1.54 seconds.
    const layer=document.createElement('div');layer.className='explosion-preview';layer.setAttribute('aria-hidden','true');
    const reflection=document.createElement('div');reflection.className='explosion-reflection';
    const wave=document.createElement('div');wave.className='explosion-wave';
    const w=viewport.clientWidth,h=viewport.clientHeight,radius=clamp(Math.min(w,h)*.095,38,86),cosine=Math.cos(32*Math.PI/180),sine=Math.sin(32*Math.PI/180),travel=w*cosine+h*sine;
    wave.style.width=radius*6+'px';wave.style.height=Math.hypot(w,h)*2+radius*8+'px';
    const transform=position=>'translate('+(w-cosine*position)+'px,'+(sine*position)+'px) translate(-50%,-50%) rotate(-32deg)';
    layer.append(reflection,wave);viewport.appendChild(layer);explosionPreview=layer;
    wave.animate([{transform:transform(-radius*1.8)},{transform:transform(travel+radius*1.8)}],{duration:1540,easing:'cubic-bezier(.52,0,.22,1)',fill:'both'});
    wave.animate([{opacity:0},{opacity:1,offset:.16},{opacity:1,offset:.7},{opacity:0}],{duration:1540,fill:'both'});
    reflection.animate([{opacity:0},{opacity:.7,offset:.09},{opacity:0,offset:.37},{opacity:0}],{duration:1540,fill:'both'});
    setTimeout(()=>{if(explosionPreview===layer)endExplosion();else layer.remove();},1580);
  }
  $('explode').onclick=()=>run(async()=>{
    ensureDoc();
    if(explodeEnabled){endExplosion();explodeEnabled=false;chooseGroup(null);changeMode('smart');refresh();return;}
    if(!doc.layout){const d=doc,candidates=await detect(d);if(d!==doc)return;remember();doc.layout=L.create(session.width,session.height,candidates);changed();}
    explodeEnabled=true;chooseGroup(null);changeMode('select');refresh();animateExplosion();
  });
  $('component-note').onclick=()=>{if(selected&&doc?.layout){const ids=selectionIds();editNote(selectionBounds(),null,ids.length===1?ids[0]:null,null,ids.length>1?ids:null);}};
  if($('reset-component'))$('reset-component').onclick=resetComponent;
  function syncAnnotationVisibility() {
    const button=$('toggle-annotations'),label=annotationsVisible?'隐藏批注与位移箭头':'显示批注与位移箭头';
    button.classList.toggle('active',annotationsVisible);
    button.setAttribute('aria-pressed',String(annotationsVisible));
    button.setAttribute('aria-label',label);button.title=label;
    button.querySelector('use').setAttribute('href',annotationsVisible?'#i-eye':'#i-eye-off');
  }
  $('toggle-annotations').onclick=()=>{annotationsVisible=!annotationsVisible;syncAnnotationVisibility();hoveredMovement=null;render();};
  syncAnnotationVisibility();
  $('apply-geometry').onclick=()=>{if(!selected||!doc?.layout)return;const inputs=['gx','gy','gw','gh'].map(id=>$(id).value),values=inputs.map(Number);if(inputs.some(value=>String(value).trim()==='')||values.some(v=>!Number.isFinite(v))||values[2]<=0||values[3]<=0){notify('请输入有效的位置和尺寸');return;}const before=snapshot(doc);try{applyTransform({x1:values[0],y1:values[1],x2:values[0]+values[2],y2:values[1]+values[3]},before);if(!same(before,snapshot(doc))){remember(before);commit();}}catch(error){doc.layout=before.layout;doc.notes=before.notes;notify(error.message);}refresh();};
  $('gw').onchange=()=>{if($('lock-ratio').checked&&selected){const b=selectionBounds();$('gh').value=Math.max(1,Math.round(Number($('gw').value)*(b.y2-b.y1)/(b.x2-b.x1)));}};
  $('gh').onchange=()=>{if($('lock-ratio').checked&&selected){const b=selectionBounds();$('gw').value=Math.max(1,Math.round(Number($('gh').value)*(b.x2-b.x1)/(b.y2-b.y1)));}};
  function travelHistory(which) {if(!doc)return;const hadLayout=!!doc.layout,h=history(),from=h[which],to=h[which==='undo'?'redo':'undo'];if(!from.length)return;to.push(snapshot(doc));const data=from.pop();doc.notes=data.notes;doc.layout=data.layout;if(!hadLayout&&doc.layout)explodeEnabled=true;selected=null;selectedNote=null;componentWheel=null;changed();refresh();}
  $('undo').onclick=()=>travelHistory('undo');$('redo').onclick=()=>travelHistory('redo');$('fit').onclick=viewportFit;$('zoom-in').onclick=()=>setZoom(zoom*1.2);$('zoom-out').onclick=()=>setZoom(zoom/1.2);
  function feedbackDocument(d) {return {id:d.id,timestampUs:d.timestampUs,width:d.image.naturalWidth,height:d.image.naturalHeight,image:d.data,movements:rawMovements(d),notes:d.notes.map(n=>({source:clone(n.movementSource?n.source:componentNoteBounds(n,d)||n.source),text:n.text,...(n.movementSource?{movementSource:clone(n.movementSource)}:{})}))};}
  function exportDoc(d,embed=true,exportedAt) {return F.exportImage(feedbackDocument(d),embed,exportedAt);}
  function feedback(embed=true) {
    if(!session)throw new Error('请先打开素材');
    // All delivery paths share one export time until the document changes, so
    // a saved backup and an idempotent Agent submission identify the same snapshot.
    if(!feedbackStamp||feedbackStamp.session!==session||feedbackStamp.revision!==projectRevision)feedbackStamp={session,revision:projectRevision,exportedAt:new Date().toISOString()};
    if(feedbackStamp.snapshot){const {image,...withoutImage}=feedbackStamp.snapshot;return clone(embed?feedbackStamp.snapshot:withoutImage);}
    const exportedAt=feedbackStamp.exportedAt;
    const value=session.kind==='image'?exportDoc(session.document,embed,exportedAt):F.exportVideo({source:session.video.source,durationMs:session.video.durationMs,frames:session.frames.map(feedbackDocument)},embed,exportedAt);
    return feedbackStamp.metadata?{...value,...feedbackStamp.metadata}:value;
  }
  function download(name,blob) {const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),30000);}
  function jsonBlob(data) {return new Blob([JSON.stringify(data,null,2)],{type:'application/json'});}
  const baseName = () => (session?.name||'EditHere').replace(/\.[^.]+$/,'').replace(/[\\/:*?"<>|\u0000-\u001f]/g,'-');
  const saveModes={file:{label:'保存 JSON 文件',icon:'#i-download'},content:{label:'复制 JSON 内容',icon:'#i-json-copy-text'},project:{label:'保存项目',icon:'#i-save'},view:{label:'查看 JSON 文件',icon:'#i-json'}};
  const preference=(key,fallback)=>{try{return localStorage.getItem('edithere.'+key)??fallback;}catch{return fallback;}};
  const rememberPreference=(key,value)=>{try{localStorage.setItem('edithere.'+key,value);}catch{/* This view still remembers its selection when host storage is unavailable. */}};
  let saveMode=preference('saveMode','file');if(!saveModes[saveMode])saveMode='file';
  $('embed').checked=preference('includeImage','true')!=='false';
  function chooseSaveMode(mode) {
    saveMode=mode;rememberPreference('saveMode',mode);
    $('save-json').querySelector('use').setAttribute('href',saveModes[mode].icon);
    $('save-json').title=saveModes[mode].label+' (Ctrl / ⌘ + S)';$('save-json').setAttribute('aria-label',saveModes[mode].label);
    document.body.dataset.saveMode=mode;
    for(const option of $('save-options').querySelectorAll('[data-save-mode]'))option.setAttribute('aria-checked',String(option.dataset.saveMode===mode));
    window.dispatchEvent(new Event('edithere:layout'));
  }
  function closeSaveOptions(focus=false) {$('save-options').hidden=true;$('save-options-toggle').setAttribute('aria-expanded','false');if(focus)$('save-options-toggle').focus();}
  function refreshExport() {
    const embed=$('embed').checked;
    // Reuse one snapshot across file and clipboard delivery, including video
    // export time, and avoid re-encoding large images until the editing changes.
    if(!exportCache||exportCache.session!==session||exportCache.revision!==projectRevision||exportCache.embed!==embed){
      exportData=feedback(embed);exportText=F.serialize(exportData);
      exportCache={session,revision:projectRevision,embed,preview:F.preview(exportData),summary:F.summary(exportData,exportText)};
    }
    $('json-preview').value=exportCache.preview;
    const summary=exportCache.summary,size=bytes=>bytes<1024?bytes+' B':bytes<1024*1024?(bytes/1024).toFixed(1)+' KB':(bytes/1024/1024).toFixed(2)+' MB';
    $('json-summary').textContent=summary.schemaVersion+' · '+summary.annotations+' 条意见 · '+summary.movements+' 次位移 · '+size(summary.bytes)+($('embed').checked&&summary.imageBytes?'（图片 '+size(summary.imageBytes)+'）':'');
    $('export-hint').textContent=$('embed').checked?'预览已折叠图片编码；复制和文件保存包含完整原图。':'不包含图片，内容更小。仅用于 Agent 已持有同一原图；否则请同时附上原图或对应帧截图。此文件不能单独恢复画面。';
  }
  function openExport() {try{closeSaveOptions();refreshExport();if(!$('export-dialog').open)$('export-dialog').showModal();}catch(error){notify(error.message);}}
  function saveJsonFile() {
    refreshExport();download(baseName()+'-feedback.json',new Blob([exportText],{type:'application/json'}));
    $('export-hint').textContent='已发起 JSON 文件下载；完成后将文件交给 Agent';notify('已发起 JSON 文件下载');
  }
  async function copyJson() {
    refreshExport();const complete=exportText;
    try{await navigator.clipboard.writeText(complete);$('export-hint').textContent='完整反馈 JSON 已复制';notify('JSON 内容已复制，可粘贴到 Agent 对话');}
    catch{openExport();$('json-preview').value=complete;$('json-preview').focus();$('json-preview').select();$('export-hint').textContent='剪贴板不可用。请按 Ctrl+C / ⌘C 复制选中的完整 JSON，或保存 JSON 文件';}
  }
  async function performSave() {if(!session)return;try{if(saveMode==='project')$('save-project').click();else if(saveMode==='content')await copyJson();else if(saveMode==='view')openExport();else saveJsonFile();}catch(error){notify(error.message);}}
  chooseSaveMode(saveMode);
  // Keep the popup outside the backdrop-filtered header, which otherwise
  // changes the containing block for fixed positioning in narrow panels.
  document.body.append($('save-options'));
  function placeSaveOptions() {
    if($('save-options').hidden)return;
    const anchor=$('save-options-toggle').getBoundingClientRect(),menu=$('save-options').getBoundingClientRect();
    $('save-options').style.left=Math.max(10,Math.min(anchor.right-menu.width,innerWidth-menu.width-10))+'px';
    $('save-options').style.top=anchor.bottom+8+'px';
  }
  $('save-json').onclick=performSave;
  $('save-options-toggle').onclick=()=>{
    const open=$('save-options').hidden;$('save-options').hidden=!open;$('save-options-toggle').setAttribute('aria-expanded',String(open));
    if(open){placeSaveOptions();$('save-options').querySelector('[data-save-mode="'+saveMode+'"]').focus();}
  };
  for(const option of $('save-options').querySelectorAll('[data-save-mode]'))option.onclick=()=>{chooseSaveMode(option.dataset.saveMode);closeSaveOptions();$('save-json').focus();};
  $('save-options').onkeydown=event=>{
    const options=[...$('save-options').querySelectorAll('[role^="menuitem"]')],at=options.indexOf(document.activeElement);
    if(event.key==='Escape'){event.preventDefault();closeSaveOptions(true);}
    else if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){event.preventDefault();options[event.key==='Home'?0:event.key==='End'?options.length-1:(at+(event.key==='ArrowDown'?1:-1)+options.length)%options.length].focus();}
    else if(event.key==='Tab')closeSaveOptions();
  };
  window.addEventListener('resize',placeSaveOptions);
  document.addEventListener('pointerdown',event=>{if(!event.target.closest('.save-control,#save-options'))closeSaveOptions();});
  document.addEventListener('keydown',event=>{if(event.key==='Escape')closeSaveOptions();});
  $('export').onclick=openExport;
  $('embed').onchange=()=>{rememberPreference('includeImage',String($('embed').checked));refreshExport();};
  $('download-json').onclick=()=>{chooseSaveMode('file');saveJsonFile();};
  $('copy-json').onclick=()=>{chooseSaveMode('content');copyJson();};
  function serializedDoc(d) {return {id:d.id,timestampUs:d.timestampUs,image:d.data,notes:clone(d.notes),layout:clone(d.layout)};}
  function project() {return {schemaVersion:'edithere-web-project-1',tool:'EditHere Web',savedAt:new Date().toISOString(),kind:session.kind,name:session.name,width:session.width,height:session.height,...(session.kind==='image'?{document:serializedDoc(session.document)}:{video:clone(session.video),signature:clone(session.signature),frames:session.frames.filter(hasContent).map(serializedDoc)})};}
  $('save-project').onclick=()=>{try{download(baseName()+'.edithere-web.json',jsonBlob(project()));dirty=false;notify(session.kind==='video'?'已发起项目下载；重开后重新选择源视频即可继续播放':'已发起项目下载，包含原图、批注和组件布局');}catch(error){notify(error.message);}};
  $('preview').onclick=()=>run(async()=>{if(!doc)throw new Error('请先在当前画面添加批注');const c=makeCanvas(session.width,session.height),p=c.getContext('2d');if(doc.layout)L.paint(p,doc.image,doc.layout);else p.drawImage(doc.image,0,0);const line=Math.max(1,session.width/800),markers=movementMarkers();p.lineWidth=line*2;p.lineCap='round';p.lineJoin='round';
    for(const m of markers){const {start,end}=markerLine(m),dx=end.x-start.x,dy=end.y-start.y,length=Math.hypot(dx,dy);if(length<.0001)continue;const ux=dx/length,uy=dy/length,head=Math.min(9*line,length*.35);p.strokeStyle='white';p.lineWidth=4*line;p.beginPath();p.moveTo(start.x,start.y);p.lineTo(end.x,end.y);p.stroke();p.strokeStyle='#346df1';p.lineWidth=1.5*line;p.beginPath();p.moveTo(start.x,start.y);p.lineTo(end.x,end.y);p.moveTo(end.x-ux*head-uy*head*.5,end.y-uy*head+ux*head*.5);p.lineTo(end.x,end.y);p.lineTo(end.x-ux*head+uy*head*.5,end.y-uy*head-ux*head*.5);p.stroke();p.fillStyle='white';p.beginPath();p.arc(start.x,start.y,3*line,0,Math.PI*2);p.fill();p.stroke();}
    p.lineWidth=line*2;p.strokeStyle='#346df1';p.font=`bold ${Math.round(line*13)}px sans-serif`;for(const [i,n] of noteEntries().entries()){if(n.draft)continue;const r=noteRect(n);if(!r)continue;if(!n.movement&&r.x2>r.x1)p.strokeRect(r.x1,r.y1,r.x2-r.x1,r.y2-r.y1);const marker=markers.find(m=>m.noteId===n.id),at=marker?markerAnchor(marker,1):{x:r.x1,y:r.y1},x=clamp(at.x,12*line,c.width-12*line),y=clamp(at.y,12*line,c.height-12*line);p.fillStyle='#346df1';p.beginPath();p.arc(x,y,line*12,0,Math.PI*2);p.fill();p.fillStyle='#fff';p.textAlign='center';p.textBaseline='middle';p.fillText(String(i+1),x,y);}const blob=await new Promise(resolve=>c.toBlob(resolve,'image/png'));if(!blob)throw new Error('无法保存图片');download(baseName()+(session.kind==='video'?'-'+doc.timestampUs:'')+'-annotations.png',blob);});
  function validateRect(r,w,h,pointAllowed=true) {if(r===null)return;if(!r||Object.keys(r).sort().join(',')!=='x1,x2,y1,y2'||Object.values(r).some(v=>!Number.isFinite(v))||r.x1<0||r.y1<0||r.x2>w||r.y2>h||r.x2<r.x1||r.y2<r.y1||((r.x1===r.x2)!==(r.y1===r.y2))||!pointAllowed&&(r.x1===r.x2))throw new Error('项目区域坐标无效');if(r.x1===r.x2&&(r.x1>=w||r.y1>=h))throw new Error('项目点坐标越界');}
  function validateSources(sources,source,w,h) {
    if(sources===undefined)return;
    if(!Array.isArray(sources)||!sources.length||sources.length>4096)throw new Error('位移原始切片数量无效');
    for(const region of sources){validateRect(region,w,h,false);if(!region||!coversRect(source,region))throw new Error('位移切片超出原始范围');}
    const sorted=sources.slice().sort((a,b)=>a.x1-b.x1);
    for(let i=0;i<sorted.length;i++)for(let j=i+1;j<sorted.length&&sorted[j].x1<sorted[i].x2-1e-7;j++){
      const a=sorted[i],b=sorted[j];if(Math.min(a.y2,b.y2)>Math.max(a.y1,b.y1)+1e-7)throw new Error('位移原始切片重复');
    }
  }
  function validateNotes(notes,w,h,layout) {if(!Array.isArray(notes)||notes.length>1000)throw new Error('项目批注数量无效');const ids=new Set();for(const n of notes){if(typeof n.id!=='string'||!n.id||ids.has(n.id)||typeof n.text!=='string'||(!n.text.trim()&&!n.movementSource)||n.text.length>10000)throw new Error('项目批注格式无效');ids.add(n.id);validateRect(n.source,w,h);if(n.groupId&&(!layout||!layout.groups.some(g=>g.id===n.groupId)))throw new Error('项目引用了不存在的组件');if(n.groupIds!==undefined){if(!layout||n.source===null||n.groupId||n.movementSource||!Array.isArray(n.groupIds)||!n.groupIds.length||n.groupIds.length>512||new Set(n.groupIds).size!==n.groupIds.length||n.groupIds.some(id=>typeof id!=='string'||!layout.groups.some(g=>g.id===id)))throw new Error('项目多选组件引用无效');validateRect(n.source,w,h,false);}if(n.movementSource){if(!layout||n.source===null)throw new Error('位移意见缺少布局或目标区域');validateRect(n.movementSource,w,h,false);validateSources(n.movementSources,n.movementSource,w,h);if(n.groupId){if(!equalRect(n.movementSource,sourceBounds(layout,n.groupId))||!equalRect(n.source,rawMovements({layout}).find(m=>m.groupId===n.groupId)?.destination||L.bounds(layout,n.groupId)))throw new Error('位移意见与组件布局不一致');}}else if(n.movementSources!==undefined)throw new Error('位移切片缺少原始范围');}}
  function validateLayoutMovements(layout,w,h) {
    if(layout.movements===undefined)return;
    if(!Array.isArray(layout.movements)||layout.movements.length>8192)throw new Error('项目位移数量无效');
    const ids=new Set();
    for(const m of layout.movements){
      if(!m||!m.source||!m.destination)throw new Error('项目位移格式无效');
      validateRect(m.source,w,h,false);validateRect(m.destination,w,h,false);validateSources(m.sources,m.source,w,h);
      if(m.groupId!==null&&(typeof m.groupId!=='string'||ids.has(m.groupId)||!layout.groups.some(g=>g.id===m.groupId)))throw new Error('项目位移引用无效');
      if(m.groupId){
        ids.add(m.groupId);
        if(!equalRect(m.source,sourceBounds(layout,m.groupId))||!coversRect(m.destination,L.bounds(layout,m.groupId)))throw new Error('项目位移与组件布局不一致');
      }
    }
  }
  async function restoredDoc(value,w,h,isVideo) {const image=await decode(value.image);if(image.naturalWidth!==w||image.naturalHeight!==h)throw new Error('项目画面尺寸不匹配');if(typeof value.id!=='string'||!/^[^\\/:*?"<>|\u0000-\u001f]+$/.test(value.id)||value.id.length>256)throw new Error('项目画面编号无效');if(isVideo&&(!Number.isSafeInteger(value.timestampUs)||value.timestampUs<0))throw new Error('项目时间点无效');if(value.layout){L.validate(value.layout,w,h);validateLayoutMovements(value.layout,w,h);}validateNotes(value.notes,w,h,value.layout);const d=makeDoc(image,value.image,value.timestampUs,value.id);d.notes=clone(value.notes);d.layout=clone(value.layout);return d;}
  async function docFromFeedback(f,w,h,timestampUs=null,id=uid()) {
    f=F.normalizeImage(f);
    if(!f||f.annotationSpace!=='result'||!Array.isArray(f.objects)||f.objects.length>9704)throw new Error('反馈格式不支持，请导入 EditHere objects 格式');
    const image=await decode(f.image);w=w||image.naturalWidth;h=h||image.naturalHeight;if(w!==image.naturalWidth||h!==image.naturalHeight)throw new Error('反馈图片尺寸不匹配');
    const d=makeDoc(image,f.image,timestampUs,id),groups=new Map();
    // Partition every source before moving any piece: destinations may cover later sources.
    for(const obj of f.objects){validateRect(obj.source,w,h);if(!Array.isArray(obj.movements)||!Array.isArray(obj.annotations))throw new Error('反馈对象格式无效');
      if(obj.movements.length){if(!obj.source)throw new Error('全局意见不能包含移动');validateRect(obj.source,w,h,false);for(const movement of obj.movements)validateRect(movement.to,w,h,false);if(!d.layout)d.layout=L.create(w,h);const groupId=L.addRegion(d.layout,obj.source);if(!groupId)throw new Error('无法还原反馈的移动区域');groups.set(obj,groupId);}}
    const importedMovements=[];for(const obj of f.objects){const groupId=groups.get(obj);if(groupId){for(const movement of obj.movements)L.transform(d.layout,groupId,movement.to);importedMovements.push({groupId,source:clone(obj.source),destination:clone(obj.movements.at(-1).to)});}}if(d.layout)d.layout.movements=importedMovements;
    for(const {object:obj,text} of F.annotationEntries(f.objects).sort((a,b)=>a.number-b.number)){const groupId=groups.get(obj);d.notes.push({id:uid(),source:clone(groupId?obj.movements.at(-1).to:obj.source),text,...(groupId?{groupId,movementSource:clone(obj.source)}:{})});}
    validateNotes(d.notes,w,h,d.layout);return {doc:d,width:w,height:h};
  }
  function validateVideoMetadata(value,w=value?.width,h=value?.height,empty=false) {
    if(!value||typeof value.source!=='string'||!value.source.trim()||value.source.length>32768||!Number.isSafeInteger(value.durationMs)||value.durationMs<0||value.durationMs>9007199254740)throw new Error('视频信息或时长无效');
    // Native empty video feedback has no frame dimensions until its source is linked.
    if(empty&&value.width===0&&value.height===0)return;
    sizeOK(value.width,value.height);
    if(value.width!==w||value.height!==h)throw new Error('视频尺寸与项目不匹配');
  }
  function validateFrameMetadata(frames,durationMs) {
    if(!Array.isArray(frames)||frames.length>1000)throw new Error('视频帧数量无效');
    const ids=new Set(),times=new Set();
    for(const f of frames){
      if(!f||typeof f.id!=='string'||!f.id||f.id.length>256||!/^[^\\/:*?"<>|\u0000-\u001f]+$/.test(f.id)||ids.has(f.id)||!Number.isSafeInteger(f.timestampUs)||f.timestampUs<0||durationMs>0&&Math.floor(f.timestampUs/1000)>durationMs||times.has(f.timestampUs))throw new Error('视频帧编号或时间无效');
      ids.add(f.id);times.add(f.timestampUs);
    }
  }
  async function importJSON(file) {
    if(file.size>160*1024*1024)throw new Error('项目过大，最多支持 160 MB');
    const data=JSON.parse(await file.text());let next,nextDoc;
    if(!data||typeof data!=='object'||Array.isArray(data))throw new Error('项目格式无效');
    if(data.schemaVersion==='edithere-web-project-1'){
      sizeOK(data.width,data.height);
      if(typeof data.name!=='string'||!['image','video'].includes(data.kind))throw new Error('项目类型无效');
      next={kind:data.kind,name:data.name,width:data.width,height:data.height};
      if(data.kind==='image'){
        nextDoc=await restoredDoc(data.document,data.width,data.height,false);next.document=nextDoc;
      }else{
        validateVideoMetadata(data.video,data.width,data.height,Array.isArray(data.frames)&&data.frames.length===0);validateFrameMetadata(data.frames,data.video.durationMs);
        next.video=clone(data.video);next.signature=data.signature||null;
        next.frames=await Promise.all(data.frames.map(f=>restoredDoc(f,data.width,data.height,true)));
        nextDoc=next.frames[0]||null;
      }
    }else if(data.schemaVersion==='video-feedback-1'){
      validateVideoMetadata(data.video,data.video?.width,data.video?.height,Array.isArray(data.frames)&&data.frames.length===0);validateFrameMetadata(data.frames,data.video.durationMs);
      next={kind:'video',name:data.video.source,width:data.video.width||640,height:data.video.height||360,video:clone(data.video),signature:null,frames:[]};
      for(const f of data.frames){
        if(f.timestampMs!==Math.floor(f.timestampUs/1000))throw new Error('视频反馈时间无效');
        const item=await docFromFeedback(f.feedback,next.width,next.height,f.timestampUs,f.id);next.frames.push(item.doc);
      }
      nextDoc=next.frames[0]||null;
    }else if(Object.hasOwn(data,'schemaVersion')||data.objects||data.annotations&&data.changes){
      const result=await docFromFeedback(data);nextDoc=result.doc;next={kind:'image',name:file.name,width:result.width,height:result.height,document:nextDoc};
    }else throw new Error('请选择网页项目或含内嵌图片的反馈 JSON；桌面 .edithere 项目暂不支持');
    releaseVideo();session=next;doc=nextDoc;activate();if(doc)detect(doc);if(session.kind==='video')notify('批注帧已恢复；继续播放请重新选择源视频');
  }
  async function openFile(file) {if(!file||!replaceOK())return;if(/\.(json|edithere-web)$/i.test(file.name)||file.type==='application/json')await importJSON(file);else if(file.type.startsWith('video/')||/\.(mp4|mov|m4v|webm|ogv)$/i.test(file.name))await openVideo(file);else if(file.type.startsWith('image/'))await openImage(file);else throw new Error('请选择浏览器支持的图片、视频或 JSON 项目');}
  $('open').onclick=$('drop-card').onclick=()=>$('file-input').click();$('file-input').onchange=()=>{const file=$('file-input').files[0];$('file-input').value='';run(()=>openFile(file));};
  $('relink').onclick=()=>$('video-input').click();$('video-input').onchange=()=>{const file=$('video-input').files[0];$('video-input').value='';if(!file||session?.kind!=='video')return;run(async()=>{
    const prepared=await prepareVideo(file),signature=session.signature;
    const emptyFeedback=!session.frames.length&&session.video.width===0&&session.video.height===0;
    if(!emptyFeedback&&(prepared.width!==session.width||prepared.height!==session.height)||session.video.durationMs>0&&Math.abs(prepared.durationMs-session.video.durationMs)>250){URL.revokeObjectURL(prepared.url);throw new Error('视频尺寸或时长与项目不匹配，请选择原视频');}
    try{validateFrameMetadata(session.frames,prepared.durationMs);}catch(error){URL.revokeObjectURL(prepared.url);throw error;}
    if(signature&&(signature.name!==file.name||signature.size!==file.size||signature.lastModified!==file.lastModified)&&!confirm('这个文件与原视频记录不同。请确认内容相同，否则旧时间点可能对应其他画面。仍要关联吗？')){URL.revokeObjectURL(prepared.url);return;}
    const previous=doc;releaseVideo();await attachVideo(file,prepared);
    if(emptyFeedback){session.width=prepared.width;session.height=prepared.height;session.video.width=prepared.width;session.video.height=prepared.height;session.signature={name:file.name,size:file.size,lastModified:file.lastModified};}
    if(!session.video.durationMs)session.video.durationMs=prepared.durationMs;
    if(emptyFeedback)activate();
    if(previous){doc=previous;refresh();}else liveFrame();updateTime();notify('源视频已关联，可以继续播放和批注');
  });};
  let dropDepth=0;document.addEventListener('dragenter',event=>{if([...event.dataTransfer?.types||[]].includes('Files')){event.preventDefault();dropDepth++;$('drop-overlay').hidden=false;}});document.addEventListener('dragover',event=>{if([...event.dataTransfer?.types||[]].includes('Files')){event.preventDefault();event.dataTransfer.dropEffect='copy';}});document.addEventListener('dragleave',event=>{event.preventDefault();if(--dropDepth<=0){dropDepth=0;$('drop-overlay').hidden=true;}});document.addEventListener('drop',event=>{event.preventDefault();dropDepth=0;$('drop-overlay').hidden=true;const files=event.dataTransfer.files;if(files.length>1)notify('每次编辑一个素材，已打开第一个文件');if(files[0])run(()=>openFile(files[0]));});
  document.addEventListener('paste',event=>{if(event.target.closest('textarea,input,[contenteditable]'))return;const image=[...event.clipboardData?.items||[]].find(i=>i.type.startsWith('image/'));if(image){event.preventDefault();run(()=>openFile(image.getAsFile()));}});
  $('demo').onclick=()=>run(async()=>{if(!replaceOK())return;const brand=document.querySelector('.brand-lockup-light');await brand.decode();const c=makeCanvas(1100,700),p=c.getContext('2d');p.fillStyle='#f2f5fb';p.fillRect(0,0,1100,700);p.fillStyle='#fff';p.fillRect(0,0,190,700);p.fillRect(225,35,835,90);p.fillStyle='#346df1';p.fillRect(245,170,245,150);p.fillStyle='#36ae95';p.fillRect(520,170,245,150);p.fillStyle='#8a6add';p.fillRect(795,170,245,150);p.fillStyle='#fff';p.fillRect(245,355,795,270);p.fillStyle='#243149';p.font='bold 25px sans-serif';p.drawImage(brand,24,29,142,142*128/722);p.fillText('Overview',245,88);p.font='16px sans-serif';p.fillStyle='#7b879a';for(const [i,t] of ['Dashboard','Projects','Analytics','Settings'].entries())p.fillText(t,30,150+i*55);p.fillStyle='#fff';p.font='14px sans-serif';p.fillText('Visitors',266,205);p.fillText('Conversions',541,205);p.fillText('Revenue',816,205);p.font='bold 38px sans-serif';p.fillText('24,680',266,265);p.fillText('1,842',541,265);p.fillText('$38,420',816,265);p.fillStyle='#346df1';p.fillRect(910,60,110,38);p.fillStyle='#fff';p.font='14px sans-serif';p.fillText('Export',940,85);p.fillStyle='#243149';p.font='bold 18px sans-serif';p.fillText('Traffic over time',270,397);p.strokeStyle='#346df1';p.lineWidth=3;p.beginPath();p.moveTo(275,580);p.bezierCurveTo(420,420,480,630,620,470);p.bezierCurveTo(740,350,850,540,1000,430);p.stroke();const data=c.toDataURL('image/png'),image=await decode(data);releaseVideo();session={kind:'image',name:'示例仪表盘.png',width:1100,height:700,document:makeDoc(image,data)};doc=session.document;activate();detect(doc);});
  $('play').onclick=()=>run(async()=>{if(!mediaURL)throw new Error('请重新选择源视频');if(video.paused){const at=doc?doc.timestampUs/1000000:video.currentTime;await seekTo(at);await video.play();liveFrame();}else video.pause();updateTime();});
  $('back').onclick=()=>run(()=>seekTo((doc?doc.timestampUs/1000000:video.currentTime)-1));$('forward').onclick=()=>run(()=>seekTo((doc?doc.timestampUs/1000000:video.currentTime)+1));$('timeline').oninput=()=>{if(!mediaURL)return;video.pause();liveFrame();video.currentTime=Number($('timeline').value);updateTime();};$('frame-list').onclick=event=>{const id=event.target.closest('[data-frame]')?.dataset.frame;if(id)run(()=>selectFrame(id));};
  video.addEventListener('seeking',()=>{seeking=true;presentedTime=null;});video.addEventListener('seeked',()=>{seeking=false;updateTime();});for(const event of ['timeupdate','pause','play','ended'])video.addEventListener(event,updateTime);
  document.addEventListener('keydown',event=>{
    if(event.target.closest('textarea,input,dialog'))return;
    const ctrl=event.ctrlKey||event.metaKey,key=event.key.toLowerCase();
    if(document.body.dataset.agentLocked==='true'){if(ctrl&&key==='s'){event.preventDefault();$(event.shiftKey?'save-project':'save-json').click();}else if(ctrl&&key==='e'){event.preventDefault();$('export').click();}return;}
    if(busy){if(ctrl&&['o','s','e','z','y'].includes(key)||['v','b','p','r','c','h','0','1','+','=','-','escape','delete','backspace','arrowleft','arrowright','arrowup','arrowdown',' '].includes(key))event.preventDefault();return;}
    if(ctrl&&key==='o'){event.preventDefault();$('open').click();return;}if(!session)return;
    if(ctrl&&key==='s'){event.preventDefault();$(event.shiftKey?'save-project':'save-json').click();return;}
    if(ctrl&&key==='e'){event.preventDefault();$('export').click();return;}
    if(ctrl&&(key==='z'||key==='y')){event.preventDefault();travelHistory(key==='y'||event.shiftKey?'redo':'undo');return;}
    if(key==='0'){event.preventDefault();viewportFit();return;}if(key==='1'&&!ctrl){event.preventDefault();setZoom(1);return;}
    if(event.key==='Escape'){event.preventDefault();cancelDrag();finishPan();selected=null;selectedNote=null;hover=null;hoveredMovement=null;refresh();return;}
    if(event.code==='Space'){event.preventDefault();if(!event.repeat){spaceDown=true;spaceUsed=false;stage.style.cursor='grab';}return;}
    if(event.key==='Delete'||event.key==='Backspace'){if(selectedNote&&doc?.notes.some(n=>n.id===selectedNote)){event.preventDefault();remember();doc.notes=doc.notes.filter(n=>n.id!==selectedNote);selectedNote=null;commit();}return;}
    if(ctrl)return;
    const tools={v:'select',b:'smart',p:'point',r:'rect',c:'cut',h:'pan'};
    if(tools[key]){if(key==='c'&&!explodeEnabled)return;event.preventDefault();changeMode(tools[key]);return;}
    if(key==='+'||key==='='){event.preventDefault();setZoom(zoom*1.2);return;}if(key==='-'){event.preventDefault();setZoom(zoom/1.2);return;}
    if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key)){
      const amount=event.shiftKey?10:1,dx=event.key==='ArrowLeft'?-amount:event.key==='ArrowRight'?amount:0,dy=event.key==='ArrowUp'?-amount:event.key==='ArrowDown'?amount:0;
      if(selectedNote&&doc?.notes.some(n=>n.id===selectedNote&&!n.movementSource&&n.source)){event.preventDefault();const before=snapshot(doc),n=doc.notes.find(n=>n.id===selectedNote),original=clone(noteRect(n));applyNoteDrag({type:'note-move',noteId:n.id,original,before,start:{x:0,y:0}},{x:dx,y:dy});if(!same(before,snapshot(doc))){remember(before);commit();}return;}
      if(selected&&doc?.layout){event.preventDefault();const b=selectionBounds(),before=snapshot(doc);try{applyTransform({x1:b.x1+dx,y1:b.y1+dy,x2:b.x2+dx,y2:b.y2+dy},before);if(!same(before,snapshot(doc))){remember(before);commit();}}catch(error){doc.layout=before.layout;doc.notes=before.notes;notify(error.message);refresh();}}
    }
  });
  document.addEventListener('keyup',event=>{if(event.code!=='Space')return;const tapped=spaceDown&&!spaceUsed;spaceDown=false;stage.style.cursor=mode==='pan'?'grab':mode==='select'?'default':'crosshair';if(tapped&&session?.kind==='video'&&document.activeElement===stage&&!event.target.closest('textarea,input,dialog'))$('play').click();});
  window.addEventListener('blur',()=>{spaceDown=false;finishPan();cancelDrag();});
  window.addEventListener('edithere:layout',()=>{if(session&&fitMode)viewportFit();});
  const fitObserver=new ResizeObserver(()=>{if(session&&fitMode)viewportFit();});
  for(const selector of ['#viewport','.header','.media-bar','.view-controls','.tools','.status-bar','#video-controls','.sidebar','.utility-dock']){const element=document.querySelector(selector);if(element)fitObserver.observe(element);}
  window.addEventListener('beforeunload',event=>{if(dirty){event.preventDefault();event.returnValue='';}});
  // Editing stays independent of HTTP and MCP transports. A draft is considered
  // saved only when the transport acknowledges this exact editing revision.
  globalThis.EditHereWeb = {
    bindAgent() {agentBound=true;},
    async loadAgentImage(value) {
      if(session)throw new Error('当前页面已有素材，请为批注会话打开新页面');
      const image=await decode(value.image);
      releaseVideo();agentBound=true;
      session={kind:'image',name:value.name,width:image.naturalWidth,height:image.naturalHeight,document:makeDoc(image,value.image)};
      doc=session.document;activate();detect(doc);
    },
    async loadAgentProject(value) {
      if(!value.project||value.project.kind!=='image'||value.project.document?.image!==value.image)throw new Error('批注草稿与会话原图不匹配');
      await importJSON({name:value.name||value.project.name,size:JSON.stringify(value.project).length,text:async()=>JSON.stringify(value.project)});
      agentBound=true;
    },
    async loadAgentFeedback(value) {
      if(value.feedback?.image!==value.image)throw new Error('反馈与会话原图不匹配');
      await importJSON({name:value.name,size:JSON.stringify(value.feedback).length,text:async()=>JSON.stringify(value.feedback)});
      const metadata=F.imageMetadata(value.feedback);
      feedbackStamp={session,revision:projectRevision,exportedAt:metadata.exportedAt||new Date().toISOString(),metadata,snapshot:clone(value.feedback)};
      exportCache=null;
      agentBound=true;
    },
    project() {if(!session)throw new Error('请先打开素材');return clone(project());},
    projectRevision() {return projectRevision;},
    onProjectChange(listener) {projectListeners.add(listener);return ()=>projectListeners.delete(listener);},
    markAgentDraftSaved(revision) {if(revision===projectRevision)dirty=false;},
    feedback() {return clone(feedback(true));},
    notify,
    canCancel() {return !dirty||confirm('取消这次 AI 批注会话？当前编辑仍会保留在页面中，可保存项目。');},
    releaseAgent() {agentBound=false;},
    completedAgent() {
      dirty=false;document.body.dataset.agentState='completed';
      $('workspace').inert=true;$('open').disabled=true;
      globalThis.EditHereChrome?.syncGeometry(null);
    }
  };
})();
