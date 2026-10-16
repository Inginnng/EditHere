'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const layout = require('../src/layout.js');
const feedback = require('../src/feedback.js');
const rect = (x1, y1, x2, y2) => ({ x1, y1, x2, y2 });
const plain = value => JSON.parse(JSON.stringify(value));

// Execute the complete production app. Only browser rendering and DOM storage
// are stubbed; transforms, note submission, reset, history and feedback export
// below all call the real closures from app.js.
function appHarness() {
  const elements = new Map();
  const drawing = new Proxy({}, { get: (object, key) => object[key] ?? (() => {}), set: (object, key, value) => (object[key] = value, true) });
  function element(id) {
    if (!elements.has(id)) {
      const classes = new Set();
      const node = { id, style: {}, dataset: {}, value: '', textContent: '', innerHTML: '', hidden: false, checked: true,
        classList: { add: name => classes.add(name), remove: name => classes.delete(name), toggle: (name, force) => force ? classes.add(name) : classes.delete(name), contains: name => classes.has(name) },
        addEventListener() {}, setAttribute() {}, removeAttribute() {}, focus() {}, select() {}, setCustomValidity() {}, reportValidity() {}, append() {},
        querySelector: selector => element(id + ' ' + selector), querySelectorAll: () => [],
        getContext: () => drawing, getBoundingClientRect: () => ({ x: 0, y: 0, left: 0, top: 0, right: 200, bottom: 100, width: 200, height: 100 }),
        showModal() { this.open = true; }, close() { this.open = false; }, click() { return this.onclick?.(); },
        pause() {}, load() {}, hasPointerCapture: () => false, releasePointerCapture() {}, setPointerCapture() {}, clientWidth: 200, clientHeight: 100 };
      elements.set(id, node);
    }
    return elements.get(id);
  }
  const document = { getElementById: element, querySelector: selector => element(selector.replace(/^#/, '')), querySelectorAll: () => [],
    addEventListener() {}, createElement: tag => element('created-' + tag), documentElement: { outerHTML: '', cloneNode:()=>({outerHTML:'',querySelectorAll:()=>[]}) }, body: element('body') };
  const sandbox = { document, EditHereLayout: layout, EditHereFeedback: feedback, window: { addEventListener() {}, dispatchEvent() {} }, Event:class {}, console,
    ResizeObserver: class { observe() {} }, setTimeout: () => 0, clearTimeout() {}, getComputedStyle: () => ({ display: 'block', visibility: 'visible', opacity: '1' }),
    // Media decoding is outside these model tests. Preserve fixture dimensions
    // while exercising the actual project metadata restore/validation path.
    Image: class { constructor() { this.complete = false; this.naturalWidth = 200; this.naturalHeight = 100; } async decode() {} } };
  vm.createContext(sandbox);
  const production = fs.readFileSync(path.join(__dirname, '../src/app.js'), 'utf8');
  const exposure = `
    globalThis.__movementTest = {
      load(value) {
        doc = makeDoc({ complete: false, naturalWidth: value.width, naturalHeight: value.height }, 'data:image/png;base64,Zml4dHVyZQ==', null, 'movement-fixture');
        doc.layout = clone(value); session = { kind: 'image', name: 'fixture.png', width: value.width, height: value.height, document: doc };
        selected = null;
      },
      select(id) { chooseGroup(id); },
      addToSelection(id) { chooseGroup(id, true); },
      transform(destination) { applyTransform(destination); },
      moveSelection(destination) {
        $('gx').value = destination.x1; $('gy').value = destination.y1;
        $('gw').value = destination.x2 - destination.x1; $('gh').value = destination.y2 - destination.y1;
        $('apply-geometry').onclick();
      },
      annotate(groupId, text) {
        const marker = movementMarkers().find(item => item.groupId === groupId);
        if (!marker) throw new Error('No active arrow for ' + groupId);
        editMovement(marker); $('note-text').value = text; $('note-form').onsubmit({ preventDefault() {} });
      },
      annotateSelection(text) { $('component-note').onclick(); $('note-text').value = text; $('note-form').onsubmit({ preventDefault() {} }); },
      reset() { $('reset-component').onclick(); },
      undo() { travelHistory('undo'); }, redo() { travelHistory('redo'); },
      state() { return clone({ layout: doc.layout, notes: doc.notes, raw: rawMovements(), markers: movementMarkers() }); },
      noteBounds(id) { return noteRect(doc.notes.find(note => note.id === id)); },
      serialized() { return serializedDoc(doc); },
      async restore(value) { const restored = await restoredDoc(value, session.width, session.height, false); doc = restored; session.document = doc; chooseGroup(null); },
      async importFeedback(value) {
        const restored = await docFromFeedback(value); doc = restored.doc;
        session = { kind: 'image', name: 'native-feedback.png', width: restored.width, height: restored.height, document: doc };
        chooseGroup(null);
      },
      async restoreAgentFeedback(value) { await globalThis.EditHereWeb.loadAgentFeedback({image:value.image,name:'saved-feedback',feedback:value}); },
      rawFeedback() { return feedback(); },
      feedback() { return exportDoc(doc, false); }
    };
  `;
  assert.match(production, /\}\)\(\);\s*$/, 'app IIFE must have an identifiable end');
  vm.runInContext(production.replace(/\}\)\(\);\s*$/, exposure + '\n})();'), sandbox, { filename: 'app.js' });
  const actual = sandbox.__movementTest;
  return { ...actual, state: () => plain(actual.state()), rawFeedback: () => plain(actual.rawFeedback()), feedback: () => {
    const {$schema, schemaVersion, tool, exportedAt, ...value} = plain(actual.feedback());
    return value; // These model fixtures compare geometry; metadata has its own export tests.
  }, noteBounds: id => plain(actual.noteBounds(id)), serialized: () => plain(actual.serialized()) };
}

function fixture(app) {
  const value = layout.create(200, 100, [
    { label: 'child', bounds: rect(10, 10, 50, 90) },
    { label: 'parent', bounds: rect(10, 10, 90, 90) }
  ]);
  const parent = value.groups.find(group => group.label === 'parent').id;
  const child = value.groups.find(group => group.label === 'child').id;
  app.load(value);
  return { parent, child };
}

function groupPieces(value, id) {
  const members = new Set(value.groups.find(group => group.id === id).pieces);
  return value.pieces.filter(piece => members.has(piece.id));
}

test('native nested parent and child feedback imports and reexports the same original source frames and targets', async () => {
  const app = appHarness();
  const objects = [
    { source: rect(10, 10, 90, 90), movements: [{ to: rect(100, 10, 180, 90) }], annotations: ['整体移到右侧'] },
    { source: rect(10, 10, 50, 90), movements: [{ to: rect(20, 10, 60, 90) }], annotations: ['左边子组件单独调整'] }
  ];
  await app.importFeedback({ annotationSpace: 'result', image: 'data:image/png;base64,Zml4dHVyZQ==', objects });
  assert.deepEqual(app.feedback(), { annotationSpace: 'result', objects });
  const state = app.state();
  assert.deepEqual(state.markers.map(marker => ({ source: marker.source, destination: marker.destination })), objects.map(object => ({ source: object.source, destination: object.movements[0].to })));
  assert.deepEqual(state.notes.map(note => note.text), ['整体移到右侧', '左边子组件单独调整']);
  assert.equal(layout.validate(state.layout), true);
});

test('the editor imports versioned and legacy image feedback and rejects unknown versions before replacing work', async () => {
  const app = appHarness();
  const source = rect(10, 10, 40, 30), destination = rect(50, 10, 80, 30);
  const current = feedback.exportImage({image:'data:image/png;base64,Zml4dHVyZQ==', movements:[{source,destination}], notes:[{source,movementSource:source,text:'保留位移'}]});
  await app.importFeedback(current);
  assert.deepEqual(app.feedback().objects, current.objects);
  const before = app.state();
  await assert.rejects(app.importFeedback({...current,schemaVersion:'feedback-minimal-99'}), /version/);
  assert.deepEqual(app.state(), before);
  await app.importFeedback({annotationSpace:'result',image:current.image,changes:[{from:source,to:destination}],annotations:[{change:0,text:'旧移动'},{point:{x:1,y:2},text:'旧点'}]});
  assert.deepEqual(app.feedback().objects, [
    {source,movements:[{to:destination}],annotations:['旧移动']},
    {source:rect(1,2,1,2),movements:[],annotations:['旧点']}
  ]);
});

test('reopening saved Agent feedback preserves its original export metadata in a manual backup', async () => {
  const app = appHarness();
  const current = feedback.exportImage({image:'data:image/png;base64,Zml4dHVyZQ==', toolVersion:'0.10.0',movements:[],notes:[{source:null,text:'已保存'}]}, true, '2026-10-15T09:30:00+08:00');
  await app.restoreAgentFeedback(current);
  assert.deepEqual(app.rawFeedback(), current);
});

test('detaching a returned child preserves the original parent trajectory while reset restores only the remaining pixels', () => {
  const app = appHarness(), { parent, child } = fixture(app);
  app.select(parent); app.transform(rect(100, 10, 180, 90));
  app.select(child); app.transform(rect(10, 10, 50, 90));
  const detached = app.state(), marker = detached.markers.find(item => item.groupId === parent);
  assert.deepEqual(layout.bounds(detached.layout, parent), rect(140, 10, 180, 90));
  assert.deepEqual(layout.bounds(detached.layout, child), rect(10, 10, 50, 90));
  assert.deepEqual(marker.source, rect(10, 10, 90, 90), 'native trajectories retain their original frames independently from detached pixel pieces');
  assert.deepEqual(marker.destination, rect(100, 10, 180, 90));
  assert.deepEqual(detached.raw.find(item => item.groupId === parent).destination, marker.destination);
  assert.equal(detached.markers.some(item => item.groupId === child), false, 'a child at its original location has no active movement arrow');
  const childBeforeReset = groupPieces(detached.layout, child);
  app.select(parent); app.reset();
  const restored = app.state();
  assert.deepEqual(layout.bounds(restored.layout, parent), rect(50, 10, 90, 90));
  assert.deepEqual(groupPieces(restored.layout, child), childBeforeReset);
  for (const piece of groupPieces(restored.layout, parent)) assert.deepEqual(piece.to, piece.source, 'reset must preserve each remaining slice size');
  assert.deepEqual(layout.movements(restored.layout), []);
  assert.deepEqual(restored.markers, []);
  assert.equal(layout.validate(restored.layout), true);
});

test('parent and detached-child arrow notes keep their own source scopes through project reload, reset and history', async () => {
  const app = appHarness(), { parent, child } = fixture(app);
  app.select(parent); app.transform(rect(100, 10, 180, 90)); app.annotate(parent, 'parent right half');
  app.select(child); app.transform(rect(20, 10, 60, 90)); app.annotate(child, 'independent child');
  const detached = app.state(), parentNote = detached.notes.find(note => note.text === 'parent right half');
  assert.deepEqual(parentNote.movementSource, rect(10, 10, 90, 90));
  assert.deepEqual(parentNote.source, rect(100, 10, 180, 90), 'the parent note stays on its original destination frame when a child detaches');
  const moved = app.feedback().objects;
  assert.deepEqual(moved.find(object => object.annotations.includes('parent right half')), {
    source: rect(10, 10, 90, 90), movements: [{ to: rect(100, 10, 180, 90) }], annotations: ['parent right half']
  });
  assert.deepEqual(moved.find(object => object.annotations.includes('independent child')), {
    source: rect(10, 10, 50, 90), movements: [{ to: rect(20, 10, 60, 90) }], annotations: ['independent child']
  });
  assert.equal(moved.filter(object => object.annotations.includes('parent right half')).length, 1);
  assert.equal(moved.filter(object => object.annotations.includes('independent child')).length, 1);
  const savedDetached = app.serialized(); await app.restore(savedDetached);
  assert.deepEqual(app.feedback().objects, moved, 'project validation must accept retained parent frames after a child detaches');
  const childBeforeReset = groupPieces(detached.layout, child);
  app.select(parent); app.reset();
  const parentRestored = app.state(), feedbackRestored = app.feedback();
  assert.deepEqual(groupPieces(parentRestored.layout, child), childBeforeReset);
  assert.deepEqual(feedbackRestored.objects.find(object => object.annotations.includes('parent right half')), {
    source: rect(10, 10, 90, 90), movements: [], annotations: ['parent right half']
  });
  assert.deepEqual(feedbackRestored.objects.find(object => object.annotations.includes('independent child')), {
    source: rect(10, 10, 50, 90), movements: [{ to: rect(20, 10, 60, 90) }], annotations: ['independent child']
  });
  app.undo(); assert.deepEqual(app.feedback(), { annotationSpace: 'result', objects: moved });
  app.redo(); assert.deepEqual(app.feedback(), feedbackRestored);
  app.select(child); app.reset();
  assert.deepEqual(layout.movements(app.state().layout), []);
  assert.deepEqual(app.feedback().objects, [
    { source: rect(10, 10, 90, 90), movements: [], annotations: ['parent right half'] },
    { source: rect(10, 10, 50, 90), movements: [], annotations: ['independent child'] }
  ]);
});

test('moving A, then B, then A again preserves first-creation trajectory and note order through history', () => {
  const app = appHarness(), aSource = rect(10, 10, 30, 90), bSource = rect(70, 10, 90, 90);
  const value = layout.create(200, 100, [{ label: 'A', bounds: aSource }, { label: 'B', bounds: bSource }]);
  const a = value.groups.find(group => group.label === 'A').id, b = value.groups.find(group => group.label === 'B').id;
  app.load(value); app.select(a); app.moveSelection(rect(100, 10, 120, 90));
  app.select(b); app.moveSelection(rect(160, 10, 180, 90));
  const firstOrder = app.state(), before = app.feedback();
  app.select(a); app.moveSelection(rect(110, 10, 130, 90));
  const repeated = app.state(), after = app.feedback();
  assert.deepEqual(repeated.raw.map(movement => movement.source), [aSource, bSource], 'updating an existing trajectory must not append it behind later movements');
  assert.deepEqual(after.objects.map(object => object.source), [aSource, bSource]);
  assert.deepEqual(repeated.notes.map(note => note.id), firstOrder.notes.map(note => note.id));
  assert.deepEqual(after.objects[0].movements, [{ to: rect(110, 10, 130, 90) }]);
  app.undo(); assert.deepEqual(app.feedback(), before); app.redo(); assert.deepEqual(app.feedback(), after);
});

test('one multi-selection note follows disjoint components and exports its result bounds independently from the actual trajectories', async () => {
  const app = appHarness();
  const value = layout.create(200, 100, [
    { label: 'A', bounds: rect(10, 10, 30, 90) },
    { label: 'B', bounds: rect(70, 10, 90, 90) }
  ]);
  const a = value.groups.find(group => group.label === 'A').id, b = value.groups.find(group => group.label === 'B').id;
  app.load(value); app.select(a); app.addToSelection(b); app.annotateSelection('move both components together');
  const original = app.state();
  assert.equal(original.notes.length, 1, 'one selection comment remains one UI note');
  const noteId = original.notes[0].id;
  assert.deepEqual(original.notes[0].groupIds, [a, b], 'the note must reference actual components rather than a temporary union group');
  assert.deepEqual(original.notes[0].source, rect(10, 10, 90, 90));
  app.moveSelection(rect(100, 10, 180, 90));
  const moved = app.state(), movedFeedback = app.feedback();
  assert.equal(moved.notes.length, 3, 'each newly created trajectory gets its own chronological empty movement note');
  assert.deepEqual(moved.notes[0].source, rect(100, 10, 180, 90));
  assert.deepEqual(app.noteBounds(noteId), rect(100, 10, 180, 90));
  assert.deepEqual(moved.notes[0].groupIds, [a, b]);
  assert.equal(moved.layout.groups.length, 2, 'temporary multi-selection geometry must not remain in the document');
  assert.deepEqual(movedFeedback.objects.slice().sort((left, right) => left.source.x1 - right.source.x1), [
    { source: rect(10, 10, 30, 90), movements: [{ to: rect(100, 10, 120, 90) }], annotations: [] },
    { source: rect(70, 10, 90, 90), movements: [{ to: rect(160, 10, 180, 90) }], annotations: [] },
    { source: rect(100, 10, 180, 90), movements: [], annotations: ['move both components together'] }
  ], 'only movementSource links text to a trajectory; an ordinary note uses its current result rectangle');
  app.undo();
  assert.deepEqual(app.state().notes, original.notes);
  assert.deepEqual(app.noteBounds(noteId), rect(10, 10, 90, 90));
  app.redo(); assert.deepEqual(app.state().notes, moved.notes); assert.deepEqual(app.feedback(), movedFeedback);
  const saved = JSON.parse(JSON.stringify(app.serialized()));
  assert.deepEqual(saved.notes[0].groupIds, [a, b]);
  await app.restore(saved);
  assert.deepEqual(app.state().notes, moved.notes); assert.deepEqual(app.noteBounds(noteId), rect(100, 10, 180, 90));
  assert.deepEqual(app.feedback(), movedFeedback);
  const invalid = JSON.parse(JSON.stringify(saved)); invalid.notes[0].groupIds = [a, 'missing-component'];
  await assert.rejects(() => app.restore(invalid), /组件|引用|不存在/, 'project restore must reject references to nonexistent components');
});
