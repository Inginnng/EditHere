// The ordinary offline app keeps its dependency-free builder. Only this adapter
// bundles the official MCP Apps SDK; release users do not need npm or a CDN.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
import crypto from 'node:crypto';
import {build} from 'esbuild';

const directory = path.dirname(fileURLToPath(import.meta.url));
const web = path.dirname(directory);
execFileSync(process.execPath, [path.join(web, 'build.cjs')], {stdio:'inherit'});
const browser = await build({
  entryPoints:[path.join(web, 'src/mcp-app.js')], bundle:true, write:false,
  platform:'browser', format:'iife', target:'es2022', minify:true, metafile:true,
  nodePaths:[path.join(directory, 'node_modules')], legalComments:'inline'
});
const bridge = browser.outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
let html = fs.readFileSync(path.join(web, 'EditHere.html'), 'utf8');
// Replace only the connector script, leaving the existing application and brand.
const tail = /<script>\/\* Activated only by a capability URL[\s\S]*?<\/script>\s*<\/body>/;
if (!tail.test(html)) throw new Error('Cannot locate the HTTP adapter in the built app.');
html = html.replace(tail, () => '<script data-edithere-mcp-bridge>' + bridge + '</script>\n</body>');
fs.writeFileSync(path.join(web, 'EditHere.mcp.html'), html);
const server = await build({
  entryPoints:[path.join(directory, 'server.mjs')], bundle:true,
  outfile:path.join(directory, 'server.bundle.cjs'), platform:'node', format:'cjs',
  target:'node20', minify:true, metafile:true, legalComments:'inline',
  define:{'EDITHERE_MCP_DIR':'__dirname','import.meta.url':'EDITHERE_IMPORT_META_URL'},
  banner:{js:'const EDITHERE_IMPORT_META_URL = require("node:url").pathToFileURL(__filename).href;'}
});

// Full notices for the dependencies actually embedded in either distributable.
const packages = new Map();
for (const input of [...Object.keys(browser.metafile.inputs), ...Object.keys(server.metafile.inputs)]) {
  const absolute = path.resolve(input);
  const parts = absolute.split(path.sep), index = parts.lastIndexOf('node_modules');
  if (index < 0) continue;
  const end = parts[index + 1].startsWith('@') ? index + 3 : index + 2;
  const root = parts.slice(0, end).join(path.sep);
  const metadata = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8'));
  const license = fs.readdirSync(root).find(name => /^licen[cs]e(?:\.md|\.txt)?$/i.test(name));
  if (!license) throw new Error('Missing full license for bundled dependency ' + metadata.name);
  packages.set(metadata.name, {metadata, license:fs.readFileSync(path.join(root, license), 'utf8')});
}
const notices = ['# EditHere Web MCP bundled dependencies\n\nBuilt from the pinned web/mcp/package-lock.json. Cowart informed the transport workflow; its source and branding are not bundled.\n'];
for (const [name, {metadata, license}] of [...packages].sort(([a],[b]) => a.localeCompare(b))) {
  notices.push(`\n## ${name} ${metadata.version}\n\nLicense: ${metadata.license || 'See below'}\n\n${license.trim()}\n`);
}
fs.writeFileSync(path.join(web, 'MCP-THIRD-PARTY-NOTICES.md'), notices.join(''));
// Git may check text out as CRLF. Hash canonical UTF-8/LF text so the downloaded
// build remains verifiable on Windows, macOS and Linux without changing assets.
const root = path.dirname(web), hash = file => crypto.createHash('sha256').update(fs.readFileSync(file,'utf8').replace(/\r\n/g,'\n')).digest('hex');
const sourcePaths = new Set([
  ...Object.keys(browser.metafile.inputs), ...Object.keys(server.metafile.inputs),
  ...fs.readdirSync(path.join(web,'src')).filter(file=>/\.(?:js|html|css)$/.test(file)).map(file=>path.join(web,'src',file)),
  path.join(web,'build.cjs'), path.join(directory,'build.mjs'),path.join(directory,'package.json'),path.join(directory,'package-lock.json'),
  ...['brand/edithere-lockup.svg','brand/edithere-lockup-light.svg','icons/edithere.svg'].map(file=>path.join(root,'assets',file))
].map(file=>path.resolve(file)).filter(file=>!file.split(path.sep).includes('node_modules')));
const sources=Object.fromEntries([...sourcePaths].sort().map(file=>[path.relative(root,file).split(path.sep).join('/'),hash(file)]));
const artifacts=Object.fromEntries(['web/EditHere.mcp.html','web/mcp/server.bundle.cjs','web/MCP-THIRD-PARTY-NOTICES.md'].map(file=>[file,hash(path.join(root,file))]));
fs.writeFileSync(path.join(directory,'build-info.json'),JSON.stringify({schemaVersion:1,hashEncoding:'utf8-lf',sources,artifacts},null,2)+'\n');
console.log('Built web/EditHere.mcp.html and web/mcp/server.bundle.cjs with ' + packages.size + ' dependency notices.');
