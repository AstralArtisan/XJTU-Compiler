/* ═══════════════════════════════════════════════════
   XJTU Compiler Visualizer
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

/* ── DFA Form / JSON mode toggle ─────────────────── */
const dfaForm = document.getElementById('dfa-form');
const dfaJson = document.getElementById('dfa-json');
const btnForm = document.getElementById('btn-mode-form');
const btnJson = document.getElementById('btn-mode-json');

btnForm.addEventListener('click', () => {
  dfaForm.classList.remove('hidden');
  dfaJson.classList.add('hidden');
  btnForm.setAttribute('aria-pressed', 'true');
  btnForm.classList.add('active-toggle');
  btnJson.setAttribute('aria-pressed', 'false');
  btnJson.classList.remove('active-toggle');
});
btnJson.addEventListener('click', () => {
  dfaForm.classList.add('hidden');
  dfaJson.classList.remove('hidden');
  btnJson.setAttribute('aria-pressed', 'true');
  btnJson.classList.add('active-toggle');
  btnForm.setAttribute('aria-pressed', 'false');
  btnForm.classList.remove('active-toggle');
  syncFormToJson();
});

function syncFormToJson() {
  const alpha = document.getElementById('df-alphabet').value.trim();
  const states = parseInt(document.getElementById('df-states').value) || 3;
  const start = parseInt(document.getElementById('df-start').value) || 1;
  const accept = document.getElementById('df-accept').value.trim().split(/[\s,]+/).map(Number).filter(n => !isNaN(n));
  const lines = document.getElementById('df-trans').value.trim().split('\n').filter(l => l.trim());
  const trans = lines.map(l => l.trim().split(/\s+/).map(Number));
  dfaJson.value = JSON.stringify({ extended: false, states, start, accept, accept_labels: {}, alphabet: alpha, transitions: trans, keywords: {} }, null, 2);
}

function syncJsonToForm(dfa) {
  document.getElementById('df-alphabet').value = dfa.alphabet || '';
  document.getElementById('df-states').value = dfa.states || 3;
  document.getElementById('df-start').value = dfa.start || 1;
  document.getElementById('df-accept').value = (dfa.accept || []).join(' ');
  document.getElementById('df-trans').value = (dfa.transitions || []).map(r => r.join(' ')).join('\n');
}

function getDFA() {
  if (!dfaForm.classList.contains('hidden')) {
    syncFormToJson();
  }
  return JSON.parse(dfaJson.value);
}

/* ── DFA Canvas ──────────────────────────────────── */
let currentDFA = null;
let dfaNodes = [];
let dfaEdges = [];
let highlightNode = -1;
let highlightEdge = -1;
const canvas = document.getElementById('dfa-canvas');
const ctx = canvas.getContext('2d');

const C = {
  node: '#1e293b', nodeBorder: '#334155',
  start: '#1d4ed8', startBorder: '#60a5fa', startGlow: 'rgba(96,165,250,.25)',
  accept: '#166534', acceptBorder: '#4ade80', acceptGlow: 'rgba(74,222,128,.2)',
  hl: '#7c3aed', hlGlow: 'rgba(167,139,250,.3)',
  edge: '#475569', edgeLabel: '#94a3b8',
  text: '#f1f5f9'
};

function resizeCanvas() {
  const wrap = canvas.parentElement;
  const w = wrap.clientWidth;
  const h = wrap.clientHeight;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = w * dpr;
  canvas.height = h * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function layoutNodes(dfa) {
  const lo = dfa.extended ? 0 : 1;
  const hi = dfa.extended ? dfa.states - 1 : dfa.states;
  const count = hi - lo + 1;
  const wrap = canvas.parentElement;
  const W = wrap.clientWidth, H = wrap.clientHeight;
  const cx = W / 2, cy = H / 2;
  const R = Math.min(W, H) * 0.34;
  const nodes = [];
  for (let i = 0; i < count; i++) {
    const s = lo + i;
    const angle = -Math.PI / 2 + (2 * Math.PI * i) / count;
    nodes.push({
      id: s, x: cx + R * Math.cos(angle), y: cy + R * Math.sin(angle), r: 26,
      isStart: s === dfa.start, isAccept: dfa.accept.includes(s),
      label: dfa.accept_labels && dfa.accept_labels[s] ? s + '(' + dfa.accept_labels[s] + ')' : '' + s
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
      const key = s + '->' + t;
      if (!map[key]) map[key] = [];
      map[key].push(symbols[c] || '' + c);
    }
  }
  return Object.entries(map).map(([key, syms]) => {
    const parts = key.split('->');
    return { from: +parts[0], to: +parts[1], label: syms.length > 4 ? syms.slice(0,3).join(',') + '…' : syms.join(',') };
  });
}

function getNode(id) { return dfaNodes.find(n => n.id === id); }

function drawArrow(x1, y1, x2, y2, r1, r2, color, width) {
  const dx = x2 - x1, dy = y2 - y1;
  const dist = Math.sqrt(dx * dx + dy * dy);
  if (dist < 1) return;
  const ux = dx / dist, uy = dy / dist;
  const sx = x1 + ux * r1, sy = y1 + uy * r1;
  const ex = x2 - ux * (r2 + 5), ey = y2 - uy * (r2 + 5);
  ctx.beginPath(); ctx.moveTo(sx, sy); ctx.lineTo(ex, ey);
  ctx.strokeStyle = color; ctx.lineWidth = width; ctx.stroke();
  ctx.beginPath(); ctx.moveTo(ex + ux * 10, ey + uy * 10);
  ctx.lineTo(ex - ux * 2 + uy * 5, ey - uy * 2 - ux * 5);
  ctx.lineTo(ex - ux * 2 - uy * 5, ey - uy * 2 + ux * 5);
  ctx.closePath(); ctx.fillStyle = color; ctx.fill();
}

function drawSelfLoop(x, y, r, color, width) {
  const lr = 18, cx = x, cy = y - r - lr;
  ctx.beginPath(); ctx.arc(cx, cy, lr, 0.3, Math.PI * 2 - 0.3);
  ctx.strokeStyle = color; ctx.lineWidth = width; ctx.stroke();
  const a = -0.3, ax = cx + lr * Math.cos(a), ay = cy + lr * Math.sin(a);
  ctx.beginPath(); ctx.moveTo(ax, ay);
  ctx.lineTo(ax + 4, ay + 8); ctx.lineTo(ax - 5, ay + 6);
  ctx.closePath(); ctx.fillStyle = color; ctx.fill();
}

/* ── Draw DFA ─────────────────────────────────────── */
function drawDFA() {
  const W = canvas.parentElement.clientWidth, H = canvas.parentElement.clientHeight;
  ctx.clearRect(0, 0, W, H);

  dfaEdges.forEach((e, i) => {
    const from = getNode(e.from), to = getNode(e.to);
    if (!from || !to) return;
    const isHl = i === highlightEdge;
    const color = isHl ? '#a78bfa' : C.edge;
    const w = isHl ? 2.5 : 1.5;
    if (e.from === e.to) {
      drawSelfLoop(from.x, from.y, from.r, color, w);
      ctx.font = '500 11px "JetBrains Mono", monospace';
      ctx.fillStyle = isHl ? '#c4b5fd' : C.edgeLabel;
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(e.label, from.x, from.y - from.r - 40);
    } else {
      drawArrow(from.x, from.y, to.x, to.y, from.r, to.r, color, w);
      const mx = (from.x + to.x) / 2, my = (from.y + to.y) / 2;
      const dx = to.x - from.x, dy = to.y - from.y;
      const dist = Math.sqrt(dx * dx + dy * dy) || 1;
      ctx.font = '500 11px "JetBrains Mono", monospace';
      ctx.fillStyle = isHl ? '#c4b5fd' : C.edgeLabel;
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(e.label, mx + (-dy / dist) * 14, my + (dx / dist) * 14);
    }
  });

  dfaNodes.forEach(n => {
    const isHl = n.id === highlightNode;
    if (isHl || n.isStart || n.isAccept) {
      ctx.beginPath(); ctx.arc(n.x, n.y, n.r + 8, 0, Math.PI * 2);
      ctx.fillStyle = isHl ? C.hlGlow : n.isStart ? C.startGlow : C.acceptGlow;
      ctx.fill();
    }
    ctx.beginPath(); ctx.arc(n.x, n.y, n.r, 0, Math.PI * 2);
    const g = ctx.createRadialGradient(n.x - 6, n.y - 6, 2, n.x, n.y, n.r);
    if (isHl) { g.addColorStop(0, '#4c1d95'); g.addColorStop(1, '#7c3aed'); }
    else if (n.isStart) { g.addColorStop(0, '#1e3a5f'); g.addColorStop(1, C.start); }
    else if (n.isAccept) { g.addColorStop(0, '#14532d'); g.addColorStop(1, C.accept); }
    else { g.addColorStop(0, '#1e293b'); g.addColorStop(1, C.node); }
    ctx.fillStyle = g; ctx.fill();
    ctx.strokeStyle = isHl ? '#a78bfa' : n.isStart ? C.startBorder : n.isAccept ? C.acceptBorder : C.nodeBorder;
    ctx.lineWidth = isHl || n.isAccept ? 2.5 : 1.5;
    ctx.stroke();
    if (n.isAccept) {
      ctx.beginPath(); ctx.arc(n.x, n.y, n.r - 4, 0, Math.PI * 2);
      ctx.strokeStyle = isHl ? '#a78bfa' : C.acceptBorder; ctx.lineWidth = 1; ctx.stroke();
    }
    if (n.isStart) {
      const ax = n.x - n.r - 20;
      ctx.beginPath(); ctx.moveTo(ax - 14, n.y); ctx.lineTo(ax, n.y);
      ctx.strokeStyle = C.startBorder; ctx.lineWidth = 2; ctx.stroke();
      ctx.beginPath(); ctx.moveTo(ax, n.y); ctx.lineTo(ax - 7, n.y - 5); ctx.lineTo(ax - 7, n.y + 5);
      ctx.closePath(); ctx.fillStyle = C.startBorder; ctx.fill();
    }
    ctx.font = '600 13px "JetBrains Mono", monospace';
    ctx.fillStyle = C.text; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText(n.label, n.x, n.y);
  });
}

function renderDFA(dfa) {
  currentDFA = dfa;
  resizeCanvas();
  dfaNodes = layoutNodes(dfa);
  dfaEdges = buildEdges(dfa);
  highlightNode = -1; highlightEdge = -1;
  drawDFA();
}

window.addEventListener('resize', () => {
  if (currentDFA) { resizeCanvas(); dfaNodes = layoutNodes(currentDFA); drawDFA(); }
});

/* ── DFA Simulate & Enumerate ────────────────────── */
function simulateDFA(dfa, input) {
  const lo = dfa.extended ? 0 : 1;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const steps = []; let state = dfa.start;
  for (let i = 0; i < input.length; i++) {
    const cls = symbols.indexOf(input[i]);
    const row = dfa.transitions[state - lo];
    const next = (cls >= 0 && row) ? row[cls] : -1;
    const valid = next >= 0 && (!dfa.extended || next !== 0 || state === 0);
    steps.push({ char: input[i], from: state, to: valid ? next : -1, valid });
    if (!valid) { state = -1; break; }
    state = next;
  }
  return { steps, finalState: state, accepted: state >= 0 && dfa.accept.includes(state) };
}

function enumerateDFA(dfa, maxLen) {
  const lo = dfa.extended ? 0 : 1;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const results = [];
  (function rec(state, depth, buf) {
    if (dfa.accept.includes(state)) results.push(buf || 'ε');
    if (depth >= maxLen) return;
    const row = dfa.transitions[state - lo]; if (!row) return;
    for (let c = 0; c < symbols.length; c++) {
      const next = row[c];
      if (dfa.extended && next === 0 && state !== 0) continue;
      rec(next, depth + 1, buf + symbols[c]);
    }
  })(dfa.start, 0, '');
  return results;
}

/* ── DFA Event Handlers ──────────────────────────── */
document.getElementById('btn-load-sample').addEventListener('click', () => {
  syncJsonToForm(SAMPLE_DFA);
  dfaJson.value = JSON.stringify(SAMPLE_DFA, null, 2);
  renderDFA(SAMPLE_DFA);
});

document.getElementById('btn-render-dfa').addEventListener('click', () => {
  try { renderDFA(getDFA()); }
  catch (e) { alert('Invalid DFA: ' + e.message); }
});

document.getElementById('dfa-file-input')?.addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = () => { dfaJson.value = r.result; try { syncJsonToForm(JSON.parse(r.result)); } catch(_){} };
  r.readAsText(f);
});

document.getElementById('btn-test-string').addEventListener('click', () => {
  if (!currentDFA) return;
  const input = document.getElementById('dfa-test-input').value;
  const res = simulateDFA(currentDFA, input);
  const el = document.getElementById('dfa-test-result');
  el.className = 'result-box ' + (res.accepted ? 'accept' : 'reject');
  el.textContent = '"' + input + '" → ' + (res.accepted ? 'ACCEPTED' : 'REJECTED') + '  (final: ' + res.finalState + ')';
  const trace = document.getElementById('dfa-step-display');
  trace.innerHTML = res.steps.map((s, i) => {
    const last = i === res.steps.length - 1;
    const cls = !s.valid ? 'st-reject' : (last && res.accepted ? 'st-accept' : 'st-active');
    return '<span class="st st-active">' + s.from + '</span><span class="arrow"> —</span><span class="ch">' + s.char + '</span><span class="arrow">→ </span><span class="st ' + cls + '">' + (s.valid ? s.to : '✗') + '</span>';
  }).join('<span class="arrow">  </span>');
});

let animTimer = null;
document.getElementById('btn-animate').addEventListener('click', () => {
  if (!currentDFA) return;
  if (animTimer) { clearTimeout(animTimer); animTimer = null; }
  const input = document.getElementById('dfa-test-input').value;
  const res = simulateDFA(currentDFA, input);
  let i = 0;
  (function step() {
    if (i >= res.steps.length) {
      const last = res.steps[res.steps.length - 1];
      highlightNode = last && last.valid ? last.to : -1;
      highlightEdge = -1; drawDFA(); return;
    }
    const s = res.steps[i];
    highlightNode = s.from;
    highlightEdge = dfaEdges.findIndex(e => e.from === s.from && e.to === (s.valid ? s.to : -1));
    drawDFA();
    document.getElementById('dfa-step-display').innerHTML =
      'Step ' + (i+1) + ': <span class="st st-active">' + s.from + '</span> <span class="arrow">—</span><span class="ch">' + s.char + '</span><span class="arrow">→</span> <span class="st ' + (s.valid ? 'st-active' : 'st-reject') + '">' + (s.valid ? s.to : '✗') + '</span>';
    i++;
    if (s.valid) animTimer = setTimeout(step, 550);
  })();
});

document.getElementById('btn-enumerate').addEventListener('click', () => {
  if (!currentDFA) return;
  const maxLen = parseInt(document.getElementById('dfa-enum-len').value) || 3;
  const results = enumerateDFA(currentDFA, maxLen);
  document.getElementById('dfa-enum-result').innerHTML =
    results.map(s => '<span class="item">' + s + '</span>').join('') +
    '<div class="total">' + results.length + ' strings found</div>';
});

/* ── Frontend Tokenizer (JS port of scanner.c) ──── */
const KEYWORDS = { int:'INT', float:'FLOAT', void:'VOID', 'if':'IF', 'else':'ELSE', 'while':'WHILE', 'return':'RETURN', input:'INPUT', print:'PRINT' };

function tokenize(src) {
  const tokens = []; let pos = 0, line = 1, col = 1;
  function peek(k) { const c = src.charCodeAt(pos + (k||0)); return isNaN(c) ? -1 : c; }
  function adv() { const c = peek(); if (c < 0) return -1; pos++; if (c === 10) { line++; col = 1; } else col++; return c; }
  function isAlpha(c) { return (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c === 95; }
  function isDigit(c) { return c >= 48 && c <= 57; }
  function skipWS() {
    while (true) {
      const c = peek();
      if (c === 32 || c === 9 || c === 13 || c === 10) { adv(); continue; }
      if (c === 47 && peek(1) === 47) { while (peek() >= 0 && peek() !== 10) adv(); continue; }
      if (c === 47 && peek(1) === 42) { adv(); adv(); while (peek() >= 0 && !(peek() === 42 && peek(1) === 47)) adv(); if (peek() >= 0) { adv(); adv(); } continue; }
      break;
    }
  }
  while (true) {
    skipWS();
    const sl = line, sc = col, sp = pos;
    const c = peek();
    if (c < 0) break;
    if (isAlpha(c)) {
      while (isAlpha(peek()) || isDigit(peek())) adv();
      const lex = src.slice(sp, pos);
      tokens.push({ kind: KEYWORDS[lex] || 'ID', lexeme: lex, line: sl, col: sc });
    } else if (isDigit(c) || (c === 46 && isDigit(peek(1)))) {
      if (c === 46) adv();
      while (isDigit(peek())) adv();
      let isFloat = c === 46;
      if (c !== 46 && peek() === 46) { isFloat = true; adv(); while (isDigit(peek())) adv(); }
      const e = peek(); if (e === 101 || e === 69) { const save = pos; adv(); if (peek()===43||peek()===45) adv(); if (isDigit(peek())) { isFloat = true; while (isDigit(peek())) adv(); } else pos = save; }
      tokens.push({ kind: isFloat ? 'FLOAT_LIT' : 'NUM', lexeme: src.slice(sp, pos), line: sl, col: sc });
    } else {
      adv();
      const n = peek(); let kind = 'ERR';
      switch (c) {
        case 43: if (n===43){adv();kind='AAA';}else if(n===61){adv();kind='AAS';}else kind='ADD'; break;
        case 45: kind='SUB'; break; case 42: kind='MUL'; break; case 47: kind='DIV'; break;
        case 60: if(n===61){adv();kind='LE';}else kind='LT'; break;
        case 62: if(n===61){adv();kind='GE';}else kind='GT'; break;
        case 61: if(n===61){adv();kind='EQ';}else kind='ASG'; break;
        case 33: if(n===61){adv();kind='NE';}else kind='NOT'; break;
        case 38: if(n===38){adv();kind='AND';} break;
        case 124: if(n===124){adv();kind='OR';} break;
        case 40: kind='LPAR'; break; case 41: kind='RPAR'; break;
        case 91: kind='LBK'; break; case 93: kind='RBK'; break;
        case 123: kind='LBR'; break; case 125: kind='RBR'; break;
        case 44: kind='CMA'; break; case 58: kind='COL'; break;
        case 59: kind='SCO'; break; case 46: kind='DOT'; break;
      }
      tokens.push({ kind, lexeme: src.slice(sp, pos), line: sl, col: sc });
    }
  }
  return tokens;
}

/* ── Scanner UI ──────────────────────────────────── */
const TOKEN_CLS = {
  INT:'tk-keyword',FLOAT:'tk-keyword',VOID:'tk-keyword',IF:'tk-keyword',
  ELSE:'tk-keyword',WHILE:'tk-keyword',RETURN:'tk-keyword',INPUT:'tk-keyword',PRINT:'tk-keyword',
  ID:'tk-id',NUM:'tk-num',FLOAT_LIT:'tk-float',
  ADD:'tk-op',SUB:'tk-op',MUL:'tk-op',DIV:'tk-op',
  LT:'tk-op',LE:'tk-op',EQ:'tk-op',GT:'tk-op',GE:'tk-op',NE:'tk-op',
  AND:'tk-op',OR:'tk-op',NOT:'tk-op',ASG:'tk-op',AAS:'tk-op',AAA:'tk-op',
  LPAR:'tk-delim',RPAR:'tk-delim',LBK:'tk-delim',RBK:'tk-delim',
  LBR:'tk-delim',RBR:'tk-delim',CMA:'tk-delim',COL:'tk-delim',SCO:'tk-delim',DOT:'tk-delim',
  ERR:'tk-err'
};

function esc(s) { return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); }

function renderTokens(tokens) {
  document.querySelector('#token-table tbody').innerHTML = tokens.map((t, i) =>
    '<tr><td>' + (i+1) + '</td><td class="' + (TOKEN_CLS[t.kind]||'') + '">' + t.kind + '</td><td>' + esc(t.lexeme) + '</td><td>' + t.line + ':' + t.col + '</td></tr>'
  ).join('');
  const errs = tokens.filter(t => t.kind === 'ERR').length;
  document.getElementById('scan-stats').innerHTML =
    '<span class="n">' + tokens.length + '</span> tokens' + (errs ? ' · <span class="err">' + errs + ' errors</span>' : '');
  const hl = document.getElementById('scan-highlighted');
  hl.innerHTML = tokens.map(t =>
    '<span class="' + (TOKEN_CLS[t.kind]||'') + '" title="' + t.kind + ' @' + t.line + ':' + t.col + '">' + esc(t.lexeme) + '</span>'
  ).join(' ');
  hl.classList.add('visible');
}

document.getElementById('btn-scan-sample').addEventListener('click', () => {
  document.getElementById('scan-source').value = SAMPLE_SOURCE;
  renderTokens(tokenize(SAMPLE_SOURCE));
});

document.getElementById('btn-scan-run').addEventListener('click', () => {
  const src = document.getElementById('scan-source').value;
  if (!src.trim()) return;
  renderTokens(tokenize(src));
});

document.getElementById('scan-file-input').addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = () => { try { renderTokens(JSON.parse(r.result)); } catch (err) { alert('Invalid JSON: ' + err.message); } };
  r.readAsText(f);
});

/* ── Init ────────────────────────────────────────── */
syncJsonToForm(SAMPLE_DFA);
dfaJson.value = JSON.stringify(SAMPLE_DFA, null, 2);
requestAnimationFrame(() => renderDFA(SAMPLE_DFA));
