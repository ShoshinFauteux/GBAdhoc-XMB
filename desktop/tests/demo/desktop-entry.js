// The Windows app: open Pokemon Emerald's card, the XMB preview, Make entry, the arrival.
await sleep(700);
const card = () => [...document.querySelectorAll('.card')].find(c => /Pokemon Emerald/.test(c.textContent));
await click(card(), 900);
await sleep(1900);
await click('#pv-bg', 700); await sleep(900);
await click('#pv-bg', 400); await sleep(1000);
await click('#a-make', 800);
await waitFor(() => /is in the XMB/.test((document.querySelector('.ovl .dlg h3') || {}).textContent || ''), 30000);
await sleep(2600);
await click('.ovl .btns .b', 700);
await sleep(1200);
