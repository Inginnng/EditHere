'use strict';
// Exercise the actual exported bytes. The visible JSON preview deliberately
// folds image payloads and is never an authoritative serialization fixture.
const assert = require('node:assert/strict');

async function ensureUtility(page) {
  const toggle = page.locator('#utility-toggle');
  if (await toggle.isVisible() && await toggle.getAttribute('aria-expanded') !== 'true') await toggle.click();
}

async function downloadText(page, selector = '#download-json') {
  if (selector === '#save-project') await ensureUtility(page);
  const pending = page.waitForEvent('download');
  await page.locator(selector).click();
  const download = await pending;
  const stream = await download.createReadStream();
  assert(stream, 'the export must provide downloadable bytes');
  const chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  assert.equal(await download.failure(), null);
  return Buffer.concat(chunks).toString('utf8');
}

async function readExportJson(page) {
  return JSON.parse(await downloadText(page));
}

module.exports = { downloadText, readExportJson, ensureUtility };
