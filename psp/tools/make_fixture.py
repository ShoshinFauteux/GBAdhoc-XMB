#!/usr/bin/env python3
"""A test Memory Stick for GBAdhoc XMB runs in PPSSPP (tools/run_ppsspp.sh).

    python tools/make_fixture.py <out dir> [<GBAdhoc EBOOT.PBP>]

Writes <out>/ms/ : PSP/GAME/GBAdhoc with the design's sample library as
PLACEHOLDER files (zero bytes, sparse, at the real sizes: the app reads only
names and sizes -- no ROM is involved), GBAdhoc's own boxart/hero for some
of them (drawn here: gradients, shapes and the title, no copyrighted art),
and a PICTURE folder.  The GBAdhoc EBOOT, if given, is copied so the app
finds a real install (its PARAM.SFO title carries the version).
"""
import os, shutil, sys
from PIL import Image, ImageDraw, ImageFont

ROMS = [
    ('roms', 'Pokemon - Emerald Version (USA, Europe).gba', 16),
    ('roms', 'Kirby & The Amazing Mirror (USA).gba', 8),
    ('roms/racing', 'Mario Kart - Super Circuit (USA).gba', 4),
    ('roms', 'Golden Sun (USA, Europe).gba', 8),
    ('roms/hacks', 'Pokemon MonoMyth v1.2.gba', 32),
    ('roms/hacks', "Fire Emblem - The Morrow's Golden Country.gba", 32),
    ('roms', 'Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc', 2),
    ('roms', 'Tetris (World) (Rev 1).gb', 0.03),
    ('roms', 'mygame_final2 (1).gba', 4),
    # GBAdhoc 4.6.0-beta.4's per-system subfolders
    ('roms/GBA', 'Kirby - Nightmare in Dream Land (USA).gba', 8),
    ('roms/GBC', 'Pokemon - Gold Version (USA, Europe).gbc', 2),
    ('roms/GB', "Kirby's Dream Land (USA, Europe).gb", 0.25),
]
# saves and states: never ROMs, never scanned (inside roms/ and beside it)
NOT_ROMS = ['roms/saves/Kirby - Nightmare in Dream Land (USA).sav', 'roms/states/Kirby - Nightmare in Dream Land (USA).st0',
            'saves/Golden Sun (USA, Europe).sav', 'states/Golden Sun (USA, Europe).st1',
            'roms/GBA/Kirby - Nightmare in Dream Land (USA).sav', 'roms/saves/decoy.gba']
OWN_BOX = {'Pokemon - Emerald Version (USA, Europe)': ((31, 143, 95), (11, 61, 44), 'Pokemon Emerald'),
           'Kirby & The Amazing Mirror (USA)': ((255, 122, 184), (91, 29, 87), 'Kirby'),
           'Mario Kart - Super Circuit (USA)': ((240, 90, 40), (58, 16, 48), 'Mario Kart'),
           'Pokemon - Crystal Version (USA, Europe) (Rev 1)': ((57, 198, 232), (21, 58, 110), 'Pokemon Crystal')}
OWN_HERO = {'Pokemon - Emerald Version (USA, Europe)': ((11, 61, 44), (31, 143, 95), (138, 195, 42)),
            'Mario Kart - Super Circuit (USA)': ((58, 16, 48), (240, 90, 40), (244, 187, 32))}
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'gen', '.inst', 'Inter-800.ttf')


def grad(w, h, a, b):
    im = Image.new('RGB', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = (x / w + y / h) / 2
            px[x, y] = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
    return im


def boxart(a, b, title):
    im = grad(320, 300, a, b)
    d = ImageDraw.Draw(im)
    for r in (120, 90, 60):
        d.ellipse((180 - r, 140 - r, 180 + r, 140 + r), outline=(255, 255, 255), width=8)
    d.rectangle((0, 0, 44, 300), fill=(91, 45, 142))
    f = ImageFont.truetype(FONT, 34) if os.path.exists(FONT) else ImageFont.load_default()
    d.text((60, 230), title, font=f, fill=(255, 255, 255))
    return im


def hero(a, b, c):
    im = grad(480, 272, a, b)
    d = ImageDraw.Draw(im)
    for r in (150, 110, 70):
        d.ellipse((340 - r, 90 - r, 340 + r, 90 + r), outline=c, width=10)
    return im


def main():
    out = os.path.join(sys.argv[1], 'ms')
    if os.path.exists(out):
        shutil.rmtree(out)
    gb = os.path.join(out, 'PSP', 'GAME', 'GBAdhoc')
    for folder, name, mb in ROMS:
        d = os.path.join(gb, folder)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, name), 'wb') as f:
            f.truncate(int(mb * 1024 * 1024))
    for rel in NOT_ROMS:
        p2 = os.path.join(gb, rel)
        os.makedirs(os.path.dirname(p2), exist_ok=True)
        with open(p2, 'wb') as f:
            f.truncate(128 * 1024)
    os.makedirs(os.path.join(gb, 'boxart'), exist_ok=True)
    os.makedirs(os.path.join(gb, 'hero'), exist_ok=True)
    for stem, (a, b, t) in OWN_BOX.items():
        boxart(a, b, t).save(os.path.join(gb, 'boxart', stem + '.png'))
    for stem, (a, b, c) in OWN_HERO.items():
        hero(a, b, c).save(os.path.join(gb, 'hero', stem + '.png'))
    if len(sys.argv) > 2:
        shutil.copy(sys.argv[2], os.path.join(gb, 'EBOOT.PBP'))
    pic = os.path.join(out, 'PICTURE')
    os.makedirs(pic, exist_ok=True)
    hero((40, 90, 160), (20, 30, 70), (255, 255, 255)).save(os.path.join(pic, 'IMG_0412.JPG'), 'JPEG')
    hero((220, 110, 40), (90, 40, 20), (255, 240, 200)).save(os.path.join(pic, 'sunset.png'))
    print('fixture ->', out)


if __name__ == '__main__':
    main()
