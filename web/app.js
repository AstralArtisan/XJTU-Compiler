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

const SAMPLE_TOKENS = [
  {kind:"INT",lexeme:"int",line:1,col:1},
  {kind:"ID",lexeme:"gcd",line:1,col:5},
  {kind:"LPAR",lexeme:"(",line:1,col:8},
  {kind:"INT",lexeme:"int",line:1,col:9},
  {kind:"ID",lexeme:"a",line:1,col:13},
  {kind:"CMA",lexeme:",",line:1,col:14},
  {kind:"INT",lexeme:"int",line:1,col:16},
  {kind:"ID",lexeme:"b",line:1,col:20},
  {kind:"RPAR",lexeme:")",line:1,col:21},
  {kind:"LBR",lexeme:"{",line:1,col:23},
  {kind:"WHILE",lexeme:"while",line:2,col:5},
  {kind:"LPAR",lexeme:"(",line:2,col:11},
  {kind:"ID",lexeme:"b",line:2,col:12},
  {kind:"NE",lexeme:"!=",line:2,col:14},
  {kind:"NUM",lexeme:"0",line:2,col:17},
  {kind:"RPAR",lexeme:")",line:2,col:18},
  {kind:"LBR",lexeme:"{",line:2,col:20},
  {kind:"INT",lexeme:"int",line:3,col:9},
  {kind:"ID",lexeme:"t",line:3,col:13},
  {kind:"ASG",lexeme:"=",line:3,col:15},
  {kind:"ID",lexeme:"b",line:3,col:17},
  {kind:"SCO",lexeme:";",line:3,col:18},
  {kind:"RBR",lexeme:"}",line:4,col:5},
  {kind:"RETURN",lexeme:"return",line:5,col:5},
  {kind:"ID",lexeme:"a",line:5,col:12},
  {kind:"SCO",lexeme:";",line:5,col:13},
  {kind:"RBR",lexeme:"}",line:6,col:1}
];

const SAMPLE_SOURCE = `int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
    }
    return a;
}`;

/* ── DFA State ───────────────────────────────────── */
let currentDFA = null;
let network = null;

/* ── DFA: Render Graph ───────────────────────────── */
function renderDFA(dfa) {
  currentDFA = dfa;
  const nodes = [];
  const edges = [];
  const lo = dfa.extended ? 0 : 1;
  const hi = dfa.extended ? dfa.states - 1 : dfa.states;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');

  for (let s = lo; s <= hi; s++) {
    const isAccept = dfa.accept.includes(s);
    const isStart = s === dfa.start;
    const label = dfa.accept_labels && dfa.accept_labels[s]
      ? `${s}\n(${dfa.accept_labels[s]})` : `${s}`;
    nodes.push({
      id: s, label,
      shape: isAccept ? 'box' : 'circle',
      color: {
        background: isStart ? '#1f6feb' : isAccept ? '#238636' : '#2d333b',
        border: isStart ? '#58a6ff' : isAccept ? '#3fb950' : '#30363d',
        highlight: { background: '#58a6ff', border: '#79c0ff' }
      },
      font: { color: '#e6edf3', size: 13, face: 'Consolas, monospace' },
      borderWidth: isAccept ? 3 : 1,
      size: 25
    });
  }

  const edgeMap = {};
  for (let s = lo; s <= hi; s++) {
    const row = dfa.transitions[s - lo];
    if (!row) continue;
    for (let c = 0; c < row.length; c++) {
      const t = row[c];
      if (dfa.extended && t === 0 && s !== 0) continue;
      const key = `${s}->${t}`;
      const sym = symbols[c] || `${c}`;
      if (edgeMap[key]) edgeMap[key].push(sym);
      else edgeMap[key] = [sym];
    }
  }

  for (const [key, syms] of Object.entries(edgeMap)) {
    const [from, to] = key.split('->').map(Number);
    const label = syms.length > 3 ? syms.slice(0, 3).join(',') + '...' : syms.join(',');
    edges.push({
      from, to, label,
      arrows: 'to',
      color: { color: '#6e7681', highlight: '#58a6ff' },
      font: { color: '#8b949e', size: 11, face: 'Consolas, monospace', strokeWidth: 0 },
      smooth: from === to ? { type: 'curvedCW', roundness: 0.6 } : { type: 'curvedCW', roundness: 0.15 }
    });
  }

  const container = document.getElementById('dfa-graph');
  const data = { nodes: new vis.DataSet(nodes), edges: new vis.DataSet(edges) };
  const options = {
    physics: { solver: 'forceAtlas2Based', forceAtlas2Based: { gravitationalConstant: -60, springLength: 120 } },
    interaction: { hover: true, zoomView: true, dragView: true },
    layout: { improvedLayout: true }
  };
  network = new vis.Network(container, data, options);
}

/* ── DFA: Simulate ───────────────────────────────── */
function simulateDFA(dfa, input) {
  const lo = dfa.extended ? 0 : 1;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const steps = [];
  let state = dfa.start;

  for (let i = 0; i < input.length; i++) {
    const ch = input[i];
    let cls = -1;
    if (!dfa.extended) {
      cls = symbols.indexOf(ch);
    } else {
      cls = -1; // extended mode needs char_to_class which we don't have in JSON
    }
    const row = dfa.transitions[state - lo];
    const next = (cls >= 0 && row) ? row[cls] : -1;
    const valid = dfa.extended ? (next !== 0 || state === 0) && next >= 0 : next >= 0;
    steps.push({ char: ch, from: state, to: valid ? next : -1, valid });
    if (!valid) { state = -1; break; }
    state = next;
  }

  const accepted = state >= 0 && dfa.accept.includes(state);
  return { steps, finalState: state, accepted };
}

/* ── DFA: Enumerate ──────────────────────────────── */
function enumerateDFA(dfa, maxLen) {
  const lo = dfa.extended ? 0 : 1;
  const symbols = dfa.extended ? (dfa.classes || []) : (dfa.alphabet || '').split('');
  const results = [];

  function rec(state, depth, buf) {
    if (dfa.accept.includes(state)) results.push(buf || '<ε>');
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

document.getElementById('dfa-file-input').addEventListener('change', (e) => {
  const file = e.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => { document.getElementById('dfa-json').value = reader.result; };
  reader.readAsText(file);
});

document.getElementById('btn-render-dfa').addEventListener('click', () => {
  try {
    const dfa = JSON.parse(document.getElementById('dfa-json').value);
    renderDFA(dfa);
  } catch (e) {
    alert('Invalid JSON: ' + e.message);
  }
});

document.getElementById('btn-test-string').addEventListener('click', () => {
  if (!currentDFA) { alert('Please render a DFA first.'); return; }
  const input = document.getElementById('dfa-test-input').value;
  const result = simulateDFA(currentDFA, input);
  const el = document.getElementById('dfa-test-result');
  el.className = 'test-result ' + (result.accepted ? 'accept' : 'reject');
  el.textContent = `"${input}" → ${result.accepted ? 'ACCEPTED' : 'REJECTED'} (final state: ${result.finalState})`;

  const stepEl = document.getElementById('dfa-step-display');
  stepEl.innerHTML = result.steps.map((s, i) =>
    `<span class="${s.valid ? 'state-active' : 'state-reject'}">${s.from}</span>` +
    ` —<span class="char-current">${s.char}</span>→ ` +
    `<span class="${s.valid ? (i === result.steps.length - 1 && result.accepted ? 'state-accept' : 'state-active') : 'state-reject'}">${s.valid ? s.to : '✗'}</span>`
  ).join('  ');
});

document.getElementById('btn-animate').addEventListener('click', () => {
  if (!currentDFA || !network) { alert('Please render a DFA first.'); return; }
  const input = document.getElementById('dfa-test-input').value;
  const result = simulateDFA(currentDFA, input);
  let i = 0;

  function step() {
    if (i > 0) network.unselectAll();
    if (i >= result.steps.length) {
      const final = result.steps.length > 0 ? result.steps[result.steps.length - 1] : null;
      if (final && final.valid) {
        network.selectNodes([final.to]);
      }
      return;
    }
    const s = result.steps[i];
    network.selectNodes([s.from]);
    const stepEl = document.getElementById('dfa-step-display');
    stepEl.innerHTML = `Step ${i + 1}: state <span class="state-active">${s.from}</span> —<span class="char-current">${s.char}</span>→ <span class="${s.valid ? 'state-active' : 'state-reject'}">${s.valid ? s.to : '✗'}</span>`;
    i++;
    if (s.valid) setTimeout(step, 600);
  }
  step();
});

document.getElementById('btn-enumerate').addEventListener('click', () => {
  if (!currentDFA) { alert('Please render a DFA first.'); return; }
  const maxLen = parseInt(document.getElementById('dfa-enum-len').value) || 3;
  const results = enumerateDFA(currentDFA, maxLen);
  const el = document.getElementById('dfa-enum-result');
  el.innerHTML = results.map(s => `<div class="enum-item">${s}</div>`).join('') +
    `<div class="enum-total">Total: ${results.length} strings</div>`;
});

/* ── Scanner Event Handlers ──────────────────────── */

const TOKEN_COLORS = {
  INT:'tk-keyword', FLOAT_KW:'tk-keyword', VOID:'tk-keyword', IF:'tk-keyword',
  ELSE:'tk-keyword', WHILE:'tk-keyword', RETURN:'tk-keyword', INPUT:'tk-keyword', PRINT:'tk-keyword',
  ID:'tk-id', NUM:'tk-num', FLOAT:'tk-float', FLOAT_LIT:'tk-float',
  ADD:'tk-op', SUB:'tk-op', MUL:'tk-op', DIV:'tk-op',
  LT:'tk-op', LE:'tk-op', EQ:'tk-op', GT:'tk-op', GE:'tk-op', NE:'tk-op',
  AND:'tk-op', OR:'tk-op', NOT:'tk-op',
  ASG:'tk-op', AAS:'tk-op', AAA:'tk-op',
  LPAR:'tk-delim', RPAR:'tk-delim', LBK:'tk-delim', RBK:'tk-delim',
  LBR:'tk-delim', RBR:'tk-delim', CMA:'tk-delim', COL:'tk-delim', SCO:'tk-delim', DOT:'tk-delim',
  ERR:'tk-err'
};

let currentTokens = { hand: null, table: null };

function renderTokenTable(tokens) {
  const tbody = document.querySelector('#token-table tbody');
  tbody.innerHTML = tokens.map((t, i) => {
    const cls = TOKEN_COLORS[t.kind] || '';
    return `<tr><td>${i + 1}</td><td class="${cls}">${t.kind}</td><td>${escapeHtml(t.lexeme)}</td><td>${t.line}</td><td>${t.col}</td></tr>`;
  }).join('');

  const stats = document.getElementById('scan-stats');
  const errors = tokens.filter(t => t.kind === 'ERR').length;
  stats.innerHTML = `<span class="stat-count">${tokens.length}</span> tokens` +
    (errors > 0 ? `, <span class="stat-error">${errors} errors</span>` : '');
}

function renderHighlightedSource(tokens) {
  const el = document.getElementById('scan-highlighted');
  el.innerHTML = tokens.map(t => {
    const cls = TOKEN_COLORS[t.kind] || '';
    return `<span class="${cls}" title="${t.kind} @${t.line}:${t.col}">${escapeHtml(t.lexeme)}</span>`;
  }).join(' ');
  el.classList.add('visible');
}

function escapeHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function loadTokens(tokens, mode) {
  currentTokens[mode] = tokens;
  const activeMode = document.getElementById('scan-mode').value;
  if (activeMode === mode) {
    renderTokenTable(tokens);
    renderHighlightedSource(tokens);
  }
}

document.getElementById('btn-scan-sample').addEventListener('click', () => {
  document.getElementById('scan-source').value = SAMPLE_SOURCE;
  loadTokens(SAMPLE_TOKENS, 'hand');
  loadTokens(SAMPLE_TOKENS, 'table');
});

document.getElementById('scan-file-input').addEventListener('change', (e) => {
  const file = e.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const tokens = JSON.parse(reader.result);
      const mode = document.getElementById('scan-mode').value;
      loadTokens(tokens, mode);
    } catch (err) {
      alert('Invalid JSON: ' + err.message);
    }
  };
  reader.readAsText(file);
});

document.getElementById('scan-mode').addEventListener('change', () => {
  const mode = document.getElementById('scan-mode').value;
  const tokens = currentTokens[mode];
  if (tokens) {
    renderTokenTable(tokens);
    renderHighlightedSource(tokens);
  }
});

/* ── Init: load sample DFA on page load ──────────── */
document.getElementById('dfa-json').value = JSON.stringify(SAMPLE_DFA, null, 2);
renderDFA(SAMPLE_DFA);