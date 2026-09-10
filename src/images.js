import { uid, now, hash, checkPng, base64ToBytes, validateDocument } from './model.js';
export const MAX_BYTES = 48 * 1024 * 1024;
export function decodeImage(blob) {
  return new Promise((resolve,reject)=>{const image=new Image(),url=URL.createObjectURL(blob);image.onload=()=>{URL.revokeObjectURL(url);resolve(image);};image.onerror=()=>{URL.revokeObjectURL(url);reject(new Error('无法读取这张图片，请选择 PNG、JPG 或 WebP。'));};image.src=url;});
}
export function toBlob(canvas) { return new Promise((resolve,reject)=>canvas.toBlob(blob=>blob?resolve(blob):reject(new Error('图片尺寸过大，无法导出。')),'image/png')); }
export async function fromPng(png, source, title, importedInfo=null) {
  checkPng(png);
  const blob=new Blob([png],{type:'image/png'}),image=await decodeImage(blob);
  if(image.naturalWidth*image.naturalHeight>32000000 || image.naturalWidth>32767 || image.naturalHeight>32767)throw new Error('图片过大，请使用不超过 3200 万像素的图片。');
  const id=uid();
  const info=importedInfo ? {...importedInfo,pngBase64:null} : {id,createdAt:now(),source,title,imageFile:'capture-'+id.slice(0,8)+'.png',width:image.naturalWidth,height:image.naturalHeight,coordinateSpace:'image-pixels',origin:'top-left',rectangleConvention:'top-left-inclusive-bottom-right-exclusive',screenBounds:null,sha256:await hash(png),pngBase64:null};
  if(info.width!==image.naturalWidth || info.height!==image.naturalHeight)throw new Error('原图尺寸与项目不一致。');
  return {info,png,image,url:URL.createObjectURL(blob),notes:[],candidates:[],dirty:false};
}
export async function fromImageFile(file,source='file') {
  if(file.size>MAX_BYTES)throw new Error('图片文件不能超过 48 MB。');
  if(!/\.(png|jpe?g|webp|bmp|gif|avif)$/i.test(file.name||'') && !/^image\/(png|jpeg|webp|bmp|gif|avif)$/.test(file.type))throw new Error('请选择 PNG、JPG、WebP、BMP、GIF 或 AVIF 图片。');
  const image=await decodeImage(file);
  if(image.naturalWidth*image.naturalHeight>32000000 || image.naturalWidth>32767 || image.naturalHeight>32767)throw new Error('图片过大，请使用不超过 3200 万像素的图片。');
  const canvas=document.createElement('canvas');canvas.width=image.naturalWidth;canvas.height=image.naturalHeight;canvas.getContext('2d').drawImage(image,0,0);
  return fromPng(new Uint8Array(await (await toBlob(canvas)).arrayBuffer()),source,file.name||'剪贴板图片');
}
export async function importFiles(files) {
  const project=files.find(f=>f.name.toLowerCase().endsWith('.json'));
  if(!project) { if(!files.length)throw new Error('没有选择图片。');return fromImageFile(files[0]); }
  if(project.size>96*1024*1024)throw new Error('项目文件过大。');
  const exported=validateDocument(JSON.parse(await project.text()));
  let png;
  if(exported.capture.pngBase64)png=base64ToBytes(exported.capture.pngBase64);
  else {
    const original=files.find(f=>f.name.toLowerCase()===exported.capture.imageFile.toLowerCase());
    if(!original)throw new Error('这个 JSON 没有包含原图。请同时选中 JSON 和 '+exported.capture.imageFile+' 再打开。');
    if(original.size>MAX_BYTES)throw new Error('原图文件过大。');
    png=new Uint8Array(await original.arrayBuffer());
  }
  if(await hash(png)!==exported.capture.sha256)throw new Error('原图校验不一致，无法保证批注位置正确。');
  const doc=await fromPng(png,exported.capture.source,exported.capture.title,exported.capture);doc.notes=exported.annotations;return doc;
}
export async function captureScreen() {
  if(!navigator.mediaDevices?.getDisplayMedia)throw new Error('当前浏览器不支持屏幕截图，请使用上传或粘贴图片。');
  // Must be called directly from a user gesture. Always release every track, including cancellation/errors.
  const stream=await navigator.mediaDevices.getDisplayMedia({video:true,audio:false});
  const video=document.createElement('video');video.muted=true;video.playsInline=true;video.srcObject=stream;
  try {
    await video.play();
    if(!video.videoWidth)await new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(new Error('没有收到屏幕画面，请重试。')),5000);video.addEventListener('loadeddata',()=>{clearTimeout(timeout);resolve();},{once:true});});
    await new Promise(resolve=>setTimeout(resolve,180));
    const canvas=document.createElement('canvas');canvas.width=video.videoWidth;canvas.height=video.videoHeight;
    if(!canvas.width||!canvas.height||canvas.width*canvas.height>32000000)throw new Error('屏幕尺寸过大或无法采集，请上传截图。');
    canvas.getContext('2d').drawImage(video,0,0);
    const blob=await toBlob(canvas);
    return await fromPng(new Uint8Array(await blob.arrayBuffer()),'screen','屏幕截图');
  } finally {stream.getTracks().forEach(track=>track.stop());video.srcObject=null;}
}
export async function cropDocument(doc,r) {
  const canvas=document.createElement('canvas');canvas.width=r.x2-r.x1;canvas.height=r.y2-r.y1;
  canvas.getContext('2d').drawImage(doc.image,r.x1,r.y1,canvas.width,canvas.height,0,0,canvas.width,canvas.height);
  return fromPng(new Uint8Array(await (await toBlob(canvas)).arrayBuffer()),doc.info.source,doc.info.title);
}
export function disposeDocument(doc) { if(doc?.url)URL.revokeObjectURL(doc.url); }
export function download(name, blob) {
  const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),20000);
}
export async function previewBlob(doc) {
  const pad=24,side=320,gap=24;
  const measure=document.createElement('canvas').getContext('2d');measure.font='14px system-ui, sans-serif';
  const wrap=(text,max)=>{const lines=[];for(const paragraph of text.split('\n')){let line='';for(const ch of paragraph){if(line&&measure.measureText(line+ch).width>max){lines.push(line);line='';}line+=ch;}lines.push(line);}return lines;};
  const rows=doc.notes.map(n=>({note:n,lines:wrap(n.comment,side-36)}));
  const height=Math.max(doc.info.height+pad*2,76+rows.reduce((sum,r)=>sum+60+r.lines.length*22+12,0));
  const canvas=document.createElement('canvas');canvas.width=doc.info.width+pad*2+side+gap;canvas.height=height;
  if(canvas.width*height>32000000 || height>32767)throw new Error('批注预览过大，请导出项目与原图。');
  const ctx=canvas.getContext('2d');ctx.fillStyle='#f5f5f7';ctx.fillRect(0,0,canvas.width,height);ctx.drawImage(doc.image,pad,pad);
  const badge=(x,y,num)=>{ctx.fillStyle='#007aff';ctx.strokeStyle='white';ctx.lineWidth=2;ctx.beginPath();ctx.arc(x,y,14,0,Math.PI*2);ctx.fill();ctx.stroke();ctx.fillStyle='white';ctx.font=(num<100?'12':'10')+'px system-ui';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(String(num),x,y);};
  for(const n of doc.notes){const p=n.point||{x:n.rectangle.x1,y:n.rectangle.y1};if(n.rectangle){const r=n.rectangle;ctx.strokeStyle='#007aff';ctx.lineWidth=2;ctx.strokeRect(pad+r.x1,pad+r.y1,r.x2-r.x1,r.y2-r.y1);}badge(pad+p.x,pad+p.y,n.number);}
  const x=doc.info.width+pad+gap;let y=28;ctx.fillStyle='#1d1d1f';ctx.font='600 18px system-ui';ctx.textAlign='left';ctx.textBaseline='top';ctx.fillText('批注 '+doc.notes.length,x,y);y+=44;
  for(const {note,lines} of rows){const h=60+lines.length*22;ctx.fillStyle='white';ctx.beginPath();ctx.roundRect(x,y,side,h,12);ctx.fill();badge(x+28,y+26,note.number);ctx.fillStyle='#8e8e93';ctx.font='11px system-ui';ctx.textAlign='left';ctx.textBaseline='middle';ctx.fillText(note.kind==='point'?'('+note.point.x+', '+note.point.y+')':'('+note.rectangle.x1+', '+note.rectangle.y1+') → ('+note.rectangle.x2+', '+note.rectangle.y2+')',x+52,y+26);ctx.fillStyle='#1d1d1f';ctx.font='14px system-ui';ctx.textBaseline='top';lines.forEach((line,i)=>ctx.fillText(line,x+18,y+50+i*22));y+=h+12;}
  return toBlob(canvas);
}
