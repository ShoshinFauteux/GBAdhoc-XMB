# Changelog

## 0.2.0-beta.1

**Host online room (Windows, experimental).** The Windows app becomes a 2-in-1: besides making
XMB entries, its new **Online** tab can host GBAdhoc's online play (4.6.0-beta.4 and later) on
this PC, so the PSPs connect here instead of the shared relay.

- **The same relay as the server.** GBAdhoc's GBR1 relay core is vendored in `shared/relay/`
  (from GBAdhoc `cb6eb47f`, GPL-2.0-or-later) and runs on a background thread of the app. It
  keeps the server's hardening: the cookie challenge, no amplification, per-address and
  per-player rate limits, expiring rooms, fixed-size tables. One read-only addition lists the
  rooms for the tab.
- **UPnP-IGD.** Hosting asks the router to forward UDP 27420-27421 (lease 1 h, renewed every
  30 min; without a lease when the router only allows that) and removes the mappings on Stop.
  An answer that points outside your network is never followed.
- **Says when it cannot work.** No UPnP router, a refused mapping, a port already forwarded to
  another device, carrier-grade NAT or a second router in front: each gets its own message, and
  the shared relay (`relay.rayquaza.ca`) is suggested. PSPs on your network can still join.
- **The tab:** Host / Stop; the listening ports, the router, where the room is reachable from,
  and the public address (hidden until *Show*); every room's code and its players with the ping
  to this PC, the loss and packets per second; a network check; a log. And the note: *no game
  files pass through this PC*.
- **Never touches the firewall.** The tab explains what to allow if Windows asks. While hosting,
  the PC is kept from sleeping.
- **Tests** (`desktop/tests/online/test_online.sh`, loopback only): the relay core's NAT model and
  fuzz test (ASan/UBSan, and again built with mingw), the UPnP client against a simulated router
  in 11 modes, start/stop x50 with no leaked allocation, handle, thread or socket, two simulated
  consoles through create, join, the relay and the direct path, and the real app's Online tab.
- The PSP app is unchanged apart from its version number.

## 0.1.0-beta.2

**Themes.** Both apps now come in four colour themes, chosen in Settings › Theme:

- **Graphite**, the new default: neutral near-black greys, a white focus ring and the logo's
  amber as the one accent. Warnings move to orange so they never read as the accent.
- **Slate**: cool slate greys with a steel-blue accent.
- **Teal**: the slate ground with the logo's own teal as the accent; information moves to
  steel blue.
- **Classic**: the indigo and violet of 0.1.0-beta.1, unchanged.

On the PSP, press ◀ or ▶ on the Theme row; on Windows, click a theme. The change applies at
once and is kept: `theme = graphite` in the PSP app's `xmbapp.ini`, `"palette"` in the Windows
app's `prefs.json`. Dark and light in the Windows app work with every theme.

- **Title cards follow the theme.** A Game Boy Advance game with no art gets a title card in
  the theme's colour family. Entries already written keep their icon; only entries made or
  updated after a change use the new theme.
- **The app's own icon** in the XMB is drawn in Graphite.
- **PSP Settings scroll**, so the last rows are no longer cut off by the button bar.
- **Contrast.** Graphite, Slate and Teal meet WCAG AA for every piece of text on the PSP
  (`psp/tools/check_shots.py`). Classic keeps beta 1's colours exactly, including its dimmer
  small text.
- Windows: the confidence chips on art keep readable colours in light mode; the selected card
  has a visible focus ring in light mode.
- **Fixed:** the PSP app no longer says "The network dialog could not open" when the dialog
  opened fine (a missing block in `psp/main.c`).

## 0.1.0-beta.1

The first test build: the PSP app and the Windows app, making XMB entries that open straight
into GBAdhoc 4.6.0-beta.4 or later.
