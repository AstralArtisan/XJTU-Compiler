/* ── Lab7: Memory Map tab ──────────────────────────── */
(function () {
  const $ = (id) => document.getElementById(id);

  /* 用例下拉 */
  const sel = $('mm-case');
  if (sel && typeof CASE_SOURCES === 'object') {
    Object.keys(CASE_SOURCES).sort((a, b) => +a - +b).forEach(k => {
      const opt = document.createElement('option');
      opt.value = k; opt.textContent = `${k}.src`;
      sel.appendChild(opt);
    });
    sel.addEventListener('change', () => {
      if (sel.value) $('mm-source').value = CASE_SOURCES[sel.value] || '';
    });
  }

  $('btn-mm-clear').addEventListener('click', () => {
    $('mm-source').value = ''; sel.value = '';
    setStatus('Idle', '');
    $('mm-quads').innerHTML = '<p class="muted">点击 Compute 后显示。</p>';
    $('mm-layout').innerHTML = '<p class="muted">栈帧表将在计算后呈现。</p>';
    $('mm-errors').innerHTML = '<p class="muted">尚未运行。</p>';
    $('mm-summary').textContent = '';
  });

  function setStatus(text, cls) {
    const el = $('mm-status');
    el.textContent = text;
    el.className = 'stats-badge ' + (cls || '');
  }

  function renderQuads(quads) {
    if (!quads || !quads.length) {
      $('mm-quads').innerHTML = '<p class="muted">无四元式。</p>';
      return;
    }
    const rows = quads.map(q => `<tr class="q-row q-cat-${q.cat || 'ctrl'}">
      <td class="q-i">${q.i}</td>
      <td class="q-op">${esc(q.op)}</td>
      <td class="q-arg">${esc(q.arg1)}</td>
      <td class="q-arg">${esc(q.arg2)}</td>
      <td class="q-result">${esc(q.result)}</td>
    </tr>`).join('');
    $('mm-quads').innerHTML =
      `<table class="quad-table"><thead><tr><th>#</th><th>op</th><th>a1</th><th>a2</th><th>r</th></tr></thead>
       <tbody>${rows}</tbody></table>`;
  }

  function renderLayout(memmap) {
    if (!memmap) { $('mm-layout').innerHTML = '<p class="muted">无内存映射数据。</p>'; return; }
    let html = '';
    if (memmap.globals && memmap.globals.length) {
      const rows = memmap.globals.map(g => `<tr>
        <td class="mm-off">global</td>
        <td class="mm-name">${esc(g.name)}</td>
        <td class="mm-kind mm-kind-global">${esc(g.kind)}</td>
        <td class="mm-size">${g.size}${g.array_len ? '[' + g.array_len + ']' : ''}</td>
      </tr>`).join('');
      html += `<div class="mm-card">
        <div class="mm-head">Globals · .bss</div>
        <table class="mm-table">${rows}</table></div>`;
    }
    for (const f of memmap.functions || []) {
      const rows = (f.slots || []).map(s => `<tr>
        <td class="mm-off">[x29, ${s.offset >= 0 ? '+' : ''}${s.offset}]</td>
        <td class="mm-name">${esc(s.name)}</td>
        <td class="mm-kind mm-kind-${s.kind}">${esc(s.kind)}</td>
        <td class="mm-size">${s.size}${s.array_len ? '[' + s.array_len + ']' : ''}</td>
      </tr>`).join('');
      html += `<div class="mm-card">
        <div class="mm-head">${esc(f.name)}() <span class="mm-frame">· frame=${f.frame_size}B · ${f.slots.length} slots</span></div>
        ${rows ? `<table class="mm-table">${rows}</table>` : '<p class="muted">空</p>'}
      </div>`;
    }
    $('mm-layout').innerHTML = html || '<p class="muted">空</p>';
  }

  function renderErrors(errs) {
    if (!errs || !errs.length) {
      $('mm-errors').innerHTML = '<p class="lr0-ok">✓ No errors.</p>';
      return;
    }
    $('mm-errors').innerHTML = '<ul class="err-list-ul">' + errs.map(e => `
      <li>
        <span class="err-kind err-${e.kind}">${esc(e.kind)}</span>
        <span class="err-loc">${e.line}:${e.col}</span>
        <span class="err-msg">${esc(e.message)}</span>
      </li>`).join('') + '</ul>';
  }

  $('btn-mm-run').addEventListener('click', async () => {
    const source = $('mm-source').value;
    if (!source.trim()) return;
    if (!apiAvailable) { setStatus('API offline', 'err'); return; }
    setStatus('Computing…', '');
    try {
      const r = await fetch(API_URL + '/api/memmap', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source }),
        signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
      });
      if (!r.ok) throw new Error('API ' + r.status);
      const data = await r.json();
      renderQuads(data.quads || []);
      renderLayout(data.memmap);
      renderErrors(data.errors || []);
      const fn = (data.memmap && data.memmap.functions) || [];
      const tot = fn.reduce((a, f) => a + f.frame_size, 0);
      $('mm-summary').textContent = `${fn.length} 个函数 · 总栈帧 ${tot}B`;
      setStatus(data.accepted ? `Accepted ✓ · ${fn.length} funcs` : `${(data.errors || []).length} errs`,
                data.accepted ? 'ok' : 'err');
    } catch (e) {
      setStatus('Error', 'err');
      $('mm-layout').innerHTML = '<p class="lr0-err">' + esc(e.message || String(e)) + '</p>';
    }
  });
})();
