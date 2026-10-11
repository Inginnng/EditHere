'use strict';
// Acceptance for the compact Agent panel, using real notes and pointer editing.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { readExportJson } = require('./export-test-utils.cjs');
const root = path.resolve(__dirname, '../..');
const output = path.join(__dirname, 'output/panel');
const first = values => values.filter(Boolean).find(value => fs.existsSync(value));
const modulePath = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core'), path.join(root, 'node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe']);
assert(modulePath && executablePath, 'Set EDITHERE_PLAYWRIGHT_MODULE and EDITHERE_BROWSER_PATH.');
const { chromium } = require(modulePath);

async function main() {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ executablePath, headless: true });
  const context = await browser.newContext({ offline: true, viewport: { width: 560, height: 720 } });
  const errors = [], requests = [], results = [];
  context.on('page', page => { page.on('pageerror', error => errors.push(String(error))); page.on('dialog', dialog => dialog.accept()); });
  context.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
  async function note(page, text) {
    await page.locator('#note-text').fill(text); await page.locator('#note-form [type="submit"]').click(); await page.locator('#note-dialog').waitFor({ state: 'hidden' });
  }
  async function ensureGlobal(page) {
    if (await page.locator('body').getAttribute('data-inspector-open') !== 'true') await page.locator('#inspect-toggle').click();
  }
  async function ensureUtility(page) {
    const toggle = page.locator('#utility-toggle');
    if (await toggle.isVisible() && await toggle.getAttribute('aria-expanded') !== 'true') await toggle.click();
  }
  async function feedback(page) {
    await page.locator('#export').click(); const data = await readExportJson(page); await page.locator('[data-close="export-dialog"]').first().click(); return data;
  }
  async function at(page, x, y) {
    return page.locator('#stage').evaluate((stage, point) => { const r = stage.getBoundingClientRect(); return { x: r.x + point.x * r.width / 1100, y: r.y + point.y * r.height / 700 }; }, { x, y });
  }
  async function metrics(page) {
    return page.evaluate(() => {
      const rect = element => { const r = element.getBoundingClientRect(); return { x: r.x, y: r.y, width: r.width, height: r.height, right: r.right, bottom: r.bottom }; };
      return { width: innerWidth, height: innerHeight, scrollWidth: document.documentElement.scrollWidth, stage: rect(document.querySelector('#stage')), panel: rect(document.querySelector('.sidebar')), tools: rect(document.querySelector('.tools')), view: rect(document.querySelector('.view-controls')), header: rect(document.querySelector('.header')), headerWrap: document.body.dataset.headerWrap, topIslands: ['.brand', '.view-controls', '.header-actions'].map(selector => ({ selector, ...rect(document.querySelector(selector)) })), zoomButtons: ['#zoom-out', '#fit', '#zoom-in'].map(selector => ({ selector, ...rect(document.querySelector(selector)) })), utility: rect(document.querySelector('.utility-dock')), notes: rect(document.querySelector('#notes')), noteScrollHeight: document.querySelector('#notes').scrollHeight, noteScrollTop: document.querySelector('#notes').scrollTop };
    });
  }
  async function fitSettled(page) {
    await page.locator('#fit').click(); await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  }
  try {
    for (const [width, height] of [[814, 979], [613, 653], [560, 560], [480, 640], [420, 720], [390, 844], [320, 640], [720, 720], [1000, 1000]]) {
      const page = await context.newPage(); await page.setViewportSize({ width, height });
      try {
        await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href); await page.locator('#demo').click(); await page.locator('#workspace').waitFor({ state: 'visible' });
        assert(await page.locator('#media-name').isHidden(), 'source filename must not occupy the canvas');
        assert.equal(await page.locator('.sidebar #global').count(), 1, 'global note action belongs to the opinions header');
        assert.equal(await page.locator('#global span').count(), 0, 'global note action stays an icon');
        assert.equal(await page.locator('.panel-actions #geometry-toggle + #global').count(), 1, 'component layout belongs immediately to the left of the global-note button');
        assert(await page.locator('#geometry-toggle').isDisabled(), 'component layout is disabled without a selected component');
        assert(await page.locator('#geometry').isHidden(), 'component layout starts closed and occupies no panel space');
        assert.equal(await page.locator('#export-dialog footer #preview').count(), 1, 'PNG export belongs in the export dialog');
        assert.equal(await page.locator('body').getAttribute('data-inspector-open'), 'false', 'compact workspace starts with the image visible');
        await page.locator('[data-mode="rect"]').click(); const a = await at(page, 815, 230), b = await at(page, 964, 275);
        await page.mouse.move(a.x, a.y); await page.mouse.down(); await page.mouse.move(b.x, b.y, { steps: 6 }); await page.mouse.up(); await note(page, '123');
        assert.equal(await page.locator('body').getAttribute('data-inspector-open'), 'true', 'saving a note must reveal it automatically');
        assert.equal(await page.locator('#inspect-count').textContent(), '1');
        assert.equal(await page.locator('.note-card p').textContent(), '123');
        await fitSettled(page); const measured = await metrics(page);
        assert(measured.panel.height <= 100, 'one short note should hug its content: ' + JSON.stringify(measured));
        assert(measured.stage.width >= width * .63, 'the image should keep most of the narrow panel width: ' + JSON.stringify(measured));
        assert(Math.abs(measured.stage.x + measured.stage.width / 2 - width / 2) < 1, 'portrait and square images must stay centered');
        assert(measured.stage.bottom + 5 <= measured.panel.y, 'notes must not cover the fitted image');
        assert(measured.panel.bottom + 5 <= measured.tools.y, 'notes must clear the tools');
        assert(measured.scrollWidth <= width, 'no horizontal overflow');
        assert(height - measured.tools.bottom >= 0 && height - measured.tools.bottom <= 12, 'tools stay close to the bottom edge');
        for (let i = 0; i < measured.topIslands.length; i++) for (let j = i + 1; j < measured.topIslands.length; j++) {
          const a = measured.topIslands[i], b = measured.topIslands[j];
          assert(Math.min(a.right, b.right) - Math.max(a.x, b.x) <= 1 || Math.min(a.bottom, b.bottom) - Math.max(a.y, b.y) <= 1, 'top controls must not overlap: ' + JSON.stringify(measured.topIslands));
        }
        const neededHeaderWidth = measured.topIslands.reduce((sum, island) => sum + island.width, 0) + 20;
        if (neededHeaderWidth <= measured.header.width) {
          const brand = measured.topIslands.find(island => island.selector === '.brand');
          assert.equal(measured.headerWrap, 'false', 'a header that fits must not unnecessarily put zoom on a second row');
          assert(Math.abs(measured.view.y + measured.view.height / 2 - brand.y - brand.height / 2) <= 1, 'brand and zoom must share the first row when all controls fit: ' + JSON.stringify(measured));
        }
        for (let i = 0; i < measured.zoomButtons.length - 1; i++) {
          const gap = measured.zoomButtons[i + 1].x - measured.zoomButtons[i].right;
          assert(gap >= 0 && gap <= 6, 'minus, zoom percentage and plus must have compact edge spacing: ' + JSON.stringify(measured.zoomButtons));
        }
        if (width <= 649) {
          assert(await page.locator('#utility-toggle').isVisible(), 'narrow panels need a visible More action');
          assert(measured.utility.x >= 0 && measured.utility.x <= 16 && height - measured.utility.bottom >= 0 && height - measured.utility.bottom <= 16 && measured.utility.y > height * .75, 'More belongs at the lower left: ' + JSON.stringify(measured.utility));
          await ensureUtility(page);
          const popup = await page.locator('#utility-actions').boundingBox();
          assert(popup && popup.y + popup.height <= measured.utility.y - 4 && popup.x >= 0 && popup.x + popup.width <= width, 'More actions must open upward inside the narrow viewport: ' + JSON.stringify({ popup, utility: measured.utility }));
          if (width === 613 || width === 560) await page.screenshot({ path: path.join(output, `utility-upward-${width}x${height}.png`) });
          await page.locator('#utility-toggle').click();
        }
        assert.equal(await page.locator('.panel-eyebrow, .sidebar-footer, .note-card small').count(), 0, 'no repeated inspector labels, coordinate footer or per-note coordinate rows');
        await page.screenshot({ path: path.join(output, `single-note-${width}x${height}.png`) });
        await page.locator('#panel-close').click(); assert.equal(await page.locator('#inspect-count').isVisible(), true, 'collapsed notes remain discoverable');
        await ensureGlobal(page); await page.locator('#global').click(); await note(page, '新的批注自动显示'); assert.equal(await page.locator('body').getAttribute('data-inspector-open'), 'true');
        await ensureUtility(page); await page.locator('#help').click(); assert(await page.locator('#help-dialog').isVisible()); await page.keyboard.press('Escape');
        if (width === 320) {
          await page.locator('#export').click();
          const buttons = await page.locator('#export-dialog footer button').evaluateAll(elements => elements.map(element => {
            const r = element.getBoundingClientRect(), hit = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
            return { label: element.textContent, x: r.x, right: r.right, y: r.y, bottom: r.bottom, reachable: hit === element || element.contains(hit) };
          }));
          assert(buttons.every(button => button.x >= 0 && button.right <= width && button.reachable), 'all export actions must remain accessible in a 320px panel: ' + JSON.stringify(buttons));
          await page.screenshot({ path: path.join(output, 'export-320x640.png') }); await page.keyboard.press('Escape');
        }
        await ensureUtility(page); await page.locator('#theme-toggle').click(); await page.screenshot({ path: path.join(output, `two-notes-${width}x${height}-dark.png`) });
        results.push({ name: 'content-sized notes, automatic reveal and centered fit ' + width + 'x' + height, ...measured });
      } catch (error) { await page.screenshot({ path: path.join(output, `failure-${width}x${height}.png`) }); throw error; }
      finally { await page.close(); }
    }
    const page = await context.newPage();
    await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href); await page.locator('#demo').click(); await page.locator('#workspace').waitFor({ state: 'visible' });
    const image = await page.locator('#canvas').evaluate(canvas => canvas.toDataURL());
    const many = { schemaVersion: 'edithere-web-project-1', kind: 'image', name: 'many-comments.png', width: 1100, height: 700, document: { id: 'many', timestampUs: null, image, layout: null, notes: Array.from({ length: 30 }, (_, index) => ({ id: 'note-' + index, source: null, text: '第 ' + (index + 1) + ' 条：多行意见保留可读性。\n面板不应无限挤占原图。' })) } };
    await page.locator('#file-input').setInputFiles({ name: 'many.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(many)) }); await page.waitForFunction(() => document.body.dataset.busy !== 'true');
    await page.locator('#panel-close').click(); await ensureGlobal(page); await page.locator('#global').click(); await note(page, '最后一条要自动滚动到这里'); await fitSettled(page);
    const measured = await metrics(page); assert(measured.panel.height <= 720 * .26 + 1); assert(measured.noteScrollHeight > measured.notes.height); assert(measured.noteScrollTop > 0);
    const last = await page.locator('.note-card').last().boundingBox(); assert(last.y >= measured.notes.y - 1 && last.y + last.height <= measured.notes.bottom + 1, 'new note must be visible inside the scroll area');
    assert.equal((await feedback(page)).objects.flatMap(object => object.annotations).length, 31);
    await page.screenshot({ path: path.join(output, 'many-notes-560x720.png') }); results.push({ name: 'many notes stay bounded and reveal the newest note', ...measured });
    await page.locator('#file-input').setInputFiles({ name: 'clean.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify({ ...many, document: { ...many.document, notes: [] } })) }); await page.waitForFunction(() => document.body.dataset.busy !== 'true');
    await page.locator('#panel-close').click();
    const unchanged = await feedback(page), pixels = await page.locator('#canvas').evaluate(canvas => canvas.toDataURL());
    await page.locator('#explode').click();
    assert.equal(await page.locator('#explode').getAttribute('aria-pressed'), 'true'); assert(await page.locator('[data-mode="cut"]').isVisible());
    assert(await page.locator('#geometry').isHidden(), 'opening big bang must not open the layout popup');
    const animation = await page.locator('.explosion-preview').evaluate(layer => ({ pieces: layer.children.length, active: layer.getAnimations({ subtree: true }).filter(animation => animation.playState === 'running').length }));
    assert(animation.pieces > 1 && animation.active > 1, 'opening big bang must animate the desktop reflection and diagonal wave');
    await page.screenshot({ path: path.join(output, 'explode-opening.png') });
    await page.locator('#explode').click(); assert.equal(await page.locator('#explode').getAttribute('aria-pressed'), 'false'); assert(await page.locator('[data-mode="cut"]').isHidden()); assert.equal(await page.locator('.explosion-preview').count(), 0);
    assert.deepEqual(await feedback(page), unchanged); assert.equal(await page.locator('#canvas').evaluate(canvas => canvas.toDataURL()), pixels, 'opening/closing must not alter image pixels');
    await page.locator('#explode').click(); await page.locator('.explosion-preview').waitFor({ state: 'hidden' });
    await page.locator('[data-mode="cut"]').click(); const a = await at(page, 245, 170), b = await at(page, 490, 320);
    await page.mouse.move(a.x, a.y); await page.mouse.down(); await page.mouse.move(b.x, b.y, { steps: 6 }); await page.mouse.up();
    const from = await at(page, 365, 240), to = await at(page, 405, 270); await page.mouse.move(from.x, from.y); await page.mouse.down(); await page.mouse.move(to.x, to.y, { steps: 6 }); await page.mouse.up();
    const moved = await feedback(page); assert(moved.objects.some(object => object.movements.length)); const movedPixels = await page.locator('#canvas').evaluate(canvas => canvas.toDataURL());
    await ensureGlobal(page);
    assert.equal(await page.locator('.movement-empty p').first().textContent(), '位置变化，点击可写下想法...', 'a movement without text must explain how to add it');
    assert.equal(await page.locator('.movement-empty .note-number').first().textContent(), '1', 'an empty movement is a numbered real annotation');
    const line = page.locator('.movement-line').first(); assert(['#346df1', '#2456c7'].includes(await line.getAttribute('stroke')), 'movement arrows use the blue accent');
    const outlines = await page.locator('#overlay rect[stroke]').evaluateAll(elements => elements.map(element => element.getAttribute('stroke')));
    assert(outlines.length && outlines.every(stroke => ['#346df1', '#2456c7'].includes(stroke)), 'selection and annotation frames use the blue accent');
    await page.locator('.movement-empty [data-edit]').first().click(); assert.equal(await page.locator('#note-text').inputValue(), ''); await page.keyboard.press('Escape');
    results.push({ name: 'empty movement explains its click action and blue arrows/selection frames remain editable' });
    await page.locator('#explode').click(); assert.deepEqual(await feedback(page), moved); assert.equal(await page.locator('#canvas').evaluate(canvas => canvas.toDataURL()), movedPixels, 'closing must keep prior edits');
    await page.locator('#explode').click(); await page.locator('#explode').click(); assert.deepEqual(await feedback(page), moved, 'repeated toggles must remain non-destructive');
    await page.setViewportSize({ width: 560, height: 560 }); await page.locator('#explode').click();
    const component = await at(page, 435, 300); await page.mouse.click(component.x, component.y);
    if (await page.locator('body').getAttribute('data-inspector-open') !== 'true') await page.locator('#inspect-toggle').click();
    assert(await page.locator('#geometry-toggle').isEnabled()); assert(await page.locator('#geometry').isHidden(), 'selecting a component must not automatically expand layout controls');
    const panelBefore = await page.locator('.sidebar').boundingBox();
    await page.locator('#geometry-toggle').click(); assert(await page.locator('#geometry').isVisible());
    const panelAfter = await page.locator('.sidebar').boundingBox(), layoutPopup = await page.locator('#geometry').boundingBox();
    assert.equal(panelAfter.height, panelBefore.height, 'opening the layout popup must not enlarge the opinions panel');
    assert(layoutPopup.x >= 0 && layoutPopup.x + layoutPopup.width <= 560 && layoutPopup.y >= 0 && layoutPopup.y + layoutPopup.height <= 560, 'numeric controls must remain inside the small square viewport');
    await page.screenshot({ path: path.join(output, 'component-layout-popup-560x560.png') });
    await page.locator('#lock-ratio').uncheck();
    await page.locator('#gw').fill('200'); await page.locator('#gh').fill('125'); await page.locator('#apply-geometry').click();
    const resized = await feedback(page); assert(resized.objects.some(object => object.movements.some(movement => Math.abs(movement.to.x2 - movement.to.x1 - 200) < .01 && Math.abs(movement.to.y2 - movement.to.y1 - 125) < .01)), 'numeric controls must remain usable in a small square panel');
    assert(await page.locator('#geometry').isHidden(), 'clicking outside the popup closes it');
    await page.locator('#geometry-toggle').click(); await page.locator('#geometry-close').click(); assert(await page.locator('#geometry').isHidden());
    for (const [width, height] of [[320, 640], [650, 390]]) {
      await page.setViewportSize({ width, height }); await page.locator('#geometry-toggle').click();
      const popup = await page.locator('#geometry').boundingBox();
      assert(popup.x >= 0 && popup.x + popup.width <= width && popup.y >= 0 && popup.y + popup.height <= height, 'layout popup must stay within narrow and short windows: ' + JSON.stringify({ width, height, popup }));
      await page.locator('#geometry-close').click();
    }
    await page.setViewportSize({ width: 560, height: 560 }); await page.locator('#geometry-toggle').click(); await page.locator('#explode').click();
    assert(await page.locator('#geometry').isHidden()); assert(await page.locator('#geometry-toggle').isDisabled(), 'leaving big bang closes and disables the layout popup');
    results.push({ name: 'small layout button opens usable bounded controls without consuming opinions-panel space' });
    await page.emulateMedia({ reducedMotion: 'reduce' }); await page.locator('#explode').click(); assert.equal(await page.locator('.explosion-preview').count(), 0); assert.equal(await page.locator('#explode').getAttribute('aria-pressed'), 'true'); await page.locator('#explode').click();
    results.push({ name: 'desktop wave animation, repeated exit/re-entry and reduced-motion support preserve edits', animation });
    await page.close();
    assert.deepEqual(errors, []); assert.deepEqual(requests, []);
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ results, errors, requests }, null, 2));
    console.log('Compact panel acceptance: ' + results.length + ' checks passed.');
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
