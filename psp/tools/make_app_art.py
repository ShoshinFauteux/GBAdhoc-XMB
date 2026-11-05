#!/usr/bin/env python3
"""The app's own XMB art: assets/ICON0.PNG (144x80) and assets/PIC1.PNG (480x272).

    python tools/make_app_art.py            (from psp/; after bake_fonts.py)

Drawn from the design's tokens: the indigo ground with the violet glow
(psp.css .app), the GBAdhoc logo (the repo's own) and the violet XMB plate.
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPO = os.path.abspath(os.path.join(ROOT, '..'))
LOGO = os.path.join(REPO, 'desktop', 'ui', 'assets', 'logo-dark@2x.png')
FONT = os.path.join(ROOT, 'gen', '.inst', 'Inter-800.ttf')


def ground(w, h, cx, cy, rx, ry, glow):
    im = Image.new('RGB', (w, h))
    px = im.load()
    stops = [(0, (0x26, 0x22, 0x66)), (0.42, (0x14, 0x17, 0x40)), (1, (0x0c, 0x0e, 0x26))]
    for y in range(h):
        for x in range(w):
            t = math.hypot((x + .5 - cx) / rx, (y + .5 - cy) / ry)
            c = stops[-1][1]
            for (a, ca), (b, cb) in zip(stops, stops[1:]):
                if t <= b:
                    k = (t - a) / (b - a)
                    c = tuple(int(ca[i] + (cb[i] - ca[i]) * k) for i in range(3))
                    break
            gx, gy, gr = glow
            d = math.hypot(x + .5 - gx, y + .5 - gy) / gr
            if d < .62:
                a = .28 * (1 - d / .62)
                c = tuple(int(c[i] * (1 - a) + (0x8d, 0x7b, 0xf3)[i] * a) for i in range(3))
            px[x, y] = c
    return im


def plate(draw, cx, top, size, track):
    f = ImageFont.truetype(FONT, size)
    text = 'XMB'
    widths = [f.getlength(ch) for ch in text]
    tw = sum(widths) + track * (len(text) - 1)
    pw, ph = tw + size * 0.9, size * 1.55
    x0 = cx - pw / 2
    draw.rounded_rectangle((x0, top, x0 + pw, top + ph), radius=size * 0.4, fill=(0x8d, 0x7b, 0xf3))
    x = cx - tw / 2
    for ch, w in zip(text, widths):
        draw.text((x, top + ph / 2), ch, font=f, fill=(0x0c, 0x0b, 0x20), anchor='lm')
        x += w + track


def main():
    logo = Image.open(LOGO).convert('RGBA')
    # ICON0: logo over the plate, 4x then down
    S = 4
    ic = ground(144 * S, 80 * S, 144 * S, 0, 130 * S, 56 * S, (120 * S, 10 * S, 90 * S)).convert('RGBA')
    lw = 112 * S
    lg = logo.resize((lw, int(lw * logo.height / logo.width)), Image.LANCZOS)
    ic.alpha_composite(lg, ((144 * S - lw) // 2, 10 * S))
    plate(ImageDraw.Draw(ic), 72 * S, 52 * S, 12 * S, 3.4 * S)
    ic.resize((144, 80), Image.LANCZOS).convert('RGB').save(os.path.join(ROOT, 'assets', 'ICON0.PNG'))
    # PIC1: the app's ground, wide and quiet (the XMB draws the column on it)
    pic = ground(480, 272, 480, 0, 432, 190, (420, 30, 212)).convert('RGBA')
    lw = 220
    lg = logo.resize((lw, int(lw * logo.height / logo.width)), Image.LANCZOS)
    lg.putalpha(lg.getchannel('A').point(lambda a: int(a * 0.14)))
    pic.alpha_composite(lg, (480 - lw - 28, 272 - lg.height - 34))
    pic.convert('RGB').save(os.path.join(ROOT, 'assets', 'PIC1.PNG'))
    print('assets/ICON0.PNG, assets/PIC1.PNG')


if __name__ == '__main__':
    main()
