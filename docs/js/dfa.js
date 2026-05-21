/* ═══════════════════════════════════════════════════
   Lab 1 — DFA Explorer
   Form/JSON toggle, force-directed layout, simulate, enumerate
   Depends on: shared.js
   ═══════════════════════════════════════════════════ */

const SAMPLE_DFA = {
  extended: false, states: 4, start: 1,
  accept: [4], accept_labels: {},
  alphabet: "ab",
  transitions: [[2,3],[4,3],[2,4],[4,4]],
  keywords: {}
};

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

const canvas = document.getElementById('dfa-canvas');
const ctx = canvas.getContext('2d');

function screenToWorld(sx, sy) {
  const rect = canvas.getBoundingClientRect();
  return { x: (sx - rect.left - viewX) / viewScale, y: (sy - rect.top - viewY) / viewScale };
}

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

/* ── Init ─────────────────────────────────────────── */
syncJsonToForm(SAMPLE_DFA);
dfaJsonEl.value = JSON.stringify(SAMPLE_DFA, null, 2);
