'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const root = path.resolve(__dirname, '../..');
const output = path.join(__dirname, 'output/visual');
const first = values => values.filter(Boolean).find(value => fs.existsSync(value));
const modulePath = first([process.env.EDITHERE_PLAYWRIGHT_MODULE, path.resolve(root, '../EditHere宣传/动画/node_modules/playwright-core'), path.join(root, 'node_modules/playwright-core')]);
const executablePath = first([process.env.EDITHERE_BROWSER_PATH, 'C:/Users/InGing/AppData/Local/ms-playwright/chromium-1228/chrome-win64/chrome.exe', 'C:/Program Files/Google/Chrome/Application/chrome.exe']);
assert(modulePath && executablePath, 'Set EDITHERE_PLAYWRIGHT_MODULE and EDITHERE_BROWSER_PATH to installed browser dependencies.');
const { chromium } = require(modulePath);

async function main() {
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ executablePath, headless: true });
  const context = await browser.newContext({ offline: true });
  const errors = [], requests = [], states = [];
  context.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
  context.on('page', page => page.on('pageerror', error => errors.push(String(error))));
  const identity = [
    ['.brand-lockup-light', 'assets/brand/edithere-lockup.svg'],
    ['.brand-lockup-dark', 'assets/brand/edithere-lockup-light.svg'],
    ['.brand-symbol', 'assets/icons/edithere.svg']
  ];
  try {
    for (const [width, height] of [[1480, 1000], [1280, 800], [560, 900], [390, 844], [320, 640], [844, 390]]) {
      const page = await context.newPage();
      await page.setViewportSize({ width, height });
      await page.goto(pathToFileURL(path.join(root, 'web/EditHere.html')).href);
      for (const [selector, asset] of identity) {
        const source = await page.locator(selector).getAttribute('src');
        assert(source.startsWith('data:image/svg+xml;base64,'), asset + ' must remain embedded for offline use');
        assert.deepEqual(Buffer.from(source.split(',')[1], 'base64'), fs.readFileSync(path.join(root, asset)), 'The checked-in brand geometry and lettering must be reused without modification');
      }
      for (const theme of ['light', 'dark']) {
        if (theme === 'dark') await page.locator('#theme-toggle').click();
        const brandSelector = width <= 370 ? '.brand-symbol' : '.brand-lockup-' + theme;
        await page.locator(brandSelector).evaluate(image => image.decode());
        assert(await page.locator(brandSelector).isVisible(), 'correct original identity must be visible: ' + width + ' ' + theme);
        const metrics = await page.evaluate(() => {
          const rect = element => { const r = element.getBoundingClientRect(); return { x: r.x, y: r.y, right: r.right, bottom: r.bottom, width: r.width, height: r.height }; };
          const brand = [...document.querySelectorAll('.brand img')].find(element => getComputedStyle(element).display !== 'none');
          const rgb = value => (value.match(/[\d.]+/g) || []).slice(0, 3).map(Number);
          const luminance = color => rgb(color).map(value => { const n = value / 255; return n <= .04045 ? n / 12.92 : ((n + .055) / 1.055) ** 2.4; }).reduce((sum, value, index) => sum + value * [.2126, .7152, .0722][index], 0);
          const style = getComputedStyle(document.querySelector('.intro')), background = getComputedStyle(document.body).backgroundColor;
          const lightness = [luminance(style.color), luminance(background)].sort((a, b) => b - a);
          return { brand: rect(brand), ratio: brand.naturalWidth / brand.naturalHeight, header: rect(document.querySelector('.header')), title: rect(document.querySelector('h1')), card: rect(document.querySelector('#drop-card')), brandBox: rect(document.querySelector('.brand')), actions: rect(document.querySelector('.header-actions')), width: innerWidth, height: innerHeight, scrollWidth: document.documentElement.scrollWidth, contrast: (lightness[0] + .05) / (lightness[1] + .05) };
        });
        assert(Math.abs(metrics.brand.width / metrics.brand.height - metrics.ratio) < .002, 'brand must retain its original aspect ratio');
        assert(metrics.brandBox.right + 5 <= metrics.actions.x, 'brand and actions must have a clear gap');
        assert(metrics.scrollWidth <= width, 'home must not overflow horizontally');
        assert(metrics.title.y >= metrics.header.bottom + 5, 'headline must clear the header');
        assert(metrics.card.x >= 0 && metrics.card.right <= width, 'source upload must remain inside the window');
        assert(metrics.contrast >= 4.5, 'secondary introductory text must have readable contrast: ' + metrics.contrast);
        states.push({ width, height, theme, ...metrics });
        await page.screenshot({ path: path.join(output, `home-${width}x${height}-${theme}.png`) });
        await page.locator('#demo').click();
        await page.locator('#workspace').waitFor({ state: 'visible' });
        if (width < 900) await page.locator('#inspect-toggle').click();
        await page.locator('#global').click();
        await page.locator('#note-text').fill('保留品牌；加强标题层级与留白。\n多行文字、English 2026 与中文可以共同阅读。');
        await page.locator('#note-form [type="submit"]').click();
        await page.locator('#note-dialog').waitFor({ state: 'hidden' });
        const note = await page.locator('.note-card p').first().evaluate(element => ({ fontSize: Number.parseFloat(getComputedStyle(element).fontSize), scrollWidth: element.scrollWidth, clientWidth: element.clientWidth }));
        assert(note.fontSize >= 12, 'comment text must remain readable');
        assert(note.scrollWidth <= note.clientWidth + 1, 'mixed comment text must wrap without horizontal clipping');
        await page.screenshot({ path: path.join(output, `workspace-${width}x${height}-${theme}.png`) });
        // Return to a fresh empty document, retaining only the chosen theme.
        if (theme === 'light') {
          page.once('dialog', dialog => dialog.accept());
          await page.reload();
        }
      }
      await page.close();
    }
    assert.deepEqual(errors, []);
    assert.deepEqual(requests, []);
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ states, errors, requests }, null, 2));
    console.log('Visual acceptance: 12 theme/viewport states; original brand bytes, aspect ratio, readable contrast and mixed text verified.');
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
