// Builds the distributable single-file app. No bundler or runtime dependencies.
const fs = require('node:fs');
const path = require('node:path');
const dir = __dirname;
let html = fs.readFileSync(path.join(dir, 'src/shell.html'), 'utf8');
for (const [name, file] of [
  ['BRAND_LOCKUP', 'brand/edithere-lockup.svg'],
  ['BRAND_LOCKUP_DARK', 'brand/edithere-lockup-light.svg'],
  ['BRAND_ICON', 'icons/edithere.svg']
]) {
  const data = fs.readFileSync(path.join(dir, '../assets', file));
  html = html.replaceAll('/*__' + name + '__*/', 'data:image/svg+xml;base64,' + data.toString('base64'));
}
for (const [name, file] of [['STYLE', 'style.css'], ['LAYOUT', 'layout.js'], ['DETECTOR', 'detector.js'], ['UI', 'ui.js'], ['APP', 'app.js']]) {
  const source = fs.readFileSync(path.join(dir, 'src', file), 'utf8');
  if (name !== 'STYLE') new (require('node:vm').Script)(source, {filename: file});
  html = html.replace('/*__' + name + '__*/', () => source.replace(/<\/script/gi, '<\\/script'));
}
if (/\/\*__[A-Z_]+__\*\//.test(html)) throw new Error('Unresolved asset or source placeholder');
fs.writeFileSync(path.join(dir, 'EditHere.html'), html);
console.log('Built web/EditHere.html (' + Buffer.byteLength(html) + ' bytes)');
