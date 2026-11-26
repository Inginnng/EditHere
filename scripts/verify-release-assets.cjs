#!/usr/bin/env node
'use strict';

const { createHash } = require('node:crypto');
const { createReadStream, readFileSync, statSync } = require('node:fs');
const { basename } = require('node:path');

async function main() {
  const args = process.argv.slice(2);
  const published = args[0] === '--published';
  if (published) args.shift();
  const [jsonPath, tag, ...files] = args;
  if (!jsonPath || !tag || files.length === 0) {
    throw new Error('Usage: verify-release-assets.cjs [--published] <release.json> <tag> <asset>...');
  }
  const release = JSON.parse(readFileSync(jsonPath, 'utf8'));
  if (release?.tag_name !== tag || release.draft !== !published || release.prerelease !== false) {
    throw new Error(`Expected a ${published ? 'published' : 'draft'} stable release for ${tag}.`);
  }
  if (!Array.isArray(release.assets)) throw new Error('GitHub did not return an asset list.');
  const expected = new Map(files.map(file => [basename(file), file]));
  if (expected.size !== files.length) throw new Error('Local asset names must be unique.');
  const uploaded = new Map();
  for (const asset of release.assets) {
    if (!expected.has(asset?.name) || uploaded.has(asset.name)) {
      throw new Error(`Unexpected or duplicate asset: ${JSON.stringify(asset?.name)}.`);
    }
    uploaded.set(asset.name, asset);
  }
  for (const [name, file] of expected) {
    const asset = uploaded.get(name);
    if (asset?.state !== 'uploaded') throw new Error(`Asset is missing or not uploaded: ${name}.`);
    const stat = statSync(file);
    if (!stat.isFile() || stat.size <= 0 || asset.size !== stat.size) {
      throw new Error(`Asset size does not match: ${name}.`);
    }
    const hash = createHash('sha256');
    for await (const chunk of createReadStream(file)) hash.update(chunk);
    if (asset.digest !== `sha256:${hash.digest('hex')}`) {
      throw new Error(`Asset SHA-256 does not match: ${name}.`);
    }
  }
  console.log(`Verified all ${files.length} ${published ? 'published' : 'draft'} assets for ${tag}.`);
}

main().catch(error => {
  console.error(error.message);
  process.exitCode = 1;
});
