/* ═══════════════════════════════════════════════════
   XJTU Compiler Visualizer — app.js
   ═══════════════════════════════════════════════════ */

/* ── Tab Switching ────────────────────────────────── */
document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => { t.classList.remove('active'); t.setAttribute('aria-selected', 'false'); });
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    tab.classList.add('active');
    tab.setAttribute('aria-selected', 'true');
    document.getElementById('view-' + tab.dataset.view).classList.add('active');
  });
});

/* ── Sample Data ─────────────────────────────────── */
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

const SAMPLE_TOKENS = [
  {kind:"INT",lexeme:"int",line:1,col:1},{kind:"ID",lexeme:"gcd",line:1,col:5},
  {kind:"LPAR",lexeme:"(",line:1,col:8},{kind:"INT",lexeme:"int",line:1,col:9},
  {kind:"ID",lexeme:"a",line:1,col:13},{kind:"CMA",lexeme:",",line:1,col:14},
  {kind:"INT",lexeme:"int",line:1,col:16},{kind:"ID",lexeme:"b",line:1,col:20},
  {kind:"RPAR",lexeme:")",line:1,col:21},{kind:"LBR",lexeme:"{",line:1,col:23},
  {kind:"WHILE",lexeme:"while",line:2,col:5},{kind:"LPAR",lexeme:"(",line:2,col:11},
  {kind:"ID",lexeme:"b",line:2,col:12},{kind:"NE",lexeme:"!=",line:2,col:14},
  {kind:"NUM",lexeme:"0",line:2,col:17},{kind:"RPAR",lexeme:")",line:2,col:18},
  {kind:"LBR",lexeme:"{",line:2,col:20},{kind:"INT",lexeme:"int",line:3,col:9},
  {kind:"ID",lexeme:"t",line:3,col:13},{kind:"ASG",lexeme:"=",line:3,col:15},
  {kind:"ID",lexeme:"b",line:3,col:17},{kind:"SCO",lexeme:";",line:3,col:18},
  {kind:"ID",lexeme:"b",line:4,col:9},{kind:"ASG",lexeme:"=",line:4,col:11},
  {kind:"ID",lexeme:"a",line:4,col:13},{kind:"SUB",lexeme:"-",line:4,col:15},
  {kind:"LPAR",lexeme:"(",line:4,col:17},{kind:"ID",lexeme:"a",line:4,col:18},
  {kind:"DIV",lexeme:"/",line:4,col:20},{kind:"ID",lexeme:"b",line:4,col:22},
  {kind:"RPAR",lexeme:")",line:4,col:23},{kind:"MUL",lexeme:"*",line:4,col:25},
  {kind:"ID",lexeme:"b",line:4,col:27},{kind:"SCO",lexeme:";",line:4,col:28},
  {kind:"ID",lexeme:"a",line:5,col:9},{kind:"ASG",lexeme:"=",line:5,col:11},
  {kind:"ID",lexeme:"t",line:5,col:13},{kind:"SCO",lexeme:";",line:5,col:14},
  {kind:"RBR",lexeme:"}",line:6,col:5},
  {kind:"RETURN",lexeme:"return",line:7,col:5},{kind:"ID",lexeme:"a",line:7,col:12},
  {kind:"SCO",lexeme:";",line:7,col:13},{kind:"RBR",lexeme:"}",line:8,col:1}
];

/* ── DFA State ───────────────────────────────────── */
let currentDFA = null;
let dfaNodes = [];
let dfaEdges = [];
let highlightNode = -1;
let highlightEdge = -1;
const canvas = document.getElementById('dfa-canvas');
const ctx = canvas.getContext('2d');

const COLORS = {
  bg: '#0a0e17', node: '#1e293b', nodeBorder: '#334155',
  start: '#1d4ed8', startBorder: '#60a5fa', startGlow: 'rgba(96,165,250,.25)',
  accept: '#166534', acceptBorder: '#4ade80', acceptGlow: 'rgba(74,222,128,.2)',
  highlight: '#7c3aed', highlightGlow: 'rgba(167,139,250,.3)',
  edge: '#475569', edgeLabel: '#94a3b8',
  text: '#f1f5f9', textDim: '#94a3b8'
};

function resizeCanvas() {
  const rect = canvas.parentElement.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = rect.width * dpr;
  canvas.height = 420 * dpr;
  canvas.style.width = rect.width + 'px';
  canvas.style.height = '420px';
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function layoutNodes(dfa) {
  const lo = dfa.extended ? 0 : 1;
  const hi = dfa.extended ? dfa.states - 1 : dfa.states;
  const count = hi - lo + 1;
  const nodes = [];
  const W = canvas.clientWidth;
  const H = 420;
  const cx = W / 2, cy = H / 2;
  const R = Math.min(W, H) * 0.34;

  for (let i = 0; i < count; i++) {
    const s = lo + i;
    const angle = -Math.PI / 2 + (2 * Math.PI * i) / count;
    nodes.push({
      id: s,
      x: cx + R * Math.cos(angle),
      y: cy + R * Math.sin(angle),
      r: 26,
      isStart: s === dfa.start,
      isAccept: dfa.accept.includes(s),
      label: dfa.accept_labels && dfa.accept_labels[s] ? `${s}(${dfa.accept_labels[s]})` : `${s}`
    });
  }
  return nodes;
}

function buildEdges(dfa) {
  const lo = dfa.extended ? 0 : 1;
  const hi = dfa.extended ? dfa.states - 1 : dfa.states;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const map = {};

  for (let s = lo; s <= hi; s++) {
    const row = dfa.transitions[s - lo];
    if (!row) continue;
    for (let c = 0; c < row.length; c++) {
      const t = row[c];
      if (dfa.extended && t === 0 && s !== 0) continue;
      const key = `${s}->${t}`;
      if (!map[key]) map[key] = [];
      map[key].push(symbols[c] || `${c}`);
    }
  }

  return Object.entries(map).map(([key, syms]) => {
    const [from, to] = key.split('->').map(Number);
    return { from, to, label: syms.length > 4 ? syms.slice(0,3).join(',')+'…' : syms.join(',') };
  });
}

function getNode(id) { return dfaNodes.find(n => n.id === id); }

function drawArrow(x1, y1, x2, y2, r2, color, width) {
  const dx = x2 - x1, dy = y2 - y1;
  const dist = Math.sqrt(dx*dx + dy*dy);
  const ux = dx/dist, uy = dy/dist;
  const ex = x2 - ux * (r2 + 4), ey = y2 - uy * (r2 + 4);
  const aLen = 10, aW = 5;

  ctx.beginPath();
  ctx.moveTo(x1 + ux * r2, y1 + uy * r2);
  ctx.lineTo(ex, ey);
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.stroke();

  ctx.beginPath();
  ctx.moveTo(ex, ey);
  ctx.lineTo(ex - ux*aLen + uy*aW, ey - uy*aLen - ux*aW);
  ctx.lineTo(ex - ux*aLen - uy*aW, ey - uy*aLen + ux*aW);
  ctx.closePath();
  ctx.fillStyle = color;
  ctx.fill();
}

function drawSelfLoop(x, y, r, color, width) {
  const loopR = 18;
  const cx = x, cy = y - r - loopR;
  ctx.beginPath();
  ctx.arc(cx, cy, loopR, 0.3, Math.PI * 2 - 0.3);
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.stroke();
  const aLen = 7, angle = -0.3;
  const ax = cx + loopR * Math.cos(angle), ay = cy + loopR * Math.sin(angle);
  ctx.beginPath();
  ctx.moveTo(ax, ay);
  ctx.lineTo(ax + 4, ay + aLen);
  ctx.lineTo(ax - 5, ay + aLen - 2);
  ctx.closePath();
  ctx.fillStyle = color;
  ctx.fill();
}

/* ── Main Draw ───────────────────────────────────── */
function drawDFA() {
  const W = canvas.clientWidth, H = 420;
  ctx.clearRect(0, 0, W, H);

  dfaEdges.forEach((e, i) => {
    const from = getNode(e.from), to = getNode(e.to);
    if (!from || !to) return;
    const isHl = i === highlightEdge;
    const color = isHl ? '#a78bfa' : COLORS.edge;
    const width = isHl ? 2.5 : 1.5;

    if (e.from === e.to) {
      drawSelfLoop(from.x, from.y, from.r, color, width);
      ctx.font = '500 11px "JetBrains Mono"';
      ctx.fillStyle = isHl ? '#c4b5fd' : COLORS.edgeLabel;
      ctx.textAlign = 'center';
      ctx.fillText(e.label, from.x, from.y - from.r - 38);
    } else {
      const mx = (from.x + to.x) / 2, my = (from.y + to.y) / 2;
      const dx = to.x - from.x, dy = to.y - from.y;
      const dist = Math.sqrt(dx*dx + dy*dy);
      const nx = -dy / dist * 12, ny = dx / dist * 12;
      drawArrow(from.x, from.y, to.x, to.y, to.r, color, width);
      ctx.font = '500 11px "JetBrains Mono"';
      ctx.fillStyle = isHl ? '#c4b5fd' : COLORS.edgeLabel;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(e.label, mx + nx, my + ny);
    }
  });

  dfaNodes.forEach(n => {
    const isHl = n.id === highlightNode;

    if (isHl || n.isStart || n.isAccept) {
      ctx.beginPath();
      ctx.arc(n.x, n.y, n.r + 8, 0, Math.PI * 2);
      ctx.fillStyle = isHl ? COLORS.highlightGlow : n.isStart ? COLORS.startGlow : COLORS.acceptGlow;
      ctx.fill();
    }

    ctx.beginPath();
    ctx.arc(n.x, n.y, n.r, 0, Math.PI * 2);
    const grad = ctx.createRadialGradient(n.x - 6, n.y - 6, 2, n.x, n.y, n.r);
    if (isHl) {
      grad.addColorStop(0, '#4c1d95'); grad.addColorStop(1, '#7c3aed');
    } else if (n.isStart) {
      grad.addColorStop(0, '#1e3a5f'); grad.addColorStop(1, COLORS.start);
    } else if (n.isAccept) {
      grad.addColorStop(0, '#14532d'); grad.addColorStop(1, COLORS.accept);
    } else {
      grad.addColorStop(0, '#1e293b'); grad.addColorStop(1, COLORS.node);
    }
    ctx.fillStyle = grad;
    ctx.fill();
    ctx.strokeStyle = isHl ? '#a78bfa' : n.isStart ? COLORS.startBorder : n.isAccept ? COLORS.acceptBorder : COLORS.nodeBorder;
    ctx.lineWidth = isHl ? 2.5 : n.isAccept ? 2.5 : 1.5;
    ctx.stroke();

    if (n.isAccept) {
      ctx.beginPath();
      ctx.arc(n.x, n.y, n.r - 4, 0, Math.PI * 2);
      ctx.strokeStyle = isHl ? '#a78bfa' : COLORS.acceptBorder;
      ctx.lineWidth = 1;
      ctx.stroke();
    }

    if (n.isStart) {
      const ax = n.x - n.r - 20, ay = n.y;
      ctx.beginPath();
      ctx.moveTo(ax - 14, ay);
      ctx.lineTo(ax, ay);
      ctx.strokeStyle = COLORS.startBorder;
      ctx.lineWidth = 2;
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(ax, ay);
      ctx.lineTo(ax - 7, ay - 5);
      ctx.lineTo(ax - 7, ay + 5);
      ctx.closePath();
      ctx.fillStyle = COLORS.startBorder;
      ctx.fill();
    }

    ctx.font = '600 13px "JetBrains Mono"';
    ctx.fillStyle = COLORS.text;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(n.label, n.x, n.y);
  });
}

function renderDFA(dfa) {
  currentDFA = dfa;
  resizeCanvas();
  dfaNodes = layoutNodes(dfa);
  dfaEdges = buildEdges(dfa);
  highlightNode = -1;
  highlightEdge = -1;
  drawDFA();
}

window.addEventListener('resize', () => { if (currentDFA) { resizeCanvas(); dfaNodes = layoutNodes(currentDFA); drawDFA(); } });

/* ── Simulate & Enumerate ────────────────────────── */
function simulateDFA(dfa, input) {
  const lo = dfa.extended ? 0 : 1;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const steps = [];
  let state = dfa.start;
  for (let i = 0; i < input.length; i++) {
    const ch = input[i];
    const cls = symbols.indexOf(ch);
    const row = dfa.transitions[state - lo];
    const next = (cls >= 0 && row) ? row[cls] : -1;
    const valid = next >= 0 && (!dfa.extended || next !== 0 || state === 0);
    steps.push({ char: ch, from: state, to: valid ? next : -1, valid });
    if (!valid) { state = -1; break; }
    state = next;
  }
  return { steps, finalState: state, accepted: state >= 0 && dfa.accept.includes(state) };
}

function enumerateDFA(dfa, maxLen) {
  const lo = dfa.extended ? 0 : 1;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const results = [];
  function rec(state, depth, buf) {
    if (dfa.accept.includes(state)) results.push(buf || 'ε');
    if (depth >= maxLen) return;
    const row = dfa.transitions[state - lo];
    if (!row) return;
    for (let c = 0; c < symbols.length; c++) {
      const next = row[c];
      if (dfa.extended && next === 0 && state !== 0) continue;
      rec(next, depth + 1, buf + symbols[c]);
    }
  }
  rec(dfa.start, 0, '');
  return results;
}

/* ── DFA Event Handlers ──────────────────────────── */
document.getElementById('btn-load-sample').addEventListener('click', () => {
  document.getElementById('dfa-json').value = JSON.stringify(SAMPLE_DFA, null, 2);
});

document.getElementById('dfa-file-input').addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = () => { document.getElementById('dfa-json').value = r.result; };
  r.readAsText(f);
});

document.getElementById('btn-render-dfa').addEventListener('click', () => {
  try { renderDFA(JSON.parse(document.getElementById('dfa-json').value)); }
  catch (e) { alert('Invalid JSON: ' + e.message); }
});

document.getElementById('btn-test-string').addEventListener('click', () => {
  if (!currentDFA) return;
  const input = document.getElementById('dfa-test-input').value;
  const res = simulateDFA(currentDFA, input);
  const el = document.getElementById('dfa-test-result');
  el.className = 'result-box ' + (res.accepted ? 'accept' : 'reject');
  el.textContent = `"${input}" → ${res.accepted ? 'ACCEPTED' : 'REJECTED'}  (final: ${res.finalState})`;

  const trace = document.getElementById('dfa-step-display');
  trace.innerHTML = res.steps.map((s, i) => {
    const last = i === res.steps.length - 1;
    const cls = !s.valid ? 'st-reject' : (last && res.accepted ? 'st-accept' : 'st-active');
    return `<span class="st st-active">${s.from}</span><span class="arrow"> —</span><span class="ch">${s.char}</span><span class="arrow">→ </span><span class="st ${cls}">${s.valid ? s.to : '✗'}</span>`;
  }).join('<span class="arrow">  </span>');
});

let animTimer = null;
document.getElementById('btn-animate').addEventListener('click', () => {
  if (!currentDFA) return;
  if (animTimer) { clearTimeout(animTimer); animTimer = null; }
  const input = document.getElementById('dfa-test-input').value;
  const res = simulateDFA(currentDFA, input);
  let i = 0;
  function step() {
    if (i >= res.steps.length) {
      const last = res.steps[res.steps.length - 1];
      highlightNode = last && last.valid ? last.to : -1;
      highlightEdge = -1;
      drawDFA();
      return;
    }
    const s = res.steps[i];
    highlightNode = s.from;
    highlightEdge = dfaEdges.findIndex(e => e.from === s.from && e.to === (s.valid ? s.to : -1));
    drawDFA();
    const trace = document.getElementById('dfa-step-display');
    trace.innerHTML = `Step ${i+1}: <span class="st st-active">${s.from}</span> <span class="arrow">—</span><span class="ch">${s.char}</span><span class="arrow">→</span> <span class="st ${s.valid ? 'st-active' : 'st-reject'}">${s.valid ? s.to : '✗'}</span>`;
    i++;
    if (s.valid) animTimer = setTimeout(step, 550);
  }
  step();
});

document.getElementById('btn-enumerate').addEventListener('click', () => {
  if (!currentDFA) return;
  const maxLen = parseInt(document.getElementById('dfa-enum-len').value) || 3;
  const results = enumerateDFA(currentDFA, maxLen);
  const el = document.getElementById('dfa-enum-result');
  el.innerHTML = results.map(s => `<span class="item">${s}</span>`).join('') +
    `<div class="total">${results.length} strings found</div>`;
});

/* ── Scanner ─────────────────────────────────────── */
const TOKEN_CLS = {
  INT:'tk-keyword',FLOAT_KW:'tk-keyword',VOID:'tk-keyword',IF:'tk-keyword',
  ELSE:'tk-keyword',WHILE:'tk-keyword',RETURN:'tk-keyword',INPUT:'tk-keyword',PRINT:'tk-keyword',
  ID:'tk-id',NUM:'tk-num',FLOAT:'tk-float',FLOAT_LIT:'tk-float',
  ADD:'tk-op',SUB:'tk-op',MUL:'tk-op',DIV:'tk-op',
  LT:'tk-op',LE:'tk-op',EQ:'tk-op',GT:'tk-op',GE:'tk-op',NE:'tk-op',
  AND:'tk-op',OR:'tk-op',NOT:'tk-op',ASG:'tk-op',AAS:'tk-op',AAA:'tk-op',
  LPAR:'tk-delim',RPAR:'tk-delim',LBK:'tk-delim',RBK:'tk-delim',
  LBR:'tk-delim',RBR:'tk-delim',CMA:'tk-delim',COL:'tk-delim',SCO:'tk-delim',DOT:'tk-delim',
  ERR:'tk-err'
};

let tokenData = { hand: null, table: null };

function esc(s) { return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

function renderTokens(tokens) {
  const tbody = document.querySelector('#token-table tbody');
  tbody.innerHTML = tokens.map((t, i) =>
    `<tr><td>${i+1}</td><td class="${TOKEN_CLS[t.kind]||''}">${t.kind}</td><td>${esc(t.lexeme)}</td><td>${t.line}:${t.col}</td></tr>`
  ).join('');

  const errs = tokens.filter(t => t.kind === 'ERR').length;
  document.getElementById('scan-stats').innerHTML =
    `<span class="n">${tokens.length}</span> tokens` + (errs ? ` · <span class="err">${errs} errors</span>` : '');

  const hl = document.getElementById('scan-highlighted');
  hl.innerHTML = tokens.map(t =>
    `<span class="${TOKEN_CLS[t.kind]||''}" title="${t.kind} @${t.line}:${t.col}">${esc(t.lexeme)}</span>`
  ).join(' ');
  hl.classList.add('visible');
}

function loadTokens(tokens, mode) {
  tokenData[mode] = tokens;
  if (document.getElementById('scan-mode').value === mode) renderTokens(tokens);
}

document.getElementById('btn-scan-sample').addEventListener('click', () => {
  document.getElementById('scan-source').value = SAMPLE_SOURCE;
  loadTokens(SAMPLE_TOKENS, 'hand');
  loadTokens(SAMPLE_TOKENS, 'table');
});

document.getElementById('scan-file-input').addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = () => {
    try {
      const tokens = JSON.parse(r.result);
      loadTokens(tokens, document.getElementById('scan-mode').value);
    } catch (err) { alert('Invalid JSON: ' + err.message); }
  };
  r.readAsText(f);
});

document.getElementById('scan-mode').addEventListener('change', () => {
  const t = tokenData[document.getElementById('scan-mode').value];
  if (t) renderTokens(t);
});

/* ── Init ────────────────────────────────────────── */
document.getElementById('dfa-json').value = JSON.stringify(SAMPLE_DFA, null, 2);
requestAnimationFrame(() => renderDFA(SAMPLE_DFA));
