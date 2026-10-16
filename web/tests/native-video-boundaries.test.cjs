'use strict';
// Native video-feedback permits an empty 0×0 project, unknown duration, and
// sub-millisecond timestamps throughout the final declared millisecond.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { downloadText, readExportJson } = require('./export-test-utils.cjs');
const root = path.resolve(__dirname, '../..');
const output = path.join(__dirname, 'output/native-video');
const first = values => values.filter(Boolean).find(value => fs.existsSync(value));
const modulePath = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core'), path.join(root, 'node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe']);
const videoFixture = first([process.env.EDITHERE_TEST_VIDEO, path.resolve(root, '../EditHere宣传/短片-视频修改/out/EditHere-视频改一帧-zh-clean.mp4')]);
assert(modulePath && executablePath && videoFixture, 'Set the EDITHERE_* test dependency and video fixture overrides.');
const { chromium } = require(modulePath);

async function main() {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ executablePath, headless: true });
  const context = await browser.newContext({ offline: true, acceptDownloads: true, viewport: { width: 1100, height: 800 } });
  const page = await context.newPage(), errors = [], requests = [], results = [];
  page.on('pageerror', error => errors.push(String(error))); page.on('dialog', dialog => dialog.accept());
  context.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
  async function checkpoint(name, action) { await action(); results.push(name); console.log('PASS ' + name); }
  async function importValue(value) {
    await page.locator('#file-input').setInputFiles({ name: 'native-video-feedback.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(value)) });
    await page.waitForFunction(() => document.body.dataset.busy !== 'true');
  }
  async function exported() { await page.locator('#export').click(); const value = await readExportJson(page); await page.keyboard.press('Escape'); for(const frame of value.frames)assert.equal(frame.feedback.schemaVersion,'feedback-minimal-2'); return value; }
  async function savedProject() { return JSON.parse(await downloadText(page, '#save-project')); }
  const metadata = value => {
    const stable = JSON.parse(JSON.stringify(value)); delete stable.exportedAt;
    for(const frame of stable.frames){delete frame.feedback.$schema;delete frame.feedback.schemaVersion;delete frame.feedback.tool;delete frame.feedback.exportedAt;}
    return stable;
  };
  let current = 'load';
  try {
    await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href);
    const empty = { schemaVersion: 'video-feedback-1', tool: 'EditHere', exportedAt: '2026-10-11T00:00:00Z', video: { source: path.basename(videoFixture), durationMs: 0, width: 0, height: 0 }, frames: [], objects: [] };
    current = 'empty video';
    await checkpoint('native empty 0×0 video feedback imports, exports and survives a saved web project round trip', async () => {
      await importValue(empty); await page.locator('#workspace').waitFor({ state: 'visible' });
      assert.equal(await page.locator('#play').isDisabled(), true); assert.equal(await page.locator('#frame-list button').count(), 0);
      const canvas = await page.locator('#canvas').evaluate(canvas => ({ width: canvas.width, height: canvas.height })); assert(canvas.width > 0 && canvas.height > 0, 'empty feedback needs a usable viewport even before source dimensions are known');
      assert.deepEqual(metadata(await exported()), metadata(empty));
      const project = await savedProject(); assert.equal(project.video.width, 0); assert.equal(project.video.height, 0); assert.deepEqual(project.frames, []);
      await importValue(project); assert.deepEqual(metadata(await exported()), metadata(empty));
      fs.writeFileSync(path.join(output, 'empty-native-video-project.json'), JSON.stringify(project, null, 2));
    });
    current = 'empty source relink';
    await checkpoint('linking an empty feedback project adopts real video dimensions and supports new frame annotations', async () => {
      await page.locator('#video-input').setInputFiles(videoFixture);
      await page.waitForFunction(() => document.body.dataset.busy !== 'true' && !document.querySelector('#play').disabled && document.querySelector('#video').readyState >= 2);
      const video = await page.locator('#video').evaluate(video => ({ width: video.videoWidth, height: video.videoHeight, durationMs: Math.round(video.duration * 1000) }));
      const project = await savedProject(); assert.equal(project.width, video.width); assert.equal(project.height, video.height); assert.equal(project.video.width, video.width); assert.equal(project.video.height, video.height); assert.equal(project.video.durationMs, video.durationMs);
      await page.locator('#play').click(); await page.waitForFunction(() => !document.querySelector('#video').paused && document.querySelector('#video').currentTime > .05); await page.locator('#play').click();
      if (await page.locator('body').getAttribute('data-inspector-open') !== 'true') await page.locator('#inspect-toggle').click();
      await page.locator('#global').click(); await page.locator('#note-text').fill('空视频反馈重新关联后仍可批注'); await page.locator('#note-form [type="submit"]').click(); await page.locator('#note-dialog').waitFor({ state: 'hidden' });
      const data = await exported(); assert.equal(data.frames.length, 1); assert.equal(data.video.width, video.width); assert.equal(data.video.height, video.height); assert.deepEqual(data.frames[0].feedback.objects[0].annotations, ['空视频反馈重新关联后仍可批注']);
      await page.screenshot({ path: path.join(output, 'empty-video-linked-and-annotated.png') });
    });
    const image = await page.evaluate(() => { const canvas = document.createElement('canvas'); canvas.width = 200; canvas.height = 100; canvas.getContext('2d').fillRect(0, 0, 200, 100); return canvas.toDataURL('image/png'); });
    function feedbackAt(timestampUs, durationMs) {
      const id = 't-' + timestampUs, timestampMs = Math.floor(timestampUs / 1000), object = { source: null, movements: [], annotations: ['保留原生微秒时间边界'] };
      return { schemaVersion: 'video-feedback-1', tool: 'EditHere', exportedAt: '2026-10-11T00:00:00Z', video: { source: 'native-time.mp4', durationMs, width: 200, height: 100 }, frames: [{ id, timestampUs, timestampMs, imageFile: 'frame-' + id + '.png', feedback: { annotationSpace: 'result', image, objects: [object] } }], objects: [{ ...object, timestampMs, frameId: id }] };
    }
    current = 'last millisecond';
    await checkpoint('timestampUs 12540999 is valid for durationMs 12540 through feedback and project reload', async () => {
      const expected = feedbackAt(12540999, 12540); await importValue(expected); assert.deepEqual(metadata(await exported()), metadata(expected));
      const project = await savedProject(); await importValue(project); assert.deepEqual(metadata(await exported()), metadata(expected));
    });
    current = 'unknown duration';
    await checkpoint('durationMs 0 means unknown and accepts an annotated frame at a nonzero timestamp', async () => {
      const expected = feedbackAt(12540999, 0); await importValue(expected); assert.deepEqual(metadata(await exported()), metadata(expected));
      const project = await savedProject(); await importValue(project); assert.deepEqual(metadata(await exported()), metadata(expected));
    });
    current = 'next millisecond rejection';
    await checkpoint('timestampUs 12541000 exceeds the final millisecond and rejection preserves the current project', async () => {
      const retained = metadata(await exported()), pixels = await page.locator('#canvas').evaluate(canvas => canvas.toDataURL());
      await importValue(feedbackAt(12541000, 12540)); assert.deepEqual(metadata(await exported()), retained); assert.equal(await page.locator('#canvas').evaluate(canvas => canvas.toDataURL()), pixels);
      const invalid = await savedProject(); invalid.video.durationMs = 12540; invalid.frames[0].timestampUs = 12541000;
      await importValue(invalid); assert.deepEqual(metadata(await exported()), retained); assert.equal(await page.locator('#canvas').evaluate(canvas => canvas.toDataURL()), pixels);
    });
    assert.deepEqual(errors, []); assert.deepEqual(requests, []);
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ results, errors, requests }, null, 2)); console.log('Native video boundaries: ' + results.length + ' checks passed.');
  } catch (error) {
    await page.screenshot({ path: path.join(output, 'failure.png') }).catch(() => {}); fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ failed: current, results, error: error.stack, errors, requests }, null, 2)); throw error;
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
