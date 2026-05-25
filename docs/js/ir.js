/* ── Lab6: IR Generator tab ────────────────────────────── */

(function () {
  const $ = (id) => document.getElementById(id);

  /* 用例下拉沿用 parse_cases.js 的 CASE_SOURCES */
  const sel = $('ir-case');
  if (sel && typeof CASE_SOURCES === 'object') {
    Object.keys(CASE_SOURCES).sort((a, b) => +a - +b).forEach(k => {
      const opt = document.createElement('option');
      opt.value = k; opt.textContent = `${k}.src`;
      sel.appendChild(opt);
    });
    sel.addEventListener('change', () => {
      if (!sel.value) return;
      $('ir-source').value = CASE_SOURCES[sel.value] || '';
    });
  }

  $('btn-ir-clear').addEventListener('click', () => {
    $('ir-source').value = '';
    sel.value = '';
    setStatus('Idle', '');
    $('ir-quads').innerHTML = '<p class="muted">点击 Generate 后显示四元式。</p>';
    $('ir-symtab').innerHTML = '<p class="muted">符号表将在生成后呈现。</p>';
    $('ir-errors').innerHTML = '<p class="muted">尚未运行。</p>';
    $('ir-summary').textContent = '';
  });

  function setStatus(text, cls) {
    const el = $('ir-status');
    el.textContent = text;
    el.className = 'stats-badge ' + (cls || '');
  }

  /* 把 arg 字符串渲染成带类别的 span：临时变量 / 标签 / 字面量 / 标识符 */
  function renderArg(s, isLabel) {
    if (s === '_' || s === '') return '<span class="muted">_</span>';
    if (isLabel) return `<span class="q-label" data-label="${esc(s)}">${esc(s)}</span>`;
    if (/^t\d+$/.test(s))      return `<span class="q-temp">${esc(s)}</span>`;
    if (/^L\d+$/.test(s))      return `<span class="q-label" data-label="${esc(s)}">${esc(s)}</span>`;
    if (/^-?\d+(\.\d+)?$/.test(s)) return `<span class="q-lit">${esc(s)}</span>`;
    return esc(s);
  }

  function renderQuads(data) {
    const quads = data.quads || [];
    if (!quads.length) {
      $('ir-quads').innerHTML = '<p class="muted">没有生成四元式（可能因语义错误）。</p>';
      return;
    }
    /* 跳转目标（GOTO / IF_FALSE）的 result 是标签 */
    const jumpOps = new Set(['GOTO', 'IF_FALSE']);
    const rows = quads.map(q => {
      const cat = q.cat || 'ctrl';
      const resultIsLabel = jumpOps.has(q.op) || q.op === 'LABEL';
      return `<tr class="q-row q-cat-${cat}" data-i="${q.i}" data-line="${q.line || 0}"
              data-label="${q.op === 'LABEL' ? esc(q.result) : ''}">
        <td class="q-i">${q.i}</td>
        <td class="q-op">${esc(q.op)}</td>
        <td class="q-arg">${renderArg(q.arg1)}</td>
        <td class="q-arg">${renderArg(q.arg2)}</td>
        <td class="q-result">${renderArg(q.result, resultIsLabel)}</td>
      </tr>`;
    }).join('');
    $('ir-quads').innerHTML =
      `<table class="quad-table"><thead><tr>
         <th>#</th><th>op</th><th>arg1</th><th>arg2</th><th>result</th>
       </tr></thead><tbody>${rows}</tbody></table>`;

    /* 标签悬停联动：高亮跳转目标行 */
    $('ir-quads').querySelectorAll('.q-label').forEach(node => {
      node.addEventListener('mouseenter', () => {
        const label = node.dataset.label;
        $('ir-quads').querySelectorAll('.q-row').forEach(r => {
          if (r.dataset.label === label) r.classList.add('q-target-hi');
        });
      });
      node.addEventListener('mouseleave', () => {
        $('ir-quads').querySelectorAll('.q-row.q-target-hi').forEach(r => r.classList.remove('q-target-hi'));
      });
    });
  }

  function renderSymtab(data) {
    const scopes = data.symtab || [];
    if (!scopes.length) {
      $('ir-symtab').innerHTML = '<p class="muted">空</p>';
      return;
    }
    $('ir-symtab').innerHTML = scopes.map(s => {
      const rows = (s.symbols || []).map(sym => `
        <tr>
          <td class="sym-name">${esc(sym.name)}</td>
          <td class="sym-type">${esc(sym.type)}${sym.is_array ? '[]' : ''}${sym.is_func ? '()' : ''}</td>
          <td class="sym-loc">${sym.line}:${sym.col}</td>
        </tr>`).join('');
      return `<div class="symtab-scope">
        <div class="scope-head">Scope ${s.scope} · level ${s.level}</div>
        ${rows ? `<table class="symtab-table">${rows}</table>` : '<p class="muted">空</p>'}
      </div>`;
    }).join('');
  }

  function renderErrors(data) {
    const errs = data.errors || [];
    if (!errs.length) {
      $('ir-errors').innerHTML = '<p class="lr0-ok">✓ No errors.</p>';
      return;
    }
    $('ir-errors').innerHTML = '<ul class="err-list-ul">' + errs.map(e => `
      <li>
        <span class="err-kind err-${e.kind}">${esc(e.kind)}</span>
        <span class="err-loc">${e.line}:${e.col}</span>
        <span class="err-msg">${esc(e.message)}</span>
      </li>`).join('') + '</ul>';
  }

  function renderAll(data) {
    renderQuads(data);
    renderSymtab(data);
    renderErrors(data);
    const ok = !!data.accepted;
    const n = (data.quads || []).length;
    setStatus(ok ? `Accepted ✓ · ${n} quads` : `${(data.errors || []).length} error(s) · ${n} quads`,
              ok ? 'ok' : 'err');
    $('ir-summary').textContent =
      `${n} quads · ${data.temp_count || 0} temps · ${data.label_count || 0} labels`;
  }

  async function generate(source) {
    if (!apiAvailable) {
      setStatus('API offline', 'err');
      $('ir-quads').innerHTML = '<p class="lr0-err">需要后端 API。请先启动 server.py 或检查 API 状态。</p>';
      return null;
    }
    setStatus('Generating…', '');
    const r = await fetch(API_URL + '/api/ir', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ source }),
      signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
    });
    if (!r.ok) throw new Error('API returned ' + r.status);
    return await r.json();
  }

  $('btn-ir-run').addEventListener('click', async () => {
    const source = $('ir-source').value;
    if (!source.trim()) return;
    try {
      const data = await generate(source);
      if (data) renderAll(data);
    } catch (e) {
      setStatus('Error', 'err');
      $('ir-quads').innerHTML = '<p class="lr0-err">' + esc(e.message || String(e)) + '</p>';
    }
  });

  /* 与 parse.js 保持一致的预期失败集合（仅注释里有 "错误：…" 的用例） */
  const IR_EXPECTED_FAIL = new Set(['2','6','8','9','17','25','33']);

  /* ── 批量回归看板 ──────────────────────────────── */
  $('btn-ir-runall').addEventListener('click', async () => {
    const batch = $('ir-batch');
    batch.classList.remove('hidden');
    const grid = $('ir-batch-result');
    grid.innerHTML = '';
    let pass = 0, failOk = 0, mismatch = 0, errCnt = 0;
    const cells = [];
    for (let i = 1; i <= 33; i++) {
      const key = String(i);
      const src = (typeof CASE_SOURCES === 'object' && CASE_SOURCES[key]) || '';
      const expectFail = IR_EXPECTED_FAIL.has(key);
      let cls = 'cell-err', title = key + '.src';
      try {
        const data = await generate(src);
        if (!data) { cls = 'cell-err'; errCnt++; }
        else if (data.accepted) {
          const n = (data.quads || []).length;
          if (expectFail) { cls = 'cell-mismatch'; mismatch++;
            title = `${key}.src · 预期失败但实际通过 · ${n} quads`;
          } else            { cls = 'cell-pass'; pass++;
            title = `${key}.src · ${n} quads · ${data.temp_count || 0} temps`;
          }
        } else {
          const n = (data.quads || []).length;
          const e = (data.errors || []).length;
          if (expectFail) { cls = 'cell-fail-ok'; failOk++;
            title = `${key}.src · 预期失败已检出 · ${e} errors`;
          } else            { cls = 'cell-mismatch'; mismatch++;
            title = `${key}.src · 预期通过但 ${e} errors · ${n} quads`;
          }
        }
      } catch (e) {
        errCnt++; cls = 'cell-err';
        title = key + '.src — ' + (e.message || String(e));
      }
      cells.push(`<div class="batch-cell ${cls}" title="${esc(title)}">${key}</div>`);
      grid.innerHTML = cells.join('');
      $('ir-batch-stats').textContent =
        `${pass} pass · ${failOk} fail-as-expected · ${mismatch} mismatch · ${errCnt} error · ${cells.length}/33`;
    }
    /* 批量完成后恢复主状态徽章 */
    setStatus(`Batch done · ${pass + failOk} 对预期 · ${mismatch + errCnt} 偏差`,
              mismatch + errCnt === 0 ? 'ok' : '');
  });
})();
