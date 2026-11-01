// Original analytic assets for the forest stress scene. No external art.
// Run with Node 24: node bench/forest/make-assets.mjs
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
const directory = fileURLToPath(new URL('.', import.meta.url));
let state = 1731;
function random() { state = (Math.imul(state, 1664525) + 1013904223) >>> 0; return state / 4294967296; }
const add = (a,b) => a.map((x,i)=>x+b[i]);
const mul = (a,s) => a.map(x=>x*s);
const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const unit = a => mul(a,1/Math.hypot(...a));
class Mesh {
  constructor() { this.lines = ['# Original analytic forest fixture; make-assets.mjs']; this.count = 0; this.triangles=0; this.points=[]; }
  triangle(points, normals, uvs) {
    this.points.push(...points);
    const ids = [];
    for (let i=0;i<3;i++) {
      this.lines.push(`v ${points[i].map(x=>x.toFixed(6)).join(' ')}`);
      this.lines.push(`vn ${normals[i].map(x=>x.toFixed(6)).join(' ')}`);
      this.lines.push(`vt ${uvs[i].map(x=>x.toFixed(6)).join(' ')}`);
      ids.push(++this.count);
    }
    this.lines.push('f '+ids.map(i=>`${i}/${i}/${i}`).join(' ')); this.triangles++;
  }
  quad(p,n,uv=[[0,0],[1,0],[1,1],[0,1]]) {
    for (const t of [[0,1,2],[0,2,3]]) this.triangle(t.map(i=>p[i]),t.map(()=>n),t.map(i=>uv[i]));
  }
  save(name) { writeFileSync(directory+name, this.lines.join('\n')+'\n'); console.log(name, this.triangles,'triangles'); }
}
function cylinder(mesh,a,b,r0,r1,sides) {
  const axis=unit(add(b,mul(a,-1))), right=unit(cross(axis,Math.abs(axis[1])>.9?[1,0,0]:[0,1,0])), forward=cross(right,axis);
  for (let i=0;i<sides;i++) {
    const vectors=[i,i+1].map(j=>add(mul(right,Math.cos(j*2*Math.PI/sides)),mul(forward,Math.sin(j*2*Math.PI/sides))));
    const [v,w]=vectors;
    const p=[add(a,mul(v,r0)),add(b,mul(v,r1)),add(b,mul(w,r1)),add(a,mul(w,r0))];
    // Outward winding in Tide's imported right-handed mesh coordinates.
    mesh.quad(p,unit(add(v,w)),[[i/sides,0],[(i+1)/sides,0],[(i+1)/sides,1],[i/sides,1]]);
  }
}
for (let level=0;level<3;level++) {
  const bark=new Mesh(), leaves=new Mesh(); state=1731;
  cylinder(bark,[0,0,0],[.12,8.8,-.2],.32,.055,level===0?9:level===1?6:4);
  const branches=level===0?9:level===1?5:0;
  for(let i=0;i<branches;i++) {
    const angle=i*2.39996, y=3.1+i*.53;
    cylinder(bark,[0,y,0],[Math.cos(angle)*2.3,y+2.2,Math.sin(angle)*2.3],.105,.018,level===0?5:3);
  }
  // Crown clusters are cards carrying a whole spray of small leaves. The
  // distant representation retains actual volume, not one camera-facing quad.
  const cards=[92,36,12][level];
  for(let i=0;i<cards;i++) {
    const z=1-2*(i+.5)/cards, angle=i*2.399963;
    const radial=Math.sqrt(Math.max(0,1-z*z));
    const center=[Math.cos(angle)*radial*3.1,8+z*3.3,Math.sin(angle)*radial*3.1];
    const yaw=random()*Math.PI*2, tilt=(random()-.5)*1.7;
    const right=[Math.cos(yaw),0,Math.sin(yaw)];
    const up=unit([Math.sin(yaw)*Math.sin(tilt),Math.cos(tilt),-Math.cos(yaw)*Math.sin(tilt)]);
    const scale=[1.28,1.8,2.8][level]*(.8+random()*.4);
    const corners=[[-1,-1],[1,-1],[1,1],[-1,1]].map(([x,y])=>add(center,add(mul(right,x*scale),mul(up,y*scale))));
    // Rounded crown normals deliberately suppress the visible flat-card frame.
    const normal=unit([center[0]*.65,.7+z,center[2]*.65]);
    leaves.quad(corners,normal);
  }
  bark.save(`bark-${level}.obj`); leaves.save(`leaves-${level}.obj`);
  // Match ForestBend's response, maximum gust and minimum authored scale.
  // Wind preserves local Y; each local X/Z displacement is bounded by the
  // world magnitude / minScale, independent of tree yaw and wind direction.
  for (const [name,mesh,flutter] of [['bark',bark,0],['leaves',leaves,.012]]) {
    let maximum=0;
    for (const [x,y,z] of mesh.points) {
      const h=Math.max(0,y), displacement=(.45*1.5*.012*h*h + .45*flutter*h)/.8;
      maximum=Math.max(maximum,Math.abs(x)+displacement,Math.abs(z)+displacement);
      if (maximum >= 8-.001 || y < -2.5+.001 || y > 14.5-.001)
        throw new Error(`${name}-${level}: wind exceeds boundsCenter(0,6,0), boundsSize(16,17,16)`);
    }
    console.log(`${name}-${level}: max wind-expanded local horizontal extent ${maximum.toFixed(4)} < 8`);
  }
}
// Tide's asset importer reflects X. Author the terrain's analytic height with
// source -X so it agrees with ForestHeight at the final world coordinates.
function height(x,z) { return Math.sin(-x*.008)*5+Math.sin(z*.012-x*.003)*3; }
const terrain=new Mesh(), steps=80, side=2400;
function point(x,z) { return [x,height(x,z),z]; }
function normal(x,z) { return unit([.04*Math.cos(-x*.008)+.009*Math.cos(z*.012-x*.003),1,-.036*Math.cos(z*.012-x*.003)]); }
for(let z=0;z<=steps;z++) for(let x=0;x<=steps;x++) {
  const x0=(x/steps-.5)*side,z0=(z/steps-.5)*side;
  terrain.lines.push('v '+point(x0,z0).map(x=>x.toFixed(6)).join(' '));
  terrain.lines.push('vn '+normal(x0,z0).map(x=>x.toFixed(6)).join(' '));
  terrain.lines.push(`vt ${(x0/20).toFixed(6)} ${(z0/20).toFixed(6)}`);
}
for(let z=0;z<steps;z++) for(let x=0;x<steps;x++) {
  const a=z*(steps+1)+x+1,b=a+steps+1;
  for(const t of [[a,b,b+1],[a,b+1,a+1]]) { terrain.lines.push('f '+t.map(i=>`${i}/${i}/${i}`).join(' ')); terrain.triangles++; }
}
terrain.save('terrain.obj');
