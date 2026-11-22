#!/usr/bin/env node
/**
 * Build and verify all 5 EditHere skill packages from the single-source skills-src/ tree.
 *
 * Usage:
 *   node scripts/build-skills.mjs          Generate/sync all target skill files and docs/PROJECT-REVIEW.md
 *   node scripts/build-skills.mjs --check  Verify zero drift (exit 1 if any target file is missing, stale, or stray)
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SRC = path.join(ROOT, 'skills-src');

const TARGET_SPECS = [
  { id: 'desktop-cli', dir: 'skills/edithere', template: 'desktop-cli.md.tmpl' },
  { id: 'desktop-mcp', dir: 'connector/skills/edithere', template: 'desktop-mcp.md.tmpl' },
  { id: 'trae-plugin', dir: 'trae-plugin/skills/edithere', template: 'trae-plugin.md.tmpl' },
  { id: 'web', dir: 'web/skills/edithere-web', template: 'web.md.tmpl', extrasDir: 'web-extras' },
  { id: 'dsh', dir: 'dsh/skill/edithere-dsh', template: 'dsh.md.tmpl' },
];

const SHARED_REFERENCES = [
  'project-review.md',
  'capture.md',
  'layout-translation.md',
  'feedback-format.md',
  'troubleshooting.md',
];

const SHARED_SCRIPTS = [
  { name: 'image-set.mjs', src: path.join(SRC, 'shared', 'scripts', 'image-set.mjs') },
  { name: 'image-set.cjs', src: path.join(ROOT, 'web', 'image-set.cjs') },
  { name: 'capture-web.mjs', src: path.join(SRC, 'shared', 'scripts', 'capture-web.mjs') },
];

function normalizeLf(text) {
  return String(text).replace(/\r\n/g, '\n');
}

function sha256Lf(content) {
  return crypto.createHash('sha256').update(normalizeLf(content), 'utf8').digest('hex');
}

function readUtf8Lf(filePath) {
  return normalizeLf(fs.readFileSync(filePath, 'utf8'));
}

function loadPartials() {
  const partialsDir = path.join(SRC, 'shared', 'partials');
  const map = new Map();
  for (const entry of fs.readdirSync(partialsDir, { withFileTypes: true })) {
    if (!entry.isFile() || !entry.name.endsWith('.tmpl')) continue;
    const key = entry.name.slice(0, -'.tmpl'.length);
    map.set(key, readUtf8Lf(path.join(partialsDir, entry.name)).trimEnd());
  }
  return map;
}

function renderTemplate(templatePath, partials) {
  const raw = readUtf8Lf(templatePath);
  const rendered = raw.replace(/\{\{PARTIAL:([a-zA-Z0-9_-]+)\}\}/g, (_, name) => {
    if (!partials.has(name)) {
      throw new Error(`Unknown partial "{{PARTIAL:${name}}}" referenced in ${path.relative(ROOT, templatePath)}`);
    }
    return partials.get(name);
  });
  if (/\{\{PARTIAL:[^}]*\}\}/.test(rendered)) {
    throw new Error(`Nested or malformed partial placeholder left in ${path.relative(ROOT, templatePath)}`);
  }
  return rendered.endsWith('\n') ? rendered : `${rendered}\n`;
}

function walkRelativeFiles(rootDir, relative = '') {
  const full = path.join(rootDir, relative);
  if (!fs.existsSync(full)) return [];
  const results = [];
  for (const entry of fs.readdirSync(full, { withFileTypes: true })) {
    const rel = relative ? `${relative}/${entry.name}` : entry.name;
    if (entry.isDirectory()) {
      results.push(...walkRelativeFiles(rootDir, rel));
    } else if (entry.isFile()) {
      results.push(rel);
    }
  }
  return results.sort();
}

function buildManifest() {
  const partials = loadPartials();
  const files = new Map(); // repo-relative posix path -> string content (LF)
  const targetOwnedDirs = [];

  for (const spec of TARGET_SPECS) {
    const targetPrefix = spec.dir;
    const expectedSubfiles = new Set();

    const skillContent = renderTemplate(path.join(SRC, 'targets', spec.template), partials);
    files.set(`${targetPrefix}/SKILL.md`, skillContent);
    expectedSubfiles.add('SKILL.md');

    for (const refName of SHARED_REFERENCES) {
      const content = readUtf8Lf(path.join(SRC, 'shared', 'references', refName));
      files.set(`${targetPrefix}/references/${refName}`, content);
      expectedSubfiles.add(`references/${refName}`);
    }

    for (const script of SHARED_SCRIPTS) {
      const content = readUtf8Lf(script.src);
      files.set(`${targetPrefix}/scripts/${script.name}`, content);
      expectedSubfiles.add(`scripts/${script.name}`);
    }

    if (spec.extrasDir) {
      const extrasRoot = path.join(SRC, 'targets', spec.extrasDir);
      for (const rel of walkRelativeFiles(extrasRoot)) {
        const content = readUtf8Lf(path.join(extrasRoot, rel));
        files.set(`${targetPrefix}/${rel}`, content);
        expectedSubfiles.add(rel);
      }
    }

    targetOwnedDirs.push({ targetPrefix, expectedSubfiles });
  }

  files.set('docs/PROJECT-REVIEW.en.md', readUtf8Lf(path.join(SRC, 'shared', 'references', 'project-review.md')));
  files.set('scripts/capture-web.mjs', readUtf8Lf(path.join(SRC, 'shared', 'scripts', 'capture-web.mjs')));

  return { files, targetOwnedDirs };
}

function runCheck() {
  const { files, targetOwnedDirs } = buildManifest();
  const drifted = [];
  const missing = [];
  const extra = [];

  for (const [relPath, expected] of files.entries()) {
    const absPath = path.join(ROOT, relPath);
    if (!fs.existsSync(absPath)) {
      missing.push(relPath);
      continue;
    }
    const actual = readUtf8Lf(absPath);
    if (sha256Lf(actual) !== sha256Lf(expected)) {
      drifted.push(relPath);
    }
  }

  for (const { targetPrefix, expectedSubfiles } of targetOwnedDirs) {
    const targetAbs = path.join(ROOT, targetPrefix);
    for (const rel of walkRelativeFiles(targetAbs)) {
      if (!expectedSubfiles.has(rel)) {
        extra.push(`${targetPrefix}/${rel}`);
      }
    }
  }

  if (missing.length || drifted.length || extra.length) {
    console.error(JSON.stringify({
      ok: false,
      missing,
      drifted,
      extra,
      hint: 'Run "node scripts/build-skills.mjs" to regenerate target skill files.',
    }, null, 2));
    process.exitCode = 1;
    return;
  }

  console.log(JSON.stringify({
    ok: true,
    checked: files.size,
    targets: TARGET_SPECS.length,
  }));
}

function runWrite() {
  const { files, targetOwnedDirs } = buildManifest();
  let written = 0;
  let unchanged = 0;
  let removed = 0;

  for (const { targetPrefix, expectedSubfiles } of targetOwnedDirs) {
    const targetAbs = path.join(ROOT, targetPrefix);
    for (const rel of walkRelativeFiles(targetAbs)) {
      if (!expectedSubfiles.has(rel)) {
        fs.unlinkSync(path.join(targetAbs, rel));
        removed += 1;
      }
    }
  }

  for (const [relPath, content] of files.entries()) {
    const absPath = path.join(ROOT, relPath);
    fs.mkdirSync(path.dirname(absPath), { recursive: true });
    if (fs.existsSync(absPath)) {
      const currentRaw = fs.readFileSync(absPath, 'utf8');
      if (currentRaw === content) {
        unchanged += 1;
        continue;
      }
    }
    fs.writeFileSync(absPath, content, { encoding: 'utf8' });
    written += 1;
  }

  console.log(JSON.stringify({
    ok: true,
    written,
    unchanged,
    removed,
    total: files.size,
    targets: TARGET_SPECS.length,
  }));
}

const args = process.argv.slice(2);
if (args.includes('--help') || args.includes('-h')) {
  console.log('Usage: node scripts/build-skills.mjs [--check]');
} else if (args.includes('--check')) {
  runCheck();
} else if (args.length === 0) {
  runWrite();
} else {
  console.error(JSON.stringify({ ok: false, error: `Unknown argument(s): ${args.join(' ')}` }));
  process.exitCode = 2;
}
