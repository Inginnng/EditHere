'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const feedback = require('../src/feedback.js');
const fs = require('node:fs');
const path = require('node:path');
const version = /project\(\s*EditHere\s+VERSION\s+(\d+\.\d+\.\d+)\b/.exec(fs.readFileSync(path.join(__dirname, '../../CMakeLists.txt'), 'utf8'))[1];
const exportedAt = '2026-10-16T16:30:00+08:00';
const metadata = {$schema:'urn:edithere:feedback-minimal:2', schemaVersion:'feedback-minimal-2', tool:{name:'EditHere', version}, exportedAt};
const rectangle = (x1, y1, x2, y2) => ({x1, y1, x2, y2});
const image = require('./image-fixture.cjs').pngData(320,180);
const imageDescriptor=mode=>feedback.imageFormat.descriptor(image,mode,'feedback.png');
const source = rectangle(10, 10, 70, 50);
const destination = rectangle(50, 50, 140, 110);

test('native minimalFeedbackRoundTrips fixture keeps movement and independent result notes', () => {
  // tests/layout_test.cpp minimalFeedbackRoundTripsAndRejectsCorruption.
  const actual = feedback.exportImage({image,
    movements:[{source, destination}],
    notes:[{source:rectangle(20, 20, 20, 20), text:'加大标题'}, {source, text:'表格右移'}]
  }, false, exportedAt);
  assert.deepEqual(feedback.legacyImage(actual), {...metadata, annotationSpace:'result', image:imageDescriptor('none'), objects:[
    {id:'o1', source:rectangle(20, 20, 20, 20), movements:[], annotations:[{number:1,text:'加大标题'}]},
    {id:'o2', source, movements:[], annotations:[{number:2,text:'表格右移'}]},
    {id:'o3', source, movements:[{to:destination}], annotations:[]}
  ]});
});

test('native movementSource notes merge with their exact source without exporting placeholders', () => {
  // app/model.cpp exportFeedback: ensureEntry is keyed by exact original source;
  // movement text is optional, and source-identical standalone notes are separate.
  const actual = feedback.exportImage({image,
    movements:[{source, destination}, {source, destination:rectangle(60, 60, 150, 120)}],
    notes:[
      {source:destination, movementSource:source, text:''},
      {source:destination, movementSource:source, text:'保持这个位置，视觉更轻盈'},
      {source, text:'独立框批注'},
      {source:null, text:'整体采用杂志风格'}
    ]
  });
  assert.deepEqual(actual.objects.map(feedback.legacyObject), [
    {id:'o1', source, movements:[{to:rectangle(60, 60, 150, 120)}], annotations:[{number:2,text:'保持这个位置，视觉更轻盈'}]},
    {id:'o2', source, movements:[], annotations:[{number:3,text:'独立框批注'}]},
    {id:'o3', source:null, movements:[], annotations:[{number:4,text:'整体采用杂志风格'}]}
  ]);
  assert.equal(actual.image.mode, 'file');assert.equal(actual.image.sha256,feedback.imageFormat.inspect(image).sha256);
});

test('native tracked parent and child sources stay nested instead of exporting pixel fragments', () => {
  // app/layout.cpp transformLayoutGroup preserves the tracked parent frame when
  // an independently transformed child leaves its parent's piece membership.
  const parent = rectangle(10, 10, 90, 90), child = rectangle(10, 10, 50, 90);
  const actual = feedback.exportImage({image,
    movements:[
      {source:parent, destination:rectangle(100, 10, 180, 90)},
      {source:child, destination:rectangle(20, 10, 60, 90)}
    ],
    notes:[
      {source:rectangle(140, 10, 180, 90), movementSource:parent, text:'父区域'},
      {source:rectangle(20, 10, 60, 90), movementSource:child, text:'子区域'}
    ]
  }, false);
  assert.deepEqual(actual.objects.map(feedback.legacyObject), [
    {id:'o1', source:parent, movements:[{to:rectangle(100, 10, 180, 90)}], annotations:[{number:1,text:'父区域'}]},
    {id:'o2', source:child, movements:[{to:rectangle(20, 10, 60, 90)}], annotations:[{number:2,text:'子区域'}]}
  ]);
});

test('native returning home drops movement while retaining its text on the original source', () => {
  // tests/layout_test.cpp movement-attached/global notes round-trip fixture.
  const actual = feedback.exportImage({image, movements:[{source, destination:source}],
    notes:[{source, movementSource:source, text:'回到原位后保留'}]
  }, false);
  assert.deepEqual(actual.objects.map(feedback.legacyObject), [{id:'o1', source, movements:[], annotations:[{number:1,text:'回到原位后保留'}]}]);
});

test('native fractional sources that round to the same pixels merge their absolute trajectory', () => {
  // tests/layout_test.cpp feedbackMergesSourcesThatRoundToTheSamePixels.
  const actual = feedback.exportImage({image,
    movements:[
      {source:rectangle(10.000001, 10, 30.000001, 30), destination:rectangle(50, 30, 70, 50)},
      {source:rectangle(10.000002, 10, 30.000002, 30), destination:rectangle(80, 50, 100, 70)}
    ], notes:[]
  }, false);
  assert.equal(actual.objects.length, 1);
  assert.equal(actual.objects[0].source.x1, 10);
  assert.equal(Object.hasOwn(actual.objects[0],'movements'),false);
  assert.deepEqual(actual.objects[0].movement,{rect:rectangle(80,50,100,70),dx:70,dy:40});
  feedback.normalizeImage(actual);
});

test('native compact export and folded image preview carry separate complete bytes', () => {
  // app/model.cpp serializeFeedback + app/editor.cpp JSON preview/copy handlers.
  const actual = feedback.exportImage({image, movements:[{source, destination}], notes:[]}, true, exportedAt);
  const bytes = feedback.serialize(actual);
  assert(!bytes.includes('\n '), 'serialization stays compact');
  assert(bytes.endsWith('\n'));
  assert.deepEqual(JSON.parse(feedback.preview(actual)), {...actual, image:{...actual.image,dataUrl:'data:image/png;base64,[图片编码已折叠]'}});
  assert.deepEqual(JSON.parse(bytes), actual);
  assert.equal(actual.image.dataUrl, image, 'preview cannot replace original bytes');
  assert.equal(feedback.summary(actual).schemaVersion, 'feedback-minimal-3');
});

test('native video fixtures keep microseconds and chronological frame/image references', () => {
  // tests/video_project_test.cpp sameCoordinatesAtDifferentTimesRemainSeparate
  // and blankFramesAreOmittedAndMovementOnlyFramesRemain.
  const frame = (id, timestampUs, notes, movements = []) => ({
    id, timestampUs, width:320, height:180, image, notes, movements
  });
  const actual = feedback.exportVideo({source:'demo.mp4', durationMs:20000, frames:[
    frame('later', 12540999, [{source, text:'后面的画面'}]),
    frame('blank', 0, []),
    frame('earlier', 4000123, [{source, text:'前面的画面'}]),
    frame('movement', 18000000, [], [{source, destination}])
  ]}, true, '2026-10-11T12:00:00.000Z');
  assert.deepEqual(actual.video, {source:'demo.mp4', durationMs:20000, width:320, height:180});
  assert.deepEqual(actual.frames.map(frame => [frame.id, frame.timestampMs, frame.timestampUs]), [
    ['earlier', 4000, 4000123], ['later', 12540, 12540999], ['movement', 18000, 18000000]
  ]);
  assert.equal(actual.objects.length, 3);
  for (const frame of actual.frames) {
    assert.equal(frame.imageFile, 'frame-' + frame.id + '.png');
    assert.equal(frame.feedback.image.dataUrl, image);
    assert.deepEqual(feedback.imageMetadata(feedback.legacyImage(frame.feedback)), {...metadata, exportedAt:actual.exportedAt});
    assert.deepEqual(actual.objects.filter(object => object.frameId === frame.id),
      frame.feedback.objects.map(object => ({...object, timestampMs:frame.timestampMs, frameId:frame.id})));
  }
  assert.equal(actual.frames[2].feedback.objects[0].annotations.length, 0);
  assert.equal(feedback.serialize(actual).split(image).length - 1, 3, 'one original per frame, none in the root index');
  assert(!feedback.preview(actual).includes('iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB'));
  assert(feedback.serialize(actual).includes(image));
});

test('versioned image metadata uses the actual product build and remains valid without image encoding', () => {
  for (const embed of [true, false]) {
    const actual = feedback.exportImage({image, notes:[], movements:[]}, embed);
    assert.equal(actual.tool.version, version);
    assert.equal(actual.tool.name, 'EditHere');
    assert.match(actual.exportedAt, /^\d{4}-\d{2}-\d{2}T.*Z$/);
    assert.equal(actual.image.mode,embed?'embedded':'none');
    assert.deepEqual(feedback.normalizeImage(actual), actual);
    assert.deepEqual(JSON.parse(feedback.preview(actual)).tool, actual.tool);
  }
});

test('known version metadata is mandatory and unknown versions never fall through to object parsing', () => {
  const current = {...metadata, annotationSpace:'result', objects:[]};
  assert.deepEqual(feedback.normalizeImage(current), current);
  for (const invalid of [
    {...current, schemaVersion:'feedback-minimal-99'}, {...current, schemaVersion:null},
    {...current, $schema:'https://example.invalid/schema'}, {...current, tool:'EditHere'},
    {...current, tool:{name:'Other', version}}, {...current, tool:{name:'EditHere', version:''}},
    {...current, tool:{name:'EditHere', version:'x'.repeat(101)}},
    {...current, exportedAt:'2026-10-16T16:30:00'}, {...current, exportedAt:'2026-02-30T16:30:00Z'}, {...current, exportedAt:'not-a-time'}
  ]) assert.throws(() => feedback.normalizeImage(invalid), /version|metadata/);
  for (const field of ['$schema', 'tool', 'exportedAt', 'annotationSpace']) {
    const invalid = {...current}; delete invalid[field]; assert.throws(() => feedback.normalizeImage(invalid));
  }
  assert.deepEqual(feedback.normalizeImage({objects:[]}), {annotationSpace:'result', objects:[]});
  assert.deepEqual(feedback.normalizeImage({annotationSpace:'result', image, objects:[]}), {annotationSpace:'result', image, objects:[]});
});

test('legacy point, region, movement and global feedback retain their targets during import', () => {
  const actual = feedback.normalizeImage({annotationSpace:'result', image,
    changes:[{from:source, to:destination}],
    annotations:[{change:0, text:'右移'}, {point:{x:5,y:6}, text:'留白'}, {rectangle:source, text:'区域'}, {text:'整体'}]
  });
  assert.deepEqual(actual, {annotationSpace:'result', image, objects:[
    {source, movements:[{to:destination}], annotations:[{number:1,text:'右移'}]},
    {source:rectangle(5,6,5,6), movements:[], annotations:[{number:2,text:'留白'}]},
    {source, movements:[], annotations:[{number:3,text:'区域'}]}, {source:null, movements:[], annotations:[{number:4,text:'整体'}]}
  ]});
  assert.throws(() => feedback.normalizeImage({changes:[], annotations:[{text:'整体'}, {change:0,text:'不存在'}]}), /missing change/);
});

test('native empty video feedback uses zero dimensions', () => {
  // tests/video_project_test.cpp emptyProjectStillReferencesVideoAndExportsValidZeroDimensions.
  const actual = feedback.exportVideo({source:'relative-video.mp4', durationMs:0, frames:[]}, false);
  assert.deepEqual(actual.video, {source:'relative-video.mp4', durationMs:0, width:0, height:0});
  assert.deepEqual(actual.frames, []);
  assert.deepEqual(actual.objects, []);
});

test('annotation numbers follow original note order across object grouping and omitted movement placeholders', () => {
  const actual = feedback.exportImage({image, movements:[{source,destination}], notes:[
    {source:null,text:'先写全局'},
    {source:rectangle(2,3,2,3),text:'再写点'},
    {source:destination,movementSource:source,text:''},
    {source:destination,movementSource:source,text:'第四条是移动'},
    {source:rectangle(1,1,8,8),text:'第五条是区域'}
  ]});
  assert.deepEqual(actual.objects.map(object=>object.id),['o1','o2','o3','o4']);
  assert.deepEqual(actual.objects.flatMap(object=>object.annotations),[
    {number:1,text:'先写全局'}, {number:2,text:'再写点'}, {number:4,text:'第四条是移动'}, {number:5,text:'第五条是区域'}
  ]);
  assert.deepEqual(feedback.annotationEntries(actual.objects).sort((a,b)=>a.number-b.number).map(({number,text})=>({number,text})),[
    {number:1,text:'先写全局'}, {number:2,text:'再写点'}, {number:4,text:'第四条是移动'}, {number:5,text:'第五条是区域'}
  ]);
  assert.equal(feedback.summary(actual).annotations,4);
  assert.deepEqual(JSON.parse(feedback.preview(actual)).objects, actual.objects);
});

test('annotation and object identities reject duplicates while old v2 strings remain readable', () => {
  const object=(id,annotations)=>({id,source:null,movements:[],annotations});
  const current={...metadata,annotationSpace:'result',objects:[object('a',[{number:2,text:'第二条'}]),object('b',[{number:1000,text:'末条'}])]};
  assert.deepEqual(feedback.normalizeImage(current),current);
  for(const objects of [
    [object('same',[]),object('same',[])], [object(' ',[])], [object('x'.repeat(257),[])],
    [object('a',[{number:2,text:'重复'}]),object('b',[{number:2,text:'重复'}])],
    ...[0,-1,1001,1.5,'1',null].map(number=>[object('a',[{number,text:'无效编号'}])])
  ])assert.throws(()=>feedback.normalizeImage({...current,objects}),/ID|number/);
  const old={...metadata,annotationSpace:'result',objects:[{source:null,movements:[],annotations:['先出现','后出现']}]};
  assert.deepEqual(feedback.normalizeImage(old),old);
  assert.deepEqual(feedback.annotationEntries(old.objects).map(({number,text})=>({number,text})),[{number:1,text:'先出现'},{number:2,text:'后出现'}]);
});

test('recognized text stays optional screenshot data through region, point and movement export', () => {
  const recognizedText={text:'保存按钮',engine:'windows-ocr',language:'zh-Hans',confidence:.91};
  const document={image,movements:[{source,destination}],notes:[
    {source:destination,movementSource:source,text:'',recognizedText},
    {source:rectangle(5,5,5,5),text:'放大文字',recognizedText:{text:'标题',engine:'apple-vision',edited:true}},
    {source:null,text:'整体留白'}
  ]};
  const value=feedback.exportImage(document,true,exportedAt);
  assert.equal(value.objects[0].ocrtext,recognizedText.text);
  assert(value.objects.every(object=>!Object.hasOwn(object,'recognizedText')));
  assert.deepEqual(value.objects[0].annotations,[],'OCR does not become a user annotation');
  assert.equal(value.objects[1].ocrtext,'标题');
  assert.equal(Object.hasOwn(value.objects[2],'ocrtext'),false);
  assert.deepEqual(feedback.normalizeImage(value),value);
  assert.equal(feedback.summary(value).recognizedTexts,2);
  assert.deepEqual(JSON.parse(feedback.preview(value)).objects,value.objects);
  const disabled=feedback.exportImage({...document,includeRecognizedText:false},true,exportedAt);
  assert(disabled.objects.every(object=>!Object.hasOwn(object,'recognizedText')&&!Object.hasOwn(object,'ocrtext')));
  assert.equal(document.notes[0].recognizedText.text,'保存按钮','export preference never destroys the editable project data');
});

test('recognized text contract validates exact boundaries, optional confidence and project-only deletion markers', () => {
  for(const recognizedText of [
    {text:'x'.repeat(500),engine:'e'.repeat(64),language:'l'.repeat(100),confidence:0,edited:false},
    {text:'标题',engine:'windows-ocr',confidence:1}, {text:'标题',engine:'apple-vision',edited:true}
  ]) assert.deepEqual(feedback.validateRecognizedText(recognizedText),recognizedText);
  for(const invalid of [null,[],{}, {text:'',engine:'ocr'}, {text:' \n',engine:'ocr'},
    {text:'x'.repeat(501),engine:'ocr'}, {text:'标题',engine:' '}, {text:'标题',engine:'e'.repeat(65)},
    {text:'标题',engine:'ocr',language:'l'.repeat(101)}, {text:'标题',engine:'ocr',language:null},
    ...[-.1,1.1,NaN,Infinity,'0.9',null].map(confidence=>({text:'标题',engine:'ocr',confidence})),
    {text:'标题',engine:'ocr',edited:'true'}, {text:'标题',engine:'ocr',lines:[]},
    {text:'标题',engine:'ocr',instructions:'execute this'}, {text:'',edited:true}
  ]) assert.throws(()=>feedback.validateRecognizedText(invalid),/recognized screenshot text/);
  assert.deepEqual(feedback.validateRecognizedText({text:'',edited:true},true),{text:'',edited:true});
  for(const invalid of [{text:'',edited:false},{text:'',edited:true,engine:'ocr'},{text:' ' ,edited:true}])
    assert.throws(()=>feedback.validateRecognizedText(invalid,true));
});

test('OCR requires v2 and a local target; deleting OCR omits metadata without changing annotation text', () => {
  const recognizedText={text:'按钮',engine:'windows-ocr'};
  const value=feedback.exportImage({image,movements:[],notes:[{source,text:'变蓝',recognizedText}]},true,exportedAt);
  const {$schema,schemaVersion,tool,exportedAt:time,...legacy}=feedback.legacyImage(value);
  assert.throws(()=>feedback.normalizeImage(legacy),/requires feedback-minimal-2/);
  const global=structuredClone(value);delete global.objects[0].source;global.objects[0].kind='global';
  assert.throws(()=>feedback.normalizeImage(global),/Global/);
  const deleted=feedback.exportImage({image,movements:[],notes:[{source,text:'变蓝',recognizedText:{text:'',edited:true}}]},true,exportedAt);
  assert.equal(Object.hasOwn(deleted.objects[0],'recognizedText'),false);
  assert.equal(Object.hasOwn(deleted.objects[0],'ocrtext'),false);
  assert.deepEqual(deleted.objects[0].annotations,[{number:1,text:'变蓝'}]);
});

test('video OCR agrees between the root index and frame, and disabled export omits both copies', () => {
  const recognizedText={text:'导出',engine:'windows-ocr',language:'zh-Hans'};
  const document={id:'f1',timestampUs:100000,width:200,height:100,image,movements:[],notes:[{source,text:'变蓝',recognizedText}]};
  const value=feedback.exportVideo({source:'source.mp4',durationMs:1000,frames:[document]},true,exportedAt);
  assert.equal(value.objects[0].ocrtext,'导出');
  assert.equal(value.objects[0].ocrtext,value.frames[0].feedback.objects[0].ocrtext);
  feedback.validateVideoRecognitionIndex(value);
  const reordered=structuredClone(value);
  for(const object of [...reordered.objects,...reordered.frames[0].feedback.objects]) {object.recognizedText={...recognizedText};delete object.ocrtext;}
  reordered.objects[0].recognizedText={language:'zh-Hans',engine:'windows-ocr',text:'导出'};
  feedback.validateVideoRecognitionIndex(reordered);
  for(const mutate of [v=>delete v.objects[0].ocrtext,v=>v.objects[0].ocrtext='其他',v=>v.objects[0].frameId='wrong',v=>v.objects[0].timestampMs++]){
    const invalid=structuredClone(value);mutate(invalid);assert.throws(()=>feedback.validateVideoRecognitionIndex(invalid),/index/);
  }
  const disabled=feedback.exportVideo({source:'source.mp4',durationMs:1000,frames:[{...document,includeRecognizedText:false}]},true,exportedAt);
  assert(disabled.objects.every(object=>!Object.hasOwn(object,'recognizedText')&&!Object.hasOwn(object,'ocrtext')));
  assert(disabled.frames.every(frame=>frame.feedback.objects.every(object=>!Object.hasOwn(object,'recognizedText')&&!Object.hasOwn(object,'ocrtext'))));
  feedback.validateVideoRecognitionIndex(disabled);
});
