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
  assert.equal(value.schemaVersion,'feedback-minimal-4');
  assert.equal(Object.hasOwn(value,'$schema'),false);
  assert.equal(value.annotationSpace,'result');
  assert.deepEqual(value.objects,[
    {id:'o1',kind:'region',source,annotations:[{number:1,text:'移动并缩放'}],movement:{rect:final,dx:50,dy:10,scaleX:2,scaleY:.5}},
    {id:'o2',kind:'point',point:{x:3,y:4},annotations:[{number:2,text:'点意见'}]},
    {id:'o3',kind:'global',annotations:[{number:3,text:'整体意见'}]},
    {id:'o4',kind:'region',source:rect(110,10,150,40),annotations:[{number:4,text:'普通框意见'}]}
  ]);
  assert.deepEqual(F.normalizeImage(value),value);
  assert.deepEqual(validateFeedback(value,image),value,'Agent submission preserves every v3 field');
});

test('ordinary and movement objects follow their first visible annotation in one creation order',()=>{
  const other=rect(110,10,150,40),otherTarget=rect(120,40,160,70);
  const value=F.exportImage({image,movements:[{source,destination:final},{source:other,destination:otherTarget}],notes:[
    {source:null,text:'第一条普通意见'},
    {source:final,movementSource:source,text:''},
    {source:rect(3,4,3,4),text:'第三条普通意见'},
    {source:otherTarget,movementSource:other,text:'第四条位移意见'},
    {source:null,text:'第五条普通意见'}
  ]});
  assert.deepEqual(value.objects.map(o=>o.kind),['global','region','point','region','global']);
  assert.deepEqual(value.objects.map(o=>o.annotations.map(a=>a.number)),[[1],[],[3],[4],[5]]);
  assert.deepEqual(value.objects.map(o=>!!o.movement),[false,true,false,true,false]);
  assert(value.objects.every(o=>!Object.hasOwn(o,'movements')&&!Object.hasOwn(o,'result')));
});

test('compact v3 reads older schema IDs and empty movements without changing the saved file',()=>{
  const value={...exported(),exportedAt:'2026-10-17T10:00:00+08:00'},old=structuredClone(value);
  old.schemaVersion='feedback-minimal-3';
  old.$schema='urn:edithere:feedback-minimal:3';
  for(const object of old.objects) {
    if(object.movement) {object.result=object.movement;object.movements=[{to:object.movement.rect}];delete object.movement;}
    else object.movements=[];
  }
  const before=structuredClone(old);
  assert.deepEqual(F.normalizeImage(old),old);
  assert.deepEqual(validateFeedback(old,image),old);
  assert.deepEqual(F.legacyImage(old),F.legacyImage(value));
  assert.deepEqual(old,before);
  assert.equal(F.summary(value).movements,1);
  for(const invalid of [v=>v.$schema='urn:wrong',v=>v.objects[1].movements=null,v=>v.objects[3].movements={},v=>v.objects[3].movement=value.objects[0].movement]) {
    const v=structuredClone(value);invalid(v);assert.throws(()=>F.normalizeImage(v));assert.throws(()=>validateFeedback(v,image));
  }
});

test('ocrtext is a sibling string and old recognizedText remains readable',()=>{
  const value=exported();value.objects[1].ocrtext='保存\n<script>literal</script>';
  assert.deepEqual(F.normalizeImage(value),value);
  assert.deepEqual(validateFeedback(value,image),value);
  assert.equal(F.summary(value).recognizedTexts,1);
  assert.equal(F.legacyImage(value).objects[1].recognizedText.text,value.objects[1].ocrtext);
  const older=structuredClone(value);delete older.objects[1].ocrtext;
  older.objects[1].recognizedText={text:value.objects[1].ocrtext,engine:'windows-ocr',language:'zh-Hans'};
  assert.deepEqual(F.normalizeImage(older),older);
  for(const mutate of [
    ...['', ' \n\t', 'x'.repeat(501), null, 4, {}, []].map(text=>v=>v.objects[1].ocrtext=text),
    v=>v.objects[1].recognizedText={text:'conflict',engine:'legacy'},
    v=>v.objects[2].ocrtext='global'
  ]) {const invalid=structuredClone(value);mutate(invalid);assert.throws(()=>F.normalizeImage(invalid));assert.throws(()=>validateFeedback(invalid,image));}
});

test('J08 migrates both old formats without changing object IDs, badge numbers or coordinates',()=>{
  const old=F.legacyImage({...exported(),exportedAt:'2026-10-17T10:00:00+08:00'});
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
  assert.deepEqual(migrated.objects[0].movement,{rect:final,dx:50,dy:10,scaleX:2,scaleY:.5});
  assert.deepEqual(F.annotationEntries(migrated.objects).map(n=>n.number),[1,2,3]);
});

test('J09 empty and returned-home movements do not invent identity results',()=>{
  const value=F.exportImage({...document,movements:[{source,destination:source}]});
  assert(value.objects.every(o=>!Object.hasOwn(o,'movement')));
  assert.equal(value.objects[0].kind,'region');
  assert.deepEqual(value.objects[0].annotations,[{number:1,text:'移动并缩放'}]);
});

test('integer export debounces each scale axis and leaves editable geometry untouched',()=>{
  const largeImage=require('./image-fixture.cjs').pngData(1120,720);
  const source=rect(10.2,20.7,310.4,120.9);
  const target=rect(41.6,15.2,41.6+300.2*1.1263,15.2+100.2*1.003);
  const input={image:largeImage,movements:[{source,destination:target}],notes:[]},before=structuredClone(input);
  const value=F.exportImage(input,'embedded');
  assert.deepEqual(value.objects[0].source,rect(10,21,310,121));
  assert.deepEqual(value.objects[0].movement,{rect:rect(42,15,380,115),dx:32,dy:-6,scaleX:1.13});
  assert.deepEqual(input,before);
  assert.deepEqual(F.normalizeImage(value),value);
  assert.deepEqual(validateFeedback(value,largeImage),value);
  const older=structuredClone(value),object=older.objects[0];
  object.source=source;object.movements=[{to:target}];
  object.result={rect:target,dx:target.x1-source.x1,dy:target.y1-source.y1,scaleX:1.1263,scaleY:1.003};delete object.movement;
  assert.deepEqual(F.normalizeImage(older),older,'older fractional coordinates and full ratios stay readable');
  for(const change of [r=>delete r.scaleX,r=>r.scaleX=1.12,r=>r.scaleY=.99,r=>r.dx=32.1]) {
    const invalid=structuredClone(value);change(invalid.objects[0].movement);
    assert.throws(()=>F.normalizeImage(invalid));assert.throws(()=>validateFeedback(invalid,largeImage));
  }
});

test('resize jitter disappears from targets and keeps annotations and OCR',()=>{
  const image=require('./image-fixture.cjs').pngData(1120,720),source=rect(10,10,1010,610);
  for (const factor of [.996,1.003]) {
    const input={image,movements:[{source,destination:rect(10.2,10.2,10.2+1000*factor,10.2+600*factor)}],notes:[]};
    assert.deepEqual(F.exportImage(input).objects,[]);
    input.notes=[{source,movementSource:source,text:'保留意见',recognizedText:{text:'保留识别',engine:'test'}}];
    assert.deepEqual(F.exportImage(input).objects,[{id:'o1',kind:'region',source,annotations:[{number:1,text:'保留意见'}],ocrtext:'保留识别'}]);
  }
  const moved=F.exportImage({image,movements:[{source,destination:rect(25.6,21.6,1028.6,619.8)}],notes:[]});
  assert.deepEqual(moved.objects[0].movement,{rect:rect(26,22,1026,622),dx:16,dy:12});
  assert.deepEqual(F.normalizeImage(moved),moved);
});

test('two-decimal scale boundaries and canvas edge regions remain valid',()=>{
  const image=require('./image-fixture.cjs').pngData(2000,1200),source=rect(10,10,1010,1010);
  for(const [size,scale] of [[994,.99],[995,undefined],[1004,undefined],[1005,1.01],[1126.3,1.13]]) {
    const value=F.exportImage({image,movements:[{source,destination:rect(30,20,30+size,1020)}],notes:[]});
    assert.equal(value.objects[0].movement.scaleX,scale);
    assert(!Object.hasOwn(value.objects[0].movement,'scaleY'));
    F.normalizeImage(value);
  }
  const tiny=F.exportImage({image,movements:[{source:rect(1999.8,1199.8,1999.9,1199.9),destination:rect(100.8,100.8,100.9,100.9)}],notes:[]});
  assert.deepEqual(tiny.objects[0].source,rect(1999,1199,2000,1200));
  assert.deepEqual(tiny.objects[0].movement,{rect:rect(101,101,102,102),dx:-1898,dy:-1098});
  F.normalizeImage(tiny);
  const yOnly=F.exportImage({image,movements:[{source,destination:rect(30,20,1030,1146.3)}],notes:[]});
  assert.deepEqual(yOnly.objects[0].movement,{rect:rect(30,20,1030,1146),dx:20,dy:10,scaleY:1.13});
  const compressed=F.exportImage({image,movements:[{source,destination:rect(30,20,30.1,1020)}],notes:[]});
  assert.equal(compressed.objects[0].movement.scaleX,.01);F.normalizeImage(compressed);
});

test('video omits frames containing only rounding and resize jitter',()=>{
  const image=require('./image-fixture.cjs').pngData(1120,720),source=rect(10,10,1010,610);
  const value=F.exportVideo({source:'source.mp4',durationMs:1000,frames:[{id:'f1',timestampUs:100000,
    width:1120,height:720,image,movements:[{source,destination:rect(10.2,10.2,1013.2,608.4)}],notes:[]}]});
  assert.deepEqual(value.frames,[]);assert.deepEqual(value.objects,[]);
  assert.equal(value.video.width,0);assert.equal(value.video.height,0);
});

test('J09 drops a trajectory whose final target returns home',()=>{
  const value=F.exportImage({image,toolVersion:'0.11.1',movements:[
    {source,destination:middle},
    {source,destination:source}
  ],notes:[]},'embedded','2026-10-17T10:00:00+08:00');
  assert.deepEqual(value.objects,[]);
});

test('J09 keeps a returned-home region when its movement note has content',()=>{
  const value=F.exportImage({image,toolVersion:'0.11.1',movements:[
    {source,destination:middle},
    {source,destination:source}
  ],notes:[{source,movementSource:source,text:'回到原位后保留'}]},'embedded','2026-10-17T10:00:00+08:00');
  assert.deepEqual(value.objects,[
    {id:'o1',kind:'region',source,annotations:[{number:1,text:'回到原位后保留'}]}
  ]);
});

test('J09 uses the final target after a trajectory returns home and moves again',()=>{
  const value=F.exportImage({image,toolVersion:'0.11.1',movements:[
    {source,destination:middle},
    {source,destination:source},
    {source,destination:final}
  ],notes:[]},'embedded','2026-10-17T10:00:00+08:00');
  assert.equal(value.objects.length,1);
  assert.equal(Object.hasOwn(value.objects[0],'movements'),false);
  assert.deepEqual(value.objects[0].movement,{rect:final,dx:50,dy:10,scaleX:2,scaleY:.5});
});

test('J09 omits video frames whose final movement target returns home',()=>{
  const value=F.exportVideo({source:'source.mp4',durationMs:1000,frames:[{
    id:'f1',timestampUs:100000,width:200,height:100,image,movements:[
      {source,destination:middle}, {source,destination:source}
    ],notes:[]
  }]},'embedded','2026-10-17T10:00:00+08:00');
  assert.deepEqual(value.frames,[]);
  assert.deepEqual(value.objects,[]);
});

test('J08 rejects non-integer point coordinates in v3 feedback',()=>{
  const value=exported();
  value.objects[1].point.x=3.5;
  assert.throws(()=>F.normalizeImage(value),/Invalid feedback point/);
});

test('J09 nested parent and child use their own composed destinations without double transforms',()=>{
  const parent=rect(10,10,90,90),child=rect(10,10,50,90);
  const value=F.exportImage({image,movements:[
    {source:parent,destination:rect(100,10,180,90)},
    {source:child,destination:rect(20,10,60,90)}
  ],notes:[]});
  assert.deepEqual(value.objects.map(o=>o.movement),[
    {rect:rect(100,10,180,90),dx:90,dy:0},
    {rect:rect(20,10,60,90),dx:10,dy:0}
  ]);
});

test('J08/J09 reject ambiguous types and forged derived results before Agent acceptance',()=>{
  const cases=[
    v=>v.objects[0].movement=null,
    v=>v.objects[0].movement.dx=40,
    v=>v.objects[0].movement.scaleX=1,
    v=>v.objects[0].movement.rect=middle,
    v=>v.objects[0].movement.scaleY=Infinity,
    v=>v.objects[0].kind='unknown',
    v=>v.objects[0].point={x:1,y:2},
    v=>v.objects[0].source=rect(10,10,10,30),
    v=>v.objects[0].movement.rect=rect(1,1,1,2),
    v=>v.objects[0].movements=[{to:final}],
    v=>v.objects[0].result=v.objects[0].movement,
    v=>v.objects[1].source=rect(3,4,3,4),
    v=>v.objects[1].movements=[{to:final}],
    v=>v.objects[1].point.x=NaN,
    v=>v.objects[2].source=null,
    v=>v.objects[2].recognizedText={text:'伪造',engine:'ocr'},
    v=>v.objects[3].movement={rect:source,dx:0,dy:0,scaleX:1,scaleY:1},
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
  for(const mutate of [v=>v.objects[0].movement.dx++,v=>v.objects[1].point.x++,v=>v.objects.pop(),v=>v.objects[2].frameId='wrong']){
    const invalid=structuredClone(value);mutate(invalid);assert.throws(()=>F.validateVideoRecognitionIndex(invalid));
  }
});
