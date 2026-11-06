# Changelog

## Unreleased (planned 0.3.0-beta.1)

**Friends outside your network join with a code (Windows, experimental).** A PSP no longer has
to be pointed at this PC with `online_relay`: it types a code in GBAdhoc's Wireless › Online ›
Join with code.

- **Room code, via the directory.** Hosting picks a four-letter room code and lists it in a small
  directory on the shared server (`relay.rayquaza.ca`, GBAdhoc's new REGISTER / LOOKUP messages).
  A PSP looks the code up and then talks only to this PC; the directory never carries game
  traffic. Registration is refreshed every 60 s (32 bytes, answered with 24) and removed on Stop.
  On by default: Settings › *List the room in the directory* (`XMB_DIRECTORY=off` for experts).
- **Invite code, no server.** Eight Crockford base32 characters (this PC's public address and a
  CRC-8 check, the default port), eleven with another port (address, port, CRC-7). Works when
  the directory is down; the tab then puts it first. A LAN invite code is shown for PSPs at home.
- **Direct or via host.** Two PSPs that join this PC are introduced and hole-punch a direct path;
  this PC forwards only when that fails. Each player's row says *Direct* or *Via host*.
- **The same relay core**, vendored again from GBAdhoc `092a4879` (`shared/relay/README.md`):
  the directory, the home code, `relay_dirclient.c` and `invite_code.h`.
- **What it costs the shared server:** about 3.8 KB of UDP payload per two-player room-hour by
  room code (measured: 3,480 B an hour to stay listed, 176 B to register, 105 B per lookup),
  about 7 KB with IP/UDP headers. A room joined only by invite code costs the server just its
  registration.
- **Tests** (loopback only): the desktop-host + directory NAT model and the invite-code tests
  (every single typo and swap refused), fuzzing over the new messages, the Online tab's codes on
  fixtures, and a discovery run of the real host code: it registers with an in-process directory
  on 127.0.0.1, one console joins by the room code, the other by the invite code, and Stop frees
  the code. In PPSSPP, two GBAdhoc instances joined a desktop-mode host by invite code alone,
  went direct, and completed an Emerald Union Room trade; then again with one PSP by room code
  (one LOOKUP at a loopback directory, 393 B there for the whole session) and the other by invite
  code (GBAdhoc `docs/ONLINE-TUNNEL.md`). The
  fuzzer no longer checks a timer's PEER LEFT as a reply (a false "amplification" on one seed).
- **Owner decisions (2026-11-05):** both codes, one PSP Join box (room code first, invite code as
  the backup); codes never expire (an invite is valid while the address and port stay the same);
  the public address hidden until *Show* (an invite code encodes it, so that is cosmetic for
  anyone holding the code); no QR code; relay via this PC when the punch fails.
- **Both players join.** On the PSP both press *Join with code*; the first in hosts the session
  (GBAdhoc `claude/online-desktophost`). The PSP's online module checks the code (4 letters, or an
  invite's check) before it opens a socket, so the PSP app's boot memory is unchanged.
- **Vendored invite decoder** is GBAdhoc's 32-bit one; `test_invite` checks it against the first
  decoder on 4 M inputs.

**Windows app fixes (owner review of 0.2.0-beta.1).**

- **A busy port is handled.** When Host finds UDP 27420-27421 taken on this PC, the app sends a
  GBR1 PROBE there. If a GBAdhoc relay answers (the standalone `gbadhoc-relay`, say), the tab
  offers *Use it*: that relay's LAN invite, and its internet invite when the router forwards the
  port. It also offers *Host on the next free port*, and the invite code then carries the port.
  Another program on the port: the app hosts on the next free pair by itself. It never binds over
  the program holding the port, or stops it.
- **Any window size.** A maximised window fills: the UI zooms up to 1.3x on big windows (less
  under Windows' 125/150 % scaling), the library grid takes more columns, Settings, Art sources
  and Online centre a 1040 px column, and the empty states centre in the content area.
- **"Connect your PSP"** shows a render of the real PSP with GBAdhoc XMB on its screen (the README
  hero's model, "Sony PSP" by Ilya Ostrovsky, CC BY 4.0, credited in Settings › About) instead
  of the drawn one.
- Test flags for screenshots: `--size WxH`, `--max`, `--scale 1.25`.

**PSP hardware fixes (fix/hw-net-ui).**

- **PSP: Wi-Fi connects with any saved connection.** The app tried only connection slot 1; it now tries every saved XMB connection (the one that worked last first), as GBAdhoc does, and opens the network dialog only when none connects. Every network failure shows its step and firmware code (Settings > Network, toasts, log).
- **PSP: clean rounded shapes on hardware.** Pills, rings, outlines and the boot "XMB" plate no longer show seams or doubled edges: the shape tiles are no longer mirrored with flipped UVs and are sampled with filtering.
- **PSP: the logo's rainbow underline sits on whole pixel rows** instead of stepping between colours.

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
