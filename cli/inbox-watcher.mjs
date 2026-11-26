/**
 * Mainline DSH `inbox` hot-add directory watcher.
 * Watches a local directory for newly dropped screenshots, waits for writes to stabilize,
 * deduplicates by SHA-256, and registers pages into an EditHere image-set manifest.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { VERSION, readManifest, writeManifest, upsertPage } = require('../web/image-set.cjs');

const SUPPORTED_EXT = new Set(['.png', '.jpg', '.jpeg', '.webp', '.gif', '.bmp']);

/**
 * Convert an arbitrary filename into a valid EditHere page ID matching /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$/.
 * Uses a deterministic SHA-256 suffix when non-ASCII characters are stripped so distinct Chinese names never collide.
 */
export function sanitizePageId(filename) {
  const stem = path.parse(filename).name;
  const ascii = stem.replace(/[^a-zA-Z0-9._-]+/g, '-').replace(/^[^a-zA-Z0-9]+/, '').replace(/-+$/g, '');
  if (ascii && ascii === stem && ascii.length <= 128) {
    return ascii;
  }
  const suffix = crypto.createHash('sha256').update(stem, 'utf8').digest('hex').slice(0, 8);
  const prefix = (ascii || 'page').slice(0, 118);
  return `${prefix}-${suffix}`;
}

function sha256File(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

/**
 * Start polling an inbox folder and syncing new/updated images into `manifestPath`.
 */
export function startInboxWatcher({
  inboxDir,
  manifestPath,
  manifestName = 'EditHere Gallery',
  pollIntervalMs = 800,
  settleMs = 120,
  onAdded = null,
  onError = null,
}) {
  if (!inboxDir || !manifestPath) {
    throw new Error('Both inboxDir and manifestPath are required.');
  }
  const absInbox = path.resolve(inboxDir);
  const absManifest = path.resolve(manifestPath);
  fs.mkdirSync(absInbox, { recursive: true });

  if (!fs.existsSync(absManifest)) {
    writeManifest(absManifest, { schemaVersion: VERSION, name: manifestName, pages: [] }, { create: true });
  } else {
    readManifest(absManifest);
  }

  const seenHashes = new Map(); // absFile -> sha256
  const lastSizeProbe = new Map(); // absFile -> { size, mtimeMs, seenAt }
  let stopped = false;

  function scanOnce({ forceImmediate = false } = {}) {
    if (stopped) return [];
    const added = [];
    let entries;
    try {
      entries = fs.readdirSync(absInbox, { withFileTypes: true });
    } catch (err) {
      if (onError) onError(err);
      return added;
    }

    const now = Date.now();
    for (const entry of entries) {
      if (!entry.isFile()) continue;
      const ext = path.extname(entry.name).toLowerCase();
      if (!SUPPORTED_EXT.has(ext)) continue;

      const absFile = path.join(absInbox, entry.name);
      let stat;
      try {
        stat = fs.statSync(absFile);
      } catch {
        continue;
      }
      if (stat.size === 0 || stat.size > 48 * 1024 * 1024) continue;

      if (!forceImmediate) {
        const prev = lastSizeProbe.get(absFile);
        if (!prev || prev.size !== stat.size || prev.mtimeMs !== stat.mtimeMs) {
          lastSizeProbe.set(absFile, { size: stat.size, mtimeMs: stat.mtimeMs, seenAt: now });
          continue;
        }
        if (now - prev.seenAt < settleMs) continue;
      }

      let digest;
      try {
        digest = sha256File(absFile);
      } catch {
        continue;
      }
      if (seenHashes.get(absFile) === digest) continue;

      const id = sanitizePageId(entry.name);
      const displayName = path.parse(entry.name).name || id;
      try {
        upsertPage(absManifest, { id, name: displayName, imagePath: absFile });
        seenHashes.set(absFile, digest);
        const event = { id, name: displayName, file: absFile, sha256: digest, manifestPath: absManifest };
        added.push(event);
        if (onAdded) onAdded(event);
      } catch (err) {
        if (onError) onError(err);
      }
    }
    return added;
  }

  const timer = pollIntervalMs > 0 ? setInterval(() => scanOnce({ forceImmediate: false }), pollIntervalMs) : null;
  if (timer && typeof timer.unref === 'function') timer.unref();

  return {
    inboxDir: absInbox,
    manifestPath: absManifest,
    scanOnce,
    seenCount: () => seenHashes.size,
    stop() {
      stopped = true;
      if (timer) clearInterval(timer);
    },
  };
}
