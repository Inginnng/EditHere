/* Run: node web/tests/browser.test.cjs
 * Existing local dependencies are reused. Optional overrides:
 * EDITHERE_PLAYWRIGHT_MODULE, EDITHERE_BROWSER_PATH, EDITHERE_TEST_VIDEO.
 * Output files are generated in tests/output/ and must not be committed.
 */
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { pathToFileURL } = require('node:url');
const layout = require('../src/layout.js');
const { readExportJson } = require('./export-test-utils.cjs');

const root = path.resolve(__dirname, '../..');
const bundle = path.join(root, 'web/EditHere.html');
const output = path.join(__dirname, 'output');
const sibling = path.resolve(root, '../EditHere宣传');
const firstExisting = (candidates, label) => {
  const found = candidates.filter(Boolean).find(p => fs.existsSync(p));
  assert(found, `${label} unavailable. Set the matching EDITHERE_* environment override.`);
  return found;
};
const playwrightPath = firstExisting([
  process.env.EDITHERE_PLAYWRIGHT_MODULE,
  path.join(sibling, '动画/node_modules/playwright-core'),
  path.join(sibling, '短片-批注/node_modules/playwright-core'),
  path.join(root, 'node_modules/playwright-core')
], 'Playwright');
const executablePath = firstExisting([
  process.env.EDITHERE_BROWSER_PATH,
  'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe',
  process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'ms-playwright/chromium-1228/chrome-win64/chrome.exe'),
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
], 'Chromium');
const videoFixture = firstExisting([
  process.env.EDITHERE_TEST_VIDEO,
  path.join(sibling, '短片-视频修改/out/EditHere-视频改一帧-zh-clean.mp4'),
  path.join(sibling, '宣传视频.mp4')
], 'A local MP4/WebM fixture');
const { chromium } = require(playwrightPath);

// Validate the actual repository schemas, including local/external references.
// This deliberately supports only the keywords these two schemas use.
function validateSchema(value, schemaPath) {
  const cache = new Map();
  function read(file) {
    if (!cache.has(file)) cache.set(file, JSON.parse(fs.readFileSync(file, 'utf8')));
    return cache.get(file);
  }
  function check(v, s, file, location = '$') {
    if (s.$ref) {
      const [relative, pointer = ''] = s.$ref.split('#');
      const targetFile = relative ? path.resolve(path.dirname(file), relative) : file;
      let target = read(targetFile);
      for (const part of pointer.split('/').slice(1)) target = target[part.replace(/~1/g, '/').replace(/~0/g, '~')];
      assert(target, `unresolved schema reference ${s.$ref}`);
      check(v, target, targetFile, location);
    }
    if (s.oneOf) {
      const matches = s.oneOf.filter(branch => { try { check(v, branch, file, location); return true; } catch { return false; } });
      assert.equal(matches.length, 1, `${location}: oneOf`);
    }
    if (Object.hasOwn(s, 'const')) assert.deepEqual(v, s.const, `${location}: const`);
    if (s.type) {
      const valid = { null: v === null, object: v !== null && typeof v === 'object' && !Array.isArray(v),
        array: Array.isArray(v), number: typeof v === 'number' && Number.isFinite(v),
        integer: Number.isSafeInteger(v), string: typeof v === 'string' };
      assert(valid[s.type], `${location}: expected ${s.type}`);
    }
    if (typeof v === 'number') {
      if (s.minimum !== undefined) assert(v >= s.minimum, `${location}: minimum`);
      if (s.maximum !== undefined) assert(v <= s.maximum, `${location}: maximum`);
    }
    if (typeof v === 'string') {
      if (s.minLength !== undefined) assert(v.length >= s.minLength, `${location}: minLength`);
      if (s.maxLength !== undefined) assert(v.length <= s.maxLength, `${location}: maxLength`);
      if (s.pattern) assert(new RegExp(s.pattern).test(v), `${location}: pattern ${s.pattern}`);
      if (s.format === 'date-time') assert(Number.isFinite(Date.parse(v)), `${location}: date-time`);
    }
    if (Array.isArray(v)) {
      if (s.maxItems !== undefined) assert(v.length <= s.maxItems, `${location}: maxItems`);
      if (s.items) v.forEach((item, i) => check(item, s.items, file, `${location}[${i}]`));
    } else if (v !== null && typeof v === 'object') {
      for (const key of s.required || []) assert(Object.hasOwn(v, key), `${location}: missing ${key}`);
      for (const [key, item] of Object.entries(v)) {
        if (s.properties && Object.hasOwn(s.properties, key)) check(item, s.properties[key], file, `${location}.${key}`);
        else if (s.additionalProperties === false) assert.fail(`${location}: unexpected ${key}`);
      }
    }
  }
  check(value, read(schemaPath), schemaPath);
}

async function run() {
  fs.mkdirSync(output, { recursive: true });
  assert(fs.existsSync(bundle), 'Build web/EditHere.html before running this test.');
  const browser = await chromium.launch({ executablePath, headless: true, args: ['--mute-audio'] });
  const context = await browser.newContext({ viewport: { width: 1480, height: 1000 }, offline: true, acceptDownloads: true });
  const httpRequests = [], errors = [], passed = [];
  context.on('request', request => { if (/^https?:/i.test(request.url())) httpRequests.push(request.url()); });
  context.on('page', p => p.on('pageerror', error => errors.push(String(error))));
  const page = await context.newPage();
  page.setDefaultTimeout(12000);
  page.on('dialog', dialog => dialog.accept());
  let current = 'load';
  async function checkpoint(label, action) {
    current = label;
    await action();
    passed.push(label);
    console.log(`PASS ${label}`);
  }
  const visible = (id, targetPage = page) => targetPage.locator(id).waitFor({ state: 'visible' });
  async function ensureGlobal(targetPage = page) {
    if (await targetPage.locator('body').getAttribute('data-inspector-open') !== 'true') await targetPage.locator('#inspect-toggle').click();
  }
  async function ensureUtility(targetPage = page) {
    const toggle = targetPage.locator('#utility-toggle');
    if (await toggle.isVisible() && await toggle.getAttribute('aria-expanded') !== 'true') await toggle.click();
  }
  async function note(text, targetPage = page) {
    await visible('#note-dialog', targetPage);
    await targetPage.locator('#note-text').fill(text);
    await targetPage.locator('#note-form [type="submit"]').click();
    await targetPage.locator('#note-dialog').waitFor({ state: 'hidden' });
  }
  async function coords(x, y, targetPage = page) {
    const b = await targetPage.locator('#stage').boundingBox();
    const size = await targetPage.locator('#canvas').evaluate(c => ({ w: c.width, h: c.height }));
    return { x: b.x + x * b.width / size.w, y: b.y + y * b.height / size.h };
  }
  async function clickAt(x, y, targetPage = page) { const p = await coords(x, y, targetPage); await targetPage.mouse.click(p.x, p.y); }
  async function drag(x1, y1, x2, y2, targetPage = page) {
    const a = await coords(x1, y1, targetPage), b = await coords(x2, y2, targetPage);
    await targetPage.mouse.move(a.x, a.y); await targetPage.mouse.down();
    await targetPage.mouse.move(b.x, b.y, { steps: 10 }); await targetPage.mouse.up();
  }
  async function clickMovementArrow() {
    const points = await page.locator('path[data-movement]').filter({ visible: true }).first().evaluate(element => {
      const matrix = element.getScreenCTM(), id = element.dataset.movement;
      // Hover redraws the SVG paths. Click actual screen coordinates on the
      // line, away from its middle badge, instead of retaining an SVG handle.
      return [.2, .8, .1, .9].map(fraction => {
        const local = element.getPointAtLength(element.getTotalLength() * fraction);
        const screen = local.matrixTransform(matrix), hit = document.elementFromPoint(screen.x, screen.y);
        return { x: screen.x, y: screen.y, reachable: hit?.closest('[data-movement]')?.dataset.movement === id && !hit.closest('[data-note]') };
      });
    });
    const target = points.find(point => point.reachable);
    assert(target, 'the movement line must be reachable by an ordinary pointer: ' + JSON.stringify(points));
    await page.mouse.move(target.x, target.y);
    assert(await page.evaluate(({ x, y }) => !!document.elementFromPoint(x, y)?.closest('[data-movement]'), target),
      'hover must leave the movement line accessible');
    await page.mouse.click(target.x, target.y);
  }
  async function dragSvgHandle(targetPage, selector, dx, dy) {
    const at = await targetPage.locator(selector).evaluate(element => {
      const box = element.getBBox(), local = new DOMPoint(box.x + box.width / 2, box.y + box.height / 2);
      const screen = local.matrixTransform(element.getScreenCTM());
      return { x: screen.x, y: screen.y };
    });
    const scale = await targetPage.locator('#stage').evaluate(stage => stage.getBoundingClientRect().width / document.querySelector('#canvas').width);
    await targetPage.mouse.move(at.x, at.y); await targetPage.mouse.down();
    await targetPage.mouse.move(at.x + dx * scale, at.y + dy * scale, { steps: 10 }); await targetPage.mouse.up();
  }
  function stableFeedback(data) {
    if(Object.hasOwn(data,'exportedAt'))data.exportedAt='2026-10-16T00:00:00.000Z';
    for (const frame of data.frames || []) if(Object.hasOwn(frame.feedback,'exportedAt'))frame.feedback.exportedAt='2026-10-16T00:00:00.000Z';
    return data;
  }
  async function readFeedback(targetPage = page) {
    await targetPage.evaluate(()=>document.querySelector('#image-mode').value='embedded');
    await targetPage.locator('#export').click();
    const data = await readExportJson(targetPage);
    await targetPage.locator('[data-close="export-dialog"]').click();
    return stableFeedback(data);
  }
  async function downloadBy(selector, name) {
    if (selector === '#save-project') await ensureUtility();
    if (selector === '#preview') await page.locator('#export').click();
    const pending = page.waitForEvent('download');
    await page.locator(selector).click();
    const download = await pending;
    const filename = path.join(output, name || download.suggestedFilename());
    await download.saveAs(filename);
    assert.equal(await download.failure(), null);
    if (selector === '#preview') await page.locator('[data-close="export-dialog"]').click();
    return filename;
  }
  async function savedProject(name) {
    const filename = await downloadBy('#save-project', name);
    return { filename, data: JSON.parse(fs.readFileSync(filename, 'utf8')) };
  }
  async function geometry() {
    if (await page.locator('body').getAttribute('data-inspector-open') !== 'true') await page.locator('#inspect-toggle').click();
    if (await page.locator('#geometry').isHidden()) await page.locator('#geometry-toggle').click();
    if (!await page.locator('#geometry-details').evaluate(details => details.open)) await page.locator('#geometry-details summary').click();
    return Promise.all(['gx', 'gy', 'gw', 'gh'].map(id => page.locator('#' + id).inputValue())).then(v => v.map(Number));
  }
  async function seek(seconds) {
    const presented = await page.locator('#timeline').evaluate(async (input, value) => {
      const video = document.querySelector('#video');
      const frame = video.requestVideoFrameCallback ? new Promise(resolve => {
        const timer = setTimeout(() => resolve(null), 700);
        video.requestVideoFrameCallback((_now, info) => { clearTimeout(timer); resolve(info.mediaTime); });
      }) : Promise.resolve(null);
      input.value = value;
      input.dispatchEvent(new Event('input', { bubbles: true }));
      return frame;
    }, seconds);
    await page.waitForFunction(target => { const v = document.querySelector('#video'); return !v.seeking && v.readyState >= 2 && Math.abs(v.currentTime - target) < .02; }, seconds);
    const image = await page.locator('#video').evaluate(video => {
      const c = document.createElement('canvas'); c.width = video.videoWidth; c.height = video.videoHeight;
      c.getContext('2d').drawImage(video, 0, 0); return c.toDataURL('image/png');
    });
    return { presented, image };
  }
  try {
    await checkpoint('file:// opens with an offline context', async () => {
      await page.goto(pathToFileURL(bundle).href); await page.evaluate(()=>{const el=document.querySelector('#image-mode');el.value='embedded';});
      await visible('#empty');
      assert.equal(await page.title(), 'EditHere · 改这里');
      await page.screenshot({ path: path.join(output, '01-empty.png'), fullPage: true });
    });
    await checkpoint('demo loads at its original resolution', async () => {
      await page.locator('#demo').click(); await visible('#workspace');
      await page.waitForFunction(() => document.querySelector('#canvas').width === 1100);
      assert.match(await page.locator('#media-meta').textContent(), /1100 × 700/);
    });
    let originalFeedback;
    await checkpoint('point, rectangle and global annotations preserve text and coordinates', async () => {
      await page.locator('[data-mode="point"]').click(); await clickAt(60, 65); await note('点批注：修改标题');
      await page.locator('[data-mode="rect"]').click(); await drag(750, 380, 1000, 480); await note('框批注：图表应更清楚');
      await ensureGlobal(); await page.locator('#global').click(); await note('全局意见：保留轻快风格');
      assert.equal(await page.locator('#count').textContent(), '3');
      originalFeedback = await readFeedback();
      validateSchema(originalFeedback, path.join(root, 'schema/feedback-minimal.schema.json'));
      assert.equal(originalFeedback.objects.length, 3);
      assert.deepEqual(originalFeedback.objects[0].source, { x1: 60, y1: 65, x2: 60, y2: 65 });
      assert.deepEqual(originalFeedback.objects[1].source, { x1: 750, y1: 380, x2: 1000, y2: 480 });
      assert.equal(originalFeedback.objects[2].source, null);
      assert.match(originalFeedback.image.dataUrl, /^data:image\/png;base64,/);
    });
    await checkpoint('annotated PNG downloads at original resolution', async () => {
      const filename = await downloadBy('#preview', 'image-annotations.png'), bytes = fs.readFileSync(filename);
      assert.equal(bytes.subarray(1, 4).toString(), 'PNG');
      assert.equal(bytes.readUInt32BE(16), 1100); assert.equal(bytes.readUInt32BE(20), 700);
    });
    let draggedFeedback, resizedFeedback;
    await checkpoint('big bang manually cuts and drags a component', async () => {
      await page.locator('#explode').click(); await visible('[data-mode="cut"]');
      await page.locator('[data-mode="cut"]').click(); await drag(245, 170, 490, 320);
      assert(await page.locator('#geometry').isHidden(), 'selection must leave the layout popup collapsed');
      assert.deepEqual(await geometry(), [245, 170, 245, 150]);
      await drag(365, 260, 405, 300);
      assert.deepEqual(await geometry(), [285, 210, 245, 150]);
      draggedFeedback = await readFeedback();
      assert(draggedFeedback.objects.some(o => o.movements.length), 'drag must export movement objects');
    });
    await checkpoint('component resize, undo and redo restore exact feedback', async () => {
      await geometry();
      await page.locator('#lock-ratio').uncheck();
      await page.locator('#gw').fill('220'); await page.locator('#gh').fill('135');
      await page.locator('#apply-geometry').click();
      assert.deepEqual(await geometry(), [285, 210, 220, 135]);
      resizedFeedback = await readFeedback();
      assert.notDeepEqual(resizedFeedback, draggedFeedback);
      await page.locator('#undo').click(); assert.deepEqual(await readFeedback(), draggedFeedback);
      await page.locator('#redo').click(); assert.deepEqual(await readFeedback(), resizedFeedback);
      validateSchema(resizedFeedback, path.join(root, 'schema/feedback-minimal.schema.json'));
      await page.screenshot({ path: path.join(output, '02-image-edited.png'), fullPage: true });
    });
    let movementText;
    await checkpoint('clicking a movement arrow adds and edits text on that same movement object', async () => {
      const original = resizedFeedback;
      const moved = original.objects.find(object => object.movements.length);
      assert(moved, 'resize must retain a movement to annotate');
      const arrow = page.locator('path[data-movement]').filter({ visible: true }).first();
      await arrow.waitFor({ state: 'visible' });
      assert.deepEqual(JSON.parse(await arrow.getAttribute('data-source')), moved.source);
      assert.deepEqual(JSON.parse(await arrow.getAttribute('data-destination')), moved.movements.at(-1).to);
      await clickMovementArrow();
      await note('位移意见：按钮往右下调整，保持文字清晰');
      const added = await readFeedback();
      const sameMovement = data => data.objects.find(object =>
        JSON.stringify(object.source) === JSON.stringify(moved.source)
        && JSON.stringify(object.movements) === JSON.stringify(moved.movements));
      assert(sameMovement(added), 'the annotated arrow must preserve its original source and destination');
      assert.deepEqual(sameMovement(added).annotations.map(annotation=>annotation.text), ['位移意见：按钮往右下调整，保持文字清晰']);
      assert.equal(added.objects.length, original.objects.length, 'arrow text belongs to the movement, rather than an extra located object');
      validateSchema(added, path.join(root, 'schema/feedback-minimal.schema.json'));
      await clickMovementArrow();
      await visible('#note-dialog');
      assert.equal(await page.locator('#note-text').inputValue(), '位移意见：按钮往右下调整，保持文字清晰');
      movementText = '位移意见已修改：保留这个目标位置，加大字重';
      await note(movementText);
      resizedFeedback = await readFeedback();
      assert.deepEqual(sameMovement(resizedFeedback).annotations.map(annotation=>annotation.text), [movementText]);
      await page.locator('#undo').click(); assert.deepEqual(await readFeedback(), added);
      await page.locator('#redo').click(); assert.deepEqual(await readFeedback(), resizedFeedback);
      await page.locator('#undo').click(); await page.locator('#undo').click();
      assert.deepEqual(await readFeedback(), original, 'undo must remove the arrow text while retaining the layout change');
      await page.locator('#redo').click(); await page.locator('#redo').click();
      assert.deepEqual(await readFeedback(), resizedFeedback);
      await page.screenshot({ path: path.join(output, '08-movement-with-text.png'), fullPage: true });
    });
    await checkpoint('annotated PNG includes movement arrows as well as badges', async () => {
      const filename = await downloadBy('#preview', 'image-movement-annotations.png');
      const bytes = fs.readFileSync(filename);
      assert.equal(bytes.readUInt32BE(16), 1100); assert.equal(bytes.readUInt32BE(20), 700);
      const painted = await page.evaluate(async encoded => {
        const image = new Image(); image.src = 'data:image/png;base64,' + encoded; await image.decode();
        const exported = document.createElement('canvas'); exported.width = image.width; exported.height = image.height;
        exported.getContext('2d').drawImage(image, 0, 0);
        const base = document.querySelector('#canvas').getContext('2d').getImageData(0, 0, image.width, image.height).data;
        const actual = exported.getContext('2d').getImageData(0, 0, image.width, image.height).data;
        const trajectory = document.querySelector('path[data-movement]');
        const length = trajectory.getTotalLength(), pixels = new Set(), bluePixels = new Set();
        // Ignore the middle badge: the exported line itself must be painted.
        for (let fraction = .08; fraction <= .92; fraction += .02) {
          if (fraction >= .32 && fraction <= .68) continue;
          const point = trajectory.getPointAtLength(length * fraction);
          for (let dy = -3; dy <= 3; dy++) for (let dx = -3; dx <= 3; dx++) {
            const x = Math.round(point.x + dx), y = Math.round(point.y + dy);
            if (x < 0 || y < 0 || x >= image.width || y >= image.height) continue;
            const index = (y * image.width + x) * 4;
            if ([0, 1, 2, 3].some(channel => Math.abs(base[index + channel] - actual[index + channel]) > 30)) pixels.add(index);
            if (Math.abs(actual[index] - 52) < 28 && Math.abs(actual[index + 1] - 109) < 28 && Math.abs(actual[index + 2] - 241) < 18 && Math.abs(base[index] - actual[index]) > 30) bluePixels.add(index);
          }
        }
        return { changedPixels: pixels.size, bluePixels: bluePixels.size, trajectoryLength: length };
      }, bytes.toString('base64'));
      assert(painted.trajectoryLength > 5, JSON.stringify(painted));
      assert(painted.changedPixels > 12, 'movement line is absent from the downloaded PNG: ' + JSON.stringify(painted));
      assert(painted.bluePixels > 6, 'the downloaded movement line must use the blue accent: ' + JSON.stringify(painted));
    });
    await checkpoint('image JSON download and project round trip retain annotations and layout', async () => {
      await page.locator('#export').click();
      const filename = await downloadBy('#download-json', 'image-feedback.json');
      await page.locator('[data-close="export-dialog"]').click();
      assert.deepEqual(stableFeedback(JSON.parse(fs.readFileSync(filename, 'utf8'))), resizedFeedback);
      const saved = await savedProject('image-project.json');
      assert.equal(saved.data.document.notes.length, 4); assert(saved.data.document.layout.pieces.length);
      await page.locator('#file-input').setInputFiles(saved.filename);
      await page.waitForFunction(expected => document.querySelector('#count').textContent === String(expected), resizedFeedback.objects.length);
      assert.deepEqual(await readFeedback(), resizedFeedback);
      assert.match(await page.locator('#notes').textContent(), new RegExp(movementText));
      await clickMovementArrow();
      await visible('#note-dialog');
      assert.equal(await page.locator('#note-text').inputValue(), movementText, 'restored movement arrows must retain editable text');
      await page.locator('[data-close="note-dialog"]').first().click();
      const again = await savedProject('image-project-restored.json');
      assert.deepEqual(again.data.document, saved.data.document);
    });
    await checkpoint('unbounded pan, cursor-centered zoom, actual size and mark visibility preserve feedback', async () => {
      const before = await readFeedback();
      await page.locator('#fit').click();
      await page.locator('[data-mode="pan"]').click();
      const initial = await page.locator('#stage').boundingBox();
      const at = await coords(110, 390);
      await page.mouse.move(at.x, at.y); await page.mouse.down();
      await page.mouse.move(at.x + 85, at.y - 45, { steps: 8 }); await page.mouse.up();
      const panned = await page.locator('#stage').boundingBox();
      assert(Math.abs(panned.x - initial.x - 85) < 1, JSON.stringify({ initial, panned }));
      assert(Math.abs(panned.y - initial.y + 45) < 1, JSON.stringify({ initial, panned }));
      assert.equal(panned.width, initial.width); assert.equal(panned.height, initial.height);
      const anchor = await coords(100, 100);
      await page.mouse.move(anchor.x, anchor.y);
      await page.keyboard.down('Control'); await page.mouse.wheel(0, -180); await page.keyboard.up('Control');
      const enlarged = await page.locator('#stage').boundingBox();
      assert(enlarged.width > panned.width, 'Ctrl + wheel should enlarge the view');
      assert(Math.abs((anchor.x - enlarged.x) / enlarged.width * 1100 - 100) < 1, 'zoom must retain the image pixel under the pointer');
      assert(Math.abs((anchor.y - enlarged.y) / enlarged.height * 700 - 100) < 1, 'zoom must retain the image pixel under the pointer');
      assert.equal(await page.locator('#actual-size').count(), 0);
      await page.keyboard.press('1');
      const actual = await page.locator('#stage').boundingBox();
      assert(Math.abs(actual.width - 1100) < 1 && Math.abs(actual.height - 700) < 1, JSON.stringify(actual));
      await page.locator('#fit').click();
      assert(await page.locator('#pins .pin').count() > 0);
      await page.locator('#toggle-annotations').click();
      assert.equal(await page.locator('#pins .pin').count(), 0);
      assert.equal(await page.locator('path[data-movement]').count(), 0);
      assert.match(await page.locator('#notes').textContent(), new RegExp(movementText));
      await page.locator('#toggle-annotations').click();
      assert(await page.locator('path[data-movement]').count() > 0);
      assert.deepEqual(await readFeedback(), before, 'view changes must not create or modify feedback objects');
    });
    await checkpoint('selection mode moves and resizes ordinary annotations while preserving text and undo history', async () => {
      const editor = await context.newPage(); editor.on('dialog', dialog => dialog.accept());
      try {
        await editor.goto(pathToFileURL(bundle).href);
        const pieces = layout.create(1100, 700);
        layout.addRegion(pieces, { x1: 520, y1: 170, x2: 765, y2: 320 });
        const fixture = { schemaVersion: 'edithere-web-project-1', tool: 'EditHere Web', kind: 'image', name: 'annotation-adjustment.png', width: 1100, height: 700,
          document: { id: 'annotation-adjustment', timestampUs: null, image: originalFeedback.image.dataUrl, layout: pieces, notes: [
            { id: 'point-to-adjust', source: { x1: 50, y1: 70, x2: 50, y2: 70 }, text: 'point to adjust' },
            { id: 'rectangle-to-adjust', source: { x1: 250, y1: 180, x2: 450, y2: 300 }, text: 'rectangle to adjust' }
          ] } };
        await editor.locator('#file-input').setInputFiles({ name: 'annotation-adjustment.edithere-web.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(fixture)) });
        await editor.waitForFunction(() => document.querySelector('#count').textContent === '2');
        await editor.locator('[data-mode="select"]').click();
        await clickAt(640, 240, editor);
        if (await editor.locator('body').getAttribute('data-inspector-open') !== 'true') await editor.locator('#inspect-toggle').click();
        await editor.locator('#geometry-toggle').click();
        await editor.locator('#lock-ratio').uncheck();
        await drag(350, 240, 390, 270, editor);
        let changed = await readFeedback(editor);
        assert.deepEqual(changed.objects.find(object => object.annotations.some(annotation => annotation.text === 'rectangle to adjust')).source, { x1: 290, y1: 210, x2: 490, y2: 330 });
        await editor.locator('[data-note-handle="se"]').waitFor({ state: 'visible' });
        await dragSvgHandle(editor, '[data-note-handle="se"]', 30, 20);
        changed = await readFeedback(editor);
        assert.deepEqual(changed.objects.find(object => object.annotations.some(annotation => annotation.text === 'rectangle to adjust')).source, { x1: 290, y1: 210, x2: 520, y2: 350 });
        const beforePoint = changed;
        await drag(50, 70, 85, 95, editor);
        assert.equal(await editor.locator('#note-dialog').isVisible(), false, 'dragging a point should adjust its location without opening text editing');
        changed = await readFeedback(editor);
        assert.deepEqual(changed.objects.find(object => object.annotations.some(annotation => annotation.text === 'point to adjust')).source, { x1: 85, y1: 95, x2: 85, y2: 95 });
        assert.equal(changed.objects.length, 2); assert(changed.objects.every(object => object.movements.length === 0));
        validateSchema(changed, path.join(root, 'schema/feedback-minimal.schema.json'));
        await editor.locator('#undo').click(); assert.deepEqual(await readFeedback(editor), beforePoint);
        await editor.locator('#redo').click(); assert.deepEqual(await readFeedback(editor), changed);
        await editor.screenshot({ path: path.join(output, '09-annotations-adjusted.png'), fullPage: true });
      } catch (error) {
        await editor.screenshot({ path: path.join(output, '09-annotations-adjustment-failure.png'), fullPage: true }).catch(() => {});
        throw error;
      } finally { await editor.close(); }
    });
    await checkpoint('multiple components transform together and selected-component wheel scaling leaves the view and other components unchanged', async () => {
      const editor = await context.newPage(); editor.on('dialog', dialog => dialog.accept());
      try {
        await editor.goto(pathToFileURL(bundle).href);
        const pieces = layout.create(1100, 700);
        layout.addRegion(pieces, { x1: 245, y1: 170, x2: 490, y2: 320 });
        layout.addRegion(pieces, { x1: 520, y1: 170, x2: 765, y2: 320 });
        const fixture = { schemaVersion: 'edithere-web-project-1', tool: 'EditHere Web', kind: 'image', name: 'multi-selection.png', width: 1100, height: 700,
          document: { id: 'multi-selection', timestampUs: null, image: originalFeedback.image.dataUrl, notes: [], layout: pieces } };
        await editor.locator('#file-input').setInputFiles({ name: 'multi-selection.edithere-web.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(fixture)) });
        await editor.locator('#workspace').waitFor({ state: 'visible' });
        await editor.locator('[data-mode="select"]').click();
        if (await editor.locator('body').getAttribute('data-inspector-open') !== 'true') await editor.locator('#inspect-toggle').click();
        await clickAt(365, 240, editor);
        await editor.keyboard.down('Control'); await clickAt(640, 240, editor); await editor.keyboard.up('Control');
        assert.equal(Number(await editor.locator('#gw').inputValue()), 520, 'multi-selection geometry must describe the combined bounds');
        await drag(365, 240, 400, 270, editor);
        const translated = await readFeedback(editor);
        const objectFor = (feedback, x) => feedback.objects.find(object => object.source.x1 === x && object.source.y1 === 170);
        assert.deepEqual(objectFor(translated, 245).movements, [{ to: { x1: 280, y1: 200, x2: 525, y2: 350 } }]);
        assert.deepEqual(objectFor(translated, 520).movements, [{ to: { x1: 555, y1: 200, x2: 800, y2: 350 } }]);
        if (await editor.locator('#geometry').isHidden()) await editor.locator('#geometry-toggle').click();
        if (!await editor.locator('#geometry-details').evaluate(details => details.open)) await editor.locator('#geometry-details summary').click();
        await editor.locator('#lock-ratio').uncheck();
        if (await editor.locator('body').getAttribute('data-inspector-open') !== 'true') await editor.locator('#inspect-toggle').click();
        await editor.locator('#gw').fill('572'); await editor.locator('#gh').fill('165'); await editor.locator('#apply-geometry').click();
        const scaled = await readFeedback(editor), first = objectFor(scaled, 245).movements[0].to, second = objectFor(scaled, 520).movements[0].to;
        const close = (actual, expected) => assert(Math.abs(actual - expected) < .01, `${actual} != ${expected}`);
        close(first.x1, 280); close(first.y1, 200); close(first.x2, 549.5); close(first.y2, 365);
        close(second.x1, 582.5); close(second.y1, 200); close(second.x2, 852); close(second.y2, 365);
        validateSchema(scaled, path.join(root, 'schema/feedback-minimal.schema.json'));
        await editor.locator('#undo').click(); assert.deepEqual(await readFeedback(editor), translated);
        await editor.locator('#redo').click(); assert.deepEqual(await readFeedback(editor), scaled);
        await clickAt(300, 220, editor);
        const beforeView = await editor.locator('#stage').boundingBox(), wheelAt = await coords(300, 220, editor);
        await editor.mouse.move(wheelAt.x, wheelAt.y); await editor.mouse.wheel(0, -100);
        const wheelScaled = await readFeedback(editor), resized = objectFor(wheelScaled, 245).movements[0].to;
        assert(resized.x2 - resized.x1 > first.x2 - first.x1, 'ordinary wheel should scale the selected component');
        close((resized.x2 - resized.x1) / (first.x2 - first.x1), (resized.y2 - resized.y1) / (first.y2 - first.y1));
        assert.deepEqual(objectFor(wheelScaled, 520), objectFor(scaled, 520), 'scaling the first component must leave its former co-selection unchanged');
        const afterView = await editor.locator('#stage').boundingBox(); close(afterView.width, beforeView.width); close(afterView.height, beforeView.height);
        await editor.locator('#undo').click(); assert.deepEqual(await readFeedback(editor), scaled);
        await editor.screenshot({ path: path.join(output, '10-multi-selection-transformed.png'), fullPage: true });
      } catch (error) {
        await editor.screenshot({ path: path.join(output, '10-multi-selection-failure.png'), fullPage: true }).catch(() => {});
        throw error;
      } finally { await editor.close(); }
    });
    await checkpoint('local video import, play, pause and seeks keep zero unannotated frames', async () => {
      await page.locator('#file-input').setInputFiles(videoFixture); await visible('#video-controls');
      await page.waitForFunction(() => document.querySelector('#video').readyState >= 2 && !document.querySelector('#play').disabled);
      assert.equal(await page.locator('#frame-list button').count(), 0);
      await page.locator('#play').click();
      await page.waitForFunction(() => !document.querySelector('#video').paused && document.querySelector('#video').currentTime > .1);
      await page.locator('#play').click();
      await page.waitForFunction(() => document.querySelector('#video').paused);
      for (const second of [.5, 1, 1.5]) await seek(second);
      assert.equal(await page.locator('#frame-list button').count(), 0);
      const saved = await savedProject('video-unannotated.json'); assert.deepEqual(saved.data.frames, []);
    });
    let videoFeedback, videoProject;
    await checkpoint('two timestamp annotations export only two frames and a matching index', async () => {
      const first = await seek(.5);
      await page.locator('[data-mode="point"]').click(); await clickAt(60, 65); await note('视频第一帧：调整标题');
      assert.equal(await page.locator('#frame-list button').count(), 1);
      const second = await seek(1.5); await ensureGlobal(); await page.locator('#global').click(); await note('视频第二帧：转场慢一点');
      assert.equal(await page.locator('#frame-list button').count(), 2);
      videoFeedback = await readFeedback();
      validateSchema(videoFeedback, path.join(root, 'schema/video-feedback-v1.schema.json'));
      assert.equal(videoFeedback.frames.length, 2); assert.equal(videoFeedback.objects.length, 2);
      assert(videoFeedback.frames[0].timestampUs >= 400000 && videoFeedback.frames[0].timestampUs <= 600000);
      assert(videoFeedback.frames[1].timestampUs >= 1400000 && videoFeedback.frames[1].timestampUs <= 1600000);
      for (const [i, captured] of [first, second].entries()) {
        assert.equal(videoFeedback.frames[i].feedback.image.dataUrl, captured.image, 'exported frame must match the paused video pixels');
        if (captured.presented !== null) assert.equal(videoFeedback.frames[i].timestampUs, Math.round(captured.presented * 1000000), 'timestamp must use the presented media frame');
      }
      for (const frame of videoFeedback.frames) {
        assert.equal(frame.timestampMs, Math.floor(frame.timestampUs / 1000));
        assert.match(frame.feedback.image.dataUrl, /^data:image\/png;base64,/);
        assert(videoFeedback.objects.some(o => o.frameId === frame.id && o.timestampMs === frame.timestampMs));
      }
      await page.locator('#export').click();
      const filename = await downloadBy('#download-json', 'video-feedback.json');
      await page.locator('[data-close="export-dialog"]').click();
      const downloaded = JSON.parse(fs.readFileSync(filename, 'utf8')); validateSchema(downloaded, path.join(root, 'schema/video-feedback-v1.schema.json'));
      assert.deepEqual(stableFeedback(downloaded).frames, videoFeedback.frames);
      videoProject = await savedProject('video-project.json');
      assert.equal(videoProject.data.frames.length, 2); assert(!Object.hasOwn(videoProject.data, 'videoData'));
      await page.screenshot({ path: path.join(output, '03-video-annotated.png'), fullPage: true });
    });
    await checkpoint('video project restores without source video, then relinks and plays', async () => {
      await page.locator('#file-input').setInputFiles(videoProject.filename);
      await page.waitForFunction(() => document.body.dataset.busy !== 'true' && document.querySelector('#play').disabled && document.querySelector('#frame-list').children.length === 2);
      assert(await page.locator('#canvas').isVisible());
      assert.equal(await page.locator('#video').getAttribute('src'), null);
      await page.locator('#frame-list button').nth(1).click();
      assert.match(await page.locator('#notes').textContent(), /视频第二帧/);
      const restored = await readFeedback(); assert.deepEqual(restored.frames, videoFeedback.frames);
      await page.locator('#video-input').setInputFiles(videoFixture);
      await page.waitForFunction(() => !document.querySelector('#play').disabled);
      await page.waitForFunction(() => document.querySelector('#toast').textContent === '源视频已关联，可以继续播放和批注');
      assert.match(await page.locator('#notes').textContent(), /视频第二帧/);
      await page.locator('#play').click(); await page.waitForFunction(() => !document.querySelector('#video').paused);
      const currentTime = await page.locator('#video').evaluate(v => v.currentTime);
      assert(Math.abs(currentTime - videoFeedback.frames[1].timestampUs / 1000000) < .5, 'playing restored frame must resume at that frame');
      await page.locator('#play').click(); await page.waitForFunction(() => document.querySelector('#video').paused);
      assert.equal(await page.locator('#frame-list button').count(), 2);
      await page.screenshot({ path: path.join(output, '04-video-restored.png'), fullPage: true });
    });
    await checkpoint('feedback import preserves both sources when one destination overlaps the next source', async () => {
      const image = await page.evaluate(() => {
        const c = document.createElement('canvas'); c.width = 120; c.height = 40;
        const p = c.getContext('2d');
        for (const [i, color] of ['#ff0000', '#00ff00', '#0000ff', '#ffffff'].entries()) {
          p.fillStyle = color; p.fillRect(i * 30, 0, 30, 40);
        }
        return c.toDataURL('image/png');
      });
      const feedback = { annotationSpace: 'result', image, objects: [
        { source: { x1: 0, y1: 0, x2: 30, y2: 40 }, movements: [{ to: { x1: 30, y1: 0, x2: 60, y2: 40 } }], annotations: ['A 红色组件'] },
        { source: { x1: 30, y1: 0, x2: 60, y2: 40 }, movements: [{ to: { x1: 60, y1: 0, x2: 90, y2: 40 } }], annotations: ['B 绿色组件'] }
      ] };
      await page.locator('#file-input').setInputFiles({ name: 'overlap-feedback.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(feedback)) });
      await page.waitForFunction(() => document.querySelector('#canvas').width === 120 && document.querySelector('#notes').textContent.includes('B 绿色组件'));
      const pixels = await page.locator('#canvas').evaluate(canvas => {
        const c = canvas.getContext('2d');
        return [Array.from(c.getImageData(45, 20, 1, 1).data), Array.from(c.getImageData(75, 20, 1, 1).data)];
      });
      assert.deepEqual(pixels, [[255, 0, 0, 255], [0, 255, 0, 255]]);
      const exported = await readFeedback();
      validateSchema(exported, path.join(root, 'schema/feedback-minimal.schema.json'));
      assert.equal(exported.schemaVersion, 'feedback-minimal-2');
      assert.equal(exported.tool.name, 'EditHere');
      const legacyObjects=objects=>objects.map(({id,...object})=>({...object,annotations:object.annotations.map(annotation=>annotation.text)}));
      assert.deepEqual({annotationSpace:exported.annotationSpace,image:exported.image.dataUrl,objects:legacyObjects(exported.objects)}, feedback);
      const project = await savedProject('overlap-project.json');
      await page.locator('#file-input').setInputFiles(project.filename);
      await page.waitForFunction(() => document.querySelector('#notes').textContent.includes('B 绿色组件'));
      const restored = await readFeedback();
      assert.deepEqual({annotationSpace:restored.annotationSpace,image:restored.image.dataUrl,objects:legacyObjects(restored.objects)}, feedback);
    });
    await checkpoint('distributed offline HTML independently reopens, edits and restores a project', async () => {
      assert.equal(await page.locator('#offline').count(), 0);
      const saved = await savedProject('offline-reopen-project.json');
      const expected = await readFeedback();
      const fresh = await context.newPage(); fresh.on('dialog', dialog => dialog.accept()); await fresh.goto(pathToFileURL(bundle).href);
      assert.equal(await fresh.locator('#offline').count(), 0);
      assert.equal(await fresh.locator('#actual-size').count(), 0);
      assert(await fresh.locator('#empty').isVisible());
      const base64 = fs.readFileSync(path.join(output, 'image-annotations.png')).toString('base64');
      await fresh.evaluate(encoded => {
        const bytes = Uint8Array.from(atob(encoded), c => c.charCodeAt(0));
        const dataTransfer = new DataTransfer();
        dataTransfer.items.add(new File([bytes], 'local-dropped-image.png', { type: 'image/png' }));
        document.dispatchEvent(new DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer }));
      }, base64);
      await fresh.locator('#workspace').waitFor({ state: 'visible' });
      assert.equal(await fresh.locator('#media-name').textContent(), 'local-dropped-image.png');
      await ensureGlobal(fresh); await fresh.locator('#global').click(); await fresh.locator('#note-text').fill('离线文件里的新批注');
      await fresh.locator('#note-form [type="submit"]').click();
      assert.equal(await fresh.locator('#count').textContent(), '1');
      await fresh.locator('#file-input').setInputFiles(saved.filename);
      await fresh.waitForFunction(() => document.body.dataset.busy !== 'true');
      assert.deepEqual(await readFeedback(fresh), expected);
      await fresh.screenshot({ path: path.join(output, '05-offline-project-restored.png'), fullPage: true });
      await fresh.close();
    });
    await checkpoint('390px mobile home has no horizontal overflow', async () => {
      const mobile = await context.newPage();
      await mobile.setViewportSize({ width: 390, height: 844 });
      await mobile.goto(pathToFileURL(bundle).href);
      await mobile.locator('#empty').waitFor({ state: 'visible' });
      const dimensions = await mobile.evaluate(() => ({ viewport: innerWidth, content: document.documentElement.scrollWidth }));
      assert(dimensions.content <= dimensions.viewport + 1, JSON.stringify(dimensions));
      await mobile.screenshot({ path: path.join(output, '06-mobile-390.png'), fullPage: true });
      await mobile.close();
    });
    const viewportReports = [];
    await checkpoint('workspace stays usable in seven wide, square, Agent-panel, phone and short viewports', async () => {
      const sizes = [[1920, 1080], [1280, 800], [900, 900], [560, 900], [390, 844], [844, 390], [1400, 500]];
      for (const [width, height] of sizes) {
        const workspace = await context.newPage();
        await workspace.setViewportSize({ width, height });
        workspace.on('dialog', dialog => dialog.accept());
        current = `workspace ${width}x${height}`;
        try {
          await workspace.goto(pathToFileURL(bundle).href);
          await workspace.locator('#demo').click(); await workspace.locator('#workspace').waitFor({ state: 'visible' });
          await workspace.waitForFunction(() => document.querySelector('#canvas').width === 1100);
          const item = { width, height, layouts: [] };
          async function inspectLayout(label) {
            await workspace.locator('#fit').click();
            const measured = await workspace.evaluate(() => {
              const rectangle = element => {
                const r = element.getBoundingClientRect();
                return { x: r.x, y: r.y, width: r.width, height: r.height, right: r.right, bottom: r.bottom };
              };
              const rendered = element => {
                if (!element || element.hidden) return false;
                const style = getComputedStyle(element), r = element.getBoundingClientRect();
                return style.display !== 'none' && style.visibility !== 'hidden' && Number(style.opacity) !== 0 && r.width > 0 && r.height > 0;
              };
              const stage = rectangle(document.querySelector('#stage'));
              const layers = ['.tools', '.sidebar', '.media-bar', '.view-controls', '.status-bar', '.utility-dock', '#video-controls'].map(selector => {
                const element = document.querySelector(selector);
                return rendered(element) ? { selector, ...rectangle(element) } : null;
              }).filter(Boolean);
              const selectors = ['#open', '#save-json', '#export', '#fit', '#inspect-toggle'];
              if (!document.querySelector('#video-controls').hidden) selectors.push('#play', '#timeline', '#relink');
              if (document.body.dataset.inspectorOpen === 'true') selectors.push('#panel-close');
              const buttons = selectors.map(selector => {
                const element = document.querySelector(selector), r = element.getBoundingClientRect();
                const hit = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
                return { selector, visible: rendered(element), disabled: element.disabled, reachable: hit === element || element.contains(hit), ...rectangle(element) };
              });
              return { inspector: document.body.dataset.inspectorOpen, stage, viewport: rectangle(document.querySelector('#viewport')),
                content: { htmlWidth: document.documentElement.scrollWidth, htmlHeight: document.documentElement.scrollHeight,
                  bodyWidth: document.body.scrollWidth, bodyHeight: document.body.scrollHeight, width: innerWidth, height: innerHeight }, layers, buttons };
            });
            const description = `${width}x${height} ${label}: ${JSON.stringify(measured)}`;
            assert(measured.content.htmlWidth <= width + 1 && measured.content.bodyWidth <= width + 1, 'horizontal page overflow: ' + description);
            assert(measured.content.htmlHeight <= height + 1 && measured.content.bodyHeight <= height + 1, 'vertical page overflow: ' + description);
            const { stage, viewport } = measured;
            assert(stage.width > 40 && stage.height > 25, 'fit must leave an editable image: ' + description);
            assert(stage.x >= viewport.x - 1 && stage.y >= viewport.y - 1 && stage.right <= viewport.right + 1 && stage.bottom <= viewport.bottom + 1,
              'fit must keep the image inside the workspace: ' + description);
            for (const layer of measured.layers) {
              const overlapWidth = Math.min(stage.right, layer.right) - Math.max(stage.x, layer.x);
              const overlapHeight = Math.min(stage.bottom, layer.bottom) - Math.max(stage.y, layer.y);
              assert(overlapWidth <= 1 || overlapHeight <= 1, 'fit hides image under ' + layer.selector + ': ' + description);
            }
            for (let i = 0; i < measured.layers.length; i++) for (let j = i + 1; j < measured.layers.length; j++) {
              const a = measured.layers[i], b = measured.layers[j];
              assert(Math.min(a.right, b.right) - Math.max(a.x, b.x) <= 1 || Math.min(a.bottom, b.bottom) - Math.max(a.y, b.y) <= 1,
                'floating panels overlap (' + a.selector + ', ' + b.selector + '): ' + description);
            }
            for (const button of measured.buttons) {
              assert(button.visible && !button.disabled && button.reachable, 'unusable core button: ' + description);
              assert(button.x >= -1 && button.y >= -1 && button.right <= width + 1 && button.bottom <= height + 1,
                'core button outside the viewport: ' + description);
            }
            item.layouts.push({ label, ...measured });
            await workspace.screenshot({ path: path.join(output, `workspace-${width}x${height}-${label}.png`), fullPage: true });
          }
          await inspectLayout('default');
          await workspace.locator('#inspect-toggle').click();
          await inspectLayout('inspector-toggled');
          await ensureGlobal(workspace); await workspace.locator('#global').click(); await workspace.locator('#note-dialog').waitFor({ state: 'visible' });
          await workspace.locator('#note-text').fill(`视口 ${width} × ${height}：工具和批注都可操作`);
          await workspace.locator('#note-form [type="submit"]').click();
          await workspace.locator('#note-dialog').waitFor({ state: 'hidden' });
          await workspace.locator('#export').click();
          const feedback = await readExportJson(workspace);
          assert.equal(feedback.objects[0].annotations[0].text, `视口 ${width} × ${height}：工具和批注都可操作`);
          await workspace.locator('[data-close="export-dialog"]').click();
          // Video controls add another floating layer, so check the same fit budget.
          await workspace.locator('#file-input').setInputFiles(videoFixture);
          await workspace.locator('#video-controls').waitFor({ state: 'visible' });
          await workspace.waitForFunction(() => document.querySelector('#video').readyState >= 2);
          await inspectLayout('video');
          async function exerciseVideoControls() {
            await workspace.locator('#play').click();
            await workspace.waitForFunction(() => !document.querySelector('#video').paused);
            await workspace.locator('#play').click();
            await workspace.waitForFunction(() => document.querySelector('#video').paused);
            const timeline = await workspace.locator('#timeline').boundingBox();
            await workspace.locator('#timeline').click({ position: { x: timeline.width * .6, y: timeline.height / 2 } });
            await workspace.waitForFunction(() => { const video = document.querySelector('#video'); return !video.seeking && video.currentTime > .05; });
            const [chooser] = await Promise.all([workspace.waitForEvent('filechooser'), workspace.locator('#relink').click()]);
            await chooser.setFiles(videoFixture);
            await workspace.waitForFunction(() => document.querySelector('#video').readyState >= 2 && !document.querySelector('#play').disabled);
          }
          await exerciseVideoControls();
          await workspace.locator('#inspect-toggle').click();
          await inspectLayout('video-inspector-toggled');
          await exerciseVideoControls();
          if (await workspace.locator('body').getAttribute('data-inspector-open') !== 'true') await workspace.locator('#inspect-toggle').click();
          await workspace.locator('#panel-close').click();
          assert.equal(await workspace.locator('body').getAttribute('data-inspector-open'), 'false', 'the video inspector close action must be reachable');
          viewportReports.push(item);
        } catch (error) {
          await workspace.screenshot({ path: path.join(output, `workspace-${width}x${height}-failure.png`), fullPage: true }).catch(() => {});
          throw error;
        } finally {
          await workspace.close();
        }
      }
      fs.writeFileSync(path.join(output, 'workspace-viewport-report.json'), JSON.stringify(viewportReports, null, 2));
    });
    const materialRatioReports = [];
    await checkpoint('square, portrait, ultrawide and long images fit wide and Agent-panel workspaces without distortion', async () => {
      const materials = [[800, 800, '1-1'], [450, 800, '9-16'], [840, 360, '21-9'], [320, 1280, '1-4']];
      for (const [width, height] of [[1280, 800], [560, 900]]) {
        const workspace = await context.newPage();
        await workspace.setViewportSize({ width, height });
        workspace.on('dialog', dialog => dialog.accept());
        try {
          await workspace.goto(pathToFileURL(bundle).href);
          for (const [imageWidth, imageHeight, ratio] of materials) {
            current = `material ${ratio} in ${width}x${height}`;
            const data = await workspace.evaluate(([w, h]) => {
              const image = document.createElement('canvas'); image.width = w; image.height = h;
              const paint = image.getContext('2d'); paint.fillStyle = '#e8edf7'; paint.fillRect(0, 0, w, h);
              paint.fillStyle = '#346df1'; paint.fillRect(w * .15, h * .15, w * .7, h * .7);
              return image.toDataURL('image/png').split(',')[1];
            }, [imageWidth, imageHeight]);
            await workspace.locator('#file-input').setInputFiles({ name: `material-${ratio}.png`, mimeType: 'image/png', buffer: Buffer.from(data, 'base64') });
            await workspace.waitForFunction(([w, h]) => {
              const canvas = document.querySelector('#canvas'); return canvas.width === w && canvas.height === h;
            }, [imageWidth, imageHeight]);
            await workspace.locator('#fit').click();
            const measured = await workspace.evaluate(() => {
              const box = element => { const r = element.getBoundingClientRect(); return { x: r.x, y: r.y, right: r.right, bottom: r.bottom, width: r.width, height: r.height }; };
              const layers = ['.header', '.media-bar', '.view-controls', '.tools', '.status-bar', '.sidebar', '.utility-dock'].map(selector => {
                const element = document.querySelector(selector); if (!element || element.hidden) return null;
                const style = getComputedStyle(element), r = box(element);
                return style.display !== 'none' && style.visibility !== 'hidden' && Number(style.opacity) !== 0 && r.width > 0 && r.height > 0 ? { selector, ...r } : null;
              }).filter(Boolean);
              return { stage: box(document.querySelector('#stage')), viewport: box(document.querySelector('#viewport')), layers,
                pageWidth: Math.max(document.body.scrollWidth, document.documentElement.scrollWidth), pageHeight: Math.max(document.body.scrollHeight, document.documentElement.scrollHeight) };
            });
            const label = `${width}x${height}, material ${ratio}: ${JSON.stringify(measured)}`, { stage, viewport } = measured;
            assert(measured.pageWidth <= width + 1 && measured.pageHeight <= height + 1, 'material causes page overflow: ' + label);
            assert(Math.abs(stage.width / stage.height - imageWidth / imageHeight) < .001, 'fit distorts the source image aspect ratio: ' + label);
            assert(stage.width > 40 && stage.height > 25, 'fit leaves too little material to edit: ' + label);
            assert(stage.x >= viewport.x - 1 && stage.y >= viewport.y - 1 && stage.right <= viewport.right + 1 && stage.bottom <= viewport.bottom + 1,
              'material lies outside its workspace: ' + label);
            for (const layer of measured.layers) {
              assert(Math.min(stage.right, layer.right) - Math.max(stage.x, layer.x) <= 1 || Math.min(stage.bottom, layer.bottom) - Math.max(stage.y, layer.y) <= 1,
                'material is hidden under ' + layer.selector + ': ' + label);
            }
            for (const selector of ['#open', '#save-json', '#export', '#fit', '#inspect-toggle']) {
              assert(await workspace.locator(selector).isVisible() && await workspace.locator(selector).isEnabled(), 'material makes a core action unavailable: ' + label);
            }
            await workspace.locator('#export').click();
            const feedback = await readExportJson(workspace);
            assert.deepEqual(feedback.objects, []); assert.equal(feedback.image.mode,'file'); assert.match(feedback.image.sha256,/^[a-f0-9]{64}$/);
            await workspace.locator('[data-close="export-dialog"]').click();
            await workspace.screenshot({ path: path.join(output, `material-${ratio}-${width}x${height}.png`), fullPage: true });
            materialRatioReports.push({ width, height, imageWidth, imageHeight, ratio, ...measured });
          }
        } catch (error) {
          await workspace.screenshot({ path: path.join(output, `material-${width}x${height}-failure.png`), fullPage: true }).catch(() => {});
          throw error;
        } finally { await workspace.close(); }
      }
      fs.writeFileSync(path.join(output, 'material-ratio-report.json'), JSON.stringify(materialRatioReports, null, 2));
    });
    await checkpoint('HTTP-loaded page keeps editing and exporting after its server closes and context goes offline', async () => {
      const server = http.createServer((_req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); res.end(fs.readFileSync(bundle));
      });
      await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
      const online = await browser.newContext({ viewport: { width: 1480, height: 1000 }, acceptDownloads: true });
      const postLoadRequests = [], onlineErrors = [];
      let loaded = false, closed = false;
      online.on('request', request => { if (loaded && /^https?:/i.test(request.url())) postLoadRequests.push(request.url()); });
      const web = await online.newPage(); web.on('pageerror', error => onlineErrors.push(String(error)));
      try {
        await web.goto(`http://127.0.0.1:${server.address().port}/EditHere.html`);
        await web.locator('#empty').waitFor({ state: 'visible' });
        await online.setOffline(true); loaded = true;
        await new Promise(resolve => server.close(resolve)); closed = true;
        await web.locator('#demo').click(); await web.locator('#workspace').waitFor({ state: 'visible' });
        await ensureGlobal(web); await web.locator('#global').click(); await web.locator('#note-text').fill('网页获取后断网本地处理');
        await web.locator('#note-form [type="submit"]').click();
        await web.locator('#export').click();
        const feedback = await readExportJson(web);
        validateSchema(feedback, path.join(root, 'schema/feedback-minimal.schema.json'));
        assert.equal(feedback.objects[0].annotations[0].text, '网页获取后断网本地处理');
        const pending = web.waitForEvent('download'); await web.locator('#download-json').click();
        const downloaded = await pending; await downloaded.saveAs(path.join(output, 'http-then-offline-feedback.json'));
        assert.equal(await downloaded.failure(), null);
        assert.deepEqual(postLoadRequests, []); assert.deepEqual(onlineErrors, []);
        await web.screenshot({ path: path.join(output, '07-http-then-offline.png'), fullPage: true });
      } finally {
        await online.close();
        if (!closed) await new Promise(resolve => server.close(resolve));
      }
    });
    await checkpoint('all editing performs no HTTP requests and raises no runtime errors', async () => {
      assert.deepEqual(httpRequests, []); assert.deepEqual(errors, []);
    });
    const report = { passed, fixture: videoFixture, browser: executablePath, httpRequests, errors, viewports: viewportReports.map(item => ({ width: item.width, height: item.height, checkedStates: item.layouts.length })),
      materialRatios: materialRatioReports.map(item => ({ width: item.width, height: item.height, ratio: item.ratio })) };
    fs.writeFileSync(path.join(output, 'browser-report.json'), JSON.stringify(report, null, 2));
    console.log(`Browser acceptance: ${passed.length} checks passed. Evidence: ${output}`);
  } catch (error) {
    await page.screenshot({ path: path.join(output, 'failure.png'), fullPage: true }).catch(() => {});
    const evidence = { failed: current, passed, error: error.stack, httpRequests, errors,
      toast: await page.locator('#toast').textContent().catch(() => ''),
      meta: await page.locator('#media-meta').textContent().catch(() => ''),
      notes: await page.locator('#notes').textContent().catch(() => ''),
      video: await page.locator('#video').evaluate(v => ({ paused: v.paused, seeking: v.seeking, currentTime: v.currentTime, duration: v.duration, readyState: v.readyState, source: v.getAttribute('src') })).catch(() => null) };
    fs.writeFileSync(path.join(output, 'browser-report.json'), JSON.stringify(evidence, null, 2));
    throw error;
  } finally {
    await context.close(); await browser.close();
  }
}

run().catch(error => { console.error(error.stack); process.exitCode = 1; });
