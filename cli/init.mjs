/**
 * One-command EditHere setup (`edithere-cli init`).
 * Atomically installs the canonical English EditHere skill into AI host skill directories,
 * writes resolved local binary/MCP paths into `references/local-installation.md`,
 * and prints ready-to-paste unified MCP configuration without mutating host config files.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { resolveDesktopCli } from './desktop-locator.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const CANONICAL_SKILL = path.join(ROOT, 'skills', 'edithere');
const SKILL_VERSION_FILE = path.join(ROOT, 'skills-src', 'skill-version.json');
const UNIFIED_CLI = path.join(ROOT, 'cli', 'edithere.mjs');
const UNIFIED_MCP = path.join(ROOT, 'cli', 'unified-mcp.mjs');
const SEMVER_RE = /^\d+\.\d+\.\d+$/;
const APP_VERSION = '0.14.1';

function isValidSemver(value) {
  return typeof value === 'string' && SEMVER_RE.test(value.trim());
}

/**
 * Compare two X.Y.Z semver strings (-1, 0, or 1).
 */
export function compareSemver(a, b) {
  const parse = (v) => (isValidSemver(v) ? v.trim().split('.').map(Number) : [0, 0, 0]);
  const pa = parse(a);
  const pb = parse(b);
  for (let i = 0; i < 3; i += 1) {
    if (pa[i] < pb[i]) return -1;
    if (pa[i] > pb[i]) return 1;
  }
  return 0;
}

/**
 * Read bundled skill semantic version metadata.
 */
export function readBundledSkillVersion() {
  for (const candidate of [path.join(CANONICAL_SKILL, 'skill-version.json'), SKILL_VERSION_FILE]) {
    try {
      const parsed = JSON.parse(fs.readFileSync(candidate, 'utf8').replace(/^\uFEFF/, ''));
      if (isValidSemver(parsed?.skillVersion)) {
        return {
          skillVersion: parsed.skillVersion.trim(),
          minCompatibleSkillVersion: isValidSemver(parsed.minCompatibleSkillVersion)
            ? parsed.minCompatibleSkillVersion.trim()
            : parsed.skillVersion.trim(),
        };
      }
    } catch {
      // try next candidate
    }
  }
  return { skillVersion: '1.0.0', minCompatibleSkillVersion: '1.0.0' };
}

function samePath(a, b) {
  if (!a || !b) return false;
  const resolveSafe = (p) => {
    const abs = path.resolve(p);
    try {
      return fs.realpathSync(abs);
    } catch {
      return abs;
    }
  };
  const ra = resolveSafe(a);
  const rb = resolveSafe(b);
  return process.platform === 'win32' ? ra.toLowerCase() === rb.toLowerCase() : ra === rb;
}

function readInstalledMeta(dest) {
  const metaPath = path.join(dest, '.edithere-skill.json');
  try {
    if (!fs.existsSync(metaPath)) return null;
    const parsed = JSON.parse(fs.readFileSync(metaPath, 'utf8').replace(/^\uFEFF/, ''));
    return parsed && typeof parsed === 'object' ? parsed : null;
  } catch {
    return null;
  }
}

function resolveEffectiveDesktopCli({ env = process.env } = {}) {
  const desktop = resolveDesktopCli({ env });
  if (desktop.found || env.EDITHERE_CLI) return desktop;
  const siblingExe = path.join(ROOT, process.platform === 'win32' ? 'edithere-cli.exe' : 'edithere-cli');
  try {
    fs.accessSync(siblingExe, fs.constants.X_OK);
    return { found: true, path: path.resolve(siblingExe), source: 'bundled-root' };
  } catch {
    return desktop;
  }
}

function pathExists(target) {
  try {
    fs.lstatSync(target);
    return true;
  } catch (err) {
    if (err && err.code === 'ENOENT') return false;
    throw err;
  }
}

function collectFiles(rootDir, rel = '') {
  const entries = fs.readdirSync(path.join(rootDir, rel), { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const nextRel = rel ? `${rel}/${entry.name}` : entry.name;
    if (entry.isDirectory()) {
      files.push(...collectFiles(rootDir, nextRel));
    } else if (entry.isFile()) {
      files.push(nextRel);
    }
  }
  return files.sort();
}

export function resolveHostTargets({
  host = 'auto',
  destination = null,
  homeDir = os.homedir(),
  env = process.env,
} = {}) {
  if (destination) {
    if (!path.isAbsolute(destination)) {
      throw new Error('--destination must be an absolute directory path.');
    }
    return [{ host: 'custom', destination: path.resolve(destination) }];
  }

  const codexHome = env.CODEX_HOME ? path.resolve(env.CODEX_HOME) : path.join(homeDir, '.codex');
  const map = {
    codex: { host: 'codex', name: 'Codex', root: codexHome, destination: path.join(codexHome, 'skills', 'edithere') },
    agents: { host: 'agents', name: 'Codex Agents (~/.agents)', root: path.join(homeDir, '.agents'), destination: path.join(homeDir, '.agents', 'skills', 'edithere') },
    claude: { host: 'claude', name: 'Claude Code', root: path.join(homeDir, '.claude'), destination: path.join(homeDir, '.claude', 'skills', 'edithere') },
    cursor: { host: 'cursor', name: 'Cursor', root: path.join(homeDir, '.cursor'), destination: path.join(homeDir, '.cursor', 'skills', 'edithere') },
    trae: { host: 'trae', name: 'Trae', root: path.join(homeDir, '.trae'), destination: path.join(homeDir, '.trae', 'skills', 'edithere') },
    antigravity: { host: 'antigravity', name: 'Antigravity', root: path.join(homeDir, '.gemini', 'antigravity'), destination: path.join(homeDir, '.gemini', 'antigravity', 'skills', 'edithere') },
  };

  if (host === 'all') {
    return Object.values(map);
  }
  if (host === 'auto') {
    const detected = Object.values(map).filter((item) => fs.existsSync(item.root));
    return detected.length ? detected : [map.codex];
  }
  if (!Object.hasOwn(map, host)) {
    throw new Error(`Unsupported --host "${host}". Expected auto, codex, agents, claude, cursor, trae, antigravity, or all.`);
  }
  return [map[host]];
}

export function inspectSkillDestination(dest, { bundledVersion, cliPath, unifiedMcpPath } = {}) {
  const version = bundledVersion || readBundledSkillVersion().skillVersion;
  const skillMd = path.join(dest, 'SKILL.md');
  if (!fs.existsSync(skillMd)) {
    return { installed: false, installedVersion: null, status: 'not-installed', meta: null };
  }
  const meta = readInstalledMeta(dest);
  if (!meta || !isValidSemver(meta.skillVersion)) {
    return { installed: true, installedVersion: null, status: 'legacy', meta };
  }
  const installedVersion = meta.skillVersion.trim();
  const cmp = compareSemver(installedVersion, version);
  if (cmp < 0) {
    return { installed: true, installedVersion, status: 'outdated', meta };
  }
  if (cmp > 0) {
    return { installed: true, installedVersion, status: 'newer', meta };
  }
  const recordedCli = typeof meta.cliPath === 'string' && meta.cliPath ? meta.cliPath : null;
  const recordedMcp = typeof meta.unifiedMcpPath === 'string' && meta.unifiedMcpPath ? meta.unifiedMcpPath : null;
  const cliStale = cliPath && (recordedCli ? (!fs.existsSync(recordedCli) || !samePath(recordedCli, cliPath)) : !recordedMcp);
  const mcpStale = unifiedMcpPath && recordedMcp && (!fs.existsSync(recordedMcp) || !samePath(recordedMcp, unifiedMcpPath));
  const missingRecorded = (recordedCli && !fs.existsSync(recordedCli)) || (recordedMcp && !fs.existsSync(recordedMcp));
  return {
    installed: true,
    installedVersion,
    status: cliStale || mcpStale || missingRecorded ? 'path-stale' : 'up-to-date',
    meta,
  };
}

export function inspectHostSkills({ homeDir = os.homedir(), env = process.env } = {}) {
  const versionInfo = readBundledSkillVersion();
  const desktop = resolveEffectiveDesktopCli({ env });
  const targets = resolveHostTargets({ host: 'all', homeDir, env });
  const hosts = targets.map((t) => {
    const webSkillPath = path.join(t.root, 'skills', 'edithere-web');
    const webSkillInstalled = fs.existsSync(path.join(webSkillPath, 'SKILL.md'));
    const statusInfo = inspectSkillDestination(t.destination, {
      bundledVersion: versionInfo.skillVersion,
      cliPath: desktop.found ? desktop.path : null,
      unifiedMcpPath: UNIFIED_MCP,
    });
    return {
      id: t.host,
      name: t.name,
      root: t.root,
      present: fs.existsSync(t.root),
      skillPath: t.destination,
      skillInstalled: statusInfo.installed,
      skillVersion: statusInfo.installedVersion,
      skillStatus: statusInfo.status,
      webSkillInstalled,
    };
  });
  return {
    bundledSkillVersion: versionInfo.skillVersion,
    minCompatibleSkillVersion: versionInfo.minCompatibleSkillVersion,
    hosts,
  };
}

function buildLocalInstallationNote({ desktop, mcpConfig, skillVersion }) {
  return [
    '# Local EditHere Entry Points',
    '',
    `Generated by \`edithere init\` (Skill v${skillVersion}). Re-run after moving this repository or installing the desktop app.`,
    '',
    '## Unified CLI & MCP (Auto-Routes Desktop or Web)',
    '',
    `- Node.js: \`${process.execPath}\``,
    `- Unified CLI: \`${UNIFIED_CLI}\``,
    `- Unified MCP Server: \`${UNIFIED_MCP}\``,
    '',
    '## Desktop CLI',
    '',
    desktop.found
      ? `- Path (${desktop.source}): \`${desktop.path}\``
      : `- Not installed (${desktop.error || 'not found'}). Unified commands automatically use the bundled Web workbench.`,
    '',
    '## Host MCP Configuration Snippet',
    '',
    '```json',
    JSON.stringify(mcpConfig, null, 2),
    '```',
    '',
  ].join('\n');
}

function installOneTarget(
  target,
  { desktop, mcpConfig, skillVersion, force = false, update = false, dryRun = false }
) {
  const dest = path.resolve(target.destination);
  const exists = pathExists(dest);
  const inspected = exists
    ? inspectSkillDestination(dest, {
        bundledVersion: skillVersion,
        cliPath: desktop.found ? desktop.path : null,
        unifiedMcpPath: UNIFIED_MCP,
      })
    : { installed: false, installedVersion: null, status: 'not-installed', meta: null };

  if (update && !force) {
    if (!exists || inspected.status === 'not-installed') {
      return {
        host: target.host,
        destination: dest,
        state: 'skipped-not-installed',
        message: 'Skill is not installed in this host; omit --update to install.',
      };
    }
    if (inspected.status === 'up-to-date') {
      return {
        host: target.host,
        destination: dest,
        state: 'up-to-date',
        skillVersion,
      };
    }
    if (inspected.status === 'newer') {
      return {
        host: target.host,
        destination: dest,
        state: 'skipped-newer',
        installedVersion: inspected.installedVersion,
        bundledVersion: skillVersion,
        message: `Installed skill v${inspected.installedVersion} is newer than bundled v${skillVersion}; re-run with --force to downgrade.`,
      };
    }
  } else if (exists && !force) {
    return {
      host: target.host,
      destination: dest,
      state: 'skipped-exists',
      skillVersion: inspected.installedVersion,
      message: 'Destination already exists; re-run with --update or --force to update.',
    };
  }

  if (dryRun) {
    return {
      host: target.host,
      destination: dest,
      state: exists ? 'would-overwrite' : 'would-install',
      fromVersion: inspected.installedVersion,
      toVersion: skillVersion,
    };
  }

  const skillFiles = collectFiles(CANONICAL_SKILL).filter(
    (rel) => rel !== '.edithere-skill.json' && rel !== 'references/local-installation.md'
  );
  const parent = path.dirname(dest);
  fs.mkdirSync(parent, { recursive: true });
  const suffix = crypto.randomUUID();
  const staging = path.join(parent, `.edithere-init-${suffix}`);
  const backup = path.join(parent, `.edithere-init-old-${suffix}`);
  fs.mkdirSync(staging, { recursive: true });

  try {
    for (const rel of skillFiles) {
      const outPath = path.join(staging, rel);
      fs.mkdirSync(path.dirname(outPath), { recursive: true });
      fs.copyFileSync(path.join(CANONICAL_SKILL, rel), outPath);
    }
    fs.mkdirSync(path.join(staging, 'references'), { recursive: true });
    fs.writeFileSync(
      path.join(staging, 'references', 'local-installation.md'),
      buildLocalInstallationNote({ desktop, mcpConfig, skillVersion }),
      'utf8'
    );
    fs.writeFileSync(
      path.join(staging, '.edithere-skill.json'),
      JSON.stringify(
        {
          schemaVersion: 1,
          skillVersion,
          appVersion: APP_VERSION,
          channel: 'desktop',
          cliPath: desktop.found ? desktop.path : '',
          unifiedCliPath: UNIFIED_CLI,
          unifiedMcpPath: UNIFIED_MCP,
          installedAt: new Date().toISOString(),
        },
        null,
        2
      ) + '\n',
      'utf8'
    );

    if (exists) {
      fs.renameSync(dest, backup);
      try {
        fs.renameSync(staging, dest);
      } catch (swapErr) {
        try {
          fs.renameSync(backup, dest);
        } catch {
          // leave backup path visible in error if recovery fails
        }
        throw swapErr;
      }
      fs.rmSync(backup, { recursive: true, force: true });
    } else {
      fs.renameSync(staging, dest);
    }
  } catch (error) {
    fs.rmSync(staging, { recursive: true, force: true });
    throw error;
  }

  return {
    host: target.host,
    destination: dest,
    state: exists ? 'updated' : 'installed',
    fromVersion: inspected.installedVersion,
    skillVersion,
    filesCopied: skillFiles.length + 2,
  };
}

/**
 * Install or update canonical EditHere skill and prepare unified MCP configuration.
 */
export function runInit({
  host = 'auto',
  destination = null,
  homeDir = os.homedir(),
  env = process.env,
  force = false,
  update = false,
  dryRun = false,
} = {}) {
  if (!fs.existsSync(path.join(CANONICAL_SKILL, 'SKILL.md'))) {
    throw new Error(`Canonical skill missing at ${CANONICAL_SKILL}`);
  }
  const { skillVersion, minCompatibleSkillVersion } = readBundledSkillVersion();
  const desktop = resolveEffectiveDesktopCli({ env });
  const mcpConfig = {
    mcpServers: {
      edithere: {
        command: process.execPath,
        args: [UNIFIED_MCP],
      },
    },
  };

  const targets = resolveHostTargets({ host, destination, homeDir, env });
  const results = targets.map((target) =>
    installOneTarget(target, { desktop, mcpConfig, skillVersion, force, update, dryRun })
  );
  return {
    ok: true,
    dryRun,
    skillVersion,
    minCompatibleSkillVersion,
    desktop,
    unifiedCli: UNIFIED_CLI,
    unifiedMcp: UNIFIED_MCP,
    mcpConfig,
    results,
  };
}
