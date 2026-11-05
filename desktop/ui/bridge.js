/* GBAdhoc XMB: the bridge to the native host (WebView2 web messages).
   HOST.call(cmd, args, onProgress) -> Promise; HOST.on(event, fn). */
(function () {
  'use strict';
  const wv = window.chrome && window.chrome.webview;
  let seq = 1;
  const pending = new Map();
  const handlers = {};
  if (wv) {
    wv.addEventListener('message', (e) => {
      const m = e.data;
      if (!m || typeof m !== 'object') return;
      if (m.event) { (handlers[m.event] || []).forEach(fn => { try { fn(m.data); } catch (err) { console.error(err); } }); return; }
      const p = pending.get(m.id);
      if (!p) return;
      if (m.progress) { if (p.onProgress) try { p.onProgress(m.progress); } catch (err) { console.error(err); } return; }
      pending.delete(m.id);
      if (m.ok) p.resolve(m.result);
      else { const err = new Error(m.error || 'Something went wrong.'); err.code = m.code || 'error'; p.reject(err); }
    });
  }
  function call(cmd, args, onProgress) {
    if (!wv) return Promise.reject(Object.assign(new Error('Open GBAdhoc-XMB.exe: this page needs the app around it.'), { code: 'nohost' }));
    const id = seq++;
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject, onProgress });
      wv.postMessage(JSON.stringify({ id, cmd, args: args || {} }));
    });
  }
  function on(ev, fn) { (handlers[ev] = handlers[ev] || []).push(fn); }
  window.HOST = { call, on, native: !!wv };
})();
