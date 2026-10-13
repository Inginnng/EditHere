'use strict';
// Render the shared SVGs through the real offline app. This catches broken
// symbol references, blank geometry and stale state glyphs after a new image.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const root = path.resolve(__dirname, '../..');
const first = files => files.filter(Boolean).find(file => fs.existsSync(file));
const modulePath = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core'), path.join(root, 'node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe']);
assert(modulePath && executablePath, 'Set EDITHERE_PLAYWRIGHT_MODULE and EDITHERE_BROWSER_PATH.');
const {chromium} = require(modulePath);
const output = path.join(__dirname, 'output/icons');

async function main() {
  fs.mkdirSync(output, {recursive:true});
  const browser = await chromium.launch({executablePath, headless:true});
  const context = await browser.newContext({offline:true, viewport:{width:480,height:720}});
  const page = await context.newPage(), errors = [], requests = [];
  page.on('pageerror', error => errors.push(String(error)));
  page.on('dialog', dialog => dialog.accept());
  context.on('request', request => {if (/^https?:/.test(request.url())) requests.push(request.url());});
  try {
    await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href);
    const rendered = await page.evaluate(async () => {
      const namespace = 'http://www.w3.org/2000/svg', results = [];
      for (const symbol of document.querySelectorAll('.svg-library symbol')) {
        const svg = document.createElementNS(namespace, 'svg'), use = document.createElementNS(namespace, 'use');
        svg.setAttribute('width', '24');svg.setAttribute('height', '24');svg.setAttribute('viewBox', '0 0 24 24');
        use.setAttribute('href', '#' + symbol.id);svg.append(use);document.body.append(svg);
        const box = use.getBBox();results.push({id:symbol.id,width:box.width,height:box.height});svg.remove();
      }
      await new Promise(resolve => requestAnimationFrame(resolve));
      const missing = [...document.querySelectorAll('use')].map(use => use.getAttribute('href')).filter(href => !href || !document.getElementById(href.slice(1)));
      return {results,missing};
    });
    const manifest = JSON.parse(fs.readFileSync(path.join(root, 'assets/ui-icons/manifest.json'), 'utf8'));
    assert.equal(rendered.results.length, manifest.icons.length, 'offline app must include the complete shared registry');
    assert(rendered.results.every(icon => icon.width + icon.height > 0), 'every shared SVG must render geometry');
    assert.deepEqual(rendered.missing, [], 'all startup and tutorial icon references must resolve');
    const shapes = await page.evaluate(() => Object.fromEntries(['save','image-save','feedback-save','json','json-copy-file','json-copy-text','download','eye','eye-off'].map(id => [id,document.getElementById('i-' + id).innerHTML])));
    for (const ids of [['save','image-save','feedback-save'],['json','json-copy-file','json-copy-text','download'],['eye','eye-off']]) assert.equal(new Set(ids.map(id => shapes[id])).size, ids.length, 'different actions must have distinguishable paths: ' + ids.join(', '));
    await page.locator('#demo').click();await page.locator('[data-mode="point"]').click();await page.locator('#stage').click();
    await page.locator('#note-text').fill('共享 SVG 状态回归');await page.locator('#note-form [type="submit"]').click();
    assert.equal(await page.locator('.pin').count(), 1);
    const project = await page.evaluate(() => EditHereWeb.project());
    await page.locator('#toggle-annotations').click();
    assert.equal(await page.locator('#toggle-annotations use').getAttribute('href'), '#i-eye-off');
    assert.equal(await page.locator('#toggle-annotations').getAttribute('aria-pressed'), 'false');
    assert.equal(await page.locator('#toggle-annotations').getAttribute('aria-label'), '显示批注与位移箭头');
    assert.equal(await page.locator('.pin').count(), 0);
    await page.locator('#file-input').setInputFiles({name:'icon-regression.edithere-web.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(project))});
    await page.waitForFunction(() => document.querySelector('#toggle-annotations').getAttribute('aria-pressed') === 'true');
    assert.equal(await page.locator('#toggle-annotations use').getAttribute('href'), '#i-eye');
    assert.equal(await page.locator('#toggle-annotations').getAttribute('aria-label'), '隐藏批注与位移箭头');
    assert.equal(await page.locator('.pin').count(), 1);
    for (const [mode,id] of [['file','download'],['content','json-copy-text'],['project','save'],['view','json']]) {
      await page.locator('#save-options-toggle').click();await page.locator('#save-options [data-save-mode="' + mode + '"]').click();
      assert.equal(await page.locator('#save-json use').getAttribute('href'), '#i-' + id);
    }
    await page.locator('#save-options-toggle').click();await page.screenshot({path:path.join(output,'toolbar-and-save-light-480.png')});
    await page.locator('#save-options-toggle').click();
    // Host-driven theme changes use the same state synchronization as clicks.
    await page.evaluate(() => {document.documentElement.dataset.theme='dark';});
    await page.waitForFunction(() => document.querySelector('#theme-toggle use').getAttribute('href') === '#i-sun');
    assert.equal(await page.locator('#theme-toggle').getAttribute('aria-label'), '切换浅色主题');
    await page.screenshot({path:path.join(output,'toolbar-dark-480.png')});
    await page.evaluate(() => {document.documentElement.dataset.theme='light';});
    await page.waitForFunction(() => document.querySelector('#theme-toggle use').getAttribute('href') === '#i-moon');
    assert.deepEqual(errors, []);assert.deepEqual(requests, []);
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({rendered,errors,requests}, null, 2));
    console.log('PASS Shared SVG registry renders offline; action shapes are distinct; annotation and host-theme glyphs stay synchronized; selected save action uses its own icon.');
  } finally {await browser.close();}
}
main().catch(error => {console.error(error);process.exitCode=1;});
