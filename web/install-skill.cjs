'use strict';
// Install or update the Web skill from a writable, extracted Agent Web package.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');

const runtimeFiles = ['EditHere.html', 'EditHere.mcp.html', 'agent.cjs', 'agent-session.cjs', 'image-set.cjs', 'preview.cjs', 'preview-server.cjs', 'src/feedback.js', 'src/layout.js', 'src/image-format.js', 'src/bundle.js', 'mcp/server.bundle.cjs', 'mcp/mcp.json', 'mcp/build-info.json', 'MCP-THIRD-PARTY-NOTICES.md'];
const SEMVER_RE = /^\d+\.\d+\.\d+$/;

function isValidSemver(value) {
  return typeof value === 'string' && SEMVER_RE.test(value.trim());
}

function compareSemver(a, b) {
  const parse = (v) => (isValidSemver(v) ? v.trim().split('.').map(Number) : [0, 0, 0]);
  const pa = parse(a);
  const pb = parse(b);
  for (let i = 0; i < 3; i += 1) {
    if (pa[i] < pb[i]) return -1;
    if (pa[i] > pb[i]) return 1;
  }
  return 0;
}

function samePath(a, b) {
  if (!a || !b) return false;
  const resolveSafe = (p) => {
    const abs = path.resolve(p);
    try { return fs.realpathSync(abs); } catch { return abs; }
  };
  const ra = resolveSafe(a);
  const rb = resolveSafe(b);
  return process.platform === 'win32' ? ra.toLowerCase() === rb.toLowerCase() : ra === rb;
}

function exists(file) {
  try { fs.lstatSync(file); return true; }
  catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}

function parseArgs(args, environment = process.env) {
  let dest = null;
  let update = false;
  let force = false;
  for (let i = 0; i < args.length; i += 1) {
    const token = args[i];
    if (token === '--update') { update = true; continue; }
    if (token === '--force') { force = true; continue; }
    if (token === '--destination') {
      if (dest !== null || i + 1 >= args.length || !args[i + 1]) {
        throw new Error('Usage: node install-skill.cjs [--destination ABSOLUTE_SKILL_DIRECTORY] [--update] [--force]');
      }
      dest = args[++i];
      continue;
    }
    throw new Error('Usage: node install-skill.cjs [--destination ABSOLUTE_SKILL_DIRECTORY] [--update] [--force]');
  }
  const home = environment.CODEX_HOME || path.join(os.homedir(), '.codex');
  const target = dest !== null ? dest : path.join(home, 'skills', 'edithere-web');
  if (!target || !path.isAbsolute(target)) throw new Error('The skill destination must be an absolute directory.');
  return {destination: path.resolve(target), update, force};
}

function destination(args, environment = process.env) {
  return parseArgs(args, environment).destination;
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

function readBundledSkillVersion(sourceDir) {
  for (const candidate of [path.join(sourceDir, 'skill-version.json'), path.resolve(__dirname, '..', 'skills-src', 'skill-version.json')]) {
    try {
      const parsed = JSON.parse(fs.readFileSync(candidate, 'utf8').replace(/^\uFEFF/, ''));
      if (isValidSemver(parsed?.skillVersion)) return parsed.skillVersion.trim();
    } catch {}
  }
  return '1.0.0';
}

function inspectInstalledWebSkill(target, bundledVersion, cli, mcpServer) {
  if (!fs.existsSync(path.join(target, 'SKILL.md'))) return {status: 'legacy', installedVersion: null};
  let meta = null;
  try {
    meta = JSON.parse(fs.readFileSync(path.join(target, '.edithere-skill.json'), 'utf8').replace(/^\uFEFF/, ''));
  } catch {}
  if (!meta || !isValidSemver(meta.skillVersion)) return {status: 'legacy', installedVersion: null};
  const installedVersion = meta.skillVersion.trim();
  const cmp = compareSemver(installedVersion, bundledVersion);
  if (cmp < 0) return {status: 'outdated', installedVersion};
  if (cmp > 0) return {status: 'newer', installedVersion};
  const recCli = typeof meta.cli === 'string' ? meta.cli : '';
  const recMcp = typeof meta.mcpServer === 'string' ? meta.mcpServer : '';
  if (!recCli || !recMcp || !fs.existsSync(recCli) || !fs.existsSync(recMcp) || !samePath(recCli, cli) || !samePath(recMcp, mcpServer)) {
    return {status: 'path-stale', installedVersion};
  }
  return {status: 'up-to-date', installedVersion};
}

function install(target, directory = __dirname, {update = false, force = false} = {}) {
  if (!path.isAbsolute(target)) throw new Error('The skill destination must be an absolute directory.');
  target = path.resolve(target);
  directory = path.resolve(directory);
  for (const relative of runtimeFiles) {
    const file = path.join(directory, relative);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) throw new Error('Required Web runtime file is missing: ' + relative);
  }
  const source = path.join(directory, 'skills', 'edithere-web');
  if (!fs.existsSync(path.join(source, 'SKILL.md'))) throw new Error('Required skill package is missing: skills/edithere-web/SKILL.md');
  const bundledVersion = readBundledSkillVersion(source);
  const cli = path.join(directory, 'agent.cjs');
  const mcpServer = path.join(directory, 'mcp', 'server.bundle.cjs');
  const mcpConfig = {mcpServers:{'edithere-web':{command:process.execPath, args:[mcpServer]}}};

  const files = skillFiles(source).filter((rel) => {
    const normalized = rel.split(path.sep).join('/');
    return normalized !== '.edithere-skill.json' && normalized !== 'references/local-installation.md';
  });

  const hadExisting = exists(target);
  let installedInfo = {status: 'not-installed', installedVersion: null};
  if (hadExisting) {
    if (!update && !force) throw new Error('Skill destination already exists; nothing was changed: ' + target);
    installedInfo = inspectInstalledWebSkill(target, bundledVersion, cli, mcpServer);
    if (update && !force) {
      if (installedInfo.status === 'up-to-date') {
        return {state:'up-to-date', destination:target, skillVersion:bundledVersion, cli, node:process.execPath, mcpServer, mcpConfig};
      }
      if (installedInfo.status === 'newer') {
        return {state:'skipped-newer', destination:target, installedVersion:installedInfo.installedVersion, bundledVersion, cli, node:process.execPath, mcpServer, mcpConfig};
      }
    }
  }

  const parent = path.dirname(target);
  fs.mkdirSync(parent, {recursive:true});
  const suffix = crypto.randomUUID();
  const staging = path.join(parent, '.edithere-web-install-' + suffix);
  const backup = path.join(parent, '.edithere-web-old-' + suffix);
  if (path.dirname(path.resolve(staging)) !== parent) throw new Error('Invalid installation staging directory.');
  fs.mkdirSync(staging);
  try {
    for (const relative of files) {
      const output = path.join(staging, relative);
      fs.mkdirSync(path.dirname(output), {recursive:true});
      fs.copyFileSync(path.join(source, relative), output, fs.constants.COPYFILE_EXCL);
    }
    const reference = '# 本机网页入口\n\n' +
      '安装时已核对文件的 MCP 服务（Skill v' + bundledVersion + '，需要 Node.js 20+）：\n\n```text\n' + mcpServer + '\n```\n\n' +
      '本机 Node.js：\n\n```text\n' + process.execPath + '\n```\n\n' +
      '宿主 MCP 配置参考（按宿主格式合并，安装器未修改宿主配置或验证连接）：\n\n```json\n' + JSON.stringify(mcpConfig, null, 2) + '\n```\n\n' +
      '普通浏览器兼容 CLI（Node.js 18+）：\n\n```text\n' + cli + '\n```\n\n' +
      '此技能指向当前解压目录，不复制运行时，也不修改桌面 EditHere 技能。保留该目录中的网页与服务文件。' +
      'MCP 会话默认位于 ~/.edithere/web-sessions，可用 EDITHERE_WEB_DATA_DIR 指定持久目录。' +
      '移动或删除运行时后需重新定位入口。\n';
    fs.mkdirSync(path.join(staging, 'references'), {recursive:true});
    fs.writeFileSync(path.join(staging, 'references', 'local-installation.md'), reference, {encoding:'utf8'});
    fs.writeFileSync(
      path.join(staging, '.edithere-skill.json'),
      JSON.stringify({schemaVersion:1, skillVersion:bundledVersion, channel:'web', cli, mcpServer, installedAt:new Date().toISOString()}, null, 2) + '\n',
      {encoding:'utf8'}
    );

    if (hadExisting) {
      fs.renameSync(target, backup);
      try {
        fs.renameSync(staging, target);
      } catch (swapError) {
        try { fs.renameSync(backup, target); } catch {}
        throw swapError;
      }
      fs.rmSync(backup, {recursive:true, force:true});
      return {state:'updated', destination:target, fromVersion:installedInfo.installedVersion, skillVersion:bundledVersion, cli, node:process.execPath, mcpServer, mcpConfig};
    }

    // mkdir is the no-overwrite claim: rename can replace an empty directory on Unix.
    fs.mkdirSync(target);
    try {
      for (const entry of fs.readdirSync(staging)) fs.renameSync(path.join(staging, entry), path.join(target, entry));
    } catch (error) {
      // Only this invocation created target; remove its incomplete installation.
      fs.rmSync(target, {recursive:true, force:true});
      throw error;
    }
    return {state:'installed', destination:target, cli, node:process.execPath, mcpServer, mcpConfig};
  } finally {
    fs.rmSync(staging, {recursive:true, force:true});
  }
}

if (require.main === module) {
  try {
    const parsed = parseArgs(process.argv.slice(2));
    console.log(JSON.stringify(install(parsed.destination, __dirname, {update: parsed.update, force: parsed.force})));
  } catch (error) { console.error(JSON.stringify({error:error.message})); process.exitCode = 1; }
}
module.exports = {destination, parseArgs, install, runtimeFiles, compareSemver};
