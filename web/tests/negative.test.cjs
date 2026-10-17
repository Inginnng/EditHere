'use strict';
// Real offline browser inputs, cancellation and retained-work checks.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const L = require('../src/layout.js');
const { readExportJson } = require('./export-test-utils.cjs');
const root = path.resolve(__dirname, '../..');
const output = path.join(__dirname, 'output/negative');
const first = values => values.filter(Boolean).find(value => fs.existsSync(value));
const modulePath = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core'), path.join(root, 'node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe']);
assert(modulePath && executablePath, 'Set EDITHERE_PLAYWRIGHT_MODULE and EDITHERE_BROWSER_PATH.');
const { chromium } = require(modulePath);
const clone = value => JSON.parse(JSON.stringify(value));
const rect = (x1, y1, x2, y2) => ({ x1, y1, x2, y2 });

async function main() {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ executablePath, headless: true });
  const context = await browser.newContext({ offline: true, acceptDownloads: true, viewport: { width: 1280, height: 900 } });
  const page = await context.newPage();
  const errors = [], requests = [], results = [];
  page.on('pageerror', error => errors.push(String(error)));
  context.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
  let acceptReplacement = true;
  page.on('dialog', dialog => acceptReplacement ? dialog.accept() : dialog.dismiss());
  async function checkpoint(name, fn) {
    try { await fn(); results.push({ name, passed: true }); console.log('PASS ' + name); }
    catch (error) { results.push({ name, passed: false, error: String(error) }); console.log('FAIL ' + name + ': ' + error.message.slice(0, 230)); if (await page.locator('dialog[open]').count()) await page.keyboard.press('Escape'); }
  }
  async function importValue(value, name = 'input.edithere-web.json', mimeType = 'application/json') {
    await page.locator('#file-input').setInputFiles({ name, mimeType, buffer: Buffer.from(typeof value === 'string' ? value : JSON.stringify(value)) });
    await page.waitForFunction(() => document.body.dataset.busy !== 'true');
  }
  async function feedback() {
    await page.locator('#export').click();
    const value = await readExportJson(page);
    await page.locator('[data-close="export-dialog"]').first().click();
    delete value.exportedAt;
    for (const frame of value.frames || []) delete frame.feedback.exportedAt;
    return value;
  }
  async function snapshot() {
    return { feedback: await feedback(), pixels: await page.locator('#canvas').evaluate(canvas => canvas.toDataURL()), name: await page.locator('#media-name').textContent(), undo: await page.locator('#undo').isEnabled(), redo: await page.locator('#redo').isEnabled() };
  }
  async function ensureGlobal() {
    if (await page.locator('body').getAttribute('data-inspector-open') !== 'true') await page.locator('#inspect-toggle').click();
  }
  async function addNote(text) {
    await ensureGlobal();
    await page.locator('#global').click();
    await page.locator('#note-text').fill(text);
    await page.locator('#note-form [type="submit"]').click();
    await page.locator('#note-dialog').waitFor({ state: 'hidden' });
  }
  async function coords(x, y) {
    return page.locator('#stage').evaluate((stage, p) => { const b = stage.getBoundingClientRect(), c = document.querySelector('#canvas'); return { x: b.x + p.x * b.width / c.width, y: b.y + p.y * b.height / c.height }; }, { x, y });
  }
  try {
    await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href); await page.evaluate(()=>{const el=document.querySelector('#image-mode');el.value='embedded';});
    const image = await page.evaluate(() => { const c = document.createElement('canvas'); c.width = 200; c.height = 100; const p = c.getContext('2d'); p.fillStyle = '#edf1f8'; p.fillRect(0, 0, 200, 100); p.fillStyle = '#df513f'; p.fillRect(10, 10, 30, 30); return c.toDataURL('image/png'); });
    const layout = L.create(200, 100), source = rect(10, 10, 40, 40), destination = rect(90, 20, 120, 50);
    const groupId = L.addRegion(layout, source); L.transform(layout, groupId, destination);
    layout.movements = [{ groupId, source, destination, sources: [source] }];
    const malicious = '<img src=x onerror="globalThis.injected=true"> & <script>throw 1</script> 原样显示';
    const project = { schemaVersion: 'edithere-web-project-1', tool: 'EditHere Web', kind: 'image', name: 'retained.png', width: 200, height: 100, document: { id: 'image-fixture', timestampUs: null, image, layout, notes: [{ id: 'global-original', source: null, text: malicious }, { id: 'movement-original', source: destination, text: '保留位移意见', groupId, movementSource: source, movementSources: [source] }] } };
    const video = { source: 'fixture.mp4', durationMs: 1000, width: 200, height: 100 };
    const frame = { ...clone(project.document), id: 't-200000', timestampUs: 200000 };
    const videoProject = { ...clone(project), kind: 'video', video, frames: [frame] }; delete videoProject.document;
    const frameFeedback = { annotationSpace: 'result', image, objects: [{ source: null, movements: [], annotations: ['视频意见'] }] };
    const videoFeedback = { schemaVersion: 'video-feedback-1', tool: 'EditHere', exportedAt: new Date().toISOString(), video, frames: [{ id: 't-200000', timestampUs: 200000, timestampMs: 200, imageFile: 'frame-t-200000.png', feedback: frameFeedback }], objects: [] };
    async function restore() { await importValue(project, 'retained.edithere-web.json'); await addNote('导入失败后仍能撤销的真实编辑'); }
    await restore();
    await checkpoint('valid video project and feedback remain importable with accurate timestamps', async () => {
      for (const value of [videoProject, videoFeedback]) {
        await importValue(value); const data = await feedback(); assert.equal(data.schemaVersion, 'video-feedback-1'); assert.equal(data.frames.length, 1); assert.equal(data.frames[0].timestampUs, 200000); assert.equal(data.frames[0].feedback.image.dataUrl, image); assert.equal(await page.locator('#play').isDisabled(), true);
      }
      await importValue({ ...clone(videoProject), frames: [] }); const empty = await feedback(); assert.equal(empty.frames.length, 0); assert.equal(empty.objects.length, 0);
      await restore();
    });
    await checkpoint('HTML-like annotation text remains literal and never executes', async () => {
      assert((await page.locator('#notes').textContent()).includes(malicious));
      assert.equal(await page.locator('#notes img, #notes script').count(), 0);
      assert.equal(await page.evaluate(() => globalThis.injected), undefined);
    });
    const invalid = [];
    function invalidProject(name, mutate) { const value = clone(project); mutate(value); invalid.push([name, value]); }
    function invalidVideo(name, mutate, isFeedback = false) { const value = clone(isFeedback ? videoFeedback : videoProject); mutate(value); invalid.push([name, value]); }
    invalid.push(['malformed JSON', '{broken JSON']);
    invalidProject('zero image dimensions', value => value.width = 0);
    invalidProject('oversized image dimensions', value => { value.width = 32767; value.height = 32767; });
    invalidProject('embedded image dimension mismatch', value => value.width = 201);
    invalidProject('remote image reference', value => value.document.image = 'https://example.invalid/image.png');
    invalidProject('corrupt embedded image', value => value.document.image = 'data:image/png;base64,YmFk');
    invalidProject('duplicate annotation IDs', value => value.document.notes.push(clone(value.document.notes[0])));
    invalidProject('whitespace-only annotation', value => value.document.notes[0].text = '  \n ');
    invalidProject('out-of-bounds annotation', value => value.document.notes[0].source = rect(0, 0, 201, 20));
    invalidProject('invalid point on image boundary', value => value.document.notes[0].source = rect(200, 100, 200, 100));
    invalidProject('missing component reference', value => value.document.notes[1].groupId = 'missing');
    invalidProject('duplicate multi-selection references', value => { const note = value.document.notes[1]; delete note.groupId; delete note.movementSource; delete note.movementSources; note.groupIds = [groupId, groupId]; });
    invalidProject('layout missing original pixels', value => value.document.layout.pieces.pop());
    invalidProject('layout reuses original pixels', value => value.document.layout.pieces.push({ ...clone(value.document.layout.pieces[0]), id: 'duplicate-pixels' }));
    invalidProject('dangling movement component', value => value.document.layout.movements[0].groupId = 'missing');
    invalidProject('arrow destination disagrees with actual pixels', value => value.document.layout.movements[0].destination = rect(120, 20, 150, 50));
    invalidProject('arrow source disagrees with component members', value => { value.document.layout.movements[0].source = rect(0, 0, 40, 40); delete value.document.layout.movements[0].sources; });
    invalidProject('duplicate original movement slices', value => value.document.layout.movements[0].sources.push(clone(source)));
    invalidProject('movement comment disagrees with tracked component', value => value.document.notes[1].movementSource = rect(0, 0, 40, 40));
    invalidVideo('negative video duration', value => value.video.durationMs = -1);
    invalidVideo('fractional video duration', value => value.video.durationMs = 1.5);
    invalidVideo('blank video source', value => value.video.source = '  ');
    invalidVideo('video dimensions disagree with project', value => value.video.width = 201);
    invalidVideo('duplicate video frame IDs', value => value.frames.push(clone(value.frames[0])));
    invalidVideo('duplicate video timestamps with distinct IDs', value => value.frames.push({ ...clone(value.frames[0]), id: 'other-id' }));
    invalidVideo('video frame beyond final millisecond', value => value.frames[0].timestampUs = 1001000);
    invalidVideo('feedback missing duration', value => delete value.video.durationMs, true);
    invalidVideo('feedback blank source', value => value.video.source = '', true);
    invalidVideo('feedback duplicate frame IDs', value => value.frames.push(clone(value.frames[0])), true);
    invalidVideo('feedback duplicate frame timestamps', value => value.frames.push({ ...clone(value.frames[0]), id: 'other-id' }), true);
    invalidVideo('feedback frame beyond final millisecond', value => { value.frames[0].timestampUs = 1001000; value.frames[0].timestampMs = 1001; }, true);
    invalidVideo('feedback fractional duration', value => value.video.durationMs = 1000.5, true);
    invalidVideo('feedback oversized frame ID', value => value.frames[0].id = 'a'.repeat(257), true);
    for (const [name, value] of invalid) {
      await restore(); const before = await snapshot();
      await checkpoint('reject ' + name + ' without losing pixels, text or history', async () => {
        await importValue(value);
        assert.deepEqual(await snapshot(), before, 'Rejected input must leave the complete editing session unchanged');
        assert.equal(await page.locator('#toast').isVisible(), true, 'explain the rejected input');
        assert.equal(await page.evaluate(() => document.body.dataset.busy), undefined);
      });
    }
    await restore();
    await checkpoint('unsupported file and failed image/video decoding preserve the current edit', async () => {
      for (const [name, mime] of [['bad.txt', 'text/plain'], ['bad.png', 'image/png'], ['bad.mp4', 'video/mp4']]) {
        const before = await snapshot(); await importValue('not an image or video', name, mime); assert.deepEqual(await snapshot(), before);
      }
    });
    await checkpoint('cancel replacing dirty work retains the image and undo history', async () => {
      const before = await snapshot(); acceptReplacement = false;
      try { await importValue(videoProject); } finally { acceptReplacement = true; }
      assert.deepEqual(await snapshot(), before);
    });
    await checkpoint('cancel new/edit notes and refuse blank submission without changing feedback', async () => {
      const before = await snapshot(); await ensureGlobal(); await page.locator('#global').click(); await page.locator('#note-text').fill('取消的新意见'); await page.keyboard.press('Escape'); assert.deepEqual(await snapshot(), before);
      await page.locator('[data-edit="global-original"]').click(); await page.locator('#note-text').fill(' '); await page.locator('#note-form [type="submit"]').click(); assert.equal(await page.locator('#note-dialog').isVisible(), true); await page.keyboard.press('Escape'); assert.deepEqual(await snapshot(), before);
    });
    await checkpoint('input shortcuts type normally and never move components or switch tools', async () => {
      const before = await snapshot(); await ensureGlobal(); await page.locator('#global').click(); await page.locator('#note-text').fill('bpvcr'); await page.keyboard.press('ArrowLeft'); await page.keyboard.press('Backspace'); assert.equal(await page.locator('[data-mode="smart"]').getAttribute('aria-pressed'), 'true'); await page.keyboard.press('Escape'); assert.deepEqual(await snapshot(), before);
    });
    await checkpoint('escape during a component drag restores exact pixels and feedback', async () => {
      await page.locator('[data-mode="select"]').click(); const start = await coords(115, 47); await page.mouse.click(start.x, start.y); const before = await snapshot();
      await page.mouse.move(start.x, start.y); await page.mouse.down(); await page.mouse.move(start.x + 32, start.y + 17, { steps: 6 }); await page.keyboard.press('Escape'); await page.mouse.up(); assert.deepEqual(await snapshot(), before);
    });
    await checkpoint('empty/zero/negative component dimensions are rejected without changing layout', async () => {
      const start = await coords(115, 47); await page.mouse.click(start.x, start.y); const before = await snapshot();
      await ensureGlobal();
      for (const [id, value] of [['gx', ''], ['gw', '0'], ['gh', '-1']]) { if (await page.locator('#geometry').isHidden()) await page.locator('#geometry-toggle').click(); await page.locator('#' + id).fill(value); await page.locator('#apply-geometry').click(); assert.deepEqual(await snapshot(), before); }
    });
    await restore();
    await checkpoint('undo and redo still work after repeated failed imports', async () => {
      await importValue('{bad'); const before = await feedback(); await page.locator('#undo').click(); const undone = await feedback(); assert.notDeepEqual(undone, before); assert(!JSON.stringify(undone.objects).includes('真实编辑')); await page.locator('#redo').click(); assert.deepEqual(await feedback(), before);
    });
    await checkpoint('shortcuts during a delayed failed import cannot mutate the retained session', async () => {
      const before = await snapshot(); const invalidImage = clone(project); invalidImage.document.image = 'data:image/png;base64,ZGVsYXllZC1mYWlsdXJl';
      await page.locator('#stage').focus();
      await page.evaluate(() => {
        const original = HTMLImageElement.prototype.decode;
        HTMLImageElement.prototype.decode = function () {
          if (this.src === 'data:image/png;base64,ZGVsYXllZC1mYWlsdXJl') return new Promise((_resolve, reject) => { globalThis.releaseDecodeFailure = () => { HTMLImageElement.prototype.decode = original; delete globalThis.releaseDecodeFailure; reject(new Error('测试延迟解码失败')); }; });
          return original.call(this);
        };
      });
      try {
        await page.locator('#file-input').setInputFiles({ name: 'delayed.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(invalidImage)) });
        await page.waitForFunction(() => !!globalThis.releaseDecodeFailure && document.body.dataset.busy === 'true');
        await page.keyboard.press('Control+z'); await page.keyboard.press('v'); await page.keyboard.press('Delete'); await page.keyboard.press('ArrowRight');
      } finally { await page.evaluate(() => globalThis.releaseDecodeFailure?.()); }
      await page.waitForFunction(() => document.body.dataset.busy !== 'true'); assert.deepEqual(await snapshot(), before);
    });
    await checkpoint('invalid video relink preserves saved frames and leaves playback disabled', async () => {
      await importValue(videoProject); const before = await snapshot();
      await page.locator('#video-input').setInputFiles({ name: 'broken.mp4', mimeType: 'video/mp4', buffer: Buffer.from('broken video') }); await page.waitForFunction(() => document.body.dataset.busy !== 'true'); assert.deepEqual(await snapshot(), before); assert(await page.locator('#play').isDisabled()); assert.equal(await page.locator('#frame-list button').count(), 1);
    });
    await checkpoint('invalid video duration or frame metadata never makes export unavailable', async () => { assert.equal((await feedback()).schemaVersion, 'video-feedback-1'); });
    assert.deepEqual(errors, [], 'no unhandled browser errors'); assert.deepEqual(requests, [], 'offline editor never attempts a network request');
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ browser: browser.version(), offline: true, results, errors, requests }, null, 2));
    const failed = results.filter(result => !result.passed); assert.equal(failed.length, 0, failed.length + ' negative acceptance cases failed; inspect output/negative/report.json');
    console.log('Negative acceptance: ' + results.length + ' cases passed.');
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
