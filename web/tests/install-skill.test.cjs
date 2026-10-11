'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {runtimeFiles} = require('../install-skill.cjs');

function fixture(t) {
  const parent = path.resolve(__dirname, 'output', 'skill-install-tests');
  fs.mkdirSync(parent, {recursive:true});
  const root = fs.mkdtempSync(path.join(parent, 'fixture-'));
  t.after(() => {
    assert.equal(path.dirname(path.resolve(root)), parent);
    fs.rmSync(root, {recursive:true, force:true});
  });
  const web = path.join(root, '中文 Agent Web 包', 'web');
  fs.mkdirSync(web, {recursive:true});
  fs.copyFileSync(path.join(__dirname, '../install-skill.cjs'), path.join(web, 'install-skill.cjs'));
  for (const relative of runtimeFiles) {
    const file = path.join(web, relative);
    fs.mkdirSync(path.dirname(file), {recursive:true});
    fs.writeFileSync(file, 'runtime fixture\n');
  }
  const skill = path.join(web, 'skills', 'edithere-web');
  fs.mkdirSync(path.join(skill, 'agents'), {recursive:true});
  fs.mkdirSync(path.join(skill, 'references'), {recursive:true});
  fs.writeFileSync(path.join(skill, 'SKILL.md'), '---\nname: edithere-web\ndescription: Test fixture\n---\n');
  fs.writeFileSync(path.join(skill, 'agents', 'openai.yaml'), 'interface: {}\n');
  fs.writeFileSync(path.join(skill, 'references', 'capture.md'), 'capture fixture\n');
  fs.writeFileSync(path.join(skill, 'references', 'local-installation.md'), 'old placeholder\n');
  return {root, web, target:path.join(root, '测试 技能目录', 'edithere-web')};
}

function run(web, args, environment = {}) {
  return spawnSync(process.execPath, [path.join(web, 'install-skill.cjs'), ...args], {
    encoding:'utf8', env:{...process.env, ...environment}
  });
}

test('installs from Chinese and spaced paths and records this runtime without changing the desktop skill', t => {
  const {web, target} = fixture(t);
  const desktop = path.join(path.dirname(target), 'edithere', 'SKILL.md');
  fs.mkdirSync(path.dirname(desktop), {recursive:true});
  fs.writeFileSync(desktop, 'preserve desktop skill\n');
  const result = run(web, ['--destination', target]);
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(JSON.parse(result.stdout), {state:'installed', destination:target, cli:path.join(web, 'agent.cjs'), node:process.execPath});
  const reference = fs.readFileSync(path.join(target, 'references', 'local-installation.md'), 'utf8');
  assert.ok(reference.includes(path.join(web, 'agent.cjs')));
  assert.ok(reference.includes(process.execPath));
  assert.ok(!reference.includes('old placeholder'));
  assert.equal(fs.readFileSync(path.join(target, 'references', 'capture.md'), 'utf8'), 'capture fixture\n');
  assert.equal(fs.readFileSync(desktop, 'utf8'), 'preserve desktop skill\n');
});

test('uses CODEX_HOME by default', t => {
  const {root, web} = fixture(t);
  const home = path.join(root, 'Codex home 中文');
  const result = run(web, [], {CODEX_HOME:home});
  assert.equal(result.status, 0, result.stderr);
  assert.equal(JSON.parse(result.stdout).destination, path.join(home, 'skills', 'edithere-web'));
});

test('refuses an existing destination, including an empty directory, without changing it', t => {
  const {web, target} = fixture(t);
  fs.mkdirSync(target, {recursive:true});
  let result = run(web, ['--destination', target]);
  assert.equal(result.status, 1);
  assert.match(JSON.parse(result.stderr).error, /already exists/);
  assert.deepEqual(fs.readdirSync(target), []);
  fs.writeFileSync(path.join(target, 'SKILL.md'), 'unique user changes');
  result = run(web, ['--destination', target]);
  assert.equal(result.status, 1);
  assert.equal(fs.readFileSync(path.join(target, 'SKILL.md'), 'utf8'), 'unique user changes');
});

test('refuses a missing feedback runtime before creating the skill destination', t => {
  const {web, target} = fixture(t);
  fs.unlinkSync(path.join(web, 'src', 'feedback.js'));
  const result = run(web, ['--destination', target]);
  assert.equal(result.status, 1);
  assert.match(JSON.parse(result.stderr).error, /src[\\/]feedback\.js/);
  assert.equal(fs.existsSync(path.dirname(target)), false);
});

test('rejects a relative destination', t => {
  const {web} = fixture(t);
  const result = run(web, ['--destination', 'relative-skill']);
  assert.equal(result.status, 1);
  assert.match(JSON.parse(result.stderr).error, /absolute/);
});
