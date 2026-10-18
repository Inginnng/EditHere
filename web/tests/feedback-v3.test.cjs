'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const F=require('../src/feedback.js');
const {validateFeedback}=require('../agent-session.cjs');
const image=require('./image-fixture.cjs').pngData(200,100);
const rect=(x1,y1,x2,y2)=>({x1,y1,x2,y2});
const source=rect(10,10,30,30),middle=rect(40,20,60,40),final=rect(60,20,100,30);
const document={image,toolVersion:'0.11.1',movements:[{source,destination:middle},{source,destination:final}],notes:[
  {source:final,movementSource:source,text:'移动并缩放'},
  {source:rect(3,4,3,4),text:'点意见'}, {source:null,text:'整体意见'},
  {source:rect(110,10,150,40),text:'普通框意见'}
]};
const exported=()=>F.exportImage(structuredClone(document),'embedded','2026-10-17T10:00:00+08:00');

test('J08/J09 exports discriminated targets and cumulative results from the original source',()=>{
  const value=exported();
  assert.equal(value.schemaVersion,'feedback-minimal-3');
  assert.equal(value.$schema,'urn:edithere:feedback-minimal:3');
  assert.equal(value.annotationSpace,'result');
  assert.deepEqual(value.objects,[
    {id:'o1',kind:'region',source,movements:[{to:middle},{to:final}],annotations:[{number:1,text:'移动并缩放'}],result:{rect:final,dx:50,dy:10,scaleX:2,scaleY:.5}},
    {id:'o2',kind:'point',point:{x:3,y:4},movements:[],annotations:[{number:2,text:'点意见'}]},
    {id:'o3',kind:'global',movements:[],annotations:[{number:3,text:'整体意见'}]},
    {id:'o4',kind:'region',source:rect(110,10,150,40),movements:[],annotations:[{number:4,text:'普通框意见'}]}
  ]);
  assert.deepEqual(F.normalizeImage(value),value);
  assert.deepEqual(validateFeedback(value,image),value,'Agent submission preserves every v3 field');
});

test('J08 migrates both old formats without changing object IDs, badge numbers or coordinates',()=>{
  const old=F.legacyImage(exported());
  old.objects[0].id='saved';old.objects[0].annotations[0].number=17;
  old.objects[1].annotations[0].number=9;
  const before=structuredClone(old),value=F.upgradeImage(old);
  assert.deepEqual(old,before,'migration does not mutate the saved v2 file');
  assert.equal(value.objects[0].id,'saved');
  assert.equal(value.objects[0].annotations[0].number,17);
  assert.deepEqual(value.objects[1].point,{x:3,y:4});
  assert.deepEqual(F.legacyImage(value),old);
  const legacy={annotationSpace:'result',image,changes:[{from:source,to:final}],annotations:[{change:0,text:'移动'},{point:{x:3,y:4},text:'点'},{text:'全局'}]};
  const migrated=F.upgradeImage(legacy);
  assert.deepEqual(migrated.objects.map(o=>o.kind),['region','point','global']);
  assert.deepEqual(migrated.objects[0].result,{rect:final,dx:50,dy:10,scaleX:2,scaleY:.5});
  assert.deepEqual(F.annotationEntries(migrated.objects).map(n=>n.number),[1,2,3]);
});

test('J09 empty and returned-home movements do not invent identity results',()=>{
  const value=F.exportImage({...document,movements:[{source,destination:source}]});
  assert(value.objects.every(o=>!Object.hasOwn(o,'result')));
  assert.equal(value.objects[0].kind,'region');
  assert.deepEqual(value.objects[0].annotations,[{number:1,text:'移动并缩放'}]);
});

test('J09 nested parent and child use their own composed destinations without double transforms',()=>{
  const parent=rect(10,10,90,90),child=rect(10,10,50,90);
  const value=F.exportImage({image,movements:[
    {source:parent,destination:rect(100,10,180,90)},
    {source:child,destination:rect(20,10,60,90)}
  ],notes:[]});
  assert.deepEqual(value.objects.map(o=>o.result),[
    {rect:rect(100,10,180,90),dx:90,dy:0,scaleX:1,scaleY:1},
    {rect:rect(20,10,60,90),dx:10,dy:0,scaleX:1,scaleY:1}
  ]);
});

test('J08/J09 reject ambiguous types and forged derived results before Agent acceptance',()=>{
  const cases=[
    v=>delete v.objects[0].result,
    v=>v.objects[0].result.dx=40,
    v=>v.objects[0].result.scaleX=1,
    v=>v.objects[0].result.rect=middle,
    v=>v.objects[0].result.scaleY=Infinity,
    v=>v.objects[0].kind='unknown',
    v=>v.objects[0].point={x:1,y:2},
    v=>v.objects[0].source=rect(10,10,10,30),
    v=>v.objects[0].movements[0]={to:rect(1,1,1,2)},
    v=>v.objects[1].source=rect(3,4,3,4),
    v=>v.objects[1].movements=[{to:final}],
    v=>v.objects[1].point.x=NaN,
    v=>v.objects[2].source=null,
    v=>v.objects[2].recognizedText={text:'伪造',engine:'ocr'},
    v=>v.objects[3].result={rect:source,dx:0,dy:0,scaleX:1,scaleY:1},
    v=>{v.schemaVersion='feedback-minimal-2';v.$schema='urn:edithere:feedback-minimal:2';},
    v=>delete v.objects[1].kind,
    v=>v.objects[1].annotations[0].number=1
  ];
  for(const mutate of cases){const v=exported();mutate(v);assert.throws(()=>F.normalizeImage(v));assert.throws(()=>validateFeedback(v,image));}
});

test('J08/J09 video index preserves all typed targets and rejects altered copies',()=>{
  const frame={...document,width:200,height:100,id:'f1',timestampUs:100000};
  const value=F.exportVideo({source:'source.mp4',durationMs:1000,frames:[frame]},'embedded');
  F.validateVideoRecognitionIndex(value);
  for(const mutate of [v=>v.objects[0].result.dx++,v=>v.objects[1].point.x++,v=>v.objects.pop(),v=>v.objects[2].frameId='wrong']){
    const invalid=structuredClone(value);mutate(invalid);assert.throws(()=>F.validateVideoRecognitionIndex(invalid));
  }
});
