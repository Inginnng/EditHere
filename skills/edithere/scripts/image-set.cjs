'use strict';
// A small, portable project manifest. Agents own screenshots and update pages
// by stable ID; review sessions take a snapshot instead of following live files.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const VERSION = 'edithere-image-set-1';
const MAX_PAGES = 64;
const MAX_BYTES = 96 * 1024 * 1024;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function imageSize(bytes) {
  let width, height;
  if (bytes.length >= 24 && bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) { width=bytes.readUInt32BE(16);height=bytes.readUInt32BE(20); }
  else if (bytes.length >= 10 && /^GIF8[79]a$/.test(bytes.toString('ascii',0,6))) { width=bytes.readUInt16LE(6);height=bytes.readUInt16LE(8); }
  else if (bytes.length >= 26 && bytes.toString('ascii',0,2)==='BM') { width=Math.abs(bytes.readInt32LE(18));height=Math.abs(bytes.readInt32LE(22)); }
  else if (bytes.length >= 30 && bytes.toString('ascii',0,4)==='RIFF' && bytes.toString('ascii',8,12)==='WEBP') {
    const type=bytes.toString('ascii',12,16);
    if(type==='VP8X'){width=1+bytes.readUIntLE(24,3);height=1+bytes.readUIntLE(27,3);}
    else if(type==='VP8 '){width=bytes.readUInt16LE(26)&16383;height=bytes.readUInt16LE(28)&16383;}
    else if(type==='VP8L'){const bits=bytes.readUInt32LE(21);width=(bits&16383)+1;height=((bits>>>14)&16383)+1;}
  } else if (bytes.length >= 4 && bytes[0]===255 && bytes[1]===216) {
    for(let at=2;at+4<bytes.length;){
      if(bytes[at++]!==255)break;
      while(bytes[at]===255)at++;
      const marker=bytes[at++];if(marker===217||marker===218)break;
      if(marker===1||marker>=208&&marker<=215)continue;
      if(at+2>bytes.length)break;
      const size=bytes.readUInt16BE(at);if(size<2||at+size>bytes.length)break;
      if([192,193,194,195,197,198,199,201,202,203,205,206,207].includes(marker)&&size>=7){height=bytes.readUInt16BE(at+3);width=bytes.readUInt16BE(at+5);break;}
      at+=size;
    }
  }
  if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width>32767||height>32767||width*height>32000000)throw new Error('Unsupported or oversized image dimensions.');
  return {width,height};
}
function pageId(id) {
  if (typeof id !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$/.test(id)) throw new Error('Page id must contain 1–128 letters, digits, dots, underscores or hyphens.');
  return id;
}
function validateManifest(value) {
  if (!value || value.schemaVersion !== VERSION || typeof value.name !== 'string' || !value.name.trim() || value.name.length > 200 || !Array.isArray(value.pages) || value.pages.length > MAX_PAGES) throw new Error('Invalid EditHere image set manifest.');
  const ids = new Set();
  for (const page of value.pages) {
    pageId(page?.id);
    if (ids.has(page.id) || typeof page.name !== 'string' || !page.name.trim() || page.name.length > 200 || typeof page.path !== 'string' || !page.path || page.path.includes('\0')) throw new Error('Invalid or duplicate image set page.');
    if (page.sha256 !== undefined && !/^[a-f0-9]{64}$/.test(page.sha256)) throw new Error('Invalid page SHA-256.');
    ids.add(page.id);
  }
  return value;
}
function readManifest(file) {
  const stat = fs.statSync(file);
  if (!stat.isFile() || stat.size > 1024 * 1024) throw new Error('An image set manifest must be a JSON file smaller than 1 MiB.');
  return validateManifest(JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '')));
}
function snapshotManifest(file) {
  const inputPath = path.resolve(file), manifest = readManifest(inputPath);
  if (!manifest.pages.length) throw new Error('Add at least one page before starting a review.');
  let bytes = 0, pixels = 0;
  const pages = manifest.pages.map(page => {
    const imagePath = path.resolve(path.dirname(inputPath), page.path), stat = fs.statSync(imagePath);
    if (!stat.isFile() || stat.size > 48 * 1024 * 1024) throw new Error('Page image must be a file smaller than 48 MiB: ' + page.name);
    const data = fs.readFileSync(imagePath), sha256 = hash(data);
    const dimensions = imageSize(data);pixels += dimensions.width * dimensions.height;
    if(pixels>128000000)throw new Error('Image set exceeds 128 million pixels.');
    if (page.sha256 && page.sha256 !== sha256) throw new Error('Page image changed; run upsert to update its manifest: ' + page.id);
    const mime = {'.png':'png','.jpg':'jpeg','.jpeg':'jpeg','.webp':'webp','.gif':'gif','.bmp':'bmp'}[path.extname(imagePath).toLowerCase()];
    if (!mime) throw new Error('Unsupported page image: ' + imagePath);
    const image = 'data:image/' + mime + ';base64,' + data.toString('base64');
    bytes += image.length;
    if (bytes > MAX_BYTES - 1024 * 1024) throw new Error('Review images exceed 95 MiB; resize the screenshots.');
    return {id:page.id, name:page.name, image};
  });
  return {kind:'set', name:manifest.name, inputPath, pages};
}
function writeManifest(file, value, {create = false} = {}) {
  validateManifest(value);
  const output = path.resolve(file);
  fs.mkdirSync(path.dirname(output), {recursive:true});
  const bytes = JSON.stringify(value, null, 2) + '\n';
  if (create) fs.writeFileSync(output, bytes, {flag:'wx'});
  else {
    const temp = output + '.' + crypto.randomUUID() + '.tmp';
    try {
      fs.writeFileSync(temp, bytes, {flag:'wx'});
      // Windows scanners can briefly hold the destination without delete sharing.
      // Retry the atomic replacement; never remove the last valid manifest first.
      for (let attempt = 0; ; attempt++) {
        try { fs.renameSync(temp, output); break; }
        catch (error) {
          if (attempt === 3 || !['EPERM', 'EACCES', 'EBUSY'].includes(error.code)) throw error;
          Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 50 * 2 ** attempt);
        }
      }
    }
    finally { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } }
  }
  return value;
}
function upsertPage(file, {id, name, imagePath}) {
  pageId(id);
  const value = readManifest(file), index = value.pages.findIndex(page => page.id === id);
  const image = path.resolve(imagePath), stat = fs.statSync(image);
  if (!stat.isFile() || stat.size > 48 * 1024 * 1024) throw new Error('Invalid page image.');
  if(!/\.(png|jpe?g|webp|gif|bmp)$/i.test(image))throw new Error('Unsupported page image type.');
  const bytes=fs.readFileSync(image);imageSize(bytes);
  const page = {id, name:name || value.pages[index]?.name || path.basename(image), path:path.relative(path.dirname(path.resolve(file)), image).split(path.sep).join('/'), sha256:hash(bytes)};
  if (index < 0) value.pages.push(page); else value.pages[index] = page;
  return writeManifest(file, value);
}
function bundledProject(file) {
  const snapshot=snapshotManifest(file);
  const pages=snapshot.pages.map(page=>({...page,...imageSize(Buffer.from(page.image.split(',')[1],'base64')),timestampUs:null,notes:[],layout:null}));
  return {schemaVersion:'edithere-web-project-1',tool:'EditHere Web',savedAt:new Date().toISOString(),kind:'set',name:snapshot.name,width:pages[0].width,height:pages[0].height,pages,groupNotes:[]};
}
module.exports = {VERSION, MAX_PAGES, readManifest, snapshotManifest, writeManifest, upsertPage, bundledProject, imageSize};
