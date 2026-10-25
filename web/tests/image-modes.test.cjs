'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const I=require('../src/image-format.js'),F=require('../src/feedback.js'),B=require('../src/bundle.js');
const{AgentSessions,readSessionStatus}=require('../agent-session.cjs');
const png='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==';
const document={image:png,notes:[{source:null,text:'按钮变蓝'}],movements:[]};
function fixture(options={}){const directory=path.join(__dirname,'output/image-modes',crypto.randomUUID()),output=path.join(directory,'result.json'),store=new AgentSessions({directory}),created=store.create({image:png,inputPath:path.join(directory,'source.png'),output,...options});return{directory,output,store,id:created.sessionId,token:new URL(created.url).searchParams.get('agent')};}

test('three modes use actual encoded bytes, dimensions and nullable source pixel ratio',()=>{
  const info=I.inspect(png);assert.equal(info.sha256,crypto.createHash('sha256').update(info.bytes).digest('hex'));
  for(const mode of['file','embedded','none']){const value=F.exportImage(document,mode);assert.equal(value.image.mode,mode);assert.equal(value.image.width,1);assert.equal(value.image.height,1);assert.equal(value.image.devicePixelRatio,null);assert.equal(value.image.sha256,info.sha256);assert.equal(Object.hasOwn(value.image,'dataUrl'),mode==='embedded');assert.equal(Object.hasOwn(value.image,'file'),mode==='file');assert.deepEqual(F.normalizeImage(value),value);}
  assert.equal(F.exportImage(document).image.mode,'file');
  const legacy={annotationSpace:'result',objects:[],image:png};assert.deepEqual(F.normalizeImage(legacy),legacy);assert.deepEqual(F.normalizeImage({objects:[]}),{annotationSpace:'result',objects:[]});
});

test('descriptor rejects unsafe paths, MIME/signature, digest and dimension tampering',()=>{
  const valid=I.descriptor(png,'embedded');for(const change of[{sha256:'0'.repeat(64)},{width:2},{mimeType:'image/jpeg'},{dataUrl:'https://example.invalid/a.png'},{devicePixelRatio:0},{extra:true}])assert.throws(()=>I.validate({...valid,...change}));
  for(const name of['../a.png','C:\\a.png','https://evil/a.png','a/b.png','CON.png','a.png:stream','a.png.',''])assert.throws(()=>I.descriptor(png,'file',name));
  for(const preview of['../view.annotated.jpg','view.jpg','a\\view.annotated.jpg'])assert.throws(()=>I.descriptor(png,'none','unused.png',{preview}));
  const folded=JSON.parse(F.preview(F.exportImage(document,'embedded')));assert.equal(folded.image.dataUrl,'data:image/png;base64,[图片编码已折叠]');assert.equal(F.exportImage(document,'embedded').image.dataUrl,png);
});

test('ZIP keeps UTF-8 basename entries and detects corruption or path traversal',()=>{
  const files=new Map([['反馈.json',F.serialize(F.exportImage(document,'file'))],['feedback.png',I.dataBytes(png).bytes]]),bytes=Buffer.concat(B.zip(files).map(value=>Buffer.from(value))),read=B.unzip(bytes);assert.equal(read.size,2);assert.equal(Buffer.from(read.get('feedback.png')).toString('base64'),png.split(',')[1]);
  const changed=Buffer.from(bytes);changed[100]^=1;assert.throws(()=>B.unzip(changed));assert.throws(()=>B.unzip(Buffer.concat(B.zip(new Map([['../a.json','{}']])).map(value=>Buffer.from(value)))));
});

test('file sessions save companions before JSON, return paths, retry and recover across restart',()=>{
  const run=fixture(),feedback=F.exportImage({...document,imageFile:'result.png'},'file'),receipt=run.store.submit(run.token,feedback);
  assert.equal(receipt.imagePath,path.join(run.directory,'result.png'));assert.deepEqual(receipt.feedback.image,feedback.image);assert.equal(fs.readFileSync(receipt.imagePath).toString('base64'),png.split(',')[1]);
  assert.deepEqual(run.store.submit(run.token,feedback),receipt);assert.deepEqual(readSessionStatus({directory:run.directory,sessionId:run.id}),receipt);
  const raw=run.store.byId(run.id);raw.state='submitting';run.store.persist(raw);assert.deepEqual(new AgentSessions({directory:run.directory}).status(run.store.byId(run.id)),receipt);
  fs.writeFileSync(receipt.imagePath,'tampered');assert.throws(()=>readSessionStatus({directory:run.directory,sessionId:run.id}),/MIME|dimensions|image|match/);
});

test('file sessions refuse preexisting sidecars even if bytes match and never overwrite output',()=>{
  const run=fixture(),feedback=F.exportImage({...document,imageFile:'result.png'},'file'),file=path.join(run.directory,'result.png');fs.writeFileSync(file,I.dataBytes(png).bytes);
  assert.throws(()=>run.store.submit(run.token,feedback),/already exists|not overwritten/);assert.throws(()=>run.store.submit(run.token,feedback),/already exists|not overwritten/);assert.equal(fs.existsSync(run.output),false);assert.equal(fs.readFileSync(file).toString('base64'),png.split(',')[1]);
  const other=fixture();fs.writeFileSync(other.output,'foreign');assert.throws(()=>other.store.submit(other.token,F.exportImage(document,'none')),/already exists|not overwritten/);assert.equal(fs.readFileSync(other.output,'utf8'),'foreign');
});

test('none emits metadata and JSON only; embedded verifies bytes and receipts omit encoding',()=>{
  for(const mode of['none','embedded']){const run=fixture({imageMode:mode}),feedback=F.exportImage(document,mode),receipt=run.store.submit(run.token,feedback);assert.equal(receipt.feedback.image.mode,mode);assert.equal(receipt.feedback.image.dataUrl,undefined);assert.equal(receipt.imagePath,undefined);assert.equal(fs.readdirSync(run.directory).filter(name=>/\.(png|jpg)$/.test(name)).length,0);assert.deepEqual(readSessionStatus({directory:run.directory,sessionId:run.id}),receipt);}
  const run=fixture();for(const change of[{sha256:'0'.repeat(64)},{width:2},{file:'../escape.png'}]){const feedback=F.exportImage(document,'file');feedback.image={...feedback.image,...change};assert.throws(()=>run.store.submit(run.token,feedback));}assert.equal(fs.existsSync(run.output),false);
});

test('all three modes publish requested annotated JPEG privately, verify restart and reject missing/colliding previews',()=>{
  const jpeg=require('./image-fixture.cjs').jpeg;
  for(const mode of['file','embedded','none']){const run=fixture({imageMode:mode,preview:true}),feedback=F.exportImage({...document,imageFile:'result.png',previewFile:'result.annotated.jpg'},mode);assert.throws(()=>run.store.submit(run.token,feedback),/preview data/);assert.equal(fs.existsSync(run.output),false);const receipt=run.store.submit(run.token,feedback,jpeg);assert.equal(receipt.previewPath,path.join(run.directory,'result.annotated.jpg'));assert.equal(fs.readFileSync(receipt.previewPath).toString('base64'),jpeg.split(',')[1]);assert(!JSON.stringify(receipt).includes('data:image/'));assert.deepEqual(readSessionStatus({directory:run.directory,sessionId:run.id}),receipt);assert.deepEqual(run.store.submit(run.token,feedback,jpeg),receipt);fs.writeFileSync(receipt.previewPath,'bad');assert.throws(()=>readSessionStatus({directory:run.directory,sessionId:run.id}));}
  const run=fixture({preview:true}),feedback=F.exportImage({...document,previewFile:'result.annotated.jpg'},'none');fs.writeFileSync(path.join(run.directory,'result.annotated.jpg'),'foreign');assert.throws(()=>run.store.submit(run.token,feedback,jpeg),/exists|not overwritten/);assert.equal(fs.readFileSync(path.join(run.directory,'result.annotated.jpg'),'utf8'),'foreign');
});
