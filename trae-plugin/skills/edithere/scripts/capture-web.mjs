#!/usr/bin/env node
/**
 * Lossless web page capture utility for EditHere gallery reviews.
 * Uses Playwright when installed in the target project (or Node resolution path)
 * and optionally registers captured PNGs in an EditHere image-set manifest.
 */
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);

function loadImageSetHelpers() {
  const candidates = [
    new URL('./image-set.cjs', import.meta.url),
    new URL('../web/image-set.cjs', import.meta.url),
  ];
  const found = candidates.find((candidate) => fs.existsSync(candidate));
  if (!found) {
    throw createAppError('missing_dependency', 'Cannot locate image-set.cjs alongside capture-web.mjs or under ../web/.');
  }
  return require(fileURLToPath(found));
}

function createAppError(code, message, extra = {}) {
  const error = new Error(message);
  error.code = code;
  Object.assign(error, extra);
  return error;
}

export function usageText() {
  return [
    'EditHere web screenshot capture (DPR=2 lossless PNG by default)',
    '',
    'Single page:',
    '  node capture-web.mjs --url <http(s)://...|file://...> --output <shot.png> [options]',
    '',
    'Batch pages:',
    '  node capture-web.mjs --batch <spec.json> [--manifest <project.json>] [--manifest-name <title>]',
    '',
    'Options:',
    '  --viewport <WxH>       Logical viewport size (default: 1440x900)',
    '  --dpr <1..4>           Device scale factor (default: 2)',
    '  --theme <mode>         Color scheme: light | dark | no-preference (default: light)',
    '  --full-page            Capture full scrollable page instead of viewport',
    '  --selector <css>       Capture a specific element matching CSS selector',
    '  --wait-ms <0..30000>   Extra settle delay after load & fonts ready (default: 250)',
    '  --timeout <1000..300000> Navigation/element timeout in ms (default: 30000)',
    '  --manifest <set.json>  Upsert captured page(s) into an EditHere image-set manifest',
    '  --manifest-name <name> Create manifest with this name if --manifest does not exist yet',
    '  --id <page-id>         Stable page ID (required in single mode when --manifest is set)',
    '  --name <page-name>     Human-readable page label for the manifest',
    '  -h, --help             Show this help',
  ].join('\n');
}

export function parseViewport(raw) {
  const match = /^(\d{2,5})x(\d{2,5})$/i.exec(String(raw ?? '').trim());
  if (!match) {
    throw createAppError('invalid_argument', `Invalid --viewport "${raw}". Expected WxH such as 1440x900.`);
  }
  const width = Number(match[1]);
  const height = Number(match[2]);
  if (width < 100 || width > 16384 || height < 100 || height > 16384) {
    throw createAppError('invalid_argument', `Viewport dimensions out of range (100..16384): ${raw}.`);
  }
  return { width, height };
}

function parseBoundedNumber(raw, flag, min, max, integer = false) {
  const value = Number(raw);
  if (!Number.isFinite(value) || (integer && !Number.isInteger(value)) || value < min || value > max) {
    throw createAppError('invalid_argument', `Invalid ${flag} "${raw}". Expected ${integer ? 'an integer' : 'a number'} between ${min} and ${max}.`);
  }
  return value;
}

function validateUrl(raw) {
  let parsed;
  try {
    parsed = new URL(String(raw ?? ''));
  } catch {
    throw createAppError('invalid_argument', `Invalid URL "${raw}".`);
  }
  if (!['http:', 'https:', 'file:'].includes(parsed.protocol)) {
    throw createAppError('invalid_argument', `Unsupported URL protocol "${parsed.protocol}". Only http:, https:, and file: are allowed.`);
  }
  return parsed.toString();
}

function validateTheme(raw) {
  if (!['light', 'dark', 'no-preference'].includes(raw)) {
    throw createAppError('invalid_argument', `Invalid --theme "${raw}". Expected light, dark, or no-preference.`);
  }
  return raw;
}

export function parseCliArgs(argv) {
  const opts = {
    help: false,
    url: null,
    output: null,
    batch: null,
    viewport: { width: 1440, height: 900 },
    dpr: 2,
    theme: 'light',
    fullPage: false,
    selector: null,
    waitMs: 250,
    timeout: 30000,
    manifest: null,
    manifestName: null,
    id: null,
    name: null,
  };

  const VALUE_FLAGS = new Set([
    '--url', '--output', '--batch', '--viewport', '--dpr', '--theme',
    '--selector', '--wait-ms', '--timeout', '--manifest', '--manifest-name', '--id', '--name',
  ]);

  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (token === '-h' || token === '--help') {
      opts.help = true;
      return opts;
    }
    if (token === '--full-page') {
      opts.fullPage = true;
      continue;
    }
    if (!VALUE_FLAGS.has(token)) {
      throw createAppError('invalid_argument', `Unknown option: ${token}`);
    }
    if (i + 1 >= argv.length) {
      throw createAppError('invalid_argument', `Missing value for option: ${token}`);
    }
    const value = argv[i + 1];
    i += 1;
    switch (token) {
      case '--url': opts.url = validateUrl(value); break;
      case '--output': opts.output = path.resolve(value); break;
      case '--batch': opts.batch = path.resolve(value); break;
      case '--viewport': opts.viewport = parseViewport(value); break;
      case '--dpr': opts.dpr = parseBoundedNumber(value, '--dpr', 1, 4, false); break;
      case '--theme': opts.theme = validateTheme(value); break;
      case '--selector': opts.selector = value; break;
      case '--wait-ms': opts.waitMs = parseBoundedNumber(value, '--wait-ms', 0, 30000, true); break;
      case '--timeout': opts.timeout = parseBoundedNumber(value, '--timeout', 1000, 300000, true); break;
      case '--manifest': opts.manifest = path.resolve(value); break;
      case '--manifest-name': opts.manifestName = value; break;
      case '--id': opts.id = value; break;
      case '--name': opts.name = value; break;
    }
  }

  if (opts.batch) {
    if (opts.url || opts.output || opts.id || opts.name) {
      throw createAppError('invalid_argument', '--batch cannot be combined with --url, --output, --id, or --name.');
    }
  } else {
    if (!opts.url || !opts.output) {
      throw createAppError('invalid_argument', 'Both --url and --output are required (or use --batch <spec.json>).');
    }
    if (opts.manifest && !opts.id) {
      throw createAppError('invalid_argument', '--id is required when --manifest is specified in single-capture mode.');
    }
  }

  return opts;
}

export function readPngSize(filePath) {
  const fd = fs.openSync(filePath, 'r');
  try {
    const header = Buffer.alloc(24);
    const bytesRead = fs.readSync(fd, header, 0, 24, 0);
    if (bytesRead < 24 || !header.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
      throw createAppError('io_error', `Captured output is not a valid PNG file: ${filePath}`);
    }
    return {
      width: header.readUInt32BE(16),
      height: header.readUInt32BE(20),
    };
  } finally {
    fs.closeSync(fd);
  }
}

function loadPlaywright() {
  const loaders = [
    createRequire(path.join(process.cwd(), 'package.json')),
    require,
  ];
  for (const loader of loaders) {
    for (const pkg of ['playwright', 'playwright-core']) {
      try {
        const mod = loader(pkg);
        if (mod && mod.chromium) return mod;
      } catch (error) {
        if (error && error.code !== 'MODULE_NOT_FOUND') throw error;
      }
    }
  }
  throw createAppError(
    'playwright_not_found',
    'Playwright (playwright or playwright-core) was not found in the current project or Node resolution path.',
    {
      hint: 'Use the host browser screenshot tool (see references/capture.md) or run inside a project that already has Playwright installed.',
    }
  );
}

function normalizeBatchSpec(opts) {
  let raw;
  try {
    raw = JSON.parse(fs.readFileSync(opts.batch, 'utf8').replace(/^\uFEFF/, ''));
  } catch (error) {
    throw createAppError('invalid_argument', `Failed to read batch spec "${opts.batch}": ${error.message}`);
  }
  const baseDir = path.dirname(opts.batch);
  const specObj = Array.isArray(raw) ? { pages: raw } : raw;
  if (!specObj || !Array.isArray(specObj.pages) || specObj.pages.length === 0) {
    throw createAppError('invalid_argument', 'Batch spec must be a non-empty array or an object with a non-empty pages array.');
  }
  const defaults = specObj.defaults || {};
  const manifestPath = opts.manifest || (specObj.manifest ? path.resolve(baseDir, specObj.manifest) : null);
  const manifestName = opts.manifestName || specObj.name || null;

  const jobs = specObj.pages.map((entry, idx) => {
    if (!entry || typeof entry !== 'object') {
      throw createAppError('invalid_argument', `Batch item at index ${idx} must be an object.`);
    }
    const url = validateUrl(entry.url);
    if (!entry.output || typeof entry.output !== 'string') {
      throw createAppError('invalid_argument', `Batch item at index ${idx} is missing "output".`);
    }
    const output = path.resolve(baseDir, entry.output);
    const vpRaw = entry.viewport ?? defaults.viewport;
    const viewport = vpRaw
      ? (typeof vpRaw === 'string' ? parseViewport(vpRaw) : {
          width: parseBoundedNumber(vpRaw.width, 'viewport.width', 100, 16384, true),
          height: parseBoundedNumber(vpRaw.height, 'viewport.height', 100, 16384, true),
        })
      : opts.viewport;
    const dpr = parseBoundedNumber(entry.dpr ?? defaults.dpr ?? opts.dpr, 'dpr', 1, 4, false);
    const theme = validateTheme(entry.theme ?? defaults.theme ?? opts.theme);
    const fullPage = Boolean(entry.fullPage ?? defaults.fullPage ?? opts.fullPage);
    const selector = entry.selector ?? defaults.selector ?? opts.selector ?? null;
    const waitMs = parseBoundedNumber(entry.waitMs ?? defaults.waitMs ?? opts.waitMs, 'waitMs', 0, 30000, true);
    const timeout = parseBoundedNumber(entry.timeout ?? defaults.timeout ?? opts.timeout, 'timeout', 1000, 300000, true);
    if (manifestPath && !entry.id) {
      throw createAppError('invalid_argument', `Batch item at index ${idx} needs an "id" when updating manifest "${manifestPath}".`);
    }
    return { id: entry.id || null, name: entry.name || null, url, output, viewport, dpr, theme, fullPage, selector, waitMs, timeout };
  });

  return { manifestPath, manifestName, jobs };
}

function ensureManifestReady(manifestPath, manifestName, helpers) {
  if (!manifestPath) return;
  if (fs.existsSync(manifestPath)) return;
  if (!manifestName) {
    throw createAppError(
      'manifest_not_found',
      `Manifest does not exist: ${manifestPath}. Create it first via image-set.mjs create or pass --manifest-name.`
    );
  }
  helpers.writeManifest(manifestPath, { schemaVersion: helpers.VERSION, name: manifestName, pages: [] }, { create: true });
}

async function captureJob(browser, job) {
  const context = await browser.newContext({
    viewport: job.viewport,
    deviceScaleFactor: job.dpr,
    colorScheme: job.theme,
  });
  try {
    const page = await context.newPage();
    page.setDefaultTimeout(job.timeout);
    await page.goto(job.url, { waitUntil: 'load', timeout: job.timeout });
    try {
      await page.waitForLoadState('networkidle', { timeout: Math.min(job.timeout, 5000) });
    } catch {
      // Keep moving when long-polling / SSE keeps connections open after load.
    }
    await page.evaluate(() => document.fonts ? document.fonts.ready : Promise.resolve());
    if (job.waitMs > 0) {
      await page.waitForTimeout(job.waitMs);
    }
    fs.mkdirSync(path.dirname(job.output), { recursive: true });
    if (job.selector) {
      const locator = page.locator(job.selector).first();
      await locator.waitFor({ state: 'visible', timeout: job.timeout });
      await locator.screenshot({ path: job.output, type: 'png' });
    } else {
      await page.screenshot({ path: job.output, type: 'png', fullPage: job.fullPage });
    }
    const { width, height } = readPngSize(job.output);
    return {
      id: job.id,
      name: job.name,
      url: job.url,
      output: job.output,
      width,
      height,
      dpr: job.dpr,
      theme: job.theme,
    };
  } finally {
    await context.close();
  }
}

async function main() {
  const opts = parseCliArgs(process.argv.slice(2));
  if (opts.help) {
    console.log(usageText());
    return;
  }

  const plan = opts.batch
    ? normalizeBatchSpec(opts)
    : {
        manifestPath: opts.manifest,
        manifestName: opts.manifestName,
        jobs: [{
          id: opts.id,
          name: opts.name,
          url: opts.url,
          output: opts.output,
          viewport: opts.viewport,
          dpr: opts.dpr,
          theme: opts.theme,
          fullPage: opts.fullPage,
          selector: opts.selector,
          waitMs: opts.waitMs,
          timeout: opts.timeout,
        }],
      };

  const helpers = plan.manifestPath ? loadImageSetHelpers() : null;
  if (plan.manifestPath) {
    if (fs.existsSync(plan.manifestPath)) {
      helpers.readManifest(plan.manifestPath);
    } else if (!plan.manifestName) {
      throw createAppError(
        'manifest_not_found',
        `Manifest does not exist: ${plan.manifestPath}. Create it first via image-set.mjs create or pass --manifest-name.`
      );
    }
  }

  const { chromium } = loadPlaywright();
  const browser = await chromium.launch({ headless: true });
  const captured = [];
  try {
    if (plan.manifestPath) {
      ensureManifestReady(plan.manifestPath, plan.manifestName, helpers);
    }
    for (const job of plan.jobs) {
      const item = await captureJob(browser, job);
      if (plan.manifestPath && job.id) {
        helpers.upsertPage(plan.manifestPath, { id: job.id, name: job.name || undefined, imagePath: item.output });
      }
      captured.push(item);
    }
  } finally {
    await browser.close();
  }

  const summary = {
    ok: true,
    manifest: plan.manifestPath,
    count: captured.length,
    captures: captured,
  };
  if (captured.length === 1) {
    summary.output = captured[0].output;
    summary.width = captured[0].width;
    summary.height = captured[0].height;
  }
  console.log(JSON.stringify(summary));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    const payload = {
      ok: false,
      code: error && error.code ? String(error.code) : 'capture_error',
      error: error && error.message ? error.message : String(error),
    };
    if (error && error.hint) payload.hint = error.hint;
    console.error(JSON.stringify(payload));
    process.exitCode = 1;
  });
}
