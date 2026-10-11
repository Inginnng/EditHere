'use strict';
// Install the Web skill from a writable, extracted Agent Web package.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');

const runtimeFiles = ['EditHere.html', 'agent.cjs', 'agent-session.cjs', 'preview.cjs', 'preview-server.cjs', 'src/feedback.js'];

function exists(file) {
  try { fs.lstatSync(file); return true; }
  catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}

function destination(args, environment = process.env) {
  if (args.length && (args.length !== 2 || args[0] !== '--destination')) {
    throw new Error('Usage: node install-skill.cjs [--destination ABSOLUTE_SKILL_DIRECTORY]');
  }
  const home = environment.CODEX_HOME || path.join(os.homedir(), '.codex');
  const target = args.length ? args[1] : path.join(home, 'skills', 'edithere-web');
  if (!target || !path.isAbsolute(target)) throw new Error('The skill destination must be an absolute directory.');
  return path.resolve(target);
}

function skillFiles(directory, relative = '') {
  const result = [];
  for (const entry of fs.readdirSync(path.join(directory, relative), {withFileTypes:true})) {
    const name = path.join(relative, entry.name);
    if (entry.isDirectory()) result.push(...skillFiles(directory, name));
    else if (entry.isFile()) result.push(name);
    else throw new Error('Skill package contains an unsupported file: ' + name);
  }
  return result;
}

function install(target, directory = __dirname) {
  if (!path.isAbsolute(target)) throw new Error('The skill destination must be an absolute directory.');
  target = path.resolve(target);
  directory = path.resolve(directory);
  for (const relative of runtimeFiles) {
    const file = path.join(directory, relative);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) throw new Error('Required Web runtime file is missing: ' + relative);
  }
  const source = path.join(directory, 'skills', 'edithere-web');
  if (!fs.existsSync(path.join(source, 'SKILL.md'))) throw new Error('Required skill package is missing: skills/edithere-web/SKILL.md');
  const files = skillFiles(source);
  if (exists(target)) throw new Error('Skill destination already exists; nothing was changed: ' + target);

  const parent = path.dirname(target);
  fs.mkdirSync(parent, {recursive:true});
  const staging = path.join(parent, '.edithere-web-install-' + crypto.randomUUID());
  if (path.dirname(path.resolve(staging)) !== parent) throw new Error('Invalid installation staging directory.');
  fs.mkdirSync(staging);
  try {
    for (const relative of files) {
      const output = path.join(staging, relative);
      fs.mkdirSync(path.dirname(output), {recursive:true});
      fs.copyFileSync(path.join(source, relative), output, fs.constants.COPYFILE_EXCL);
    }
    const cli = path.join(directory, 'agent.cjs');
    const reference = '# 本机网页入口\n\n' +
      '安装时已验证的网页会话 CLI：\n\n```text\n' + cli + '\n```\n\n' +
      '本机 Node.js：\n\n```text\n' + process.execPath + '\n```\n\n' +
      '此技能指向当前解压目录，不复制运行时，也不修改桌面 EditHere 技能。保留该目录中的网页与服务文件，' +
      '并确保目录可写；会话状态和日志会在其中生成。移动或删除运行时后需重新定位入口。\n';
    fs.mkdirSync(path.join(staging, 'references'), {recursive:true});
    fs.writeFileSync(path.join(staging, 'references', 'local-installation.md'), reference, {encoding:'utf8'});
    // mkdir is the no-overwrite claim: rename can replace an empty directory on Unix.
    fs.mkdirSync(target);
    try {
      for (const entry of fs.readdirSync(staging)) fs.renameSync(path.join(staging, entry), path.join(target, entry));
    } catch (error) {
      // Only this invocation created target; remove its incomplete installation.
      fs.rmSync(target, {recursive:true, force:true});
      throw error;
    }
    return {state:'installed', destination:target, cli, node:process.execPath};
  } finally {
    fs.rmSync(staging, {recursive:true, force:true});
  }
}

if (require.main === module) {
  try { console.log(JSON.stringify(install(destination(process.argv.slice(2))))); }
  catch (error) { console.error(JSON.stringify({error:error.message})); process.exitCode = 1; }
}
module.exports = {destination, install, runtimeFiles};
