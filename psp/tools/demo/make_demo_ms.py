#!/usr/bin/env python3
"""The README demo's Memory Stick (tools/demo/record_demo.sh).

    python tools/demo/make_demo_ms.py <out dir> --roms <your ROM folder>
        --gbadhoc <GBAdhoc release's PSP/GAME/GBAdhoc> [--art <dir with boxart/ and hero/>]

Writes <out>/ms/: a tidy GBAdhoc install (the release you name) with a handful
of well-known games from YOUR OWN ROM folder, copied for the recording only.
NO ROM IS EVER COMMITTED OR SHIPPED: the card lives outside the repo, and a
game missing from your folder is skipped.  GBAdhoc's own box and hero art is
copied where it has it; with --scrape, the other games' box art comes from
libretro-thumbnails (the source the app's own "Get art" uses), as if fetched
and kept earlier.  LIVE games are left without box art, so "Get art from the
internet" has something to fetch.  For those, the hero art is put in the
app's download cache (GBAdhocXMB/cache, as a finished 480x272 download), so
the live fetch gets the box art from libretro-thumbnails and finds the hero
already downloaded: PPSSPP cannot decode the 1376x768 hero masters (the
decode's allocation fails there; the PSP itself is fine).
"""
import argparse
import os
import shutil
import sys
import urllib.parse
import urllib.request

SYSTEMS = {'.gba': 'Nintendo - Game Boy Advance', '.gbc': 'Nintendo - Game Boy Color', '.gb': 'Nintendo - Game Boy'}

# (folder under roms/, file in your ROM folder (relative), box art, hero art)
GAMES = [
    ('', 'Pokemon - Emerald Version (USA, Europe).gba', 1, 1),
    ('', 'Golden Sun (USA, Europe).gba', 1, 1),
    ('', 'Legend of Zelda, The - The Minish Cap (USA).gba', 1, 1),
    ('', 'Castlevania - Aria of Sorrow (USA).gba', 1, 1),
    ('', 'Advance Wars 2 - Black Hole Rising (USA).gba', 1, 1),
    ('', 'Fire Emblem - The Sacred Stones (USA, Australia).gba', 1, 1),
    ('', 'Final Fantasy Tactics Advance (USA).gba', 1, 1),
    ('GBA', 'Kirby & The Amazing Mirror (USA).gba', 0, 1),       # box art fetched live in the demo
    ('GBA', 'Mario Kart - Super Circuit (USA).gba', 0, 1),
    ('GBA', 'Sonic Advance 2 (USA) (En,Ja,Fr,De,Es,It).gba', 0, 1),
    ('GBC', 'GBC/Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc', 0, 1),
    ('GBC', "GBC/Legend of Zelda, The - Link's Awakening DX (USA, Europe) (Rev 2) (SGB Enhanced) (GB Compatible).gbc", 0, 1),
    ('GB', 'GB/Super Mario Land (World) (Rev 1).gb', 0, 1),
]


def fnv(s):
    h = 2166136261
    for b in s.encode('utf-8'):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


# games whose box art is fetched live; their hero is pre-cached (see above)
LIVE = ['Kirby & The Amazing Mirror (USA)']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--roms', required=True)
    ap.add_argument('--gbadhoc', required=True, help="a GBAdhoc release's PSP/GAME/GBAdhoc folder")
    ap.add_argument('--art', default='', help='a folder holding boxart/ and hero/ (GBAdhoc\'s own art)')
    ap.add_argument('--scrape', action='store_true', help='fetch missing box art from libretro-thumbnails')
    ap.add_argument('--all-box', action='store_true', help='box art for the LIVE games too (the Windows app card)')
    a = ap.parse_args()
    ms = os.path.join(a.out, 'ms')
    if os.path.exists(ms):
        shutil.rmtree(ms)
    gb = os.path.join(ms, 'PSP', 'GAME', 'GBAdhoc')
    shutil.copytree(a.gbadhoc, gb, ignore=shutil.ignore_patterns('*.zip'))
    # GBAdhoc's settings for the launch recording: PPSSPP has no sleep handler to
    # take, so standby = 0 (else GBAdhoc says so in a toast); the GBA picture fits the screen
    with open(os.path.join(gb, 'CONFIG.INI'), 'w', newline='\n') as f:
        f.write('standby = 0\nscale_gba = 1\n')
    for d in ('roms', 'boxart', 'hero', 'saves'):
        os.makedirs(os.path.join(gb, d), exist_ok=True)
    os.makedirs(os.path.join(ms, 'PSP', 'SYSTEM'), exist_ok=True)
    n = 0
    for sub, rel, box, hero in GAMES:
        src = os.path.join(a.roms, rel)
        if not os.path.exists(src):
            print('skip (not in your folder):', rel)
            continue
        name = os.path.basename(rel)
        stem = os.path.splitext(name)[0]
        dst = os.path.join(gb, 'roms', sub) if sub else os.path.join(gb, 'roms')
        os.makedirs(dst, exist_ok=True)
        shutil.copy(src, os.path.join(dst, name))
        n += 1
        for kind, want in (('boxart', box), ('hero', hero)):
            if not want or not a.art:
                continue
            for ext in ('.png', '.565'):
                p = os.path.join(a.art, kind, stem + ext)
                if os.path.exists(p):
                    shutil.copy(p, os.path.join(gb, kind, stem + ext))
        bx = os.path.join(gb, 'boxart', stem + '.png')
        if a.scrape and (stem not in LIVE or a.all_box) and not os.path.exists(bx):
            ext = os.path.splitext(name)[1].lower()
            url = 'http://thumbnails.libretro.com/%s/Named_Boxarts/%s.png' % (
                urllib.parse.quote(SYSTEMS[ext]), urllib.parse.quote(stem.replace('&', '_')))
            try:
                data = urllib.request.urlopen(url, timeout=30).read()
                with open(bx, 'wb') as f:
                    f.write(data)
                print('box art (libretro-thumbnails):', stem)
            except OSError as e:
                print('no box art for', stem, e)
    cache = os.path.join(ms, 'PSP', 'GAME', 'GBAdhocXMB', 'cache')
    os.makedirs(cache, exist_ok=True)
    for stem in LIVE:
        p = os.path.join(a.art, 'hero', stem + '.png') if a.art else ''
        if p and os.path.exists(p):
            for name in {stem, stem.replace('&', '_')}:
                shutil.copy(p, os.path.join(cache, 'h%08x.png' % fnv(name)))
    print(f'demo card -> {ms}: {n} games')
    return 0 if n else 1


if __name__ == '__main__':
    sys.exit(main())
