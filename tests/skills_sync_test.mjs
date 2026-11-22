import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const SKILL_TARGETS = [
  'skills/edithere',
  'connector/skills/edithere',
  'trae-plugin/skills/edithere',
  'web/skills/edithere-web',
  'dsh/skill/edithere-dsh',
];

const SHARED_REFERENCES = [
  'references/project-review.md',
  'references/capture.md',
  'references/layout-translation.md',
  'references/feedback-format.md',
  'references/troubleshooting.md',
];

const SHARED_SCRIPTS = [
  'scripts/image-set.mjs',
  'scripts/image-set.cjs',
  'scripts/capture-web.mjs',
];

function readLf(relativePath) {
  return fs.readFileSync(path.join(ROOT, relativePath), 'utf8').replace(/\r\n/g, '\n');
}

test('single-source build-skills --check passes with zero drift', () => {
  const result = spawnSync(process.execPath, [path.join(ROOT, 'scripts', 'build-skills.mjs'), '--check'], {
    cwd: ROOT,
    encoding: 'utf8',
    windowsHide: true,
  });
  assert.equal(result.status, 0, result.stderr || result.stdout);
  const receipt = JSON.parse(result.stdout);
  assert.equal(receipt.ok, true);
  assert.equal(receipt.targets, SKILL_TARGETS.length);
});

test('all 5 target SKILL.md files are concise, structured, and link all shared references', () => {
  for (const target of SKILL_TARGETS) {
    const content = readLf(`${target}/SKILL.md`);
    const lines = content.trimEnd().split('\n');
    assert.ok(lines.length <= 125, `${target}/SKILL.md should stay <= 125 lines (got ${lines.length})`);
    assert.match(content, /^---\nname:\s+\S+/);
    assert.equal(content.includes('<image-or-project>'), false, `${target}/SKILL.md must not include <image-or-project>`);
    for (const ref of SHARED_REFERENCES) {
      assert.ok(content.includes(ref), `${target}/SKILL.md must link ${ref}`);
    }
  }
});

test('all 5 targets share identical references and scripts and sync root docs/scripts', () => {
  for (const ref of SHARED_REFERENCES) {
    const expected = readLf(`skills-src/shared/${ref}`);
    for (const target of SKILL_TARGETS) {
      assert.equal(readLf(`${target}/${ref}`), expected, `${target}/${ref} drifted from skills-src`);
    }
  }
  assert.equal(readLf('docs/PROJECT-REVIEW.md'), readLf('skills-src/shared/references/project-review.md'));

  for (const script of SHARED_SCRIPTS) {
    const expected = script.endsWith('image-set.cjs')
      ? readLf('web/image-set.cjs')
      : readLf(`skills-src/shared/${script}`);
    for (const target of SKILL_TARGETS) {
      assert.equal(readLf(`${target}/${script}`), expected, `${target}/${script} drifted from source`);
    }
  }
  assert.equal(readLf('scripts/capture-web.mjs'), readLf('skills-src/shared/scripts/capture-web.mjs'));
});

test('domain guides enforce local vs design-system separation and code-impact-driven refresh', () => {
  const layoutGuide = readLf('skills-src/shared/references/layout-translation.md');
  assert.ok(layoutGuide.includes('局部微调'));
  assert.ok(layoutGuide.includes('设计系统级调整'));
  assert.ok(layoutGuide.includes('position: absolute'));

  const reviewGuide = readLf('skills-src/shared/references/project-review.md');
  assert.ok(reviewGuide.includes('受影响页面清单'));
  assert.ok(reviewGuide.includes('必换页'));
  assert.ok(reviewGuide.includes('波及页'));
  assert.ok(reviewGuide.includes('不盲目对整组页面重截或做全量像素比对'));
});

test('capture-web.mjs validates arguments cleanly and runs standalone outside the repository', (t) => {
  const tempRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-skill-standalone-'));
  t.after(() => fs.rmSync(tempRoot, { recursive: true, force: true }));

  const standaloneSkill = path.join(tempRoot, 'edithere');
  fs.cpSync(path.join(ROOT, 'skills', 'edithere'), standaloneSkill, { recursive: true });
  const captureCli = path.join(standaloneSkill, 'scripts', 'capture-web.mjs');
  const imageSetCli = path.join(standaloneSkill, 'scripts', 'image-set.mjs');

  const helpOut = execFileSync(process.execPath, [captureCli, '--help'], { encoding: 'utf8', windowsHide: true });
  assert.match(helpOut, /EditHere web screenshot capture/);

  const badCases = [
    [],
    ['--url', 'ftp://example.com', '--output', path.join(tempRoot, 'a.png')],
    ['--url', 'http://127.0.0.1:3000', '--output', path.join(tempRoot, 'a.png'), '--viewport', 'invalid'],
    ['--url', 'http://127.0.0.1:3000', '--output', path.join(tempRoot, 'a.png'), '--manifest', path.join(tempRoot, 'set.json')],
  ];
  for (const args of badCases) {
    const res = spawnSync(process.execPath, [captureCli, ...args], {
      cwd: tempRoot,
      encoding: 'utf8',
      windowsHide: true,
    });
    assert.equal(res.status, 1, `Expected exit 1 for args: ${args.join(' ')}`);
    const payload = JSON.parse(res.stderr);
    assert.equal(payload.ok, false);
    assert.equal(payload.code, 'invalid_argument');
  }

  // Missing manifest without --manifest-name fails cleanly even before Playwright launch.
  const manifestPath = path.join(tempRoot, 'results', 'project.json');
  const missingManifestRes = spawnSync(
    process.execPath,
    [captureCli, '--url', 'http://127.0.0.1:3000', '--output', path.join(tempRoot, 'shot.png'), '--manifest', manifestPath, '--id', 'home'],
    { cwd: tempRoot, encoding: 'utf8', windowsHide: true }
  );
  assert.equal(missingManifestRes.status, 1);
  assert.equal(JSON.parse(missingManifestRes.stderr).code, 'manifest_not_found');

  // Missing Playwright inside an isolated temp dir must fail cleanly with playwright_not_found
  // without leaving behind a half-created manifest file.
  const noPwRes = spawnSync(
    process.execPath,
    [
      captureCli,
      '--url', 'http://127.0.0.1:3000',
      '--output', path.join(tempRoot, 'shot.png'),
      '--manifest', manifestPath,
      '--manifest-name', 'Standalone Review',
      '--id', 'home',
    ],
    { cwd: tempRoot, encoding: 'utf8', windowsHide: true }
  );
  assert.equal(noPwRes.status, 1);
  const noPwJson = JSON.parse(noPwRes.stderr);
  assert.equal(noPwJson.ok, false);
  assert.equal(noPwJson.code, 'playwright_not_found');
  assert.equal(fs.existsSync(manifestPath), false);

  const created = JSON.parse(execFileSync(process.execPath, [imageSetCli, 'create', manifestPath, '--name', 'Standalone Review'], {
    cwd: tempRoot,
    encoding: 'utf8',
    windowsHide: true,
  }));
  assert.equal(created.ok, true);
  const listed = JSON.parse(execFileSync(process.execPath, [imageSetCli, 'list', manifestPath], {
    cwd: tempRoot,
    encoding: 'utf8',
    windowsHide: true,
  }));
  assert.equal(listed.ok, true);
  assert.equal(listed.name, 'Standalone Review');
});
