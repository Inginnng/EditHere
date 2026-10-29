import { packager } from '@electron/packager';
import { cp, mkdir, mkdtemp, readFile, writeFile, access } from 'node:fs/promises';
import { resolve, join } from 'node:path';

const root = resolve('.');
const out = resolve(process.env.EDITHERE_DESKTOP_OUTPUT || 'desktop-dist');
const outputFolder = join(out, 'EditHere-win32-x64');
try { await access(outputFolder); throw new Error(`Output already exists: ${outputFolder}`); }
catch (error) { if (error.code !== 'ENOENT') throw error; }
await mkdir(resolve('.desktop-stage'), {recursive: true});
const stage = await mkdtemp(join(root, '.desktop-stage', 'preview-'));
const meta = JSON.parse(await readFile('package.json', 'utf8'));
await writeFile(join(stage, 'package.json'), JSON.stringify({name: 'edithere-desktop-preview', productName: 'EditHere', version: meta.version, main: 'desktop/main.cjs', description: 'EditHere desktop interface preview', author: 'EditHere'}, null, 2));
await cp('dist', join(stage, 'dist'), {recursive: true});
await cp('desktop', join(stage, 'desktop'), {recursive: true});
const htmlPath = join(stage, 'dist', 'index.html');
const html = await readFile(htmlPath, 'utf8');
const policy = "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self' data:; connect-src 'none'; object-src 'none'; base-uri 'self'; form-action 'none'";
await writeFile(htmlPath, html.replace('<head>', `<head><meta http-equiv="Content-Security-Policy" content="${policy}">`));
const electronMeta = JSON.parse(await readFile('node_modules/electron/package.json', 'utf8'));
const folders = await packager({dir: stage, out, name: 'EditHere', executableName: 'EditHere', platform: 'win32', arch: 'x64', electronVersion: electronMeta.version, electronZipDir: process.env.EDITHERE_ELECTRON_ZIP_DIR, appVersion: meta.version, buildVersion: meta.version, asar: true, prune: false, overwrite: false, icon: resolve('desktop/icon.ico'), win32metadata: {CompanyName: 'EditHere', FileDescription: 'EditHere desktop interface preview', ProductName: 'EditHere'}});
await cp('README.md', join(folders[0], '使用说明.md'));
await cp('DESIGN-REFERENCE.md', join(folders[0], 'DESIGN-REFERENCE.md'));
await cp('public/licenses', join(folders[0], 'licenses'), {recursive: true});
console.log(`Desktop preview: ${folders[0]}`);
