#!/usr/bin/env python3
"""The app's own XMB art: assets/ICON0.PNG (144x80) and assets/PIC1.PNG (480x272).

    python tools/make_app_art.py [graphite|classic]   (from psp/)

Drawn from the design's tokens (desktop/ui/brand.css, psp/theme.c): the app's
ground with its glow (psp.css .app), the GBAdhoc logo (the repo's own) and
the XMB plate in the accent.  The app's own icon is baked at build time, so
it is drawn in the default theme, Graphite; `classic` draws 0.1.0-beta.1's
indigo and violet.
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPO = os.path.abspath(os.path.join(ROOT, '..'))
LOGO = os.path.join(REPO, 'desktop', 'ui', 'assets', 'logo-dark@2x.png')
FONT = os.path.join(ROOT, 'gen', '.inst', 'Inter-800.ttf')
FONT_VAR = os.path.join(ROOT, 'fonts', 'InterVariable.ttf')

# ground stops (0 / 42 / 100 %), glow colour and alpha, plate, plate ink
THEMES = {
    'graphite': ((0x2a, 0x2a, 0x2e), (0x17, 0x17, 0x19), (0x0d, 0x0d, 0x0e), (0xff, 0xff, 0xff), .07, (0xf5, 0xc5, 0x18), (0x14, 0x10, 0x04)),
    'classic': ((0x26, 0x22, 0x66), (0x14, 0x17, 0x40), (0x0c, 0x0e, 0x26), (0x8d, 0x7b, 0xf3), .28, (0x8d, 0x7b, 0xf3), (0x0c, 0x0b, 0x20)),
}
TH = THEMES['graphite']


def font(size):
    if os.path.exists(FONT):
        return ImageFont.truetype(FONT, size)
    f = ImageFont.truetype(FONT_VAR, size)     # Inter's variable font at weight 800
    f.set_variation_by_axes([14 if a['name'] == b'Optical size' else 800 for a in f.get_variation_axes()])
    return f


def ground(w, h, cx, cy, rx, ry, glow):
    im = Image.new('RGB', (w, h))
    px = im.load()
    stops = [(0, TH[0]), (0.42, TH[1]), (1, TH[2])]
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
                a = TH[4] * (1 - d / .62)
                c = tuple(int(c[i] * (1 - a) + TH[3][i] * a) for i in range(3))
            px[x, y] = c
    return im


def plate(draw, cx, top, size, track):
    f = font(size)
    text = 'XMB'
    widths = [f.getlength(ch) for ch in text]
    tw = sum(widths) + track * (len(text) - 1)
    pw, ph = tw + size * 0.9, size * 1.55
    x0 = cx - pw / 2
    draw.rounded_rectangle((x0, top, x0 + pw, top + ph), radius=size * 0.4, fill=TH[5])
    x = cx - tw / 2
    for ch, w in zip(text, widths):
        draw.text((x, top + ph / 2), ch, font=f, fill=TH[6], anchor='lm')
        x += w + track


def main():
    global TH
    import sys
    TH = THEMES[sys.argv[1] if len(sys.argv) > 1 else 'graphite']
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
