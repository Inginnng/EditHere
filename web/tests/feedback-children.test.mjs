import test from 'node:test';
import assert from 'node:assert/strict';
import zlib from 'node:zlib';
import fs from 'node:fs';
import feedbackFormat from '../src/feedback.js';

// exportImage reads real pixels bounds, so the fixture needs a real size.
const crcTable = Array.from({length:256},(_,n)=>{let c=n;for(let k=0;k<8;k++)c=c&1?0xedb88320^(c>>>1):c>>>1;return c>>>0;});
const crc32 = buffer => {let c=0xffffffff;for(const byte of buffer)c=crcTable[(c^byte)&255]^(c>>>8);return (c^0xffffffff)>>>0;};
function dataUrl(width,height) {
  const chunk=(type,data)=>{const size=Buffer.alloc(4);size.writeUInt32BE(data.length);
    const body=Buffer.concat([Buffer.from(type,'latin1'),data]),crc=Buffer.alloc(4);crc.writeUInt32BE(crc32(body));
    return Buffer.concat([size,body,crc]);};
  const header=Buffer.alloc(13);header.writeUInt32BE(width,0);header.writeUInt32BE(height,4);header[8]=8;header[9]=2;
  const rows=Buffer.alloc((width*3+1)*height,255);
  for(let y=0;y<height;y++)rows[y*(width*3+1)]=0;
  const png=Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),
    chunk('IDAT',zlib.deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]);
  return 'data:image/png;base64,'+png.toString('base64');
}
const image = dataUrl(200,120);
const moved = {source:{x1:10,y1:10,x2:50,y2:50}, destination:{x1:60,y1:10,x2:100,y2:50}};
const document = notes => ({image, devicePixelRatio:null, includeRecognizedText:true, movements:[moved], notes});

test('a note inside a moved component is nested as its child', () => {
  const feedback = feedbackFormat.exportImage(document([
    {id:'n1', source:{...moved.destination}, text:'', movementSource:{...moved.source}},
    {id:'n2', source:{x1:70,y1:20,x2:90,y2:40}, text:'按钮再大一点', parent:{source:{...moved.source},state:'child'}},
  ]), 'none');
  assert.equal(feedback.schemaVersion,'feedback-minimal-4');
  assert.equal(feedback.objects.length,1);
  const [object] = feedback.objects;
  assert.equal(object.kind,'region');
  assert.deepEqual(object.source,{x1:10,y1:10,x2:50,y2:50});
  assert.equal(object.children.length,1);
  assert.deepEqual(object.children[0],{id:'o2',kind:'region',source:{x1:70,y1:20,x2:90,y2:40},
    annotations:[{number:2,text:'按钮再大一点'}]});
  assert.notEqual(object.children[0].id,object.id);
  // The nested list trails the parent object instead of splitting its fields.
  assert.equal(Object.keys(object).at(-1),'children');
  assert.equal(Object.keys(object).at(-2),'id');
  for (const encoded of [feedbackFormat.serialize(feedback), feedbackFormat.preview(feedback)]) {
    assert.equal(Object.keys(JSON.parse(encoded).objects[0]).at(-1),'children');
  }
  // Numbering stays complete, so the visible badge numbers still line up.
  assert.deepEqual(feedbackFormat.summary(feedback).objects,2);
  assert.equal(feedbackFormat.summary(feedback).annotations,1);
});

test('an undetermined parent blocks the export until the user answers', () => {
  assert.throws(() => feedbackFormat.exportImage(document([
    {id:'n1', source:{...moved.destination}, text:'', movementSource:{...moved.source}},
    {id:'n2', source:{x1:70,y1:20,x2:90,y2:40}, text:'这是指移动后的内容吗', parent:{source:{...moved.source},state:'unknown'}},
  ]), 'none'), /归属/);
});

test('a child whose component is gone exports as an ordinary object', () => {
  // Deleting the movement sends the component home, so the annotation loses its
  // parent instead of pointing at a movement that no longer exists.
  const feedback = feedbackFormat.exportImage({...document([
    {id:'n1', source:{x1:70,y1:20,x2:90,y2:40}, text:'这个按钮要更圆',
     parent:{source:{x1:1,y1:1,x2:2,y2:2},state:'child'}},
  ]), movements:[]}, 'none');
  assert.equal(feedback.objects.length,1);
  assert.equal(feedback.objects[0].children,undefined);
  assert.equal(feedback.objects[0].kind,'region');
  assert.deepEqual(feedback.objects[0].annotations,[{number:1,text:'这个按钮要更圆'}]);
});

test('a v4 export with children round-trips through validation', () => {
  const feedback = feedbackFormat.exportImage(document([
    {id:'n1', source:{...moved.destination}, text:'', movementSource:{...moved.source}},
    {id:'n2', source:{x1:70,y1:20,x2:90,y2:40}, text:'按钮再大一点', parent:{source:{...moved.source},state:'child'}},
  ]), 'none');
  const normalized = feedbackFormat.normalizeImage(feedback);
  assert.deepEqual(normalized.objects,feedback.objects);
  assert.equal(feedbackFormat.annotationEntries(normalized.objects).length,1);
  // Serialization keeps the nesting instead of flattening it away.
  assert.match(feedbackFormat.serialize(feedback), /"children":\[/);
});

test('only a boxed region can own or be a child annotation', () => {
  const base = {schemaVersion:'feedback-minimal-4', tool:{name:'EditHere',version:'0.11.1'},
    annotationSpace:'result', objects:[]};
  const child = {kind:'region', source:{x1:1,y1:1,x2:5,y2:5}, annotations:[{number:2,text:'子批注'}]};
  assert.throws(() => feedbackFormat.normalizeImage({...base, objects:[
    {kind:'global', annotations:[{number:1,text:'全局'}], children:[child]}]}), /region/);
  assert.throws(() => feedbackFormat.normalizeImage({...base, objects:[
    {kind:'region', source:{x1:1,y1:1,x2:9,y2:9}, annotations:[],
     children:[{kind:'global', annotations:[{number:2,text:'全局子批注'}]}]}]}), /region/);
  assert.throws(() => feedbackFormat.normalizeImage({...base, objects:[
    {kind:'region', source:{x1:1,y1:1,x2:9,y2:9}, annotations:[], children:[]}]}), /子|child/i);
});

test('v3 feedback still reads and upgrades to the v4 object shape', () => {
  const v3 = {schemaVersion:'feedback-minimal-3', $schema:'urn:edithere:feedback-minimal:3',
    tool:{name:'EditHere',version:'0.11.1'}, exportedAt:'2026-10-21T00:00:00Z', annotationSpace:'result',
    objects:[{kind:'region', source:{x1:1,y1:1,x2:9,y2:9},
      movement:{rect:{x1:2,y1:2,x2:10,y2:10},dx:1,dy:1}, annotations:[{number:1,text:'旧版'}]}]};
  const upgraded = feedbackFormat.upgradeImage(v3);
  assert.equal(upgraded.schemaVersion,'feedback-minimal-4');
  assert.equal(upgraded.$schema,undefined);
  assert.equal(upgraded.objects[0].kind,'region');
  // The v2 bridge flattens children, which that format cannot express.
  const feedback = feedbackFormat.exportImage(document([
    {id:'n1', source:{...moved.destination}, text:'', movementSource:{...moved.source}},
    {id:'n2', source:{x1:70,y1:20,x2:90,y2:40}, text:'按钮再大一点', parent:{source:{...moved.source},state:'child'}},
  ]), 'none');
  const legacy = feedbackFormat.legacyImage(feedback);
  assert.equal(legacy.schemaVersion,'feedback-minimal-2');
  assert.equal(legacy.objects.length,2);
  assert.ok(legacy.objects.every(object => !Object.hasOwn(object,'children')));
});

const contract=JSON.parse(fs.readFileSync(new URL('../../tests/fixtures/child-feedback-contract.json',import.meta.url),'utf8'));
for(const fixture of contract.cases)test('shared native/Web contract: '+fixture.name,()=>{
  const input={...structuredClone(fixture.document),image:dataUrl(fixture.document.width,fixture.document.height)},before=structuredClone(input);
  const result=feedbackFormat.exportImage(input,'none');
  assert.equal(result.schemaVersion,contract.schemaVersion);
  assert.deepEqual(result.objects,fixture.expected.objects);
  assert.equal(feedbackFormat.serialize({objects:result.objects}),fixture.expected.serializedObjects);
  assert.deepEqual(input,before,'export projection must not change the editable document');
});

test('exports synthesize a missing empty parent only in the projection and retain the 1000-note limit',()=>{
  const child={source:{x1:70,y1:20,x2:90,y2:40},text:'child',parent:{source:moved.source,state:'child'}};
  const without=document([child]),withParent=document([{source:moved.destination,movementSource:moved.source,text:''},child]);
  assert.deepEqual(feedbackFormat.exportImage(without,'none'),feedbackFormat.exportImage(withParent,'none'));
  assert.equal(without.notes.length,1);
  const full=document([...Array.from({length:999},()=>({source:null,text:'global'})),child]);
  assert.throws(()=>feedbackFormat.exportImage(full,'none'),/1000/);
});

test('source matching rejects ambiguous rounded rectangles and fractional orphans',()=>{
  const child=source=>({source:{x1:70,y1:20,x2:90,y2:40},text:'child',parent:{source,state:'child'}});
  const fractional={source:{x1:10.2,y1:10.2,x2:50.2,y2:50.2},destination:moved.destination};
  const rounded={x1:10,y1:10,x2:50,y2:50};
  const unique=feedbackFormat.exportImage({...document([child(rounded)]),movements:[fractional]},'none');
  assert.equal(unique.objects[0].children.length,1,'integer legacy source can resolve a unique fractional component');
  const orphan=feedbackFormat.exportImage({...document([child({...fractional.source,x1:10.3})]),movements:[fractional]},'none');
  assert(orphan.objects.every(object=>!object.children));
  const ambiguous=feedbackFormat.exportImage({...document([child(fractional.source)]),movements:[fractional,{source:rounded,destination:{...moved.destination,x1:61,x2:101}}]},'none');
  assert(ambiguous.objects.every(object=>!object.children));
});

test('identical source rectangles on separate tracks never select the last parent',()=>{
  const first={...structuredClone(moved),groupId:'first'};
  const second={source:{...moved.source},destination:{x1:120,y1:10,x2:160,y2:50},groupId:'second'};
  const child={source:{x1:70,y1:20,x2:90,y2:40},text:'ambiguous child',parent:{source:{...moved.source},state:'child'}};
  for(const movements of [[first,second],[second,first]]){
    const input={...document([structuredClone(child)]),movements},before=structuredClone(input);
    const result=feedbackFormat.exportImage(input,'none');
    assert(result.objects.every(object=>!Object.hasOwn(object,'children')));
    const standalone=result.objects.find(object=>object.annotations.some(annotation=>annotation.text===child.text));
    assert(standalone,'the ambiguous child remains visible as standalone feedback');
    assert.deepEqual(standalone.source,child.source);
    assert.deepEqual(input,before,'ambiguity cleanup must not mutate the editable document');
  }
});

test('v3 point and global objects upgrade without losing their kinds or optional evidence',()=>{
  const old={schemaVersion:'feedback-minimal-3',tool:{name:'EditHere',version:'0.11.1'},exportedAt:'2026-10-21T00:00:00Z',annotationSpace:'result',objects:[
    {id:'point',kind:'point',point:{x:3,y:4},ocrtext:'label',annotations:[{number:1,text:'point'}]},
    {id:'global',kind:'global',annotations:[{number:2,text:'global'}]}
  ]};
  assert.deepEqual(feedbackFormat.upgradeImage(old).objects,old.objects);
  assert.throws(()=>feedbackFormat.normalizeImage({...old,objects:[{kind:'region',source:moved.source,annotations:[],children:[old.objects[0]]}]}),/feedback-minimal-4/);
});
