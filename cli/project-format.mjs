/**
 * Unified EditHere Project Format (`edithere-project-2`) validator and bidirectional converter.
 * Supports:
 *   - `edithere-image-set-1` manifest <-> `edithere-project-2`
 *   - legacy `edithere-web-project-1` -> extracted `edithere-image-set-1` manifest + PNG/asset files
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const {
  VERSION: MANIFEST_VERSION,
  MAX_PAGES,
  readManifest,
  writeManifest,
  upsertPage,
  imageSize,
} = require('../web/image-set.cjs');

export const UNIFIED_SCHEMA_VERSION = 'edithere-project-2';
export const TOOL_VERSION = '0.14.0';

const MIME_MAP = {
  '.png': { mime: 'image/png', ext: '.png' },
  '.jpg': { mime: 'image/jpeg', ext: '.jpg' },
  '.jpeg': { mime: 'image/jpeg', ext: '.jpg' },
  '.webp': { mime: 'image/webp', ext: '.webp' },
  '.gif': { mime: 'image/gif', ext: '.gif' },
  '.bmp': { mime: 'image/bmp', ext: '.bmp' },
};

const EXT_BY_MIME = {
  'image/png': '.png',
  'image/jpeg': '.jpg',
  'image/webp': '.webp',
  'image/gif': '.gif',
  'image/bmp': '.bmp',
};

function assertPageId(id) {
  if (typeof id !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$/.test(id) || id.includes('..')) {
    throw new Error(`Unsafe or invalid page id: ${String(id)}`);
  }
  return id;
}

function sha256Bytes(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

function decodeDataUrl(dataUrl) {
  const match = /^data:(image\/(?:png|jpeg|webp|gif|bmp));base64,([A-Za-z0-9+/]+={0,2})$/.exec(String(dataUrl || ''));
  if (!match) {
    throw new Error('Unsupported or malformed image dataUrl.');
  }
  const mimeType = match[1];
  const bytes = Buffer.from(match[2], 'base64');
  const dims = imageSize(bytes);
  return { mimeType, bytes, ...dims, sha256: sha256Bytes(bytes) };
}

/**
 * Validate an `edithere-project-2` object structurally and verify image metadata.
 */
export function validateUnifiedProject(project) {
  if (!project || typeof project !== 'object' || Array.isArray(project)) {
    throw new Error('Unified project must be a JSON object.');
  }
  if (project.schemaVersion !== UNIFIED_SCHEMA_VERSION) {
    throw new Error(`Expected schemaVersion "${UNIFIED_SCHEMA_VERSION}", got "${String(project.schemaVersion)}".`);
  }
  if (!['single', 'set', 'video'].includes(project.kind)) {
    throw new Error(`Invalid project kind: ${String(project.kind)}`);
  }
  if (typeof project.name !== 'string' || !project.name.trim() || project.name.length > 200) {
    throw new Error('Invalid project name.');
  }
  if (!Array.isArray(project.pages) || project.pages.length === 0 || project.pages.length > MAX_PAGES) {
    throw new Error(`Unified project pages must contain 1..${MAX_PAGES} items.`);
  }
  if (!Array.isArray(project.groupNotes)) {
    throw new Error('Unified project groupNotes must be an array.');
  }

  const seenIds = new Set();
  for (const page of project.pages) {
    if (!page || typeof page !== 'object' || Array.isArray(page)) {
      throw new Error('Each project page must be an object.');
    }
    assertPageId(page.id);
    if (seenIds.has(page.id)) throw new Error(`Duplicate page id: ${page.id}`);
    seenIds.add(page.id);
    if (typeof page.name !== 'string' || !page.name.trim() || page.name.length > 200) {
      throw new Error(`Invalid page name for ${page.id}.`);
    }
    if (!page.image || typeof page.image !== 'object') {
      throw new Error(`Missing image descriptor on page ${page.id}.`);
    }
    if (!['embedded', 'file'].includes(page.image.mode)) {
      throw new Error(`Unsupported image.mode "${String(page.image.mode)}" on page ${page.id}.`);
    }
    if (!/^[a-f0-9]{64}$/.test(String(page.image.sha256 || ''))) {
      throw new Error(`Invalid image.sha256 on page ${page.id}.`);
    }
    if (page.image.mode === 'embedded') {
      const decoded = decodeDataUrl(page.image.dataUrl);
      if (decoded.sha256 !== page.image.sha256) {
        throw new Error(`SHA-256 mismatch for embedded image on page ${page.id}.`);
      }
    }
    if (!Array.isArray(page.objects)) {
      throw new Error(`Page ${page.id} objects must be an array.`);
    }
  }
  return project;
}

/**
 * Convert an `edithere-image-set-1` manifest into a portable `edithere-project-2` payload.
 */
export function manifestToUnifiedProject(manifestPath) {
  const absManifest = path.resolve(manifestPath);
  const manifest = readManifest(absManifest);
  if (!manifest.pages.length) {
    throw new Error('Cannot convert an empty manifest with 0 pages.');
  }
  const baseDir = path.dirname(absManifest);

  const pages = manifest.pages.map((entry) => {
    assertPageId(entry.id);
    const absImage = path.resolve(baseDir, entry.path);
    const ext = path.extname(absImage).toLowerCase();
    const mapping = MIME_MAP[ext];
    if (!mapping) throw new Error(`Unsupported image type for ${entry.id}: ${entry.path}`);
    const bytes = fs.readFileSync(absImage);
    const digest = sha256Bytes(bytes);
    if (entry.sha256 && entry.sha256 !== digest) {
      throw new Error(`Image checksum drifted for ${entry.id}; run image-set.mjs upsert first.`);
    }
    const { width, height } = imageSize(bytes);
    return {
      id: entry.id,
      name: entry.name,
      mediaType: 'image',
      image: {
        mode: 'embedded',
        mimeType: mapping.mime,
        width,
        height,
        devicePixelRatio: null,
        sha256: digest,
        dataUrl: `data:${mapping.mime};base64,${bytes.toString('base64')}`,
      },
      objects: [],
    };
  });

  const project = {
    schemaVersion: UNIFIED_SCHEMA_VERSION,
    tool: { name: 'EditHere', version: TOOL_VERSION },
    kind: pages.length === 1 ? 'single' : 'set',
    name: manifest.name,
    createdAt: new Date().toISOString(),
    pages,
    groupNotes: [],
  };
  return validateUnifiedProject(project);
}

/**
 * Extract an `edithere-project-2` or legacy `edithere-web-project-1` JSON into a directory
 * containing `project.json` (`edithere-image-set-1`) and lossless image files under `images/`.
 */
export function unifiedProjectToManifest(projectPath, outputDir, { name = null } = {}) {
  const absProject = path.resolve(projectPath);
  const raw = JSON.parse(fs.readFileSync(absProject, 'utf8').replace(/^\uFEFF/, ''));

  let projectName;
  let extractedPages;

  if (raw?.schemaVersion === UNIFIED_SCHEMA_VERSION) {
    validateUnifiedProject(raw);
    projectName = name || raw.name;
    extractedPages = raw.pages.map((p) => {
      if (p.image.mode === 'embedded') {
        const decoded = decodeDataUrl(p.image.dataUrl);
        return { id: p.id, name: p.name, ...decoded };
      }
      const sourcePath = path.resolve(path.dirname(absProject), p.image.file || '');
      const bytes = fs.readFileSync(sourcePath);
      const digest = sha256Bytes(bytes);
      if (digest !== p.image.sha256) {
        throw new Error(`Referenced file checksum mismatch on page ${p.id}.`);
      }
      const dims = imageSize(bytes);
      return { id: p.id, name: p.name, mimeType: p.image.mimeType, bytes, sha256: digest, ...dims };
    });
  } else if (raw?.schemaVersion === 'edithere-web-project-1' && Array.isArray(raw.pages)) {
    projectName = name || raw.name || 'Imported Web Project';
    extractedPages = raw.pages.map((p) => {
      const id = assertPageId(p.id);
      const decoded = decodeDataUrl(p.image);
      return { id, name: p.name || id, ...decoded };
    });
  } else {
    throw new Error(`Unsupported project schemaVersion: ${String(raw?.schemaVersion || 'none')}`);
  }

  const absOut = path.resolve(outputDir);
  const imagesDir = path.join(absOut, 'images');
  const manifestFile = path.join(absOut, 'project.json');
  if (fs.existsSync(manifestFile)) {
    throw new Error(`Destination manifest already exists: ${manifestFile}`);
  }

  fs.mkdirSync(imagesDir, { recursive: true });
  writeManifest(manifestFile, { schemaVersion: MANIFEST_VERSION, name: projectName, pages: [] }, { create: true });

  for (const page of extractedPages) {
    const ext = EXT_BY_MIME[page.mimeType] || '.png';
    const imageFile = path.join(imagesDir, `${page.id}${ext}`);
    fs.writeFileSync(imageFile, page.bytes);
    upsertPage(manifestFile, { id: page.id, name: page.name, imagePath: imageFile });
  }

  return {
    ok: true,
    schemaVersion: raw.schemaVersion,
    manifestPath: manifestFile,
    imagesDir,
    pageCount: extractedPages.length,
  };
}
