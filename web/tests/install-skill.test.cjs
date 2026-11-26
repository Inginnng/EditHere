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
  fs.mkdirSync(path.join(skill, 'scripts'));
  for (const name of ['image-set.mjs','image-set.cjs']) {
    fs.copyFileSync(path.join(__dirname, '../skills/edithere-web/scripts', name), path.join(skill, 'scripts', name));
  }
  return {root, web, target:path.join(root, '测试 技能目录', 'edithere-web')};
}

function run(web, args, environment = {}) {
  return spawnSync(process.execPath, [path.join(web, 'install-skill.cjs'), ...args], {
    encoding:'utf8', env:{...process.env, ...environment}
  });
}

test('installs from Chinese and spaced paths and records this runtime without changing the desktop skill', t => {
  const {root, web, target} = fixture(t);
  const desktop = path.join(path.dirname(target), 'edithere', 'SKILL.md');
  fs.mkdirSync(path.dirname(desktop), {recursive:true});
  fs.writeFileSync(desktop, 'preserve desktop skill\n');
  const result = run(web, ['--destination', target]);
  assert.equal(result.status, 0, result.stderr);
  const receipt = JSON.parse(result.stdout);
  const mcpServer = path.join(web, 'mcp', 'server.bundle.cjs');
  assert.deepEqual(receipt, {state:'installed', destination:target, cli:path.join(web, 'agent.cjs'), node:process.execPath,
    mcpServer, mcpConfig:{mcpServers:{'edithere-web':{command:process.execPath, args:[mcpServer]}}}});
  const reference = fs.readFileSync(path.join(target, 'references', 'local-installation.md'), 'utf8');
  assert.ok(reference.includes(path.join(web, 'agent.cjs')));
  assert.ok(reference.includes(process.execPath));
  assert.ok(reference.includes(mcpServer));
  const jsonConfig = reference.match(/```json\n([\s\S]*?)\n```/);
  assert.ok(jsonConfig, 'the installed reference provides a usable MCP entry');
  assert.deepEqual(JSON.parse(jsonConfig[1]), receipt.mcpConfig);
  assert.ok(!reference.includes('old placeholder'));
  assert.equal(fs.readFileSync(path.join(target, 'references', 'capture.md'), 'utf8'), 'capture fixture\n');
  assert.equal(fs.readFileSync(desktop, 'utf8'), 'preserve desktop skill\n');
  const manifest=path.join(root,'installed-review.json'),image=path.resolve(__dirname,'../../tests/fixtures/ocr-small-url.png');
  const helper=path.join(target,'scripts','image-set.mjs');
  const runHelper=(...args)=>{
    const command=spawnSync(process.execPath,[helper,...args],{cwd:target,encoding:'utf8',windowsHide:true});
    assert.equal(command.status,0,command.stderr);
    return JSON.parse(command.stdout);
  };
  runHelper('create',manifest,'--name','Installed skill review');
  runHelper('upsert',manifest,'--id','settings.ai','--image',image);
  const bundled=path.join(root,'installed-review-bundle.json');
  assert.equal(runHelper('bundle',manifest,'--output',bundled).pageCount,1);
  assert.equal(JSON.parse(fs.readFileSync(bundled,'utf8')).pages[0].id,'settings.ai');
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

test('refuses an incomplete MCP package before installing a misleading MCP skill', t => {
  const {web, target} = fixture(t);
  fs.unlinkSync(path.join(web, 'mcp', 'server.bundle.cjs'));
  const result = run(web, ['--destination', target]);
  assert.equal(result.status, 1);
  assert.match(JSON.parse(result.stderr).error, /mcp[\\/]server\.bundle\.cjs/);
  assert.equal(fs.existsSync(path.dirname(target)), false);
});

test('rejects a relative destination', t => {
  const {web} = fixture(t);
  const result = run(web, ['--destination', 'relative-skill']);
  assert.equal(result.status, 1);
  assert.match(JSON.parse(result.stderr).error, /absolute/);
});

test('supports --update and --force with skillVersion checks and safe downgrade protection', t => {
  const {root, web, target} = fixture(t);
  fs.writeFileSync(path.join(web, 'skills', 'edithere-web', 'skill-version.json'), JSON.stringify({skillVersion:'1.2.0', minCompatibleSkillVersion:'1.0.0'}));

  const first = run(web, ['--destination', target]);
  assert.equal(first.status, 0, first.stderr);
  const metaPath = path.join(target, '.edithere-skill.json');
  assert.equal(JSON.parse(fs.readFileSync(metaPath, 'utf8')).skillVersion, '1.2.0');

  // Same version & valid runtime path -> up-to-date no-op
  const upToDate = run(web, ['--destination', target, '--update']);
  assert.equal(upToDate.status, 0, upToDate.stderr);
  assert.equal(JSON.parse(upToDate.stdout).state, 'up-to-date');

  // Outdated version -> atomic replacement preserving no old files
  fs.writeFileSync(path.join(target, 'old-junk.txt'), 'delete me');
  fs.writeFileSync(metaPath, JSON.stringify({skillVersion:'1.0.0', cli:path.join(web, 'agent.cjs'), mcpServer:path.join(web, 'mcp', 'server.bundle.cjs')}));
  const upgraded = run(web, ['--destination', target, '--update']);
  assert.equal(upgraded.status, 0, upgraded.stderr);
  assert.equal(JSON.parse(upgraded.stdout).state, 'updated');
  assert.equal(fs.existsSync(path.join(target, 'old-junk.txt')), false);

  // Moved runtime / stale path -> updated
  fs.writeFileSync(metaPath, JSON.stringify({skillVersion:'1.2.0', cli:path.join(root, 'moved/agent.cjs'), mcpServer:path.join(web, 'mcp', 'server.bundle.cjs')}));
  const pathFixed = run(web, ['--destination', target, '--update']);
  assert.equal(pathFixed.status, 0, pathFixed.stderr);
  assert.equal(JSON.parse(pathFixed.stdout).state, 'updated');

  // Legacy (no .edithere-skill.json) -> updated
  fs.unlinkSync(metaPath);
  const legacyFixed = run(web, ['--destination', target, '--update']);
  assert.equal(legacyFixed.status, 0, legacyFixed.stderr);
  assert.equal(JSON.parse(legacyFixed.stdout).state, 'updated');

  // Higher installed skillVersion -> skipped-newer unless --force
  fs.writeFileSync(metaPath, JSON.stringify({skillVersion:'9.0.0', cli:path.join(web, 'agent.cjs'), mcpServer:path.join(web, 'mcp', 'server.bundle.cjs')}));
  const skipped = run(web, ['--destination', target, '--update']);
  assert.equal(skipped.status, 0, skipped.stderr);
  assert.equal(JSON.parse(skipped.stdout).state, 'skipped-newer');
  assert.equal(JSON.parse(fs.readFileSync(metaPath, 'utf8')).skillVersion, '9.0.0');

  const forced = run(web, ['--destination', target, '--force']);
  assert.equal(forced.status, 0, forced.stderr);
  assert.equal(JSON.parse(forced.stdout).state, 'updated');
  assert.equal(JSON.parse(fs.readFileSync(metaPath, 'utf8')).skillVersion, '1.2.0');
});
