<p align="center">
  <picture>
    <source srcset="docs/img/logo-xmb-dark.png" media="(prefers-color-scheme: dark)">
    <source srcset="docs/img/logo-xmb-light.png" media="(prefers-color-scheme: light)">
    <img src="docs/img/logo-xmb-light.png" alt="GBAdhoc XMB" width="400">
  </picture>
</p>

<h3 align="center">Every GBA, Game Boy and Game Boy Color game its own icon in your PSP's XMB,<br>opening straight into GBAdhoc.</h3>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/licence-GPL--2.0--or--later-5a5a5f" alt="Licence: GPL-2.0-or-later"></a>
  <a href="https://github.com/ShoshinFauteux/GBAdhoc-XMB/releases"><img src="https://img.shields.io/badge/release-0.2.0--beta.1%20(pre--release)-f5c518" alt="Release: 0.2.0-beta.1, pre-release"></a>
  <img src="https://img.shields.io/badge/PSP-1000%20%C2%B7%202000%20%C2%B7%203000%20%C2%B7%20Go-1fb5b0" alt="PSP-1000, 2000, 3000 and Go">
  <img src="https://img.shields.io/badge/Windows-10%20%C2%B7%2011-8ac32a" alt="Windows 10 and 11">
</p>

<p align="center">
  <a href="https://github.com/ShoshinFauteux/GBAdhoc-XMB/releases"><b>⬇&nbsp; Download the pre-release</b></a>
  &nbsp;&nbsp;·&nbsp;&nbsp; <a href="#how-it-works">How it works</a>
  &nbsp;&nbsp;·&nbsp;&nbsp; <a href="#install">Install</a>
  &nbsp;&nbsp;·&nbsp;&nbsp; <a href="#online-hosting">Online hosting</a>
  &nbsp;&nbsp;·&nbsp;&nbsp; <a href="#pre-release-more-work-before-10">Known issues</a>
  &nbsp;&nbsp;·&nbsp;&nbsp; <a href="https://github.com/ShoshinFauteux/GBAdhoc">GBAdhoc</a>
</p>

<p align="center">
  <img src="docs/img/hero.png" alt="GBAdhoc XMB for Windows, with Pokemon Emerald's entry open and its XMB preview, and in front of it a PSP whose screen shows GBAdhoc XMB's library with Golden Sun selected" width="880">
  <br>
  <sub><b>The Windows app and the PSP app.</b> Both screens captured from the real apps, on a test folder of placeholder ROMs; the PSP is a 3D render of the <a href="https://sketchfab.com/3d-models/sony-psp-dca89d10ec304d0cab76837750df7761">Sony PSP model</a> by <a href="https://sketchfab.com/strov">Ilya Ostrovsky</a>, <a href="http://creativecommons.org/licenses/by/4.0/">CC BY 4.0</a>.</sub>
</p>

**GBAdhoc XMB is a companion to [GBAdhoc](https://github.com/ShoshinFauteux/GBAdhoc),** the
GBA, Game Boy and Game Boy Color emulator for the PSP. It gives each of your games its own
entry in the XMB's Game column, with its own title, icon and background. Pick the game in the
XMB and it opens straight into GBAdhoc, with the same saves and save states as the game list.

It comes as two apps with one design. **The PSP app** does everything on the console: it
finds your ROMs, recognises them, fetches their art over Wi-Fi and makes the entries.
**The Windows app** does the same over USB, for a whole library at once.
Both come in four colour themes, chosen in Settings: **Graphite**, the default, shown here,
**Slate**, **Teal** and **Classic**.

An entry is a small launcher of about 130 KB and a `launch.ini` that names the ROM. **Your
ROMs are never copied.**

> **This is a pre-release, 0.1.0 beta 2.** It works on a real PSP Go, and there is more to do
> before 1.0: see [Pre-release: more work before 1.0](#pre-release-more-work-before-10).
> Entries need **GBAdhoc 4.6.0-beta.4 or later**, which added the launch mode they start. Beta 4 is in testing and will be published on the [GBAdhoc releases page](https://github.com/ShoshinFauteux/GBAdhoc/releases) shortly; until then entries cannot start a game.

<br>

## What it does

<table>
<tr>
<td width="50%" valign="top">
<img src="docs/img/psp-library.png" alt="The PSP app's library: Golden Sun selected in the filmstrip, its details, and a preview of where it will sit in the XMB" width="100%">
<b>Your library, on the PSP</b><br>
<sub>Every ROM in GBAdhoc's <code>roms</code> folder, recognised by its No-Intro name, with a preview of its place in the XMB. L and R filter by what is in the XMB, what is not, and what needs attention.</sub>
</td>
<td width="50%" valign="top">
<img src="docs/img/psp-game.png" alt="The PSP app's game screen for Golden Sun: title, box art, hero and icon rows, Get art from the internet and Make entry" width="100%">
<b>One game, your way</b><br>
<sub>Change the title with the on-screen keyboard, pick the box art and the hero art, or use a picture from the PSP. Then <i>Make entry</i>.</sub>
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src="docs/img/psp-make-all.png" alt="The PSP app's Make 9 entries dialog, showing each icon that will be written to ms0:/PSP/GAME" width="100%">
<b>All of them at once</b><br>
<sub><i>Make all entries</i> shows every icon before it writes, and how much space they take. Games with no art yet get a title card.</sub>
</td>
<td width="50%" valign="top">
<img src="docs/img/psp-in-the-xmb.png" alt="The PSP app's arrival screen: Golden Sun's new icon in the XMB's Game column, marked In the XMB" width="100%">
<b>In the XMB</b><br>
<sub>The new icon, where it will be in the Game column. Quit to the XMB and it is there.</sub>
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src="docs/img/desktop-entry.png" alt="The Windows app with Pokemon Emerald's entry open: an XMB preview, the title, the box art and the hero art" width="100%">
<b>The Windows app</b><br>
<sub>The same design over USB. It finds the PSP by itself, shows every game as a card, and previews each entry in the XMB before it writes.</sub>
</td>
<td width="50%" valign="top">
<img src="docs/img/desktop-queue.png" alt="The Windows app after a queue: 7 entries made, each listed with how it was made" width="100%">
<b>Safe to unplug</b><br>
<sub>Each entry is written to a staging folder, checked, and only then moved into place. Pull the cable mid-queue and nothing is left half-written; plug it back in and the queue resumes.</sub>
</td>
</tr>
</table>

## Features

### PSP app

- **Runs on the console.** A standalone app in the Game column. It finds GBAdhoc on the Memory
  Stick or on a PSP Go's internal storage and scans its `roms` folder, the per-system folders
  `roms/GBA`, `roms/GB` and `roms/GBC` included, plus up to 16 folders you add.
- **Recognises your games by name** against about 9,300 names built into the app (No-Intro titles and GBAdhoc-heroart's), so
  recognition works offline. ROM hacks with their own hero art are recognised too.
- **Fetches art over Wi-Fi:** box art from libretro-thumbnails, in the region you prefer, and
  hero art from GBAdhoc-heroart, over TLS. Art is downloaded once and kept in GBAdhoc's own
  `boxart` and `hero` folders, where GBAdhoc uses it too. Art GBAdhoc already has is used first.
- **Makes, updates and removes entries**, one at a time, for the games you mark, or all at once.
  An entry whose ROM has moved is found and can be fixed with one button.
- **Composes the icon** from the hero and the box art, uses the box art alone, or draws a title
  card for a game with no art yet. Your own pictures work too.
- **Previews the XMB** for each game before it writes anything.
- **Uses the PSP's own network settings** and works offline: without Wi-Fi you can still make
  entries with the art already on the stick.
- **Four colour themes.** Graphite (neutral greys with an amber accent, the default), Slate
  (cool greys, steel blue), Teal (the logo's teal) and Classic (the indigo and violet of the
  first beta). Change it in Settings › Theme with ◀ and ▶; it applies at once and is kept.

### Windows app

- **One exe, nothing to install.** The UI is inside it, shown by the WebView2 runtime that
  Windows 10 and 11 include. The same four colour themes as the PSP app, in Settings › Theme,
  each in dark or light.
- **Finds the PSP by itself**, whatever drive letter Windows gives it, on a PSP-1000, 2000, 3000
  or Go. A Go's Memory Stick and internal storage are both shown.
- **Recognises ROMs properly** against the No-Intro lists from libretro-database: by checksum,
  by file name or by the game code in the ROM's header. A ROM with a game's code but not its
  dump is recognised as a hack of that game.
- **Box art from libretro-thumbnails** in every region it exists in, **hero art from
  GBAdhoc-heroart**, or your own pictures, from the PSP's PICTURE folder or from the PC.
- **Make, update and remove,** one game or a whole queue, with a preview of the XMB for each.
- **Safe staged writes.** An entry is written to `<GBAdhoc>/xmb/stage/`, flushed, read back and
  only then moved into `PSP/GAME` in one rename. After an unplug, the next scan finishes or
  undoes what was interrupted, and the queue resumes at the game it stopped on.
- **Hosts GBAdhoc online play** (experimental): the Online tab runs GBAdhoc's relay on this
  PC, opens its ports with UPnP while you host, and shows the rooms, the players and their
  ping. See [Online hosting](#online-hosting).
- **Light on the network.** Lists and art are cached in `%LOCALAPPDATA%\GBAdhoc XMB`, GitHub is
  asked a few times a week, and when GitHub rate-limits, thumbnails.libretro.com is used
  instead.

## How it works

An entry is a folder in `PSP/GAME` that looks like any other game to the XMB:

```
PSP/GAME/XMB_Pokemon_Emerald/
├── EBOOT.PBP     about 130 KB: the launcher, with this game's title, ICON0 and PIC1
└── launch.ini    which ROM to start, and where GBAdhoc is
```

```ini
rom = Pokemon - Emerald Version (USA, Europe).gba
gbadhoc = PSP/GAME/GBAdhoc
```

- **Every entry has the same launcher code.** Only its title, its icon (ICON0, 144x80), its
  background (PIC1, 480x272) and its `launch.ini` differ.
- **The launcher starts your installed GBAdhoc** in its launch mode, with the ROM from
  `launch.ini`. Updating GBAdhoc updates every entry.
- **The theme is only the app's.** The one piece of an entry it reaches is the title card, the
  icon a game with no art gets: its Game Boy Advance cards are drawn in the theme's colour
  family (Game Boy and Game Boy Color cards look the same in every theme). An entry keeps the
  icon it was written with; changing the theme only changes entries made or updated after.
  Box art, hero art, your own pictures and the launcher are the same in every theme.
- **No ROM is copied.** The ROM stays in GBAdhoc's `roms` folder, so an entry is never a copy
  of a game. Saves, save states, settings and cheats are the game list's own: a game is the
  same game from its entry or from the list.
- *Exit* in the game goes back to the XMB, and *Quit to game list* opens GBAdhoc's list. If the
  ROM or GBAdhoc is missing, the launcher says so on screen, with the path it looked for.
- An entry on a PSP Go's internal storage can start a GBAdhoc on the Memory Stick, and the
  other way round.

Both apps write entries with the same code (`shared/xw_core.c`), so an entry is the same
whichever app made it. The full contract is in [docs/XMB-LAUNCH.md](docs/XMB-LAUNCH.md).

## Install

First, install **GBAdhoc 4.6.0-beta.4 or later** in `PSP/GAME/GBAdhoc`, with your ROMs in its
`roms` folder. Then use either app, or both.

### PSP app

1. Download `GBAdhoc-XMB-0.2.0-beta.1-psp.zip` from the
   [pre-release](https://github.com/ShoshinFauteux/GBAdhoc-XMB/releases) and unzip it to the
   **root** of your Memory Stick or your PSP Go's internal storage. It lands in
   `PSP/GAME/GBAdhocXMB`.
2. Start **GBAdhoc XMB** from the Game column. It finds GBAdhoc and scans your ROMs.
3. Open a game with ✕, choose *Get art from the internet* if you are online, then *Make entry*.
   Or press △ for the library actions and choose *Make all entries*.
4. Quit with HOME. Your games are in the Game column.

### Windows app

1. Download `GBAdhoc-XMB-0.2.0-beta.1-windows.zip` from the
   [pre-release](https://github.com/ShoshinFauteux/GBAdhoc-XMB/releases) and unzip it anywhere.
2. Connect the PSP in USB mode (on a Go, Settings › USB Connection) and run
   `GBAdhoc-XMB.exe`. It finds the PSP and scans it.
3. Click a game for its entry, or use *Make all entries*. When the app says *Done*, leave USB
   mode. The entries are in the Game column.

The exe is not signed yet, so Windows SmartScreen may say *Windows protected your PC*. Click
**More info**, then **Run anyway**.

To undo everything, use *Remove entry* in either app, or delete the entry folders in `PSP/GAME`
(each one holds a `launch.ini`; the Windows app names them `XMB_<title>`).
ROMs, saves and GBAdhoc are never touched.

## Requirements

- **GBAdhoc 4.6.0-beta.4 or later.** Entries start GBAdhoc's launch mode, which earlier versions
  do not have.
- **A PSP running custom firmware.** Developed and tested on ARK-4. PSP-1000, 2000, 3000 and Go.
- **For art over Wi-Fi on the PSP:** a 2.4 GHz network. For WPA2 networks, ARK-4 with its WPA2
  plugin. Without Wi-Fi the PSP app still makes entries, with the art already on the stick or
  a title card.
- **For the Windows app:** Windows 10 or 11 with the WebView2 runtime, which both include. The
  app explains what to do if it is missing.

## Online hosting

> **Experimental**, like GBAdhoc's online play itself (4.6.0-beta.4 and later).

GBAdhoc can carry its link play over the internet: the two PSPs meet at a relay server, which
passes their link traffic along, and talk to each other directly when their routers allow it.
The Windows app's **Online** tab can be that relay. *Host online room* runs GBAdhoc's own relay
(the same code as the shared server, vendored in `shared/relay/`) on a background thread, and
asks your router to open its ports with UPnP while you host.

<p align="center">
  <img src="docs/img/desktop-online.png" alt="The Windows app's Online tab while hosting: UDP 37440-37441 open via UPnP, the public address hidden behind Show, and room KQTR with two players, each with a ping to this PC, loss and packets per second" width="880">
  <br>
  <sub><b>The Online tab, hosting.</b> Captured from the real app in a loopback test: the router is a simulated UPnP device and the two players are GBAdhoc's simulated test consoles.</sub>
</p>

**What the tab shows:** Host / Stop; **the two codes to give a friend** (below); the ports it
listens on, the router's answer and where the room can be reached from (this network only, or
the internet too); the public address, hidden until you click *Show*; the room and its players,
with the path between them (*Direct* when the two PSPs reach each other, *Via host* when this PC
forwards for them), the ping from each PSP to this PC, the loss and packets per second; a
*Network check*; and a log of the room, players joining and leaving, the directory, and refusals.

**Two codes.** Hosting gives this PC one room and two ways to name it:

- the **room code**, four letters (`KQTR`). The app lists it in a small directory on the shared
  server (`relay.rayquaza.ca`): the PSP asks the directory where the room is, then talks to this
  PC. The directory only answers that lookup (about 100 bytes per player, and about 3.5 KB per
  hour the PC hosts for the refreshes); no game traffic ever goes through it.
- the **invite code**, eight characters (`SC07-2AGV`; eleven when the port is not GBAdhoc's
  default 27420). It holds this PC's public address and a check character, so it needs no server
  at all: when the directory is down, it is the one to share. Crockford base32 (no I, L, O or
  U); the PSP forgives case, dashes and O/0, I/L/1, and catches mistypes. A second, LAN invite
  code is shown for PSPs on this network.

**To play:**

1. On the PC, open **Online** and click **Host online room**. Windows may ask whether GBAdhoc
   XMB may use the network: allow it on the network you are on.
2. On each PSP (a GBAdhoc build with online play, Settings › Online play on): Wireless › Online ›
   **Join with code**, and type either code. One PSP creates the room with it, the other joins.
   PSPs on this network do the same.
3. The two PSPs try to reach each other directly (a hole punch); when their routers do not
   allow it, this PC forwards for them.

**The honest caveats:**

- **It needs UPnP and a public IPv4 address.** If your router has no UPnP, or refuses, or its
  own internet address is not public (carrier-grade NAT, a second router in front of it, many
  IPv6-only plans, dorms, phone hotspots), nobody on the internet can reach this PC. The tab
  says which of these it found, and suggests the **shared relay** (`relay.rayquaza.ca`, which
  needs nothing from your router) instead. PSPs on your own network can still join.
- **The PC must stay on.** The room lasts while the app is open and hosting. The app keeps the
  PC from sleeping while it hosts; closing it, or the lid, ends the room.
- **The guest sees the host's IP address, and the invite code contains it.** A player on the
  internet connects to your PC, so they learn its public IP; share the invite code only with
  people you would tell your address. With the shared relay, nobody sees anyone's address.
- **The firewall is yours.** The app never changes Windows Firewall settings. If you refused
  the prompt, allow GBAdhoc XMB in Windows Security › Firewall & network protection › Allow an
  app through firewall.
- **No game files pass through this PC**, only link traffic, the same as between two PSPs. The
  relay never reads or stores the game data it passes on.

**Security.** The same rules as the public server, from the same code: a cookie challenge before
any room is made (so a forged address gets nothing), no reply ever larger than the request that
caused it (no amplification), per-address limits on rooms and joins, per-player limits on
packets and bytes, rooms that expire, and fixed-size tables. The ports are open only while you
host; *Stop* removes the router's mappings. A UPnP answer is only followed to an address on your
own network.

**Known limits.** One room per hosting PC can be found by code (up to five players). A PSP at home behind the same
router as this PC plays *via host* with a remote player (this PC is on its LAN, so that costs a
few ms). An invite code is valid while this PC's public address and port stay the same. The
directory entry lives 3 minutes after the PC stops refreshing it (a crash), or ends at once on
*Stop*. The reachability check
confirms the router's public address with the shared relay but cannot test inbound traffic from
outside. NAT-PMP/PCP routers are not tried, only UPnP-IGD.

**Decided by the owner (2026-11-05).** Two codes, kept separate, in one PSP Join box told apart by
length: the room code first, the invite code as the backup. Codes never expire. The public address
is hidden until *Show*, but an invite code encodes it, so hiding it only matters to people who do
not have the code. No QR code. When the direct path fails, this PC relays (the default). The
directory listing is on by default (Settings › *List the room in the directory*). The long invite
form uses a CRC-7 check, not the mockup's low 7 bits of a CRC-8, which let some single typos through.

## Pre-release: more work before 1.0

GBAdhoc XMB has been tested on a real PSP Go, where it works. This is what we know is left:

- **The line through the logo on the PSP app's start screen:** fixed, awaiting confirmation on
  hardware.
- **A PSP Go's internal storage and the network dialog** have only been lightly tested on real
  hardware. Reports from a Go are welcome.
- **The Windows exe is unsigned**, so SmartScreen warns about it the first time.
- **A ROM hack's match confidence is fixed** rather than measured, so a hack's score does not
  yet say how close it is to its base game.
- **Online hosting is experimental.** It has been tested on this PC's loopback against a
  simulated router and simulated consoles, not yet with a real router and two PSPs over the
  internet. Reports are welcome, with the Online tab's log.
- **More art sources.** Today box art comes from libretro-thumbnails and hero art from
  GBAdhoc-heroart only.

Found something else? Open an [issue](https://github.com/ShoshinFauteux/GBAdhoc-XMB/issues)
with your PSP model, and for the PSP app, `PSP/GAME/GBAdhocXMB/log/xmbapp.log`; for the Windows
app, `%LOCALAPPDATA%\GBAdhoc XMB\log.txt`.

## Building from source

Docker supplies both toolchains: `pspdev/pspdev` for the PSP, and a small Debian image with
mingw-w64 for Windows (`desktop/Dockerfile`, built on first use).

```bash
./build.sh            # the launcher, the PSP app and the Windows app
./build.sh psp        # launcher/EBOOT.PBP and psp/EBOOT.PBP
./build.sh desktop    # launcher/EBOOT.PBP and desktop/dist/GBAdhoc-XMB.exe
```

It runs from Git Bash on Windows or from Linux and WSL. Each part also builds on its own; the
command is at the top of `launcher/Makefile`, `psp/Makefile` and `desktop/build.sh`.

Tests:

```bash
tools/test_core.sh        # Linux/WSL: entries checked byte by byte, PARAM.SFO against the SDK's mksfoex
desktop/test_app.sh       # Windows: the real app on fake PSPs, offline, cable pulls included
desktop/tests/online/test_online.sh  # Windows: online hosting, loopback only (relay core NAT model and
                                     # fuzz, UPnP vs a fake router, start/stop x50, two simulated consoles,
                                     # and the real app's Online tab)
psp/tools/run_ppsspp.sh   # Linux/WSL: the PSP app in PPSSPP, driven by a key script
python tools/test_themes.py                  # every theme defines every token, in both apps
python psp/tools/check_shots.py <shots dir>  # contrast, size and overlap of the text in those shots
```

The test folders hold placeholder ROMs only: correct headers and names, no game.

| Folder | What is in it |
|---|---|
| `launcher/` | The per-game launcher every entry carries |
| `psp/` | The PSP app. `psp/gen/` holds generated headers (fonts, art, the name list, the launcher), committed; `psp/tools/` regenerates them |
| `desktop/` | The Windows app: the C host, the WebView2 bridge and the UI in `desktop/ui/` |
| `shared/` | The entry writer and the No-Intro matcher both apps build; `shared/relay/`, GBAdhoc's online relay core ([provenance and licence](shared/relay/README.md)) |
| `tools/` | The command-line maker, the entry checker and the core test |
| `docs/` | [The entry contract](docs/XMB-LAUNCH.md) and [the shared core](docs/SHARED-CORE.md) |

## Credits

- **[GBAdhoc](https://github.com/ShoshinFauteux/GBAdhoc)**, the emulator these entries start.
  Its launch mode is what makes an entry possible.
- **[libretro-thumbnails](https://github.com/libretro-thumbnails)**, where the box art comes
  from, and **[libretro-database](https://github.com/libretro/libretro-database)**, for the
  No-Intro lists the Windows app recognises ROMs with.
- **[GBAdhoc-heroart](https://github.com/ShoshinFauteux/GBAdhoc-heroart)**, the hero art.
- **No-Intro**, whose naming every lookup is built on.
- **Mbed TLS**, for https:// on the PSP; **stb_image** by Sean Barrett, for decoding and encoding
  art; **Inter** by Rasmus Andersson and **JetBrains Mono**, the typefaces; **PSPSDK / pspdev**
  and **psp-cfw-sdk**, the PSP toolchain.
- **ShoshinFauteux**, project lead: chose the design, and tested on a real PSP Go.

Full third-party notices: [THIRD_PARTY.md](THIRD_PARTY.md).

## Licence

GPL-2.0-or-later, the same as GBAdhoc. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

GBAdhoc XMB contains no games, BIOS files or game artwork. PSP and XMB are trademarks of Sony
Interactive Entertainment, and Game Boy is a trademark of Nintendo; this project is not
affiliated with either.
