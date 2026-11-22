/**
 * Unified environment diagnostics (`edithere-cli doctor`).
 * Inspects Desktop CLI, Node.js runtime, bundled Web Agent, Playwright availability,
 * loopback preview port status, and AI host skill installations.
 */
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { inspectDesktopCli } from './desktop-locator.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function probeTcpPort(port, host = '127.0.0.1', timeoutMs = 400) {
  return new Promise((resolve) => {
    const socket = net.createConnection({ port, host });
    const done = (listening) => {
      socket.removeAllListeners();
      socket.destroy();
      resolve(listening);
    };
    socket.setTimeout(timeoutMs);
    socket.once('connect', () => done(true));
    socket.once('timeout', () => done(false));
    socket.once('error', () => done(false));
  });
}

function detectPlaywright(cwd) {
  const loaders = [
    createRequire(path.join(cwd, 'package.json')),
    createRequire(import.meta.url),
  ];
  for (const loader of loaders) {
    for (const pkg of ['playwright', 'playwright-core']) {
      try {
        const resolved = loader.resolve(pkg);
        return { installed: true, package: pkg, resolved };
      } catch {
        // continue
      }
    }
  }
  return { installed: false, package: null, resolved: null };
}

function detectHosts({ homeDir = os.homedir(), env = process.env } = {}) {
  const codexRoot = env.CODEX_HOME ? path.resolve(env.CODEX_HOME) : path.join(homeDir, '.codex');
  const definitions = [
    { id: 'codex', name: 'Codex', root: codexRoot, skillSubdir: ['skills', 'edithere'] },
    { id: 'agents', name: 'Codex Agents (~/.agents)', root: path.join(homeDir, '.agents'), skillSubdir: ['skills', 'edithere'] },
    { id: 'claude', name: 'Claude Code', root: path.join(homeDir, '.claude'), skillSubdir: ['skills', 'edithere'] },
    { id: 'cursor', name: 'Cursor', root: path.join(homeDir, '.cursor'), skillSubdir: ['skills', 'edithere'] },
    { id: 'trae', name: 'Trae', root: path.join(homeDir, '.trae'), skillSubdir: ['skills', 'edithere'] },
    { id: 'antigravity', name: 'Antigravity', root: path.join(homeDir, '.gemini', 'antigravity'), skillSubdir: ['skills', 'edithere'] },
  ];

  return definitions.map((def) => {
    const hostExists = fs.existsSync(def.root);
    const skillPath = path.join(def.root, ...def.skillSubdir);
    const webSkillPath = path.join(def.root, 'skills', 'edithere-web');
    const skillInstalled = fs.existsSync(path.join(skillPath, 'SKILL.md'));
    const webSkillInstalled = fs.existsSync(path.join(webSkillPath, 'SKILL.md'));
    return {
      id: def.id,
      name: def.name,
      root: def.root,
      present: hostExists,
      skillPath,
      skillInstalled,
      webSkillInstalled,
    };
  });
}

/**
 * Run all EditHere readiness checks without mutating system state.
 */
export async function runDoctor({
  cwd = process.cwd(),
  env = process.env,
  homeDir = os.homedir(),
  probePort = true,
  probeDesktop = true,
} = {}) {
  const nodeVersion = process.versions.node;
  const nodeMajor = Number(nodeVersion.split('.')[0]);
  const node = {
    executable: process.execPath,
    version: nodeVersion,
    compatOk: nodeMajor >= 18,
    mcpAppsOk: nodeMajor >= 20,
  };

  const desktop = probeDesktop
    ? inspectDesktopCli({ env })
    : { found: false, path: null, source: 'skipped', version: null, supportsVideo: false, supportsImageSet: false, running: null };

  const webFiles = {
    offlineHtml: fs.existsSync(path.join(ROOT, 'web', 'EditHere.html')),
    agentCli: fs.existsSync(path.join(ROOT, 'web', 'agent.cjs')),
    mcpBundle: fs.existsSync(path.join(ROOT, 'web', 'mcp', 'server.bundle.cjs')),
    unifiedCli: fs.existsSync(path.join(ROOT, 'cli', 'edithere.mjs')),
    unifiedMcp: fs.existsSync(path.join(ROOT, 'cli', 'unified-mcp.mjs')),
    canonicalSkill: fs.existsSync(path.join(ROOT, 'skills', 'edithere', 'SKILL.md')),
  };

  const port = Number(env.EDITHERE_WEB_PORT || 7330);
  const validPort = Number.isInteger(port) && port >= 1 && port <= 65535;
  const portBusy = probePort && validPort ? await probeTcpPort(port) : false;

  const playwright = detectPlaywright(cwd);
  const hosts = detectHosts({ homeDir, env });

  const webReady = webFiles.agentCli && webFiles.offlineHtml && webFiles.mcpBundle;
  const activeRoute = desktop.found ? 'desktop' : (webReady && node.compatOk ? 'web' : 'unavailable');
  const ok = Boolean(node.compatOk && webFiles.canonicalSkill && (desktop.found || webReady));

  const recommendations = [];
  if (!desktop.found) {
    recommendations.push('Desktop edithere-cli was not detected; unified CLI/MCP will auto-route to the bundled Web workbench. Install Desktop from https://github.com/Inginnng/EditHere/releases/latest for native screenshot/OCR/video support.');
  } else if (!desktop.supportsVideo) {
    recommendations.push(`Desktop edithere-cli ${desktop.version || 'unknown'} is older than 0.10.0; upgrade to unlock video annotation.`);
  }
  if (!node.mcpAppsOk) {
    recommendations.push(`Node.js ${nodeVersion} works for browser compatibility mode (18+); upgrade to Node.js 20+ for MCP Apps.`);
  }
  if (!hosts.some((h) => h.skillInstalled || h.webSkillInstalled)) {
    recommendations.push('No AI host has EditHere skill installed yet. Run: node cli/edithere.mjs init');
  }
  if (!playwright.installed) {
    recommendations.push('Optional: Playwright was not found in the current working directory (capture-web.mjs needs Playwright; host browser screenshot tools still work).');
  }

  return {
    ok,
    summary: {
      activeRoute,
      desktopAvailable: desktop.found,
      webAvailable: webReady,
      nodeOk: node.compatOk,
      installedHosts: hosts.filter((h) => h.skillInstalled || h.webSkillInstalled).map((h) => h.id),
    },
    desktop,
    node,
    web: {
      ready: webReady,
      port: validPort ? port : null,
      portListening: portBusy,
      files: webFiles,
    },
    playwright,
    hosts,
    recommendations,
  };
}

export function formatDoctorReport(report) {
  const lines = [];
  lines.push(`EditHere Doctor — active route: ${report.summary.activeRoute.toUpperCase()} (${report.ok ? 'READY' : 'NEEDS ATTENTION'})`);
  lines.push('');
  lines.push(`[Desktop] ${report.desktop.found ? `OK (${report.desktop.version || 'unknown'} via ${report.desktop.source}: ${report.desktop.path})` : `Not installed (${report.desktop.error || 'web fallback active'})`}`);
  lines.push(`[Node.js] ${report.node.compatOk ? 'OK' : 'TOO OLD'} v${report.node.version} (${report.node.executable})`);
  lines.push(`[Web Fallback] ${report.web.ready ? 'OK' : 'INCOMPLETE'} (port ${report.web.port}${report.web.portListening ? ' listening' : ' free'})`);
  lines.push(`[Playwright] ${report.playwright.installed ? `OK (${report.playwright.package})` : 'Not found in cwd (optional for capture-web.mjs)'}`);
  lines.push('[AI Hosts]');
  for (const h of report.hosts) {
    const state = h.skillInstalled ? 'skill installed' : h.present ? 'detected (skill missing)' : 'not present';
    lines.push(`  - ${h.id}: ${state} (${h.skillPath})`);
  }
  if (report.recommendations.length) {
    lines.push('');
    lines.push('[Recommendations]');
    for (const rec of report.recommendations) {
      lines.push(`  * ${rec}`);
    }
  }
  return lines.join('\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const asJson = process.argv.includes('--json');
  runDoctor().then((report) => {
    console.log(asJson ? JSON.stringify(report, null, 2) : formatDoctorReport(report));
    if (!report.ok) process.exitCode = 1;
  }).catch((error) => {
    console.error(JSON.stringify({ ok: false, error: error.message }));
    process.exitCode = 1;
  });
}
