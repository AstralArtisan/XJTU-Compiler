/* ═══════════════════════════════════════════════════
   Lab 5 — Parse & Semantic Analyzer
   AST + Symbol Table + Errors  (Result mode)
   Optional Theater mode (trace replay) at the bottom.
   Depends on: shared.js, parse_cases.js
   ═══════════════════════════════════════════════════ */

/* ── 用例下拉填充 ─────────────────────────────────── */
(function initCaseDropdown() {
  const sel = document.getElementById('parse-case');
  if (!sel) return;
  const keys = Object.keys(CASE_SOURCES).sort((a, b) => +a - +b);
  for (const k of keys) {
    const opt = document.createElement('option');
    opt.value = k;
    opt.textContent = `${k}.src`;
    sel.appendChild(opt);
  }
  sel.addEventListener('change', () => {
    const v = sel.value;
    if (!v) return;
    document.getElementById('parse-source').value = CASE_SOURCES[v] || '';
  });
})();

document.getElementById('btn-parse-clear').addEventListener('click', () => {
  document.getElementById('parse-source').value = '';
  document.getElementById('parse-case').value = '';
});

/* ── AST 渲染 ─────────────────────────────────────── */
function astLabel(node) {
  let label = node.kind;
  if (node.name)   label += ' · ' + node.name;
  if (node.type && node.type !== 'void') label += ' : ' + node.type;
  if (node.is_array) label += '[]';
  if (typeof node.value !== 'undefined') label += ' = ' + node.value;
  return label;
}

function renderAst(root) {
  const el = document.getElementById('parse-ast');
  if (!root) { el.innerHTML = '<p class="muted">AST 不可用。</p>'; return; }
  const build = (n) => {
    const item = document.createElement('li');
    item.className = 'ast-node ast-kind-' + (n.kind || 'unknown');
    const head = document.createElement('span');
    head.className = 'ast-label';
    head.textContent = astLabel(n);
    head.title = `line ${n.line}:${n.col}`;
    item.appendChild(head);
    if (n.children && n.children.length) {
      const ul = document.createElement('ul');
      ul.className = 'ast-children';
      for (const c of n.children) ul.appendChild(build(c));
      item.appendChild(ul);
    }
    return item;
  };
  const root_ul = document.createElement('ul');
  root_ul.className = 'ast-root';
  root_ul.appendChild(build(root));
  el.innerHTML = '';
  el.appendChild(root_ul);
}

/* ── 符号表渲染 ───────────────────────────────────── */
function renderSymtab(scopes) {
  const el = document.getElementById('parse-symtab');
  if (!scopes || !scopes.length) {
    el.innerHTML = '<p class="muted">符号表为空。</p>';
    return;
  }
  el.innerHTML = scopes.map(sc => {
    const rows = sc.symbols.map(s => {
      const flags = [s.is_func ? '()' : '', s.is_array ? '[]' : ''].join('');
      const params = s.is_func && s.params
        ? `(${s.params.map(p => p.type + (p.is_array ? '[]' : '')).join(', ')})`
        : '';
      return `<tr>
        <td class="sym-name">${esc(s.name)}${flags}</td>
        <td class="sym-type">${esc(s.type)}${esc(params)}</td>
        <td class="sym-loc">@${s.line}:${s.col}</td>
      </tr>`;
    }).join('');
    const heading = sc.scope === 0
      ? `Scope 0 · global`
      : `Scope ${sc.scope} · level ${sc.level} (parent ${sc.parent})`;
    return `<div class="symtab-scope" data-scope="${sc.scope}">
      <div class="scope-head">${heading}</div>
      ${sc.symbols.length ? `<table class="symtab-table">${rows}</table>` : '<p class="muted">(empty)</p>'}
    </div>`;
  }).join('');
}

/* ── 错误列表渲染 ─────────────────────────────────── */
function renderErrors(errors) {
  const el = document.getElementById('parse-errors');
  if (!errors || !errors.length) {
    el.innerHTML = '<p class="lr0-ok">✓ No errors.</p>';
    return;
  }
  el.innerHTML = '<ul class="err-list-ul">' +
    errors.map(e => `<li>
      <span class="err-kind err-${esc(e.kind)}">${esc(e.kind)}</span>
      <span class="err-loc">${e.line}:${e.col}</span>
      <span class="err-msg">${esc(e.message)}</span>
    </li>`).join('') + '</ul>';
}

/* ── Verdict 徽章 ─────────────────────────────────── */
function updateStatus(accepted, errCount) {
  const el = document.getElementById('parse-status');
  if (accepted) {
    el.textContent = 'Accepted ✓';
    el.className = 'stats-badge ok';
  } else {
    el.textContent = `${errCount} error(s)`;
    el.className = 'stats-badge err';
  }
}

/* ── Parse 主调度 ─────────────────────────────────── */
async function callParseAPI(source, trace) {
  if (!apiAvailable) throw new Error('API offline — 启动 server.py 后刷新页面');
  const r = await fetch(API_URL + '/api/parse', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source, trace: !!trace }),
    signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS),
  });
  if (!r.ok) throw new Error('API returned ' + r.status);
  return r.json();
}

document.getElementById('btn-parse-run').addEventListener('click', async () => {
  const src = document.getElementById('parse-source').value;
  if (!src.trim()) { alert('Source is empty'); return; }
  const traceOn = document.getElementById('parse-trace').checked;
  const status = document.getElementById('parse-status');
  status.textContent = 'Parsing…';
  status.className = 'stats-badge';
  try {
    const data = await callParseAPI(src, traceOn);
    renderAst(data.ast);
    renderSymtab(data.symtab);
    renderErrors(data.errors);
    updateStatus(!!data.accepted, (data.errors || []).length);
    if (traceOn && data.steps) {
      document.getElementById('parse-theater').classList.remove('hidden');
      theaterLoad(data);
    } else {
      document.getElementById('parse-theater').classList.add('hidden');
    }
  } catch (err) {
    status.textContent = 'Error';
    status.className = 'stats-badge err';
    document.getElementById('parse-errors').innerHTML =
      '<p class="lr0-err">' + esc(err.message || String(err)) + '</p>';
  }
});

/* ── Run all 33（批量回归） ───────────────────────── */

/* 用例预期：仅列出注释里明确写"错误"的；其余 24 份视为应当通过。
 * 9.src（负号）与 24.src（负数）按 PPT 注释属"弹性"——
 * 这里按"如自定义词法可接受负号则可认为正确"的宽容口径，9 仍判 fail（默认不支持），
 * 24 判 pass（注释"可认为不报错"）。 */
const EXPECTED_FAIL = new Set([
  '2',   // && 不支持
  '6',   // foo 未声明
  '8',   // 字符串未定义
  '9',   // 负号未定义（弹性 fail）
  '17',  // c 未声明
  '25',  // 返回类型不一致
  '33',  // for / 二维数组 / 声明同时赋值
]);

function expectedFor(id) {
  return EXPECTED_FAIL.has(id) ? 'fail' : 'pass';
}

document.getElementById('btn-parse-runall').addEventListener('click', async () => {
  if (!apiAvailable) { alert('API offline'); return; }
  const batch = document.getElementById('parse-batch');
  const result = document.getElementById('parse-batch-result');
  const stats  = document.getElementById('parse-batch-stats');
  batch.classList.remove('hidden');
  result.innerHTML = '<p class="muted">running…</p>';
  let passCorrect = 0, failCorrect = 0, mismatch = 0;
  const cells = [];
  for (const k of Object.keys(CASE_SOURCES).sort((a, b) => +a - +b)) {
    const exp = expectedFor(k);
    try {
      const data = await callParseAPI(CASE_SOURCES[k], false);
      const got = data.accepted ? 'pass' : 'fail';
      const ok  = (got === exp);
      if (ok && got === 'pass') passCorrect++;
      else if (ok && got === 'fail') failCorrect++;
      else mismatch++;
      const cls = ok ? (got === 'pass' ? 'cell-pass' : 'cell-fail-ok') : 'cell-mismatch';
      cells.push(`<div class="batch-cell ${cls}" title="expected=${exp} got=${got}">${k}</div>`);
    } catch (e) {
      cells.push(`<div class="batch-cell cell-err" title="${esc(e.message)}">${k}</div>`);
      mismatch++;
    }
    result.innerHTML = cells.join('');
  }
  stats.textContent = `${passCorrect} pass · ${failCorrect} fail-as-expected · ${mismatch} mismatch`;
  stats.className = 'stats-badge ' + (mismatch === 0 ? 'ok' : 'err');
});

/* ── Theater 模式：trace 重放 ─────────────────────── */
const theater = {
  data: null,
  index: 0,
  timer: null,
  speed: 320,
};

function theaterLoad(data) {
  theater.data = data;
  theater.index = 0;
  if (theater.timer) { clearInterval(theater.timer); theater.timer = null; }
  document.getElementById('theater-play').textContent = '▶';
  document.getElementById('theater-stats').textContent =
    (data.steps?.length ?? 0) + ' steps · ' + (data.tokens?.length ?? 0) + ' tokens';
  theaterRender();
}

function prodText(prod) {
  if (!prod) return '?';
  return prod.lhs + ' → ' + (prod.rhs.length ? prod.rhs.join(' ') : 'ε');
}

function theaterRender() {
  const data = theater.data;
  if (!data) return;
  const step = data.steps[theater.index];
  const stepEl = document.getElementById('theater-step');
  stepEl.textContent = `step ${theater.index + 1} / ${data.steps.length}`;

  /* Tokens 列：高亮 input_pos */
  const tokensHtml = (data.tokens || []).map((t, i) => {
    const cls = i === step.input_pos ? 'tok-cur' : '';
    return `<div class="theater-token ${cls}">
      <span class="tk-kind">${esc(t.kind)}</span>
      <span class="tk-lex">${esc(t.lexeme)}</span>
    </div>`;
  }).join('');
  document.getElementById('theater-tokens').innerHTML = tokensHtml;
  /* 滚动到当前 token */
  const cur = document.querySelector('.theater-token.tok-cur');
  if (cur) cur.scrollIntoView({ block: 'nearest', inline: 'nearest' });

  /* 状态栈 */
  const stackHtml = (step.stack || []).map((s, i) =>
    `<div class="stack-cell" data-i="${i}">${s}</div>`).join('');
  document.getElementById('theater-stack').innerHTML = stackHtml;

  /* 当前动作 */
  let actText = '';
  if (step.action === 'shift') {
    actText = `<span class="act-shift">shift</span> → state ${step.target}`;
  } else if (step.action === 'reduce') {
    const pr = data.productions[step.prod];
    actText = `<span class="act-reduce">reduce</span> ${step.prod}: ${esc(prodText(pr))}`;
  } else if (step.action === 'accept') {
    actText = `<span class="act-accept">ACCEPT</span>`;
  } else {
    actText = `<span class="act-error">error</span>`;
  }
  document.getElementById('theater-action').innerHTML = actText;

  /* AST 累积：截止到当前 step 已经 reduce 出的节点 id */
  const visibleIds = new Set();
  for (let i = 0; i <= theater.index; i++) {
    const sp = data.steps[i];
    if (typeof sp.new_ast !== 'undefined') visibleIds.add(sp.new_ast);
  }
  const lines = [];
  function walk(n, depth) {
    if (!n) return;
    if (visibleIds.has(n.id)) {
      lines.push(`<div class="ast-line" data-depth="${depth}">${'  '.repeat(depth)}${esc(astLabel(n))}<span class="ast-id">#${n.id}</span></div>`);
    }
    if (n.children) for (const c of n.children) walk(c, depth + 1);
  }
  walk(data.ast, 0);
  document.getElementById('theater-ast').innerHTML =
    lines.length ? lines.join('') : '<p class="muted">尚无节点生成</p>';

  /* 符号表显示最终态（简化实现） */
  document.getElementById('theater-symtab').innerHTML = (data.symtab || []).map(sc =>
    `<div class="theater-scope">
      <div class="theater-scope-head">scope ${sc.scope}</div>
      ${sc.symbols.map(s =>
        `<div class="theater-sym">${esc(s.name)} : ${esc(s.type)}${s.is_array ? '[]' : ''}${s.is_func ? '()' : ''}</div>`
      ).join('')}
    </div>`).join('');
}

function theaterStep(delta) {
  if (!theater.data) return;
  theater.index = Math.max(0, Math.min(theater.data.steps.length - 1, theater.index + delta));
  theaterRender();
}

document.getElementById('theater-rewind').addEventListener('click', () => { theater.index = 0; theaterRender(); });
document.getElementById('theater-back').addEventListener('click', () => theaterStep(-1));
document.getElementById('theater-next').addEventListener('click', () => theaterStep(1));
document.getElementById('theater-end').addEventListener('click', () => {
  if (!theater.data) return;
  theater.index = theater.data.steps.length - 1;
  theaterRender();
});
document.getElementById('theater-play').addEventListener('click', () => {
  if (!theater.data) return;
  const btn = document.getElementById('theater-play');
  if (theater.timer) {
    clearInterval(theater.timer); theater.timer = null;
    btn.textContent = '▶';
    return;
  }
  btn.textContent = '⏸';
  theater.timer = setInterval(() => {
    if (theater.index >= theater.data.steps.length - 1) {
      clearInterval(theater.timer); theater.timer = null;
      btn.textContent = '▶';
      return;
    }
    theater.index++;
    theaterRender();
  }, theater.speed);
});
document.getElementById('theater-speed').addEventListener('input', e => {
  theater.speed = parseInt(e.target.value, 10);
  if (theater.timer) {
    clearInterval(theater.timer);
    theater.timer = setInterval(() => {
      if (theater.index >= theater.data.steps.length - 1) {
        clearInterval(theater.timer); theater.timer = null;
        document.getElementById('theater-play').textContent = '▶';
        return;
      }
      theater.index++;
      theaterRender();
    }, theater.speed);
  }
});

/* 切到 Parse tab 时若 textarea 为空，默认填 1.src */
document.querySelectorAll('.nav-tab[data-view="parse"]').forEach(t => {
  t.addEventListener('click', () => {
    const src = document.getElementById('parse-source');
    if (!src.value.trim() && CASE_SOURCES['1']) {
      src.value = CASE_SOURCES['1'];
      const sel = document.getElementById('parse-case');
      if (sel) sel.value = '1';
    }
  });
});
