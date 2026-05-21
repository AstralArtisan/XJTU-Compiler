/* ═══════════════════════════════════════════════════
   Lab 2 — Lexical Analyzer (tokens)
   Frontend tokenizer + API integration
   Depends on: shared.js
   ═══════════════════════════════════════════════════ */

const SAMPLE_SOURCE = `int gcd(int a, int b) {
    while (b != 0) {
        int t;
        t = b;
        b = a - a / b * b;
        a = t;
    }
    return a;
}

void main() {
    int x;
    int y;
    float pi;
    pi = 3.14;
    pi = .5;
    pi = 1e-3;
    pi = 12.3E+4;
    x = 0;
    y = 10;
    while (x <= y) {
        if (x == y) {
            print(x);
        } else {
            x += 1;
        }
        x++;
    }
    if (x > 0 && y >= 0 || !x) {
        return;
    }
}`;

/* ── Frontend Tokenizer (Local fallback) ──────────── */
const KW = { int:'INT', float:'FLOAT_KW', void:'VOID', 'if':'IF', 'else':'ELSE',
             'while':'WHILE', 'return':'RETURN', input:'INPUT', print:'PRINT' };

function tokenize(src) {
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
      let fl=false;
      if(c===46){adv();fl=true;while(isD(pk()))adv();}
      else{while(isD(pk()))adv();if(pk()===46){fl=true;adv();while(isD(pk()))adv();}}
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
const TC = {
  INT:'tk-keyword', FLOAT_KW:'tk-keyword', VOID:'tk-keyword', IF:'tk-keyword',
  ELSE:'tk-keyword', WHILE:'tk-keyword', RETURN:'tk-keyword', INPUT:'tk-keyword',
  PRINT:'tk-keyword',
  ID:'tk-id', NUM:'tk-num', FLOAT_LIT:'tk-float',
  ADD:'tk-op', SUB:'tk-op', MUL:'tk-op', DIV:'tk-op', LT:'tk-op', LE:'tk-op',
  EQ:'tk-op', GT:'tk-op', GE:'tk-op', NE:'tk-op', AND:'tk-op', OR:'tk-op',
  NOT:'tk-op', ASG:'tk-op', AAS:'tk-op', AAA:'tk-op',
  LPAR:'tk-delim', RPAR:'tk-delim', LBK:'tk-delim', RBK:'tk-delim',
  LBR:'tk-delim', RBR:'tk-delim', CMA:'tk-delim', COL:'tk-delim',
  SCO:'tk-delim', DOT:'tk-delim',
  ERR:'tk-err'
};

function showTokens(toks) {
  document.querySelector('#token-table tbody').innerHTML = toks.map((t,i) =>
    '<tr><td>'+(i+1)+'</td><td class="'+(TC[t.kind]||'')+'">'+t.kind+'</td><td>'+esc(t.lexeme)+'</td><td>'+t.line+':'+t.col+'</td></tr>'
  ).join('');
  const errs = toks.filter(t => t.kind === 'ERR').length;
  document.getElementById('scan-stats').innerHTML =
    '<span class="n">'+toks.length+'</span> tokens' +
    (errs ? ' · <span class="err">'+errs+' errors</span>' : '');
  const hl = document.getElementById('scan-highlighted');
  hl.innerHTML = toks.map(t =>
    '<span class="'+(TC[t.kind]||'')+'" title="'+t.kind+' @'+t.line+':'+t.col+'">'+esc(t.lexeme)+'</span>'
  ).join(' ');
  hl.classList.add('visible');
  document.getElementById('table-empty').classList.add('hidden');
}

document.getElementById('btn-scan-sample').addEventListener('click', () => {
  document.getElementById('scan-source').value = SAMPLE_SOURCE;
  showTokens(tokenize(SAMPLE_SOURCE));
});

document.getElementById('btn-scan-run').addEventListener('click', async () => {
  const src = document.getElementById('scan-source').value;
  if (!src.trim()) return;
  const btn = document.getElementById('btn-scan-run');
  btn.disabled = true; btn.textContent = 'Scanning...';
  try {
    if (apiAvailable) {
      const r = await fetch(API_URL + '/api/scan', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({source: src}),
        signal: AbortSignal.timeout(API_SCAN_TIMEOUT_MS)
      });
      if (r.ok) {
        const data = await r.json();
        showTokens(data.tokens || []);
        btn.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true"><polygon points="5 3 19 12 5 21 5 3"/></svg> Scan (API)';
        btn.disabled = false;
        return;
      }
    }
  } catch (_) {}
  showTokens(tokenize(src));
  btn.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" aria-hidden="true"><polygon points="5 3 19 12 5 21 5 3"/></svg> Scan (Local)';
  btn.disabled = false;
});

document.getElementById('scan-file-input').addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = () => {
    try { showTokens(JSON.parse(r.result)); }
    catch (err) { alert('Invalid JSON: ' + err.message); }
  };
  r.readAsText(f);
});
