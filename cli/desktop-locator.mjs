/**
 * Shared Desktop CLI resolution and safe synchronous/asynchronous invocation helpers.
 */
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

/**
 * Look up an executable name on PATH.
 * @param {string} name
 * @param {NodeJS.ProcessEnv} env
 * @returns {string | null}
 */
export function lookupOnPath(name, env = process.env) {
  const dirs = String(env.PATH || '').split(path.delimiter).filter(Boolean);
  const extensions = process.platform === 'win32'
    ? String(env.PATHEXT || '.EXE;.CMD;.BAT').split(';').filter(Boolean)
    : [''];
  for (const dir of dirs) {
    for (const ext of extensions) {
      const candidate = path.join(dir, name.toLowerCase().endsWith(ext.toLowerCase()) ? name : name + ext);
      try {
        fs.accessSync(candidate, fs.constants.X_OK);
        return candidate;
      } catch {
        // continue searching
      }
    }
  }
  return null;
}

/**
 * Locate the native EditHere desktop CLI without falling back past an explicit invalid EDITHERE_CLI.
 * @param {{ env?: NodeJS.ProcessEnv }} [options]
 * @returns {{ found: boolean, path: string | null, source: string, error?: string }}
 */
export function resolveDesktopCli({ env = process.env } = {}) {
  const explicit = env.EDITHERE_CLI;
  if (explicit) {
    try {
      fs.accessSync(explicit, fs.constants.X_OK);
      return { found: true, path: path.resolve(explicit), source: 'EDITHERE_CLI' };
    } catch {
      return {
        found: false,
        path: explicit,
        source: 'EDITHERE_CLI',
        error: `EDITHERE_CLI points to a missing or non-executable binary: ${explicit}`,
      };
    }
  }

  const exeName = process.platform === 'win32' ? 'edithere-cli.exe' : 'edithere-cli';
  const onPath = lookupOnPath(exeName, env);
  if (onPath) {
    return { found: true, path: path.resolve(onPath), source: 'PATH' };
  }

  const candidates = [];
  if (process.platform === 'win32' && env.LOCALAPPDATA) {
    candidates.push([path.join(env.LOCALAPPDATA, 'Programs', 'EditHere', 'edithere-cli.exe'), 'default-install']);
  } else if (process.platform === 'darwin') {
    candidates.push([path.join(os.homedir(), 'Applications', 'EditHere.app', 'Contents', 'MacOS', 'edithere-cli'), 'default-install']);
    candidates.push(['/Applications/EditHere.app/Contents/MacOS/edithere-cli', 'default-install']);
  } else if (process.platform === 'linux') {
    candidates.push([path.join(os.homedir(), '.local', 'bin', 'edithere-cli'), 'default-install']);
    candidates.push(['/usr/local/bin/edithere-cli', 'default-install']);
  }

  for (const [candidate, source] of candidates) {
    try {
      fs.accessSync(candidate, fs.constants.X_OK);
      return { found: true, path: path.resolve(candidate), source };
    } catch {
      // continue
    }
  }

  return {
    found: false,
    path: null,
    source: 'not-found',
    error: `Desktop ${exeName} not found via EDITHERE_CLI, PATH, or default install paths.`,
  };
}

/**
 * Run a Desktop CLI command synchronously with bounded timeout.
 * @param {string} cliPath
 * @param {string[]} args
 * @param {{ timeoutMs?: number }} [options]
 */
export function runDesktopCliSync(cliPath, args, { timeoutMs = 8000 } = {}) {
  const res = spawnSync(cliPath, args, {
    encoding: 'utf8',
    timeout: timeoutMs,
    windowsHide: true,
  });
  const stdout = String(res.stdout || '').trim();
  const stderr = String(res.stderr || '').trim();
  let json = null;
  if (stdout.startsWith('{')) {
    try {
      json = JSON.parse(stdout.split('\n').pop());
    } catch {
      json = null;
    }
  }
  return {
    exitCode: typeof res.status === 'number' ? res.status : -1,
    stdout,
    stderr,
    json,
    error: res.error ? String(res.error.message || res.error) : null,
  };
}

/**
 * Parse major.minor.patch tuple from `EditHere x.y.z` output and compare against minimum.
 * @param {string} versionText
 */
export function parseVersionTuple(versionText) {
  const match = /(\d+)\.(\d+)\.(\d+)/.exec(String(versionText || ''));
  return match ? [Number(match[1]), Number(match[2]), Number(match[3])] : null;
}

export function versionAtLeast(actual, minimum) {
  const a = parseVersionTuple(actual);
  const b = parseVersionTuple(minimum);
  if (!a || !b) return false;
  for (let i = 0; i < 3; i += 1) {
    if (a[i] !== b[i]) return a[i] > b[i];
  }
  return true;
}

const probeCache = new Map();

function buildProbeCacheKey(cliPath) {
  try {
    const stat = fs.statSync(cliPath);
    return `${cliPath}|${stat.mtimeMs}|${stat.size}`;
  } catch {
    return null;
  }
}

export function clearDesktopProbeCache() {
  probeCache.clear();
}

/**
 * Probe static desktop binary capabilities (--version, --help) with mtime/size caching.
 * Never caches live `status` / `running`.
 */
export function getDesktopCapabilities({ env = process.env } = {}) {
  const resolved = resolveDesktopCli({ env });
  if (!resolved.found || !resolved.path) {
    return {
      ...resolved,
      version: null,
      supportsVideo: false,
      supportsImageSet: false,
      supportsFastCapture: false,
      cacheHit: false,
    };
  }

  const cacheKey = buildProbeCacheKey(resolved.path);
  if (cacheKey && probeCache.has(cacheKey)) {
    return {
      ...resolved,
      ...probeCache.get(cacheKey),
      cacheHit: true,
    };
  }

  const verRes = runDesktopCliSync(resolved.path, ['--version'], { timeoutMs: 5000 });
  const versionTuple = parseVersionTuple(verRes.stdout);
  const version = versionTuple ? versionTuple.join('.') : null;

  const helpRes = runDesktopCliSync(resolved.path, ['--help'], { timeoutMs: 5000 });
  const helpText = helpRes.stdout + '\n' + helpRes.stderr;
  const supportsImageSet = /image-set/i.test(helpText);
  const supportsFastCapture = /--window/i.test(helpText) || Boolean(version && versionAtLeast(version, '0.14.1'));

  const caps = {
    version,
    supportsVideo: Boolean(version && versionAtLeast(version, '0.10.0')),
    supportsImageSet,
    supportsFastCapture,
  };
  if (cacheKey && (verRes.exitCode === 0 || helpRes.exitCode === 0)) {
    probeCache.set(cacheKey, caps);
  }
  return {
    ...resolved,
    ...caps,
    cacheHit: false,
  };
}

/**
 * Probe desktop binary capabilities (--version, --help, live status).
 */
export function inspectDesktopCli({ env = process.env } = {}) {
  const caps = getDesktopCapabilities({ env });
  if (!caps.found || !caps.path) {
    return {
      ...caps,
      running: false,
      statusProbe: null,
    };
  }

  const statusRes = runDesktopCliSync(caps.path, ['status'], { timeoutMs: 5000 });
  const statusJson = statusRes.json;
  const running = statusJson && Object.hasOwn(statusJson, 'running') ? statusJson.running : null;

  return {
    ...caps,
    running,
    statusProbe: statusJson || { exitCode: statusRes.exitCode, error: statusRes.error || statusRes.stderr },
  };
}
