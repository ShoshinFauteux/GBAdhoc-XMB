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
  function sharedTip() { return `For internet play, use the shared relay instead: leave each PSP's online server at its default (${SHARED}); this PC is not needed.`; }

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
        if (rel.state === 'ok') text += ' The shared relay sees the same address.';
        else if (rel.state === 'noanswer' || rel.state === 'nodns') text += ' Not verified from outside: the shared relay did not answer.';
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
    if (up.state === 'failed') return { tone: 'warn', title: "Your router didn't open a port", text: `${up.detail || 'The router refused the port mapping.'} Turn on UPnP in the router's settings, or forward ${ports(st)} to this PC by hand. PSPs on this network can still join.`, shared: true };
    return { tone: 'warn', title: "Your router didn't open a port", text: `No UPnP router answered. Turn on UPnP in the router's settings, or forward ${ports(st)} to ${(st.lan || [])[0] || 'this PC'} by hand. Until then only PSPs on this network can join.`, shared: true };
  }

  function fmtDur(s) { s = Math.max(0, Math.floor(s || 0)); if (s < 60) return s + ' s'; if (s < 3600) return Math.floor(s / 60) + ' min ' + (s % 60) + ' s'; return Math.floor(s / 3600) + ' h ' + Math.floor(s % 3600 / 60) + ' min'; }
  function pingCell(p) {
    if (p.path === 'direct') return '<span class="pill ok" title="These two PSPs found a direct path; this PC only keeps the room">direct</span>';
    if (p.pingMs < 0) return '<span class="dim">—</span>';
    const tone = p.pingMs <= 60 ? 'ok' : p.pingMs <= 150 ? 'warn' : 'bad';
    return `<span class="pill ${tone}">${p.pingMs} ms</span>`;
  }
  function lossCell(p) { if (p.path === 'direct' || p.lossPct < 0) return '<span class="dim">—</span>'; return `<span class="${p.lossPct >= 3 ? 'bad' : p.lossPct >= 1 ? 'warn' : ''}">${p.lossPct.toFixed(1)} %</span>`; }
  const MODE = { cable: 'Link Cable', adapter: 'Wireless Adapter', any: 'any link' };

  function roomsHTML(st) {
    const rooms = (st && st.rooms) || [];
    if (!rooms.length) return `<div class="srow"><div class="t"><b>No rooms yet</b><small class="p">When a PSP pointed at this PC creates a room, its code and players appear here.</small></div></div>`;
    return rooms.map(r => `<div class="oroom" data-code="${esc(r.code)}"><div class="orh"><span class="ocode">${esc(r.code)}</span><span class="t"><b>${r.players.length} ${r.players.length === 1 ? 'player' : 'players'}</b><small class="p">${esc(MODE[r.mode] || 'any link')} · open ${fmtDur(r.ageS)}${r.joined ? '' : ' · waiting for a guest (the code lasts 10 minutes)'}</small></span><button class="b sm" data-copy="${esc(r.code)}">Copy code</button></div>
      <table class="otab"><thead><tr><th>Player</th><th>From</th><th>Ping to this PC</th><th>Loss</th><th>Packets/s in · out</th><th>Connected</th></tr></thead><tbody>
      ${r.players.map(p => `<tr data-slot="${p.slot}"><td><b>${p.slot === 0 ? 'Host' : 'Guest ' + p.slot}</b>${p.nick ? ' · ' + esc(p.nick) : ''}</td><td>${esc(p.where)}<small> ${esc(p.ip)}</small></td><td>${pingCell(p)}</td><td>${lossCell(p)}</td><td class="mono">${p.ppsIn} · ${p.ppsOut}</td><td>${fmtDur(p.connS)}</td></tr>`).join('')}
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
    const steps = st && st.running ? `<div class="srow"><div class="t"><b>On the PSPs</b><small class="p">GBAdhoc 4.6.0-beta.4 or later, Settings › Online play on. In GBAdhoc's <span class="mono">config.ini</span> on each PSP, set <span class="mono">online_relay = ${esc(relayAddr(lan[0] || 'this PC’s address', st))}</span> for a PSP on this network${pub && st.verdict === 'open' ? ` or <span class="mono">online_relay = ${view.showIp ? esc(relayAddr(pub, st)) : 'the public address'}</span> for a player on the internet` : ''}. Then one PSP opens Wireless › Online › Create room and the other joins with its code.</small></div></div>` : '';
    return `<div class="note ${d.tone || 'info'} onote"><b>${esc(d.title)}</b><div>${esc(d.text)}${d.shared ? ' ' + esc(sharedTip()) : ''}</div></div>
      <div class="sec"><h3>This PC</h3>${rows || `<div class="srow"><div class="t"><b>Ports</b><small>${esc(ports(st))} (UDP), opened on the router with UPnP while you host and closed when you stop</small></div><span class="pill">Off</span></div>`}${steps}</div>
      <div class="sec"><h3>Rooms</h3>${st && st.running ? roomsHTML(st) : `<div class="srow"><div class="t"><b>Not hosting</b><small class="p">Rooms appear here while this PC hosts.</small></div></div>`}</div>`;
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
          <div class="srow"><div class="t"><b>Port</b><small class="p">UDP, and the next one up for the address check. GBAdhoc's default is 27420</small></div><input class="num" id="on-port" type="number" min="1024" max="65534" value="${esc(view.port || 27420)}" ${running ? 'disabled' : ''}></div>
          <div class="srow"><div class="t"><b>Open the ports with UPnP</b><small class="p">Asks your router to forward them to this PC while you host, and removes them when you stop</small></div><span class="tog ${view.upnp !== false ? 'on' : ''}" id="on-upnp" ${running ? 'aria-disabled="true"' : ''}></span></div></div>
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

  root.ONLINE = { describe, publicAddr, html, statusHTML, logHTML, checkHTML, roomsHTML, NO_FILES, SHARED };
})(typeof window !== 'undefined' ? window : globalThis);
