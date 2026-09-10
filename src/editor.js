import { annotation, rectFromPoints, clamp, contains, CandidatePicker, moveGeometry, clone } from './model.js';
import { escapeHtml, icon } from './icons.js';

export class Editor {
  constructor(viewport, callbacks) {
    this.viewport=viewport;this.callbacks=callbacks;this.doc=null;this.mode='smart';this.zoom=1;this.pan={x:0,y:0};this.fitMode=true;this.picker=new CandidatePicker();this.selected=null;this.drag=null;this.space=false;this.showCandidates=false;this.notesVisible=false;
    viewport.innerHTML='<section class="review-window" aria-label="截图与批注" hidden><div class="image-column"><header class="image-grip"><span class="image-meta"></span><button class="icon-button small" type="button" data-action="crop" title="重新裁剪" aria-label="重新裁剪">'+icon('crop')+'</button><button class="icon-button small" type="button" data-action="clear" title="关闭图片" aria-label="关闭图片">'+icon('close')+'</button></header><div class="image-padding"><div class="image-space" tabindex="0" role="application" aria-label="图片批注画布"><img class="source-image" draggable="false" alt="待批注的截图"/><svg class="annotation-layer" aria-hidden="true"></svg><div class="pin-layer"></div></div></div></div><aside class="notes-panel" aria-label="批注列表" hidden><header><h2>批注 <span class="note-count">0</span></h2><button class="icon-button small" type="button" data-action="notes" title="收起批注" aria-label="收起批注">'+icon('sidebar')+'</button></header><div class="note-list"></div></aside></section>';
    this.frame=viewport.querySelector('.review-window');this.surface=viewport.querySelector('.image-space');this.svg=viewport.querySelector('svg.annotation-layer');this.pins=viewport.querySelector('.pin-layer');this.image=viewport.querySelector('.source-image');this.notesPanel=viewport.querySelector('.notes-panel');
    this.surface.addEventListener('pointerdown',e=>this.down(e));
    this.surface.addEventListener('pointermove',e=>this.move(e));
    this.surface.addEventListener('pointerup',e=>this.up(e));
    this.surface.addEventListener('pointercancel',()=>this.cancel());
    this.surface.addEventListener('lostpointercapture',()=>{if(this.drag)this.cancel();});
    this.surface.addEventListener('pointerleave',()=>{if(!this.drag){this.picker.reset();this.renderLayers();}});
    this.surface.addEventListener('wheel',e=>this.wheel(e),{passive:false});
    this.surface.addEventListener('dblclick',e=>{if(this.mode==='select'){const p=this.point(e);const n=this.hit(e,p);if(n)this.callbacks.edit(n,false,e);}});
    this.surface.addEventListener('contextmenu',e=>{e.preventDefault();this.cancel();this.callbacks.context(e);});
    this.frame.querySelector('.image-grip').addEventListener('pointerdown',e=>{if(!e.target.closest('button'))this.startPan(e);});
    this.frame.addEventListener('pointermove',e=>{if(this.drag?.type==='pan')this.move(e);});
    this.frame.addEventListener('pointerup',e=>{if(this.drag?.type==='pan')this.up(e);});
    this.frame.addEventListener('pointercancel',()=>this.cancel());
    this.frame.addEventListener('click',e=>{
      const action=e.target.closest('[data-action]')?.dataset.action;if(action)this.callbacks.action(action,e);
      const card=e.target.closest('[data-note]');
      if(card&&!action){this.selected=card.dataset.note;this.renderLayers();this.renderNotes();if(e.detail===2)this.callbacks.edit(this.doc.notes.find(n=>n.id===this.selected),false,e);}
    });
    this.pins.addEventListener('keydown',e=>{if(e.key==='Enter'){const n=this.doc.notes.find(n=>n.id===e.target.dataset.noteId);if(n){e.preventDefault();this.callbacks.edit(n,false,e);}}});
    new ResizeObserver(()=>{if(this.doc&&this.fitMode)this.fit();}).observe(viewport);
  }
  setDocument(doc) {this.cancel();this.doc=doc;this.selected=null;this.pan={x:0,y:0};this.mode='smart';this.picker.reset();this.notesVisible=doc.notes.length>0;this.image.src=doc.url;this.frame.hidden=false;this.frame.querySelector('.image-meta').textContent=doc.info.width+' × '+doc.info.height;this.frame.querySelector('.image-meta').title=doc.info.title;this.surface.dataset.mode=this.mode;this.renderNotes();this.fit();this.renderLayers();}
  clear(){this.cancel();this.doc=null;this.frame.hidden=true;this.picker.reset();}
  setMode(mode){this.cancel();this.mode=mode;this.surface.dataset.mode=mode;this.picker.reset();this.renderLayers();}
  point(e){const rect=this.surface.getBoundingClientRect();return{x:clamp((e.clientX-rect.left)/this.zoom,0,this.doc.info.width),y:clamp((e.clientY-rect.top)/this.zoom,0,this.doc.info.height)};}
  hit(e,p){const id=e.target.closest('[data-note-id]')?.dataset.noteId;if(id)return this.doc.notes.find(n=>n.id===id);if(this.mode==='select')return [...this.doc.notes].reverse().find(n=>n.rectangle&&contains(n.rectangle,p.x,p.y));return null;}
  startPan(e){if(e.button!==0&&e.button!==1)return;e.preventDefault();this.drag={type:'pan',x:e.clientX,y:e.clientY,pan:{...this.pan},target:e.currentTarget||this.frame};this.drag.target.setPointerCapture(e.pointerId);}
  down(e){
    if(!this.doc||e.button===2)return;
    if(e.button===1){e.preventDefault();if(Math.abs(this.zoom-1)<.01)this.fit();else this.setZoom(1,e);return;}
    if(e.button!==0)return;
    if(this.space||e.altKey){this.startPan(e);return;}
    this.surface.focus({preventScroll:true});const p=this.point(e),n=this.hit(e,p),handle=e.target.closest('[data-handle]')?.dataset.handle;
    if(n){
      this.selected=n.id;this.renderNotes();
      if(this.mode!=='select'){e.preventDefault();this.callbacks.edit(n,false,e);return;}
      this.drag={type:'modify',start:p,original:clone(n),preview:clone(n),handle:handle||'move',target:this.surface};
    }else if(this.mode==='select'){this.selected=null;this.renderLayers();this.startPan(e);return;}
    else {this.picker.update(this.doc.candidates,p.x,p.y);this.drag={type:'draw',start:p,current:p,candidate:this.picker.current?clone(this.picker.current):null,target:this.surface};}
    e.preventDefault();this.surface.setPointerCapture(e.pointerId);this.renderLayers();
  }
  move(e){
    if(!this.doc)return;
    const d=this.drag;
    if(d?.type==='pan'){this.pan={x:d.pan.x+e.clientX-d.x,y:d.pan.y+e.clientY-d.y};this.layout();return;}
    const p=this.point(e);
    if(d?.type==='modify'){d.preview=moveGeometry(d.original,p.x-d.start.x,p.y-d.start.y,this.doc.info.width,this.doc.info.height,d.handle);this.renderLayers();}
    else if(d?.type==='draw'){d.current=p;this.renderLayers();}
    else if(this.mode==='smart'){this.picker.update(this.doc.candidates,p.x,p.y);this.renderLayers();this.hint();}
  }
  up(e){
    const d=this.drag;if(!d)return;this.drag=null;
    if(d.target.hasPointerCapture(e.pointerId))d.target.releasePointerCapture(e.pointerId);
    if(d.type==='modify'){if(JSON.stringify(d.original.point||d.original.rectangle)!==JSON.stringify(d.preview.point||d.preview.rectangle))this.callbacks.change(d.original.id,d.preview);this.renderLayers();return;}
    if(d.type==='pan')return;
    const {width,height}=this.doc.info;
    const distance=Math.hypot(d.current.x-d.start.x,d.current.y-d.start.y)*this.zoom;
    let n=null;
    if(this.mode==='rect'||(this.mode==='smart'&&distance>5)){const r=rectFromPoints(d.start,d.current,width,height);if(r.x2>r.x1&&r.y2>r.y1)n=annotation('rectangle',r);}
    else if(this.mode==='smart'&&d.candidate)n=annotation('rectangle',clone(d.candidate.bounds),d.candidate.target);
    else n=annotation('point',{x:clamp(Math.round(d.start.x),0,width-1),y:clamp(Math.round(d.start.y),0,height-1)});
    this.renderLayers();if(n)this.callbacks.edit(n,true,e);
  }
  cancel(){const active=!!this.drag;this.drag=null;this.renderLayers();return active;}
  wheel(e){
    if(!this.doc)return;e.preventDefault();if(this.drag)return;
    if(this.mode==='smart'&&!e.ctrlKey&&!e.metaKey){const p=this.point(e);this.picker.update(this.doc.candidates,p.x,p.y);this.picker.step(e.deltaY<0?1:-1);this.renderLayers();this.hint();}
    else this.setZoom(this.zoom*(e.deltaY<0?1.12:1/1.12),e);
  }
  hint(){const p=this.picker;this.callbacks.hint(p.current?p.current.target.label+' · '+(p.index+1)+' / '+p.levels.length+' · 滚轮 ↑ 更大 ↓ 更小':'单击添加批注 · 拖动框选');}
  fit(){
    if(!this.doc)return;this.fitMode=true;this.pan={x:0,y:0};
    const mobile=this.viewport.clientWidth<=820,side=this.notesVisible&&!mobile?272:0,below=this.notesVisible&&mobile?190:0;
    const maxW=Math.max(100,this.viewport.clientWidth-80-side),maxH=Math.max(100,this.viewport.clientHeight-80-below-36);
    this.zoom=Math.min(1,maxW/this.doc.info.width,maxH/this.doc.info.height);
    this.layout();this.renderLayers();this.callbacks.zoom(this.zoom);
  }
  setZoom(value,event=null){
    if(!this.doc)return;const old=this.surface.getBoundingClientRect();
    const p=event?this.point(event):{x:this.doc.info.width/2,y:this.doc.info.height/2};
    const anchor={x:old.left+p.x*this.zoom,y:old.top+p.y*this.zoom};
    this.zoom=clamp(value,.02,4);this.fitMode=false;this.layout();
    const next=this.surface.getBoundingClientRect();this.pan.x+=anchor.x-(next.left+p.x*this.zoom);this.pan.y+=anchor.y-(next.top+p.y*this.zoom);
    this.layout();this.renderLayers();this.callbacks.zoom(this.zoom);
  }
  toggleNotes(value=!this.notesVisible){this.notesVisible=value;this.renderNotes();if(this.fitMode)this.fit();else this.layout();}
  layout(){
    if(!this.doc)return;this.surface.style.width=this.doc.info.width*this.zoom+'px';this.surface.style.height=this.doc.info.height*this.zoom+'px';
    this.frame.style.transform='translate(-50%, -50%) translate('+this.pan.x+'px, '+this.pan.y+'px)';
    this.notesPanel.hidden=!this.notesVisible;
  }
  renderLayers(){
    if(!this.doc)return;
    const {width,height}=this.doc.info;this.svg.setAttribute('viewBox','0 0 '+width+' '+height);
    const rect=(r,cls,extra='')=>'<rect class="'+cls+'" x="'+r.x1+'" y="'+r.y1+'" width="'+(r.x2-r.x1)+'" height="'+(r.y2-r.y1)+'" vector-effect="non-scaling-stroke" '+extra+'/>';
    let svg='';
    if(this.showCandidates)for(const c of this.doc.candidates)svg+=rect(c.bounds,'all-candidate');
    if(!this.drag&&this.mode==='smart'&&this.picker.current)svg+=rect(this.picker.current.bounds,'hover-candidate');
    const notes=this.doc.notes.map(n=>this.drag?.type==='modify'&&this.drag.original.id===n.id?this.drag.preview:n);
    for(const n of notes)if(n.rectangle)svg+=rect(n.rectangle,n.id===this.selected?'note-rect selected':'note-rect');
    if(this.drag?.type==='draw'&&(this.mode==='rect'||Math.hypot(this.drag.current.x-this.drag.start.x,this.drag.current.y-this.drag.start.y)*this.zoom>5))svg+=rect(rectFromPoints(this.drag.start,this.drag.current,width,height),'draft-rect');
    this.svg.innerHTML=svg;
    this.pins.innerHTML=notes.map(n=>{const p=n.point||{x:n.rectangle.x1,y:n.rectangle.y1};return '<button type="button" class="pin '+(n.id===this.selected?'selected':'')+'" data-note-id="'+escapeHtml(n.id)+'" style="left:'+p.x*this.zoom+'px;top:'+p.y*this.zoom+'px" aria-label="批注 '+n.number+'">'+n.number+'</button>';}).join('');
    const selected=notes.find(n=>n.id===this.selected);
    if(this.mode==='select'&&selected?.rectangle){const r=selected.rectangle,x=(r.x1+r.x2)/2,y=(r.y1+r.y2)/2;for(const [handle,px,py] of [['nw',r.x1,r.y1],['n',x,r.y1],['ne',r.x2,r.y1],['e',r.x2,y],['se',r.x2,r.y2],['s',x,r.y2],['sw',r.x1,r.y2],['w',r.x1,y]])this.pins.insertAdjacentHTML('beforeend','<span class="resize-handle '+handle+'" data-handle="'+handle+'" data-note-id="'+escapeHtml(selected.id)+'" style="left:'+px*this.zoom+'px;top:'+py*this.zoom+'px"></span>');}
  }
  renderNotes(){
    if(!this.doc)return;this.notesPanel.hidden=!this.notesVisible;this.frame.querySelector('.note-count').textContent=this.doc.notes.length;
    this.frame.querySelector('.note-list').innerHTML=this.doc.notes.length?this.doc.notes.map(n=>'<article class="note-card '+(this.selected===n.id?'active':'')+'" data-note="'+escapeHtml(n.id)+'"><div class="note-card-head"><span class="number">'+n.number+'</span><div><button type="button" class="icon-button small" data-action="edit-note" data-id="'+escapeHtml(n.id)+'" aria-label="编辑批注 '+n.number+'">'+icon('edit')+'</button><button type="button" class="icon-button small" data-action="delete-note" data-id="'+escapeHtml(n.id)+'" aria-label="删除批注 '+n.number+'">'+icon('trash')+'</button></div></div><p>'+escapeHtml(n.comment)+'</p><small>'+coordinates(n)+'</small></article>').join(''):'<div class="no-notes">在图片上点选或画框，<br/>写下第一条修改意见。</div>';
  }
  refresh(){this.renderNotes();this.renderLayers();}
}
export function coordinates(n){return n.point?'点 ('+n.point.x+', '+n.point.y+')':'框 ('+n.rectangle.x1+', '+n.rectangle.y1+') → ('+n.rectangle.x2+', '+n.rectangle.y2+')';}
