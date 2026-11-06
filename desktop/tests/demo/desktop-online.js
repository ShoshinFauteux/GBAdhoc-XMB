// Online: the Online tab, Host online room; the router (simulated UPnP) opens the ports,
// the room is ready with its code; two consoles (GBAdhoc's test clients) join.
// (No Copy button is pressed: it would write to this PC's clipboard.)
await sleep(600);
window.DESK.onView.port = 37440;
await click('#nav a[data-v=online]', 900);
await sleep(1300);
await click('#on-host', 800);
await waitFor(() => /Ready: friends/.test(document.body.textContent), 40000);
await sleep(1200);


await waitFor(() => (document.body.textContent.match(/Direct/g) || []).length >= 2, 40000).catch(() => {});
await sleep(600);
const room = document.querySelector(".oroom");
if (room) { room.scrollIntoView({ behavior: 'smooth', block: 'center' }); }
await sleep(4000);
