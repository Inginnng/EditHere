(function(root){
  const defaults={"radius":7.8,"width":5,"x":32.30555828147456,"y":32.2,"scale":1,"angle":-45,"tip":0.75,"shoulder":2.5,"lock":false};
  const n=v=>Number(v.toFixed(5));
  const pt=p=>`${n(p.x)} ${n(p.y)}`;
  function rounded(points,radii){
    const corners=points.map((v,i)=>{
      const prev=points[(i+points.length-1)%points.length],next=points[(i+1)%points.length];
      const l1=Math.hypot(v.x-prev.x,v.y-prev.y),l2=Math.hypot(next.x-v.x,next.y-v.y);
      const a={x:(v.x-prev.x)/l1,y:(v.y-prev.y)/l1},b={x:(next.x-v.x)/l2,y:(next.y-v.y)/l2};
      const turn=Math.atan2(a.x*b.y-a.y*b.x,a.x*b.x+a.y*b.y),factor=Math.abs(Math.tan(turn/2));
      const d=Math.min(radii[i]*factor,l1*.45,l2*.45),r=factor>1e-8?d/factor:0;
      return {entry:{x:v.x-a.x*d,y:v.y-a.y*d},exit:{x:v.x+b.x*d,y:v.y+b.y*d},r,sweep:turn>0?1:0};
    });
    return {corners,path:corners.map((c,i)=>`${i?'L':'M'} ${pt(c.entry)} ${c.r>1e-6?`A ${n(c.r)} ${n(c.r)} 0 0 ${c.sweep} ${pt(c.exit)}`:`L ${pt(c.exit)}`}`).join(' ')+' Z'};
  }
  function geometry(s){
    const R=s.radius,w=s.width,a=16+R,b=48-R,e=30,r=R-w,c=w/2;
    const frame=`M 48 ${64-e} V ${b} A ${R} ${R} 0 0 1 ${b} 48 H ${a} A ${R} ${R} 0 0 1 16 ${b} V ${a} A ${R} ${R} 0 0 1 ${a} 16 H ${e} A ${c} ${c} 0 0 1 ${e} ${16+w} H ${a} A ${r} ${r} 0 0 0 ${16+w} ${a} V ${b} A ${r} ${r} 0 0 0 ${a} ${48-w} H ${b} A ${r} ${r} 0 0 0 ${48-w} ${b} V ${64-e} A ${c} ${c} 0 0 1 48 ${64-e} Z`;
    const angle=s.angle*Math.PI/180,cos=Math.cos(angle),sin=Math.sin(angle);
    let points=[[0,0],[6.2,-3.7],[24,-3.7],[24,3.7],[6.2,3.7]].map(([x,y])=>({x:(x*cos-y*sin)*s.scale,y:(x*sin+y*cos)*s.scale}));
    if(s.lock){const base=points[4].y,offset=Math.max(0,.8*s.scale-points[1].x);points=points.map((p,i)=>i?{x:p.x+offset,y:p.y-base}:{x:0,y:0});}
    points=points.map(p=>({x:p.x+s.x,y:p.y+(s.lock?48:s.y)}));
    const pen=rounded(points,[s.tip,s.shoulder,1.4,1.4,s.shoulder].map(r=>r*s.scale));
    return {frame,pen:pen.path,corners:pen.corners,points};
  }
  function svg(s){const g=geometry(s);return `<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="8 6 52 52"><title>EditHere</title><metadata>${JSON.stringify(s)}</metadata><defs><linearGradient id="helpdesign-blue" gradientUnits="userSpaceOnUse" x1="48" y1="10" x2="16" y2="48"><stop offset="0" stop-color="#61ADFF"/><stop offset="1" stop-color="#3155D9"/></linearGradient></defs><path fill="url(#helpdesign-blue)" d="${g.frame}"/><path fill="url(#helpdesign-blue)" d="${g.pen}"/></svg>`;}
  root.HelpDesignGeometry={defaults,geometry,svg};
  if(typeof module!=='undefined')module.exports=root.HelpDesignGeometry;
})(globalThis);
