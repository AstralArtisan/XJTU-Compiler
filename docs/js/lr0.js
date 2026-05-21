/* ═══════════════════════════════════════════════════
   Lab 3 — LR(0) Builder
   Grammar parse, closure/goto, canonical collection, canvas + conflicts
   Depends on: shared.js
   Exports (globals, also used by slr.js):
     SAMPLE_GRAMMAR, AMBIG_GRAMMAR
     lr0ParseGrammar, lr0Closure, lr0GotoLocal, lr0ItemsEqual
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

function lr0ParseGrammar(text) {
  const symbols = new Map();
  const symbolList = [];
  const terminalDecls = new Set();
  let startName = '';
  let seenRule = false;
  const productions = [];

  const intern = (name, kind) => {
    if (symbols.has(name)) {
      const sym = symbolList[symbols.get(name)];
      if (kind === 'nonterm' && sym.kind === 'term') sym.kind = 'nonterm';
      return sym;
    }
    const sym = { id: symbolList.length, name, kind };
    symbols.set(name, sym.id);
    symbolList.push(sym);
    return sym;
  };

  intern('$', 'end');
  intern('epsilon', 'eps');

  const stripComment = line => line.replace(/#.*/, '').trim();
  const isEps = tok => tok === 'epsilon' || tok === 'EPSILON' || tok === 'ε';
  const parseRhs = rhs => {
    const out = [];
    rhs.trim().split(/\s+/).filter(Boolean).forEach(tok => {
      if (isEps(tok)) return;
      let kind = 'term';
      if (terminalDecls.size > 0) {
        kind = terminalDecls.has(tok) ? 'term' : 'nonterm';
      } else if (/^[A-Z]$/.test(tok)) {
        kind = 'nonterm';
      }
      out.push(intern(tok, kind).id);
    });
    return out;
  };

  text.split(/\r?\n/).forEach(raw => {
    const line = stripComment(raw);
    if (!line) return;

    if (line.startsWith('%') && !seenRule) {
      const parts = line.slice(1).trim().split(/\s+/).filter(Boolean);
      const name = parts.shift();
      if (name === 'start') {
        startName = parts[0] || '';
      } else if (name === 'terminals') {
        parts.forEach(t => terminalDecls.add(t));
      }
      return;
    }

    const m = line.match(/^(.*?)\s*(?:->|::=|→)\s*(.*)$/);
    if (!m) throw new Error('no arrow in rule: ' + line);
    seenRule = true;
    const lhsName = m[1].trim();
    if (!lhsName) throw new Error('empty lhs in rule: ' + line);
    const lhs = intern(lhsName, 'nonterm').id;
    if (!startName) startName = lhsName;

    m[2].split('|').forEach(alt => {
      productions.push({ lhs, rhs: parseRhs(alt) });
    });
  });

  if (productions.length === 0) throw new Error('no productions');
  const start = symbols.get(startName);
  if (start === undefined) throw new Error('start symbol not in grammar: ' + startName);

  productions.forEach(p => { symbolList[p.lhs].kind = 'nonterm'; });
  const augName = startName + "'";
  const aug = intern(augName, 'nonterm').id;
  productions.unshift({ lhs: aug, rhs: [start] });

  return {
    symbols: symbolList,
    productions,
    augmentedStart: aug
  };
}

function lr0ItemKey(item) { return item.prod + ':' + item.dot; }

function lr0SortItems(items) {
  items.sort((a, b) => a.prod - b.prod || a.dot - b.dot);
  return items;
}

function lr0Closure(grammar, seed) {
  const items = seed.map(it => ({ prod: it.prod, dot: it.dot }));
  const seen = new Set(items.map(lr0ItemKey));
  let changed = true;

  while (changed) {
    changed = false;
    for (let i = 0; i < items.length; i++) {
      const it = items[i];
      const prod = grammar.productions[it.prod];
      const next = prod.rhs[it.dot];
      if (next === undefined || grammar.symbols[next].kind !== 'nonterm') continue;
      grammar.productions.forEach((p, prodId) => {
        if (p.lhs !== next) return;
        const candidate = { prod: prodId, dot: 0 };
        const key = lr0ItemKey(candidate);
        if (!seen.has(key)) {
          seen.add(key);
          items.push(candidate);
          changed = true;
        }
      });
    }
  }

  return lr0SortItems(items);
}

function lr0GotoLocal(grammar, items, sym) {
  const moved = [];
  items.forEach(it => {
    const prod = grammar.productions[it.prod];
    if (prod.rhs[it.dot] === sym) moved.push({ prod: it.prod, dot: it.dot + 1 });
  });
  return moved.length ? lr0Closure(grammar, moved) : [];
}

function lr0ItemsEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i].prod !== b[i].prod || a[i].dot !== b[i].dot) return false;
  }
  return true;
}

function lr0BuildLocal(grammarText) {
  const grammar = lr0ParseGrammar(grammarText);
  const states = [lr0Closure(grammar, [{ prod: 0, dot: 0 }])];
  const edges = [];
  const work = [0];

  while (work.length) {
    const from = work.shift();
    const seenSymbols = [];
    states[from].forEach(it => {
      const sym = grammar.productions[it.prod].rhs[it.dot];
      if (sym !== undefined && !seenSymbols.includes(sym)) seenSymbols.push(sym);
    });

    seenSymbols.forEach(sym => {
      const nextItems = lr0GotoLocal(grammar, states[from], sym);
      if (!nextItems.length) return;
      let to = states.findIndex(s => lr0ItemsEqual(s, nextItems));
      if (to < 0) {
        to = states.length;
        states.push(nextItems);
        work.push(to);
      }
      edges.push({ from, sym: grammar.symbols[sym].name, to });
    });
  }

  const conflicts = [];
  states.forEach((items, stateId) => {
    const reduceIdx = [];
    let hasShift = false;
    let shiftSym = -1;

    items.forEach((it, itemIdx) => {
      const prod = grammar.productions[it.prod];
      if (it.dot >= prod.rhs.length) {
        reduceIdx.push(itemIdx);
      } else {
        const sym = prod.rhs[it.dot];
        if (grammar.symbols[sym].kind === 'term') {
          hasShift = true;
          if (shiftSym < 0) shiftSym = sym;
        }
      }
    });

    const onlyAcceptReduce = reduceIdx.length === 1 &&
      grammar.productions[items[reduceIdx[0]].prod].lhs === grammar.augmentedStart;
    const shiftReduce = reduceIdx.length >= 1 && hasShift && !onlyAcceptReduce;
    const reduceReduce = reduceIdx.length >= 2;
    if (!shiftReduce && !reduceReduce) return;

    const conflict = {
      state: stateId,
      kind: reduceReduce ? 'reduce-reduce' : 'shift-reduce',
      reduce_items: reduceIdx.map(i => ({ prod: items[i].prod, dot: items[i].dot }))
    };
    if (shiftReduce && shiftSym >= 0) conflict.on = grammar.symbols[shiftSym].name;
    conflicts.push(conflict);
  });

  return {
    is_lr0: conflicts.length === 0,
    symbols: grammar.symbols.map(s => ({ id: s.id, name: s.name, kind: s.kind })),
    productions: grammar.productions.map((p, id) => ({
      id,
      lhs: grammar.symbols[p.lhs].name,
      rhs: p.rhs.map(sym => grammar.symbols[sym].name)
    })),
    states: states.map((items, id) => ({ id, items })),
    edges,
    conflicts
  };
}

/* ── Canvas + interaction ─────────────────────────── */
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

function lr0Render(data, source) {
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
  status.textContent = data.states.length + ' states · ' + data.edges.length + ' edges' +
    (source ? ' · ' + source : '');
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
  try {
    if (apiAvailable) {
      try {
        const r = await fetch(API_URL + '/api/lr0', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ grammar }),
          signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
        });
        if (r.ok) {
          lr0Render(await r.json(), 'API');
          return;
        }
      } catch (_) {}
    }

    lr0Render(lr0BuildLocal(grammar), 'Local');
  } catch (err) {
    status.textContent = 'Error';
    status.className = 'stats-badge err';
    document.getElementById('lr0-conflicts').innerHTML =
      '<p class="lr0-err">'+esc(err.message || String(err))+'</p>';
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
