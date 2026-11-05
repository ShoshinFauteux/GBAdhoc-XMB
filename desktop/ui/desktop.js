/* GBAdhoc XMB — the desktop app (Windows). Design A's desktop.js, wired to the real PSP,
   network and entry writer through model.js and the native host. */
(function () {
  'use strict';
  const X = window.XMB; X.injectPreviewCSS();
  const H = window.HOST;
  const esc = s => String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const $ = (id) => document.getElementById(id);
  const win = $('win');
  const ui = { view: 'lib', filter: 'all', q: '', sel: new Set(), open: null, scanning: true, scanMsg: '', scanP: 0, scanFile: '' };
  const prefs = X.prefs;
  let hello = null, netInfo = null, cacheInfo = null, cardDirs = [], queueState = null, lastDevice = null;

  // ---------- banners ----------
  function banners() {
    const b = [];
    const sc = X.scenario;
    if (sc.pspDisconnected && lastDevice) b.push(`<div class="banner bad"><b>PSP disconnected.</b> ${esc(lastDevice.where)} is gone. Reconnect the PSP in USB mode (Settings › USB connection) and it will be picked up again; nothing is half-written.<span class="sp"></span><button class="b sm" data-act="retry">Look again</button></div>`);
    else if (!sc.pspDisconnected && sc.noInstall) b.push(`<div class="banner bad"><b>GBAdhoc is not on this ${esc(X.storage.label)}.</b> Entries open GBAdhoc, so install it in PSP/GAME/GBAdhoc first, then rescan.<span class="sp"></span><button class="b sm" data-act="rescan">Rescan</button></div>`);
    if (sc.offline) b.push(`<div class="banner warn"><b>Offline.</b> Entries can still be made with GBAdhoc's own art or your own pictures; box art and hero art cannot be fetched until you are back online.</div>`);
    else if (sc.rateLimited) b.push(`<div class="banner warn"><b>GitHub is rate-limiting this PC.</b> Art already downloaded still works${sc.rateReset ? '; new lookups resume at ' + esc(sc.rateReset) : ''}.</div>`);
    if (!sc.pspDisconnected && X.freeMB() * 1024 < X.ENTRY_KB) b.push(`<div class="banner bad"><b>${esc(X.storage.label)} is full.</b> An entry needs about ${X.ENTRY_KB} KB. Free some space on ${esc(X.where())} and the Make buttons come back.</div>`);
    if (queueState && queueState.paused && !running) {
      const left = queueState.ids.length - queueState.at;
      b.push(`<div class="banner warn"><b>A queue was interrupted.</b> ${left} ${left === 1 ? 'entry is' : 'entries are'} left, starting with "${esc(queueState.next || '')}". Nothing was half-written.<span class="sp"></span><button class="b sm" data-act="resume" ${connected() ? '' : 'disabled'}>Resume</button><button class="b sm" data-act="forget">Forget it</button></div>`);
    }
    $('banners').innerHTML = b.join('');
    const r = $('banners').querySelector('[data-act=retry]'); if (r) r.addEventListener('click', async () => { const d = await X.refreshDevice().catch(() => null); if (!d) { const list = await H.call('devices'); if (X.pickDevice(list)) { scan(); return; } toast('Still no PSP on USB. Check the cable and that the PSP is in USB mode.', 'warn'); } else scan(); });
    const rs = $('banners').querySelector('[data-act=rescan]'); if (rs) rs.addEventListener('click', () => scan());
    const re = $('banners').querySelector('[data-act=resume]'); if (re) re.addEventListener('click', () => resumeQueue());
    const fo = $('banners').querySelector('[data-act=forget]'); if (fo) fo.addEventListener('click', () => { queueState = null; saveQueue(); render(); });
  }
  function connected() { return !X.scenario.pspDisconnected; }
  function canWrite() { return connected() && !X.scenario.noInstall && X.freeMB() * 1024 >= X.ENTRY_KB; }

  function windowTitle() {
    const s = X.storage;
    H.call('title', { text: connected() ? `GBAdhoc XMB · ${s.label} on ${X.where()}` : 'GBAdhoc XMB · No PSP' }).catch(() => {});
  }

  // ---------- rail ----------
  function renderRail() {
    const free = X.freeMB(); const s = X.storage; const used = s.totalMB - free;
    $('dev').className = 'dev' + (connected() ? '' : ' off');
    const others = X.devices.filter(d => d.psp || d.install);
    const pick = others.length > 1 ? `<div class="pick">${others.map(d => `<span class="${d.id === s.id ? 'on' : ''}" data-dev="${esc(d.id)}" title="${esc(d.product || '')}">${esc(d.letter || d.id)} ${d.kind === 'ef0' ? 'internal' : 'stick'}</span>`).join('')}</div>` : '';
    $('dev').innerHTML = connected()
      ? `<div class="h"><i></i>${esc(s.model)} on ${esc(X.where())}</div><div class="l">${esc(s.label)} · ${s.installTitle ? esc(s.installTitle) : 'no GBAdhoc'}</div><div class="meter"><b class="${free * 1024 < X.ENTRY_KB ? 'full' : ''}" style="width:${s.totalMB ? Math.min(100, used / s.totalMB * 100) : 0}%"></b></div><div class="f ${free * 1024 < X.ENTRY_KB ? 'bad' : ''}"><span>${X.fmtMB(free)} free</span><span>${X.fmtMB(s.totalMB)}</span></div>${pick}`
      : `<div class="h"><i></i>No PSP</div><div class="l">Waiting for a PSP in USB mode…</div>${lastDevice ? `<div class="f"><span>Last seen: ${esc(lastDevice.where)}, ${esc(lastDevice.model)}</span></div>` : ''}`;
    $('dev').querySelectorAll('[data-dev]').forEach(sp => sp.addEventListener('click', () => { if (sp.dataset.dev === s.id) return; prefs.device = sp.dataset.dev; X.storage.id = ''; X.savePrefs(); scan(); }));
    const attnKeys = ['unknown', 'moved', 'noart', 'missing'];
    const counts = { lib: X.games.filter(g => !g.orphan).length, xmb: X.games.filter(g => g.entry).length, attn: X.games.filter(g => attnKeys.includes(X.status(g).key)).length };
    $('nav').innerHTML = `<div class="cap">Games</div>
      <a data-v="lib" class="${ui.view === 'lib' ? 'on' : ''}"><i class="lib"></i>Library<span class="n">${counts.lib}</span></a>
      <a data-v="xmb" class="${ui.view === 'xmb' ? 'on' : ''}"><i class="xmbi"></i>In the XMB<span class="n">${counts.xmb}</span></a>
      <a data-v="attn" class="${ui.view === 'attn' ? 'on' : ''}"><i class="attn"></i>Needs attention<span class="n">${counts.attn}</span></a>
      <div class="cap">App</div>
      <a data-v="art" class="${ui.view === 'art' ? 'on' : ''}"><i class="art"></i>Art sources</a>
      <a data-v="settings" class="${ui.view === 'settings' ? 'on' : ''}"><i class="set"></i>Settings</a>`;
    $('nav').querySelectorAll('a').forEach(a => a.addEventListener('click', () => { ui.view = a.dataset.v; ui.filter = 'all'; render(); if (ui.view === 'art') refreshNet(); if (ui.view === 'settings') refreshSettings(); }));
  }

  // ---------- main ----------
  const ATTN = ['unknown', 'moved', 'noart', 'missing'];
  const FILTERS = {
    lib: [['all', 'All', g => !g.orphan], ['new', 'Not in XMB', g => !g.entry && !g.orphan], ['inxmb', 'In XMB', g => !!g.entry && !g.orphan], ['noart', 'No art', g => X.status(g).key === 'noart'], ['unk', 'Not recognised', g => !g.recognised && !g.orphan]],
    xmb: [['all', 'All entries', g => !!g.entry], ['moved', 'ROM moved', g => g.entry && g.entry.romMoved && !g.orphan], ['missing', 'ROM missing', g => !!g.orphan]],
    attn: [['all', 'All', g => ATTN.includes(X.status(g).key)], ['noart', 'No art', g => X.status(g).key === 'noart'], ['moved', 'ROM moved', g => X.status(g).key === 'moved' || X.status(g).key === 'missing'], ['unk', 'Not recognised', g => X.status(g).key === 'unknown']]
  };
  function visible() {
    const fs = FILTERS[ui.view] || FILTERS.lib; const f = fs.find(x => x[0] === ui.filter) || fs[0];
    return X.games.filter(f[2]).filter(g => !ui.q || (X.state[g.id].title + ' ' + g.file).toLowerCase().includes(ui.q.toLowerCase()));
  }
  function render() {
    banners(); renderRail(); statusbar();
    document.body.classList.toggle('light', win.dataset.theme === 'light');
    const m = $('main');
    if (ui.scanning) { m.innerHTML = `<div class="scanning"><div class="arc"><i></i><i></i><i></i><i></i><i></i></div><div>${esc(ui.scanMsg || 'Scanning the Memory Stick…')}</div><div class="f">${esc(ui.scanFile || '')}</div><div class="pb"><b style="width:${ui.scanP}%"></b></div></div>`; return; }
    if (ui.view === 'settings') return renderSettings(m);
    if (ui.view === 'art') return renderArt(m);
    if (!connected() && !X.games.length) return renderWaiting(m);
    const titles = { lib: 'Library', xmb: 'In the XMB', attn: 'Needs attention' };
    const fs = FILTERS[ui.view]; const list = visible(); const nsel = ui.sel.size;
    m.innerHTML = `<div class="tools"><h1>${titles[ui.view]}</h1>
      <label class="search"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/></svg><input id="q" placeholder="Search titles and files" value="${esc(ui.q)}"></label>
      <div class="chips">${fs.map(f => `<span class="${ui.filter === f[0] ? 'on' : ''}" data-f="${f[0]}">${f[1]}<i>${X.games.filter(f[2]).length}</i></span>`).join('')}</div><span class="grow"></span>
      <button class="b" id="getall" ${!connected() || X.scenario.offline || running ? 'disabled' : ''}>${nsel ? 'Get art · ' + nsel : 'Get all art'}</button>
      <button class="b primary" id="makeall" ${!canWrite() || running ? 'disabled' : ''}>${nsel ? 'Make ' + nsel + ' entries' : 'Make all entries'}</button></div>
      <div class="content"><div class="grid-wrap">${list.length ? `<div class="grid">${list.map(cardHTML).join('')}</div>` : emptyHTML()}</div><div id="insp"></div></div>`;
    m.querySelector('#q').addEventListener('input', e => { ui.q = e.target.value; render(); const q = $('q'); q.focus(); q.setSelectionRange(q.value.length, q.value.length); });
    m.querySelectorAll('.chips span').forEach(c => c.addEventListener('click', () => { ui.filter = c.dataset.f; render(); }));
    m.querySelectorAll('.card').forEach(c => {
      c.addEventListener('click', (e) => { const id = c.dataset.id; if (e.target.closest('.chk')) { ui.sel.has(id) ? ui.sel.delete(id) : ui.sel.add(id); render(); return; } ui.open = ui.open === id ? null : id; render(); });
    });
    const es = m.querySelector('.empty [data-act=settings]'); if (es) es.addEventListener('click', () => { ui.view = 'settings'; render(); refreshSettings(); });
    m.querySelector('#getall').addEventListener('click', () => batch('art'));
    m.querySelector('#makeall').addEventListener('click', () => batch('make'));
    renderInspector();
  }
  function emptyHTML() {
    if (ui.q) return `<div class="empty"><h3>Nothing here</h3>No game matches "${esc(ui.q)}".</div>`;
    if (!X.games.length) return `<div class="empty"><h3>No ROMs on this ${esc(X.storage.label)}</h3>GBAdhoc's roms folder (${esc(X.storage.gbadhoc)}/roms) is empty. Copy GBA, GB or GBC games there, or add another folder to scan.<br><button class="b" data-act="settings">Folders to scan…</button></div>`;
    return `<div class="empty"><h3>Nothing here</h3>No games match this filter.</div>`;
  }
  function renderWaiting(m) {
    m.innerHTML = `<div class="waiting"><div class="psp"></div><h2>Connect your PSP</h2><p>GBAdhoc XMB finds the PSP by itself, whichever drive letter Windows gives it, on a PSP-1000, 2000, 3000 or a PSP Go.</p>
      <ol><li><b>1</b>Connect the PSP to this PC with a USB cable.</li><li><b>2</b>On the PSP, open Settings › USB connection.</li><li><b>3</b>Your games appear here.</li></ol></div>`;
  }
  function cardHTML(g) {
    const st = X.status(g), s = X.state[g.id];
    const meta = g.orphan ? `${g.system} · entry ${esc(g.entry.folder)}` : `${g.system} · ${esc(g.folder)} · ${g.size >= 1 ? Math.round(g.size * 10) / 10 + ' MB' : Math.max(1, Math.round(g.size * 1024)) + ' KB'}`;
    return `<div class="card ${ui.open === g.id ? 'on' : ''} ${ui.sel.has(g.id) ? 'sel' : ''}" data-id="${g.id}">
      <div class="tile">${X.iconHTML(g)}<span class="chk">${ui.sel.has(g.id) ? '✓' : ''}</span></div>
      <div class="meta"><h3>${esc(s.title)}</h3><div class="f">${meta}</div>
      <div class="s"><span class="sys">${g.system}</span>${g.orphan ? '<span class="no">ROM not on the stick</span>' : g.recognised ? `${g.save ? '<span class="ok">Save</span>' : '<span>No save</span>'}` : '<span class="no">No No-Intro match</span>'}<span class="badge ${st.tone}">${esc(st.label)}</span></div></div></div>`;
  }
  function statusbar() {
    const real = X.games.filter(g => !g.orphan); const rec = real.filter(g => g.recognised).length;
    const net = X.scenario.offline ? 'Offline' : X.scenario.rateLimited ? 'GitHub rate limit' + (X.scenario.rateReset ? ' · resets ' + esc(X.scenario.rateReset) : '') : 'libretro-thumbnails · GBAdhoc-heroart reachable';
    $('statusbar').innerHTML = `<span><b>${real.length}</b> ROMs in ${Math.max(1, X.storage.folders.length)} ${X.storage.folders.length > 1 ? 'folders' : 'folder'} · <b>${rec}</b> recognised · <b>${real.length - rec}</b> not</span><span><b>${X.games.filter(g => g.entry).length}</b> entries in the XMB</span><span class="sp"></span><span>${net}</span><span>${connected() ? esc(X.where()) + ' · ' + X.fmtMB(X.freeMB()) + ' free' : 'No PSP'}</span>`;
  }

  // ---------- inspector ----------
  function romRel(g) { return g.launchRom; }
  function renderInspector() {
    const host = $('insp'); if (!host) return;
    const g = X.games.find(x => x.id === ui.open); if (!g) { host.innerHTML = ''; host.className = ''; return; }
    const s = X.state[g.id];
    host.className = 'insp';
    let note = '';
    if (g.orphan) note = `<div class="note bad"><b>This entry's ROM is not on the stick.</b> launch.ini points at <b>${esc(g.entry.rom)}</b>, which is gone. Until it is back, the entry opens GBAdhoc's game list instead. Copy the ROM back and rescan, or remove the entry.</div>`;
    else if (g.entry && g.entry.romMoved) note = `<div class="note bad"><b>This entry's ROM moved.</b> launch.ini points at <b>${esc(g.entry.romWas)}</b>, which is gone; the ROM is now in <b>${esc(g.folder)}</b>. Update the entry to rewrite the path. Until then the entry opens GBAdhoc's game list instead.</div>`;
    else if (!g.recognised) note = `<div class="note warn"><b>Not recognised.</b> "${esc(g.file)}" did not match a No-Intro title, so box art and hero art cannot be looked up. Give it a title and use your own picture, or rename the ROM and rescan.</div>`;
    else if (g.hack) note = `<div class="note info"><b>ROM hack</b> of "${esc(g.base)}": it has that game's code but not its No-Intro dump. Hero art is looked up by ROM name in GBAdhoc-heroart; box art may not exist (the base game's is shown as nearest).</div>`;
    else if (g.how === 'fuzzy') note = `<div class="note info"><b>Matched by name</b> to "${esc(g.nointro)}" (${g.confidence}%). If that is the wrong game, rename the ROM to its No-Intro name and rescan.</div>`;
    const sugg = [...new Set([g.shortTitle, g.title, g.file.replace(/\.[^.]+$/, '').replace(/\s*\([^)]*\)/g, '').trim()].filter(Boolean))].filter(t => t !== s.title).slice(0, 3);
    const folder = g.entry ? g.entry.folder : X.folderFor(s.title);
    host.innerHTML = `<div class="sc">
      <div class="hd"><h2>${esc(s.title)}</h2><span class="x" title="Close">✕</span></div>
      ${note}
      <div class="pvbox"><span class="tag">XMB PREVIEW</span></div>
      <div class="pvcap"><span>Game column, as it will look</span><a id="pv-bg">${s.bg.kind === 'none' ? 'No background' : 'Toggle background'}</a></div>
      <div class="fld"><label>Title in the XMB</label><input type="text" id="title" maxlength="64" value="${esc(s.title)}"><div class="hint" id="title-hint">${titleHint(s.title, folder)}</div>${sugg.length ? `<div class="sg">${sugg.map(t => `<span data-t="${esc(t)}">${esc(t)}</span>`).join('')}</div>` : ''}</div>
      <div class="fld"><div class="rowh"><label>Box art</label><span class="lk" data-pick="icon">Change…</span></div>${choiceHTML(g, 'icon')}</div>
      <div class="fld"><div class="rowh"><label>Hero art · PIC1 480×272</label><span class="lk" data-pick="bg">Change…</span></div>${choiceHTML(g, 'bg')}</div>
      <div class="fld"><div class="rowh"><label>Icon · ICON0 144×80</label><span class="seg" id="fit"><b class="${s.iconFit === 'tile' ? 'on' : ''}" data-f="tile">Tile</b><b class="${s.iconFit === 'fit' ? 'on' : ''}" data-f="fit">Fit</b><b class="${s.iconFit === 'fill' ? 'on' : ''}" data-f="fill">Fill</b></span></div><div class="hint">${s.icon.kind === 'entry' ? 'The icon the entry has now. Choose box art to make a new one.' : !X.hasIcon(g) ? 'No art yet: the icon is a title card with the game\'s name. Add box art or a hero to change it.' : s.iconFit === 'tile' ? 'Hero behind, box art in front: a real 144×80 icon.' : s.iconFit === 'fit' ? 'Box art fitted on a transparent icon; the background shows at the sides.' : 'Box art cropped to fill the icon.'}</div></div>
      <div class="fld"><label>What gets written</label><div class="ini">PSP/GAME/${esc(folder)}/
  EBOOT.PBP     launcher, 130 KB
  ICON0.PNG     ${s.icon.kind === 'entry' ? '144×80 · as it is now' : !X.hasIcon(g) ? '144×80 title card (no art yet)' : '144×80 · ' + (s.iconFit === 'tile' ? 'tile of hero + box art' : s.iconFit === 'fit' ? 'box art fitted' : 'box art filled')}
  PIC1.PNG      ${s.bg.kind === 'none' ? '(none: XMB wave)' : '480×272 from ' + esc(X.sourceLabel(s.bg))}
  launch.ini    rom = ${esc(romRel(g))}
                gbadhoc = ${esc(X.storage.gbadhoc)}</div><div class="hint">The ROM is never copied. Saves, states and settings stay GBAdhoc's.</div></div>
    </div>
    <div class="acts">
      <button class="b" id="a-fetch" ${!g.recognised || X.scenario.offline || g.orphan ? 'disabled' : ''}>Get art</button>
      <button class="b ${g.entry ? 'primary' : 'ok'}" id="a-make" ${!canWrite() || g.orphan || running ? 'disabled' : ''}>${g.entry ? (g.entry.romMoved ? 'Update entry · fix path' : 'Update entry') : 'Make entry'}</button>
      ${g.entry ? `<button class="b danger full" id="a-remove" ${connected() && !running ? '' : 'disabled'}>Remove from the XMB</button>` : ''}
    </div>`;
    host.querySelector('.pvbox').insertBefore(X.xmbPreview(g), host.querySelector('.pvbox .tag'));
    host.querySelector('.x').addEventListener('click', () => { ui.open = null; render(); });
    const ti = host.querySelector('#title');
    ti.addEventListener('input', () => { s.title = ti.value; refreshQuiet(g); });
    host.querySelectorAll('.sg span').forEach(sp => sp.addEventListener('click', () => { s.title = sp.dataset.t; render(); }));
    host.querySelectorAll('#fit b').forEach(b => b.addEventListener('click', () => { s.iconFit = b.dataset.f; if (s.icon.kind === 'entry') s.icon = { kind: 'none' }; render(); }));
    let bgOff = false;
    host.querySelector('#pv-bg').addEventListener('click', () => { if (s.bg.kind === 'none') return; bgOff = !bgOff; host.querySelector('.xmbp-bg').style.opacity = bgOff ? 0 : 1; });
    host.querySelectorAll('[data-pick]').forEach(l => l.addEventListener('click', () => pickerSheet(g, l.dataset.pick)));
    host.querySelectorAll('.choice').forEach(c => c.addEventListener('click', () => pickerSheet(g, c.dataset.which)));
    host.querySelector('#a-fetch').addEventListener('click', () => fetchFlow(g));
    host.querySelector('#a-make').addEventListener('click', () => makeFlow(g));
    const rm = host.querySelector('#a-remove'); if (rm) rm.addEventListener('click', () => removeFlow(g));
  }
  function titleHint(t, folder) { return `${t.length}/64 · PARAM.SFO title; folder PSP/GAME/${esc(folder)}/`; }
  function refreshQuiet(g) {
    const host = $('insp'); const t = X.state[g.id].title;
    const lab = host.querySelector('.xmbp-item.sel .xmbp-label'); if (lab) lab.textContent = t;
    host.querySelector('h2').textContent = t; const card = document.querySelector(`.card[data-id="${g.id}"] h3`); if (card) card.textContent = t;
    const h = host.querySelector('#title-hint'); if (h) h.innerHTML = titleHint(t, g.entry ? g.entry.folder : X.folderFor(t));
  }
  function choiceHTML(g, which) {
    const s = X.state[g.id]; const v = which === 'icon' ? s.icon : s.bg;
    const src = which === 'icon' ? X.iconSrc(g) : X.bgSrc(g);
    const im = which === 'icon' ? (src ? `<div class="im"><div class="ic0"><div class="l ${v.kind === 'entry' ? 'fill' : 'fit'}" style="background-image:url('${src}')"></div></div></div>` : '<div class="im none">None</div>') : (src ? `<div class="im bg" style="background-image:url('${src}')"></div>` : '<div class="im none">None</div>');
    const where = v.kind === 'own' ? `GBAdhoc's ${which === 'icon' ? 'boxart' : 'hero'} folder on the PSP` : v.kind === 'scraped' ? (which === 'icon' ? 'libretro-thumbnails' + (v.conf ? ` · ${v.conf}% match` : '') : 'GBAdhoc-heroart') : v.kind === 'picture' ? 'Your own picture' : v.kind === 'entry' ? 'What the entry on the PSP has now' : which === 'icon' ? (s.fetched ? 'Nothing chosen' : g.recognised ? 'Nothing yet: Get art, or choose a picture' : 'Not recognised: choose a picture') : 'The XMB wave shows behind the column';
    return `<div class="choice" data-which="${which}">${im}<div class="t"><b>${esc(X.sourceLabel(v))}</b><small>${esc(where)}</small></div></div>`;
  }
  function pickerSheet(g, which) {
    const s = X.state[g.id]; const list = cands(g, which).filter(c => c.kind !== 'fetch');
    const d = dialog({ title: `${which === 'icon' ? 'Box art' : 'Hero art'} for ${s.title}`, sub: which === 'icon' ? "Goes into the 144x80 icon. Downloaded art is kept in GBAdhoc's boxart folder." : 'PIC1, 480x272, fills the screen behind the Game column.', body: '', buttons: [{ label: 'Get art from the internet', action: () => fetchFlow(g).then(() => pickerSheet(g, which)) }, { label: 'Close', cls: 'primary' }] });
    d.el.querySelector('.dlg').classList.add('picker');
    if (X.scenario.offline || !g.recognised) d.el.querySelector('.btns .b').disabled = true;
    const loading = !s.fetched && g.recognised && !X.scenario.offline && !g.orphan;
    const tiles = list.map((c, i) => `<div class="pk ${isCurrent(g, which, c) ? 'on' : ''} ${c.below ? 'below' : ''}" data-i="${i}"><div class="im ${c.kind === 'none' || c.kind === 'own-file' ? 'none' : ''} ${c.cover || which === 'bg' ? 'cover' : ''}" ${c.src ? `style="background-image:url('${c.src}')"` : ''}>${c.kind === 'none' ? 'None' : c.kind === 'own-file' ? 'Browse for a file…' : ''}${c.conf !== undefined ? `<span class="conf ${c.below ? 'below' : c.conf < 80 ? 'low' : ''}">${c.conf}%</span>` : ''}</div><div class="lab">${esc(c.label)}${isCurrent(g, which, c) ? '<span class="cur">IN USE</span>' : ''}</div><div class="meta">${esc(c.meta)}</div></div>`);
    if (loading) tiles.splice(list.findIndex(c => c.kind === 'picture' || c.kind === 'own-file'), 0, `<div class="pk"><div class="im loading"><i></i></div><div class="lab">Looking up…</div><div class="meta">${which === 'icon' ? 'libretro-thumbnails' : 'GBAdhoc-heroart'}</div></div>`);
    d.set(`<div class="grid2">${tiles.join('')}</div>`);
    if (loading) { X.fetchArt(g).then(res => { s.fetched = res; if (d.el.isConnected) { d.close(); pickerSheet(g, which); } }); }
    d.el.querySelectorAll('.pk[data-i]').forEach(pk => pk.addEventListener('click', async () => {
      const c = list[+pk.dataset.i];
      if (c.kind === 'own-file') {
        const f = await H.call('pickImage').catch(() => null);
        if (!f) return;
        const v = { kind: 'picture', name: f.name, src: f.url, path: f.path };
        if (which === 'icon') s.icon = v; else s.bg = v;
        d.close(); render(); return;
      }
      const v = c.kind === 'own' ? { kind: 'own', src: c.src, path: c.path } : c.kind === 'none' ? { kind: 'none' } : c.kind === 'entry' ? { kind: 'entry', src: c.src } : c.kind === 'picture' ? { kind: 'picture', name: c.name, src: c.src, path: c.path } : which === 'icon' ? { kind: 'scraped', region: c.region, conf: c.conf, src: c.src, path: c.path } : { kind: 'scraped', src: c.src, path: c.path };
      if (which === 'icon') s.icon = v; else s.bg = v; d.close(); render();
    }));
  }
  function cands(g, which) {
    const s = X.state[g.id]; const out = [];
    const own = which === 'icon' ? g.ownBoxart : g.ownHero;
    if (own) out.push({ kind: 'own', label: "GBAdhoc's own", meta: which === 'icon' ? 'boxart/' : 'hero/', src: own, path: which === 'icon' ? g.ownBoxartPath : g.ownHeroPath });
    if (g.entry && (which === 'icon' ? g.entry.iconUrl : g.entry.picUrl)) out.push({ kind: 'entry', label: 'In the XMB now', meta: 'PSP/GAME/' + g.entry.folder, src: which === 'icon' ? g.entry.iconUrl : g.entry.picUrl, cover: true });
    const f = s.fetched;
    if (f) {
      if (which === 'icon') f.box.forEach(c => out.push({ kind: 'scraped', label: c.region, meta: c.below ? 'below the match line' : 'libretro-thumbnails', conf: c.conf, below: c.below, src: c.src, path: c.path, region: c.region }));
      else if (f.hero) out.push({ kind: 'scraped', label: 'GBAdhoc-heroart', meta: f.hero.name + '.png', src: f.hero.src, path: f.hero.path });
    } else if (g.recognised) out.push({ kind: 'fetch', label: 'Internet', meta: X.scenario.offline ? 'offline' : 'look it up' });
    X.pictures.forEach(p => out.push({ kind: 'picture', label: p.name, meta: 'Pictures on the PSP', src: p.src, name: p.name, path: p.path, cover: true }));
    if (s.icon.kind === 'picture' && which === 'icon' && !X.pictures.some(p => p.name === s.icon.name)) out.push({ kind: 'picture', label: s.icon.name, meta: 'Your own picture', src: s.icon.src, name: s.icon.name, path: s.icon.path, cover: true });
    if (s.bg.kind === 'picture' && which === 'bg' && !X.pictures.some(p => p.name === s.bg.name)) out.push({ kind: 'picture', label: s.bg.name, meta: 'Your own picture', src: s.bg.src, name: s.bg.name, path: s.bg.path, cover: true });
    out.push({ kind: 'own-file', label: 'Browse…', meta: 'PNG, JPEG, BMP on this PC' });
    out.push({ kind: 'none', label: 'None', meta: which === 'icon' ? (s.bg.kind !== 'none' ? 'hero alone as icon' : 'title card as icon') : 'XMB wave' });
    return out;
  }
  function isCurrent(g, which, c) { const v = which === 'icon' ? X.state[g.id].icon : X.state[g.id].bg; if (c.kind !== v.kind) return false; if (c.kind === 'picture') return c.name === v.name; if (c.kind === 'scraped' && which === 'icon') return c.src === v.src; return true; }

  // ---------- dialogs ----------
  function dialog(opts) {
    const o = document.createElement('div'); o.className = 'ovl';
    o.innerHTML = `<div class="dlg"><h3>${esc(opts.title)}</h3>${opts.sub ? `<div class="sub">${esc(opts.sub)}</div>` : ''}<div class="bd">${opts.body || ''}</div><div class="btns">${(opts.buttons || []).map((b, i) => `<button class="b ${b.cls || ''}" data-i="${i}">${esc(b.label)}</button>`).join('')}</div></div>`;
    win.appendChild(o);
    o.querySelectorAll('.btns .b').forEach(b => b.addEventListener('click', () => { o.remove(); const a = opts.buttons[+b.dataset.i]; a.action && a.action(); }));
    o.addEventListener('click', (e) => { if (e.target === o && opts.dismiss !== false) o.remove(); });
    return { el: o, close: () => o.remove(), set: (html) => { o.querySelector('.bd').innerHTML = html; }, setButtons: (btns) => { opts.buttons = btns; o.querySelector('.btns').innerHTML = btns.map((b, i) => `<button class="b ${b.cls || ''}" data-i="${i}">${esc(b.label)}</button>`).join(''); o.querySelectorAll('.btns .b').forEach(b => b.addEventListener('click', () => { o.remove(); const a = btns[+b.dataset.i]; a.action && a.action(); })); }, title: (t) => { o.querySelector('h3').textContent = t; } };
  }
  function toast(msg, tone) { const t = document.createElement('div'); t.className = 'toast ' + (tone || ''); t.textContent = msg; $('toasts').appendChild(t); setTimeout(() => t.remove(), 3600); }
  const stepsHTML = (steps, live) => `<div class="steps">${steps.map(st => `<div class="step ${st.pending && live ? 'pend' : st.ok === false ? 'bad' : st.warn ? 'warn' : st.ok || !st.pending ? 'ok' : ''}"><i></i><span>${esc(st.msg)}</span></div>`).join('')}</div>`;

  async function fetchFlow(g) {
    const s = X.state[g.id];
    const d = dialog({ title: 'Getting art', sub: s.title, body: '<div class="steps"></div>', buttons: [], dismiss: false });
    const live = [];
    const res = await X.fetchArt(g, (st) => { const last = live[live.length - 1]; if (last && last.pending && (last.src === st.src || !st.pending)) live.pop(); live.push(st); d.set(stepsHTML(live, true)); });
    X.applyFetch(g, res);
    d.title(res.status === 'ok' ? 'Art found' : res.status === 'offline' ? 'Offline' : res.status === 'rate' ? 'Rate-limited by GitHub' : 'No art found');
    d.set(stepsHTML(res.steps.filter(st => !st.pending), false) + (res.box.length ? `<div class="thumbs">${res.box.map(c => `<div style="background-image:url('${c.src}');opacity:${c.below ? .5 : 1}"><span style="color:${c.below ? '#ff8aa8' : '#b7ea5c'}">${c.conf}%</span></div>`).join('')}</div>` : '') + (res.status === 'ok' ? '<p style="margin:10px 0 0">The best match is now the box art and the hero art the background. Change either from the panel.</p>' : ''));
    d.setButtons([{ label: res.status === 'rate' ? 'Finish the rest later' : 'Close', cls: 'primary', action: () => render() }]);
    render();
  }
  function makeFlow(g, replace) {
    const s = X.state[g.id];
    if (!s.title.trim()) { toast('Give the entry a title first', 'warn'); return; }
    if (X.freeMB() * 1024 < X.ENTRY_KB) { dialog({ title: 'Not enough space on the ' + X.storage.label, sub: X.fmtMB(X.freeMB()) + ' free on ' + X.where(), body: `An entry needs about ${X.ENTRY_KB} KB: the launcher EBOOT, ICON0 and PIC1. Free some space and try again.`, buttons: [{ label: 'OK', cls: 'primary' }] }); return; }
    if (g.entry && !replace && !g.entry.romMoved) { dialog({ title: 'Replace the existing entry?', sub: `PSP/GAME/${g.entry.folder}/ · made ${g.entry.made}`, body: `"${esc(g.entry.title)}" will be rewritten with the title, icon and background shown. The game's saves and settings are untouched: they belong to GBAdhoc, not to the entry.`, buttons: [{ label: 'Replace', cls: 'primary', action: () => makeFlow(g, true) }, { label: 'Keep it' }] }); return; }
    const d = dialog({ title: g.entry ? 'Updating entry' : 'Making entry', sub: X.where() + '/PSP/GAME/… · ' + s.title, body: '', buttons: [], dismiss: false });
    const steps = [];
    running = true;
    (async () => {
      if ((!X.hasIcon(g) || s.bg.kind === 'none') && !s.fetched && g.recognised && !X.scenario.offline) {
        steps.push({ msg: 'Looking up art first…', pending: true }); d.set(stepsHTML(steps, true));
        const r = await X.fetchArt(g); X.applyFetch(g, r); steps.pop();
        steps.push({ msg: r.status === 'ok' ? `Art found: ${r.box.filter(c => !c.below).length ? 'box art' : 'no box art'}, ${r.hero ? 'hero art' : 'no hero art'}` : r.status === 'none' ? 'No art found: a title card is used' : 'Art lookup failed (' + r.status + '): a title card is used', warn: r.status !== 'ok' }); d.set(stepsHTML(steps));
      }
      return X.makeEntry(g, (st) => { st.pending = false; steps.push(st); d.set(stepsHTML(steps)); }, { replace: true });
    })().then(async res => {
      running = false;
      d.title(res.ok ? 'Entry made' : 'Could not make the entry');
      if (res.ok) {
        await X.delay(500);
        d.el.querySelector('.dlg').classList.add('wide');
        d.title(`"${s.title}" is in the XMB`);
        d.set('<div class="arrival"></div><p style="margin:10px 0 0;font-size:12px;color:var(--text-3)">The Game column after the PSP leaves USB mode. Exit in the game returns here; Quit to game list opens GBAdhoc\'s list.</p>');
        d.el.querySelector('.arrival').appendChild(X.xmbPreview(g, { arrive: true }));
      }
      d.setButtons([{ label: res.ok ? 'Done' : 'Close', cls: 'primary', action: () => { render(); if (res.ok) quietRescan(); } }]);
      render();
    });
  }
  function removeFlow(g) {
    dialog({ title: 'Remove this entry?', sub: `PSP/GAME/${g.entry.folder}/`, body: `"${esc(g.entry.title)}" disappears from the XMB's Game column. The ROM, its save and GBAdhoc's art stay; you can make the entry again any time.`, buttons: [{ label: 'Remove', cls: 'danger', action: async () => { try { await X.removeEntry(g); toast('Entry removed', 'ok'); if (ui.open === g.id && g.orphan === undefined) ui.open = null; } catch (e) { toast(e.message, 'bad'); } render(); } }, { label: 'Keep it' }] });
  }

  // ---------- batch (queue drawer), resumable after an unplug ----------
  let running = false;
  function saveQueue() { H.call('store', { key: 'queue', value: queueState }).catch(() => {}); }
  function batch(mode) {
    const chosen = ui.sel.size ? X.games.filter(g => ui.sel.has(g.id) && !g.orphan) : (mode === 'make' ? X.games.filter(g => g.recognised && !g.entry && !g.orphan) : X.games.filter(g => g.recognised && !g.orphan));
    if (!chosen.length) { toast(mode === 'make' ? 'Every recognised game is already in the XMB' : 'Nothing to fetch', 'ok'); return; }
    const noArt = chosen.filter(g => !X.hasIcon(g) && !X.state[g.id].fetched).length;
    const body = mode === 'make'
      ? `${chosen.map(g => esc(X.state[g.id].title)).join(', ')}.<br><br>${noArt ? `${noArt} ${noArt === 1 ? 'game has' : 'games have'} no art yet; box art and hero art are fetched first${X.scenario.offline ? ' (offline now, so they get a title card as the icon)' : ', and any still without art get a title card'}. ` : ''}About ${Math.round(chosen.length * X.ENTRY_KB)} KB will be written to ${esc(X.where())}. Entries that already exist are skipped.`
      : `Box art from libretro-thumbnails and hero art from GBAdhoc-heroart for ${chosen.length} games. Games with art already keep it unless a better match is found.`;
    dialog({ title: mode === 'make' ? `Make ${chosen.length} entries?` : `Get art for ${chosen.length} games?`, body, buttons: [{ label: mode === 'make' ? 'Make them' : 'Get art', cls: 'primary', action: () => runQueue(chosen, mode, 0, ui.sel.size > 0) }, { label: 'Not now' }] });
  }
  async function resumeQueue() {
    if (!queueState) return;
    // the write said "disconnected": is the PSP there again (same storage)?
    const d = await X.refreshDevice().catch(() => null);
    if (!d) { toast('Still no PSP on USB. Check the cable and that the PSP is in USB mode.', 'warn'); return; }
    X.scenario.pspDisconnected = false;
    const list = queueState.ids.map(id => X.games.find(g => g.id === id)).filter(Boolean);
    const at = list.findIndex(g => g.id === queueState.ids[queueState.at]);
    runQueue(list, queueState.mode, Math.max(0, at), queueState.chosen);
  }
  async function runQueue(list, mode, startAt, chosenOnly) {
    const dr = $('drawer'); dr.classList.add('open'); running = true; let stop = false, unplugged = null;
    const items = list.map((g, i) => ({ g, st: i < startAt ? 'ok' : '', msg: i < startAt ? 'Done earlier' : 'Waiting' }));
    queueState = { mode, ids: list.map(g => g.id), at: startAt || 0, chosen: !!chosenOnly, paused: false, dev: X.storage.id, next: '' };
    saveQueue(); render();
    const paint = (done) => {
      const n = items.filter(i => i.st === 'ok').length, bad = items.filter(i => i.st === 'bad').length, warn = items.filter(i => i.st === 'warn').length;
      const head = unplugged ? `Stopped: the PSP was disconnected at "${esc(X.state[unplugged.g.id].title)}"` : done ? (mode === 'make' ? `${n + warn} entries made` : `Art fetched for ${n + warn} games`) + (bad ? `, ${bad} failed` : '') + (stop ? ', stopped' : '') : (mode === 'make' ? 'Making entries' : 'Getting art') + `… ${n + bad + warn} of ${items.length}`;
      const sub = unplugged ? 'Nothing is half-written. Reconnect the PSP and resume from this game.' : done ? (mode === 'make' ? 'Entries appear in the XMB after the PSP is disconnected from USB.' : "Downloaded art is kept in GBAdhoc's boxart and hero folders when an entry is made.") : (mode === 'make' ? 'Nothing is half-written if you stop.' : 'libretro-thumbnails first, then GBAdhoc-heroart.');
      const btn = unplugged ? `<button class="b sm primary" id="q-resume" ${connected() ? '' : 'disabled'}>${connected() ? 'Resume' : 'Waiting for the PSP…'}</button><button class="b sm" id="q-close">Close</button>` : done ? '<button class="b sm primary" id="q-close">Close</button>' : '<button class="b sm" id="q-stop">Stop after this one</button>';
      dr.innerHTML = `<div class="dh"><h3>${head}</h3><span class="sub">${sub}</span><span class="sp"></span>${btn}</div>
        <div class="bar2"><b style="width:${(n + bad + warn) / items.length * 100}%"></b></div>
        <div class="list">${items.map(i => `<div class="qr ${i.st}">${i.st === 'pend' ? '<i class="sp"></i>' : `<span class="im">${X.iconHTML(i.g)}</span>`}<span class="t">${esc(X.state[i.g.id].title)}</span><span class="st">${esc(i.msg)}</span></div>`).join('')}</div>`;
      const s = dr.querySelector('#q-stop'); if (s) s.addEventListener('click', () => { stop = true; s.textContent = 'Stopping…'; });
      const c = dr.querySelector('#q-close'); if (c) c.addEventListener('click', () => { dr.classList.remove('open'); dr.repaint = null; render(); });
      const r = dr.querySelector('#q-resume'); if (r) r.addEventListener('click', () => { dr.repaint = null; resumeQueue(); });
    };
    dr.repaint = null;
    paint(false);
    for (let k = 0; k < items.length; k++) {
      const it = items[k];
      if (k < startAt) continue;
      if (stop || unplugged) { it.msg = unplugged ? 'Waiting' : 'Skipped'; continue; }
      queueState.at = k; queueState.next = X.state[it.g.id].title; saveQueue();
      const s = X.state[it.g.id];
      if (mode === 'art' || ((!X.hasIcon(it.g) || s.bg.kind === 'none') && !s.fetched && it.g.recognised && !X.scenario.offline)) {
        it.st = 'pend'; it.msg = 'Getting art…'; paint(false);
        const res = await X.fetchArt(it.g); X.applyFetch(it.g, res);
        if (mode === 'art') { it.st = res.status === 'ok' ? 'ok' : res.status === 'none' ? 'warn' : 'bad'; it.msg = res.status === 'ok' ? `${res.box.filter(c => !c.below).length} box art · ${res.hero ? 'hero found' : 'no hero'}` : res.status === 'offline' ? 'Offline' : res.status === 'rate' ? 'Rate-limited' + (res.rateReset ? ' until ' + res.rateReset : '') : 'Nothing found'; render(); paint(false); if (res.status === 'rate' || res.status === 'offline') { stop = true; } continue; }
        if (res.status !== 'ok') it.note = res.status === 'offline' ? 'offline' : res.status === 'rate' ? 'rate-limited' : 'no art';
      }
      if (mode === 'make') {
        if (!connected()) { it.st = 'bad'; it.msg = 'PSP disconnected'; unplugged = it; paint(false); continue; }
        it.st = 'pend'; it.msg = 'Writing…'; paint(false);
        if (it.g.entry && !chosenOnly) { it.st = 'ok'; it.msg = 'Already in the XMB, skipped'; paint(false); continue; }
        const r = await X.makeEntry(it.g, null, { replace: true });
        if (r.ok) { it.st = X.hasIcon(it.g) ? 'ok' : 'warn'; it.msg = X.hasIcon(it.g) ? 'Made' : `Made with a title card (${it.note || 'no art'})`; }
        else if (r.code === 'disconnected') { it.st = 'bad'; it.msg = 'PSP disconnected'; unplugged = it; X.scenario.pspDisconnected = true; }
        else { it.st = 'bad'; it.msg = r.code === 'nospace' ? 'Not enough space' : r.code === 'exists' ? 'Another folder has this name' : (r.steps[r.steps.length - 1] || {}).msg || r.code; if (r.code === 'nospace') stop = true; }
        render(); paint(false);
      }
    }
    running = false;
    if (unplugged) {
      queueState.at = items.indexOf(unplugged); queueState.next = X.state[unplugged.g.id].title; queueState.paused = true; saveQueue();
      dr.repaint = () => paint(false);
      // look for it again every 2 s, so Resume lights up when it is back
      const poll = setInterval(async () => {
        if (dr.repaint === null || !queueState || !queueState.paused || running) { clearInterval(poll); return; }
        const was = X.scenario.pspDisconnected; const d = await X.refreshDevice().catch(() => null);
        if (d && was) { X.scenario.pspDisconnected = false; render(); if (dr.repaint) dr.repaint(); }
      }, 2000);
      render(); paint(false);
      return;
    }
    queueState = null; saveQueue();
    ui.sel.clear(); render(); paint(true);
    if (mode === 'make') quietRescan();
  }

  // ---------- settings + art sources ----------
  async function refreshSettings() {
    try { cacheInfo = await H.call('cacheStats'); } catch (e) { cacheInfo = null; }
    try { cardDirs = connected() ? await H.call('dirs', { dev: X.storage.id }) : []; } catch (e) { cardDirs = []; }
    if (ui.view === 'settings') render();
  }
  async function refreshNet() { netInfo = await X.netStatus(); render(); }
  function renderSettings(m) {
    const folders = [X.storage.gbadhoc + '/roms', ...prefs.folders];
    const countIn = (f) => X.games.filter(g => !g.orphan && (g.path || '').toLowerCase().startsWith((X.storage.root + f + '/').toLowerCase())).length;
    const options = cardDirs.filter(f => !folders.some(x => x.toLowerCase() === f.toLowerCase()));
    m.innerHTML = `<div class="tools"><h1>Settings</h1></div><div class="page">
      <div class="sec"><h3>Folders to scan on ${esc(X.where())}</h3>${folders.map((f, i) => `<div class="srow"><div class="t"><b>${esc(f)}</b><small>${countIn(f)} ROMs</small></div>${i === 0 ? '<span class="pill ok">GBAdhoc</span>' : `<button class="b sm" data-rm="${esc(f)}">Remove</button>`}</div>`).join('')}
        <div class="srow"><div class="t"><b>Add a folder</b><small class="p">ROMs outside GBAdhoc's roms folder get an absolute ${esc(X.storage.kind)}: path in launch.ini</small></div><select class="sel" id="addf" ${options.length ? '' : 'disabled'}>${options.length ? options.map(f => `<option>${esc(f)}</option>`).join('') : '<option>No other folders</option>'}</select><button class="b sm" id="addb" ${options.length ? '' : 'disabled'}>Add</button><button class="b sm" id="rescan" ${connected() ? '' : 'disabled'}>Rescan</button></div></div>
      <div class="sec"><h3>Art</h3>
        <div class="srow"><div class="t"><b>Prefer GBAdhoc's own art</b><small class="p">Use the boxart and hero folders before asking the internet</small></div><span class="tog ${prefs.preferOwn ? 'on' : ''}" data-p="preferOwn"></span></div>
        <div class="srow"><div class="t"><b>Box art region</b><small class="p">Which No-Intro region wins when several match</small></div><select class="sel" id="region">${['USA, Europe first', 'Europe first', 'Japan first'].map(r => `<option ${prefs.region === r ? 'selected' : ''}>${r}</option>`).join('')}</select></div>
        <div class="srow"><div class="t"><b>Fetch hero art</b><small>github.com/ShoshinFauteux/GBAdhoc-heroart</small></div><span class="tog ${prefs.heroes ? 'on' : ''}" data-p="heroes"></span></div>
        <div class="srow"><div class="t"><b>Keep downloaded art in GBAdhoc's folders</b><small class="p">So GBAdhoc's own game list shows it too; existing files are never replaced</small></div><span class="tog ${prefs.keepArt ? 'on' : ''}" data-p="keepArt"></span></div>
        <div class="srow"><div class="t"><b>Downloaded art cache</b><small>${cacheInfo ? esc(cacheInfo.dir.replace(/^.*\/(GBAdhoc XMB)/, '%LOCALAPPDATA%/$1')) + ' · ' + cacheInfo.files + ' files · ' + cacheInfo.mb + ' MB' : '%LOCALAPPDATA%/GBAdhoc XMB/cache'}</small></div><button class="b sm" id="clear">Clear</button></div></div>
      <div class="sec"><h3>PSP</h3>
        <div class="srow"><div class="t"><b>GBAdhoc</b><small>${connected() ? esc(X.where()) + '/' + esc(X.storage.gbadhoc) + (X.storage.installTitle ? ' · ' + esc(X.storage.installTitle) : '') : 'No PSP connected'}</small></div><span class="pill ${connected() && !X.scenario.noInstall ? 'ok' : 'bad'}">${connected() ? (X.scenario.noInstall ? 'Not found' : 'Found') : 'No PSP'}</span></div>
        <div class="srow"><div class="t"><b>Launcher</b><small class="p">Every entry gets the same 130 KB launcher; updating GBAdhoc updates every entry</small></div><span class="pill">1.0 · built in</span></div>
        <div class="srow"><div class="t"><b>Written straight through to the stick</b><small class="p">Every file is flushed as it is written and checked after, and an entry only appears in PSP/GAME once it is whole, so pulling the cable never leaves half an entry</small></div><span class="pill ok">Always</span></div></div>
      <div class="sec"><h3>About</h3><div class="srow"><div class="t"><b>GBAdhoc XMB ${esc(hello ? hello.version : '0.1.0-beta.1')}</b><small class="p">A sibling of GBAdhoc. Box art: libretro-thumbnails (Named_Boxarts). Hero art: GBAdhoc-heroart. No-Intro names: libretro-database. WebView2 ${esc(hello ? hello.runtime : '')}.</small></div></div></div></div>`;
    m.querySelectorAll('.tog[data-p]').forEach(t => t.addEventListener('click', () => { prefs[t.dataset.p] = !prefs[t.dataset.p]; X.savePrefs(); render(); }));
    m.querySelector('#region').addEventListener('change', e => { prefs.region = e.target.value; X.savePrefs(); });
    m.querySelectorAll('[data-rm]').forEach(b => b.addEventListener('click', () => { prefs.folders = prefs.folders.filter(f => f !== b.dataset.rm); X.savePrefs(); toast('Folder removed; rescan to update the library', 'ok'); render(); }));
    m.querySelector('#addb').addEventListener('click', () => { const f = m.querySelector('#addf').value; if (!f) return; prefs.folders.push(f); X.savePrefs(); toast(`Added ${f}. Rescan to pick up its ROMs.`, 'ok'); render(); });
    m.querySelector('#rescan').addEventListener('click', () => scan());
    m.querySelector('#clear').addEventListener('click', async () => { try { cacheInfo = await H.call('cacheClear'); X.games.forEach(g => { X.state[g.id].fetched = null; }); toast('Downloaded art cleared', 'ok'); } catch (e) { toast(e.message, 'bad'); } render(); });
  }
  function renderArt(m) {
    const rate = X.scenario.rateLimited, off = X.scenario.offline;
    const pill = off ? '<span class="pill bad">Offline</span>' : rate ? `<span class="pill warn">Rate-limited${X.scenario.rateReset ? ' · resets ' + esc(X.scenario.rateReset) : ''}</span>` : netInfo ? '<span class="pill ok">Reachable</span>' : '<span class="pill">Checking…</span>';
    const own = X.games.filter(g => g.ownBoxart).length, ownH = X.games.filter(g => g.ownHero).length;
    m.innerHTML = `<div class="tools"><h1>Art sources</h1></div><div class="page">
      <div class="sec"><h3>Box art</h3><div class="srow"><div class="t"><b>libretro-thumbnails · Named_Boxarts</b><small>github.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance, …Game_Boy_Color, …Game_Boy</small></div>${pill}</div>
        <div class="srow"><div class="t"><b>Matching</b><small class="p">ROM → No-Intro title (checksum, file name or game code, from libretro-database) → thumbnail file. Below 80% is "below the match line": shown, never chosen for you.</small></div></div>
        ${netInfo && netInfo.remaining !== undefined ? `<div class="srow"><div class="t"><b>GitHub requests left this hour</b><small class="p">Lists are cached for a week and art for good, so a whole library costs a handful of requests. If GitHub says no, thumbnails.libretro.com is used instead.</small></div><span class="pill ${netInfo.remaining > 5 ? 'ok' : 'warn'}">${netInfo.remaining} of ${netInfo.limit}</span></div>` : ''}</div>
      <div class="sec"><h3>Hero art</h3><div class="srow"><div class="t"><b>GBAdhoc-heroart</b><small>github.com/ShoshinFauteux/GBAdhoc-heroart · files named after the ROM</small></div>${pill}</div>
        <div class="srow"><div class="t"><b>Contribute</b><small class="p">A 480×272 PNG named like the ROM, by pull request. Hero art for ROM hacks lives here too. <span class="lnk" id="contrib">Open the repository</span></small></div></div></div>
      <div class="sec"><h3>On the PSP</h3><div class="srow"><div class="t"><b>${esc(X.where())}/${esc(X.storage.gbadhoc)}/boxart</b><small>${own} ${own === 1 ? 'game has' : 'games have'} art here</small></div><span class="pill">Used first</span></div><div class="srow"><div class="t"><b>${esc(X.where())}/${esc(X.storage.gbadhoc)}/hero</b><small>${ownH} ${ownH === 1 ? 'game has' : 'games have'} art here</small></div><span class="pill">Used first</span></div>
        <div class="srow"><div class="t"><b>Downloaded art is kept there too</b><small class="p">so GBAdhoc's own Shelf and Marquee browsers show it as well</small></div></div></div></div>`;
    const c = m.querySelector('#contrib'); if (c) c.addEventListener('click', () => H.call('openUrl', { url: 'https://github.com/ShoshinFauteux/GBAdhoc-heroart' }));
  }

  // ---------- scan ----------
  async function scan() {
    ui.scanning = true; ui.scanP = 5; ui.scanMsg = 'Looking for the PSP…'; ui.scanFile = ''; render();
    try {
      await X.scan((p) => { if (p.msg) ui.scanMsg = p.msg; if (p.file) ui.scanFile = p.file; if (p.p !== undefined) ui.scanP = p.p; render(); });
    } catch (e) {
      toast(e.message, 'bad');
    }
    if (connected()) lastDevice = { where: X.where(), model: X.storage.model, id: X.storage.id };
    ui.scanP = 100; render(); await X.delay(250); ui.scanning = false;
    if (ui.open && !X.games.find(g => g.id === ui.open)) ui.open = null;
    for (const r of X.storage.recovered || []) toast(r.did === 'finished' ? `Finished writing PSP/GAME/${r.folder}, which an unplug had interrupted` : r.did === 'restored' ? `Put PSP/GAME/${r.folder} back as it was before an unplug` : `Cleared a half-written ${r.folder} left by an unplug (it never reached the XMB)`, 'ok');
    windowTitle();
    render();
  }
  async function quietRescan() {
    // after writing: re-read entries (their real ICON0 and PIC1) without the scanning screen
    try { await X.scan(null); } catch (e) { /* keep what we have */ }
    render();
  }

  // ---------- devices coming and going ----------
  H.on('devices', async (list) => {
    const had = connected(), id = X.storage.id;
    const same = list.find(d => d.id === id && d.root === X.storage.root);
    if (had && !same) {
      X.scenario.pspDisconnected = true;
      toast('PSP disconnected', 'bad');
      render(); windowTitle();
      if ($('drawer').repaint) $('drawer').repaint();
      return;
    }
    if (had && same) { X.setDevice(same); render(); return; }
    // nothing connected before: a PSP arrived
    const d = X.pickDevice(list);
    if (d) {
      toast(`${d.go ? 'PSP Go' : 'PSP'} connected on ${d.letter || 'the test folder'}`, 'ok');
      await scan();
      if ($('drawer').repaint) $('drawer').repaint();
    } else render();
  });

  // ---------- chrome wiring ----------
  function setTheme(t) {
    win.dataset.theme = t; $('theme').querySelectorAll('button').forEach(x => x.classList.toggle('on', x.dataset.t === t));
    document.body.classList.toggle('light', t === 'light');
    H.call('theme', { dark: t === 'dark' }).catch(() => {});
  }
  $('theme').querySelectorAll('button').forEach(b => b.addEventListener('click', () => { setTheme(b.dataset.t); prefs.theme = b.dataset.t; X.savePrefs(); }));
  window.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && e.target.tagName !== 'INPUT') { const o = win.querySelector('.ovl'); if (o && !o.querySelector('.btns:empty')) { o.remove(); return; } if (ui.open) { ui.open = null; render(); } }
    if ((e.key === 'F5' || (e.ctrlKey && e.key === 'r')) && !running) { e.preventDefault(); scan(); }
  });

  async function start() {
    if (!H.native) { $('main').innerHTML = '<div class="waiting"><h2>GBAdhoc XMB</h2><p>This page is the inside of GBAdhoc-XMB.exe. Run the app to use it.</p></div>'; ui.scanning = false; return; }
    hello = await H.call('hello');
    const st = hello.store || {};
    if (st.prefs) Object.assign(prefs, st.prefs);
    if (!Array.isArray(prefs.folders)) prefs.folders = [];
    if (st.queue && st.queue.ids && st.queue.ids.length) { queueState = st.queue; queueState.paused = true; }
    $('ver').textContent = 'GBAdhoc XMB ' + hello.version;
    setTheme(prefs.theme || (hello.dark ? 'dark' : 'light'));
    X.netStatus().then(n => { netInfo = n; render(); });
    await scan();
    if (queueState && !X.games.some(g => g.id === queueState.ids[queueState.at])) { queueState = null; saveQueue(); render(); }
    runDemo();
  }

  // demo deep links (screenshots and tests): ?demo=insp:<title words> | picker:.. | make:.. | queue | settings | art | light
  function runDemo() {
    const demo = /[?&]demo=([a-z]+)(?::([^&]+))?/.exec(location.search);
    if (!demo) return;
    const want = decodeURIComponent(demo[2] || '').toLowerCase();
    const g = X.games.find(x => want && (X.state[x.id].title + ' ' + x.file).toLowerCase().includes(want)) || X.games[0];
    if (demo[1] === 'light') setTheme('light');
    if (demo[1] === 'settings') { ui.view = 'settings'; refreshSettings(); }
    if (demo[1] === 'art') { ui.view = 'art'; refreshNet(); }
    if (demo[1] === 'xmb') ui.view = 'xmb';
    if (demo[1] === 'attn') ui.view = 'attn';
    if (g && ['insp', 'picker', 'make', 'light', 'fetch'].includes(demo[1])) ui.open = g.id;
    render();
    if (!g) return;
    if (demo[1] === 'picker') pickerSheet(g, 'icon');
    if (demo[1] === 'make') makeFlow(g);
    if (demo[1] === 'fetch') fetchFlow(g);
    if (demo[1] === 'queue') runQueue(X.games.filter(x => x.recognised && !x.entry && !x.orphan), 'make', 0, false);
  }

  window.DESK = { ui, render, scan, runQueue, resumeQueue, makeFlow, fetchFlow, pickerSheet, get running() { return running; }, get queueState() { return queueState; } };
  start().catch(e => { ui.scanning = false; toast(e.message, 'bad'); render(); });
})();
