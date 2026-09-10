import { rectFromPoints, clamp, CandidatePicker } from './model.js';
import { icon } from './icons.js';
export function chooseCrop(doc, {detect}={}) {
  return new Promise(resolve=>{
    const dialog=document.createElement('dialog');dialog.className='crop-dialog';dialog.setAttribute('aria-label','裁剪截图');
    dialog.innerHTML='<div class="crop-top"><span>拖动选择截图范围</span><span class="crop-hint">滚轮切换区域 · Esc 取消</span></div><div class="crop-work"><div class="crop-image"><img alt="待裁剪的画面" draggable="false"/><svg aria-hidden="true"></svg></div></div><div class="crop-actions"><span class="crop-size"></span><button type="button" class="button" data-crop="reset">重选</button><button type="button" class="button" data-crop="all">使用整张</button><button type="button" class="button" data-crop="cancel">取消</button><button type="button" class="button primary" data-crop="confirm">'+icon('check')+'完成裁剪</button></div>';
    document.body.append(dialog);dialog.showModal();
    const image=dialog.querySelector('.crop-image'),img=image.querySelector('img'),svg=image.querySelector('svg'),size=dialog.querySelector('.crop-size'),work=dialog.querySelector('.crop-work');
    img.src=doc.url;
    const {width,height}=doc.info;let scale=1,selection=null,start=null,hover=null,done=false;const picker=new CandidatePicker();
    function layout(){scale=Math.min((work.clientWidth-24)/width,(work.clientHeight-24)/height,1);image.style.width=width*scale+'px';image.style.height=height*scale+'px';svg.setAttribute('viewBox','0 0 '+width+' '+height);paint();}
    const observer=new ResizeObserver(layout);observer.observe(work);
    function p(e){const r=image.getBoundingClientRect();return{x:clamp((e.clientX-r.left)/scale,0,width),y:clamp((e.clientY-r.top)/scale,0,height)};}
    function paint(){
      const r=selection||hover;
      const path=r?'M0 0H'+width+'V'+height+'H0Z M'+r.x1+' '+r.y1+'V'+r.y2+'H'+r.x2+'V'+r.y1+'Z':'M0 0H'+width+'V'+height+'H0Z';
      svg.innerHTML='<path d="'+path+'" fill="rgba(20,20,24,.45)" fill-rule="evenodd"/>'+(r?'<rect x="'+r.x1+'" y="'+r.y1+'" width="'+(r.x2-r.x1)+'" height="'+(r.y2-r.y1)+'" fill="none" stroke="#007aff" stroke-width="2" vector-effect="non-scaling-stroke"/>':'');
      size.textContent=r?(r.x2-r.x1)+' × '+(r.y2-r.y1):'';
      dialog.querySelector('[data-crop=confirm]').disabled=!r||r.x2<=r.x1||r.y2<=r.y1;
    }
    function finish(value){if(done)return;done=true;observer.disconnect();dialog.close();dialog.remove();resolve(value);}
    image.addEventListener('pointerdown',e=>{if(e.button!==0)return;e.preventDefault();start=p(e);image.setPointerCapture(e.pointerId);});
    image.addEventListener('pointermove',e=>{const point=p(e);if(start)selection=rectFromPoints(start,point,width,height);else if(!selection){picker.update(doc.candidates,point.x,point.y);hover=picker.current?.bounds||null;}paint();});
    image.addEventListener('pointerup',e=>{if(!start)return;const end=p(e);if(Math.hypot(end.x-start.x,end.y-start.y)*scale<5)selection=hover;else selection=rectFromPoints(start,end,width,height);start=null;image.releasePointerCapture(e.pointerId);paint();});
    image.addEventListener('pointercancel',()=>{start=null;});
    image.addEventListener('wheel',e=>{e.preventDefault();if(start||selection)return;const point=p(e);picker.update(doc.candidates,point.x,point.y);hover=picker.step(e.deltaY<0?1:-1)?.bounds||null;paint();},{passive:false});
    image.addEventListener('contextmenu',e=>{e.preventDefault();if(selection){selection=null;paint();}else finish(null);});
    image.addEventListener('dblclick',()=>{if(selection||hover)finish(selection||hover);});
    dialog.addEventListener('cancel',e=>{e.preventDefault();finish(null);});
    dialog.addEventListener('keydown',e=>{if(e.key==='Enter'&&(selection||hover)){e.preventDefault();finish(selection||hover);}});
    dialog.addEventListener('click',e=>{const action=e.target.closest('[data-crop]')?.dataset.crop;if(action==='confirm')finish(selection||hover);if(action==='all')finish({x1:0,y1:0,x2:width,y2:height});if(action==='cancel')finish(null);if(action==='reset'){selection=null;start=null;paint();}});
    if(detect)detect(doc).then(c=>{if(!done){doc.candidates=c;dialog.querySelector('.crop-hint').textContent='滚轮切换区域 · Esc 取消';}}).catch(()=>{});
    requestAnimationFrame(layout);
  });
}
