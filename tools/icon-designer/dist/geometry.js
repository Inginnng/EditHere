(function(root){
  const defaults={"radius":5.2,"width":4,"x":28.30555828147456,"y":48.2,"scale":1,"angle":-45,"tip":1,"shoulder":3.15,"lock":false};
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
    const R=s.radius,w=s.width,a=16+R,b=48-R,e=a+2,r=R-w,c=w/2;
    const frame=`M ${e} 48 H ${a} A ${R} ${R} 0 0 1 16 ${b} V ${a} A ${R} ${R} 0 0 1 ${a} 16 H ${b} A ${R} ${R} 0 0 1 48 ${a} V ${e} A ${c} ${c} 0 0 1 ${48-w} ${e} V ${a} A ${r} ${r} 0 0 0 ${b} ${16+w} H ${a} A ${r} ${r} 0 0 0 ${16+w} ${a} V ${b} A ${r} ${r} 0 0 0 ${a} ${48-w} H ${e} A ${c} ${c} 0 0 1 ${e} 48 Z`;
    const angle=s.angle*Math.PI/180,cos=Math.cos(angle),sin=Math.sin(angle);
    let points=[[0,0],[5.75,-4.25],[25.25,-4.25],[25.25,4.25],[5.75,4.25]].map(([x,y])=>({x:(x*cos-y*sin)*s.scale,y:(x*sin+y*cos)*s.scale}));
    if(s.lock){const base=points[4].y,offset=Math.max(0,.8*s.scale-points[1].x);points=points.map((p,i)=>i?{x:p.x+offset,y:p.y-base}:{x:0,y:0});}
    points=points.map(p=>({x:p.x+s.x,y:p.y+(s.lock?48:s.y)}));
    const pen=rounded(points,[s.tip,s.shoulder,1,1,s.shoulder].map(r=>r*s.scale));
    return {frame,pen:pen.path,corners:pen.corners,points};
  }
  function svg(s){const g=geometry(s);return `<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 64 64"><title>HelpDesign</title><metadata>${JSON.stringify(s)}</metadata><rect x="4" y="4" width="56" height="56" rx="13" fill="#2875F0"/><path fill="white" d="${g.frame}"/><path fill="white" d="${g.pen}"/></svg>`;}
  root.HelpDesignGeometry={defaults,geometry,svg};
  if(typeof module!=='undefined')module.exports=root.HelpDesignGeometry;
})(globalThis);
