/* ═══════════════════════════════════════════════════
   Lab 4 — SLR(1) Builder
   FIRST/FOLLOW + ACTION/GOTO tables, conflict reporting
   Depends on: shared.js, lr0.js
     (uses lr0ParseGrammar / lr0Closure / lr0GotoLocal / lr0ItemsEqual
      and SAMPLE_GRAMMAR / AMBIG_GRAMMAR)
   ═══════════════════════════════════════════════════ */

const SLR_DEMO_GRAMMAR = `# SLR(1) 用 FOLLOW 集消解归约-归约冲突
# LR(0)：状态 I4 同时含 A -> c · 和 B -> c ·，必然 reduce-reduce 冲突
# SLR(1)：FOLLOW(A)={a}, FOLLOW(B)={b}，按输入分流，冲突消解
%start S
%terminals a b c

S -> A a | B b
A -> c
B -> c
`;

const SLR_DANGLING_GRAMMAR = `# 悬挂 else：经典 shift-reduce，SLR(1) 也无法消解
%start S
%terminals IF THEN ELSE a

S -> IF E THEN S | IF E THEN S ELSE S | a
E -> a
`;

/* ── 本地 SLR(1) 构建（API 离线时使用） ───────────── */
function slrBuildLocal(grammarText) {
  const grammar = lr0ParseGrammar(grammarText);
  const symbols = grammar.symbols;
  const productions = grammar.productions;
  const isNT = s => symbols[s].kind === 'nonterm';
  const isT  = s => symbols[s].kind === 'term' || symbols[s].kind === 'end';

  /* LR(0) 项目集（复用 lr0Closure / lr0GotoLocal） */
  const stateItems = [lr0Closure(grammar, [{ prod: 0, dot: 0 }])];
  const edges = [];
  const work = [0];
  while (work.length) {
    const from = work.shift();
    const seenSyms = [];
    stateItems[from].forEach(it => {
      const sym = productions[it.prod].rhs[it.dot];
      if (sym !== undefined && !seenSyms.includes(sym)) seenSyms.push(sym);
    });
    seenSyms.forEach(sym => {
      const next = lr0GotoLocal(grammar, stateItems[from], sym);
      if (!next.length) return;
      let to = stateItems.findIndex(s => lr0ItemsEqual(s, next));
      if (to < 0) { to = stateItems.length; stateItems.push(next); work.push(to); }
      edges.push({ from, sym, to });
    });
  }

  /* FIRST（含 nullable） */
  const first = symbols.map(() => new Set());
  const nullable = symbols.map(() => false);
  let changed = true;
  while (changed) {
    changed = false;
    for (let pi = 0; pi < productions.length; pi++) {
      const p = productions[pi];
      const A = p.lhs;
      if (p.rhs.length === 0) { if (!nullable[A]) { nullable[A] = true; changed = true; } continue; }
      let allNullable = true;
      for (const X of p.rhs) {
        if (isT(X)) {
          if (!first[A].has(X)) { first[A].add(X); changed = true; }
          allNullable = false;
          break;
        }
        first[X].forEach(s => { if (!first[A].has(s)) { first[A].add(s); changed = true; } });
        if (!nullable[X]) { allNullable = false; break; }
      }
      if (allNullable && !nullable[A]) { nullable[A] = true; changed = true; }
    }
  }

  const firstOfSeq = seq => {
    const set = new Set();
    let allNull = true;
    for (const X of seq) {
      if (isT(X)) { set.add(X); allNull = false; break; }
      first[X].forEach(s => set.add(s));
      if (!nullable[X]) { allNull = false; break; }
    }
    return { set, nullable: allNull };
  };

  /* FOLLOW */
  const follow = symbols.map(() => new Set());
  const endSym = symbols.findIndex(s => s.kind === 'end');
  if (endSym >= 0) follow[grammar.augmentedStart].add(endSym);
  changed = true;
  while (changed) {
    changed = false;
    for (const p of productions) {
      for (let i = 0; i < p.rhs.length; i++) {
        const B = p.rhs[i];
        if (!isNT(B)) continue;
        const beta = p.rhs.slice(i + 1);
        const fb = firstOfSeq(beta);
        fb.set.forEach(s => { if (!follow[B].has(s)) { follow[B].add(s); changed = true; } });
        if (fb.nullable) {
          follow[p.lhs].forEach(s => { if (!follow[B].has(s)) { follow[B].add(s); changed = true; } });
        }
      }
    }
  }

  /* ACTION / GOTO 构造 */
  const findEdge = (from, sym) => edges.find(e => e.from === from && e.sym === sym);
  const action = stateItems.map(() => new Map());
  const gotoTab = stateItems.map(() => new Map());
  const conflicts = [];

  const trySet = (st, sym, act) => {
    const cell = action[st].get(sym);
    if (!cell) { action[st].set(sym, act); return; }
    if (cell.kind === act.kind && cell.target === act.target) return;
    let kind = 'conflict';
    if ((cell.kind === 'shift' && act.kind === 'reduce') ||
        (cell.kind === 'reduce' && act.kind === 'shift'))   kind = 'shift-reduce';
    else if (cell.kind === 'reduce' && act.kind === 'reduce') kind = 'reduce-reduce';
    conflicts.push({
      state: st, sym: symbols[sym].name, kind,
      existing: { kind: cell.kind, target: cell.target },
      incoming: { kind: act.kind, target: act.target }
    });
  };

  stateItems.forEach((items, st) => {
    items.forEach(it => {
      const p = productions[it.prod];
      if (it.dot < p.rhs.length) {
        const X = p.rhs[it.dot];
        const edge = findEdge(st, X);
        if (!edge) return;
        if (isT(X))  trySet(st, X, { kind: 'shift', target: edge.to });
        else if (isNT(X) && !gotoTab[st].has(X)) gotoTab[st].set(X, edge.to);
      } else {
        if (p.lhs === grammar.augmentedStart) {
          if (endSym >= 0) trySet(st, endSym, { kind: 'accept', target: -1 });
        } else {
          follow[p.lhs].forEach(s => trySet(st, s, { kind: 'reduce', target: it.prod }));
        }
      }
    });
  });

  /* 按 LR(0) JSON 兼容的 schema 输出 */
  const terms = symbols.filter(s => isT(s.id)).map(s => s.id);
  const nts   = symbols
    .filter(s => isNT(s.id) && s.id !== grammar.augmentedStart)
    .map(s => s.id);

  return {
    is_slr1: conflicts.length === 0,
    symbols: symbols.map(s => ({ id: s.id, name: s.name, kind: s.kind })),
    productions: productions.map((p, id) => ({
      id, lhs: symbols[p.lhs].name, rhs: p.rhs.map(s => symbols[s].name)
    })),
    terminals: terms.map(id => symbols[id].name),
    nonterminals: nts.map(id => symbols[id].name),
    states: stateItems.map((items, id) => ({ id, items })),
    edges: edges.map(e => ({ from: e.from, sym: symbols[e.sym].name, to: e.to })),
    first: nts.map(A => ({
      symbol: symbols[A].name,
      nullable: nullable[A],
      set: [...first[A]].map(id => symbols[id].name)
    })),
    follow: nts.map(A => ({
      symbol: symbols[A].name,
      set: [...follow[A]].map(id => symbols[id].name)
    })),
    action: stateItems.map((_, st) => ({
      state: st,
      row: [...action[st].entries()].map(([sym, a]) => ({
        sym: symbols[sym].name,
        kind: a.kind,
        target: a.target
      }))
    })),
    goto: stateItems.map((_, st) => ({
      state: st,
      row: [...gotoTab[st].entries()].map(([sym, t]) => ({
        sym: symbols[sym].name, target: t
      }))
    })),
    conflicts
  };
}

/* ── 渲染 ────────────────────────────────────────── */
let slrData = null;

function slrFormatItem(prodId, dot) {
  if (!slrData) return '';
  const p = slrData.productions[prodId];
  if (!p) return `[bad item ${prodId}.${dot}]`;
  const parts = p.rhs.length ? [...p.rhs] : ['ε'];
  if (p.rhs.length === 0) return `${p.lhs} -> .`;
  parts.splice(dot, 0, '·');
  return `${p.lhs} -> ${parts.join(' ')}`.replace('· ε', '·');
}

function slrRenderSets(data) {
  const grid  = document.getElementById('slr-sets-grid');
  const empty = document.getElementById('slr-sets-empty');
  if (!data.first.length) { grid.classList.add('hidden'); empty.classList.remove('hidden'); return; }
  grid.classList.remove('hidden'); empty.classList.add('hidden');

  const followMap = new Map(data.follow.map(f => [f.symbol, f.set]));
  grid.innerHTML = data.first.map(f => {
    const flw = followMap.get(f.symbol) || [];
    const firstStr = f.set.length ? esc(f.set.join(', ')) : '<span class="muted">∅</span>';
    const followStr = flw.length ? esc(flw.join(', ')) : '<span class="muted">∅</span>';
    const nul = f.nullable ? ' <span class="nullable">(nullable)</span>' : '';
    return `<div class="slr-set-card">
      <div><span class="name">${esc(f.symbol)}</span>${nul}</div>
      <div class="row"><span class="label">FIRST</span><span class="value">${firstStr}</span></div>
      <div class="row"><span class="label">FOLLOW</span><span class="value">${followStr}</span></div>
    </div>`;
  }).join('');
}

function slrConflictKeySet(data) {
  /* 返回 Set('state|symbol') 以便表格高亮 */
  return new Set(data.conflicts.map(c => `${c.state}|${c.sym}`));
}

function slrRenderAction(data) {
  const table = document.getElementById('slr-action-table');
  const empty = document.getElementById('slr-action-empty');
  if (!data.terminals.length) { table.classList.add('hidden'); empty.classList.remove('hidden'); return; }
  table.classList.remove('hidden'); empty.classList.add('hidden');

  const confKeys = slrConflictKeySet(data);
  const head = `<thead><tr><th>State</th>${data.terminals.map(t => `<th>${esc(t)}</th>`).join('')}</tr></thead>`;
  const rows = data.action.map(({ state, row }) => {
    const cells = data.terminals.map(t => {
      const a = row.find(r => r.sym === t);
      const isConflict = confKeys.has(`${state}|${t}`);
      if (!a) return `<td class="empty${isConflict ? ' slr-cell-conflict' : ''}">${isConflict ? '!' : ''}</td>`;
      let cls = `slr-cell-${a.kind}`;
      let label;
      if (a.kind === 'accept')      label = 'acc';
      else if (a.kind === 'shift')  label = `s${a.target}`;
      else if (a.kind === 'reduce') label = `r${a.target}`;
      else                          label = `${a.kind}/${a.target}`;
      if (isConflict) cls += ' slr-cell-conflict';
      return `<td class="${cls}" title="${esc(a.kind)} ${a.target}">${esc(label)}</td>`;
    }).join('');
    return `<tr><th>I${state}</th>${cells}</tr>`;
  }).join('');
  table.innerHTML = head + `<tbody>${rows}</tbody>`;
}

function slrRenderGoto(data) {
  const table = document.getElementById('slr-goto-table');
  const empty = document.getElementById('slr-goto-empty');
  if (!data.nonterminals.length) { table.classList.add('hidden'); empty.classList.remove('hidden'); return; }
  table.classList.remove('hidden'); empty.classList.add('hidden');

  const head = `<thead><tr><th>State</th>${data.nonterminals.map(n => `<th>${esc(n)}</th>`).join('')}</tr></thead>`;
  const rows = data.goto.map(({ state, row }) => {
    const cells = data.nonterminals.map(n => {
      const g = row.find(r => r.sym === n);
      if (!g) return `<td class="empty"></td>`;
      return `<td class="slr-cell-goto">${g.target}</td>`;
    }).join('');
    return `<tr><th>I${state}</th>${cells}</tr>`;
  }).join('');
  table.innerHTML = head + `<tbody>${rows}</tbody>`;
}

function slrRenderConflicts(data) {
  const el = document.getElementById('slr-conflicts');
  if (data.conflicts.length === 0) {
    el.innerHTML = '<p class="lr0-ok">No conflicts. Grammar is SLR(1).</p>';
    return;
  }
  el.innerHTML = `<p class="lr0-err">Not SLR(1): ${data.conflicts.length} conflict(s)</p>` +
    '<ul class="lr0-conflict-list">' +
    data.conflicts.map(c => {
      const fmt = side => {
        if (side.kind === 'shift')  return `<code>s${side.target}</code>`;
        if (side.kind === 'reduce') return `<code>r${side.target}</code> (${esc(slrFormatItem(side.target, slrData.productions[side.target].rhs.length))})`;
        if (side.kind === 'accept') return '<code>acc</code>';
        return `<code>${esc(side.kind)}</code>`;
      };
      return `<li>I${c.state} on <code>${esc(c.sym)}</code> ` +
             `<span class="lr0-badge lr0-badge-err">${esc(c.kind)}</span>` +
             `<ul><li>kept ${fmt(c.existing)}</li><li>dropped ${fmt(c.incoming)}</li></ul></li>`;
    }).join('') + '</ul>';
}

function slrRender(data, source) {
  slrData = data;
  slrRenderSets(data);
  slrRenderAction(data);
  slrRenderGoto(data);
  slrRenderConflicts(data);

  const status = document.getElementById('slr-status');
  const summary = document.getElementById('slr-summary');
  status.textContent = data.is_slr1 ? 'SLR(1) ✓' : `${data.conflicts.length} conflict(s)`;
  status.className = 'stats-badge ' + (data.is_slr1 ? 'ok' : 'err');
  summary.textContent = `${data.states.length} states · ${data.terminals.length} terminals · ` +
                        `${data.nonterminals.length} nonterminals` +
                        (source ? ` · ${source}` : '');
}

async function slrBuild(grammar) {
  const status = document.getElementById('slr-status');
  status.textContent = 'Building…';
  status.className = 'stats-badge';
  try {
    if (apiAvailable) {
      try {
        const r = await fetch(API_URL + '/api/slr', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ grammar }),
          signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
        });
        if (r.ok) { slrRender(await r.json(), 'API'); return; }
      } catch (_) {}
    }
    slrRender(slrBuildLocal(grammar), 'Local');
  } catch (err) {
    status.textContent = 'Error';
    status.className = 'stats-badge err';
    document.getElementById('slr-conflicts').innerHTML =
      '<p class="lr0-err">' + esc(err.message || String(err)) + '</p>';
  }
}

document.getElementById('btn-slr-sample').addEventListener('click', () => {
  document.getElementById('slr-grammar').value = SAMPLE_GRAMMAR;
});
document.getElementById('btn-slr-demo').addEventListener('click', () => {
  document.getElementById('slr-grammar').value = SLR_DEMO_GRAMMAR;
});
document.getElementById('btn-slr-dangling').addEventListener('click', () => {
  document.getElementById('slr-grammar').value = SLR_DANGLING_GRAMMAR;
});
document.getElementById('btn-slr-ambig').addEventListener('click', () => {
  document.getElementById('slr-grammar').value = AMBIG_GRAMMAR;
});
document.getElementById('btn-slr-build').addEventListener('click', () => {
  const g = document.getElementById('slr-grammar').value.trim();
  if (!g) { alert('Grammar is empty'); return; }
  slrBuild(g);
});
