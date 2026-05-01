/* ═══════════════════════════════════════════════════
   XJTU Compiler Visualizer
   ═══════════════════════════════════════════════════ */

/* ── API Config ───────────────────────────────────── */
const API_URL = localStorage.getItem('api_url') || 'http://igw.netperf.cc:8080';
let apiAvailable = false;

async function checkAPI() {
  const badge = document.getElementById('api-badge');
  try {
    const r = await fetch(API_URL + '/api/health', { signal: AbortSignal.timeout(2000) });
    if (r.ok) { apiAvailable = true; badge.textContent = 'API online'; badge.className = 'api-badge online'; }
  } catch (_) { apiAvailable = false; badge.textContent = 'Local mode'; badge.className = 'api-badge offline'; }
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
  extended: false, states: 3, start: 1,
  accept: [3], accept_labels: {},
  alphabet: "ab",
  transitions: [[2,1],[2,3],[2,3]],
  keywords: {}
};

const SAMPLE_SOURCE = `int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a - (a / b) * b;
        a = t;
    }
    return a;
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
  const nodes = [];
  for (let i = 0; i < count; i++) {
    const s = lo + i, angle = -Math.PI/2 + (2*Math.PI*i)/count;
    nodes.push({ id:s, x:cx+R*Math.cos(angle), y:cy+R*Math.sin(angle), r:26,
      isStart: s===dfa.start, isAccept: dfa.accept.includes(s),
      label: dfa.accept_labels&&dfa.accept_labels[s] ? s+'('+dfa.accept_labels[s]+')' : ''+s });
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

function drawArrow(x1,y1,x2,y2,r1,r2,color,w) {
  const dx=x2-x1, dy=y2-y1, d=Math.sqrt(dx*dx+dy*dy); if(d<1)return;
  const ux=dx/d, uy=dy/d;
  ctx.beginPath(); ctx.moveTo(x1+ux*r1,y1+uy*r1); ctx.lineTo(x2-ux*(r2+5),y2-uy*(r2+5));
  ctx.strokeStyle=color; ctx.lineWidth=w; ctx.stroke();
  const ex=x2-ux*(r2+5), ey=y2-uy*(r2+5);
  ctx.beginPath(); ctx.moveTo(ex+ux*9,ey+uy*9);
  ctx.lineTo(ex-ux*1+uy*4.5,ey-uy*1-ux*4.5);
  ctx.lineTo(ex-ux*1-uy*4.5,ey-uy*1+ux*4.5);
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

  dfaEdges.forEach((e,i)=>{
    const from=getNode(e.from), to=getNode(e.to); if(!from||!to) return;
    const isH = i===hlEdge, col = isH?'#a78bfa':'#52525b', w = isH?2.5:1.5;
    if (e.from===e.to) {
      drawSelfLoop(from.x,from.y,from.r,col,w);
      ctx.font='500 10px "JetBrains Mono",monospace'; ctx.fillStyle=isH?'#c4b5fd':'#71717a';
      ctx.textAlign='center'; ctx.textBaseline='middle';
      ctx.fillText(e.label,from.x,from.y-from.r-36);
    } else {
      drawArrow(from.x,from.y,to.x,to.y,from.r,to.r,col,w);
      const mx=(from.x+to.x)/2, my=(from.y+to.y)/2;
      const dx=to.x-from.x, dy=to.y-from.y, d=Math.sqrt(dx*dx+dy*dy)||1;
      ctx.font='500 10px "JetBrains Mono",monospace'; ctx.fillStyle=isH?'#c4b5fd':'#71717a';
      ctx.textAlign='center'; ctx.textBaseline='middle';
      ctx.fillText(e.label, mx+(-dy/d)*14, my+(dx/d)*14);
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
}

function renderDFA(dfa) {
  currentDFA = dfa; resizeCanvas();
  dfaNodes = layoutNodes(dfa); dfaEdges = buildEdges(dfa);
  hlNode = -1; hlEdge = -1; drawDFA();
  document.getElementById('canvas-empty').classList.add('hidden');
}

window.addEventListener('resize',()=>{ if(currentDFA){resizeCanvas();dfaNodes=layoutNodes(currentDFA);drawDFA();} });

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
  (function f(s,d,b){
    if(dfa.accept.includes(s)) res.push(b||'ε');
    if(d>=maxLen) return;
    const row=dfa.transitions[s-lo]; if(!row) return;
    for(let c=0;c<sym.length;c++){const n=row[c]; if(dfa.extended&&n===0&&s!==0) continue; f(n,d+1,b+sym[c]);}
  })(dfa.start,0,'');
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
const KW={int:'INT',float:'FLOAT',void:'VOID','if':'IF','else':'ELSE','while':'WHILE','return':'RETURN',input:'INPUT',print:'PRINT'};

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
      if(c===46)adv(); while(isD(pk()))adv(); let fl=c===46;
      if(c!==46&&pk()===46){fl=true;adv();while(isD(pk()))adv();}
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
const TC={INT:'tk-keyword',FLOAT:'tk-keyword',VOID:'tk-keyword',IF:'tk-keyword',ELSE:'tk-keyword',WHILE:'tk-keyword',RETURN:'tk-keyword',INPUT:'tk-keyword',PRINT:'tk-keyword',ID:'tk-id',NUM:'tk-num',FLOAT_LIT:'tk-float',ADD:'tk-op',SUB:'tk-op',MUL:'tk-op',DIV:'tk-op',LT:'tk-op',LE:'tk-op',EQ:'tk-op',GT:'tk-op',GE:'tk-op',NE:'tk-op',AND:'tk-op',OR:'tk-op',NOT:'tk-op',ASG:'tk-op',AAS:'tk-op',AAA:'tk-op',LPAR:'tk-delim',RPAR:'tk-delim',LBK:'tk-delim',RBK:'tk-delim',LBR:'tk-delim',RBR:'tk-delim',CMA:'tk-delim',COL:'tk-delim',SCO:'tk-delim',DOT:'tk-delim',ERR:'tk-err'};

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
        signal: AbortSignal.timeout(5000)
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
