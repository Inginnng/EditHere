'use strict';
// Browser regressions derived from tests/ui_test.cpp REG-110 and app/model.cpp.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const L = require('../src/layout.js');
const { downloadText, readExportJson } = require('./export-test-utils.cjs');
const root = path.resolve(__dirname, '../..');
const output = path.join(__dirname, 'output/latest-parity');
const first = values => values.filter(Boolean).find(value => fs.existsSync(value));
const modulePath = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core'), path.join(root, 'node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe']);
assert(modulePath && executablePath, 'Set EDITHERE_PLAYWRIGHT_MODULE and EDITHERE_BROWSER_PATH.');
const { chromium } = require(modulePath);
const rect = (x1, y1, x2, y2) => ({ x1, y1, x2, y2 });

async function main() {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ executablePath, headless: true });
  const context = await browser.newContext({ offline: true, acceptDownloads: true, viewport: { width: 1280, height: 900 } });
  const page = await context.newPage(), errors = [], requests = [], results = [];
  page.on('pageerror', error => errors.push(String(error)));
  page.on('dialog', dialog => dialog.accept());
  context.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
  async function checkpoint(name, action) { await action(); results.push(name); console.log('PASS ' + name); }
  async function importProject(project) {
    await page.locator('#file-input').setInputFiles({ name: 'native-order.edithere-web.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(project)) });
    await page.waitForFunction(() => document.body.dataset.busy !== 'true');
    await page.locator('#workspace').waitFor({ state: 'visible' });
  }
  async function savedProject() { return JSON.parse(await downloadText(page, '#save-project')); }
  async function ensurePanel() { if (await page.locator('body').getAttribute('data-inspector-open') !== 'true') await page.locator('#inspect-toggle').click(); }
  async function note(text) {
    await page.locator('#note-dialog').waitFor({ state: 'visible' }); await page.locator('#note-text').fill(text);
    await page.locator('#note-form [type="submit"]').click(); await page.locator('#note-dialog').waitFor({ state: 'hidden' });
  }
  async function globalNote(text) { await ensurePanel(); await page.locator('#global').click(); await note(text); }
  async function select(x, y) {
    const p = await page.locator('#stage').evaluate((stage, point) => { const b = stage.getBoundingClientRect(), c = document.querySelector('#canvas'); return { x: b.x + point.x * b.width / c.width, y: b.y + point.y * b.height / c.height }; }, { x, y });
    await page.locator('[data-mode="select"]').click(); await page.mouse.click(p.x, p.y); await ensurePanel();
    if (await page.locator('#geometry').isHidden()) await page.locator('#geometry-toggle').click();
    await page.locator('#geometry').waitFor({ state: 'visible' });
    if (!await page.locator('#geometry-details').evaluate(details => details.open)) await page.locator('#geometry-details summary').click();
  }
  async function move(x, y) { await page.locator('#gx').fill(String(x)); await page.locator('#gy').fill(String(y)); await page.locator('#apply-geometry').click(); }
  async function exported() { await page.locator('#export').click(); const value = await readExportJson(page); await page.locator('[data-close="export-dialog"]').click(); return value; }
  async function cards() { return page.locator('.note-card').evaluateAll(elements => elements.map(element => ({ id: element.dataset.note, number: element.querySelector('.note-number').textContent, text: element.querySelector('p').textContent, empty: element.classList.contains('movement-empty') }))); }
  let current = 'load';
  try {
    await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href);
    const image = await page.evaluate(() => { const c = document.createElement('canvas'); c.width = 400; c.height = 220; const p = c.getContext('2d'); p.fillStyle = '#edf1f8'; p.fillRect(0, 0, 400, 220); p.fillStyle = '#e74c3c'; p.fillRect(20, 30, 50, 40); p.fillStyle = '#34ad89'; p.fillRect(70, 30, 50, 40); return c.toDataURL('image/png'); });
    const layout = L.create(400, 220), left = rect(20, 30, 70, 70), right = rect(70, 30, 120, 70);
    L.addRegion(layout, left); L.addRegion(layout, right);
    const fixture = { schemaVersion: 'edithere-web-project-1', kind: 'image', name: 'native-order.png', width: 400, height: 220, document: { id: 'native-order', timestampUs: null, image, layout, notes: [] } };
    let emptyProject, textProject, movementId;
    current = 'chronology';
    await checkpoint('REG-110 global → empty movement → global preserves creation order and a numbered canvas pin', async () => {
      await importProject(fixture); await globalNote('移动前的批注'); await select(45, 50); await move(200, 100);
      const initial = await cards(); assert.equal(initial.length, 2); movementId = initial[1].id;
      assert.equal(initial[1].number, '2'); assert.equal(initial[1].empty, true); assert.equal(initial[1].text, '位置变化，点击可写下想法...');
      assert.equal(await page.locator(`.pin[data-note="${movementId}"]`).textContent(), '2');
      assert.equal(await page.locator('.movement-draft').count(), 0, 'new movements must be durable notes, rather than appended pseudo entries');
      await globalNote('移动后的批注'); const ordered = await cards();
      assert.deepEqual(ordered.map(card => card.number), ['1', '2', '3']); assert.equal(ordered[1].id, movementId); assert.equal(ordered[2].text, '移动后的批注');
      emptyProject = await savedProject(); assert.equal(emptyProject.document.notes[1].text, ''); assert.deepEqual(emptyProject.document.notes[1].movementSource, left);
      fs.writeFileSync(path.join(output, 'empty-movement-project.json'), JSON.stringify(emptyProject, null, 2));
      await page.screenshot({ path: path.join(output, 'chronological-empty-movement.png') });
    });
    current = 'edit/history';
    await checkpoint('adding or clearing movement text keeps the existing ID, number and undo/redo slot', async () => {
      await page.locator(`[data-edit="${movementId}"]`).click(); await note('移动的说明');
      let ordered = await cards(); assert.equal(ordered[1].id, movementId); assert.equal(ordered[1].number, '2'); assert.equal(ordered[1].text, '移动的说明'); assert.equal(ordered.length, 3);
      textProject = await savedProject();
      await page.locator('#undo').click(); ordered = await cards(); assert.equal(ordered[1].id, movementId); assert.equal(ordered[1].empty, true);
      await page.locator('#redo').click(); assert.equal((await cards())[1].text, '移动的说明');
      await page.locator(`[data-edit="${movementId}"]`).click(); await note(''); assert.equal((await cards())[1].empty, true);
      await page.locator('#undo').click(); assert.equal((await cards())[1].text, '移动的说明');
    });
    current = 'project reload';
    await checkpoint('saving and reopening an empty movement retains its real note, chronology and editable pin', async () => {
      await importProject(emptyProject); const ordered = await cards(); assert.equal(ordered.length, 3); assert.equal(ordered[1].id, movementId); assert.equal(ordered[1].number, '2'); assert.equal(ordered[1].empty, true);
      assert.equal(await page.locator(`.pin[data-note="${movementId}"]`).textContent(), '2');
      const saved = await savedProject(); assert.deepEqual(saved.document.notes, emptyProject.document.notes);
      const anchor = await page.locator(`.pin[data-note="${movementId}"]`).evaluate(pin => { const b = pin.getBoundingClientRect(); return { x: b.x + b.width / 2, y: b.y + b.height / 2 }; });
      await page.mouse.move(anchor.x, anchor.y);
      assert.equal(await page.evaluate(at => document.elementFromPoint(at.x, at.y)?.closest('.pin')?.dataset.note, anchor), movementId, 'the restored canvas pin must be reachable by an ordinary pointer');
      await page.mouse.click(anchor.x, anchor.y); assert.equal(await page.locator('#note-text').inputValue(), ''); await page.keyboard.press('Escape');
      const feedback = await exported(); const object = feedback.objects.find(object => object.source && Object.keys(left).every(key => object.source[key] === left[key])); assert.deepEqual(object.annotations, []); assert.equal(object.movements.length, 1);
    });
    current = 'empty reset';
    await checkpoint('returning an empty movement to its source removes its slot; undo/redo restores the same ID', async () => {
      await select(240, 130); await page.locator('#reset-component').click();
      assert.deepEqual((await cards()).map(card => card.text), ['移动前的批注', '移动后的批注']); assert.equal(await page.locator('path[data-movement]').count(), 0);
      assert.equal((await savedProject()).document.notes.some(note => note.id === movementId), false);
      await page.locator('#undo').click(); assert.equal((await cards())[1].id, movementId); assert.equal((await cards())[1].empty, true);
      await page.locator('#redo').click(); assert.equal((await cards()).length, 2);
    });
    current = 'text reset';
    await checkpoint('returning a commented movement preserves its ID and text as independent feedback', async () => {
      await importProject(textProject); await select(240, 130); await page.locator('#reset-component').click();
      const ordered = await cards(); assert.equal(ordered.length, 3); assert.equal(ordered[1].id, movementId); assert.equal(ordered[1].text, '移动的说明');
      assert.equal(await page.locator('path[data-movement]').count(), 0);
      const object = (await exported()).objects.find(object => object.annotations.some(annotation => annotation.text === '移动的说明')); assert.deepEqual(object, { id:'o2',source: left, movements: [], annotations: [{number:2,text:'移动的说明'}] });
    });
    current = 'adjacent source export';
    await checkpoint('two adjacent equal-distance moves remain separate native source objects', async () => {
      await importProject(fixture); await select(45, 50); await move(200, 100); await select(95, 50); await move(250, 100);
      const feedback = await exported(); assert.equal(feedback.objects.length, 2, 'equal transforms must not merge neighboring sources into one object');
      assert.deepEqual(feedback.objects, [
        { id:'o1',source: left, movements: [{ to: rect(200, 100, 250, 140) }], annotations: [] },
        { id:'o2',source: right, movements: [{ to: rect(250, 100, 300, 140) }], annotations: [] }
      ]);
      assert.deepEqual((await cards()).map(card => card.number), ['1', '2']);
    });
    current = 'preview/download/copy';
    await checkpoint('readable preview folds image bytes while compact download and clipboard preserve the entire image', async () => {
      await page.locator('#export').click(); const preview = await page.locator('#json-preview').inputValue();
      assert(!preview.includes(image), 'the desktop-style preview must fold full image bytes'); assert(preview.includes('图片编码已折叠'));
      const downloaded = await downloadText(page), data = JSON.parse(downloaded); assert.equal(data.image, image);
      assert(downloaded.endsWith('\n')); assert.equal(downloaded.trim().split('\n').length, 1, 'download uses the native compact encoding');
      const stable = value => Array.isArray(value) ? value.map(stable) : value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).sort().map(key => [key, stable(value[key])])) : value;
      assert.equal(downloaded, JSON.stringify(stable(data)) + '\n', 'native QJsonObject key ordering must be deterministic');
      // Stub only the OS clipboard sink, retaining the actual click handler and serializer.
      await page.evaluate(() => { globalThis.copiedFeedback = null; Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText: async text => { globalThis.copiedFeedback = text; } } }); });
      await page.locator('#copy-json').click(); await page.waitForFunction(() => globalThis.copiedFeedback !== null);
      assert.equal(await page.evaluate(() => globalThis.copiedFeedback), downloaded, 'copy must never copy the folded preview');
      await page.locator('#embed').uncheck(); const noImage = JSON.parse(await downloadText(page)); assert.equal(Object.hasOwn(noImage, 'image'), false);
      await page.locator('#embed').check(); await page.screenshot({ path: path.join(output, 'native-feedback-preview.png') }); await page.keyboard.press('Escape');
      fs.writeFileSync(path.join(output, 'adjacent-native-feedback.json'), downloaded);
    });
    assert.deepEqual(errors, []); assert.deepEqual(requests, []);
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ results, errors, requests, desktopReferences: ['REG-110', 'movementKeepsCreationOrderWithoutText', 'emptyMovementAnnotationFollowsItsBlock', 'app/model.cpp exportFeedback'] }, null, 2));
    console.log('Latest desktop parity: ' + results.length + ' checks passed.');
  } catch (error) {
    await page.screenshot({ path: path.join(output, 'failure.png') }).catch(() => {});
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ failed: current, results, error: error.stack, errors, requests, cards: await cards().catch(() => []) }, null, 2)); throw error;
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
