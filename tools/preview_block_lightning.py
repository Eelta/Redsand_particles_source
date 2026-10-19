
from pathlib import Path
import json
import argparse
from nif_schema import DirectionalNif

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--frames', required=True, type=Path, help='Production C++ block_lightning_tests --frames export')
args = parser.parse_args()
animation = json.loads(args.frames.read_text('utf-8'))
assert len(animation['frames']) == 94 and all(len(f) == 60 for f in animation['frames'])
doc = DirectionalNif.read(ROOT/'assets/Basic/Meshes/RedsandBlockLightning/Arc0.nif')
nodes = {i: doc.values(i) for i in doc.indices('NiNode')}
parents = {child: parent for parent, node in nodes.items() for child in node['children']}
branches = []
for shape in doc.indices('BSTriShape'):
    f = doc.values(shape); node = parents[shape]
    vertices = [x for v in f['vertices'] for x in (*v[:3], v[-1]/255, v[4])]
    branches.append(dict(vertices=vertices, indices=[x for tri in f['triangles'] for x in tri],
                         origin=nodes[node]['translation'], parent=(parents[node]-2)//3 if parents[node] else -1))

HTML = r'''<!doctype html><meta charset="utf-8"><title>格挡闪电 · 三维资源预览</title>
<style>body{margin:0;background:#0a101a;color:#d4e5f5;font:15px system-ui}header{padding:18px 24px;line-height:1.7}b{color:white;font-size:20px}small{display:block;color:#8ea4bc}canvas{display:block;width:100%;height:76vh}button,label{margin-right:18px}button{background:#233b55;color:white;border:1px solid #53708c;padding:7px 15px;border-radius:5px}</style>
<header><b>格挡闪电 · 三维资源预览</b><small>直接读取已生成 NIF 的三维管状网格。固定弧线，单层电芯，无抖动或外壳；可观察电流推进、空间透视及遮挡；浏览器发光近似，不是 Skyrim / ENB 实机画面。</small>
<button id="replay">重播</button><label><input id="orbit" type="checkbox">旋转视角</label><label><input id="random" type="checkbox">每次随机 RGB</label><label>默认颜色 <input id="color" type="color" value="#1f94ff"></label><label><input id="freeze" type="checkbox">定格</label><input id="time" type="range" min="0" max="1.55" step="0.005" value="0"><span id="phase"></span></header><canvas id="view"></canvas>
<script>
const data=PAYLOAD;
const animation=ANIMATION;
const canvas=document.getElementById('view'),gl=canvas.getContext('webgl',{alpha:false,antialias:true});
if(!gl)throw Error('需要支持 WebGL 的浏览器');
const vs='attribute vec3 p;attribute float a,u;uniform mat4 m,vp;varying float alpha,pathU;void main(){gl_Position=vp*m*vec4(p,1.);alpha=a;pathU=u;}',
fs='precision mediump float;varying float alpha,pathU;uniform vec4 c;uniform vec2 window;uniform bool masked;void main(){float k=1.;if(masked){float t=(pathU-window.y)/max(.025,window.x-window.y);k=smoothstep(0.,.065,t)*(1.-smoothstep(.86,1.,t));}gl_FragColor=vec4(c.rgb,c.a*alpha*k);}';
function shader(type,src){let s=gl.createShader(type);gl.shaderSource(s,src);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s}
let prog=gl.createProgram();gl.attachShader(prog,shader(gl.VERTEX_SHADER,vs));gl.attachShader(prog,shader(gl.FRAGMENT_SHADER,fs));gl.linkProgram(prog);if(!gl.getProgramParameter(prog,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(prog));gl.useProgram(prog);
const loc=Object.fromEntries(['m','vp','c','window','masked'].map(k=>[k,gl.getUniformLocation(prog,k)])),p=gl.getAttribLocation(prog,'p'),a=gl.getAttribLocation(prog,'a'),u=gl.getAttribLocation(prog,'u');gl.enableVertexAttribArray(p);gl.enableVertexAttribArray(a);gl.enableVertexAttribArray(u);
function mesh(vertices,indices){let v=gl.createBuffer(),e=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,v);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(vertices),gl.STATIC_DRAW);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,e);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint16Array(indices),gl.STATIC_DRAW);return{v,e,n:indices.length}}
const meshes=data.map(b=>mesh(b.vertices,b.indices));
function id(){return[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}
function mul(a,b){let c=Array(16).fill(0);for(let i=0;i<4;i++)for(let j=0;j<4;j++)for(let k=0;k<4;k++)c[j*4+i]+=a[k*4+i]*b[j*4+k];return c}
function transform(x,y,z,s=1){let m=id();m[0]=m[5]=m[10]=s;m[12]=x;m[13]=y;m[14]=z;return m}
function rotate(x,y,z){let sx=Math.sin(x),cx=Math.cos(x),sy=Math.sin(y),cy=Math.cos(y),sz=Math.sin(z),cz=Math.cos(z);return[cy*cz,sx*sy*cz-cx*sz,cx*sy*cz+sx*sz,0,cy*sz,sx*sy*sz+cx*cz,cx*sy*sz-sx*cz,0,-sy,sx*cy,cx*cy,0,0,0,0,1]}
function norm(v){let l=Math.hypot(...v);return v.map(x=>x/l)}
function cross(a,b){return[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]}
function dot(a,b){return a.reduce((s,x,i)=>s+x*b[i],0)}
function camera(eye){let z=norm(eye.map((x,i)=>x-[0,0,95][i])),x=norm(cross([0,0,1],z)),y=cross(z,x);return[x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,-dot(x,eye),-dot(y,eye),-dot(z,eye),1]}
function perspective(aspect){let n=5,f=3000,q=1/Math.tan(Math.PI/7);return[q/aspect,0,0,0,0,q,0,0,0,0,-(f+n)/(f-n),-1,0,0,-2*f*n/(f-n),0]}
function draw(mesh,m,color,mode=gl.TRIANGLES,window=null){gl.bindBuffer(gl.ARRAY_BUFFER,mesh.v);gl.vertexAttribPointer(p,3,gl.FLOAT,false,20,0);gl.vertexAttribPointer(a,1,gl.FLOAT,false,20,12);gl.vertexAttribPointer(u,1,gl.FLOAT,false,20,16);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,mesh.e);gl.uniformMatrix4fv(loc.m,false,new Float32Array(m));gl.uniform4fv(loc.c,color);gl.uniform1i(loc.masked,window?1:0);gl.uniform2fv(loc.window,window||[1,0]);gl.drawElements(mode,mesh.n,gl.UNSIGNED_SHORT,0)}
let gridV=[],gridI=[];for(let i=-500;i<=500;i+=50){for(let point of [[i,-500,0],[i,500,0],[-500,i,0],[500,i,0]]){gridI.push(gridV.length/5);gridV.push(...point,1,0)}}let grid=mesh(gridV,gridI);
let boxV=[],boxI=[];for(let z of [0,175])for(let y of [-80,0])for(let x of [-185,-100])boxV.push(x,y,z,1,0);for(let t of [[0,1,3],[0,3,2],[4,7,5],[4,6,7],[0,4,5],[0,5,1],[2,3,7],[2,7,6],[0,2,6],[0,6,4],[1,5,7],[1,7,3]])boxI.push(...t);let box=mesh(boxV,boxI);
const smooth=t=>{t=Math.max(0,Math.min(1,t));return t*t*(3-2*t)};
let start=performance.now(),lastCycle=-1,color=[.12,.58,1],angle=0,last=performance.now();document.getElementById('replay').onclick=()=>{start=performance.now();lastCycle=-1;document.getElementById('freeze').checked=false};
function frame(now){let dt=(now-last)/1000;last=now;if(document.getElementById('orbit').checked)angle+=dt*.24;
let cycle=Math.floor((now-start)/2400),elapsed=document.getElementById('freeze').checked?Number(document.getElementById('time').value):((now-start)%2400)/1000;if(!document.getElementById('random').checked)color=[1,3,5].map(k=>parseInt(document.getElementById('color').value.slice(k,k+2),16)/255);if(!document.getElementById('freeze').checked)document.getElementById('time').value=Math.min(animation.duration,elapsed);if(cycle!==lastCycle){lastCycle=cycle;color=document.getElementById('random').checked?[Math.random(),Math.random(),Math.random()]:[1,3,5].map(k=>parseInt(document.getElementById('color').value.slice(k,k+2),16)/255);let lo=Math.min(...color),hi=Math.max(...color);if(document.getElementById('random').checked)color=color.map(x=>.035+.965*(x-lo)/(hi-lo))}
let w=Math.round(canvas.clientWidth*devicePixelRatio),h=Math.round(canvas.clientHeight*devicePixelRatio);if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h}gl.viewport(0,0,w,h);gl.clearColor(.025,.045,.075,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.enable(gl.DEPTH_TEST);gl.disable(gl.BLEND);gl.depthMask(true);
gl.uniformMatrix4fv(loc.vp,false,new Float32Array(mul(perspective(w/h),camera([Math.sin(angle)*760,Math.cos(angle)*760,300]))));draw(grid,id(),[.12,.20,.29,1],gl.LINES);draw(box,id(),[.14,.18,.23,1]);gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE);gl.depthMask(false);
let matrices=[];const root=transform(0,0,95,1.2),t=Math.min(animation.frames.length-1,elapsed*animation.fps),f=Math.floor(t),fraction=t-f;
for(let i=0;i<data.length;i++){const aa=animation.frames[f][i],bb=animation.frames[Math.min(f+1,animation.frames.length-1)][i],pose=aa.map((x,k)=>x+(bb[k]-x)*fraction),[head,tail,alpha,x,y,z]=pose,o=data[i].origin;
let m=mul(data[i].parent<0?root:matrices[data[i].parent],mul(transform(...o),rotate(x,y,z)));matrices.push(m);if(elapsed<animation.duration&&alpha>0)draw(meshes[i],m,[...color.map(x=>x*1.5),alpha],gl.TRIANGLES,[head,tail]);}
gl.depthMask(true);document.getElementById('phase').textContent=elapsed<animation.duration?`电流推进 / 尾段消散 ${elapsed.toFixed(2)}s`:'等待下一次格挡';requestAnimationFrame(frame)}requestAnimationFrame(frame);
</script>'''
out = ROOT/'output/Publication/Previews/Block Lightning 3D.html'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(HTML.replace('PAYLOAD', json.dumps(branches, separators=(',', ':'))).replace('ANIMATION', json.dumps(animation, separators=(',', ':'))), 'utf-8')
print(out)
