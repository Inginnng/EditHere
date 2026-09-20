import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const out=path.resolve(process.argv[2]||path.join(root,'artifacts/brand'));
fs.mkdirSync(out,{recursive:true});
const require=createRequire(path.join(root,'artifacts/helpdesign-continuous-film/render.mjs'));
const {createCanvas,loadImage,GlobalFonts}=require('@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/segoeui.ttf','Segoe UI');
// The approved SVG outlines are the source of truth. Rendering previews must
// never replace the logo with a generated or obsolete lettering design.
const assets={};
for(const light of [false,true])for(const lockup of [false,true]){
 const name='edithere-'+(lockup?'lockup':'wordmark')+(light?'-light':'');
 const im=await loadImage(path.join(root,'assets/brand',name+'.svg'));assets[name]=im;
 const cv=createCanvas(im.width,im.height);cv.getContext('2d').drawImage(im,0,0);
 fs.writeFileSync(path.join(out,name+'.png'),cv.toBuffer('image/png'));
}
const cv=createCanvas(1600,1040),c=cv.getContext('2d');
function draw(im,x,y,w){c.drawImage(im,x,y,w,w*im.height/im.width);}
for(const [light,y] of [[false,0],[true,520]]){
 c.fillStyle=light?'#132039':'#f7f9fd';c.fillRect(0,y,1600,520);
 const wm=assets['edithere-wordmark'+(light?'-light':'')],lock=assets['edithere-lockup'+(light?'-light':'')];
 draw(wm,345,86+y,910);draw(lock,100,330+y,560);
 for(const [x,w] of [[820,265],[1130,210],[1390,130]])draw(wm,x,370+y,w);
 c.fillStyle=light?'#91a4c0':'#74849b';c.font='18px "Segoe UI"';c.fillText('EDITHERE / A1 AMBER CAP',105,54+y);
 c.font='15px "Segoe UI"';c.fillText('ICON + WORDMARK',105,473+y);c.fillText('BLUE #246BD9 / AMBER #FFB352',820,473+y);
}
fs.writeFileSync(path.join(out,'wordmark-preview.png'),cv.toBuffer('image/png'));
console.log(JSON.stringify({wordmark:{viewBox:'0 0 572 128',ratio:572/128},lockup:{viewBox:'0 0 722 128',ratio:722/128},preview:path.join(out,'wordmark-preview.png')}));
