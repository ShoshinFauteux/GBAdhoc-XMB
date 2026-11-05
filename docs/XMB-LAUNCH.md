# The entry contract

What a GBAdhoc XMB entry is, what it asks of GBAdhoc, and what every maker in this
repository writes. GBAdhoc's side of it is its *launch mode*
([GBAdhoc issue #29](https://github.com/ShoshinFauteux/GBAdhoc/issues/29)), in
GBAdhoc 4.6.0-beta.4 and later.

## The design

- **A small launcher per game, not a copy of the emulator.** An entry is a folder
  `PSP/GAME/<folder>/` holding an `EBOOT.PBP` of about 130 KB plus its art, and a `launch.ini`.
  The EBOOT's code is the same launcher for every game (`launcher/`); what makes it a game is
  its PARAM.SFO title, its ICON0 (144x80) and PIC1 (480x272), and `launch.ini`.
- **The launcher starts the installed GBAdhoc** with
  `argv = { <GBAdhoc>/EBOOT.PBP, "launch", <rom> }`, through the firmware's own launch call for
  the device it is on (`sctrlKernelLoadExecVSHMs2` on a Memory Stick, `...Ef2` on a PSP Go's
  internal storage). Updating GBAdhoc updates every entry. Saves, save states, settings, cheats
  and art are the game list's own: a game is the same game from its entry or from the list.
- **No ROM is copied into an entry.** `launch.ini` names the ROM relative to GBAdhoc's `roms`
  folder, so an entry is never a shareable copy of a game.
- **GBAdhoc's launch mode:** the first session boots the named ROM with the list's own save and
  state paths. *Exit* returns to the XMB; *Quit to game list* opens the list. A ROM that is not
  there opens the list instead.

## launch.ini

```
rom = Pokemon - Emerald Version (USA, Europe).gba
gbadhoc = PSP/GAME/GBAdhoc
```

- `rom`: relative to GBAdhoc's `roms` folder (subfolders allowed, such as `GBA/` or `hacks/`),
  or an absolute `dev:/` path. A name with non-ASCII characters is written in its FAT 8.3 form,
  which the firmware always opens (the makers read it off the card; GBAdhoc's list does the same).
- `gbadhoc`: GBAdhoc's folder. A bare `PSP/GAME/...` is looked for on the entry's own device
  first, then on the other one (ms0: and ef0:), so an entry on a PSP Go's internal storage can
  start a GBAdhoc on its Memory Stick and the other way round.

The launcher checks both before launching and says on screen what is missing.

## The makers

All three write entries through the same code, `shared/xw_core.c`, so an entry is the same
bytes whichever one made it.

### On the PSP (`psp/`)

A standalone EBOOT. It finds GBAdhoc on the Memory Stick or the Go's internal storage, scans its
`roms` folder (and any folders added in Settings), recognises each ROM by its No-Intro name,
fetches box art (libretro-thumbnails) and hero art (GBAdhoc-heroart) over Wi-Fi with TLS
(mbedTLS), and makes, updates and removes entries. The names it recognises are baked into the
EBOOT (`psp/gen/xmb_names.h`, titles only), so recognition works offline.

### On Windows (`desktop/`)

`GBAdhoc-XMB.exe` finds the PSP by itself (any drive letter; a PSP Go's internal storage is
`SONY "PSP" SS`, ef0:), scans GBAdhoc's `roms` and any folders added in Settings, recognises
each ROM against the No-Intro lists (libretro-database: checksum, file name or game code; a ROM
with a game's code but not its dump is a hack of it), fetches box art from libretro-thumbnails
(Named_Boxarts) and hero art from GBAdhoc-heroart, shows each entry in a preview of the XMB,
and makes, updates and removes entries one at a time or as a queue.

- One exe, nothing to install: a C host with the UI inside it, shown in the WebView2 runtime
  that Windows 10 and 11 include (the app explains what to do if it is missing).
- Writing is staged: an entry is written to `<GBAdhoc>/xmb/stage/`, flushed, read back and
  only then moved into PSP/GAME in one rename. Pulling the cable never leaves half an entry in
  the XMB; the next scan finishes or undoes what was interrupted, and a queue resumes at the
  game it stopped on.
- Lists and art are cached in `%LOCALAPPDATA%\GBAdhoc XMB`; GitHub's API is asked a few times a
  week (ETags), and when it rate-limits, thumbnails.libretro.com is used instead.

### On the command line (`tools/xw_cli.c`)

`xmbwrap.exe` (built with the desktop app) makes one entry:

```
xmbwrap.exe --rom-file G:\PSP\GAME\GBAdhoc\roms\Game.gba [--title T]
            [--icon IMG] [--background IMG] [--no-art] [--folder NAME]
```

Any PNG, JPEG or BMP works as art; the icon is fitted into 144x80 and the background fills
480x272.

## Testing

- `tools/test_core.sh` (WSL or Linux, docker): a fake card, one entry per case, each checked by
  `tools/check_entry.py` (PBP layout, DATA.PSP is the launcher's, art sizes, launch.ini), and
  every PARAM.SFO byte-identical to the SDK's own `mksfoex` for the same title. It also runs
  the No-Intro matcher's unit test and builds the shared code with psp-gcc.
- `desktop/test_app.sh` (Windows, Git Bash): the real app, host, WebView2 and UI, on fake PSPs
  made by `desktop/tests/make_fakepsp.py` (placeholder ROMs, no games), offline, including
  a pulled cable in the middle of a queue and a PSP Go's internal storage.
- `psp/tools/run_ppsspp.sh` (Linux or WSL, a PPSSPP SDL build): the PSP app on a fixture card
  from `psp/tools/make_fixture.py` (empty placeholder ROMs), driven by a key script, with
  screenshots.
- On hardware: an entry on the Memory Stick and one on a PSP Go's internal storage; Exit, Quit
  to game list, sleep in the launched game, a save made from the entry seen from the list.
