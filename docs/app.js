/* ═══════════════════════════════════════════════════
   XJTU Compiler Visualizer
   ═══════════════════════════════════════════════════ */

/* ── API Config ───────────────────────────────────── */
const DEFAULT_API_URL = 'https://lines-eternal-ray-fighting.trycloudflare.com';
const LEGACY_API_URL = 'http://igw.netperf.cc:8080';
const API_HEALTH_TIMEOUT_MS = 8000;
const API_SCAN_TIMEOUT_MS = 12000;
const normalizeApiUrl = url => (url || '').replace(/\/+$/, '');
const savedApiUrl = normalizeApiUrl(localStorage.getItem('api_url'));
const API_CANDIDATES = savedApiUrl && savedApiUrl !== LEGACY_API_URL && savedApiUrl !== DEFAULT_API_URL
  ? [savedApiUrl, DEFAULT_API_URL]
  : [DEFAULT_API_URL];
let API_URL = API_CANDIDATES[0];
let apiAvailable = false;

async function checkAPI() {
  const badge = document.getElementById('api-badge');
  apiAvailable = false;
  for (const apiUrl of API_CANDIDATES) {
    try {
      const r = await fetch(apiUrl + '/api/health', { signal: AbortSignal.timeout(API_HEALTH_TIMEOUT_MS) });
      if (r.ok) {
        API_URL = apiUrl;
        apiAvailable = true;
        badge.textContent = 'API online';
        badge.className = 'api-badge online';
        return;
      }
    } catch (_) {}
  }
  badge.textContent = 'Local mode';
  badge.className = 'api-badge offline';
}
checkAPI();

/* ── Tabs ─────────────────────────────────────────── */
document.querySelectorAll('.nav-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.nav-tab').forEach(t => { t.classList.remove('active'); t.setAttribute('aria-selected', 'false'); });
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    tab.classList.add('active');
    tab.setAttribute('aria-selected', 'true');
    document.getElementById('view-' + tab.dataset.view).classList.add('active');
  });
});

/* ── Samples ──────────────────────────────────────── */
const SAMPLE_DFA = {
  extended: false, states: 4, start: 1,
  accept: [4], accept_labels: {},
  alphabet: "ab",
  transitions: [[2,3],[4,3],[2,4],[4,4]],
  keywords: {}
};

const SAMPLE_SOURCE = `int gcd(int a, int b) {
    while (b != 0) {
        int t;
        t = b;
        b = a - a / b * b;
        a = t;
    }
    return a;
}

void main() {
    int x;
    int y;
    float pi;
    pi = 3.14;
    pi = .5;
    pi = 1e-3;
    pi = 12.3E+4;
    x = 0;
    y = 10;
    while (x <= y) {
        if (x == y) {
            print(x);
        } else {
            x += 1;
        }
        x++;
    }
    if (x > 0 && y >= 0 || !x) {
        return;
    }
}`;

/* ── DFA: Form / JSON toggle ──────────────────────── */
const dfaFormEl = document.getElementById('dfa-form');
const dfaJsonWrap = document.getElementById('dfa-json-wrap');
const dfaJsonEl = document.getElementById('dfa-json');

document.getElementById('btn-mode-form').addEventListener('click', function() {
  dfaFormEl.classList.remove('hidden'); dfaJsonWrap.classList.add('hidden');
  this.classList.add('active'); this.setAttribute('aria-pressed','true');
  document.getElementById('btn-mode-json').classList.remove('active');
  document.getElementById('btn-mode-json').setAttribute('aria-pressed','false');
});
document.getElementById('btn-mode-json').addEventListener('click', function() {
  dfaJsonWrap.classList.remove('hidden'); dfaFormEl.classList.add('hidden');
  this.classList.add('active'); this.setAttribute('aria-pressed','true');
  document.getElementById('btn-mode-form').classList.remove('active');
  document.getElementById('btn-mode-form').setAttribute('aria-pressed','false');
  syncFormToJson();
});

function syncFormToJson() {
  const a = document.getElementById('df-alphabet').value.trim();
  const s = parseInt(document.getElementById('df-states').value) || 3;
  const st = parseInt(document.getElementById('df-start').value) || 1;
  const ac = document.getElementById('df-accept').value.trim().split(/[\s,]+/).map(Number).filter(n=>!isNaN(n));
  const tr = document.getElementById('df-trans').value.trim().split('\n').filter(l=>l.trim()).map(l=>l.trim().split(/\s+/).map(Number));
  dfaJsonEl.value = JSON.stringify({extended:false,states:s,start:st,accept:ac,accept_labels:{},alphabet:a,transitions:tr,keywords:{}},null,2);
}

function syncJsonToForm(d) {
  document.getElementById('df-alphabet').value = d.alphabet||'';
  document.getElementById('df-states').value = d.states||3;
  document.getElementById('df-start').value = d.start||1;
  document.getElementById('df-accept').value = (d.accept||[]).join(', ');
  document.getElementById('df-trans').value = (d.transitions||[]).map(r=>r.join(' ')).join('\n');
}

function getDFA() {
  if (!dfaFormEl.classList.contains('hidden')) syncFormToJson();
  return JSON.parse(dfaJsonEl.value);
}

/* ── DFA: Canvas Rendering ────────────────────────── */
let currentDFA = null, dfaNodes = [], dfaEdges = [];
let hlNode = -1, hlEdge = -1;
let viewX = 0, viewY = 0, viewScale = 1;
let isDraggingNode = false, dragNode = null;
let isPanning = false, pointerStart = {x:0,y:0};
let viewStartX = 0, viewStartY = 0, nodeStartX = 0, nodeStartY = 0;
let lastPinchDist = 0;

function screenToWorld(sx, sy) {
  const rect = canvas.getBoundingClientRect();
  return { x: (sx - rect.left - viewX) / viewScale, y: (sy - rect.top - viewY) / viewScale };
}
const canvas = document.getElementById('dfa-canvas');
const ctx = canvas.getContext('2d');

function resizeCanvas() {
  const w = canvas.parentElement.clientWidth, h = canvas.parentElement.clientHeight;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = w * dpr; canvas.height = h * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function layoutNodes(dfa) {
  const lo = dfa.extended ? 0 : 1, hi = dfa.extended ? dfa.states-1 : dfa.states;
  const count = hi - lo + 1;
  const W = canvas.parentElement.clientWidth, H = canvas.parentElement.clientHeight;
  const cx = W/2, cy = H/2, R = Math.min(W,H) * 0.34;
  const pad = 40;
  const nodes = [];
  for (let i = 0; i < count; i++) {
    const s = lo + i, angle = -Math.PI/2 + (2*Math.PI*i)/count;
    nodes.push({ id:s, x:cx+R*Math.cos(angle), y:cy+R*Math.sin(angle), r:26,
      vx:0, vy:0,
      isStart: s===dfa.start, isAccept: dfa.accept.includes(s),
      label: dfa.accept_labels&&dfa.accept_labels[s] ? s+'('+dfa.accept_labels[s]+')' : ''+s });
  }
  if (count <= 3) return nodes;

  const adj = new Set();
  for (let s = lo; s <= hi; s++) {
    const row = dfa.transitions[s - lo]; if (!row) continue;
    for (let c = 0; c < row.length; c++) {
      const t = row[c]; if (s === t) continue;
      if (dfa.extended && t === 0 && s !== 0) continue;
      const a = Math.min(s,t), b = Math.max(s,t);
      adj.add(a + ',' + b);
    }
  }
  const edges = [...adj].map(k => { const p=k.split(','); return [+p[0],+p[1]]; });
  const idxOf = id => nodes.findIndex(n => n.id === id);

  const idealLen = Math.min(W, H) / Math.max(2, Math.sqrt(count) * 0.9);
  const iters = Math.min(300, 80 + count * 12);

  for (let iter = 0; iter < iters; iter++) {
    const temp = 1 - iter / iters;
    const maxDisp = Math.max(2, temp * idealLen * 0.4);

    for (let i = 0; i < nodes.length; i++) { nodes[i].vx = 0; nodes[i].vy = 0; }

    for (let i = 0; i < nodes.length; i++) {
      for (let j = i+1; j < nodes.length; j++) {
        let dx = nodes[i].x - nodes[j].x, dy = nodes[i].y - nodes[j].y;
        let d = Math.sqrt(dx*dx + dy*dy) || 1;
        const repulse = (idealLen * idealLen) / d;
        const fx = (dx/d) * repulse, fy = (dy/d) * repulse;
        nodes[i].vx += fx; nodes[i].vy += fy;
        nodes[j].vx -= fx; nodes[j].vy -= fy;
      }
    }

    for (const [a,b] of edges) {
      const ni = idxOf(a), nj = idxOf(b);
      if (ni < 0 || nj < 0) continue;
      let dx = nodes[nj].x - nodes[ni].x, dy = nodes[nj].y - nodes[ni].y;
      let d = Math.sqrt(dx*dx + dy*dy) || 1;
      const attract = (d * d) / idealLen * 0.15;
      const fx = (dx/d) * attract, fy = (dy/d) * attract;
      nodes[ni].vx += fx; nodes[ni].vy += fy;
      nodes[nj].vx -= fx; nodes[nj].vy -= fy;
    }

    for (const n of nodes) {
      const gx = (cx - n.x) * 0.01, gy = (cy - n.y) * 0.01;
      n.vx += gx; n.vy += gy;
    }

    for (const n of nodes) {
      const d = Math.sqrt(n.vx*n.vx + n.vy*n.vy) || 1;
      const s = Math.min(d, maxDisp);
      n.x += (n.vx/d) * s;
      n.y += (n.vy/d) * s;
      n.x = Math.max(pad, Math.min(W - pad, n.x));
      n.y = Math.max(pad, Math.min(H - pad, n.y));
    }
  }
  return nodes;
}

function buildEdges(dfa) {
  const lo = dfa.extended?0:1, hi = dfa.extended?dfa.states-1:dfa.states;
  const sym = dfa.extended ? (dfa.classes||[]) : (dfa.alphabet||'').split('');
  const m = {};
  for (let s=lo; s<=hi; s++) {
    const row = dfa.transitions[s-lo]; if (!row) continue;
    for (let c=0; c<row.length; c++) {
      const t = row[c]; if (dfa.extended && t===0 && s!==0) continue;
      const k = s+'->'+t; if (!m[k]) m[k]=[]; m[k].push(sym[c]||''+c);
    }
  }
  return Object.entries(m).map(([k,v])=>{
    const p=k.split('->'); return {from:+p[0],to:+p[1],label:v.length>4?v.slice(0,3).join(',')+'…':v.join(',')};
  });
}

function getNode(id) { return dfaNodes.find(n=>n.id===id); }

function drawArrow(x1,y1,x2,y2,r1,r2,color,w,curve) {
  const dx=x2-x1, dy=y2-y1, d=Math.sqrt(dx*dx+dy*dy); if(d<1)return;
  const ux=dx/d, uy=dy/d;
  const sx=x1+ux*r1, sy=y1+uy*r1;
  const ex=x2-ux*(r2+5), ey=y2-uy*(r2+5);
  ctx.beginPath();
  if (curve) {
    const mx=(sx+ex)/2+(-uy)*curve, my=(sy+ey)/2+(ux)*curve;
    ctx.moveTo(sx,sy); ctx.quadraticCurveTo(mx,my,ex,ey);
  } else {
    ctx.moveTo(sx,sy); ctx.lineTo(ex,ey);
  }
  ctx.strokeStyle=color; ctx.lineWidth=w; ctx.stroke();
  let ax,ay,adx,ady;
  if (curve) {
    const t=0.95, mt=1-t;
    const px=mt*mt*sx+2*mt*t*((sx+ex)/2+(-uy)*curve)+t*t*ex;
    const py=mt*mt*sy+2*mt*t*((sy+ey)/2+(ux)*curve)+t*t*ey;
    adx=ex-px; ady=ey-py;
    const ad=Math.sqrt(adx*adx+ady*ady)||1; adx/=ad; ady/=ad;
  } else { adx=ux; ady=uy; }
  ctx.beginPath(); ctx.moveTo(ex+adx*9,ey+ady*9);
  ctx.lineTo(ex-adx*1+ady*4.5,ey-ady*1-adx*4.5);
  ctx.lineTo(ex-adx*1-ady*4.5,ey-ady*1+adx*4.5);
  ctx.closePath(); ctx.fillStyle=color; ctx.fill();
}

function drawSelfLoop(x,y,r,color,w) {
  const lr=16, cy2=y-r-lr;
  ctx.beginPath(); ctx.arc(x,cy2,lr,0.3,Math.PI*2-0.3);
  ctx.strokeStyle=color; ctx.lineWidth=w; ctx.stroke();
  const a=-0.3, ax=x+lr*Math.cos(a), ay=cy2+lr*Math.sin(a);
  ctx.beginPath(); ctx.moveTo(ax,ay); ctx.lineTo(ax+3,ay+7); ctx.lineTo(ax-4,ay+5);
  ctx.closePath(); ctx.fillStyle=color; ctx.fill();
}

function drawDFA() {
  const W=canvas.parentElement.clientWidth, H=canvas.parentElement.clientHeight;
  ctx.clearRect(0,0,W,H);
  ctx.save();
  ctx.translate(viewX, viewY);
  ctx.scale(viewScale, viewScale);

  dfaEdges.forEach((e,i)=>{
    const from=getNode(e.from), to=getNode(e.to); if(!from||!to) return;
    const isH = i===hlEdge, col = isH?'#a78bfa':'#52525b', w = isH?2.5:1.5;
    if (e.from===e.to) {
      drawSelfLoop(from.x,from.y,from.r,col,w);
      ctx.font='500 10px "JetBrains Mono",monospace'; ctx.fillStyle=isH?'#c4b5fd':'#71717a';
      ctx.textAlign='center'; ctx.textBaseline='middle';
      ctx.fillText(e.label,from.x,from.y-from.r-36);
    } else {
      const hasReverse = dfaEdges.some(o=>o.from===e.to&&o.to===e.from&&o.from!==o.to);
      const curve = hasReverse ? (e.from < e.to ? 30 : -30) : 0;
      drawArrow(from.x,from.y,to.x,to.y,from.r,to.r,col,w,curve);
      const dx=to.x-from.x, dy=to.y-from.y, d=Math.sqrt(dx*dx+dy*dy)||1;
      const nx=-dy/d, ny=dx/d;
      const labelOff = curve ? curve*0.6 : 14;
      const mx=(from.x+to.x)/2+nx*labelOff, my=(from.y+to.y)/2+ny*labelOff;
      ctx.font='500 10px "JetBrains Mono",monospace'; ctx.fillStyle=isH?'#c4b5fd':'#71717a';
      ctx.textAlign='center'; ctx.textBaseline='middle';
      ctx.fillText(e.label, mx, my);
    }
  });

  dfaNodes.forEach(n=>{
    const isH = n.id===hlNode;
    if (isH||n.isStart||n.isAccept) {
      ctx.beginPath(); ctx.arc(n.x,n.y,n.r+8,0,Math.PI*2);
      ctx.fillStyle = isH?'rgba(167,139,250,.15)':n.isStart?'rgba(59,130,246,.12)':'rgba(34,197,94,.1)';
      ctx.fill();
    }
    ctx.beginPath(); ctx.arc(n.x,n.y,n.r,0,Math.PI*2);
    ctx.fillStyle = isH?'#4c1d95':n.isStart?'#1e3a8a':n.isAccept?'#14532d':'#27272a';
    ctx.fill();
    ctx.strokeStyle = isH?'#a78bfa':n.isStart?'#3b82f6':n.isAccept?'#22c55e':'#3f3f46';
    ctx.lineWidth = n.isAccept||isH?2.5:1.5; ctx.stroke();
    if (n.isAccept) { ctx.beginPath(); ctx.arc(n.x,n.y,n.r-4,0,Math.PI*2); ctx.strokeStyle=isH?'#a78bfa':'#22c55e'; ctx.lineWidth=1; ctx.stroke(); }
    if (n.isStart) {
      const ax=n.x-n.r-18;
      ctx.beginPath(); ctx.moveTo(ax-12,n.y); ctx.lineTo(ax,n.y);
      ctx.strokeStyle='#3b82f6'; ctx.lineWidth=2; ctx.stroke();
      ctx.beginPath(); ctx.moveTo(ax,n.y); ctx.lineTo(ax-6,n.y-4); ctx.lineTo(ax-6,n.y+4);
      ctx.closePath(); ctx.fillStyle='#3b82f6'; ctx.fill();
    }
    ctx.font='600 12px "JetBrains Mono",monospace'; ctx.fillStyle='#fafafa';
    ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.fillText(n.label,n.x,n.y);
  });
  ctx.restore();
}

function renderDFA(dfa) {
  currentDFA = dfa; resizeCanvas();
  dfaNodes = layoutNodes(dfa); dfaEdges = buildEdges(dfa);
  hlNode = -1; hlEdge = -1;
  viewX = 0; viewY = 0; viewScale = 1;
  drawDFA();
  document.getElementById('canvas-empty').classList.add('hidden');
}

window.addEventListener('resize',()=>{ if(currentDFA){resizeCanvas();dfaNodes=layoutNodes(currentDFA);viewX=0;viewY=0;viewScale=1;drawDFA();} });

/* ── DFA: Canvas Interaction (pan/zoom/drag) ─────── */
function hitTestNode(ex, ey) {
  const w = screenToWorld(ex, ey);
  for (let i = dfaNodes.length - 1; i >= 0; i--) {
    const n = dfaNodes[i];
    const dx = w.x - n.x, dy = w.y - n.y;
    if (dx*dx + dy*dy <= n.r*n.r) return n;
  }
  return null;
}

canvas.addEventListener('pointerdown', e => {
  const hit = hitTestNode(e.clientX, e.clientY);
  if (hit) {
    isDraggingNode = true; dragNode = hit;
    const w = screenToWorld(e.clientX, e.clientY);
    nodeStartX = hit.x - w.x; nodeStartY = hit.y - w.y;
    canvas.style.cursor = 'grabbing';
  } else {
    isPanning = true;
    pointerStart = {x: e.clientX, y: e.clientY};
    viewStartX = viewX; viewStartY = viewY;
    canvas.style.cursor = 'grabbing';
  }
  canvas.setPointerCapture(e.pointerId);
});

canvas.addEventListener('pointermove', e => {
  if (isDraggingNode && dragNode) {
    const w = screenToWorld(e.clientX, e.clientY);
    dragNode.x = w.x + nodeStartX; dragNode.y = w.y + nodeStartY;
    drawDFA();
  } else if (isPanning) {
    viewX = viewStartX + (e.clientX - pointerStart.x);
    viewY = viewStartY + (e.clientY - pointerStart.y);
    drawDFA();
  } else {
    const hit = hitTestNode(e.clientX, e.clientY);
    canvas.style.cursor = hit ? 'pointer' : 'grab';
  }
});

canvas.addEventListener('pointerup', e => {
  isDraggingNode = false; dragNode = null; isPanning = false;
  canvas.releasePointerCapture(e.pointerId);
  const hit = hitTestNode(e.clientX, e.clientY);
  canvas.style.cursor = hit ? 'pointer' : 'grab';
});

canvas.addEventListener('wheel', e => {
  e.preventDefault();
  const rect = canvas.getBoundingClientRect();
  const mx = e.clientX - rect.left, my = e.clientY - rect.top;
  const factor = e.deltaY < 0 ? 1.12 : 1/1.12;
  const newScale = Math.max(0.2, Math.min(5, viewScale * factor));
  viewX = mx - (mx - viewX) * (newScale / viewScale);
  viewY = my - (my - viewY) * (newScale / viewScale);
  viewScale = newScale;
  drawDFA();
}, {passive: false});

canvas.addEventListener('touchstart', e => {
  if (e.touches.length === 2) {
    e.preventDefault();
    const dx = e.touches[0].clientX - e.touches[1].clientX;
    const dy = e.touches[0].clientY - e.touches[1].clientY;
    lastPinchDist = Math.sqrt(dx*dx + dy*dy);
  }
}, {passive: false});

canvas.addEventListener('touchmove', e => {
  if (e.touches.length === 2) {
    e.preventDefault();
    const dx = e.touches[0].clientX - e.touches[1].clientX;
    const dy = e.touches[0].clientY - e.touches[1].clientY;
    const dist = Math.sqrt(dx*dx + dy*dy);
    if (lastPinchDist > 0) {
      const rect = canvas.getBoundingClientRect();
      const mx = (e.touches[0].clientX + e.touches[1].clientX)/2 - rect.left;
      const my = (e.touches[0].clientY + e.touches[1].clientY)/2 - rect.top;
      const factor = dist / lastPinchDist;
      const newScale = Math.max(0.2, Math.min(5, viewScale * factor));
      viewX = mx - (mx - viewX) * (newScale / viewScale);
      viewY = my - (my - viewY) * (newScale / viewScale);
      viewScale = newScale;
      drawDFA();
    }
    lastPinchDist = dist;
  }
}, {passive: false});

/* ── DFA: Simulate & Enumerate ────────────────────── */
function simDFA(dfa,input) {
  const lo=dfa.extended?0:1, sym=dfa.extended?(dfa.classes||[]):(dfa.alphabet||'').split('');
  const steps=[]; let state=dfa.start;
  for(let i=0;i<input.length;i++){
    const c=sym.indexOf(input[i]), row=dfa.transitions[state-lo];
    const next=(c>=0&&row)?row[c]:-1, ok=next>=0&&(!dfa.extended||next!==0||state===0);
    steps.push({char:input[i],from:state,to:ok?next:-1,ok});
    if(!ok){state=-1;break;} state=next;
  }
  return {steps,final:state,accepted:state>=0&&dfa.accept.includes(state)};
}

function enumDFA(dfa,maxLen) {
  const lo=dfa.extended?0:1, sym=dfa.extended?(dfa.classes||[]):(dfa.alphabet||'').split('');
  const res=[];
  function f(s,d,targetLen,b){
    if(d===targetLen){if(dfa.accept.includes(s)) res.push(b||'ε'); return;}
    const row=dfa.transitions[s-lo]; if(!row) return;
    for(let c=0;c<sym.length;c++){const n=row[c]; if(dfa.extended&&n===0&&s!==0) continue; f(n,d+1,targetLen,b+sym[c]);}
  }
  for(let len=0;len<=maxLen;len++) f(dfa.start,0,len,'');
  return res;
}

/* ── DFA Events ───────────────────────────────────── */
document.getElementById('btn-load-sample').addEventListener('click',()=>{
  syncJsonToForm(SAMPLE_DFA);
  dfaJsonEl.value=JSON.stringify(SAMPLE_DFA,null,2);
  renderDFA(SAMPLE_DFA);
});

document.getElementById('dfa-file-input').addEventListener('change',e=>{
  const f=e.target.files[0]; if(!f) return;
  const r=new FileReader();
  r.onload=()=>{dfaJsonEl.value=r.result; try{syncJsonToForm(JSON.parse(r.result));}catch(_){}};
  r.readAsText(f);
});

document.getElementById('btn-render-dfa').addEventListener('click',()=>{
  try{renderDFA(getDFA());}catch(e){alert('Invalid DFA: '+e.message);}
});

document.getElementById('btn-test-string').addEventListener('click',()=>{
  if(!currentDFA) return;
  const input=document.getElementById('dfa-test-input').value;
  const r=simDFA(currentDFA,input);
  const el=document.getElementById('dfa-test-result');
  el.className='result-box '+(r.accepted?'accept':'reject');
  el.textContent='"'+input+'" → '+(r.accepted?'ACCEPTED':'REJECTED')+'  (final state: '+r.final+')';
  document.getElementById('dfa-step-display').innerHTML=r.steps.map((s,i)=>{
    const last=i===r.steps.length-1;
    const c=!s.ok?'st-reject':(last&&r.accepted?'st-accept':'st-active');
    return '<span class="st st-active">'+s.from+'</span><span class="arrow"> —</span><span class="ch">'+s.char+'</span><span class="arrow">→ </span><span class="st '+c+'">'+(s.ok?s.to:'✗')+'</span>';
  }).join('<span class="arrow">  </span>');
});

let anim=null;
document.getElementById('btn-animate').addEventListener('click',()=>{
  if(!currentDFA) return; if(anim){clearTimeout(anim);anim=null;}
  const input=document.getElementById('dfa-test-input').value;
  const r=simDFA(currentDFA,input); let i=0;
  (function step(){
    if(i>=r.steps.length){const l=r.steps[r.steps.length-1]; hlNode=l&&l.ok?l.to:-1; hlEdge=-1; drawDFA(); return;}
    const s=r.steps[i]; hlNode=s.from;
    hlEdge=dfaEdges.findIndex(e=>e.from===s.from&&e.to===(s.ok?s.to:-1));
    drawDFA();
    document.getElementById('dfa-step-display').innerHTML='Step '+(i+1)+': <span class="st st-active">'+s.from+'</span> <span class="arrow">—</span><span class="ch">'+s.char+'</span><span class="arrow">→</span> <span class="st '+(s.ok?'st-active':'st-reject')+'">'+(s.ok?s.to:'✗')+'</span>';
    i++; if(s.ok) anim=setTimeout(step,500);
  })();
});

document.getElementById('btn-enumerate').addEventListener('click',()=>{
  if(!currentDFA) return;
  const n=parseInt(document.getElementById('dfa-enum-len').value)||3;
  const r=enumDFA(currentDFA,n);
  document.getElementById('dfa-enum-result').innerHTML=
    r.map(s=>'<span class="item">'+s+'</span>').join('')+
    '<div class="total">'+r.length+' strings</div>';
});

/* ── Frontend Tokenizer ───────────────────────────── */
const KW={int:'INT',float:'FLOAT_KW',void:'VOID','if':'IF','else':'ELSE','while':'WHILE','return':'RETURN',input:'INPUT',print:'PRINT'};

function tokenize(src){
  const toks=[]; let pos=0,line=1,col=1;
  const pk=(k)=>{const c=src.charCodeAt(pos+(k||0)); return isNaN(c)?-1:c;};
  const adv=()=>{const c=pk(); if(c<0)return -1; pos++; if(c===10){line++;col=1;}else col++; return c;};
  const isA=c=>(c>=65&&c<=90)||(c>=97&&c<=122)||c===95;
  const isD=c=>c>=48&&c<=57;
  function skipWS(){
    while(true){
      const c=pk();
      if(c===32||c===9||c===13||c===10){adv();continue;}
      if(c===47&&pk(1)===47){while(pk()>=0&&pk()!==10)adv();continue;}
      if(c===47&&pk(1)===42){adv();adv();while(pk()>=0&&!(pk()===42&&pk(1)===47))adv();if(pk()>=0){adv();adv();}continue;}
      break;
    }
  }
  while(true){
    skipWS(); const sl=line,sc=col,sp=pos,c=pk(); if(c<0)break;
    if(isA(c)){
      while(isA(pk())||isD(pk()))adv();
      const lex=src.slice(sp,pos); toks.push({kind:KW[lex]||'ID',lexeme:lex,line:sl,col:sc});
    } else if(isD(c)||(c===46&&isD(pk(1)))){
      let fl=false;
      if(c===46){adv();fl=true;while(isD(pk()))adv();}
      else{while(isD(pk()))adv();if(pk()===46){fl=true;adv();while(isD(pk()))adv();}}
      const e=pk(); if(e===101||e===69){const sv=pos;adv();if(pk()===43||pk()===45)adv();if(isD(pk())){fl=true;while(isD(pk()))adv();}else pos=sv;}
      toks.push({kind:fl?'FLOAT_LIT':'NUM',lexeme:src.slice(sp,pos),line:sl,col:sc});
    } else {
      adv(); const n=pk(); let k='ERR';
      switch(c){
        case 43:if(n===43){adv();k='AAA';}else if(n===61){adv();k='AAS';}else k='ADD';break;
        case 45:k='SUB';break;case 42:k='MUL';break;case 47:k='DIV';break;
        case 60:if(n===61){adv();k='LE';}else k='LT';break;
        case 62:if(n===61){adv();k='GE';}else k='GT';break;
        case 61:if(n===61){adv();k='EQ';}else k='ASG';break;
        case 33:if(n===61){adv();k='NE';}else k='NOT';break;
        case 38:if(n===38){adv();k='AND';}break;
        case 124:if(n===124){adv();k='OR';}break;
        case 40:k='LPAR';break;case 41:k='RPAR';break;
        case 91:k='LBK';break;case 93:k='RBK';break;
        case 123:k='LBR';break;case 125:k='RBR';break;
        case 44:k='CMA';break;case 58:k='COL';break;case 59:k='SCO';break;case 46:k='DOT';break;
      }
      toks.push({kind:k,lexeme:src.slice(sp,pos),line:sl,col:sc});
    }
  }
  return toks;
}

/* ── Scanner UI ───────────────────────────────────── */
const TC={INT:'tk-keyword',FLOAT_KW:'tk-keyword',VOID:'tk-keyword',IF:'tk-keyword',ELSE:'tk-keyword',WHILE:'tk-keyword',RETURN:'tk-keyword',INPUT:'tk-keyword',PRINT:'tk-keyword',ID:'tk-id',NUM:'tk-num',FLOAT_LIT:'tk-float',ADD:'tk-op',SUB:'tk-op',MUL:'tk-op',DIV:'tk-op',LT:'tk-op',LE:'tk-op',EQ:'tk-op',GT:'tk-op',GE:'tk-op',NE:'tk-op',AND:'tk-op',OR:'tk-op',NOT:'tk-op',ASG:'tk-op',AAS:'tk-op',AAA:'tk-op',LPAR:'tk-delim',RPAR:'tk-delim',LBK:'tk-delim',RBK:'tk-delim',LBR:'tk-delim',RBR:'tk-delim',CMA:'tk-delim',COL:'tk-delim',SCO:'tk-delim',DOT:'tk-delim',ERR:'tk-err'};

function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}

function showTokens(toks){
  document.querySelector('#token-table tbody').innerHTML=toks.map((t,i)=>
    '<tr><td>'+(i+1)+'</td><td class="'+(TC[t.kind]||'')+'">'+t.kind+'</td><td>'+esc(t.lexeme)+'</td><td>'+t.line+':'+t.col+'</td></tr>'
  ).join('');
  const errs=toks.filter(t=>t.kind==='ERR').length;
  document.getElementById('scan-stats').innerHTML='<span class="n">'+toks.length+'</span> tokens'+(errs?' · <span class="err">'+errs+' errors</span>':'');
  const hl=document.getElementById('scan-highlighted');
  hl.innerHTML=toks.map(t=>'<span class="'+(TC[t.kind]||'')+'" title="'+t.kind+' @'+t.line+':'+t.col+'">'+esc(t.lexeme)+'</span>').join(' ');
  hl.classList.add('visible');
  document.getElementById('table-empty').classList.add('hidden');
}

document.getElementById('btn-scan-sample').addEventListener('click',()=>{
  document.getElementById('scan-source').value=SAMPLE_SOURCE;
  showTokens(tokenize(SAMPLE_SOURCE));
});

document.getElementById('btn-scan-run').addEventListener('click', async ()=>{
  const src=document.getElementById('scan-source').value;
  if(!src.trim()) return;
  const btn=document.getElementById('btn-scan-run');
  btn.disabled=true; btn.textContent='Scanning...';
  try {
    if (apiAvailable) {
      const r = await fetch(API_URL + '/api/scan', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({source: src}),
        signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
      });
      if (r.ok) {
        const data = await r.json();
        showTokens(data.tokens || []);
        btn.innerHTML='<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true"><polygon points="5 3 19 12 5 21 5 3"/></svg> Scan (API)';
        btn.disabled=false;
        return;
      }
    }
  } catch(_) {}
  showTokens(tokenize(src));
  btn.innerHTML='<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true"><polygon points="5 3 19 12 5 21 5 3"/></svg> Scan (Local)';
  btn.disabled=false;
});

document.getElementById('scan-file-input').addEventListener('change',e=>{
  const f=e.target.files[0]; if(!f) return;
  const r=new FileReader();
  r.onload=()=>{try{showTokens(JSON.parse(r.result));}catch(err){alert('Invalid JSON: '+err.message);}};
  r.readAsText(f);
});

/* ── Init ─────────────────────────────────────────── */
syncJsonToForm(SAMPLE_DFA);
dfaJsonEl.value=JSON.stringify(SAMPLE_DFA,null,2);

/* ═══════════════════════════════════════════════════
   LR(0) Builder
   ═══════════════════════════════════════════════════ */

const SAMPLE_GRAMMAR = `# PPT 示例：表达式文法
%start E
%terminals ADD MUL LPAR RPAR ID

E -> E ADD T | T
T -> T MUL F | F
F -> LPAR E RPAR | ID
`;

const AMBIG_GRAMMAR = `# 二义表达式：制造移进-归约冲突
%start E
%terminals ADD MUL LPAR RPAR ID

E -> E ADD E | E MUL E | LPAR E RPAR | ID
`;

const lr0Canvas = document.getElementById('lr0-canvas');
const lr0Ctx    = lr0Canvas.getContext('2d');
let lr0Data = null;
let lr0Nodes = [], lr0Edges = [];
let lr0HlNode = -1;
let lr0SelectedNode = -1;
let lr0ConflictStates = new Set();
let lr0AcceptStates  = new Set();
let lr0View = { x:0, y:0, scale:1 };
let lr0Drag = { node:null, panning:false, startX:0, startY:0, viewStartX:0, viewStartY:0, nodeOffX:0, nodeOffY:0 };

function lr0Resize() {
  const w = lr0Canvas.parentElement.clientWidth, h = lr0Canvas.parentElement.clientHeight;
  const dpr = window.devicePixelRatio || 1;
  lr0Canvas.width = w * dpr; lr0Canvas.height = h * dpr;
  lr0Ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function lr0ScreenToWorld(sx, sy) {
  const r = lr0Canvas.getBoundingClientRect();
  return { x:(sx - r.left - lr0View.x)/lr0View.scale, y:(sy - r.top - lr0View.y)/lr0View.scale };
}

function lr0Layout(data) {
  const W = lr0Canvas.parentElement.clientWidth, H = lr0Canvas.parentElement.clientHeight;
  const cx = W/2, cy = H/2, R = Math.min(W,H) * 0.34, pad = 50;
  const n = data.states.length;
  const nodes = data.states.map((s,i) => {
    const a = -Math.PI/2 + (2*Math.PI*i)/n;
    return { id:s.id, x:cx + R*Math.cos(a), y:cy + R*Math.sin(a), r:30, vx:0, vy:0,
             label:'I'+s.id };
  });
  if (n <= 3) return nodes;

  const adj = new Set();
  for (const e of data.edges) {
    if (e.from === e.to) continue;
    const a = Math.min(e.from, e.to), b = Math.max(e.from, e.to);
    adj.add(a + ',' + b);
  }
  const edges = [...adj].map(k => k.split(',').map(Number));
  const idxOf = id => nodes.findIndex(no => no.id === id);

  const idealLen = Math.min(W, H) / Math.max(2, Math.sqrt(n) * 0.85);
  const iters = Math.min(300, 80 + n * 12);
  for (let it = 0; it < iters; it++) {
    const temp = 1 - it/iters;
    const maxDisp = Math.max(2, temp * idealLen * 0.4);
    nodes.forEach(no => { no.vx = 0; no.vy = 0; });
    for (let i = 0; i < n; i++)
      for (let j = i+1; j < n; j++) {
        let dx = nodes[i].x - nodes[j].x, dy = nodes[i].y - nodes[j].y;
        let d = Math.sqrt(dx*dx + dy*dy) || 1;
        const repulse = (idealLen*idealLen)/d;
        const fx = (dx/d)*repulse, fy = (dy/d)*repulse;
        nodes[i].vx += fx; nodes[i].vy += fy;
        nodes[j].vx -= fx; nodes[j].vy -= fy;
      }
    for (const [a,b] of edges) {
      const i = idxOf(a), j = idxOf(b); if (i<0||j<0) continue;
      let dx = nodes[j].x - nodes[i].x, dy = nodes[j].y - nodes[i].y;
      let d = Math.sqrt(dx*dx + dy*dy) || 1;
      const attract = (d*d)/idealLen * 0.15;
      const fx = (dx/d)*attract, fy = (dy/d)*attract;
      nodes[i].vx += fx; nodes[i].vy += fy;
      nodes[j].vx -= fx; nodes[j].vy -= fy;
    }
    nodes.forEach(no => { no.vx += (cx - no.x) * 0.01; no.vy += (cy - no.y) * 0.01; });
    nodes.forEach(no => {
      const d = Math.sqrt(no.vx*no.vx + no.vy*no.vy) || 1;
      const s = Math.min(d, maxDisp);
      no.x += (no.vx/d)*s; no.y += (no.vy/d)*s;
      no.x = Math.max(pad, Math.min(W - pad, no.x));
      no.y = Math.max(pad, Math.min(H - pad, no.y));
    });
  }
  return nodes;
}

function lr0BuildEdges(data) {
  const m = {};
  for (const e of data.edges) {
    const k = e.from + '->' + e.to;
    if (!m[k]) m[k] = [];
    m[k].push(e.sym);
  }
  return Object.entries(m).map(([k,v]) => {
    const [from,to] = k.split('->').map(Number);
    return { from, to, label: v.length > 4 ? v.slice(0,3).join(',')+'…' : v.join(',') };
  });
}

function lr0GetNode(id) { return lr0Nodes.find(n => n.id === id); }

function lr0DrawArrow(x1,y1,x2,y2,r1,r2,color,w,curve) {
  const dx=x2-x1, dy=y2-y1, d=Math.sqrt(dx*dx+dy*dy); if (d<1) return;
  const ux=dx/d, uy=dy/d;
  const sx=x1+ux*r1, sy=y1+uy*r1;
  const ex=x2-ux*(r2+5), ey=y2-uy*(r2+5);
  lr0Ctx.beginPath();
  if (curve) {
    const mx=(sx+ex)/2+(-uy)*curve, my=(sy+ey)/2+(ux)*curve;
    lr0Ctx.moveTo(sx,sy); lr0Ctx.quadraticCurveTo(mx,my,ex,ey);
  } else {
    lr0Ctx.moveTo(sx,sy); lr0Ctx.lineTo(ex,ey);
  }
  lr0Ctx.strokeStyle=color; lr0Ctx.lineWidth=w; lr0Ctx.stroke();
  let adx, ady;
  if (curve) {
    const t=0.95, mt=1-t;
    const px=mt*mt*sx+2*mt*t*((sx+ex)/2+(-uy)*curve)+t*t*ex;
    const py=mt*mt*sy+2*mt*t*((sy+ey)/2+(ux)*curve)+t*t*ey;
    adx=ex-px; ady=ey-py;
    const ad=Math.sqrt(adx*adx+ady*ady)||1; adx/=ad; ady/=ad;
  } else { adx=ux; ady=uy; }
  lr0Ctx.beginPath(); lr0Ctx.moveTo(ex+adx*9,ey+ady*9);
  lr0Ctx.lineTo(ex-adx+ady*4.5,ey-ady-adx*4.5);
  lr0Ctx.lineTo(ex-adx-ady*4.5,ey-ady+adx*4.5);
  lr0Ctx.closePath(); lr0Ctx.fillStyle=color; lr0Ctx.fill();
}

function lr0DrawSelfLoop(x,y,r,color,w,label) {
  const lr=18, cy2=y-r-lr;
  lr0Ctx.beginPath(); lr0Ctx.arc(x,cy2,lr,0.3,Math.PI*2-0.3);
  lr0Ctx.strokeStyle=color; lr0Ctx.lineWidth=w; lr0Ctx.stroke();
  const a=-0.3, ax=x+lr*Math.cos(a), ay=cy2+lr*Math.sin(a);
  lr0Ctx.beginPath(); lr0Ctx.moveTo(ax,ay); lr0Ctx.lineTo(ax+3,ay+7); lr0Ctx.lineTo(ax-4,ay+5);
  lr0Ctx.closePath(); lr0Ctx.fillStyle=color; lr0Ctx.fill();
  if (label) {
    lr0Ctx.font='500 10px "JetBrains Mono",monospace'; lr0Ctx.fillStyle='#71717a';
    lr0Ctx.textAlign='center'; lr0Ctx.textBaseline='middle';
    lr0Ctx.fillText(label, x, y - r - 36);
  }
}

function lr0Draw() {
  const W=lr0Canvas.parentElement.clientWidth, H=lr0Canvas.parentElement.clientHeight;
  lr0Ctx.clearRect(0,0,W,H);
  lr0Ctx.save();
  lr0Ctx.translate(lr0View.x, lr0View.y);
  lr0Ctx.scale(lr0View.scale, lr0View.scale);

  lr0Edges.forEach(e => {
    const from=lr0GetNode(e.from), to=lr0GetNode(e.to); if(!from||!to) return;
    const col = '#52525b', w = 1.5;
    if (e.from === e.to) {
      lr0DrawSelfLoop(from.x, from.y, from.r, col, w, e.label);
    } else {
      const hasReverse = lr0Edges.some(o => o.from===e.to && o.to===e.from && o.from !== o.to);
      const curve = hasReverse ? (e.from < e.to ? 30 : -30) : 0;
      lr0DrawArrow(from.x, from.y, to.x, to.y, from.r, to.r, col, w, curve);
      const dx=to.x-from.x, dy=to.y-from.y, d=Math.sqrt(dx*dx+dy*dy)||1;
      const nx=-dy/d, ny=dx/d;
      const labelOff = curve ? curve*0.6 : 14;
      const mx=(from.x+to.x)/2+nx*labelOff, my=(from.y+to.y)/2+ny*labelOff;
      lr0Ctx.font='500 10px "JetBrains Mono",monospace'; lr0Ctx.fillStyle='#a1a1aa';
      lr0Ctx.textAlign='center'; lr0Ctx.textBaseline='middle';
      lr0Ctx.fillText(e.label, mx, my);
    }
  });

  lr0Nodes.forEach(n => {
    const isH = n.id === lr0HlNode;
    const isSel = n.id === lr0SelectedNode;
    const isStart = n.id === 0;
    const isAccept = lr0AcceptStates.has(n.id);
    const isConflict = lr0ConflictStates.has(n.id);

    if (isH || isSel || isStart || isAccept || isConflict) {
      lr0Ctx.beginPath(); lr0Ctx.arc(n.x, n.y, n.r+8, 0, Math.PI*2);
      lr0Ctx.fillStyle = isConflict ? 'rgba(239,68,68,.15)'
                       : isSel      ? 'rgba(167,139,250,.18)'
                       : isStart    ? 'rgba(59,130,246,.12)'
                       : isAccept   ? 'rgba(34,197,94,.1)'
                                    : 'rgba(167,139,250,.10)';
      lr0Ctx.fill();
    }
    lr0Ctx.beginPath(); lr0Ctx.arc(n.x, n.y, n.r, 0, Math.PI*2);
    lr0Ctx.fillStyle = isSel ? '#4c1d95' : isConflict ? '#7f1d1d'
                      : isStart ? '#1e3a8a' : isAccept ? '#14532d' : '#27272a';
    lr0Ctx.fill();
    lr0Ctx.strokeStyle = isConflict ? '#ef4444'
                       : isSel ? '#a78bfa'
                       : isStart ? '#3b82f6'
                       : isAccept ? '#22c55e' : '#3f3f46';
    lr0Ctx.lineWidth = (isConflict || isAccept || isSel) ? 2.5 : 1.5;
    lr0Ctx.stroke();
    if (isAccept) {
      lr0Ctx.beginPath(); lr0Ctx.arc(n.x, n.y, n.r-4, 0, Math.PI*2);
      lr0Ctx.strokeStyle = '#22c55e'; lr0Ctx.lineWidth = 1; lr0Ctx.stroke();
    }
    if (isStart) {
      const ax=n.x-n.r-18;
      lr0Ctx.beginPath(); lr0Ctx.moveTo(ax-12,n.y); lr0Ctx.lineTo(ax,n.y);
      lr0Ctx.strokeStyle='#3b82f6'; lr0Ctx.lineWidth=2; lr0Ctx.stroke();
      lr0Ctx.beginPath(); lr0Ctx.moveTo(ax,n.y); lr0Ctx.lineTo(ax-6,n.y-4); lr0Ctx.lineTo(ax-6,n.y+4);
      lr0Ctx.closePath(); lr0Ctx.fillStyle='#3b82f6'; lr0Ctx.fill();
    }
    lr0Ctx.font='600 13px "JetBrains Mono",monospace'; lr0Ctx.fillStyle='#fafafa';
    lr0Ctx.textAlign='center'; lr0Ctx.textBaseline='middle';
    lr0Ctx.fillText(n.label, n.x, n.y);
  });
  lr0Ctx.restore();
}

function lr0FormatItem(prod, dot) {
  const p = lr0Data.productions[prod];
  if (!p) return '?';
  const parts = [p.lhs, '→'];
  if (p.rhs.length === 0) {
    parts.push('·');
  } else {
    for (let i = 0; i < p.rhs.length; i++) {
      if (i === dot) parts.push('·');
      parts.push(p.rhs[i]);
    }
    if (dot === p.rhs.length) parts.push('·');
  }
  return parts.join(' ');
}

function lr0ShowState(id) {
  const detail = document.getElementById('lr0-detail');
  if (id < 0 || !lr0Data) {
    detail.innerHTML = '<p class="muted">Click a state in the graph to inspect its closure.</p>';
    return;
  }
  const st = lr0Data.states[id];
  if (!st) return;
  const items = st.items.map(it => '<li><code>'+lr0FormatItem(it.prod, it.dot)+'</code></li>').join('');
  const outgoing = lr0Data.edges.filter(e => e.from === id);
  const goto = outgoing.length === 0 ? '' :
    '<p class="lr0-goto-title">Goto:</p><ul class="lr0-goto-list">' +
    outgoing.map(e => '<li><code>'+e.sym+'</code> → I'+e.to+'</li>').join('') + '</ul>';
  const conflictBadge = lr0ConflictStates.has(id) ? '<span class="lr0-badge lr0-badge-err">conflict</span>' : '';
  const acceptBadge = lr0AcceptStates.has(id) ? '<span class="lr0-badge lr0-badge-ok">accept</span>' : '';
  detail.innerHTML = '<h4>I'+id+' '+conflictBadge+acceptBadge+'</h4>'+
                     '<ul class="lr0-item-list">'+items+'</ul>'+goto;
}

function lr0ShowConflicts() {
  const el = document.getElementById('lr0-conflicts');
  if (!lr0Data) { el.innerHTML = '<p class="muted">No grammar built yet.</p>'; return; }
  if (lr0Data.conflicts.length === 0) {
    el.innerHTML = '<p class="lr0-ok">No conflicts. Grammar is LR(0).</p>';
    return;
  }
  el.innerHTML = '<p class="lr0-err">Not LR(0): '+lr0Data.conflicts.length+' conflict(s)</p>'+
    '<ul class="lr0-conflict-list">'+
    lr0Data.conflicts.map(c => {
      const items = c.reduce_items.map(it => '<li><code>'+lr0FormatItem(it.prod, it.dot)+'</code></li>').join('');
      const on = c.on ? ' on <code>'+c.on+'</code>' : '';
      return '<li><a href="#" data-state="'+c.state+'">I'+c.state+'</a> '+
             '<span class="lr0-badge lr0-badge-err">'+c.kind+'</span>'+on+
             '<ul>'+items+'</ul></li>';
    }).join('') + '</ul>';
  el.querySelectorAll('a[data-state]').forEach(a => {
    a.addEventListener('click', ev => {
      ev.preventDefault();
      const sid = parseInt(a.dataset.state, 10);
      lr0SelectedNode = sid;
      lr0ShowState(sid);
      lr0Draw();
    });
  });
}

function lr0Render(data) {
  lr0Data = data;
  lr0ConflictStates = new Set(data.conflicts.map(c => c.state));
  lr0AcceptStates = new Set();
  data.states.forEach(s => {
    s.items.forEach(it => {
      const p = data.productions[it.prod];
      if (p && p.lhs.endsWith("'") && it.dot === p.rhs.length) lr0AcceptStates.add(s.id);
    });
  });
  lr0Resize();
  lr0Nodes = lr0Layout(data);
  lr0Edges = lr0BuildEdges(data);
  lr0HlNode = -1; lr0SelectedNode = -1;
  lr0View = { x:0, y:0, scale:1 };
  lr0Draw();
  document.getElementById('lr0-canvas-empty').classList.add('hidden');
  lr0ShowState(-1);
  lr0ShowConflicts();
  const status = document.getElementById('lr0-status');
  status.textContent = data.states.length + ' states · ' + data.edges.length + ' edges';
  status.className = 'stats-badge ' + (data.is_lr0 ? 'ok' : 'err');
}

function lr0HitTest(ex, ey) {
  const w = lr0ScreenToWorld(ex, ey);
  for (let i = lr0Nodes.length - 1; i >= 0; i--) {
    const n = lr0Nodes[i];
    const dx = w.x - n.x, dy = w.y - n.y;
    if (dx*dx + dy*dy <= n.r*n.r) return n;
  }
  return null;
}

lr0Canvas.addEventListener('pointerdown', e => {
  const hit = lr0HitTest(e.clientX, e.clientY);
  lr0Drag.startX = e.clientX;
  lr0Drag.startY = e.clientY;
  if (hit) {
    lr0Drag.node = hit;
    const w = lr0ScreenToWorld(e.clientX, e.clientY);
    lr0Drag.nodeOffX = hit.x - w.x; lr0Drag.nodeOffY = hit.y - w.y;
    lr0Canvas.style.cursor = 'grabbing';
  } else {
    lr0Drag.panning = true;
    lr0Drag.viewStartX = lr0View.x; lr0Drag.viewStartY = lr0View.y;
    lr0Canvas.style.cursor = 'grabbing';
  }
  lr0Canvas.setPointerCapture(e.pointerId);
});

lr0Canvas.addEventListener('pointermove', e => {
  if (lr0Drag.node) {
    const w = lr0ScreenToWorld(e.clientX, e.clientY);
    lr0Drag.node.x = w.x + lr0Drag.nodeOffX;
    lr0Drag.node.y = w.y + lr0Drag.nodeOffY;
    lr0Draw();
  } else if (lr0Drag.panning) {
    lr0View.x = lr0Drag.viewStartX + (e.clientX - lr0Drag.startX);
    lr0View.y = lr0Drag.viewStartY + (e.clientY - lr0Drag.startY);
    lr0Draw();
  } else {
    const hit = lr0HitTest(e.clientX, e.clientY);
    lr0Canvas.style.cursor = hit ? 'pointer' : 'grab';
    if ((hit ? hit.id : -1) !== lr0HlNode) {
      lr0HlNode = hit ? hit.id : -1;
      lr0Draw();
    }
  }
});

lr0Canvas.addEventListener('pointerup', e => {
  const hit = lr0HitTest(e.clientX, e.clientY);
  const draggedFar = Math.abs(e.clientX - lr0Drag.startX) > 4 ||
                     Math.abs(e.clientY - lr0Drag.startY) > 4;
  if (hit && lr0Drag.node && !draggedFar) {
    lr0SelectedNode = hit.id;
    lr0ShowState(hit.id);
    lr0Draw();
  }
  lr0Drag.node = null; lr0Drag.panning = false;
  lr0Canvas.releasePointerCapture(e.pointerId);
  lr0Canvas.style.cursor = hit ? 'pointer' : 'grab';
});

lr0Canvas.addEventListener('wheel', e => {
  e.preventDefault();
  const r = lr0Canvas.getBoundingClientRect();
  const mx = e.clientX - r.left, my = e.clientY - r.top;
  const factor = e.deltaY < 0 ? 1.12 : 1/1.12;
  const newScale = Math.max(0.2, Math.min(5, lr0View.scale * factor));
  lr0View.x = mx - (mx - lr0View.x) * (newScale / lr0View.scale);
  lr0View.y = my - (my - lr0View.y) * (newScale / lr0View.scale);
  lr0View.scale = newScale;
  lr0Draw();
}, { passive:false });

window.addEventListener('resize', () => {
  if (lr0Data) {
    lr0Resize();
    lr0Nodes = lr0Layout(lr0Data);
    lr0View = { x:0, y:0, scale:1 };
    lr0Draw();
  }
});

async function lr0Build(grammar) {
  const status = document.getElementById('lr0-status');
  status.textContent = 'Building...';
  status.className = 'stats-badge';
  if (!apiAvailable) {
    status.textContent = 'API offline';
    status.className = 'stats-badge err';
    document.getElementById('lr0-conflicts').innerHTML =
      '<p class="lr0-err">Backend API is offline. LR(0) build needs the compiler binary running on a server.</p>';
    return;
  }
  try {
    const r = await fetch(API_URL + '/api/lr0', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ grammar }),
      signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
    });
    const data = await r.json();
    if (!r.ok) {
      status.textContent = 'Error';
      status.className = 'stats-badge err';
      document.getElementById('lr0-conflicts').innerHTML =
        '<p class="lr0-err">'+(data.error || 'build failed')+'</p>';
      return;
    }
    lr0Render(data);
  } catch (err) {
    status.textContent = 'Network error';
    status.className = 'stats-badge err';
    document.getElementById('lr0-conflicts').innerHTML =
      '<p class="lr0-err">Network error: '+err.message+'</p>';
  }
}

document.getElementById('btn-lr0-sample').addEventListener('click', () => {
  document.getElementById('lr0-grammar').value = SAMPLE_GRAMMAR;
});
document.getElementById('btn-lr0-ambig').addEventListener('click', () => {
  document.getElementById('lr0-grammar').value = AMBIG_GRAMMAR;
});
document.getElementById('btn-lr0-build').addEventListener('click', () => {
  const g = document.getElementById('lr0-grammar').value.trim();
  if (!g) { alert('Grammar is empty'); return; }
  lr0Build(g);
});

/* 切到 LR(0) Tab 时若 canvas 尺寸为 0（之前 hidden 没布局），重绘 */
document.querySelectorAll('.nav-tab[data-view="lr0"]').forEach(t => {
  t.addEventListener('click', () => {
    if (lr0Data) requestAnimationFrame(() => { lr0Resize(); lr0Draw(); });
  });
});
