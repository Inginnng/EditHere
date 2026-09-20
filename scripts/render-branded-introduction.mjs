import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(process.argv[2]||'.');
const require=createRequire(path.join(root,'artifacts/helpdesign-continuous-film/render.mjs'));
const {createCanvas,loadImage,GlobalFonts}=require('@napi-rs/canvas');
const here=path.join(root,'artifacts/promo-narrated');
for(const f of ['msyh.ttc','msyhbd.ttc'])GlobalFonts.registerFromPath('C:/Windows/Fonts/'+f,'Microsoft YaHei');
GlobalFonts.registerFromPath('C:/Windows/Fonts/consola.ttf','Consolas');
if(!process.argv[3])throw new Error('Usage: node scripts/render-branded-introduction.mjs ROOT OUTPUT [--stills]');
const outputDir=path.resolve(process.argv[3]);mkdirSync(outputDir,{recursive:true});
const brandLogo=await loadImage(readFileSync(path.join(root,'assets/icons/helpdesign.svg')));
const wordmark=await loadImage(path.join(root,'assets/brand/edithere-wordmark.svg'));
const wordmarkLight=await loadImage(path.join(root,'assets/brand/edithere-wordmark-light.svg'));
function brand(c,x,y,w,light=false){const im=light?wordmarkLight:wordmark;c.drawImage(im,x,y,w,w*im.height/im.width);}
function rebrandNative(img){const canvas=createCanvas(img.width,img.height),c=canvas.getContext('2d');c.drawImage(img,0,0);const p=c.getImageData(93,12,1,1).data;c.fillStyle=`rgb(${p[0]},${p[1]},${p[2]})`;c.fillRect(9,8,84,27);c.drawImage(brandLogo,11,11,22,22);c.font='400 13px "Microsoft YaHei"';c.textBaseline='alphabetic';c.fillStyle='#25252b';c.fillText('EditHere',37,26);return canvas;}
const gameAssets=path.resolve(here,'../promo-hud');
const W=1920,H=1080,FPS=30,START=12;
const timeline=JSON.parse(readFileSync(path.join(here,'timeline.json'),'utf8'));
const feedback=JSON.parse(readFileSync(path.join(here,'feedback-readable.json'),'utf8'));
let end=0; for(const f of timeline.frames){f.at=end;end+=f.duration;f.end=end;}
const focusBands=[];for(let j=0;j<timeline.frames.length;j++){if(timeline.frames[j].action==='typing'){const first=timeline.frames[j].at;while(j+1<timeline.frames.length&&timeline.frames[j+1].action==='typing')j++;focusBands.push([first,timeline.frames[j].end]);}}
const nativeEnd=START+end,aiEnd=nativeEnd+12,resultEnd=aiEnd+7,moveEnd=resultEnd+5,gameEnd=moveEnd+5;
const chartTimeline=JSON.parse(readFileSync(path.join(here,'charts/timeline.json'),'utf8'));let chartDuration=0;for(const f of chartTimeline.frames){f.at=chartDuration;chartDuration+=f.duration;f.end=chartDuration;}
const chartStart=gameEnd,chartNativeStart=chartStart+18,chartNativeEnd=chartNativeStart+chartDuration,chartAiEnd=chartNativeEnd+10,chartResultEnd=chartAiEnd+8,outroStart=chartResultEnd+6,RAW_DURATION=Math.ceil(outroStart+6);
const inserts=[{raw:3,duration:4.5,key:'game',kicker:'01 / 游戏界面',title:'把修改，直接放在画面上',detail:'点选 · 框选 · 全局批注'},{raw:nativeEnd,duration:4,key:'feedback',kicker:'02 / 从批注到执行',title:'一份反馈，交给 AI',detail:'原图 · 修改意见 · 布局位置'},{raw:chartStart,duration:4.5,key:'data',kicker:'03 / 数据图表',title:'位置、范围、数值，都能说清',detail:'移动图例 · 修正坐标 · 核对数据'},{raw:chartNativeEnd,duration:4,key:'result',kicker:'04 / 修改落地',title:'让图表符合你的表达',detail:'按数据核对，按批注调整'}];
let insertTotal=0;for(const s of inserts){s.start=s.raw+insertTotal;insertTotal+=s.duration;s.end=s.start+s.duration;}
const DURATION=RAW_DURATION+insertTotal,toMovie=t=>t+inserts.filter(s=>s.raw<=t).reduce((a,s)=>a+s.duration,0);
writeFileSync(path.join(outputDir,'film-timing.json'),JSON.stringify({duration:DURATION,original:3,capture:START,nativeEnd,aiEnd,resultEnd,moveEnd,outroStart,chartStart,chartNativeStart,chartNativeEnd,chartAiEnd,chartResultEnd,frames:timeline.frames.length,chartFrames:chartTimeline.frames.length,rawDuration:RAW_DURATION,transitions:inserts,presentation:{gameStart:toMovie(3),capture:toMovie(8),nativeStart:toMovie(START),nativeEnd:toMovie(nativeEnd),aiEnd:toMovie(aiEnd),resultEnd:toMovie(resultEnd),chartStart:toMovie(chartStart),chartNativeStart:toMovie(chartNativeStart),chartNativeEnd:toMovie(chartNativeEnd),chartAiEnd:toMovie(chartAiEnd),chartResultEnd:toMovie(chartResultEnd),outroStart:toMovie(outroStart)}},null,2));
const assets={}; for(const key of ['original','annotated','annotated-editor','result']) assets[key]=await loadImage(path.join(here,key+'.png'));
assets.marked=createCanvas(1120,720);assets.marked.getContext('2d').drawImage(assets.annotated,24,24,1120,720,0,0,1120,720);
assets.outro=createCanvas(W,H);{const c=assets.outro.getContext('2d'),g=c.createLinearGradient(0,0,W,H);g.addColorStop(0,'#0c1730');g.addColorStop(1,'#182c4a');c.fillStyle=g;c.fillRect(0,0,W,H);c.drawImage(brandLogo,868,205,184,184);c.textAlign='center';c.textBaseline='alphabetic';c.fillStyle='#f9fbff';brand(c,650,413,620,true);c.font='500 43px "Microsoft YaHei"';c.fillStyle='#b4c8e8';c.fillText('改这里',960,631);c.font='400 32px "Microsoft YaHei"';c.fillStyle='#e2ecfa';c.fillText('截图，批注，交给 AI。',960,726);c.font='400 23px "Microsoft YaHei"';c.fillStyle='#93aacb';c.fillText('非商业使用免费 · 商业使用需授权',960,847);c.font='400 21px "Microsoft YaHei"';c.fillText('github.com/Inginnng/EditHere',960,895);}
assets.logo=brandLogo;assets['annotated-editor']=rebrandNative(assets['annotated-editor']);
const cv=createCanvas(W,H),ctx=cv.getContext('2d');
const clamp=x=>Math.max(0,Math.min(1,x)),mix=(a,b,t)=>a+(b-a)*t,ease=t=>{t=clamp(t);return t*t*(3-2*t)},go=(t,a,b)=>ease((t-a)/(b-a));
function box(c,x,y,w,h,r,fill,stroke){c.beginPath();c.roundRect(x,y,w,h,r);if(fill){c.fillStyle=fill;c.fill();}if(stroke){c.strokeStyle=stroke;c.lineWidth=1;c.stroke();}}
function txt(c,s,x,y,size=26,color='#17233b',bold=false,align='left'){c.font=`${bold?600:400} ${size}px "Microsoft YaHei"`;c.fillStyle=color;c.textAlign=align;c.textBaseline='alphabetic';c.fillText(s,x,y);}
function wrap(c,s,x,y,width,size,color='#42506a',line=1.55){c.font=`${size}px "Microsoft YaHei"`;let row='',j=0;for(const a of s){if(c.measureText(row+a).width>width){txt(c,row,x,y+j*size*line,size,color);row='';j++;}row+=a;}txt(c,row,x,y+j*size*line,size,color);return j+1;}
function shadow(c,blur=40,a='#0b204029',y=14){c.shadowColor=a;c.shadowBlur=blur;c.shadowOffsetY=y;}
function card(c,img,x,y,w,h,r=15){c.save();shadow(c);box(c,x,y,w,h,r,'white');c.shadowColor='transparent';c.beginPath();c.roundRect(x,y,w,h,r);c.clip();c.drawImage(img,x,y,w,h);c.restore();}
const bgCanvas=createCanvas(W,H),bctx=bgCanvas.getContext('2d');bctx.fillStyle='#f7f9fd';bctx.fillRect(0,0,W,H);let g=bctx.createRadialGradient(1740,950,0,1740,950,1050);g.addColorStop(0,'#cddfff');g.addColorStop(1,'#eaf1ff00');bctx.fillStyle=g;bctx.fillRect(0,0,W,H);g=bctx.createRadialGradient(180,130,0,180,130,900);g.addColorStop(0,'#e9e6fb');g.addColorStop(1,'#e9e6fb00');bctx.fillStyle=g;bctx.fillRect(0,0,W,H);
function background(t){ctx.drawImage(bgCanvas,0,0);ctx.drawImage(brandLogo,67,22,35,35);brand(ctx,113,27,111);txt(ctx,'改这里',235,49,16,'#76849a');ctx.fillStyle='#d7dfed';ctx.fillRect(70,1052,1780,2);ctx.fillStyle='#286cff';ctx.fillRect(70,1052,1780*t/DURATION,3);}
function pointer(x,y,click=0){ctx.save();ctx.translate(x,y);if(click>0){ctx.strokeStyle=`rgba(43,116,255,${1-click})`;ctx.lineWidth=3;ctx.beginPath();ctx.arc(0,0,14+click*24,0,Math.PI*2);ctx.stroke();}shadow(ctx,6,'#00000055',2);ctx.beginPath();ctx.moveTo(0,0);ctx.lineTo(2,31);ctx.lineTo(10,24);ctx.lineTo(18,38);ctx.lineTo(24,34);ctx.lineTo(17,21);ctx.lineTo(31,19);ctx.closePath();ctx.fillStyle='#fff';ctx.fill();ctx.shadowColor='transparent';ctx.strokeStyle='#133564';ctx.lineWidth=1.7;ctx.stroke();ctx.restore();}
function pill(s,x,y,w=260,active=false){box(ctx,x,y,w,42,21,active?'#276bff':'#e8effb');txt(ctx,s,x+w/2,y+28,18,active?'white':'#4870ae',true,'center');}
let loaded=new Map();
async function nativeImage(index){if(!loaded.has(index)){loaded.set(index,rebrandNative(await loadImage(path.join(here,timeline.frames[index].file))));if(loaded.size>5)loaded.delete(loaded.keys().next().value);}return loaded.get(index);}
function findIndex(t){let a=0,b=timeline.frames.length-1;while(a<b){const m=Math.floor((a+b+1)/2);if(timeline.frames[m].at<=t)a=m;else b=m-1;}return a;}
function intro(t){const settle=go(t,0,1.1);ctx.save();ctx.translate(260,560);ctx.rotate(-.075);card(ctx,assets.original,-370,-238,740,476);ctx.restore();ctx.save();ctx.translate(1690,560);ctx.rotate(.075);card(ctx,assets['annotated-editor'],-410,-243,820,486);ctx.restore();ctx.drawImage(assets.logo,907,188+(1-settle)*25,106,106);brand(ctx,790,322,340);txt(ctx,'让 AI 看懂',960,513,68,'#15233c',true,'center');txt(ctx,'你想怎么改。',960,620,80,'#266aff',true,'center');txt(ctx,'从截图，到修改完成。',960,713,29,'#7d8ca4',false,'center');}
const captureSequence=JSON.parse(readFileSync(path.join(here,'capture-timeline.json'),'utf8')).frames;let captureDuration=0;for(const f of captureSequence){f.at=captureDuration;captureDuration+=f.duration;f.img=await loadImage(path.join(here,f.file));}
function capture(t){const u=t-8;{const local=Math.min(captureDuration-.001,u/4*captureDuration);let f=captureSequence[0];for(const v of captureSequence)if(v.at<=local)f=v;const x=300,y=160,w=1320,h=880;card(ctx,f.img,x,y,w,h,10);pointer(x+f.cursor[0]*1.1,y+f.cursor[1]*1.1);}}
async function native(t){const local=t-START,index=findIndex(local),f=timeline.frames[index],prev=timeline.frames[Math.max(0,index-1)],img=await nativeImage(index);
 // Real native frames retain the app layout; smooth focus changes only the camera.
 let sideP=0; for(const band of focusBands)sideP=Math.max(sideP,go(local,band[0]-.25,band[0]+.4)*(1-go(local,band[1]-.15,band[1]+.5))); 
 const scale=.968+sideP*.17;const x=960-1520*scale/2-sideP*108,y=148-sideP*Math.max(0,(f.cursor[1]-470)*.38);
 ctx.save();ctx.beginPath();ctx.roundRect(64,144,1792,889,16);ctx.clip();card(ctx,img,x,y,1520*scale,900*scale,14);ctx.restore();
 const travel=go(local,f.at,f.at+Math.min(.24,f.duration*.7));let px=f.cursor[0],py=f.cursor[1];if(f.action==='move'){px=mix(prev.cursor[0],px,travel);py=mix(prev.cursor[1],py,travel);}pointer(x+px*scale,y+py*scale,f.action==='click'?clamp((local-f.at)/.45):0);

}
function ai(t){
 const u=t-nativeEnd;
 const x=170,y=158,w=1580,h=875;ctx.save();shadow(ctx,50,'#0d173b35',20);box(ctx,x,y,w,h,17,'#212121');ctx.shadowColor='transparent';ctx.beginPath();ctx.roundRect(x,y,w,h,17);ctx.clip();
 ctx.fillStyle='#171717';ctx.fillRect(x,y,245,h);txt(ctx,'ChatGPT',x+26,y+40,22,'#ececec',true);box(ctx,x+204,y+24,17,16,2,null,'#969696');ctx.strokeStyle='#969696';ctx.beginPath();ctx.moveTo(x+210,y+24);ctx.lineTo(x+210,y+40);ctx.stroke();
 box(ctx,x+13,y+69,219,43,8,'#2b2b2b');txt(ctx,'＋  新聊天',x+29,y+98,18,'#eeeeee');for(const[i,label]of ['搜索聊天','图片','资料库','项目'].entries())txt(ctx,label,x+34,y+148+i*45,17,'#bdbdbd');txt(ctx,'聊天',x+30,y+360,13,'#777777');box(ctx,x+13,y+379,219,43,8,'#292929');txt(ctx,'游戏 HUD 改造',x+27,y+407,16,'#eeeeee');txt(ctx,'H',x+33,y+h-39,17,'#dadada',true);txt(ctx,'游戏开发者',x+63,y+h-40,16,'#d0d0d0');
 txt(ctx,'ChatGPT',x+277,y+42,23,'#eaeaea',true);ctx.strokeStyle='#bbbbbb';ctx.lineWidth=1.7;ctx.beginPath();ctx.moveTo(x+393,y+31);ctx.lineTo(x+398,y+36);ctx.lineTo(x+403,y+31);ctx.stroke();txt(ctx,'分享   ···',x+w-33,y+42,17,'#bbbbbb',false,'right');
 const left=x+395,right=x+w-116,area=right-left;
 const sent=go(u,2.4,3.2);const prompt='请根据这份 EditHere 反馈改造游戏 HUD，保留场景，落实全部标注和组件位置。';
 if(sent<1){txt(ctx,'今天有什么计划？',x+910,y+236,34,'#ececec',true,'center');box(ctx,left-14,y+307,area+28,226,24,'#303030');if(u>.5){wrap(ctx,prompt,left+15,y+346,area-32,20,'#eeeeee');const lines=['{ "image": "data:image/jpeg;base64,...",','  "annotations": [ /* 5 条批注 */ ],','  "changes": [ /* 2 组布局变化 */ ] }'];if(u>1)for(let i=0;i<3;i++)txt(ctx,lines[i],left+17,y+422+i*26,16,'#a6a6a6');}else txt(ctx,'询问任何问题',left+18,y+346,22,'#9c9c9c');txt(ctx,'＋',left+17,y+507,26,'#d2d2d2');box(ctx,right-36,y+481,36,36,18,'#fafafa');txt(ctx,'↑',right-18,y+507,25,'#171717',true,'center');if(u>1.8)pointer(right-18,y+499,clamp((u-2.4)/.5));}
 if(sent>0){ctx.globalAlpha=sent;const bubbleX=left+153;box(ctx,bubbleX,y+106,area-153,199,22,'#303030');wrap(ctx,prompt,bubbleX+23,y+140,area-197,19,'#eeeeee');txt(ctx,'{ "image": "data:image/jpeg;base64,...",',bubbleX+24,y+222,15,'#c1c1c1');txt(ctx,'  "annotations": [...], "changes": [...] }',bubbleX+24,y+247,15,'#c1c1c1');txt(ctx,'显示更多',bubbleX+24,y+280,14,'#969696');
 const thought=u<8.9?'思考中…':'已思考';txt(ctx,thought,left,y+376,19,u<8.9?'#b6b6b6':'#909090');if(u<8.9){let a=.4+.3*Math.sin(u*3);ctx.fillStyle=`rgba(255,255,255,${a})`;ctx.beginPath();ctx.arc(left-16,y+369,4,0,Math.PI*2);ctx.fill();txt(ctx,u<5.2?'正在读取原图和修改意见':u<7?'正在调整游戏 HUD 的布局与视觉层级':'正在检查组件坐标与最终界面',left,y+419,19,'#a9a9a9');}
 if(u>8.5){const response='已完成 HUD 改造：血量显示更清楚，小地图与任务提示已按你的坐标调整。保留场景、角色和技能栏，战斗区域不再被 HUD 遮挡。';wrap(ctx,response.slice(0,Math.floor(clamp((u-8.5)/2.2)*response.length)),left,y+425,area,22,'#ededed',1.65);if(u>10.5){box(ctx,left,y+540,area,94,12,'#292929','#454545');txt(ctx,'Mossfall.exe',left+25,y+578,23,'#eaeaea',true);txt(ctx,'游戏 HUD 改造完成   ·   查看结果  ↗',left+25,y+613,17,'#9b9b9b');}}
 box(ctx,left-19,y+h-134,area+38,81,28,'#303030');txt(ctx,'询问任何问题',left+7,y+h-98,20,'#939393');txt(ctx,'＋',left+4,y+h-65,25,'#cccccc');box(ctx,right-32,y+h-100,36,36,18,'#454545');txt(ctx,'↑',right-14,y+h-74,24,'#aaaaaa',true,'center');txt(ctx,'ChatGPT 风格对话演示 · 本次改造成果回放',x+910,y+h-19,12,'#777777',false,'center');ctx.globalAlpha=1;
 }
 ctx.restore();
}
function comparison(t){const u=t-resultEnd,p=go(u,0,3.2);const y=mix(205,345,p),w=mix(1255,565,p),h=w*720/1120,x=mix((W-1255)/2,1275,p);card(ctx,assets.result,x,y,w,h,10);ctx.globalAlpha=go(u,2.1,3.5);txt(ctx,'AI 修改后',1557,787,29,'#2467e8',true,'center');txt(ctx,'落实 5 条意见，保留调整后的布局',1557,832,18,'#8091a8',false,'center');ctx.globalAlpha=1;
 for(const [i,key,name,sub]of [[0,'original','修改前','游戏内原画面'],[1,'marked','批注图','5 条批注 · 2 组布局变化']]){const a=go(u,1.25+i*.5,3+i*.5);ctx.globalAlpha=a;card(ctx,assets[key],80+i*597,345+65*(1-a),565,565*720/1120,10);txt(ctx,name,362+i*597,787,29,'#263955',true,'center');txt(ctx,sub,362+i*597,832,18,'#8091a8',false,'center');ctx.globalAlpha=1;}
 if(u>4){ctx.globalAlpha=go(u,4,5);ctx.drawImage(assets.logo,790,927,45,45);brand(ctx,854,926,200);ctx.globalAlpha=1;}}
const gameCache=new Map();async function gameFrame(kind,index){index=Math.max(0,Math.min(209,index));const key=kind+index;if(!gameCache.has(key)){gameCache.set(key,await loadImage(path.join(gameAssets,'live-'+kind,String(index).padStart(4,'0')+'.jpg')));if(gameCache.size>6)gameCache.delete(gameCache.keys().next().value);}return gameCache.get(key);}
async function gameplay(t){const im=await gameFrame('before',Math.floor((t-3)/5*72));card(ctx,im,322,168,1276,820,10);}
async function result(t){const u=t-aiEnd,p=go(u,0,.7);const im=await gameFrame('after',Math.floor(u*30));ctx.globalAlpha=p;card(ctx,im,322,168,1276,820,10);ctx.globalAlpha=1;}
assets.result=await loadImage(path.join(gameAssets,'live-after/0209.jpg'));
const chapterCards=[
 {at:8,label:'截图'},
 {at:START,label:'批注'},
 {at:START+timeline.frames.find(f=>f.action==='wave').at,label:'布局调整'},
 {at:nativeEnd,label:'交给 AI'},
 {at:aiEnd,label:'修改完成'}
];
function chapterCard(t){
 const entry=chapterCards.find(c=>t>=c.at&&t<c.at+3.2);if(!entry)return;
 const u=t-entry.at;ctx.save();ctx.globalAlpha=go(u,0,.35)*(1-go(u,2.5,3.2));
 txt(ctx,entry.label,960,86,44,'#243650',true,'center');ctx.restore();
}
function chartChat(u,repair){
 
 const x=170,y=158,w=1580,h=875;ctx.save();shadow(ctx,50,'#0d173b35',20);box(ctx,x,y,w,h,17,'#212121');ctx.shadowColor='transparent';ctx.beginPath();ctx.roundRect(x,y,w,h,17);ctx.clip();
 ctx.fillStyle='#171717';ctx.fillRect(x,y,245,h);txt(ctx,'ChatGPT',x+26,y+40,22,'#ececec',true);box(ctx,x+204,y+24,17,16,2,null,'#969696');ctx.strokeStyle='#969696';ctx.beginPath();ctx.moveTo(x+210,y+24);ctx.lineTo(x+210,y+40);ctx.stroke();
 box(ctx,x+13,y+69,219,43,8,'#2b2b2b');txt(ctx,'＋  新聊天',x+29,y+98,18,'#eeeeee');for(const[i,label]of ['搜索聊天','图片','资料库','项目'].entries())txt(ctx,label,x+34,y+148+i*45,17,'#bdbdbd');txt(ctx,'聊天',x+30,y+360,13,'#777777');box(ctx,x+13,y+379,219,43,8,'#292929');txt(ctx,'月度营收分析',x+27,y+407,16,'#eeeeee');txt(ctx,'H',x+33,y+h-39,17,'#dadada',true);txt(ctx,'数据分析师',x+63,y+h-40,16,'#d0d0d0');
 txt(ctx,'ChatGPT',x+277,y+42,23,'#eaeaea',true);ctx.strokeStyle='#bbbbbb';ctx.lineWidth=1.7;ctx.beginPath();ctx.moveTo(x+393,y+31);ctx.lineTo(x+398,y+36);ctx.lineTo(x+403,y+31);ctx.stroke();txt(ctx,'分享   ···',x+w-33,y+42,17,'#bbbbbb',false,'right');
 const left=x+395,right=x+w-116,area=right-left;
 const sent=go(u,2.4,3.2);const prompt=repair?'按照 EditHere 的三条批注修改图表，保留原始数据，以 sales.csv 为准。':'分析 sales.csv，生成 1—6 月实际营收与目标趋势图，保留原始数据供核对。';
 if(sent<1){txt(ctx,'今天有什么计划？',x+910,y+236,34,'#ececec',true,'center');box(ctx,left-14,y+307,area+28,226,24,'#303030');if(u>.5){wrap(ctx,prompt,left+15,y+346,area-32,20,'#eeeeee');const lines=repair?['{ "image": "data:image/jpeg;base64,...",','  "annotations": [ /* 3 条批注 */ ],','  "changes": [ /* 1 组图例移动 */ ] }']:['sales.csv · 月份 / 实际营收 / 目标（万元）','1月 42/45 · 2月 55/50 · 3月 48/55','4月 72/60 · 5月 68/65 · 6月 84/70'];if(u>1)for(let i=0;i<3;i++)txt(ctx,lines[i],left+17,y+422+i*26,16,'#a6a6a6');}else txt(ctx,'询问任何问题',left+18,y+346,22,'#9c9c9c');txt(ctx,'＋',left+17,y+507,26,'#d2d2d2');box(ctx,right-36,y+481,36,36,18,'#fafafa');txt(ctx,'↑',right-18,y+507,25,'#171717',true,'center');if(u>1.8)pointer(right-18,y+499,clamp((u-2.4)/.5));}
 if(sent>0){ctx.globalAlpha=sent;const bubbleX=left+153;box(ctx,bubbleX,y+106,area-153,199,22,'#303030');wrap(ctx,prompt,bubbleX+23,y+140,area-197,19,'#eeeeee');txt(ctx,repair?'{ "image": "data:image/jpeg;base64,...",':'sales.csv · 6 行数据 · 单位：万元',bubbleX+24,y+222,15,'#c1c1c1');txt(ctx,repair?'  "annotations": [...], "changes": [...] }':'实际营收：42 / 55 / 48 / 72 / 68 / 84',bubbleX+24,y+247,15,'#c1c1c1');txt(ctx,'显示更多',bubbleX+24,y+280,14,'#969696');
 const thought=u<8.9?'思考中…':'已思考';txt(ctx,thought,left,y+376,19,u<8.9?'#b6b6b6':'#909090');if(u<8.9){let a=.4+.3*Math.sin(u*3);ctx.fillStyle=`rgba(255,255,255,${a})`;ctx.beginPath();ctx.arc(left-16,y+369,4,0,Math.PI*2);ctx.fill();txt(ctx,u<5.2?'正在读取数据与需求':u<7?'正在核对指标并生成图表':'正在整理图表与原始数据',left,y+419,19,'#a9a9a9');}
 if(u>8.5){const response=repair?'已修正：图例移到绘图区外，纵轴改为 0–100 万元，6 月数值按原始数据恢复为 84 万元。':'已生成 1—6 月营收趋势图，包含实际值、目标值及原始数据。';wrap(ctx,response.slice(0,Math.floor(clamp((u-8.5)/2.2)*response.length)),left,y+425,area,22,'#ededed',1.65);if(u>10.5){box(ctx,left,y+540,area,94,12,'#292929','#454545');txt(ctx,'营收趋势图.png',left+25,y+578,23,'#eaeaea',true);txt(ctx,'查看图表  ↗',left+25,y+613,17,'#9b9b9b');}}
 box(ctx,left-19,y+h-134,area+38,81,28,'#303030');txt(ctx,'询问任何问题',left+7,y+h-98,20,'#939393');txt(ctx,'＋',left+4,y+h-65,25,'#cccccc');box(ctx,right-32,y+h-100,36,36,18,'#454545');txt(ctx,'↑',right-14,y+h-74,24,'#aaaaaa',true,'center');txt(ctx,'ChatGPT 风格对话演示 · 本次改造成果回放',x+910,y+h-19,12,'#777777',false,'center');ctx.globalAlpha=1;
 }
 ctx.restore();
}
const chartAssets={};for(const key of ['original','result','annotated','annotated-editor'])chartAssets[key]=await loadImage(path.join(here,'charts',key+'.png'));
chartAssets['annotated-editor']=rebrandNative(chartAssets['annotated-editor']);
chartAssets.marked=createCanvas(1120,720);chartAssets.marked.getContext('2d').drawImage(chartAssets.annotated,24,24,1120,720,0,0,1120,720);
const chartCapture=JSON.parse(readFileSync(path.join(here,'charts/capture-timeline.json'),'utf8')).frames;let ccEnd=0;for(const f of chartCapture){f.at=ccEnd;ccEnd+=f.duration;f.img=await loadImage(path.join(here,'charts',f.file));}
const chartFocus=[];for(let i=0;i<chartTimeline.frames.length;i++){if(chartTimeline.frames[i].action==='typing'){const start=chartTimeline.frames[i].at;while(i+1<chartTimeline.frames.length&&chartTimeline.frames[i+1].action==='typing')i++;chartFocus.push([start,chartTimeline.frames[i].end]);}}
const chartCache=new Map();async function chartFrame(i){if(!chartCache.has(i)){chartCache.set(i,rebrandNative(await loadImage(path.join(here,'charts',chartTimeline.frames[i].file))));if(chartCache.size>5)chartCache.delete(chartCache.keys().next().value);}return chartCache.get(i);}
function shortTitle(s,t){if(t<0||t>3.2)return;ctx.save();ctx.globalAlpha=go(t,0,.3)*(1-go(t,2.5,3.2));txt(ctx,s,960,86,44,'#243650',true,'center');ctx.restore();}
async function chartNative(t){const local=t-chartNativeStart;let i=0;for(let j=0;j<chartTimeline.frames.length;j++){if(chartTimeline.frames[j].at<=local)i=j;else break;}const f=chartTimeline.frames[i],prev=chartTimeline.frames[Math.max(0,i-1)],im=await chartFrame(i);let focus=0;for(const band of chartFocus)focus=Math.max(focus,go(local,band[0]-.25,band[0]+.4)*(1-go(local,band[1]-.15,band[1]+.5)));const scale=.968+focus*.17,x=960-1520*scale/2-focus*108,y=148-focus*Math.max(0,(f.cursor[1]-470)*.38);ctx.save();ctx.beginPath();ctx.roundRect(64,144,1792,889,16);ctx.clip();card(ctx,im,x,y,1520*scale,900*scale,14);ctx.restore();const travel=go(local,f.at,f.at+Math.min(.24,f.duration*.7));let px=f.cursor[0],py=f.cursor[1];if(f.action==='move'){px=mix(prev.cursor[0],px,travel);py=mix(prev.cursor[1],py,travel);}pointer(x+px*scale,y+py*scale,f.action==='click'?clamp((local-f.at)/.45):0);shortTitle('批注',local);}
async function chartScene(t){const u=t-chartStart;
 if(u<8){chartChat(u*1.5,false);shortTitle('数据分析',u);}
 else if(u<15){card(ctx,chartAssets.original,282,164,1356,872,12);}
 else if(t<chartNativeStart){const v=(u-15)/3*ccEnd;let f=chartCapture[0];for(const a of chartCapture)if(a.at<=v)f=a;card(ctx,f.img,300,160,1320,880,10);pointer(300+f.cursor[0]*1.1,160+f.cursor[1]*1.1);}
 else if(t<chartNativeEnd)await chartNative(t);
 else if(t<chartAiEnd){chartChat((t-chartNativeEnd)*1.2,true);shortTitle('交给 AI',t-chartNativeEnd);}
 else if(t<chartResultEnd){card(ctx,chartAssets.result,282,164,1356,872,12);shortTitle('修改完成',t-chartAiEnd);}
 else {const u=t-chartResultEnd,p=go(u,0,2.5),w=mix(1356,565,p),x=mix(282,1275,p),y=mix(164,345,p);card(ctx,chartAssets.result,x,y,w,w*720/1120,10);ctx.globalAlpha=p;txt(ctx,'修改后',1557,787,29,'#2467e8',true,'center');ctx.globalAlpha=1;for(const[i,key,label]of[[0,'original','修改前'],[1,'marked','批注图']]){ctx.globalAlpha=go(u,1+i*.4,2.8+i*.4);card(ctx,chartAssets[key],80+i*597,345,565,565*720/1120,10);txt(ctx,label,362+i*597,787,29,'#263955',true,'center');ctx.globalAlpha=1;}}
}

async function drawRaw(t){ctx.resetTransform();ctx.globalAlpha=1;ctx.shadowColor='transparent';ctx.shadowBlur=0;ctx.shadowOffsetY=0;background(t);if(t<3)intro(t);else if(t<8)await gameplay(t);else if(t<START)capture(t);else if(t<nativeEnd)await native(t);else if(t<aiEnd)ai(t);else if(t<resultEnd)await result(t);else if(t<gameEnd)comparison(t);else if(t<outroStart)await chartScene(t);else{await chartScene(outroStart-.001);ctx.globalAlpha=go(t,outroStart,outroStart+.8);ctx.drawImage(assets.outro,0,0,W,H);ctx.globalAlpha=1;}chapterCard(t);}
const voiceCuesPath=path.join(outputDir,'narration-cues.json');let voiceCues=[];try{voiceCues=JSON.parse(readFileSync(voiceCuesPath,'utf8'));}catch{}
function subtitle(t){const q=voiceCues.find(q=>t>=q.start&&t<q.end);if(!q)return;ctx.save();ctx.font='500 30px "Microsoft YaHei"';const width=Math.min(1770,ctx.measureText(q.text).width+52);box(ctx,(W-width)/2,1025,width,47,10,'#101c30db');txt(ctx,q.text,960,1058,30,'#ffffff',false,'center');ctx.restore();}
function transitionArt(s,u){const enter=go(u,0,.55),leave=go(u,s.duration-.55,s.duration),width=W*(enter*(1-leave));ctx.save();ctx.beginPath();ctx.rect(0,0,width,H);ctx.clip();let grad=ctx.createLinearGradient(0,0,W,H);grad.addColorStop(0,'#111d35');grad.addColorStop(1,'#223c65');ctx.fillStyle=grad;ctx.fillRect(0,0,W,H);ctx.fillStyle='#4d87e920';ctx.beginPath();ctx.arc(1650,300,650,0,Math.PI*2);ctx.fill();ctx.strokeStyle='#ffffff12';ctx.lineWidth=1;for(let i=0;i<7;i++){ctx.beginPath();ctx.moveTo(1000+i*140,0);ctx.lineTo(750+i*140,H);ctx.stroke();}
 ctx.drawImage(brandLogo,87,44,39,39);brand(ctx,138,50,133,true);txt(ctx,'改这里',292,74,18,'#91aaca');const settle=go(u,.12,.8);ctx.globalAlpha=settle;const y=350+(1-settle)*28;txt(ctx,s.kicker,132,y-64,25,'#8fb7ee',true);const words=s.title.split('，');txt(ctx,words[0]+(words.length>1?'，':''),128,y+31,62,'#fff',true);if(words.length>1)txt(ctx,words.slice(1).join('，'),128,y+121,62,'#fff',true);txt(ctx,s.detail,132,y+209,25,'#aebed7');ctx.fillStyle='#72a9f8';ctx.fillRect(132,y+248,84,4);
 const pic=s.key==='game'?assets.original:s.key==='feedback'?assets['annotated-editor']:s.key==='data'?chartAssets.original:chartAssets.result;const ww=680,hh=ww*pic.height/pic.width;card(ctx,pic,1110+(1-settle)*90,320,ww,hh,14);ctx.globalAlpha=1;ctx.restore();}
async function draw(t){let passed=0;for(const s of inserts){if(t>=s.start&&t<s.end){await drawRaw(s.raw+(t-s.start<s.duration/2?-.001:.001));transitionArt(s,t-s.start);subtitle(t);return;}if(t>=s.end)passed+=s.duration;}await drawRaw(t-passed);subtitle(t);}
const waveStart=START+timeline.frames.find(f=>f.action==='wave').at;
const rawSamples=[moveEnd+3,chartStart+1,chartStart+6,chartStart+11,chartStart+16,chartNativeStart+4,chartNativeStart+10,chartNativeStart+18,chartNativeEnd-3,chartNativeEnd+8.5,chartAiEnd+3,chartResultEnd+5,outroStart+3];
const samples=[1.5,17.5,...inserts.map(s=>s.start+1.7),...rawSamples.map(toMovie)];
const sheet=createCanvas(1280,Math.ceil(samples.length/2)*384),sc=sheet.getContext('2d');sc.fillStyle='#e5ebf6';sc.fillRect(0,0,sheet.width,sheet.height);
for(let i=0;i<samples.length;i++){await draw(samples[i]);sc.drawImage(cv,i%2*640,Math.floor(i/2)*384,640,360);txt(sc,`${samples[i].toFixed(1)} s`,i%2*640+12,Math.floor(i/2)*384+379,14);writeFileSync(path.join(outputDir,`review-${i}.jpg`),cv.toBuffer('image/jpeg',88));}
writeFileSync(path.join(outputDir,'contact-sheet.jpg'),sheet.toBuffer('image/jpeg',86));await draw(1.5);writeFileSync(path.join(outputDir,'poster.jpg'),cv.toBuffer('image/jpeg',94));
if(process.argv.includes('--stills')){console.log(JSON.stringify({DURATION,nativeEnd,aiEnd,resultEnd,moveEnd}));process.exit(0);}
const ff=path.join(root,'.cache/promo-video-deps/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe');
const output=path.join(outputDir,'EditHere-branded-base-picture.mp4');
const ffArgs=['-y','-hide_banner','-loglevel','warning','-f','rawvideo','-pixel_format','rgba','-video_size','1920x1080','-framerate','30','-i','pipe:0','-an','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-t',String(DURATION),'-movflags','+faststart',output];
const encoder=spawn(ff,ffArgs,{stdio:['pipe','ignore','pipe'],windowsHide:true});let logs='',failure=null;encoder.stderr.on('data',d=>logs+=d);encoder.on('error',e=>failure=e);encoder.stdin.on('error',e=>failure=e);const completion=once(encoder,'close');const start=Date.now();
try{for(let k=0;k<DURATION*FPS;k++){if(failure)throw failure;await draw(k/FPS);await new Promise((resolve,reject)=>encoder.stdin.write(cv.data(),e=>e?reject(e):resolve()));if(k%300===0)console.log(`Rendered ${k/FPS}/${DURATION}s; elapsed ${Math.round((Date.now()-start)/1000)}s`);}encoder.stdin.end();const[code]=await completion;if(code)throw new Error(`ffmpeg ${code}`);}finally{writeFileSync(path.join(outputDir,'render.log'),logs);}
writeFileSync(path.join(outputDir,'render-metadata.json'),JSON.stringify({duration:DURATION,width:W,height:H,fps:FPS,frames:DURATION*FPS,nativeFrames:timeline.frames.length,nativeAnnotations:8,chartNativeFrames:chartTimeline.frames.length,source:'Native game EXE and screenshot Overlay; native Editor; actual clipboard export; ChatGPT-styled editorial replay; EditHere brand outro; native title areas rebranded only',output},null,2));console.log(output);
