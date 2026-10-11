'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const feedback = require('../src/feedback.js');
const rectangle = (x1, y1, x2, y2) => ({x1, y1, x2, y2});
const image = 'data:image/png;base64,b3JpZ2luYWw=';
const source = rectangle(10, 10, 70, 50);
const destination = rectangle(50, 50, 140, 110);

test('native minimalFeedbackRoundTrips fixture keeps movement and independent result notes', () => {
  // tests/layout_test.cpp minimalFeedbackRoundTripsAndRejectsCorruption.
  const actual = feedback.exportImage({image,
    movements:[{source, destination}],
    notes:[{source:rectangle(20, 20, 20, 20), text:'加大标题'}, {source, text:'表格右移'}]
  }, false);
  assert.deepEqual(actual, {annotationSpace:'result', objects:[
    {source, movements:[{to:destination}], annotations:[]},
    {source:rectangle(20, 20, 20, 20), movements:[], annotations:['加大标题']},
    {source, movements:[], annotations:['表格右移']}
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
  assert.deepEqual(actual.objects, [
    {source, movements:[{to:destination}, {to:rectangle(60, 60, 150, 120)}], annotations:['保持这个位置，视觉更轻盈']},
    {source, movements:[], annotations:['独立框批注']},
    {source:null, movements:[], annotations:['整体采用杂志风格']}
  ]);
  assert.equal(actual.image, image, 'the embedded image is the unchanged original');
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
  assert.deepEqual(actual.objects, [
    {source:parent, movements:[{to:rectangle(100, 10, 180, 90)}], annotations:['父区域']},
    {source:child, movements:[{to:rectangle(20, 10, 60, 90)}], annotations:['子区域']}
  ]);
});

test('native returning home drops movement while retaining its text on the original source', () => {
  // tests/layout_test.cpp movement-attached/global notes round-trip fixture.
  const actual = feedback.exportImage({image, movements:[{source, destination:source}],
    notes:[{source, movementSource:source, text:'回到原位后保留'}]
  }, false);
  assert.deepEqual(actual.objects, [{source, movements:[], annotations:['回到原位后保留']}]);
});

test('native fractional sources remain distinct even when nearly equal', () => {
  // tests/layout_test.cpp feedbackKeepsDistinctFractionalSourceRegions.
  const actual = feedback.exportImage({image,
    movements:[
      {source:rectangle(10.000001, 10, 30.000001, 30), destination:rectangle(50, 30, 70, 50)},
      {source:rectangle(10.000002, 10, 30.000002, 30), destination:rectangle(80, 50, 100, 70)}
    ], notes:[]
  }, false);
  assert.equal(actual.objects.length, 2);
  assert.equal(actual.objects[0].source.x1, 10.000001);
  assert.equal(actual.objects[1].source.x1, 10.000002);
});

test('native compact export and folded image preview carry separate complete bytes', () => {
  // app/model.cpp serializeFeedback + app/editor.cpp JSON preview/copy handlers.
  const actual = feedback.exportImage({image, movements:[{source, destination}], notes:[]});
  const bytes = feedback.serialize(actual);
  assert.equal(bytes, '{"annotationSpace":"result","image":"data:image/png;base64,b3JpZ2luYWw=","objects":[{"annotations":[],"movements":[{"to":{"x1":50,"x2":140,"y1":50,"y2":110}}],"source":{"x1":10,"x2":70,"y1":10,"y2":50}}]}\n');
  assert.equal(feedback.preview(actual), [
    '{',
    '  "image": "data:image/png;base64,[图片编码已折叠]",',
    '  "annotationSpace": "result",',
    '  "objects": [',
    '    {"annotations":[],"movements":[{"to":{"x1":50,"x2":140,"y1":50,"y2":110}}],"source":{"x1":10,"x2":70,"y1":10,"y2":50}}',
    '  ]',
    '}'
  ].join('\n'));
  assert.deepEqual(JSON.parse(bytes), actual);
  assert.equal(actual.image, image, 'preview cannot replace original bytes');
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
    assert.equal(frame.feedback.image, image);
    assert.deepEqual(actual.objects.filter(object => object.frameId === frame.id),
      frame.feedback.objects.map(object => ({...object, timestampMs:frame.timestampMs, frameId:frame.id})));
  }
  assert.equal(actual.frames[2].feedback.objects[0].annotations.length, 0);
  assert.equal(feedback.serialize(actual).split(image).length - 1, 3, 'one original per frame, none in the root index');
  assert(!feedback.preview(actual).includes('b3JpZ2luYWw='));
  assert(feedback.serialize(actual).includes('b3JpZ2luYWw='));
});

test('native empty video feedback uses zero dimensions', () => {
  // tests/video_project_test.cpp emptyProjectStillReferencesVideoAndExportsValidZeroDimensions.
  const actual = feedback.exportVideo({source:'relative-video.mp4', durationMs:0, frames:[]}, false);
  assert.deepEqual(actual.video, {source:'relative-video.mp4', durationMs:0, width:0, height:0});
  assert.deepEqual(actual.frames, []);
  assert.deepEqual(actual.objects, []);
});
