'use strict';

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { test } = require('node:test');

function draftFixture(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-release-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const tag = 'v1.2.3';
  const installer = 'EditHere-win-x64-setup.exe';
  const assets = [[installer, 'installer bytes'], [`${installer}.sha256`, 'checksum bytes'], [`${installer}.sig`, 'signature bytes']];
  const files = assets.map(([name, bytes]) => {
    const file = path.join(dir, name);
    fs.writeFileSync(file, bytes);
    return file;
  });
  const release = { tag_name: tag, draft: true, prerelease: false, assets: assets.map(([name, bytes]) => ({
    name, state: 'uploaded', size: Buffer.byteLength(bytes), digest: `sha256:${createHash('sha256').update(bytes).digest('hex')}`,
  })) };
  const jsonPath = path.join(dir, 'draft.json');
  return { release, run(candidate) {
    fs.writeFileSync(jsonPath, JSON.stringify(candidate));
    const result = spawnSync(process.execPath, [path.join(__dirname, '../scripts/verify-release-assets.cjs'), jsonPath, tag, ...files], { encoding: 'utf8' });
    assert.ifError(result.error);
    return result;
  } };
}

test('accepts an uploaded draft when all asset bytes match, regardless of listing order', t => {
  const { release, run } = draftFixture(t);
  release.assets.reverse();
  const result = run(release);
  assert.equal(result.status, 0, result.stderr);
});

test('blocks publication when the draft is wrong or uploaded assets are incomplete or changed', t => {
  const { release, run } = draftFixture(t);
  const [installer, checksum, signature] = release.assets;
  const cases = [
    [{ draft: false }, /Expected a stable release draft/],
    [{ tag_name: 'v1.2.4' }, /Expected a stable release draft/],
    [{ assets: [installer, checksum] }, /not uploaded: .*\.sig/],
    [{ assets: [installer, checksum, { ...signature, state: 'starter' }] }, /not uploaded: .*\.sig/],
    [{ assets: [installer, checksum, { ...signature, size: signature.size - 1 }] }, /size does not match: .*\.sig/],
    [{ assets: [{ ...installer, digest: `sha256:${'0'.repeat(64)}` }, checksum, signature] }, /SHA-256 does not match: .*\.exe/],
    [{ assets: [installer, checksum, signature, { ...signature, name: 'stale.sig' }] }, /Unexpected or duplicate draft asset/],
  ];
  for (const [patch, expectedError] of cases) {
    const result = run({ ...release, ...patch });
    assert.equal(result.status, 1, result.stderr);
    assert.match(result.stderr, expectedError);
  }
});
