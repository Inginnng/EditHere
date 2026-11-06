'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { canonicalVersion, payload, publicHex, signInstaller, verifyInstaller } = require('../scripts/sign-update.cjs');
test('sign final bytes, verify independently and reject changed identity or data', async t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-signer-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const file = path.join(dir, 'EditHere-win-x64-setup.exe');
  fs.writeFileSync(file, 'payload');
  const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const privatePem = pair.privateKey.export({ format: 'pem', type: 'pkcs8' });
  const options = { file, version: '99.0.0', tag: 'v99.0.0', sourceVersion: '99.0.0', privatePem,
    expectedPublicHex: publicHex(pair.privateKey) };
  const metadata = await signInstaller(options);
  const hash = await verifyInstaller({ ...options, metadata });
  assert.equal(hash, crypto.createHash('sha256').update('payload').digest('hex'));
  assert(crypto.verify('sha256', payload(options.version, path.basename(file), hash),
    { key: pair.publicKey, dsaEncoding: 'ieee-p1363' }, Buffer.from(metadata.toString().split('\n')[4], 'base64')));
  for (const patch of [{ privatePem: '' }, { privatePem: 'malformed' }, { expectedPublicHex: '' },
    { expectedPublicHex: '00'.repeat(64) }, { tag: '99.0.0' }, { tag: 'v99.0.1' }, { sourceVersion: '98.0.0' },
    { version: '099.0.0' }, { privatePem: crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' }).privateKey.export({format:'pem',type:'pkcs8'}) },
    { privatePem: crypto.generateKeyPairSync('ec', { namedCurve: 'secp384r1' }).privateKey.export({format:'pem',type:'pkcs8'}) }])
    await assert.rejects(signInstaller({ ...options, ...patch }));
  for (const bad of [Buffer.alloc(1025), Buffer.alloc(0), Buffer.from(metadata.toString().replace('99.0.0', '98.0.0')),
    Buffer.from(metadata.toString().replace(hash, '0'.repeat(64))), Buffer.concat([metadata, Buffer.from('\n')]),
    Buffer.from(metadata.toString().replace(/\n/g, '\r\n')), Buffer.from(metadata.toString().replace('setup.exe', 'setup.zip')),
    Buffer.concat([metadata.subarray(0, -89), Buffer.from('A'.repeat(88) + '\n')])])
    await assert.rejects(verifyInstaller({ ...options, metadata: bad }));
  fs.appendFileSync(file, 'changed after signing');
  await assert.rejects(verifyInstaller({ ...options, metadata }));
  const oldName = path.join(dir, 'EditHere-99.0.0-win-x64-setup.exe');
  fs.copyFileSync(file, oldName);
  await assert.rejects(signInstaller({ ...options, file: oldName }));
});
test('canonical versions and private key diagnostics never reach stdout/stderr', () => {
  for (const version of ['1.2.3\n', '01.2.3', '1.2', '1.2.3-beta', '2147483648.0.0', '1.2.3\r'])
    assert.equal(canonicalVersion(version), false);
  const sentinel = 'PRIVATE_SENTINEL_MUST_NOT_APPEAR';
  const result = spawnSync(process.execPath, ['scripts/sign-update.cjs', '--sign', 'missing.exe', 'v0.12.8'],
    { encoding: 'utf8', env: { ...process.env, EDITHERE_UPDATE_SIGNING_KEY: sentinel } });
  assert.equal(result.status, 1);
  assert(!`${result.stdout}${result.stderr}`.includes(sentinel));
});
test('independent fixtures match P1363 verification and reject another key', () => {
  const pub = fs.readFileSync(path.join(__dirname, 'fixtures/update-signature-public.h'), 'utf8').match(/ProjectPublicKeyHex\[\] = "([a-f0-9]+)"/)[1];
  const bytes = Buffer.from(pub, 'hex');
  const key = crypto.createPublicKey({ format: 'jwk', key: { kty: 'EC', crv: 'P-256',
    x: bytes.subarray(0, 32).toString('base64url'), y: bytes.subarray(32).toString('base64url') } });
  for (const [name, expected] of [['update-signature.txt', true], ['update-signature-wrong-key.txt', false]]) {
    const metadata = fs.readFileSync(path.join(__dirname, 'fixtures', name));
    const lines = metadata.toString('ascii').split('\n');
    assert.equal(crypto.verify('sha256', Buffer.from(lines.slice(0, 4).join('\n') + '\n'),
      { key, dsaEncoding: 'ieee-p1363' }, Buffer.from(lines[4], 'base64')), expected);
  }
});
test('release CLI signs with step environment, verifies without key and refuses overwrite or mismatch', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'edithere-sign-cli-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  fs.mkdirSync(path.join(dir, 'scripts')); fs.mkdirSync(path.join(dir, 'app'));
  fs.copyFileSync(path.join(__dirname, '../scripts/sign-update.cjs'), path.join(dir, 'scripts/sign-update.cjs'));
  fs.writeFileSync(path.join(dir, 'CMakeLists.txt'), 'project(EditHere VERSION 1.2.3 LANGUAGES C CXX)');
  const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  fs.writeFileSync(path.join(dir, 'app/updatekey.h'), `inline constexpr char ProjectPublicKeyHex[] = "${publicHex(pair.privateKey)}";`);
  const file = path.join(dir, 'EditHere-win-x64-setup.exe'); fs.writeFileSync(file, 'final installer bytes');
  const privatePem = pair.privateKey.export({ format: 'pem', type: 'pkcs8' });
  const invoke = (mode, tag = 'v1.2.3', key = undefined) => {
    const env = { ...process.env }; delete env.EDITHERE_UPDATE_SIGNING_KEY;
    if (key !== undefined) env.EDITHERE_UPDATE_SIGNING_KEY = key;
    const result = spawnSync(process.execPath, [path.join(dir, 'scripts/sign-update.cjs'), mode, file, tag], { env, encoding: 'utf8' });
    assert(!`${result.stdout}${result.stderr}`.includes(privatePem)); return result;
  };
  assert.equal(invoke('--sign').status, 1);
  assert.equal(invoke('--sign', 'v1.2.4', privatePem).status, 1);
  assert.equal(invoke('--sign', 'v1.2.3', privatePem).status, 0);
  const metadata = fs.readFileSync(file + '.sig');
  assert.equal(invoke('--verify').status, 0);
  assert.equal(invoke('--sign', 'v1.2.3', privatePem).status, 1);
  assert.deepEqual(fs.readFileSync(file + '.sig'), metadata);
  fs.appendFileSync(file, 'changed'); assert.equal(invoke('--verify').status, 1);
  fs.writeFileSync(file + '.sig', Buffer.alloc(1025)); assert.equal(invoke('--verify').status, 1);
});
