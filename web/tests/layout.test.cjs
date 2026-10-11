'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const layout = require('../src/layout.js');
const rect = (x1, y1, x2, y2) => ({ x1, y1, x2, y2 });
const area = r => (r.x2 - r.x1) * (r.y2 - r.y1);
const clone = value => JSON.parse(JSON.stringify(value));
const approximately = (a, b) => assert.ok(Math.abs(a - b) < 1e-6, `${a} ≠ ${b}`);
const sameRect = (a, b) => Object.keys(b).forEach(key => approximately(a[key], b[key]));
const selectedPieces = (state, id) => {
  const ids = new Set(state.groups.find(g => g.id === id).pieces);
  return state.pieces.filter(p => ids.has(p.id));
};

test('initial candidates subdivide without holes or repeated source pixels', () => {
  const state = layout.create(100, 80, [
    { bounds: rect(0, 0, 100, 80), label: 'background' },
    { bounds: rect(10, 10, 70, 50), label: 'panel' },
    { bounds: rect(20, 20, 35, 35), label: 'child' },
    { bounds: rect(20, 20, 35, 35), label: 'duplicate' },
    { bounds: rect(-10, 50, 40, 100), label: 'clipped' }
  ]);
  assert.equal(state.groups.length, 3);
  assert.equal(state.groups.some(g => g.label === 'background'), false);
  assert.equal(state.groups.some(g => g.label === 'duplicate'), false);
  assert.equal(layout.validate(state, 100, 80), true);
  assert.equal(state.pieces.reduce((sum, p) => sum + area(p.source), 0), 8000);
  assert.deepEqual(layout.movements(state), []);
  assert.equal(layout.hit(state, 22, 22)[0].label, 'child');
});

test('repeated translations and scale preserve source geometry and produce the final movement', () => {
  const state = layout.create(100, 100);
  const id = layout.addRegion(state, rect(10, 10, 30, 30));
  layout.transform(state, id, rect(40, 40, 60, 60));
  layout.transform(state, id, rect(50, 20, 90, 60));
  layout.transform(state, id, rect(20, 60, 40, 80));
  assert.deepEqual(layout.movements(state), [{ source: rect(10, 10, 30, 30), movements: [{ to: rect(20, 60, 40, 80) }], annotations: [] }]);
  assert.equal(layout.validate(state), true);
  layout.transform(state, id, rect(10, 10, 30, 30));
  assert.deepEqual(layout.movements(state), []);
});

test('target constraint retains size near borders and limits oversized transforms', () => {
  const state = layout.create(100, 100);
  const id = layout.addRegion(state, rect(10, 10, 30, 30));
  assert.deepEqual(layout.transform(state, id, rect(-5, 95, 15, 115)), rect(0, 80, 20, 100));
  assert.deepEqual(layout.transform(state, id, rect(-20, -50, 150, 140)), rect(0, 0, 100, 100));
  assert.equal(layout.validate(state), true);
  assert.throws(() => layout.transform(state, id, rect(10, 10, 10, 20)), /尺寸必须为正/);
  assert.throws(() => layout.addRegion(state, rect(NaN, 0, 10, 10)), /有限数值/);
  assert.equal(layout.addRegion(state, rect(110, 110, 120, 120)), null);
});

test('new regions in a scaled moved piece map cuts to original image coordinates', () => {
  const state = layout.create(100, 100);
  const parent = layout.addRegion(state, rect(10, 10, 30, 30));
  layout.transform(state, parent, rect(40, 40, 80, 80));
  const child = layout.addRegion(state, rect(50, 50, 70, 70));
  const pieces = selectedPieces(state, child);
  assert.equal(pieces.length, 1, 'occluded background must not join the visible selection');
  sameRect(pieces[0].source, rect(15, 15, 25, 25));
  sameRect(pieces[0].to, rect(50, 50, 70, 70));
  layout.transform(state, child, rect(70, 0, 90, 20));
  assert.equal(layout.validate(state), true);
  sameRect(layout.bounds(state, parent), rect(40, 40, 80, 80));
  sameRect(layout.bounds(state, child), rect(70, 0, 90, 20));
});

test('a region across moved foreground and visible background selects each source only once', () => {
  const state = layout.create(100, 100);
  const moved = layout.addRegion(state, rect(10, 10, 30, 30));
  layout.transform(state, moved, rect(40, 40, 60, 60));
  const region = layout.addRegion(state, rect(50, 50, 70, 70));
  const pieces = selectedPieces(state, region);
  assert.equal(pieces.reduce((sum, p) => sum + area(p.to), 0), 400);
  const originalMoved = pieces.find(p => p.source.x1 < 30);
  assert.deepEqual(originalMoved.source, rect(20, 20, 30, 30));
  assert.equal(pieces.filter(p => p.source.x1 >= 40).reduce((sum, p) => sum + area(p.to), 0), 300);
  layout.transform(state, region, rect(60, 60, 80, 80));
  assert.equal(layout.validate(state), true);
});

test('nested child follows parent transforms and detaches when independently moved', () => {
  const state = layout.create(120, 120);
  const parent = layout.addRegion(state, rect(10, 10, 60, 60));
  const child = layout.addRegion(state, rect(20, 20, 40, 40));
  layout.transform(state, parent, rect(20, 20, 120, 120));
  sameRect(layout.bounds(state, child), rect(40, 40, 80, 80));
  layout.transform(state, child, rect(0, 0, 20, 20));
  sameRect(layout.bounds(state, parent), rect(20, 20, 120, 120));
  const reloaded = clone(state);
  assert.equal(layout.validate(reloaded), true);
  assert.deepEqual(layout.movements(reloaded), layout.movements(state));
  const additional = layout.addRegion(reloaded, rect(0, 0, 10, 10));
  layout.transform(reloaded, additional, rect(80, 0, 90, 10));
  assert.equal(layout.validate(reloaded), true);
});

test('detector subdivisions with one affine transform merge into one movement', () => {
  const state = layout.create(100, 100, [{ bounds: rect(20, 20, 30, 30) }]);
  const id = layout.addRegion(state, rect(10, 10, 40, 40));
  layout.transform(state, id, rect(50, 50, 80, 80));
  assert.deepEqual(layout.movements(state), [{ source: rect(10, 10, 40, 40), movements: [{ to: rect(50, 50, 80, 80) }], annotations: [] }]);
});

test('validation detects overlaps, missing source coverage and dangling references', () => {
  const state = layout.create(10, 10);
  const duplicated = clone(state);
  duplicated.pieces.push({ ...clone(duplicated.pieces[0]), id: 'duplicate' });
  assert.throws(() => layout.validate(duplicated), /重复使用原图像素/);
  const hole = clone(state);
  hole.pieces[0].source.x2 = 9;
  assert.throws(() => layout.validate(hole), /未完整覆盖原图/);
  const invalid = clone(state);
  invalid.groups.push({ id: 'dangling', label: 'invalid', pieces: ['missing'] });
  assert.throws(() => layout.validate(invalid), /引用无效/);
  const out = clone(state);
  out.pieces[0].to.x2 = 11;
  assert.throws(() => layout.validate(out), /坐标或编号不正确/);
});

test('an unrepresentable tiny scale fails without corrupting the current layout', () => {
  const state = layout.create(100, 100);
  layout.addRegion(state, rect(10, 10, 10.000001, 20));
  const parent = layout.addRegion(state, rect(0, 0, 50, 50));
  const before = clone(state);
  assert.throws(() => layout.transform(state, parent, rect(0, 0, 0.001, 0.001)), /区域过小/);
  assert.deepEqual(state, before);
  assert.equal(layout.validate(state), true);
});

test('paint clears the canvas, restores context and draws original background before moves', () => {
  const state = layout.create(100, 100);
  const id = layout.addRegion(state, rect(10, 10, 30, 30));
  layout.transform(state, id, rect(40, 40, 60, 60));
  const calls = [];
  const ctx = Object.fromEntries(['save', 'restore', 'setTransform', 'clearRect', 'beginPath', 'rect', 'clip', 'drawImage'].map(name => [name, (...args) => calls.push({ name, args })]));
  const image = {};
  layout.paint(ctx, image, state);
  assert.equal(calls[0].name, 'save');
  assert.deepEqual(calls.find(c => c.name === 'clearRect').args, [0, 0, 100, 100]);
  const draws = calls.filter(c => c.name === 'drawImage');
  assert.equal(draws.length, state.pieces.length);
  assert.deepEqual(draws[draws.length - 1].args, [image, 10, 10, 20, 20, 40, 40, 20, 20]);
  assert.equal(calls[calls.length - 1].name, 'restore');
});

test('deterministic mixed edit sequence keeps a complete, unique source partition', () => {
  const state = layout.create(180, 140);
  let seed = 1234567;
  const random = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
  for (let i = 0; i < 35; ++i) {
    const x = Math.floor(random() * 130), y = Math.floor(random() * 90);
    const id = layout.addRegion(state, rect(x, y, x + 15 + random() * 25, y + 15 + random() * 25));
    if (id) {
      const b = layout.bounds(state, id);
      const dx = random() * 80 - 40, dy = random() * 70 - 35, factor = 0.6 + random() * 1.2;
      layout.transform(state, id, rect(b.x1 + dx, b.y1 + dy, b.x1 + dx + (b.x2 - b.x1) * factor, b.y1 + dy + (b.y2 - b.y1) * factor));
    }
    assert.equal(layout.validate(state), true, 'invalid after edit ' + i);
    const sequences = layout.movements(state);
    assert.equal(new Set(sequences.map(s => JSON.stringify(s.source))).size, sequences.length);
  }
});

test('hit selects visible membership, retaining nested choices while excluding covered groups and holes', () => {
  const state = layout.create(100, 100);
  const lower = layout.addRegion(state, rect(40, 40, 60, 60));
  const upper = layout.addRegion(state, rect(10, 10, 30, 30));
  layout.transform(state, upper, rect(40, 40, 60, 60));
  assert.deepEqual(layout.hit(state, 50, 50).map(c => c.id), [upper]);
  assert.equal(layout.hit(state, 50, 50).some(c => c.id === lower), false);
  assert.deepEqual(layout.hit(state, 20, 20), []);
  const nested = layout.create(100, 100);
  const parent = layout.addRegion(nested, rect(10, 10, 60, 60));
  const child = layout.addRegion(nested, rect(20, 20, 30, 30));
  assert.deepEqual(layout.hit(nested, 25, 25).map(c => c.id), [child, parent]);
  layout.transform(nested, child, rect(80, 80, 90, 90));
  assert.deepEqual(layout.hit(nested, 85, 85).map(c => c.id), [child]);
  assert.deepEqual(layout.hit(nested, 70, 70), [], 'a parent bounding-box gap has no visible member');
});

test('ordinary rect notes across detector subdivisions follow one common affine transform', () => {
  const state = layout.create(100, 100, [{ bounds: rect(20, 20, 30, 30) }]);
  const parent = layout.addRegion(state, rect(10, 10, 40, 40));
  const notes = [
    { id: 'rect', text: 'panel', source: rect(10, 10, 40, 40), meta: { tags: ['local'] } },
    { id: 'point', text: 'pixel', source: rect(25, 25, 25, 25) },
    { id: 'group', text: 'component', source: rect(10, 10, 40, 40), groupId: parent },
    { id: 'global', text: 'global', source: null }
  ];
  const unchanged = clone(notes), before = clone(state);
  layout.transform(state, parent, rect(40, 40, 100, 100));
  const remapped = layout.remapNotes(notes, before, state);
  sameRect(remapped[0].source, rect(40, 40, 100, 100));
  sameRect(remapped[1].source, rect(70, 70, 70, 70));
  assert.deepEqual(remapped[2], notes[2]);
  assert.equal(remapped[3].source, null);
  assert.deepEqual(notes, unchanged, 'input notes are untouched');
  remapped[0].meta.tags.push('copy');
  assert.deepEqual(notes[0].meta.tags, ['local'], 'returned notes are deep clones');
});

test('point notes follow the visible layer when another component returns to its source', () => {
  const state = layout.create(100, 100);
  const a = layout.addRegion(state, rect(10, 10, 30, 30));
  const b = layout.addRegion(state, rect(40, 40, 60, 60));
  layout.transform(state, a, rect(40, 40, 60, 60));
  layout.transform(state, b, rect(70, 70, 90, 90));
  layout.transform(state, b, rect(40, 40, 60, 60));
  const before = clone(state), notes = [{ id: 'point', text: 'visible A', source: rect(50, 50, 50, 50) }];
  layout.transform(state, a, rect(60, 60, 80, 80));
  assert.deepEqual(layout.remapNotes(notes, before, state)[0].source, rect(70, 70, 70, 70));
});

test('ordinary rect notes spanning different transforms or a transparent hole keep their anchor', () => {
  const state = layout.create(100, 100);
  const parent = layout.addRegion(state, rect(10, 10, 40, 40));
  const child = layout.addRegion(state, rect(20, 20, 30, 30));
  const before = clone(state), notes = [{ id: 'rect', text: 'mixed', source: rect(10, 10, 40, 40) }];
  layout.transform(state, child, rect(50, 50, 60, 60));
  assert.deepEqual(layout.remapNotes(notes, before, state), notes);
  const beforeHole = clone(state);
  layout.transform(state, parent, rect(20, 20, 70, 70));
  assert.deepEqual(layout.remapNotes(notes, beforeHole, state), notes, 'missing visible coverage must not infer a transform');
});

test('remapped narrow rect notes retain positive fractional geometry inside the canvas', () => {
  const state = layout.create(100, 100);
  const id = layout.addRegion(state, rect(10, 10, 30, 30));
  const before = clone(state), notes = [{ id: 'tiny', text: 'thin', source: rect(10.2, 10.4, 10.7, 20.4) }];
  layout.transform(state, id, rect(-5, 95, 5, 100));
  const mapped = layout.remapNotes(notes, before, state)[0].source;
  sameRect(mapped, rect(0.1, 95.1, 0.35, 97.6));
  assert.ok(mapped.x2 > mapped.x1 && mapped.y2 > mapped.y1);
  assert.ok(mapped.x1 >= 0 && mapped.y1 >= 0 && mapped.x2 <= state.width && mapped.y2 <= state.height);
});

test('an independently moved child is detached and stays put when its former parent moves', () => {
  const state = layout.create(100, 100);
  const parent = layout.addRegion(state, rect(10, 10, 60, 60));
  const child = layout.addRegion(state, rect(20, 20, 30, 30));
  const childIds = new Set(state.groups.find(g => g.id === child).pieces);
  layout.transform(state, child, rect(80, 80, 90, 90));
  const parentGroup = state.groups.find(g => g.id === parent);
  assert.equal(parentGroup.pieces.some(id => childIds.has(id)), false);
  sameRect(layout.bounds(state, parent), rect(10, 10, 60, 60));
  layout.transform(state, parent, rect(0, 0, 50, 50));
  sameRect(layout.bounds(state, child), rect(80, 80, 90, 90));
  assert.equal(layout.validate(state), true);
});

test('no-op and invalid child transforms retain parent membership', () => {
  const state = layout.create(100, 100);
  const parent = layout.addRegion(state, rect(10, 10, 60, 60));
  const child = layout.addRegion(state, rect(20, 20, 30, 30));
  const original = clone(state);
  layout.transform(state, child, rect(20, 20, 30, 30));
  assert.deepEqual(state, original);
  assert.throws(() => layout.transform(state, child, rect(20, 20, 20, 30)), /尺寸必须为正/);
  assert.deepEqual(state, original);
  layout.transform(state, parent, rect(20, 20, 70, 70));
  sameRect(layout.bounds(state, child), rect(30, 30, 40, 40));
  const childIds = state.groups.find(g => g.id === child).pieces;
  assert.ok(childIds.every(id => state.groups.find(g => g.id === parent).pieces.includes(id)));
});

test('moving a parent carries internal child refs exactly once', () => {
  const state = layout.create(100, 100);
  const parent = layout.addRegion(state, rect(10, 10, 50, 50));
  const child = layout.addRegion(state, rect(20, 20, 30, 30));
  layout.transform(state, parent, rect(20, 20, 100, 100));
  sameRect(layout.bounds(state, child), rect(40, 40, 60, 60));
  assert.deepEqual(layout.hit(state, 45, 45).map(c => c.id), [child, parent]);
  assert.ok(state.groups.find(g => g.id === child).pieces.every(id => state.groups.find(g => g.id === parent).pieces.includes(id)));
  assert.equal(layout.validate(state), true);
});

function adjacentMovedGroups() {
  const state = layout.create(100, 100);
  const a = layout.addRegion(state, rect(10, 10, 30, 30));
  const b = layout.addRegion(state, rect(30, 10, 50, 30));
  layout.transform(state, a, rect(50, 50, 70, 70));
  layout.transform(state, b, rect(70, 50, 90, 70));
  return { state, a: new Set(state.groups.find(g => g.id === a).pieces), b: new Set(state.groups.find(g => g.id === b).pieces) };
}

test('adjacent movements with different component notes keep their individual locations', () => {
  const { state, a } = adjacentMovedGroups();
  const notesA = ['change A'], notesB = ['change B'];
  const sequences = layout.movements(state, piece => a.has(piece.id) ? notesA : notesB);
  assert.deepEqual(sequences, [
    { source: rect(10, 10, 30, 30), movements: [{ to: rect(50, 50, 70, 70) }], annotations: ['change A'] },
    { source: rect(30, 10, 50, 30), movements: [{ to: rect(70, 50, 90, 70) }], annotations: ['change B'] }
  ]);
  notesA.push('later input mutation');
  assert.deepEqual(sequences[0].annotations, ['change A']);
  sequences[1].annotations.push('later output mutation');
  assert.deepEqual(notesB, ['change B']);
});

test('adjacent movements with identical ordered annotations still merge', () => {
  const { state } = adjacentMovedGroups();
  assert.deepEqual(layout.movements(state, () => ['same', 'second']), [
    { source: rect(10, 10, 50, 30), movements: [{ to: rect(50, 50, 90, 70) }], annotations: ['same', 'second'] }
  ]);
  // Equal strings in a different order must preserve the individual scopes.
  const idsA = new Set(state.groups[0].pieces);
  assert.equal(layout.movements(state, piece => idsA.has(piece.id) ? ['same', 'second'] : ['second', 'same']).length, 2);
});

test('movements without an annotation callback retain existing merged geometry', () => {
  const { state } = adjacentMovedGroups();
  const expected = [{ source: rect(10, 10, 50, 30), movements: [{ to: rect(50, 50, 90, 70) }], annotations: [] }];
  assert.deepEqual(layout.movements(state), expected);
  assert.deepEqual(layout.movements(state, () => []), expected);
  assert.throws(() => layout.movements(state, () => 'wrong'), /批注格式不正确/);
});
