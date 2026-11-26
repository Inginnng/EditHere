import { readFileSync } from 'node:fs';

function readVersion() {
  for (const file of ['../version.txt', '../frontend/package.json']) {
    try {
      const contents = readFileSync(new URL(file, import.meta.url), 'utf8').replace(/^\uFEFF/, '');
      const version = file.endsWith('.json') ? JSON.parse(contents)?.version : contents.trim();
      if (typeof version === 'string' && /^\d+\.\d+\.\d+$/.test(version)) return version;
    } catch {
      // Try the other layout when this file is unavailable or invalid.
    }
  }
  return '0.0.0';
}

export const APP_VERSION = readVersion();
