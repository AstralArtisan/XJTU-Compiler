/* ── Lab8: Assembly & Run tab ───────────────────────── */
(function () {
  const $ = (id) => document.getElementById(id);

  const sel = $('asm-case');
  if (sel && typeof CASE_SOURCES === 'object') {
    Object.keys(CASE_SOURCES).sort((a, b) => +a - +b).forEach(k => {
      const opt = document.createElement('option');
      opt.value = k; opt.textContent = `${k}.src`;
      sel.appendChild(opt);
    });
    sel.addEventListener('change', () => {
      if (sel.value) $('asm-source').value = CASE_SOURCES[sel.value] || '';
    });
  }

  $('btn-asm-clear').addEventListener('click', () => {
    $('asm-source').value = ''; sel.value = '';
    setStatus('Idle', '');
    $('asm-text').textContent = '点击 Generate Asm 或 Compile & Run 后显示。';
    $('asm-stdout').textContent = '';
    $('asm-cstderr').textContent = '';
    $('asm-exit').textContent = '';
    $('asm-summary').textContent = '';
  });

  function setStatus(text, cls) {
    const el = $('asm-status');
    el.textContent = text;
    el.className = 'stats-badge ' + (cls || '');
  }

  /* 把汇编文本逐行高亮：标签 / 指令 / 寄存器 / 立即数 / 注释 */
  function highlightAsm(text) {
    if (!text) return '';
    const ctrlMnem = /^(b|bl|cbz|cbnz|ret|cmp|cset|b\.\w+)$/;
    const memMnem  = /^(ldr|str|ldp|stp|adrp)$/;
    return text.split('\n').map(line => {
      if (!line.trim()) return '';
      /* 注释整行 */
      const cm = line.match(/^(\s*)\/\*(.*)\*\/\s*$/);
      if (cm) return `${esc(cm[1])}<span class="a-comment">/*${esc(cm[2])}*/</span>`;
      /* 标签：xxx: */
      const lm = line.match(/^([A-Za-z_.][\w.]*):$/);
      if (lm) return `<span class="a-label">${esc(lm[1])}:</span>`;
      /* 指令行：缩进 + mnem + 余下 */
      const im = line.match(/^(\s+)([A-Za-z_.][\w.]*)\s*(.*)$/);
      if (im) {
        const [, indent, mnem, rest] = im;
        let cls = 'a-mnem';
        if (mnem.startsWith('.')) cls = 'a-direct';
        else if (ctrlMnem.test(mnem)) cls = 'a-mnem-ctrl';
        else if (memMnem.test(mnem)) cls = 'a-mnem-mem';
        const rest2 = esc(rest)
          .replace(/\b(x\d+|w\d+|sp|x29|x30)\b/g, '<span class="a-reg">$1</span>')
          .replace(/#(-?\d+)/g, '<span class="a-imm">#$1</span>');
        return `${esc(indent)}<span class="${cls}">${esc(mnem)}</span> ${rest2}`;
      }
      return esc(line);
    }).join('\n');
  }

  async function callApi(endpoint, body) {
    if (!apiAvailable) throw new Error('API offline');
    const r = await fetch(API_URL + endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(30000)
    });
    if (!r.ok) throw new Error('API ' + r.status);
    return await r.json();
  }

  $('btn-asm-gen').addEventListener('click', async () => {
    const source = $('asm-source').value;
    if (!source.trim()) return;
    setStatus('Generating…', '');
    try {
      const data = await callApi('/api/asm', { source });
      $('asm-text').innerHTML = highlightAsm(data.asm || '');
      $('asm-stdout').textContent = ''; $('asm-cstderr').textContent = ''; $('asm-exit').textContent = '';
      const lines = (data.asm || '').split('\n').length - 1;
      $('asm-summary').textContent = `${lines} 行汇编 · ${(data.quads || []).length} 四元式`;
      setStatus(data.accepted ? `Accepted ✓ · ${lines} lines` : `${(data.errors || []).length} errs`,
                data.accepted ? 'ok' : 'err');
    } catch (e) {
      setStatus('Error', 'err');
      $('asm-text').textContent = e.message || String(e);
    }
  });

  $('btn-asm-run').addEventListener('click', async () => {
    const source = $('asm-source').value;
    if (!source.trim()) return;
    const stdin = $('asm-stdin').value || '';
    setStatus('Compiling & running…', '');
    try {
      const data = await callApi('/api/run', { source, stdin });
      $('asm-text').innerHTML = highlightAsm(data.asm || '');
      $('asm-stdout').textContent  = data.program_stdout || '';
      $('asm-cstderr').textContent = data.compile_stderr || '';
      $('asm-exit').textContent    = (data.exit_code === null || data.exit_code === undefined)
                                       ? '—' : String(data.exit_code);
      const ok = (data.exit_code === 0) && !(data.compile_stderr || '').trim();
      const lines = (data.asm || '').split('\n').length - 1;
      $('asm-summary').textContent = `${lines} 行汇编 · exit=${data.exit_code ?? '—'}`;
      setStatus(ok ? `Ran ✓ exit=0` :
                (data.exit_code === -2 ? 'Compile failed' :
                 (data.accepted ? `Ran exit=${data.exit_code}` : `${(data.errors || []).length} errs`)),
                ok ? 'ok' : 'err');
    } catch (e) {
      setStatus('Error', 'err');
      $('asm-cstderr').textContent = e.message || String(e);
    }
  });
})();
