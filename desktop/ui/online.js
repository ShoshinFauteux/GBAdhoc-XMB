/* GBAdhoc XMB — the Online tab: "Host online room" (GBAdhoc's relay on this PC).
   Pure view code: describe(status) says what the state means, html(status, check, view) draws it.
   desktop.js owns the polling and the buttons; tests/online_ui.js runs these on fixtures. */
(function (root) {
  'use strict';
  const esc = s => String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const SHARED = 'relay.rayquaza.ca';
  const NO_FILES = 'No game files pass through this PC: only link traffic, the same as between two PSPs.';

  function ports(st) { const p = (st && st.port) || 27420; return `UDP ${p}-${p + 1}`; }
  function relayAddr(ip, st) { const p = (st && st.port) || 27420; return p === 27420 ? ip : `${ip}:${p}`; }
  function sharedTip() { return `Joining needs nothing from a router, so the easy way round is to let your friend host on their PC. Or skip the PC: on the PSPs, Create room on the shared server (${SHARED}, the default), which needs nothing from either router.`; }
  // what a friend types, best first: the room code while the directory has it, else the invite
  function bestCode(st) {
    const cd = (st && st.codes) || {}, dir = cd.directory || {};
    if (cd.room && cd.roomRegistered && dir.state === 'registered') return { kind: 'room', code: cd.room, backup: cd.invite || '' };
    if (cd.invite) return { kind: 'invite', code: cd.invite, backup: '' };
    return null;
  }
  // the sentence the big Copy button puts on the clipboard (paste it in a chat)
  function shareText(st) {
    const b = bestCode(st), cd = (st && st.codes) || {};
    const how = 'On your PSP: GBAdhoc > Wireless > Online > Join with code.';
    const same = 'Use the same GBAdhoc build as me, with gba_bios.bin in place.';
    if (st && st.verdict === 'open' && b)
      return `Join my GBAdhoc room: ${b.code}. ${how}${b.backup ? ` If that code does not work, use ${b.backup}.` : ''} ${same}`;
    if (cd.inviteLan) return `Join my GBAdhoc room on our Wi-Fi: ${cd.inviteLan}. ${how} ${same}`;
    return '';
  }

  // What the state means, in words: {tone, title, text, shared}
  function describe(st) {
    if (!st || !st.running) return { tone: '', title: 'Not hosting', text: 'Host a room on this PC and the PSPs connect here instead of the shared relay: the lowest ping when your router allows it.', shared: false };
    const up = st.upnp || {}, rel = st.relay || {}, wan = up.wan || '';
    switch (st.verdict) {
      case 'checking':
        return { tone: 'info', title: 'Hosting · checking your router…', text: 'PSPs on this network can join already. Asking the router to open the ports with UPnP.', shared: false };
      case 'open': {
        const pub = rel.state === 'ok' && rel.pub ? rel.pub : wan;
        let text = `Players on the internet can reach this PC at its public address${pub ? ' (shown below)' : ''}.`;
        const dir = (st.codes && st.codes.directory) || {};
        if (dir.state === 'failed')
          return { tone: 'warn', title: 'Hosting on this PC · the directory is unreachable',
            text: `${dir.name || SHARED} did not take the room code${dir.error ? ' (' + dir.error + ')' : ''}, so nobody can look it up right now. The invite code needs no server: share that instead. The room keeps trying to register every 60 s.`, shared: false };
        text = `${ports(st)} open via UPnP. ` + text + ' Players anywhere can join with the room code or the invite code.';
        if (rel.state === 'ok') text += ' The shared relay sees the same address.';
        else if (rel.state === 'noanswer' || rel.state === 'nodns') text += ' Not verified from outside: the shared relay did not answer.';
        const b = bestCode(st);
        if (b) return { tone: 'ok', title: `Ready: friends can join with ${b.kind === 'room' ? 'code' : 'invite code'} ${b.code}`, text, shared: false };
        return { tone: 'ok', title: `Hosting on this PC · ${ports(st)} open via UPnP`, text, shared: false };
      }
      case 'cgnat':
        return { tone: 'bad', title: 'Your internet connection is behind carrier-grade NAT',
          text: `Your router's internet address${wan ? ' (' + wan + ')' : ''} is shared with other customers of your provider, so nobody on the internet can reach this PC. PSPs on this network still can.`, shared: true };
      case 'double-nat':
        return { tone: 'bad', title: 'A second router sits in front of this one',
          text: `${wan && /^(10\.|172\.(1[6-9]|2\d|3[01])\.|192\.168\.)/.test(wan) ? `Your router's internet address is ${wan}, a private address: another router (often the provider's modem) is between it and the internet` : 'The internet sees this PC at another address than your router has: another router is in between'}, and UPnP only opened the first one. Put the modem in bridge mode or forward ${ports(st)} on it too.`, shared: true };
      default: break;
    }
    // lan-only
    if (up.state === 'off') return { tone: 'warn', title: 'Hosting for this network only', text: 'Port mapping is off, so only PSPs on this network can join (or forward the ports on your router by hand).', shared: true };
    if (up.state === 'conflict') return { tone: 'warn', title: 'The port is taken on your router', text: `${up.detail || ''} Pick another port in Settings below. PSPs on this network can still join.`.trim(), shared: true };
    const fix = `The fix: turn on UPnP in your router's settings (a provider gateway you manage from a phone app has it, or Port forwarding, in that app), or forward ${ports(st)} to ${(st.lan || [])[0] || 'this PC'}; then Stop and Host again.`;
    if (up.state === 'failed') return { tone: 'warn', title: "Your router didn't open a port, so only this network can join", text: `${up.detail || 'The router refused the port mapping.'} ${fix} PSPs on this network can join already.`, shared: true };
    return { tone: 'warn', title: "Your router didn't open a port, so only this network can join", text: `No UPnP router answered. ${fix} PSPs on this network can join already.`, shared: true };
  }

  // The two codes a friend types on the PSP (Wireless › Online › Join with code), as the
  // mockup draws them: the room code via the directory, the invite code with no server.
  // When the directory cannot be used the invite code comes first.
  function codesHTML(st, view) {
    const cd = (st && st.codes) || {}, dir = cd.directory || {};
    if (!cd.room) return '';
    const outside = st.verdict === 'open';
    const dirOff = dir.state === 'off', dirDown = dir.state === 'failed' || (dir.state === 'registering' && (dir.tries || 0) >= 3);
    const roomNote = dirOff ? 'The directory is off in Settings: only PSPs on this network that point at this PC can use these four letters. Share the invite code.'
      : dir.state === 'registering' && !dirDown ? `Registering on ${esc(dir.name || SHARED)}…`
      : dirDown ? `Cannot be looked up: the directory${dir.error ? ' refused it (' + esc(dir.error) + ')' : ' is unreachable'}. The invite code needs no server.`
      : !outside && st.verdict !== 'checking' ? 'Registered, but players outside cannot reach this PC (see above). PSPs on this network can use it.'
      : `Looked up on ${esc(dir.name || SHARED)}. Lookups only; no game traffic.`;
    const inv = cd.invite || '', lanInv = cd.inviteLan || '';
    const len = s => s.replace(/-/g, '').length;
    const room = `<div class="codebox ${dirOff || dirDown || !cd.roomRegistered ? 'off' : ''}"><div class="lbl">Room code <span class="hint" title="Four letters. The PSP asks the directory where room ${esc(cd.room)} is.">4 letters · via the directory</span></div>
        <div class="big">${esc(cd.room)}</div><div class="sub">${roomNote}</div>
        <div class="acts"><button class="b sm" data-copy="${esc(cd.room)}">Copy</button></div></div>`;
    const invite = `<div class="codebox"><div class="lbl">Invite code <span class="hint" title="This PC's address and port plus a check character, Crockford base32 (no I, L, O or U).">${inv ? len(inv) : 8} characters · no server</span></div>
        <div class="big inv">${inv ? esc(inv) : '—'}</div><div class="sub">${inv ? (view.showIp ? 'Encodes this PC\'s public address' : 'Encodes this PC\'s public address (hidden above, but anyone with the code can read it)') + `${len(inv) === 11 ? ' and its port' : ', the default port,'} and a check character. No server needed.` : 'No public address found yet, so there is no invite code for the internet.'}</div>
        <div class="acts">${inv ? `<button class="b sm primary" data-copy="${esc(inv)}">Copy</button>` : ''}</div>
        ${lanInv ? `<div class="sub lan">Same house or Wi-Fi as this PC? Use <span class="mono">${esc(lanInv)}</span> <button class="b sm" data-copy="${esc(lanInv)}">Copy</button></div>` : ''}</div>`;
    const order = dirOff || dirDown ? [invite, room] : [room, invite];
    const msg = shareText(st);
    const share = msg ? `<div class="oshare"><div class="t"><small>Send this to your friend</small><div class="msg" id="on-share">${esc(msg)}</div></div><button class="b primary big" data-copy="${esc(msg)}" data-copylabel="the invite message" id="on-copyshare">Copy message</button></div>` : '';
    return `<div class="sec codes"><h3>Tell your friend</h3>${share}<div class="coderow">${order.join('')}</div>
      <div class="srow"><div class="t"><b>On their PSP</b><small class="p">Wireless › Online › <i>Join with code</i>, then type either code (one PSP creates the room with it, the other joins). GBAdhoc with online play, Settings › Online play on. PSPs on this network join the same way.</small></div></div></div>`;
  }

  function fmtDur(s) { s = Math.max(0, Math.floor(s || 0)); if (s < 60) return s + ' s'; if (s < 3600) return Math.floor(s / 60) + ' min ' + (s % 60) + ' s'; return Math.floor(s / 3600) + ' h ' + Math.floor(s % 3600 / 60) + ' min'; }
  function pathCell(p) {
    return p.path === 'direct' ? '<span class="pill ok" title="The two PSPs exchange game traffic directly; this PC only keeps the room">Direct</span>'
      : '<span class="pill warn" title="No direct path between them, so this PC forwards every packet">Via host</span>';
  }
  function pingCell(p) {
    if (p.path === 'direct') return '<span class="dim" title="Game traffic goes between the PSPs, not to this PC">—</span>';
    if (p.pingMs < 0) return '<span class="dim">—</span>';
    const tone = p.pingMs <= 60 ? 'ok' : p.pingMs <= 150 ? 'warn' : 'bad';
    return `<span class="pill ${tone}">${p.pingMs} ms</span>`;
  }
  function lossCell(p) { if (p.path === 'direct' || p.lossPct < 0) return '<span class="dim">—</span>'; return `<span class="${p.lossPct >= 3 ? 'bad' : p.lossPct >= 1 ? 'warn' : ''}">${p.lossPct.toFixed(1)} %</span>`; }
  const MODE = { cable: 'Link Cable', adapter: 'Wireless Adapter', any: 'any link' };

  function roomsHTML(st) {
    const rooms = (st && st.rooms) || [];
    if (!rooms.length) return `<div class="srow"><div class="t"><b>No rooms yet</b><small class="p">When PSPs join with this PC's room code or invite code (Wireless › Online › Join with code), they appear here. The first one in hosts the session.</small></div></div>`;
    return rooms.map(r => `<div class="oroom" data-code="${esc(r.code)}"><div class="orh"><span class="ocode">${esc(r.code)}</span><span class="t"><b>${r.players.length} ${r.players.length === 1 ? 'player' : 'players'}</b><small class="p">${esc(MODE[r.mode] || 'any link')} · open ${fmtDur(r.ageS)}${r.joined ? '' : ' · waiting for a guest (the code lasts 10 minutes)'}</small></span><button class="b sm" data-copy="${esc(r.code)}">Copy code</button></div>
      <table class="otab"><thead><tr><th>Player</th><th>From</th><th>Path</th><th>Ping to this PC</th><th>Loss</th><th>Packets/s in · out</th><th>Connected</th></tr></thead><tbody>
      ${r.players.map(p => `<tr data-slot="${p.slot}"><td><b>${p.slot === 0 ? 'Host' : 'Guest ' + p.slot}</b>${p.nick ? ' · ' + esc(p.nick) : ''}</td><td>${esc(p.where)}<small> ${esc(p.ip)}</small></td><td>${pathCell(p)}</td><td>${pingCell(p)}</td><td>${lossCell(p)}</td><td class="mono">${p.ppsIn} · ${p.ppsOut}</td><td>${fmtDur(p.connS)}</td></tr>`).join('')}
      </tbody></table></div>`).join('');
  }

  function checkHTML(c, busy) {
    if (busy) return `<div class="srow"><div class="t"><b>Checking…</b><small class="p">Asking the router and the shared relay. This takes a few seconds.</small></div></div>`;
    if (!c) return `<div class="srow"><div class="t"><b>Not run yet</b><small class="p">Finds your router, its internet address and what kind it is, and how the shared relay sees this PC. Nothing is changed.</small></div></div>`;
    const up = c.upnp || {}, rel = c.relay || {};
    const wanCls = { public: 'public', private: 'private: a second router is in front', cgnat: 'carrier-grade NAT (shared with other customers)', loopback: 'loopback', invalid: 'not valid' }[up.wanClass] || '';
    const nat = { open: 'open: one mapping per socket (direct paths can work)', symmetric: 'symmetric: a new port per destination (the relay is used)', unknown: 'unknown' }[rel.nat] || 'unknown';
    const verdict = { open: ['ok', 'Can host for the internet'], cgnat: ['bad', 'Cannot host for the internet: CGNAT'], 'double-nat': ['bad', 'Cannot host for the internet: double NAT'], 'lan-only': ['warn', 'This network only'] }[c.verdict] || ['', '—'];
    return `<div class="srow"><div class="t"><b>UPnP router</b><small>${up.state === 'found' ? esc(up.router) + ' · this PC is ' + esc(up.local) : esc(up.detail || 'none found')}</small></div><span class="pill ${up.state === 'found' ? 'ok' : 'warn'}">${up.state === 'found' ? 'Found' : 'Not found'}</span></div>
      <div class="srow"><div class="t"><b>Router's internet address</b><small>${up.wan ? `<span class="secret" data-secret="${esc(up.wan)}">hidden</span> · ${esc(wanCls)}` : '—'}</small></div>${up.wan ? '<button class="b sm" data-show>Show</button>' : ''}</div>
      <div class="srow"><div class="t"><b>Shared relay (${esc(rel.name || SHARED)})</b><small class="p">${rel.state === 'ok' ? `Sees this PC at <span class="secret" data-secret="${esc(rel.pub)}">hidden</span> · NAT ${esc(nat)}` : rel.state === 'nodns' ? 'Could not be found (no DNS answer)' : rel.state === 'off' ? 'Not asked' : 'Did not answer'}</small></div><span class="pill ${rel.state === 'ok' ? 'ok' : 'warn'}">${rel.state === 'ok' ? 'Reachable' : 'No answer'}</span></div>
      <div class="srow"><div class="t"><b>Verdict</b><small class="p">${c.verdict === 'open' ? 'UPnP and a public address: hosting here should work for players on the internet.' : c.verdict === 'lan-only' ? 'Only PSPs on this network could join, unless you forward the ports by hand.' : 'Use the shared relay for internet play.'}</small></div><span class="pill ${verdict[0]}">${verdict[1]}</span></div>`;
  }

  function statusHTML(st, view) {
    view = view || {};
    const d = describe(st);
    const up = (st && st.upnp) || {}, rel = (st && st.relay) || {};
    const lan = (st && st.lan) || [];
    const pub = rel.state === 'ok' && rel.pub ? rel.pub : up.wan;
    let rows = '';
    if (st && st.running) {
      const reach = { open: ['ok', 'Internet + this network'], checking: ['', 'Checking…'], cgnat: ['bad', 'This network only (CGNAT)'], 'double-nat': ['bad', 'This network only (double NAT)'], 'lan-only': ['warn', 'This network only'] }[st.verdict] || ['', '—'];
      rows = `<div class="srow"><div class="t"><b>Listening</b><small>${esc(ports(st))} on ${st.bind === '0.0.0.0' ? 'every network of this PC' : esc(st.bind)} · for ${fmtDur(st.sinceS)}</small></div><span class="pill ok">On</span></div>
        <div class="srow"><div class="t"><b>Router</b><small class="p">${up.state === 'mapped' ? esc(up.detail) : up.state === 'searching' || !up.state ? 'Looking for a UPnP router…' : esc(up.detail || '—')}</small></div><span class="pill ${up.state === 'mapped' ? 'ok' : up.state === 'searching' || !up.state ? '' : 'warn'}">${up.state === 'mapped' ? 'Ports open' : up.state === 'off' ? 'Off' : up.state === 'searching' || !up.state ? 'Checking' : 'Not open'}</span></div>
        <div class="srow"><div class="t"><b>Reachable from</b><small class="p">PSPs on this network${lan.length ? ': ' + lan.map(ip => `<span class="mono">${esc(relayAddr(ip, st))}</span>`).join(', ') : ''}</small></div><span class="pill ${reach[0]}">${reach[1]}</span>${lan.length ? `<button class="b sm" data-copy="${esc(relayAddr(lan[0], st))}">Copy</button>` : ''}</div>
        ${pub ? `<div class="srow"><div class="t"><b>Public address</b><small>${view.showIp ? esc(relayAddr(pub, st)) : 'hidden · the guest sees this address'}</small></div><button class="b sm" data-showpub>${view.showIp ? 'Hide' : 'Show'}</button><button class="b sm" data-copypub>Copy</button></div>` : ''}`;
    }
    const steps = '';   // the PSPs now join by code: the "Tell your friend" card (codesHTML)
    const busy = !(st && st.running) && view.conflict ? conflictHTML(view.conflict, view) : '';
    if (busy && view.useExternal) return busy;
    return `${busy || `<div class="note ${d.tone || 'info'} onote"><b>${esc(d.title)}</b><div>${esc(d.text)}${d.shared ? ' ' + esc(sharedTip()) : ''}</div></div>`}
      ${st && st.running ? codesHTML(st, view) : ''}
      <div class="sec"><h3>This PC</h3>${rows || `<div class="srow"><div class="t"><b>Ports</b><small>${esc(ports(st))} (UDP), opened on the router with UPnP while you host and closed when you stop</small></div><span class="pill">Off</span></div>`}${steps}</div>
      <div class="sec"><h3>Rooms</h3>${st && st.running ? roomsHTML(st) : `<div class="srow"><div class="t"><b>Not hosting</b><small class="p">Rooms appear here while this PC hosts.</small></div></div>`}</div>`;
  }

  // The port was busy when Host was pressed (desktop.js asked onlineConflict):
  //   checking: probing it;  relay: a GBAdhoc relay answers there (the standalone gbadhoc-relay,
  //   or another copy of this app): use it, or host on the next free port;  else another program
  //   holds it (desktop.js has already moved to the free port).  view.useExternal: "Use it" chosen.
  function conflictHTML(c, view) {
    if (!c) return '';
    if (c.checking) return `<div class="note info onote" id="on-conflict"><b>UDP ${c.port}-${c.port + 1} is in use on this PC</b><div>Checking whether it is a GBAdhoc relay…</div></div>`;
    if (!c.relay) return '';
    const p = c.port, fp = c.freePort;
    const where = c.addr && !/^127\./.test(c.addr) ? `${esc(c.addr)}:${p}` : `UDP ${p}`;
    const up = c.upnp || {};
    const reach = up.state === 'mapped' ? 'Your router forwards the port to this PC (UPnP), so players on the internet can reach it too.'
      : up.state === 'other' ? `Your router forwards UDP ${p} to another device (${esc(up.to)}), so only PSPs on this network can reach this relay.`
      : `Your router has no UPnP mapping of UDP ${p}: the internet invite works if you forwarded UDP ${p}-${p + 1} to this PC by hand.`;
    if (view && view.useExternal) {
      const inv = c.invite || '', lan = c.inviteLan || '';
      return `<div class="note ok onote" id="on-conflict"><b>Using the GBAdhoc relay already running on this PC</b><div>It listens on ${where}. GBAdhoc XMB does not start, stop or watch it: its rooms and players are in that relay's own log. ${reach}</div></div>
        <div class="sec codes"><h3>Tell your friend</h3><div class="coderow">
          <div class="codebox"><div class="lbl">Invite code <span class="hint">no server</span></div><div class="big inv">${inv ? esc(inv) : '—'}</div>
            <div class="sub">${inv ? `This PC's public address and port ${p}, plus a check character.${up.state === 'mapped' ? '' : ` Needs UDP ${p}-${p + 1} forwarded to this PC.`}` : 'No internet invite: no public address was found, or the router forwards this port to another device.'}</div>
            <div class="acts">${inv ? `<button class="b sm primary" data-copy="${esc(inv)}">Copy</button>` : ''}</div>
            ${lan ? `<div class="sub lan">For a PSP on this network: <span class="mono">${esc(lan)}</span> <button class="b sm" data-copy="${esc(lan)}">Copy</button></div>` : ''}</div>
          <div class="codebox off"><div class="lbl">Room code</div><div class="big">—</div><div class="sub">That relay keeps its own rooms: the first PSP in with the invite code creates one and the second joins it.</div></div></div>
          <div class="srow"><div class="t"><b>Or host here instead</b><small class="p">${fp ? `GBAdhoc XMB can host its own room on UDP ${fp}-${fp + 1} (the invite code then carries the port). The relay on ${p} keeps running.` : 'No free port pair nearby.'}</small></div>${fp ? `<button class="b sm" data-hostport="${fp}">Host on ${fp}</button>` : ''}<button class="b sm" data-unuse>Back</button></div></div>`;
    }
    return `<div class="note info onote" id="on-conflict"><b>A GBAdhoc relay is already running on this PC</b><div>It answers on ${where}, so GBAdhoc XMB leaves it alone. Use it (PSPs join it with an invite code), or host this app's own room on the next free port.</div>
      <div class="oacts"><button class="b sm primary" data-useext>Use it</button>${fp ? `<button class="b sm" data-hostport="${fp}">Host on UDP ${fp}-${fp + 1} instead</button>` : ''}<button class="b sm" data-unconflict>Dismiss</button></div></div>`;
  }

  function logHTML(st) {
    const log = (st && st.log) || [];
    if (!log.length) return '<div class="olog empty">Nothing yet.</div>';
    return `<div class="olog">${log.slice().reverse().map(l => `<div class="${esc(l.tone)}"><span class="mono">${esc(l.t)}</span>${esc(l.text)}</div>`).join('')}</div>`;
  }

  // the whole page; #on-live is what the poll redraws
  function html(st, check, view) {
    view = view || {};
    const running = !!(st && st.running);
    return `<div class="tools"><h1>Online play</h1><span class="pill ${running ? 'ok' : ''}" id="on-pill">${running ? 'Hosting' : 'Not hosting'}</span><span class="grow"></span>
      <button class="b" id="on-check" ${view.checking ? 'disabled' : ''}>Network check</button>
      <button class="b ${running ? 'danger' : 'primary'}" id="on-host" ${view.busy ? 'disabled' : ''}>${running ? 'Stop hosting' : 'Host online room'}</button></div>
      <div class="page opage">
        <div id="on-live">${statusHTML(st, view)}</div>
        <div class="sec"><h3>Network check</h3><div id="on-checkres">${checkHTML(check, view.checking)}</div></div>
        <div class="sec"><h3>Settings</h3>
          <div class="srow"><div class="t"><b>Port</b><small class="p">UDP, and the next one up for the address check. GBAdhoc's default is 27420; another port makes the invite code 11 characters (it has to carry the port)</small></div><input class="num" id="on-port" type="number" min="1024" max="65534" value="${esc(view.port || 27420)}" ${running ? 'disabled' : ''}></div>
          <div class="srow"><div class="t"><b>Open the ports with UPnP</b><small class="p">Asks your router to forward them to this PC while you host, and removes them when you stop</small></div><span class="tog ${view.upnp !== false ? 'on' : ''}" id="on-upnp" ${running ? 'aria-disabled="true"' : ''}></span></div></div>
          <div class="srow"><div class="t"><b>Host when GBAdhoc XMB starts</b><small class="p">Opens the room as soon as the app opens, with the settings above. Off by default.</small></div><span class="tog ${view.autoHost ? 'on' : ''}" id="on-auto"></span></div>
          <div class="srow"><div class="t"><b>List the room in the directory</b><small class="p">Registers "this room code is at this PC" on ${esc(SHARED)} so a four-letter code works from anywhere. Off, only the invite code works. The directory stores the code and this PC's address while you host; no names, no game traffic. Default on (the owner may change it).</small></div><span class="tog ${view.directory !== false ? 'on' : ''}" id="on-dir" ${running ? 'aria-disabled="true"' : ''}></span></div>
        <div class="sec"><h3>If a friend can't join</h3>
          <div class="srow"><div class="t"><b>Same GBAdhoc, same BIOS</b><small class="p">Both players need the same GBAdhoc build and <span class="mono">gba_bios.bin</span> in GBAdhoc's folder (PPSSPP players too), or the games desync.</small></div></div>
          <div class="srow"><div class="t"><b>On a phone hotspot</b><small class="p">If the phone is also on Wi-Fi, many phones (Samsung and others) share that Wi-Fi instead of mobile data. Turn the phone's Wi-Fi off.</small></div></div>
          <div class="srow"><div class="t"><b>In the same house as this PC</b><small class="p">The room code finds this PC over your own network by itself. With an invite code, use the <i>same house</i> code under it: many routers do not loop their public address back.</small></div></div>
          <div class="srow"><div class="t"><b>A PSP hosting instead</b><small class="p">Needs UDP 27500 forwarded to that PSP. Letting a PC host is easier: GBAdhoc XMB opens its own ports.</small></div></div></div>
        <div class="sec"><h3>Before you host</h3>
          <div class="srow"><div class="t"><b>No game files</b><small class="p" id="on-nofiles">${esc(NO_FILES)}</small></div></div>
          <div class="srow"><div class="t"><b>Your address</b><small class="p">A guest on the internet connects to this PC, so they see its public IP address (with the shared relay, nobody sees anyone's).</small></div></div>
          <div class="srow"><div class="t"><b>Keep the PC on</b><small class="p">The room lasts while this PC is on and hosting. GBAdhoc XMB keeps the PC from sleeping while it hosts; closing the app or the lid ends the room.</small></div></div>
          <div class="srow"><div class="t"><b>What it needs</b><small class="p">A router with UPnP and a public IPv4 address. Carrier-grade NAT, many IPv6-only plans, dorms and phone hotspots cannot host: use the shared relay.</small></div></div>
          <div class="srow"><div class="t"><b>Windows Firewall</b><small class="p">The first time you host, Windows may ask whether GBAdhoc XMB may use the network. Allow it on the network you are on (Private, at home). If you said no: Windows Security › Firewall &amp; network protection › Allow an app through firewall, and tick GBAdhoc XMB. The app never changes firewall settings itself.</small></div></div></div>
        <div class="sec"><details id="on-logbox" ${view.logOpen ? 'open' : ''}><summary><h3>Log</h3></summary><div id="on-log">${logHTML(st)}</div></details></div>
      </div>`;
  }

  // the address the Copy button of the public row copies (kept out of the page until asked)
  function publicAddr(st) { const up = (st && st.upnp) || {}, rel = (st && st.relay) || {}; const pub = rel.state === 'ok' && rel.pub ? rel.pub : up.wan; return pub ? relayAddr(pub, st) : ''; }

  root.ONLINE = { describe, shareText, bestCode, publicAddr, html, statusHTML, logHTML, checkHTML, roomsHTML, codesHTML, conflictHTML, NO_FILES, SHARED };
})(typeof window !== 'undefined' ? window : globalThis);
