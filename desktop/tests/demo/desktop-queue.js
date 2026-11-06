// Safe to unplug: four games chosen, Make 4 entries; the third write "loses" the PSP
// (the host's XMB_FAULT test hook); the queue stops there, nothing half-written;
// the PSP is back, Resume, the queue finishes.
await sleep(600);
const cardOf = (re) => [...document.querySelectorAll('.card')].find(c => re.test(c.textContent));
for (const re of [/Castlevania/, /Final Fantasy/, /Fire Emblem/, /Golden Sun/]) {
  await click(cardOf(re).querySelector('.chk'), 520); await sleep(250);
}
await sleep(400);
await click('#makeall', 700);
await sleep(1300);
await click('.ovl .btns .b.primary', 600);
await waitFor(() => document.querySelector('#q-resume'), 40000);
await sleep(1800);
await waitFor(() => !document.querySelector('#q-resume').disabled, 20000);
await sleep(900);
await click('#q-resume', 700);
await waitFor(() => /entries made/.test((document.querySelector('#drawer h3') || {}).textContent || ''), 40000);
await sleep(2500);
