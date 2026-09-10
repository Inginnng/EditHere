import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { annotation, validateDocument, makeExport, bytesToBase64, base64ToBytes, hash, checkPng, clone, rectFromPoints, CandidatePicker, manualTarget, moveGeometry, History } from '../src/model.js';
import { detectRegions, iou } from '../src/detector.js';

const desktop=JSON.parse(await readFile(new URL('./fixtures/desktop-review.json',import.meta.url),'utf8'));
test('desktop export preserves its schema, image and all annotation coordinates in a web round trip',async()=>{
  validateDocument(desktop);
  const png=base64ToBytes(desktop.capture.pngBase64);
  checkPng(png); assert.equal(await hash(png),desktop.capture.sha256);
  const result=makeExport({info:desktop.capture,png,notes:desktop.annotations},true);
  assert.deepEqual(result.annotations,desktop.annotations);
  assert.deepEqual(result.capture,desktop.capture);
  assert.equal(JSON.parse(JSON.stringify(result)).capture.coordinateSpace,'image-pixels');
});
test('non-embedded export does not mutate the working document',()=>{
  const doc={info:clone(desktop.capture),png:base64ToBytes(desktop.capture.pngBase64),notes:clone(desktop.annotations)};
  doc.notes[0].comment='标点 " \\ \n <script>中文</script>';
  const before=clone(doc); const result=makeExport(doc);
  assert.equal(result.capture.pngBase64,null);
  assert.equal(JSON.parse(JSON.stringify(result)).annotations[0].comment,doc.notes[0].comment);
  assert.deepEqual(doc,before);
});
test('schema rejects invalid geometry, extra fields, duplicate IDs, bad numbers and whitespace notes',()=>{
  const mutations=[
    d=>d.annotations[0].extra=true,
    d=>d.annotations[0].comment=' \n ',
    d=>d.annotations[1].id=d.annotations[0].id,
    d=>d.annotations[0].number=2,
    d=>{d.annotations[0].kind='point';d.annotations[0].rectangle=null;d.annotations[0].point={x:d.capture.width,y:0};},
    d=>{d.annotations[0].kind='rectangle';d.annotations[0].point=null;d.annotations[0].rectangle={x1:10,y1:0,x2:10,y2:10};},
    d=>{d.annotations[0].kind='rectangle';d.annotations[0].point=null;d.annotations[0].rectangle={x1:0,y1:0,x2:d.capture.width+1,y2:10};},
    d=>d.capture.imageFile='../image.png',
    d=>d.capture.width=32000001,
  ];
  for(const mutate of mutations){const d=clone(desktop);mutate(d);assert.throws(()=>validateDocument(d));}
});
test('base64 round-trips binary bytes and rejects malformed data',()=>{
  const bytes=Uint8Array.from({length:20000},(_,i)=>i%256);
  assert.deepEqual(base64ToBytes(bytesToBase64(bytes)),bytes);
  assert.throws(()=>base64ToBytes('bad!'));assert.throws(()=>checkPng(new Uint8Array(10)));
});
test('reverse drags clamp and round to original pixel boundaries',()=>{
  assert.deepEqual(rectFromPoints({x:250.7,y:80.2},{x:-4,y:14.8},200,100),{x1:0,y1:15,x2:200,y2:80});
});
const candidate=(x1,y1,x2,y2,source='vision')=>({bounds:{x1,y1,x2,y2},target:{...manualTarget(),source}});
test('wheel selects a real containment hierarchy, skips crossing siblings and clamps at both ends',()=>{
  const c=[candidate(0,0,100,100),candidate(10,10,20,20),candidate(15,0,50,30),candidate(5,5,50,50)];
  const picker=new CandidatePicker();picker.update(c,16,16);
  assert.deepEqual(picker.levels.map(c=>c.bounds),[c[1].bounds,c[3].bounds,c[0].bounds]);
  assert.deepEqual(picker.step(1).bounds,c[3].bounds);
  picker.update(c,17,17);assert.deepEqual(picker.current.bounds,c[3].bounds);
  picker.step(1);picker.step(1);assert.deepEqual(picker.current.bounds,c[0].bounds);
  picker.step(-1);picker.step(-1);picker.step(-1);assert.deepEqual(picker.current.bounds,c[1].bounds);
  picker.update(c,99,99);assert.equal(picker.levels.length,1);
  picker.update(c,100,100);assert.equal(picker.current,null);
});
test('duplicate bounds prefer available UIA metadata from imported desktop candidates',()=>{
  const picker=new CandidatePicker();picker.update([candidate(0,0,30,30),candidate(0,0,30,30,'uia')],10,10);
  assert.equal(picker.levels.length,1);assert.equal(picker.current.target.source,'uia');
});
test('moving a rectangle at an edge preserves dimensions; resize cannot invert or leave the image',()=>{
  const n=annotation('rectangle',{x1:20,y1:20,x2:80,y2:60});
  assert.deepEqual(moveGeometry(n,300,-50,100,100).rectangle,{x1:40,y1:0,x2:100,y2:40});
  assert.deepEqual(moveGeometry(n,-100,-100,100,100,'se').rectangle,{x1:20,y1:20,x2:21,y2:21});
  assert.deepEqual(moveGeometry(n,300,300,100,100,'nw').rectangle,{x1:79,y1:59,x2:80,y2:60});
  assert.deepEqual(n.rectangle,{x1:20,y1:20,x2:80,y2:60});
  const p=annotation('point',{x:50,y:50});assert.deepEqual(moveGeometry(p,100,-100,100,100).point,{x:99,y:0});
});
test('undo and redo retain independent snapshots and new edits discard redo',()=>{
  const history=new History(),first=[annotation('point',{x:4,y:6})];
  history.push([]);const second=clone(first);history.push(first);second[0].comment='changed';
  const undone=history.undo(second);assert.equal(undone[0].comment,'');
  assert.equal(history.redo(undone)[0].comment,'changed');
  history.undo(second);history.push(first);assert.equal(history.redoStack.length,0);
});
function scene(w,h,blocks=[]){
  const pixels=new Uint8ClampedArray(w*h*4).fill(255);
  for(const [x1,y1,x2,y2,color] of blocks)for(let y=y1;y<y2;y++)for(let x=x1;x<x2;x++)pixels.set([...color,255],(y*w+x)*4);
  return pixels;
}
test('image detector ignores a blank image and identifies separated cards in original pixels',()=>{
  assert.deepEqual(detectRegions(scene(200,160),200,160),[]);
  const cards=[[20,20,80,70,[40,100,60]],[110,80,180,140,[200,210,180]]];
  const result=detectRegions(scene(200,160,cards),200,160,1000,800);
  for(const [x1,y1,x2,y2] of cards){
    const expected={x1:x1*5,y1:y1*5,x2:x2*5,y2:y2*5};
    assert(result.some(c=>iou(c.bounds,expected)>.9));
  }
  for(const c of result){assert(c.bounds.x1>=0&&c.bounds.y1>=0&&c.bounds.x2<=1000&&c.bounds.y2<=800);assert.equal(c.target.source,'vision');}
});
