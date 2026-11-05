/* e2e.js -- driven by test_app.sh: the real app (host + UI) on a fake PSP.
   Loaded with --script before the page; runs the test named by ?test= and sends
   the outcome to the host (testResult), which writes it to --result and quits. */
(function () {
  'use strict';
  const T = /[?&]test=([a-z]+)/.exec(location.search);
  if (!T) return;
  const out = { test: T[1], checks: [], fail: 0 };
  const check = (name, ok, got) => { out.checks.push({ name, ok: !!ok, got: got === undefined ? null : got }); if (!ok) out.fail++; };
  const wait = (f, ms) => new Promise((res, rej) => { const t0 = Date.now(); const iv = setInterval(() => { let v = false; try { v = f(); } catch (e) { /* not yet */ } if (v) { clearInterval(iv); res(v); } else if (Date.now() - t0 > (ms || 30000)) { clearInterval(iv); rej(new Error('timeout')); } }, 100); });
  const done = () => window.HOST.call('testResult', out);
  const byFile = (f) => window.XMB.games.find(g => g.file === f);
  const summary = () => window.XMB.games.map(g => ({ file: g.file, title: window.XMB.state[g.id].title, status: window.XMB.status(g).key, recognised: g.recognised, how: g.how, hack: !!g.hack, nointro: g.nointro, entry: g.entry ? g.entry.folder : null, rom: g.launchRom }));

  async function main() {
    const X = window.XMB, D = window.DESK;
    const games = X.games.filter(g => !g.orphan);
    check('nine ROMs found', games.length === 9, games.length);
    check('eight recognised', games.filter(g => g.recognised).length === 8, games.filter(g => g.recognised).length);
    check('the unknown ROM is not recognised', byFile('mygame_final2 (1).gba') && !byFile('mygame_final2 (1).gba').recognised);
    check('Emerald is in the XMB', X.status(byFile('Pokemon - Emerald Version (USA, Europe).gba')).key === 'inxmb');
    check('Mario Kart entry: ROM moved', X.status(byFile('Mario Kart - Super Circuit (USA).gba')).key === 'moved');
    check('MonoMyth is a hack', byFile('Pokemon MonoMyth v1.2.gba').hack, byFile('Pokemon MonoMyth v1.2.gba').base);
    check('FE Morrow is a hack of Sacred Stones', /Sacred Stones/.test(byFile("Fire Emblem - The Morrow's Golden Country.gba").base), byFile("Fire Emblem - The Morrow's Golden Country.gba").base);
    check('Crystal is GBC', byFile('Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc').system === 'GBC');
    check('Emerald title', X.state[byFile('Pokemon - Emerald Version (USA, Europe).gba').id].title === 'Pokemon Emerald');
    check('Crystal title', X.state[byFile('Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc').id].title === 'Pokemon Crystal');
    check('Emerald has its save', byFile('Pokemon - Emerald Version (USA, Europe).gba').save);
    check('subfolder ROM path', byFile('Mario Kart - Super Circuit (USA).gba').launchRom === 'racing/Mario Kart - Super Circuit (USA).gba');
    // GBAdhoc 4.6 folders: roms/GBA, roms/GB, roms/GBC; saves/ and states/ are not ROMs
    check('roms/GBA ROM path', byFile('Kirby & The Amazing Mirror (USA).gba').launchRom === 'GBA/Kirby & The Amazing Mirror (USA).gba', byFile('Kirby & The Amazing Mirror (USA).gba').launchRom);
    check('roms/GBC ROM path', byFile('Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc').launchRom === 'GBC/Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc');
    check('roms/GB ROM path', byFile('Tetris (World) (Rev 1).gb').launchRom === 'GB/Tetris (World) (Rev 1).gb');
    check('nothing from saves/ or states/', !X.games.some(g => /Decoy|Not a game/.test(g.file)), X.games.map(g => g.file));
    check("a save in 4.6's saves/ folder", byFile('Kirby & The Amazing Mirror (USA).gba').save && byFile('Tetris (World) (Rev 1).gb').save);
    check('own box art for a roms/GBA ROM', !!byFile('Kirby & The Amazing Mirror (USA).gba').ownBoxart);
    // make all: the 3rd write is "unplugged" (XMB_FAULT=eboot@3); the queue must stop there and resume there
    const todo = X.games.filter(g => g.recognised && !g.entry && !g.orphan);
    check('six to make', todo.length === 6, todo.length);
    D.runQueue(todo, 'make', 0, false);
    await wait(() => !D.running, 60000);
    const q = D.queueState;
    check('queue paused by the unplug', q && q.paused, q);
    check('paused at the third game', q && q.at === 2, q && q.at);
    check('two made before it', X.games.filter(g => g.entry).length === 4, X.games.filter(g => g.entry).length);
    D.resumeQueue();
    await wait(() => !D.running && !D.queueState, 60000);
    check('all six made after resume', todo.every(g => !!g.entry), todo.filter(g => !g.entry).map(g => g.file));
    // fix the moved ROM's entry
    const mk = byFile('Mario Kart - Super Circuit (USA).gba');
    const ok = await X.repairEntry(mk);
    check('Mario Kart entry updated', ok);
    // remove one
    const tet = byFile('Tetris (World) (Rev 1).gb');
    await X.removeEntry(tet);
    check('Tetris entry removed', !tet.entry);
    await D.scan();
    check('after rescan: Mario Kart no longer moved', X.status(byFile('Mario Kart - Super Circuit (USA).gba')).key === 'inxmb');
    check('after rescan: Tetris not in XMB', !byFile('Tetris (World) (Rev 1).gb').entry);
    check('after rescan: 7 entries', X.games.filter(g => g.entry).length === 7, X.games.filter(g => g.entry).length);
    out.games = summary();
  }

  async function scanonly() {
    out.games = summary();
    out.recovered = window.XMB.storage.recovered;
  }

  async function makeone() {
    // write one entry; the host may be told to fail it (XMB_FAULT)
    const X = window.XMB;
    const g = byFile('Kirby & The Amazing Mirror (USA).gba');
    const r = await X.makeEntry(g, null, {});
    out.code = r.code || 'ok';
    out.steps = r.steps.map(s => s.msg);
  }

  async function nopsp() {
    const X = window.XMB;
    check('no PSP: disconnected', X.scenario.pspDisconnected);
    check('no PSP: waiting screen', !!document.querySelector('.waiting'));
  }

  async function full() {
    const X = window.XMB;
    check('stick full detected', X.scenario.stickFull);
    check('full banner', /is full/.test(document.getElementById('banners').textContent));
    check('make disabled', document.getElementById('makeall').disabled);
    const r = await X.makeEntry(byFile('Golden Sun (USA, Europe).gba'), null, {});
    check('make refused: nospace', r.code === 'nospace', r.code);
  }

  async function exists() {
    // a non-entry folder with the entry's name is never replaced
    const X = window.XMB;
    const g = byFile('Golden Sun (USA, Europe).gba');   // test_app.sh put another program in XMB_Golden_Sun
    const r = await X.makeEntry(g, null, {});
    check('other homebrew folder refused', r.code === 'exists' || r.code === 'bad', r.code);
  }

  async function unplug() {
    // test_app.sh pulls the fake PSP (renames its PSP folder) after the first entry
    // and puts it back a few seconds later
    const X = window.XMB, D = window.DESK;
    const todo = X.games.filter(g => g.recognised && !g.entry && !g.orphan);
    D.runQueue(todo, 'make', 0, false);
    await wait(() => D.queueState && D.queueState.paused && !D.running, 90000);
    out.pausedAt = D.queueState.at;
    check('the queue stopped at the unplug', D.queueState.at >= 1 && D.queueState.at < todo.length, D.queueState.at);
    check('the banner says so', /disconnected/i.test(document.getElementById('drawer').textContent + document.getElementById('banners').textContent));
    await wait(() => !X.scenario.pspDisconnected && !D.ui.scanning, 90000);
    check('the PSP was picked up again', true);
    await D.resumeQueue();
    await wait(() => !D.running && !D.queueState, 90000);
    const ids = new Set(todo.map(g => g.id));
    const now = X.games.filter(g => ids.has(g.id));
    check('every game made after the resume', now.length === todo.length && now.every(g => !!g.entry), now.filter(g => !g.entry).map(g => g.file));
  }

  const tests = { main, scanonly, makeone, nopsp, full, exists, unplug };
  window.addEventListener('DOMContentLoaded', async () => {
    try {
      await wait(() => window.DESK && !window.DESK.ui.scanning && window.HOST, 60000);
      await new Promise(r => setTimeout(r, 300));
      await tests[out.test]();
    } catch (e) { check('no exception', false, String(e && e.stack || e)); }
    done();
  });
})();
