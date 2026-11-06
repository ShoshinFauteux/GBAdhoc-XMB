/* GBAdhoc XMB desktop: the model. Design A's data.js, with its sample library and mocked
   network replaced by the real PSP, the real network and the real entry writer (HOST.call).
   The title cards, the ICON0 composition and the XMB preview are design A's own code. */
(function () {
  'use strict';
  const H = window.HOST;

  // ---------- live conditions (the design's scenario switches, now measured) ----------
  const scenario = { offline: false, rateLimited: false, rateReset: '', stickFull: false, pspDisconnected: true, noInstall: false };

  function esc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); }
  function uri(svg) { return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg).replace(/'/g, '%27'); }

  // ---------- the library: filled by scan() ----------
  const games = [];
  const state = {};
  const pictures = [];
  const customs = [];   // PSP/GAME/GBAdhocXMB/custom: {name, src, path}
  const storage = { device: '', id: '', letter: '', label: 'Memory Stick', totalMB: 0, freeMB: 0, isGo: false, kind: 'ms0',
    gbadhoc: 'PSP/GAME/GBAdhoc', gbadhocVersion: '', installTitle: '', folders: [], root: '' };
  let devices = [];
  const prefs = { preferOwn: true, region: 'USA, Europe first', heroes: true, keepArt: true, folders: [], device: '', theme: '', palette: '' };

  function freeMB() { return scenario.pspDisconnected ? 0 : storage.freeMB; }
  // the launcher (~130 KB) plus a typical ICON0 and PIC1
  const ENTRY_KB = 130 + 12 + 140;

  function iconSrc(g) { const c = state[g.id].icon; return c && c.kind !== 'none' ? c.src : null; }
  function bgSrc(g) { const c = state[g.id].bg; return c && c.kind !== 'none' ? c.src : null; }

  // A title card: the icon a game gets when no art exists at all. (design A, unchanged)
  function titleCard(g) {
    const s = state[g.id]; const sys = g.system;
    const name = s.title || g.shortTitle || g.file; let hsh = 0; for (let i = 0; i < name.length; i++) hsh = (hsh * 31 + name.charCodeAt(i)) >>> 0;
    const cs = getComputedStyle(document.documentElement), tok = (n, d) => { const v = parseFloat(cs.getPropertyValue(n)); return isNaN(v) ? d : v; };
    const fam = { GBA: [tok('--card-gba-hue', 270), tok('--card-gba-sat', 62), tok('--card-gba-light', 44)], GBC: [180, 55, 36], GB: [110, 24, 38] }[sys] || [tok('--card-other-hue', 232), 22, 38];
    const spread = tok('--card-spread', 110), hue = (fam[0] + (hsh % (spread + 1)) - spread / 2 + 360) % 360;   // a spread inside the family
    const light = fam[2] + ((hsh >> 5) % 5) * 4 - 6;
    const pal = [`hsl(${hue} ${fam[1]}% ${light}%)`, `hsl(${(hue + 14) % 360} ${fam[1] + 8}% ${Math.max(10, light - 24)}%)`, `hsl(${(hue + 40) % 360} 70% 84%)`];
    const motif = hsh % 3, mx = 96 + (hsh >> 3) % 40, my = 44 + (hsh >> 7) % 30;
    const deco = motif === 0 ? `<circle cx="${mx}" cy="${my}" r="34" fill="rgba(255,255,255,.07)"/><circle cx="${mx}" cy="${my}" r="18" fill="rgba(255,255,255,.06)"/>`
      : motif === 1 ? `<path d="M${mx - 60} 80 L${mx} 0 L${mx + 26} 0 L${mx - 34} 80Z" fill="rgba(255,255,255,.07)"/><path d="M${mx - 20} 80 L${mx + 40} 0 L${mx + 50} 0 L${mx - 10} 80Z" fill="rgba(255,255,255,.05)"/>`
      : `<circle cx="${mx}" cy="${my}" r="36" fill="none" stroke="rgba(255,255,255,.09)" stroke-width="6"/><circle cx="${mx}" cy="${my}" r="20" fill="none" stroke="rgba(255,255,255,.07)" stroke-width="6"/>`;
    const sysName = sys === 'GBA' ? 'GAME BOY ADVANCE' : sys === 'GBC' ? 'GAME BOY COLOR' : 'GAME BOY';
    const W = 122, est = (t, fs) => t.length * fs * 0.58;
    function greedy(title, fs, maxW) {
      const lines = []; let cur = '';
      for (let w of title.split(/\s+/)) {
        if (est(w, fs) > maxW && fs > 11) return null;
        while (est(w, fs) > maxW) { const n = Math.max(1, Math.floor(maxW / (fs * 0.58)) - 1); if (cur) { lines.push(cur); cur = ''; } lines.push(w.slice(0, n) + '-'); w = w.slice(n); }
        const t = cur ? cur + ' ' + w : w;
        if (est(t, fs) <= maxW) cur = t; else { if (cur) lines.push(cur); cur = w; }
      }
      if (cur) lines.push(cur);
      return lines;
    }
    function layout(title) {
      for (const fs of [19, 17, 16, 15, 14, 13, 12, 11]) {
        let lines = greedy(title, fs, W);
        if (!lines || lines.length > 3) continue;
        if (63 - (lines.length - 1) * fs * 1.06 - fs * 0.78 < 29) continue;
        if (lines.length > 1) {
          let lo = est(title, fs) / lines.length, hi = W;
          for (let i = 0; i < 12; i++) { const mid = (lo + hi) / 2; const l = greedy(title, fs, mid); if (l && l.length === lines.length) { hi = mid; lines = l; } else lo = mid; }
        }
        return { lines, fs };
      }
      return null;
    }
    const full = s.title || g.shortTitle || g.file.replace(/\.[^.]+$/, '');
    let fit = layout(full) || (g.shortTitle && g.shortTitle !== full ? layout(g.shortTitle) : null);
    if (!fit) { const l = layout(full.slice(0, 30) + '…') || { lines: [full.slice(0, 14) + '…'], fs: 11 }; fit = l; }
    const lh = fit.fs * 1.06; const base = 63;
    const text = fit.lines.map((l, i) => `<text x="12" y="${(base - (fit.lines.length - 1 - i) * lh).toFixed(1)}" font-family="Inter,Segoe UI,Arial,sans-serif" font-weight="800" font-size="${fit.fs}" letter-spacing="-.2" fill="#fff">${esc(l)}</text>`).join('');
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 144 80" width="144" height="80">
<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="${pal[0]}"/><stop offset="1" stop-color="${pal[1]}"/></linearGradient></defs>
<rect width="144" height="80" fill="url(#g)"/>
${deco}
<text x="12" y="19" font-family="Inter,Segoe UI,Arial,sans-serif" font-weight="800" font-size="7.5" letter-spacing="1.6" fill="${pal[2]}">${sysName}</text>
${text}
<rect x="0" y="77" width="144" height="3" fill="${pal[2]}" fill-opacity=".85"/>
</svg>`;
    return uri(svg);
  }

  // The ICON0 as it will be written, 144x80, as layered DOM. (design A)
  // ICON0 layouts (the PSP app's art.c draws the same five):
  //   side    box art left, the title right, over the art blurred (the default)
  //   tile    the blur frame: box art as large as it fits over a blurred, darkened copy
  //   smart   filled, anchored at the box's title band (top, or bottom when the logo is there)
  //   stretch filled after a mild 1.3x horizontal stretch, same anchor
  //   fit     fitted on a transparent icon
  const FITS = [['side', 'Box + title'], ['tile', 'Blur frame'], ['smart', 'Smart fill'], ['stretch', 'Stretch'], ['fit', 'Fitted']];
  const FIT_DEFAULT = 'side';
  const composed = new Map(), composing = new Set();
  let composedTimer = 0;
  function iconKey(g) { const s = state[g.id]; return [s.icon.kind, iconSrc(g) || '', bgSrc(g) || '', s.iconFit, s.title].join('|'); }
  function iconHTML(g, cls) {
    const s = state[g.id]; const ic = iconSrc(g); const bg = bgSrc(g);
    const mode = s.iconFit;
    if (s.icon.kind === 'entry') return `<div class="ic0 ${cls || ''}"><div class="l fill" style="background-image:url('${ic}')"></div></div>`;
    if (!hasIcon(g)) return `<div class="ic0 ${cls || ''} card"><div class="l fill" style="background-image:url('${titleCard(g)}')"></div></div>`;
    const key = iconKey(g), done = composed.get(key);   // null: it could not be composed
    if (done) return `<div class="ic0 ${cls || ''} ${mode === 'fit' ? 'transparent' : ''}"><div class="l fill" style="background-image:url('${done}')"></div></div>`;
    if (!composed.has(key) && !composing.has(key)) {
      composing.add(key);
      composeIcon(g).then(url => { composed.set(key, url); }).catch(() => { composed.set(key, null); }).finally(() => {
        composing.delete(key);
        clearTimeout(composedTimer);
        composedTimer = setTimeout(() => { if (window.XMB.onComposed) window.XMB.onComposed(); }, 60);
      });
    }
    // until it is composed: the box art fitted over its blur
    return `<div class="ic0 ${cls || ''}"><div class="l blur" style="background-image:url('${bg || ic}')"></div><div class="l fit" style="background-image:url('${ic || bg}')"></div></div>`;
  }
  function hasIcon(g) { const s = state[g.id]; return !!(iconSrc(g) || (s.iconFit !== 'fit' && bgSrc(g))); }
  function fitName(m) { const f = FITS.find(x => x[0] === m); return f ? f[1] : 'Box + title'; }
  function fitLabel(m, g) { if (g && !hasIcon(g)) return 'Title card (no art yet)'; return fitName(m); }
  function sourceLabel(c) {
    if (!c || c.kind === 'none') return 'None';
    if (c.kind === 'own') return "GBAdhoc's own";
    if (c.kind === 'scraped') return c.region ? 'Internet · ' + c.region : 'Internet';
    if (c.kind === 'picture') return c.name;
    if (c.kind === 'custom') return 'Your picture · ' + c.name;
    if (c.kind === 'entry') return 'In the XMB now';
    return c.kind;
  }

  function status(g) {
    if (g.orphan) return { key: 'missing', label: 'ROM missing', tone: 'bad' };
    if (!g.recognised && !g.entry) return { key: 'unknown', label: 'Not recognised', tone: 'warn' };
    if (g.entry && g.entry.romMoved) return { key: 'moved', label: 'ROM moved', tone: 'bad' };
    if (g.entry) return { key: 'inxmb', label: 'In XMB', tone: 'ok' };
    if (!g.recognised) return { key: 'unknown', label: 'Not recognised', tone: 'warn' };
    if (!hasIcon(g)) return { key: 'noart', label: 'No art yet', tone: 'muted' };
    return { key: 'ready', label: 'Ready', tone: 'info' };
  }

  // ---------- devices ----------
  function pickDevice(list) {
    devices = list || [];
    const keep = devices.find(d => d.id === storage.id) || devices.find(d => d.id === prefs.device && d.install)
      || devices.find(d => d.install && d.psp) || devices.find(d => d.install) || devices.find(d => d.psp) || devices[0];
    return keep || null;
  }
  function setDevice(d) {
    scenario.pspDisconnected = !d;
    if (!d) return;
    Object.assign(storage, {
      id: d.id, device: d.kind + ':', letter: d.letter || '', root: d.root, label: d.kind === 'ef0' ? 'Internal storage' : 'Memory Stick',
      totalMB: d.totalMB, freeMB: d.freeMB, isGo: !!d.go, kind: d.kind, gbadhoc: d.install || 'PSP/GAME/GBAdhoc',
      installTitle: d.installTitle || '', fake: !!d.fake, model: d.go ? 'PSP Go' : 'PSP'
    });
    scenario.noInstall = !d.install;
    scenario.stickFull = storage.freeMB * 1024 < ENTRY_KB;
    storage.folders = [storage.gbadhoc + '/roms', ...prefs.folders.filter(f => f)];
  }
  function where() { return storage.letter || (storage.fake ? 'the test folder' : storage.label); }

  // ---------- scanning ----------
  function fmtDate(t) {
    if (!t) return '';
    const d = new Date(t * 1000), now = new Date();
    if (d.toDateString() === now.toDateString()) return 'today';
    return d.getDate() + ' ' + d.toLocaleString('en-GB', { month: 'short' });
  }
  function entryOf(e, moved) {
    return { title: e.title, folder: e.folder, dir: e.dir, made: fmtDate(e.made), rom: e.rom, icon: e.iconUrl ? 'entry' : 'none',
      iconUrl: e.iconUrl || null, picUrl: e.picUrl || null, launcher: e.launcher, romMoved: !!moved || !e.romOk, romWas: e.rom };
  }
  function freshState(g, prev) {
    const s = {
      title: g.entry ? g.entry.title : (g.shortTitle || g.file.replace(/\.[^.]+$/, '')),
      icon: { kind: 'none' }, bg: { kind: 'none' }, iconFit: FIT_DEFAULT, fetched: null, marked: false
    };
    // the owner's own pictures in the custom folder come before anything else
    const ci = customFor(g, false), cb = customFor(g, true);
    if (ci) s.icon = { kind: 'custom', name: ci.name, src: ci.src, path: ci.path };
    if (cb) s.bg = { kind: 'custom', name: cb.name, src: cb.src, path: cb.path };
    if (!ci && g.ownBoxart && prefs.preferOwn) s.icon = { kind: 'own', src: g.ownBoxart, path: g.ownBoxartPath };
    if (!cb && g.ownHero && prefs.preferOwn) s.bg = { kind: 'own', src: g.ownHero, path: g.ownHeroPath };
    // an entry already in the XMB starts from what it has now
    if (!ci && !cb && g.entry && g.entry.iconUrl && s.icon.kind === 'none' && s.bg.kind === 'none') s.icon = { kind: 'entry', src: g.entry.iconUrl };
    if (!cb && g.entry && g.entry.picUrl && s.bg.kind === 'none') s.bg = { kind: 'entry', src: g.entry.picUrl };
    if (prev) { // keep choices made this session (rescan after a write)
      s.title = prev.title; s.iconFit = FITS.some(f => f[0] === prev.iconFit) ? prev.iconFit : FIT_DEFAULT; s.fetched = prev.fetched;
      if (prev.icon.kind !== 'none' && prev.icon.kind !== 'entry') s.icon = prev.icon;
      if (prev.bg.kind !== 'none' && prev.bg.kind !== 'entry') s.bg = prev.bg;
    }
    return s;
  }
  // <ROM stem>.png = icon, <ROM stem>.bg.png = background; names match without case
  function customFor(g, bg) {
    const stem = (g.stem || '').toLowerCase();
    if (!stem) return null;
    return customs.find(c => {
      const n = c.name.toLowerCase();
      if (!n.startsWith(stem)) return false;
      const rest = n.slice(stem.length);
      return bg ? /^\.bg\.[a-z]+$/.test(rest) : /^\.[a-z]+$/.test(rest);
    }) || null;
  }
  async function scan(onProgress) {
    const list = await H.call('devices');
    const d = pickDevice(list);
    setDevice(d);
    if (!d) { games.length = 0; return []; }
    const res = await H.call('scan', { dev: d.id, folders: prefs.folders }, (p) => onProgress && onProgress(p));
    customs.length = 0;
    try { (await H.call('custom', { dev: d.id })).forEach(p => customs.push({ name: p.name, src: p.url, path: p.path })); } catch (e) { /* none */ }
    const prevState = Object.assign({}, state);
    games.length = 0;
    for (const k of Object.keys(state)) delete state[k];
    for (const r of res.games) {
      const g = {
        id: r.id, path: r.path, file: r.file, stem: r.stem, folder: r.folder || 'roms', system: r.system, size: r.size,
        launchRom: r.launchRom, nointro: r.nointro, title: r.nointro || null, shortTitle: r.title, base: r.base, how: r.how,
        recognised: r.recognised, confidence: r.confidence, hack: r.hack, save: r.save, code: r.code,
        ownBoxart: r.ownBoxart || null, ownBoxartPath: r.ownBoxartPath || '', ownHero: r.ownHero || null, ownHeroPath: r.ownHeroPath || '',
        entry: r.entry ? entryOf(r.entry, r.romMoved) : null
      };
      games.push(g);
      state[g.id] = freshState(g, prevState[g.id]);
    }
    for (const e of res.orphans || []) {
      const g = { id: 'o_' + e.folder, orphan: true, path: '', file: e.rom.split('/').pop(), stem: e.rom.replace(/^.*\//, '').replace(/\.[^.]+$/, ''),
        folder: e.folder, system: /\.gbc$/i.test(e.rom) ? 'GBC' : /\.gb$/i.test(e.rom) ? 'GB' : 'GBA', size: 0, launchRom: e.rom,
        nointro: '', title: null, shortTitle: e.title, recognised: false, confidence: 0, save: false, entry: entryOf(e, true) };
      games.push(g);
      state[g.id] = freshState(g, null);
    }
    games.sort((a, b) => (state[a.id].title || '').localeCompare(state[b.id].title || '', 'en', { sensitivity: 'base' }));
    storage.recovered = res.recovered || [];
    pictures.length = 0;
    try { (await H.call('pictures', { dev: d.id })).forEach(p => pictures.push({ name: p.name, src: p.url, path: p.path })); } catch (e) { /* none */ }
    return games;
  }
  async function refreshDevice() {
    const list = await H.call('devices');
    const d = list.find(x => x.id === storage.id);
    if (d) setDevice(d);
    devices = list;
    return d;
  }

  // ---------- the network ----------
  async function netStatus() {
    try {
      const n = await H.call('netStatus');
      scenario.offline = !n.online;
      scenario.rateLimited = !!n.limited;
      scenario.rateReset = n.reset || '';
      return n;
    } catch (e) { return null; }
  }
  async function fetchArt(g, onStep) {
    const res = { box: [], hero: null, status: 'ok', steps: [] };
    const step = (s) => { if (!s.pending) res.steps.push(s); onStep && onStep(s, res); };
    try {
      const r = await H.call('fetch', { system: g.system, nointro: g.nointro || '', base: g.base || '', stem: g.stem, hack: !!g.hack,
        recognised: !!g.recognised, region: prefs.region, heroes: prefs.heroes }, step);
      res.status = r.status;
      res.steps = r.steps;
      res.box = r.box.map(c => ({ kind: 'scraped', region: c.region, conf: c.conf, below: !!c.below, src: c.url, path: c.path, name: c.name }));
      res.hero = r.hero ? { src: r.hero.url, path: r.hero.path, name: r.hero.name } : null;
      if (r.status === 'offline') scenario.offline = true; else if (r.status !== 'offline') scenario.offline = false;
      if (r.status === 'rate') { scenario.rateLimited = true; scenario.rateReset = r.rateReset || scenario.rateReset; }
    } catch (e) {
      res.status = 'offline';
      res.steps.push({ src: 'network', ok: false, msg: e.message });
    }
    return res;
  }
  function applyFetch(g, res) {
    const s = state[g.id]; s.fetched = res;
    const best = res.box.filter(c => !c.below).sort((a, b) => b.conf - a.conf)[0];
    if (best && (s.icon.kind === 'none' || s.icon.kind === 'entry')) s.icon = { kind: 'scraped', region: best.region, conf: best.conf, src: best.src, path: best.path };
    if (res.hero && (s.bg.kind === 'none' || s.bg.kind === 'entry')) s.bg = { kind: 'scraped', src: res.hero.src, path: res.hero.path };
  }

  // ---------- composing ICON0 and PIC1 (what the preview shows, pixel for pixel) ----------
  function loadImg(src) {
    return new Promise((ok, bad) => { const im = new Image(); im.onload = () => ok(im); im.onerror = () => bad(new Error('Cannot read the picture ' + src.slice(0, 80))); im.src = src; });
  }
  function drawFit(ctx, im, w, h, cover, inset) {
    const iw = im.naturalWidth || im.width, ih = im.naturalHeight || im.height;
    inset = inset || 0;
    const W = w + inset * 2, Hh = h + inset * 2;
    const sc = cover ? Math.max(W / iw, Hh / ih) : Math.min(W / iw, Hh / ih);
    const dw = iw * sc, dh = ih * sc;
    ctx.drawImage(im, -inset + (W - dw) / 2, -inset + (Hh - dh) / 2, dw, dh);
  }
  // the rect of an image that is not (nearly) transparent
  function opaqueRect(im) {
    const w = im.naturalWidth || im.width, h = im.naturalHeight || im.height;
    const cv = document.createElement('canvas'); cv.width = w; cv.height = h;
    const c = cv.getContext('2d', { willReadFrequently: true }); c.drawImage(im, 0, 0);
    const d = c.getImageData(0, 0, w, h).data;
    let x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (d[(y * w + x) * 4 + 3] > 24) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; y1 = y; }
    return x1 >= x0 ? { x0, y0, x1: x1 + 1, y1: y1 + 1 } : { x0: 0, y0: 0, x1: w, y1: h };
  }
  // the full-width part of r covering w:h, anchored at the top band (title); `stretch` widens first
  function coverTop(r, w, h, stretch) {
    const sw = r.x1 - r.x0, sh = r.y1 - r.y0, a = sw * stretch / sh, want = w / h, o = Object.assign({}, r);
    if (a < want) { const ch = sw * stretch / want; let y0 = r.y0 + sh * 0.03; if (y0 + ch > r.y1) y0 = r.y1 - ch; o.y0 = y0; o.y1 = y0 + ch; }
    else { const cw = sh * want / stretch; o.x0 = r.x0 + (sw - cw) / 2; o.x1 = o.x0 + cw; }
    return o;
  }
  // top or bottom: the window whose rows carry more saturation-weighted edge energy (art.c cover_title)
  function coverTitle(im, r, w, h, stretch) {
    const top = coverTop(r, w, h, stretch);
    if (top.y1 - top.y0 >= r.y1 - r.y0 - 1) return top;
    const bot = Object.assign({}, top); bot.y1 = r.y1 - (r.y1 - r.y0) * 0.03; bot.y0 = bot.y1 - (top.y1 - top.y0);
    const sw = 48, sh = Math.round(sw * (r.y1 - r.y0) / (r.x1 - r.x0));
    if (sh < 8) return top;
    const cv = document.createElement('canvas'); cv.width = sw; cv.height = sh;
    const c = cv.getContext('2d', { willReadFrequently: true }); c.imageSmoothingQuality = 'high';
    c.drawImage(im, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, 0, 0, sw, sh);
    const d = c.getImageData(0, 0, sw, sh).data, k = sh / (r.y1 - r.y0);
    let et = 0, eb = 0;
    for (let y = 1; y < sh - 1; y++) {
      let e = 0; const fy = r.y0 + (y + 0.5) / k;
      for (let x = 1; x < sw - 1; x++) {
        const i = (y * sw + x) * 4, q = i + 4, u = i + sw * 4, R = d[i], G = d[i + 1], B = d[i + 2];
        const mx = Math.max(R, G, B), mn = Math.min(R, G, B), sat = mx ? (mx - mn) / mx : 0;
        const gx = Math.abs(d[q] - R) + Math.abs(d[q + 1] - G) + Math.abs(d[q + 2] - B);
        const gy = Math.abs(d[u] - R) + Math.abs(d[u + 1] - G) + Math.abs(d[u + 2] - B);
        e += (gx + gy) * (0.4 + sat);
      }
      if (fy >= top.y0 && fy < top.y1) et += e;
      if (fy >= bot.y0 && fy < bot.y1) eb += e;
    }
    return eb > et * 1.2 ? bot : top;
  }
  function softBackdrop(ctx, im, dark) {
    ctx.save(); ctx.filter = `blur(5px) saturate(1.2) brightness(${dark})`;
    drawFit(ctx, im, 144, 80, true, 9); ctx.restore();
  }
  function boxWithShadow(ctx, im, r, x, y, w, h) {
    ctx.save(); ctx.shadowColor = 'rgba(0,0,0,.63)'; ctx.shadowBlur = 6; ctx.shadowOffsetY = 2;
    ctx.drawImage(im, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, x, y, w, h); ctx.restore();
  }
  // greedy wrap with canvas metrics; null when a word is wider than maxw
  function wrapTitle(ctx, t, maxw) {
    const lines = []; let cur = '';
    for (const w of t.split(/\s+/).filter(Boolean)) {
      if (ctx.measureText(w).width > maxw) return null;
      const n = cur ? cur + ' ' + w : w;
      if (ctx.measureText(n).width <= maxw) cur = n; else { lines.push(cur); cur = w; }
    }
    if (cur) lines.push(cur);
    return lines;
  }
  async function composeIcon(g) {
    const s = state[g.id]; const cv = document.createElement('canvas'); cv.width = 144; cv.height = 80;
    const ctx = cv.getContext('2d'); ctx.imageSmoothingQuality = 'high';
    const ic = iconSrc(g), bg = bgSrc(g), mode = s.iconFit;
    if (s.icon.kind === 'entry') { drawFit(ctx, await loadImg(ic), 144, 80, true); return cv.toDataURL('image/png'); }
    if (!hasIcon(g)) { drawFit(ctx, await loadImg(titleCard(g)), 144, 80, true); return cv.toDataURL('image/png'); }
    if (!ic) { drawFit(ctx, await loadImg(bg), 144, 80, true); return cv.toDataURL('image/png'); }
    const im = await loadImg(ic);
    // a finished 144x80 picture of the owner's is the icon as it is
    if (s.icon.kind === 'custom' && im.naturalWidth === 144 && im.naturalHeight === 80) { ctx.drawImage(im, 0, 0); return cv.toDataURL('image/png'); }
    const r = opaqueRect(im), rw = r.x1 - r.x0, rh = r.y1 - r.y0;
    if (mode === 'smart' || mode === 'stretch') {
      const c = coverTitle(im, r, 144, 80, mode === 'stretch' ? 1.3 : 1);
      ctx.drawImage(im, c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0, 0, 0, 144, 80);
      return cv.toDataURL('image/png');
    }
    if (mode === 'fit') {
      const sc = Math.min(144 / rw, 80 / rh), w = Math.round(rw * sc), h = Math.round(rh * sc);
      ctx.drawImage(im, r.x0, r.y0, rw, rh, Math.floor((144 - w) / 2), Math.floor((80 - h) / 2), w, h);
      return cv.toDataURL('image/png');
    }
    ctx.fillStyle = '#0a0c1e'; ctx.fillRect(0, 0, 144, 80);
    const back = bg ? await loadImg(bg).catch(() => im) : im;
    if (mode === 'side') {
      try { await document.fonts.load('800 15px Inter'); } catch (e) { /* the fallback face */ }
      const title = (s.title || '').trim();
      let bh = 70, fit = null, bw = 0;
      for (let k = 0; k < 2 && !fit; k++, bh -= 10) {
        bw = Math.min(76, Math.round(rw * bh / rh));
        const tx = 5 + bw + 7;
        for (const fs of [16, 15, 14, 13, 12, 11]) {
          ctx.font = `800 ${fs}px Inter, "Segoe UI", Arial, sans-serif`;
          const lines = wrapTitle(ctx, title, 144 - tx - 5);
          if (lines && lines.length <= 4 && lines.length * fs * 1.08 <= 72) { fit = { lines, fs, tx, bh }; break; }
        }
      }
      if (!fit) { bh = 70; bw = Math.min(76, Math.round(rw * bh / rh)); fit = { lines: [title.slice(0, 9)], fs: 11, tx: 5 + bw + 7, bh }; }
      bh = fit.bh; bw = Math.min(76, Math.round(rw * bh / rh)); if (bw === 76) bh = Math.round(rh * 76 / rw);
      softBackdrop(ctx, back, 0.42);
      boxWithShadow(ctx, im, r, 5, Math.floor((80 - bh) / 2), bw, bh);
      ctx.font = `800 ${fit.fs}px Inter, "Segoe UI", Arial, sans-serif`;
      const lh = fit.fs * 1.08, y0 = 40 - fit.lines.length * lh / 2 + fit.fs * 0.78;
      fit.lines.forEach((l, j) => {
        ctx.fillStyle = 'rgba(0,0,0,.59)'; ctx.fillText(l, fit.tx + 1, y0 + j * lh + 1);
        ctx.fillStyle = '#fff'; ctx.fillText(l, fit.tx, y0 + j * lh);
      });
      return cv.toDataURL('image/png');
    }
    // tile, the blur frame
    const sc = Math.min(140 / rw, 76 / rh), w = Math.round(rw * sc), h = Math.round(rh * sc);
    softBackdrop(ctx, back, 0.6);
    boxWithShadow(ctx, im, r, Math.floor((144 - w) / 2), Math.floor((80 - h) / 2), w, h);
    return cv.toDataURL('image/png');
  }
  async function composePic(g) {
    const bg = bgSrc(g); if (!bg) return null;
    const cv = document.createElement('canvas'); cv.width = 480; cv.height = 272;
    const ctx = cv.getContext('2d'); ctx.imageSmoothingQuality = 'high';
    drawFit(ctx, await loadImg(bg), 480, 272, true);
    return cv.toDataURL('image/png');
  }
  function folderFor(title) {
    return 'XMB_' + (title.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_|_$/g, '').slice(0, 24).replace(/_+$/, '') || 'game');
  }

  // ---------- the entry store ----------
  async function makeEntry(g, onStep, opts) {
    opts = opts || {};
    const s = state[g.id];
    const steps = [];
    const step = (x) => { steps.push(x); onStep && onStep(x, steps); };
    if (scenario.pspDisconnected) { step({ ok: false, code: 'disconnected', msg: 'The PSP was disconnected. Reconnect it in USB mode and run the queue again; nothing was half-written.' }); return { ok: false, code: 'disconnected', steps }; }
    if (freeMB() * 1024 < ENTRY_KB) { step({ ok: false, code: 'nospace', msg: `Not enough space on the ${storage.label}: an entry needs about ${ENTRY_KB} KB and ${fmtMB(freeMB())} is free.` }); return { ok: false, code: 'nospace', steps }; }
    if (g.entry && !opts.replace) { step({ ok: false, code: 'exists', msg: `An entry called "${g.entry.title}" already exists for this game.` }); return { ok: false, code: 'exists', steps }; }
    if (!g.launchRom) { step({ ok: false, code: 'bad', msg: 'This entry has no ROM on the stick to point at.' }); return { ok: false, code: 'bad', steps }; }
    const folder = g.entry ? g.entry.folder : folderFor(s.title);
    let icon, pic;
    try {
      icon = await composeIcon(g);
      pic = await composePic(g);
    } catch (e) { step({ ok: false, code: 'bad', msg: e.message }); return { ok: false, code: 'bad', steps }; }
    if (hasIcon(g)) step({ msg: 'ICON0.PNG made (144×80, ' + (s.icon.kind === 'entry' ? 'as it is now' : fitName(s.iconFit)) + ')' });
    else step({ msg: 'ICON0.PNG made (144×80 title card: no art for this game yet)', warn: true });
    if (pic) step({ msg: 'PIC1.PNG made (480×272)' });
    else step({ msg: 'No background chosen: the XMB wave shows through', warn: true });
    const keep = [];
    if (prefs.keepArt && g.stem) {
      if (s.icon.kind === 'scraped' && s.icon.path) keep.push({ src: s.icon.path, dst: 'boxart/' + g.stem + '.png' });
      if (s.bg.kind === 'scraped' && s.bg.path) keep.push({ src: s.bg.path, dst: 'hero/' + g.stem + '.png' });
    }
    try {
      const r = await H.call('make', { dev: storage.id, folder, title: s.title.trim(), rom: g.launchRom, gbadhoc: storage.gbadhoc,
        icon, pic, replace: !!g.entry || !!opts.replace, keep }, (p) => step({ msg: p.msg, ok: p.ok, warn: p.warn }));
      g.entry = { title: s.title.trim(), folder: r.folder, dir: r.dir, made: 'today', icon: 'entry', iconUrl: null, picUrl: null, rom: g.launchRom, launcher: true };
      if (g.orphan) g.orphan = false;
      storage.freeMB = Math.max(0, storage.freeMB - (r.kb + 32) / 1024);
      scenario.stickFull = storage.freeMB * 1024 < ENTRY_KB;
      for (const k of keep) { if (k.dst.startsWith('boxart/')) g.ownBoxart = g.ownBoxart || s.icon.src; else g.ownHero = g.ownHero || s.bg.src; }
      step({ ok: true, msg: `Done: "${s.title.trim()}" is in the XMB's Game column.` });
      return { ok: true, steps };
    } catch (e) {
      if (e.code === 'disconnected') scenario.pspDisconnected = true;
      step({ ok: false, code: e.code, msg: e.message });
      return { ok: false, code: e.code, steps };
    }
  }
  async function removeEntry(g) {
    await H.call('remove', { dev: storage.id, folder: g.entry.folder });
    g.entry = null;
    const s = state[g.id];
    if (s.icon.kind === 'entry') s.icon = { kind: 'none' };
    if (s.bg.kind === 'entry') s.bg = { kind: 'none' };
    storage.freeMB += 0.2;
    if (g.orphan) { const i = games.indexOf(g); if (i >= 0) games.splice(i, 1); }
    return true;
  }
  async function repairEntry(g) { const r = await makeEntry(g, null, { replace: true }); if (r.ok && g.entry) { delete g.entry.romMoved; } return r.ok; }

  function delay(ms) { return new Promise(r => setTimeout(r, ms)); }
  function fmtMB(mb) { return mb >= 1024 ? (mb / 1024).toFixed(1) + ' GB' : mb < 1 ? Math.round(mb * 1024) + ' KB' : Math.round(mb) + ' MB'; }

  // ---------- faithful XMB preview (design A) ----------
  const GLYPHS = {
    set: '<svg viewBox="0 0 24 24"><path d="M4 8h16v11H4z" fill="#fff"/><path d="M9 8V5h6v3" fill="none" stroke="#fff" stroke-width="2"/><path d="M4 12h16" stroke="#9aa" stroke-width="1.5"/></svg>',
    photo: '<svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="1.5" fill="#fff"/><path d="M5 17l4.5-5 3 3.5 2-2.5L19 17z" fill="#7a9"/><circle cx="16" cy="9" r="1.6" fill="#7a9"/></svg>',
    music: '<svg viewBox="0 0 24 24"><path d="M9 18.5V6l10-2v12.5" fill="none" stroke="#fff" stroke-width="2.2"/><circle cx="6.5" cy="18.5" r="2.8" fill="#fff"/><circle cx="16.5" cy="16.5" r="2.8" fill="#fff"/></svg>',
    video: '<svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="1.5" fill="#fff"/><path d="M6 5v14M18 5v14" stroke="#345" stroke-width="2" stroke-dasharray="2 2"/><path d="M10 9.5v5l4.5-2.5z" fill="#345"/></svg>',
    game: '<svg viewBox="0 0 24 24"><path d="M6 8h12a4 4 0 0 1 4 4.5l-.6 4A2.5 2.5 0 0 1 17 18l-2-2H9l-2 2a2.5 2.5 0 0 1-4.4-1.5l-.6-4A4 4 0 0 1 6 8z" fill="#fff"/><path d="M7.5 11v4M5.5 13h4" stroke="#345" stroke-width="1.6"/><circle cx="16" cy="12" r="1.1" fill="#345"/><circle cx="18.5" cy="14" r="1.1" fill="#345"/></svg>',
    net: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9" fill="none" stroke="#fff" stroke-width="2"/><path d="M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18" fill="none" stroke="#fff" stroke-width="1.6"/></svg>'
  };
  const CATS = [['set', 'Settings'], ['photo', 'Photo'], ['music', 'Music'], ['video', 'Video'], ['game', 'Game'], ['net', 'Network']];
  function neighboursFor(g) {
    // the real column: Memory Stick and GBAdhoc above, the next entries below
    const others = games.filter(x => x !== g && x.entry && !x.orphan).slice(0, 2);
    const n = [{ label: storage.isGo && storage.kind === 'ef0' ? 'System Storage' : 'Memory Stick™', glyph: 'ms' }, { label: 'GBAdhoc', glyph: 'gbadhoc' }, null];
    others.forEach(o => n.push({ label: state[o.id].title, game: o }));
    while (n.length < 5) n.push(undefined);
    return n;
  }
  function xmbPreview(g, opts) {
    opts = opts || {};
    const s = state[g.id];
    const bg = opts.noBg ? null : bgSrc(g);
    const neighbours = opts.neighbours || neighboursFor(g);
    const div = document.createElement('div');
    div.className = 'xmbp';
    if (opts.arrive) requestAnimationFrame(() => requestAnimationFrame(() => div.classList.add('arrive')));
    const catX = (i) => 96 + (i - 4) * (i < 4 ? 44 : 60);
    div.innerHTML = `
      <div class="xmbp-bg" style="${bg ? `background-image:url('${bg}')` : ''}"></div>
      ${bg ? '' : '<div class="xmbp-wave"><i></i></div>'}
      <div class="xmbp-cats">${CATS.map(([k, label], i) => `<span class="xmbp-cat ${k === 'game' ? 'on' : ''}" style="left:${catX(i)}px">${GLYPHS[k]}${k === 'game' ? `<b>${label}</b>` : ''}</span>`).join('')}</div>
      <div class="xmbp-clock">${opts.clock || new Date().toTimeString().slice(0, 5)}<span class="batt"></span></div>
      <div class="xmbp-col"></div>`;
    const col = div.querySelector('.xmbp-col');
    neighbours.forEach((n, i) => {
      if (n === undefined) return;
      const it = document.createElement('div');
      const isUs = n === null;
      it.className = 'xmbp-item p' + i + (isUs ? ' sel' : '');
      let icon;
      if (isUs) icon = iconHTML(g, 'xic');
      else if (n.game) icon = iconHTML(n.game, 'xic');
      else icon = `<div class="ic0 xic ${n.glyph}"></div>`;
      it.innerHTML = icon + `<div class="xmbp-label">${esc(isUs ? s.title : n.label)}</div>`;
      col.appendChild(it);
    });
    return div;
  }

  const previewCSS = `
.ic0{position:relative;width:144px;height:80px;overflow:hidden;border-radius:3px;background:var(--ink-0,#0a0c1e);transform-origin:0 0}
.ic0.transparent{background:transparent}
.ic0 .l{position:absolute;inset:0;background:center/contain no-repeat}
.ic0 .l.fill{background-size:cover}
.ic0 .l.blur{inset:-12px;background-size:cover;filter:blur(7px) saturate(1.25);opacity:.95}
.ic0.card .l{background-size:cover}
.ic0.empty{background:linear-gradient(135deg,var(--empty-a,#3a3f6e),var(--empty-b,#1b1e3c));border:1px solid rgba(255,255,255,.25);box-sizing:border-box}
.ic0.empty i{position:absolute;left:50%;top:50%;width:44px;height:26px;margin:-13px 0 0 -22px;border-radius:4px;border:2px dashed rgba(255,255,255,.45)}
.ic0.ms{background:linear-gradient(90deg,#1b3f8a,#2f6fd1);width:58px;height:80px;margin:0 43px}
.ic0.gbadhoc{background:var(--ink-0,#0a0c1e) url('assets/logo-dark@2x.png') center/72% no-repeat}
.xmbp{position:relative;width:480px;height:272px;overflow:hidden;background:#000;font-family:var(--font);color:#fff;border-radius:inherit}
.xmbp-bg{position:absolute;inset:0;background:var(--xmb-bg,#15153a) center/cover no-repeat}
.xmbp-wave{position:absolute;inset:0;background:linear-gradient(170deg,var(--xmb-wave-a,#4a3ab0),var(--xmb-wave-b,#1e1a6e) 50%,var(--xmb-wave-c,#0b0a2a))}
.xmbp-wave i{position:absolute;left:-20%;right:-20%;top:46%;height:60%;background:radial-gradient(55% 45% at 50% 0,rgba(255,255,255,.28),transparent 70%);transform:rotate(-5deg)}
.xmbp-cats{position:absolute;left:0;top:18px;height:50px;width:480px;z-index:2}
.xmbp-cat{position:absolute;top:10px;width:22px;height:22px;margin-left:-11px;opacity:.6;filter:drop-shadow(0 1px 2px rgba(0,0,0,.8))}
.xmbp-cat svg{display:block;width:100%;height:100%}
.xmbp-cat.on{top:0;width:34px;height:34px;margin-left:-17px;opacity:1}
.xmbp-cat.on b{position:absolute;left:50%;top:36px;transform:translateX(-50%);font-size:11px;font-weight:600;text-shadow:0 1px 2px rgba(0,0,0,.9);white-space:nowrap}
.xmbp-clock{position:absolute;right:14px;top:10px;font-size:13px;font-weight:600;text-shadow:0 1px 2px rgba(0,0,0,.8);display:flex;gap:8px;align-items:center;font-variant-numeric:tabular-nums}
.xmbp-clock .batt{display:inline-block;width:18px;height:9px;border:1.5px solid #fff;border-radius:2px;background:linear-gradient(90deg,#fff 70%,transparent 70%)}
.xmbp-col{position:absolute;left:0;top:0;width:480px;height:272px}
.xmbp-item{position:absolute;display:flex;align-items:center;gap:16px;transition:top .35s cubic-bezier(.2,.7,.2,1),transform .35s,opacity .35s}
.xmbp-item .ic0{flex:0 0 auto;filter:drop-shadow(0 2px 5px rgba(0,0,0,.7))}
.xmbp-item:not(.sel){transform:scale(.58);transform-origin:center center;opacity:.5}
.xmbp-item:not(.sel) .xmbp-label{display:none}
.xmbp-item.p0{left:24px;top:-6px;opacity:.28}.xmbp-item.p1{left:24px;top:50px}
.xmbp-item.sel{left:24px;top:110px}
.xmbp-item.p3{left:24px;top:196px}.xmbp-item.p4{left:24px;top:246px;opacity:.32}
.xmbp-label{font-size:16px;font-weight:600;text-shadow:0 1px 3px rgba(0,0,0,.9);max-width:280px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.xmbp.arrive .xmbp-bg{animation:xa-bg 2.4s ease-out both}
.xmbp.arrive .xmbp-item.sel .ic0{animation:xa-ic 1s .2s cubic-bezier(.2,.9,.2,1) both}
.xmbp.arrive .xmbp-item.sel .xmbp-label{animation:xa-lab .7s .6s cubic-bezier(.2,.9,.2,1) both}
@keyframes xa-bg{from{transform:scale(1.04)}to{transform:none}}
@keyframes xa-ic{0%{transform:scale(.94);filter:drop-shadow(0 2px 5px rgba(0,0,0,.7)) brightness(1.25)}55%{transform:scale(1.03)}100%{transform:none;filter:drop-shadow(0 2px 5px rgba(0,0,0,.7))}}
@keyframes xa-lab{from{transform:translateX(8px)}to{transform:none}}
@media (prefers-reduced-motion:reduce){.xmbp.arrive *{animation:none!important}}
`;
  function injectPreviewCSS() { if (document.getElementById('xmbp-css')) return; const st = document.createElement('style'); st.id = 'xmbp-css'; st.textContent = previewCSS; document.head.appendChild(st); }

  function savePrefs() { return H.call('store', { key: 'prefs', value: prefs }).catch(() => {}); }

  window.XMB = { scenario, games, state, pictures, customs, FITS, fitName, onComposed: null, storage, prefs, get devices() { return devices; }, freeMB, fmtMB, ENTRY_KB, titleCard, iconSrc, bgSrc,
    iconHTML, hasIcon, fitLabel, sourceLabel, status, fetchArt, applyFetch, makeEntry, removeEntry, repairEntry, scan, refreshDevice,
    pickDevice, setDevice, netStatus, delay, xmbPreview, injectPreviewCSS, savePrefs, where, folderFor, composeIcon, composePic, freshState };
})();
