// Local color components and grouped edges. No image data leaves the browser.
export function detectRegions(rgba, w, h, originalWidth = w, originalHeight = h) {
  if (w < 16 || h < 16) return [];
  const count = w * h, colors = new Int32Array(count), edges = new Int32Array(count), result = [];
  const add = (r, method, label) => {
    const b = { x1: Math.max(0, Math.floor(r.x1 * originalWidth / w)), y1: Math.max(0, Math.floor(r.y1 * originalHeight / h)), x2: Math.min(originalWidth, Math.ceil(r.x2 * originalWidth / w)), y2: Math.min(originalHeight, Math.ceil(r.y2 * originalHeight / h)) };
    if (b.x2 - b.x1 < 12 || b.y2 - b.y1 < 10 || area(b) > originalWidth * originalHeight * .9) return;
    result.push({ bounds: b, target: { source: 'vision', label, controlType: null, automationId: null, method, originalScreenBounds: null, clipped: false } });
  };
  for (let i = 0; i < count; i++) colors[i] = ((rgba[i*4] >> 4) << 8) | ((rgba[i*4+1] >> 4) << 4) | (rgba[i*4+2] >> 4);
  components(colors, w, h, false, (r, n) => {
    const fill = n / area(r);
    if (r.x2-r.x1 >= 24 && r.y2-r.y1 >= 18 && n > 250 && (fill >= .7 || (fill >= .22 && frameSupport(colors, r, w) > .86))) add(r, 'color-region', '色块区域');
  });
  const difference = (a, b) => Math.max(Math.abs(rgba[a*4]-rgba[b*4]),Math.abs(rgba[a*4+1]-rgba[b*4+1]),Math.abs(rgba[a*4+2]-rgba[b*4+2]));
  for (let y=1; y<h-1; y++) for (let x=1; x<w-1; x++) { const i=y*w+x; if (difference(i,i+1)>36 || difference(i,i+w)>36) edges[i]=1; }
  components(dilate(edges,w,h,3,1),w,h,true,(r,n) => {
    if (r.x2-r.x1>=24 && r.y2-r.y1>=14 && n>=80) { r.x1=Math.max(0,r.x1+2); r.x2=Math.min(w,r.x2-2); add(r,'edge-region','内容区域'); }
  });
  const dedup = [];
  for (const c of result.sort((a,b)=>area(a.bounds)-area(b.bounds))) { if (!dedup.some(other=>iou(c.bounds,other.bounds)>.87)) dedup.push(c); if(dedup.length>=180)break; }
  return dedup;
}
const area = r => (r.x2-r.x1)*(r.y2-r.y1);
export function iou(a,b) { const intersection=Math.max(0,Math.min(a.x2,b.x2)-Math.max(a.x1,b.x1))*Math.max(0,Math.min(a.y2,b.y2)-Math.max(a.y1,b.y1)); return intersection/(area(a)+area(b)-intersection)||0; }
function frameSupport(colors,r,w) {
  const value=colors[r.y1*w+r.x1]; let matches=0,total=0;
  for(let x=r.x1;x<r.x2;x++){matches+=Number(colors[r.y1*w+x]===value)+Number(colors[(r.y2-1)*w+x]===value);total+=2;}
  for(let y=r.y1;y<r.y2;y++){matches+=Number(colors[y*w+r.x1]===value)+Number(colors[y*w+r.x2-1]===value);total+=2;}
  return matches/total;
}
function components(map,w,h,skipZero,found) {
  const visited=new Uint8Array(map.length),queue=new Int32Array(map.length);
  for(let start=0;start<map.length;start++){
    if(visited[start]||(skipZero&&map[start]===0))continue;
    let head=0,tail=0,value=map[start],l=start%w,r=l,t=Math.floor(start/w),b=t;
    queue[tail++]=start;visited[start]=1;
    const enqueue=i=>{if(!visited[i]&&map[i]===value){visited[i]=1;queue[tail++]=i;}};
    while(head<tail){const i=queue[head++],x=i%w,y=Math.floor(i/w);l=Math.min(l,x);r=Math.max(r,x);t=Math.min(t,y);b=Math.max(b,y);if(x>0)enqueue(i-1);if(x+1<w)enqueue(i+1);if(y>0)enqueue(i-w);if(y+1<h)enqueue(i+w);}
    found({x1:l,y1:t,x2:r+1,y2:b+1},tail);
  }
}
function dilate(input,w,h,rx,ry) {
  const integral=new Int32Array((w+1)*(h+1)),out=new Int32Array(input.length);
  for(let y=0;y<h;y++){let row=0;for(let x=0;x<w;x++){row+=input[y*w+x];integral[(y+1)*(w+1)+x+1]=integral[y*(w+1)+x+1]+row;}}
  for(let y=0;y<h;y++)for(let x=0;x<w;x++){const l=Math.max(0,x-rx),t=Math.max(0,y-ry),r=Math.min(w,x+rx+1),b=Math.min(h,y+ry+1);out[y*w+x]=Number(integral[b*(w+1)+r]-integral[t*(w+1)+r]-integral[b*(w+1)+l]+integral[t*(w+1)+l]>0);}
  return out;
}
