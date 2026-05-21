/* ═══════════════════════════════════════════════════
   Shared infrastructure
   API health probe, nav-tabs, HTML escape
   ═══════════════════════════════════════════════════ */

/* ── API Config ───────────────────────────────────── */
const DEFAULT_API_URL = 'https://lines-eternal-ray-fighting.trycloudflare.com';
const LEGACY_API_URL = 'http://igw.netperf.cc:8080';
const API_HEALTH_TIMEOUT_MS = 8000;
const API_SCAN_TIMEOUT_MS = 12000;
const normalizeApiUrl = url => (url || '').replace(/\/+$/, '');
const savedApiUrl = normalizeApiUrl(localStorage.getItem('api_url'));
const API_CANDIDATES = savedApiUrl && savedApiUrl !== LEGACY_API_URL && savedApiUrl !== DEFAULT_API_URL
  ? [savedApiUrl, DEFAULT_API_URL]
  : [DEFAULT_API_URL];
let API_URL = API_CANDIDATES[0];
let apiAvailable = false;

async function checkAPI() {
  const badge = document.getElementById('api-badge');
  apiAvailable = false;
  for (const apiUrl of API_CANDIDATES) {
    try {
      const r = await fetch(apiUrl + '/api/health', { signal: AbortSignal.timeout(API_HEALTH_TIMEOUT_MS) });
      if (r.ok) {
        API_URL = apiUrl;
        apiAvailable = true;
        badge.textContent = 'API online';
        badge.className = 'api-badge online';
        return;
      }
    } catch (_) {}
  }
  badge.textContent = 'Local mode';
  badge.className = 'api-badge offline';
}
checkAPI();

/* ── Tabs ─────────────────────────────────────────── */
document.querySelectorAll('.nav-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.nav-tab').forEach(t => { t.classList.remove('active'); t.setAttribute('aria-selected', 'false'); });
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    tab.classList.add('active');
    tab.setAttribute('aria-selected', 'true');
    document.getElementById('view-' + tab.dataset.view).classList.add('active');
  });
});

/* ── HTML escape ──────────────────────────────────── */
function esc(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
