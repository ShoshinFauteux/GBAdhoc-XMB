// rec.mjs -- record the real GBAdhoc XMB for Windows through WebView2's DevTools port.
//   node rec.mjs <port> shot <out.png>
//   node rec.mjs <port> rec <out dir> <actions.js> [tail ms]
// `rec` starts a screencast (every painted frame, with its timestamp), runs the
// actions script in the page (an async function body: the UI's own functions and
// DOM clicks, no desktop input), waits `tail` ms, stops, and writes frames
// 00001.png... plus frames.txt (file + timestamp) for a constant-rate resample.
import fs from 'node:fs';
import path from 'node:path';

const [port, mode, out, actions, tailMs] = process.argv.slice(2);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function target() {
  for (let i = 0; i < 100; i++) {
    try {
      const l = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const p = l.find((t) => t.type === 'page' && /xmb\.gbadhoc/.test(t.url));
      if (p) return p.webSocketDebuggerUrl;
    } catch (e) { /* not up yet */ }
    await sleep(300);
  }
  throw new Error('no page on port ' + port);
}

const ws = new WebSocket(await target());
await new Promise((r) => ws.addEventListener('open', r, { once: true }));
let seq = 1;
const pend = new Map();
const handlers = [];
ws.addEventListener('message', (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pend.has(m.id)) { const p = pend.get(m.id); pend.delete(m.id); m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); }
  else if (m.method) handlers.forEach((h) => h(m));
});
const send = (method, params = {}) => new Promise((res, rej) => { const id = seq++; pend.set(id, { res, rej }); ws.send(JSON.stringify({ id, method, params })); });

// REC_W x REC_H CSS px at REC_DSF: the page laid out and painted at that size, whatever the window
if (process.env.REC_W) {
  await send('Emulation.setDeviceMetricsOverride', { width: +process.env.REC_W, height: +process.env.REC_H, deviceScaleFactor: +(process.env.REC_DSF || 1), mobile: false });
  await sleep(500);
}
// wait for the app to have scanned
for (let i = 0; i < 200; i++) {
  const r = await send('Runtime.evaluate', { expression: '!!(window.XMB && window.XMB.games.length && window.DESK && !window.DESK.ui.scanning)', returnByValue: true });
  if (r.result.value) break;
  await sleep(200);
}
await sleep(1500);

if (mode === 'shot') {
  const r = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false });
  fs.writeFileSync(out, Buffer.from(r.data, 'base64'));
  console.log('shot', out);
} else {
  fs.mkdirSync(out, { recursive: true });
  for (const f of fs.readdirSync(out)) if (/^\d+\.png$/.test(f)) fs.unlinkSync(path.join(out, f));
  const lines = [];
  let n = 0;
  handlers.push((m) => {
    if (m.method !== 'Page.screencastFrame') return;
    n++;
    const f = String(n).padStart(5, '0') + '.png';
    fs.writeFileSync(path.join(out, f), Buffer.from(m.params.data, 'base64'));
    lines.push(`${f} ${m.params.metadata.timestamp}`);
    send('Page.screencastFrameAck', { sessionId: m.params.sessionId }).catch(() => {});
  });
  await send('Page.startScreencast', { format: 'png', everyNthFrame: 1 });
  await sleep(400);
  const body = fs.readFileSync(actions, 'utf8');
  const pre = fs.readFileSync(new URL('./prelude.js', import.meta.url), 'utf8');
  const r = await send('Runtime.evaluate', { expression: `(async () => { ${pre}\n${body}\n})()`, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) console.log('actions error', JSON.stringify(r.exceptionDetails).slice(0, 400));
  await sleep(parseInt(tailMs || '800', 10));
  await send('Page.stopScreencast');
  await sleep(300);
  fs.writeFileSync(path.join(out, 'frames.txt'), lines.join('\n') + '\n');
  console.log('frames', n);
}
ws.close();
process.exit(0);
