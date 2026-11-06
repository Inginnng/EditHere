#!/usr/bin/env node
'use strict';
// Private material enters ONLY through the signing step's environment. Never
// print crypto exceptions: provider diagnostics can contain sensitive inputs.
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { pipeline } = require('node:stream/promises');
const DOMAIN = 'EditHere Windows Update Signature v1\n';
function requireThat(ok, message) { if (!ok) throw new Error(message); }
function canonicalVersion(version) {
  return typeof version === 'string' && /^(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})$/.test(version) &&
    !version.endsWith('\n') && version.split('.').every(n => Number(n) <= 2147483647);
}
function payload(version, name, hash) {
  requireThat(canonicalVersion(version), 'Invalid canonical version');
  requireThat(name === 'EditHere-win-x64-setup.exe' || name === `EditHere-${version}-win-x64-setup.exe`, 'Invalid installer name');
  requireThat(typeof hash === 'string' && hash.length === 64 && /^[0-9a-f]{64}$/.test(hash), 'Invalid SHA-256');
  return Buffer.from(`${DOMAIN}${version}\n${name}\n${hash}\n`, 'ascii');
}
function publicHex(key) {
  const pub = crypto.createPublicKey(key);
  const jwk = pub.export({ format: 'jwk' });
  requireThat(jwk.kty === 'EC' && jwk.crv === 'P-256', 'Signing key must use P-256');
  const x = Buffer.from(jwk.x, 'base64url'), y = Buffer.from(jwk.y, 'base64url');
  requireThat(x.length === 32 && y.length === 32, 'Invalid public coordinates');
  return Buffer.concat([x, y]).toString('hex');
}
function publicKey(hex) {
  requireThat(typeof hex === 'string' && hex.length === 128 && /^[0-9a-f]{128}$/.test(hex), 'No valid compiled project public key');
  const bytes = Buffer.from(hex, 'hex');
  return crypto.createPublicKey({ format: 'jwk', key: { kty: 'EC', crv: 'P-256',
    x: bytes.subarray(0, 32).toString('base64url'), y: bytes.subarray(32).toString('base64url') } });
}
async function fileHash(file) {
  requireThat(fs.statSync(file).isFile() && fs.statSync(file).size > 0, 'Installer missing or empty');
  const hash = crypto.createHash('sha256');
  await pipeline(fs.createReadStream(file), hash);
  return hash.digest('hex');
}
function validateRelease(version, tag, sourceVersion) {
  requireThat(canonicalVersion(version) && tag === `v${version}` && sourceVersion === version, 'Tag and source version must match exactly');
}
function parseMetadata(metadata, version, name, hash) {
  requireThat(Buffer.isBuffer(metadata) && metadata.length <= 1024, 'Signature metadata too large');
  const data = payload(version, name, hash);
  requireThat(metadata.subarray(0, data.length).equals(data), 'Signature fields do not match this installer');
  const tail = metadata.subarray(data.length);
  requireThat(tail.length === 89 && tail[88] === 10, 'Malformed signature metadata');
  const encoded = tail.subarray(0, 88).toString('ascii');
  const signature = Buffer.from(encoded, 'base64');
  requireThat(signature.length === 64 && signature.toString('base64') === encoded &&
    tail.subarray(0, 88).equals(Buffer.from(encoded, 'ascii')), 'Malformed P1363 signature');
  return { data, signature };
}
async function verifyInstaller({ file, metadata, version, tag, sourceVersion, expectedPublicHex }) {
  validateRelease(version, tag, sourceVersion);
  const hash = await fileHash(file);
  const { data, signature } = parseMetadata(metadata, version, path.basename(file), hash);
  requireThat(crypto.verify('sha256', data, { key: publicKey(expectedPublicHex), dsaEncoding: 'ieee-p1363' }, signature), 'Update signature verification failed');
  return hash;
}
async function signInstaller({ file, version, tag, sourceVersion, privatePem, expectedPublicHex }) {
  validateRelease(version, tag, sourceVersion);
  requireThat(path.basename(file) === 'EditHere-win-x64-setup.exe', 'Sign only the final stable installer name');
  requireThat(typeof privatePem === 'string' && privatePem.length > 0 && privatePem.length <= 4096 &&
    /^-----BEGIN PRIVATE KEY-----\n[A-Za-z0-9+/=\n]+\n-----END PRIVATE KEY-----\n?$/.test(privatePem) &&
    !privatePem.endsWith('\n\n'), 'Missing or malformed PKCS#8 signing key');
  const key = crypto.createPrivateKey({ key: privatePem, format: 'pem', type: 'pkcs8' });
  publicKey(expectedPublicHex);
  requireThat(publicHex(key) === expectedPublicHex, 'Signing key does not match compiled client key');
  const hash = await fileHash(file);
  const data = payload(version, path.basename(file), hash);
  const signature = crypto.sign('sha256', data, { key, dsaEncoding: 'ieee-p1363' });
  requireThat(signature.length === 64, 'Invalid signing result');
  const metadata = Buffer.concat([data, Buffer.from(signature.toString('base64') + '\n', 'ascii')]);
  await verifyInstaller({ file, metadata, version, tag, sourceVersion, expectedPublicHex });
  return metadata;
}
async function main() {
  const [mode, file, tag, ...extra] = process.argv.slice(2);
  requireThat(['--sign', '--verify'].includes(mode) && file && tag && !extra.length, 'Usage: sign-update.cjs --sign|--verify <installer> <vX.Y.Z>');
  const root = path.resolve(__dirname, '..');
  const sourceVersion = fs.readFileSync(path.join(root, 'CMakeLists.txt'), 'utf8').match(/project\(EditHere VERSION ([0-9.]+)/)?.[1];
  const expectedPublicHex = fs.readFileSync(path.join(root, 'app/updatekey.h'), 'utf8').match(/ProjectPublicKeyHex\[\] = "([0-9a-f]*)"/)?.[1];
  const options = { file, tag, version: tag.startsWith('v') ? tag.slice(1) : '', sourceVersion, expectedPublicHex };
  if (mode === '--sign') {
    const privatePem = process.env.EDITHERE_UPDATE_SIGNING_KEY;
    delete process.env.EDITHERE_UPDATE_SIGNING_KEY;
    const metadata = await signInstaller({ ...options, privatePem });
    // Do not overwrite an already signed artifact on retry.
    fs.writeFileSync(`${file}.sig`, metadata, { flag: 'wx' });
  }
  requireThat(fs.statSync(`${file}.sig`).size <= 1024, 'Signature metadata too large');
  await verifyInstaller({ ...options, metadata: fs.readFileSync(`${file}.sig`) });
  console.log('Installer update signature verified.');
}
module.exports = { canonicalVersion, payload, publicHex, signInstaller, verifyInstaller, parseMetadata };
if (require.main === module) main().catch(() => { console.error('Update signing/verification failed; check tag, source, final installer and matching signing key.'); process.exitCode = 1; });
