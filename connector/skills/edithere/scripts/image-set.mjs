#!/usr/bin/env node
import {createRequire} from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
const require = createRequire(import.meta.url);
function loadImageSetLib() {
  for (const rel of ['./image-set.cjs', '../web/image-set.cjs']) {
    try { return require(rel); }
    catch (error) {
      if (error && error.code === 'MODULE_NOT_FOUND') continue;
      throw error;
    }
  }
  throw new Error('Cannot locate image-set.cjs alongside image-set.mjs or under ../web/.');
}
const {VERSION, readManifest, writeManifest, upsertPage, upsertPages, bundledProject} = loadImageSetLib();

function compactResult(file, result, updatedCount) {
  return {
    manifest: path.resolve(file),
    name: result.name,
    pageCount: result.pages.length,
    updatedCount,
    pages: result.pages.map(p => ({id: p.id, name: p.name, path: p.path})),
  };
}

function parsePageSpec(spec) {
  const firstEq = spec.indexOf('=');
  if (firstEq <= 0 || firstEq === spec.length - 1) throw new Error('Invalid --page value (expected <id>=<imagePath>[=<name>]): ' + spec);
  const id = spec.slice(0, firstEq);
  const rest = spec.slice(firstEq + 1);
  const lastEq = rest.lastIndexOf('=');
  let imagePath = rest;
  let name;
  if (lastEq > 0 && !fs.existsSync(path.resolve(rest))) {
    imagePath = rest.slice(0, lastEq);
    name = rest.slice(lastEq + 1);
  }
  if (!imagePath) throw new Error('Invalid --page image path: ' + spec);
  return {id, name, imagePath};
}

function resolvePageJsonImage(baseDir, rawImage) {
  if (path.isAbsolute(rawImage)) return rawImage;
  const fromBase = path.resolve(baseDir, rawImage);
  if (fs.existsSync(fromBase)) return fromBase;
  const fromCwd = path.resolve(process.cwd(), rawImage);
  if (fs.existsSync(fromCwd)) return fromCwd;
  return fromBase;
}

function loadPagesJsonSpec(specPath) {
  const isStdin = specPath === '-';
  const raw = isStdin ? fs.readFileSync(0, 'utf8') : fs.readFileSync(path.resolve(specPath), 'utf8');
  const baseDir = isStdin ? process.cwd() : path.dirname(path.resolve(specPath));
  const parsed = JSON.parse(raw.replace(/^\uFEFF/, ''));
  let specName = null;
  let items;
  if (Array.isArray(parsed)) {
    items = parsed;
  } else if (parsed && typeof parsed === 'object' && Array.isArray(parsed.pages)) {
    if (typeof parsed.name === 'string' && parsed.name.trim()) specName = parsed.name.trim();
    items = parsed.pages;
  } else {
    throw new Error('--pages-json must be a JSON array or an object with a pages array.');
  }
  const entries = items.map((item, idx) => {
    if (!item || typeof item !== 'object') throw new Error('Invalid page object in --pages-json at index ' + idx);
    const rawImage = item.image ?? item.imagePath ?? item.path;
    if (typeof rawImage !== 'string' || !rawImage.trim()) throw new Error('Missing image path in --pages-json at index ' + idx);
    return {
      id: item.id,
      name: item.name,
      imagePath: resolvePageJsonImage(baseDir, rawImage),
    };
  });
  return {specName, entries};
}

function scanPagesDir(dirPath) {
  const absDir = path.resolve(dirPath);
  const stat = fs.statSync(absDir);
  if (!stat.isDirectory()) throw new Error('Not a directory: ' + absDir);
  const names = fs.readdirSync(absDir, {withFileTypes: true})
    .filter(e => e.isFile() && /\.(png|jpe?g|webp|gif|bmp)$/i.test(e.name))
    .map(e => e.name)
    .sort((a, b) => a.localeCompare(b, 'en', {sensitivity: 'base'}) || a.localeCompare(b));
  const usedIds = new Set();
  return names.map((fileName, idx) => {
    const ext = path.extname(fileName);
    const stem = path.basename(fileName, ext);
    let base = stem
      .replace(/[^a-zA-Z0-9._-]+/g, '-')
      .replace(/^[^a-zA-Z0-9]+/, '')
      .replace(/-+$/g, '')
      .slice(0, 120);
    if (!base) base = 'page-' + (idx + 1);
    let candidate = base;
    let suffix = 2;
    while (usedIds.has(candidate)) {
      const tag = '-' + suffix++;
      candidate = base.slice(0, 128 - tag.length) + tag;
    }
    usedIds.add(candidate);
    return {
      id: candidate,
      name: stem || candidate,
      imagePath: path.join(absDir, fileName),
    };
  });
}

const [command, file, ...args] = process.argv.slice(2);
try {
  if (!command || command === '--help') {
    console.log(
      'EditHere project image sets\n' +
      '  node scripts/image-set.mjs create <set.json> --name <project>\n' +
      '  node scripts/image-set.mjs upsert <set.json> --id <page-id> --image <screenshot> [--name <page-name>] [--compact]\n' +
      '  node scripts/image-set.mjs upsert-batch <set.json> [--name <project>] [--pages-json <spec.json|->] [--from-dir <dir>] [--page <id>=<image>[=<name>] ...] [--full]\n' +
      '  node scripts/image-set.mjs sync <set.json> ... (alias for upsert-batch)\n' +
      '  node scripts/image-set.mjs remove <set.json> --id <page-id>\n' +
      '  node scripts/image-set.mjs list <set.json>\n' +
      '  node scripts/image-set.mjs bundle <set.json> --output <new-web-project.json>\n' +
      'Open the manifest with desktop annotate, Web/MCP projectPath, or DSH start --project.'
    );
  } else {
    if (!file) throw new Error('A manifest path is required.');
    let output;
    if (command === 'upsert-batch' || command === 'sync') {
      let projectName = null;
      let specNameFallback = null;
      let seenName = false;
      let seenPagesJson = false;
      let seenFromDir = false;
      let full = false;
      const entries = [];
      for (let i = 0; i < args.length; ) {
        const flag = args[i];
        if (flag === '--full') {
          if (full) throw new Error('Unknown, duplicate or missing option: ' + flag);
          full = true;
          i += 1;
          continue;
        }
        const nextVal = args[i + 1];
        if (!nextVal) throw new Error('Unknown, duplicate or missing option: ' + flag);
        if (flag === '--name') {
          if (seenName) throw new Error('Unknown, duplicate or missing option: ' + flag);
          seenName = true;
          projectName = nextVal;
        } else if (flag === '--pages-json') {
          if (seenPagesJson) throw new Error('Unknown, duplicate or missing option: ' + flag);
          seenPagesJson = true;
          const loaded = loadPagesJsonSpec(nextVal);
          if (loaded.specName && !specNameFallback) specNameFallback = loaded.specName;
          entries.push(...loaded.entries);
        } else if (flag === '--from-dir') {
          if (seenFromDir) throw new Error('Unknown, duplicate or missing option: ' + flag);
          seenFromDir = true;
          entries.push(...scanPagesDir(nextVal));
        } else if (flag === '--page') {
          entries.push(parsePageSpec(nextVal));
        } else {
          throw new Error('Unknown, duplicate or missing option: ' + flag);
        }
        i += 2;
      }
      if (!entries.length) throw new Error('At least one page via --page, --pages-json, or --from-dir is required.');
      const manifestResult = upsertPages(file, entries, {createName: projectName || specNameFallback});
      output = full
        ? {ok: true, manifest: path.resolve(file), ...manifestResult}
        : {ok: true, ...compactResult(file, manifestResult, entries.length)};
    } else {
      const options = {};
      let compact = false;
      for (let i = 0; i < args.length; ) {
        const flag = args[i];
        if (flag === '--compact') {
          if (compact || command !== 'upsert') throw new Error('Unknown, duplicate or missing option: ' + flag);
          compact = true;
          i += 1;
          continue;
        }
        if (!/^--(?:name|id|image|output)$/.test(flag) || !args[i + 1] || options[flag] !== undefined) {
          throw new Error('Unknown, duplicate or missing option: ' + flag);
        }
        options[flag] = args[i + 1];
        i += 2;
      }
      let result;
      if (command === 'create') result = writeManifest(file, {schemaVersion:VERSION, name:options['--name'] || path.basename(file), pages:[]}, {create:true});
      else if (command === 'upsert') {
        if (!options['--image']) throw new Error('--image is required.');
        result = upsertPage(file, {id:options['--id'], name:options['--name'], imagePath:options['--image']});
        if (compact) result = compactResult(file, result, 1);
      } else if (command === 'remove') {
        const value = readManifest(file), index = value.pages.findIndex(page => page.id === options['--id']);
        if (index < 0) throw new Error('Page ID not found.');
        value.pages.splice(index, 1); result = writeManifest(file, value);
      } else if (command === 'bundle') {
        if (!options['--output']) throw new Error('--output is required for bundle.');
        const outFile = path.resolve(options['--output']); result = bundledProject(file);
        fs.mkdirSync(path.dirname(outFile), {recursive:true}); fs.writeFileSync(outFile, JSON.stringify(result), {flag:'wx'});
        result = {output:outFile, pageCount:result.pages.length};
      } else if (command === 'list') result = readManifest(file);
      else throw new Error('Unknown command: ' + command);
      output = {ok:true, manifest:path.resolve(file), ...result};
    }
    console.log(JSON.stringify(output));
  }
} catch (error) { console.error(JSON.stringify({ok:false, error:error.message})); process.exitCode = 1; }
