// prelude for the recorder's actions: a drawn pointer that glides to what it clicks
// (the recording's only addition to the page; no desktop input is used)
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
let ptr = document.getElementById('__ptr');
if (!ptr) {
  ptr = document.createElement('div'); ptr.id = '__ptr';
  ptr.innerHTML = '<svg width="22" height="26" viewBox="0 0 22 26"><path d="M2 2 L2 21 L7 16.5 L10.5 24 L14 22.5 L10.6 15.2 L17 15.2 Z" fill="#fff" stroke="#111" stroke-width="1.6" stroke-linejoin="round"/></svg>';
  Object.assign(ptr.style, { position: 'fixed', left: '0', top: '0', zIndex: 2147483647, pointerEvents: 'none', filter: 'drop-shadow(0 2px 3px rgba(0,0,0,.5))', transform: 'translate(900px, 640px)' });
  document.body.appendChild(ptr);
  window.__px = 900; window.__py = 640;
}
const ease = (t) => t < .5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
async function moveTo(x, y, ms = 650) {
  const x0 = window.__px, y0 = window.__py, t0 = performance.now();
  await new Promise(res => { (function f() { const k = Math.min(1, (performance.now() - t0) / ms), e = ease(k);
    window.__px = x0 + (x - x0) * e; window.__py = y0 + (y - y0) * e; ptr.style.transform = `translate(${window.__px}px, ${window.__py}px)`;
    k < 1 ? requestAnimationFrame(f) : res(); })(); });
}
const el = (sel) => typeof sel === 'string' ? document.querySelector(sel) : sel;
async function waitFor(f, ms = 30000) { const t0 = Date.now(); while (Date.now() - t0 < ms) { try { const v = f(); if (v) return v; } catch (e) {} await sleep(80); } throw new Error('waitFor timeout ' + f); }
async function click(sel, ms) {
  const e = await waitFor(() => el(sel));
  e.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); await sleep(250);
  const r = e.getBoundingClientRect();
  await moveTo(r.left + Math.min(r.width * 0.5, 60), r.top + r.height * 0.55, ms);
  ptr.animate([{ transform: ptr.style.transform + ' scale(1)' }, { transform: ptr.style.transform + ' scale(.85)' }, { transform: ptr.style.transform + ' scale(1)' }], { duration: 180 });
  await sleep(120);
  e.click();
}
const byText = (sel, re) => [...document.querySelectorAll(sel)].find(e => re.test(e.textContent));
