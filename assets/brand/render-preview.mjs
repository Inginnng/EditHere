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
const glyphs=[
['E',0,'M14 14H56V20L49 27H19V47H48V60H19V81H56V94H14Q5 94 5 85V23Q5 14 14 14Z'],
['d',63,'M45 14H59V94H46V87C42 93 35 96 27 96C11 96 0 83 0 65C0 47 11 34 27 34C35 34 41 37 45 42V14Z M45 65C45 53 39 46 29 46C19 46 13 53 13 65C13 77 19 84 29 84C39 84 45 77 45 65Z'],
['i',136,'M0 36H14V94H0Z M0 13H14V24L9 29H0Z'],
['t',159,'M9 19H23V36H38V42L32 48H23V73Q23 82 31 82H38V94H29Q9 94 9 74V48H0V36H9Z'],
['H',211,'M0 14H14V47H43V14H57V94H43V60H14V94H0Z'],
['e',278,'M57 68H13C14 79 20 85 30 85C38 85 44 82 49 77L57 86C50 93 41 97 30 97C11 97 0 84 0 65C0 47 12 34 30 34C47 34 58 46 58 64Z M13 57H44C43 49 38 45 30 45C21 45 16 49 13 57Z'],
['r',347,'M0 36H13V45C18 37 25 33 36 35V49C34 48 31 48 28 48C18 48 14 55 14 67V94H0Z'],
['e',389,'M57 68H13C14 79 20 85 30 85C38 85 44 82 49 77L57 86C50 93 41 97 30 97C11 97 0 84 0 65C0 47 12 34 30 34C47 34 58 46 58 64Z M13 57H44C43 49 38 45 30 45C21 45 16 49 13 57Z']
];
// Bake every glyph transform into coordinates. Each word segment then shares one
// continuous color field, and the distributed SVG needs no installed font.
function outline(d,offset){let command='',n=0;return d.match(/[A-Z]|-?\d+(?:\.\d+)?/g).map(t=>{if(/^[A-Z]$/.test(t)){command=t;n=0;return t;}const x=command==='H'||(command!=='V'&&n%2===0);n++;return String(Number((x?(Number(t)+offset)*.85:Number(t)-5).toFixed(3)));}).join(' ');}
const editPath=glyphs.slice(0,4).map(([,x,d])=>outline(d,x)).join(' ');
const herePath=glyphs.slice(4).map(([,x,d])=>outline(d,x)).join(' ');
const iconSource=fs.readFileSync(path.join(root,'assets/icons/helpdesign.svg'),'utf8');
const iconInner=iconSource.replace(/^.*?<svg\b[^>]*>/s,'').replace(/<\/svg>\s*$/s,'').replace(/<title>.*?<\/title>/s,'').replace(/<metadata>.*?<\/metadata>/s,'');
function mark(light,lockup=false){
 const editColor=light?'#F6F9FF':'#162944';
 const stops=light?['#8BC4FF','#66A0FF']:['#4A83EE','#3155D9'];
 const W=lockup?484:384;
 const paths='<g fill-rule="evenodd"><path fill="'+editColor+'" d="'+editPath+'"/><path fill="url(#edithere-here)" d="'+herePath+'"/></g>';
 return '<svg xmlns="http://www.w3.org/2000/svg" width="'+W*4+'" height="400" viewBox="0 0 '+W+' 100" role="img" aria-labelledby="edithere-title">\n<title id="edithere-title">EditHere</title>\n<desc>Original geometric EditHere wordmark, with diagonal editing details. All lettering is vector outlines; no font is required.</desc>\n<defs><linearGradient id="edithere-here" gradientUnits="userSpaceOnUse" x1="179" y1="12" x2="380" y2="92"><stop offset="0" stop-color="'+stops[0]+'"/><stop offset="1" stop-color="'+stops[1]+'"/></linearGradient></defs>\n'+(lockup?'<svg x="-13" y="-6" width="120" height="120" viewBox="8 6 52 52">'+iconInner+'</svg>\n<g transform="translate(100 0)">'+paths+'</g>':paths)+'\n</svg>\n';
}
const assets={};
for(const light of [false,true])for(const lockup of [false,true]){
 const name='edithere-'+(lockup?'lockup':'wordmark')+(light?'-light':'');
 const svg=mark(light,lockup);fs.writeFileSync(path.join(root,'assets/brand',name+'.svg'),svg);
 const image=await loadImage(Buffer.from(svg));assets[name]=image;
 const cv=createCanvas(image.width,image.height);cv.getContext('2d').drawImage(image,0,0);fs.writeFileSync(path.join(out,name+'.png'),cv.toBuffer('image/png'));
}
const cv=createCanvas(1600,1100),c=cv.getContext('2d');
for(const [light,y]of [[false,0],[true,550]]){
 const g=c.createLinearGradient(0,y,1600,y+550);g.addColorStop(0,light?'#111d35':'#f7f9fd');g.addColorStop(1,light?'#223c65':'#e3edff');c.fillStyle=g;c.fillRect(0,y,1600,550);
 const wm=assets['edithere-wordmark'+(light?'-light':'')],lock=assets['edithere-lockup'+(light?'-light':'')];
 c.drawImage(wm,362,83+y,876,228.125);
 c.drawImage(lock,102,331+y,532.4,110);
 for(const [x,size]of [[806,50],[1100,35],[1360,24]])c.drawImage(wm,x,367+y,3.84*size,size);
 c.fillStyle=light?'#8caccf':'#6b80a1';c.font='17px "Segoe UI"';c.fillText('EDITHERE / CUSTOM VECTOR WORDMARK',106,51+y);
 c.font='15px "Segoe UI"';c.fillText('HORIZONTAL LOCKUP',106,480+y);c.fillText('50 px',806,457+y);c.fillText('35 px',1100,457+y);c.fillText('24 px',1360,457+y);
}
fs.writeFileSync(path.join(out,'wordmark-preview.png'),cv.toBuffer('image/png'));
console.log(JSON.stringify({wordmark:{viewBox:'0 0 384 100',intrinsic:'1536 x 400',ratio:3.84},lockup:{viewBox:'0 0 484 100',intrinsic:'1936 x 400',ratio:4.84},preview:path.join(out,'wordmark-preview.png')}));
