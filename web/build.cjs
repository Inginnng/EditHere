// Builds the distributable single-file app. No bundler or runtime dependencies.
const fs = require('node:fs');
const path = require('node:path');
const dir = __dirname;
// Desktop and Web render the same checked-in SVG paths. The generated HTML
// remains self-contained; no icon package, CDN or runtime asset fetch is used.
const iconDirectory = path.join(dir, '../assets/ui-icons');
const iconManifest = JSON.parse(fs.readFileSync(path.join(iconDirectory, 'manifest.json'), 'utf8'));
if (iconManifest.schemaVersion !== 1 || !iconManifest.icons?.length || iconManifest.style?.viewBox !== '0 0 24 24' || iconManifest.style?.strokeWidth !== 2 || iconManifest.style?.linecap !== 'round' || iconManifest.style?.linejoin !== 'round') throw new Error('Unsupported shared icon manifest or visual style');
const iconIds = new Set();
const symbols = iconManifest.icons.map(icon => {
  if (!/^[a-z][a-z0-9-]*$/.test(icon.id) || iconIds.has(icon.id)) throw new Error('Invalid or duplicate shared icon ID: ' + icon.id);
  if (!/^icons\/[a-z0-9-]+\.svg$/.test(icon.file)) throw new Error('Invalid shared icon path: ' + icon.file);
  iconIds.add(icon.id);
  const source = fs.readFileSync(path.join(iconDirectory, icon.file), 'utf8').replace(/<\?xml[\s\S]*?\?>/g, '').replace(/<!--[\s\S]*?-->/g, '').trim();
  const match = source.match(/^<svg\b([^>]*)>([\s\S]*?)<\/svg>$/);
  if (!match || !/\bviewBox=["']0 0 24 24["']/.test(match[1])) throw new Error('Shared icon must use a 24 × 24 viewBox: ' + icon.id);
  const body = match[2].trim();
  if (!body || /<\/?(?!path\b|rect\b|circle\b|ellipse\b|line\b|polyline\b|polygon\b|g\b)[a-z]/i.test(body) || /\b(?:href|style|on[a-z]+)\s*=/i.test(body) || /url\s*\(/i.test(body)) throw new Error('Shared icon contains unsupported SVG content: ' + icon.id);
  return `<symbol id="i-${icon.id}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${body}</symbol>`;
}).join('\n');
// Check literal references in markup and JS, including the tutorial's icon()
// helper. A forgotten mapping must fail the build rather than render blank.
for (const file of fs.readdirSync(path.join(dir, 'src')).filter(file => /\.(?:html|js)$/.test(file))) {
  const source = fs.readFileSync(path.join(dir, 'src', file), 'utf8');
  const references = [...source.matchAll(/#i-([a-z][a-z0-9-]*)/g), ...source.matchAll(/\b(?:icon|card)\(\s*['"]([a-z][a-z0-9-]*)['"]/g)];
  for (const reference of references) if (!iconIds.has(reference[1])) throw new Error('Unknown shared icon in ' + file + ': ' + reference[1]);
}
const version = fs.readFileSync(path.join(dir, '../CMakeLists.txt'), 'utf8').match(/project\(\s*EditHere\s+VERSION\s+(\d+\.\d+\.\d+)\b/)?.[1];
if (!version) throw new Error('Cannot read the product version from CMakeLists.txt');
let html = fs.readFileSync(path.join(dir, 'src/shell.html'), 'utf8');
html = html.replace('/*__VERSION__*/', version);
html = html.replace('/*__UI_ICONS__*/', () => symbols);
const iconLicense = fs.readFileSync(path.join(iconDirectory, 'LICENSE'), 'utf8').trim();
if (iconLicense.includes('-->')) throw new Error('Shared icon license cannot be embedded in an HTML comment');
html = html.replace('/*__UI_ICON_LICENSE__*/', () => iconLicense);
for (const [name, file] of [
  ['BRAND_LOCKUP', 'brand/edithere-lockup.svg'],
  ['BRAND_LOCKUP_DARK', 'brand/edithere-lockup-light.svg'],
  ['BRAND_ICON', 'icons/edithere.svg']
]) {
  const data = fs.readFileSync(path.join(dir, '../assets', file));
  html = html.replaceAll('/*__' + name + '__*/', 'data:image/svg+xml;base64,' + data.toString('base64'));
}
for (const [name, file] of [['STYLE', 'style.css'], ['LAYOUT', 'layout.js'], ['IMAGE_FORMAT','image-format.js'], ['FEEDBACK', 'feedback.js'], ['BUNDLE','bundle.js'], ['DETECTOR', 'detector.js'], ['HELP', 'help.js'], ['UI', 'ui.js'], ['APP', 'app.js'], ['AGENT', 'agent.js']]) {
  const source = fs.readFileSync(path.join(dir, 'src', file), 'utf8') + (name === 'STYLE' ? '\n' + fs.readFileSync(path.join(dir,'src/help.css'),'utf8') : '');
  if (name !== 'STYLE') new (require('node:vm').Script)(source, {filename: file});
  html = html.replace('/*__' + name + '__*/', () => source.replace(/<\/script/gi, '<\\/script'));
}
if (/\/\*__[A-Z_]+__\*\//.test(html)) throw new Error('Unresolved asset or source placeholder');
fs.writeFileSync(path.join(dir, 'EditHere.html'), html);
console.log('Built web/EditHere.html (' + Buffer.byteLength(html) + ' bytes)');
