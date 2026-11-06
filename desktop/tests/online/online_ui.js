// online_ui.js -- the Online tab's states (desktop/ui/online.js) on fixtures, in node:
//   node desktop/tests/online/online_ui.js
// Every state the host can report must say the right thing, keep the "no game files" note,
// suggest the shared relay exactly when this PC cannot host for the internet, and never show
// the public address until asked.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const ctx = {};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../../ui/online.js'), 'utf8'), ctx);
const O = ctx.ONLINE;
let fails = 0, n = 0;
function check(name, ok, got) { n++; if (!ok) { fails++; console.log('  FAIL ' + name + (got !== undefined ? '  (got ' + JSON.stringify(got) + ')' : '')); } else console.log('  ok   ' + name); }

const base = { running: true, port: 27420, bind: '0.0.0.0', sinceS: 75, lan: ['192.168.1.20'],
  upnp: { state: 'mapped', detail: 'UDP 27420-27421 open on Fake Router', router: 'Fake Router', wan: '203.0.113.7', wanClass: 'public', local: '192.168.1.20', permanent: false },
  relay: { name: 'relay.rayquaza.ca:27420', state: 'ok', pub: '203.0.113.7' }, verdict: 'open', rooms: [], counters: {}, log: [] };
const S = (o) => JSON.parse(JSON.stringify(Object.assign({}, base, o, { upnp: Object.assign({}, base.upnp, o.upnp || {}), relay: Object.assign({}, base.relay, o.relay || {}) })));
const cases = [
  ['not hosting', { running: false, verdict: '' }, /^Not hosting$/, false],
  ['checking', { verdict: 'checking', upnp: { state: 'searching', wan: '' } }, /checking your router/, false],
  ['open, confirmed', {}, /UDP 27420-27421 open via UPnP/, false],
  ['open, shared relay silent', { relay: { state: 'noanswer', pub: '' } }, /open via UPnP/, false],
  ['cgnat', { verdict: 'cgnat', upnp: { wan: '100.72.1.2', wanClass: 'cgnat' } }, /carrier-grade NAT/, true],
  ['double NAT', { verdict: 'double-nat', upnp: { wan: '192.168.0.5', wanClass: 'private' } }, /second router/, true],
  ['no UPnP', { verdict: 'lan-only', upnp: { state: 'none', detail: 'No UPnP router answered on this network.', wan: '' } }, /didn't open a port/, true],
  ['refused', { verdict: 'lan-only', upnp: { state: 'failed', detail: 'The router refused AddPortMapping (UPnP error 501: ActionFailed).', wan: '203.0.113.7' } }, /didn't open a port/, true],
  ['conflict', { verdict: 'lan-only', upnp: { state: 'conflict', detail: 'UDP 27420 is already forwarded to another device (192.168.1.99).' } }, /port is taken/, true],
  ['upnp off', { verdict: 'lan-only', upnp: { state: 'off', detail: '', wan: '' } }, /this network only/, true],
];
for (const [name, o, title, shared] of cases) {
  const st = S(o);
  const d = O.describe(st);
  check(`${name}: title`, title.test(d.title), d.title);
  check(`${name}: shared relay suggested = ${shared}`, d.shared === shared, d.shared);
  const h = O.html(st, null, { port: 27420 });
  check(`${name}: "no game files" note`, h.includes('No game files pass through this PC'));
  check(`${name}: shared relay named when suggested`, !shared || h.includes('relay.rayquaza.ca'));
  check(`${name}: the public address stays hidden`, !h.includes('203.0.113.7') || !st.running);
  check(`${name}: button`, h.includes(st.running ? 'Stop hosting' : 'Host online room'));
}
// the caveats are always on the page
{
  const h = O.html(S({ running: false }), null, {});
  check('caveat: UPnP and a public IPv4', /UPnP and a public IPv4/.test(h));
  check('caveat: the PC must stay on', /Keep the PC on/.test(h));
  check('caveat: the guest sees the IP', /see its public IP address/.test(h));
  check('firewall: explained, never changed', /never changes firewall settings/.test(h));
}
// shown on request
{
  const h = O.statusHTML(S({}), { showIp: true });
  check('Show reveals the public address', h.includes('203.0.113.7'));
  const h2 = O.statusHTML(S({ port: 30000 }), { showIp: true });
  check('a non-default port is part of the address to give PSPs', h2.includes('203.0.113.7:30000') && h2.includes('192.168.1.20:30000'));
}
// rooms and players
{
  const st = S({ rooms: [{ code: 'KQTR', mode: 'cable', ageS: 42, joined: true, players: [
    { slot: 0, nick: 'Mara', where: 'LAN', ip: '192.168.1.31', nat: 'open', connS: 42, pingMs: 4, lossPct: 0, ppsIn: 61, ppsOut: 60, path: 'relay' },
    { slot: 1, nick: '', where: 'internet', ip: '198.51.x.x', nat: 'open', connS: 30, pingMs: 172, lossPct: 3.4, ppsIn: 60, ppsOut: 61, path: 'relay' }] },
    { code: 'ABCD', mode: 'any', ageS: 5, joined: false, players: [{ slot: 0, nick: '', where: 'LAN', ip: '192.168.1.40', nat: 'unknown', connS: 5, pingMs: -1, lossPct: -1, ppsIn: 0, ppsOut: 0, path: 'relay' }] }] });
  const h = O.roomsHTML(st);
  check('room code shown', h.includes('>KQTR<') && h.includes('>ABCD<'));
  check('player count', h.includes('2 players') && h.includes('1 player<'));
  check('nickname', h.includes('Mara'));
  check('ping, coloured', h.includes('pill ok">4 ms') && h.includes('pill bad">172 ms'));
  check('loss', h.includes('3.4 %') && h.includes('0.0 %'));
  check('no sample yet shows a dash', h.includes('class="dim">—'));
  check('link mode', h.includes('Link Cable'));
  check('waiting room says how long the code lasts', h.includes('10 minutes'));
  const d = O.roomsHTML(S({ rooms: [{ code: 'DRCT', mode: 'any', ageS: 9, joined: true, players: [{ slot: 0, nick: '', where: 'LAN', ip: 'x', nat: 'open', connS: 9, pingMs: -1, lossPct: -1, ppsIn: 1, ppsOut: 1, path: 'direct' }] }] }));
  check('a direct pair says Direct', d.includes('>Direct<'));
  check('a relayed pair says Via host', h.includes('>Via host<'));
  check('no rooms', O.roomsHTML(S({})).includes('No rooms yet'));
}
// the two codes (the mockup's "Tell your friend" card)
{
  const codes = { room: 'KQTR', roomRegistered: true, directory: { name: 'relay.rayquaza.ca:27420', state: 'registered', error: '', tries: 0 }, invite: 'SC07-2AGV', inviteLan: 'R2M0-251N-XYZ' };
  const h = O.statusHTML(S({ codes }), {});
  check('codes: room code shown big', h.includes('class="big">KQTR<'));
  check('codes: looked up on the directory, no game traffic', /Lookups only; no game traffic/.test(h));
  check('codes: invite code shown with copy', h.includes('SC07-2AGV') && h.includes('data-copy="SC07-2AGV"'));
  check('codes: LAN invite offered', h.includes('R2M0-251N-XYZ'));
  check('codes: Join with code on the PSP', /Join with code/.test(h));
  check('codes: no config.ini editing any more', !/online_relay/.test(h));
  check('codes: room first when the directory works', h.indexOf('Room code') < h.indexOf('Invite code'));
  check('codes: the public address still hidden', !h.includes('203.0.113.7'));
  const down = S({ codes: Object.assign({}, codes, { roomRegistered: false, directory: { name: 'relay.rayquaza.ca:27420', state: 'failed', error: 'directory full', tries: 9 } }) });
  const hd = O.statusHTML(down, {});
  check('directory down: the invite code comes first', hd.indexOf('Invite code') < hd.indexOf('Room code'));
  check('directory down: says why', /directory full/.test(hd));
  check('directory down: the banner says share the invite', /directory is unreachable/.test(O.describe(down).title));
  const off = O.statusHTML(S({ codes: Object.assign({}, codes, { roomRegistered: false, directory: { state: 'off' } }) }), {});
  check('directory off: the invite code comes first', off.indexOf('Invite code') < off.indexOf('Room code'));
  const lan = O.statusHTML(S({ verdict: 'lan-only', upnp: { state: 'none', wan: '' }, codes }), {});
  check('this network only: says outside players cannot reach', /players outside cannot reach this PC/.test(lan));
  const nopub = O.statusHTML(S({ codes: Object.assign({}, codes, { invite: '' }) }), {});
  check('no public address: no internet invite, says why', /no invite code for the internet/.test(nopub));
  check('directory setting in Settings', /List the room in the directory/.test(O.html(S({ codes }), null, {})));
  // one plain status and one sentence to paste (2026-11-05 streamlining)
  check('ready: the banner names the room code', O.describe(S({ codes })).title === 'Ready: friends can join with code KQTR', O.describe(S({ codes })).title);
  check('ready: the banner still says the ports are open', /UDP 27420-27421 open via UPnP/.test(O.describe(S({ codes })).text));
  check('ready, directory down: the banner names the invite code', /^Ready: friends can join with invite code SC07-2AGV$/.test(O.describe(down).title) || /directory is unreachable/.test(O.describe(down).title));
  const msg = O.shareText(S({ codes }));
  check('share: room code, how to join, the backup invite, same build and BIOS',
    /^Join my GBAdhoc room: KQTR\./.test(msg) && /Join with code/.test(msg) && /use SC07-2AGV/.test(msg) && /gba_bios\.bin/.test(msg), msg);
  check('share: never the public address', !msg.includes('203.0.113.7'));
  check('share: one big Copy button carries it', h.includes('id="on-copyshare"') && h.includes('data-copylabel="the invite message"'));
  const regging = S({ codes: Object.assign({}, codes, { roomRegistered: false, directory: { state: 'registering', tries: 0 } }) });
  check('share: the invite while the room code is not registered yet', /^Join my GBAdhoc room: SC07-2AGV\./.test(O.shareText(regging)), O.shareText(regging));
  const lanMsg = O.shareText(S({ verdict: 'lan-only', upnp: { state: 'none', wan: '' }, codes: Object.assign({}, codes, { invite: '' }) }));
  check('share: this network only offers the same-house code', /on our Wi-Fi: R2M0-251N-XYZ/.test(lanMsg), lanMsg);
  check('share: nothing to share when not hosting', O.shareText(S({ running: false, codes: {} })) === '');
  check('the same-house code is explained', /Same house or Wi-Fi as this PC/.test(h));
}
// router blocks hosting: one fix, said plainly; troubleshooting from real sessions; auto host
{
  const none = O.describe(S({ verdict: 'lan-only', upnp: { state: 'none', detail: '', wan: '' } }));
  check('blocked: the fix is named', /The fix: turn on UPnP/.test(none.text) && /forward UDP 27420-27421 to 192\.168\.1\.20/.test(none.text), none.text);
  check('blocked: letting the friend host is offered', /let your friend host/.test(O.html(S({ verdict: 'lan-only', upnp: { state: 'none', wan: '' } }), null, {})));
  const h = O.html(S({ running: false }), null, {});
  check('troubleshooting: one button to the README FAQ, no copy of it', h.includes('data-faq') && /#faq-and-troubleshooting$/.test(O.FAQ) && !/Turn the phone's Wi-Fi off|UDP 27500/.test(h));
  check('settings: host when the app starts, off by default', /Host when GBAdhoc XMB starts/.test(h) && !/id="on-auto"[^>]*class="tog on"/.test(h) && h.includes('<span class="tog " id="on-auto">'));
  check('settings: host when the app starts, on', O.html(S({ running: false }), null, { autoHost: true }).includes('<span class="tog on" id="on-auto">'));
}
// network check
{
  const c = { lan: ['192.168.1.20'], upnp: { state: 'found', detail: '', router: 'Fake Router', wan: '100.72.1.2', wanClass: 'cgnat', local: '192.168.1.20' }, relay: { name: 'relay.rayquaza.ca:27420', state: 'nodns', pub: '', nat: 'unknown' }, verdict: 'cgnat' };
  const h = O.checkHTML(c, false);
  check('check: router found', h.includes('Fake Router'));
  check('check: WAN hidden until Show', !h.includes('>100.72.1.2<') && h.includes('data-secret="100.72.1.2"'));
  check('check: CGNAT verdict', h.includes('Cannot host for the internet: CGNAT'));
  check('check: shared relay not found', h.includes('no DNS answer'));
  check('check: busy', O.checkHTML(null, true).includes('Checking'));
}
// a busy port (owner 2026-11-05: the standalone relay already owns 27420 on this PC)
{
  const rel = { port: 27420, freePort: 27422, relay: true, addr: '10.0.0.40', invite: 'ABCD-EFGH', inviteLan: '1800-0A0Y', upnp: { state: 'found', to: '', wanClass: 'public' } };
  check('busy: checking', O.conflictHTML({ checking: true, port: 27420 }, {}).includes('Checking whether it is a GBAdhoc relay'));
  const offer = O.statusHTML(S({ running: false }), { conflict: rel });
  check('busy: relay found, offered', offer.includes('A GBAdhoc relay is already running on this PC') && offer.includes('10.0.0.40:27420') && offer.includes('data-useext'));
  check('busy: next free port offered', offer.includes('data-hostport="27422"'));
  check('busy: no "Not hosting" note under the offer', !offer.includes('onote"><b>Not hosting'));
  const used = O.statusHTML(S({ running: false }), { conflict: rel, useExternal: true });
  check('busy: use it shows the invites', used.includes('ABCD-EFGH') && used.includes('1800-0A0Y'));
  check('busy: unverified internet invite says so', used.includes('forwarded to this PC'));
  check('busy: mapped invite verified', !O.statusHTML(S({ running: false }), { conflict: { ...rel, upnp: { state: 'mapped' } }, useExternal: true }).includes('Needs UDP'));
  check('busy: another program: no card', O.conflictHTML({ ...rel, relay: false }, {}) === '');
  check('busy: hidden while hosting', !O.statusHTML(S({ running: true, verdict: 'open', upnp: { state: 'mapped' } }), { conflict: rel }).includes('already running on this PC'));
}
// the log
{
  const h = O.logHTML(S({ log: [{ t: '12:00:01', tone: 'ok', text: 'Room KQTR opened by a PSP (LAN, 192.168.1.31)' }, { t: '12:00:09', tone: 'warn', text: 'Refused a PSP (internet, 198.51.x.x): different GBAdhoc build' }] }));
  check('log: newest first', h.indexOf('Refused') < h.indexOf('Room KQTR'));
  check('log: escapes', !O.logHTML(S({ log: [{ t: '1', tone: '', text: '<img src=x>' }] })).includes('<img'));
}
console.log(`${fails ? 'FAILED' : 'PASS'}: online UI, ${n - fails}/${n} checks`);
process.exit(fails ? 1 : 0);
