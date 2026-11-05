#!/usr/bin/env python3
"""Bake GBAdhoc XMB's faces into 8-bit coverage atlases + glyph/kerning tables.

    python tools/bake_fonts.py            (from psp/)

Writes gen/xmb_fonts.h.  Every face the design (design A, psp.css)
uses, at its CSS size and weight: Inter 4 (variable, OFL) instanced per weight
with opsz pinned at 14 -- what Google Fonts serves for `Inter:wght@...`, so the
shapes, advances and kerning are the ones the design was drawn with.

Advances and kerning are UNHINTED and fractional (1/64 px), from the font's
own hmtx and GPOS pair kerning, the way Chrome lays text out; the renderer
snaps each glyph to a whole pixel but carries the fractional pen, so a line is
as long on the PSP as in the browser.  The coverage itself is FreeType's
(Pillow), grayscale anti-aliased.

The atlas is GU_PSM_T8 with an alpha CLUT: the GE modulates the vertex colour
by coverage.  Both atlas dimensions are powers of two (the GE samples nothing
else; see tools/bake_font.py's note).  The CPU reads the same arrays to draw
title cards into ICON0 images.
"""
import os, io, sys
from PIL import Image, ImageFont, ImageDraw
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
VAR = os.path.join(ROOT, 'fonts', 'InterVariable.ttf')
OUT = os.path.join(ROOT, 'gen', 'xmb_fonts.h')
CACHE = os.path.join(ROOT, 'gen', '.inst')

# (C name, weight, px).  Sizes/weights from psp.css; title cards (data.js
# titleCard) use 800 at 19..11 and 7.5.
FACES = [
    ('F400_105', 400, 10.5), ('F400_11', 400, 11), ('F400_115', 400, 11.5), ('F400_12', 400, 12),
    ('F500_105', 500, 10.5), ('F500_12', 500, 12),
    ('F600_10', 600, 10), ('F600_105', 600, 10.5), ('F600_11', 600, 11), ('F600_115', 600, 11.5),
    ('F600_12', 600, 12), ('F600_13', 600, 13), ('F600_16', 600, 16),
    ('F700_10', 700, 10), ('F700_105', 700, 10.5), ('F700_11', 700, 11), ('F700_115', 700, 11.5), ('F700_12', 700, 12),
    ('F700_15', 700, 15),
    ('F800_75', 800, 7.5), ('F800_10', 800, 10), ('F800_105', 800, 10.5), ('F800_11', 800, 11),
    ('F800_12', 800, 12), ('F800_13', 800, 13), ('F800_14', 800, 14), ('F800_15', 800, 15),
    ('F800_16', 800, 16), ('F800_17', 800, 17), ('F800_19', 800, 19),
]

EXTRA = ('éÉèÈêëàáâäÀÁÂÄãåçÇíìîïÍóòôöõÓÖúùûüÚÜñÑßøæœ'
         '’‘“”…·×›‹—–™®©°•←→▲▼◀▶△○□✓⌫★☆')
CROSS = 0x2715           # ✕: not in Inter; synthesised (square, like the PSP's)
CPS = sorted(set(list(range(0x20, 0x7F)) + [ord(c) for c in EXTRA] + [CROSS]))
KERN_CPS = [c for c in CPS if c < 0x7F or chr(c) in '’‘“”']


def instance(weight):
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, f'Inter-{weight}.ttf')
    if not os.path.exists(p):
        f = TTFont(VAR)
        st = instancer.instantiateVariableFont(f, {'wght': weight, 'opsz': 14})
        st.save(p)
    return p


def gpos_pairs(tt, cmap, cps):
    """(cpA, cpB) -> kerning in font units, from GPOS 'kern' PairPos lookups."""
    g2c = {}
    for cp in cps:
        gn = cmap.get(cp)
        if gn:
            g2c.setdefault(gn, []).append(cp)
    glyphs = set(g2c)
    gpos = tt['GPOS'].table
    idx = set()
    for fr in gpos.FeatureList.FeatureRecord:
        if fr.FeatureTag == 'kern':
            idx.update(fr.Feature.LookupListIndex)
    out = {}

    def xadv(v):
        return getattr(v, 'XAdvance', 0) or 0 if v is not None else 0

    def subtables(lk):
        for st in lk.SubTable:
            if lk.LookupType == 9:
                yield st.ExtSubTable.LookupType, st.ExtSubTable
            else:
                yield lk.LookupType, st

    for li in sorted(idx):
        lk = gpos.LookupList.Lookup[li]
        for typ, st in subtables(lk):
            if typ != 2:
                continue
            cov = st.Coverage.glyphs
            if st.Format == 1:
                for i, first in enumerate(cov):
                    if first not in glyphs:
                        continue
                    for pvr in st.PairSet[i].PairValueRecord:
                        if pvr.SecondGlyph in glyphs:
                            k = xadv(pvr.Value1)
                            if k:
                                key = (first, pvr.SecondGlyph)
                                out.setdefault(key, k)
            else:
                c1 = st.ClassDef1.classDefs if st.ClassDef1 else {}
                c2 = st.ClassDef2.classDefs if st.ClassDef2 else {}
                for first in cov:
                    if first not in glyphs:
                        continue
                    r1 = st.Class1Record[c1.get(first, 0)]
                    for second in glyphs:
                        k = xadv(r1.Class2Record[c2.get(second, 0)].Value1)
                        if k:
                            out.setdefault((first, second), k)
    res = {}
    for (a, b), k in out.items():
        for ca in g2c[a]:
            for cb in g2c[b]:
                res[(ca, cb)] = k
    return res


def cross_mask(px, adv):
    """A square cross, anti-aliased by 4x supersampling."""
    s = 4
    n = int(round(px))
    im = Image.new('L', (n * s, n * s), 0)
    d = ImageDraw.Draw(im)
    w = max(1.0, px * 0.15) * s
    d.line([(s * 0.5, s * 0.5), (n * s - s * 0.5, n * s - s * 0.5)], fill=255, width=int(w))
    d.line([(n * s - s * 0.5, s * 0.5), (s * 0.5, n * s - s * 0.5)], fill=255, width=int(w))
    return im.resize((n, n), Image.LANCZOS)


def bake_face(name, weight, px):
    path = instance(weight)
    tt = TTFont(path)
    upm = tt['head'].unitsPerEm
    hmtx = tt['hmtx'].metrics
    cmap = tt.getBestCmap()
    scale = px / upm
    big = ImageFont.truetype(path, px * 4)
    asc = tt['hhea'].ascent * scale
    desc = -tt['hhea'].descent * scale
    glyphs = []
    for cp in CPS:
        if cp == CROSS:
            n = int(round(px * 0.62))
            m = cross_mask(px * 0.62, 0)
            adv = px * 0.8
            # centred on the x-height-ish middle of the caps
            cap = tt['OS/2'].sCapHeight * scale
            glyphs.append((cp, m, int(round((adv - n) / 2)), -int(round(cap / 2 + n / 2)), adv))
            continue
        gn = cmap.get(cp)
        if gn is None:
            continue
        adv = hmtx[gn][0] * scale
        ch = chr(cp)
        if ch == ' ':
            glyphs.append((cp, None, 0, 0, adv))
            continue
        # UNHINTED coverage, as Chrome draws it: FreeType's hinting at these
        # sizes squeezes glyphs a pixel narrower than their unhinted advance
        # (a gap after every 'm'), so the glyph is drawn 4x and box-filtered.
        S, P = 4, int(px) + 4
        cw, chh = (P * 3) * S, (P * 3) * S
        canvas = Image.new('L', (cw, chh), 0)
        ImageDraw.Draw(canvas).text((P * S, 2 * P * S), ch, font=big, fill=255, anchor='ls')
        small = canvas.resize((P * 3, P * 3), Image.BOX)
        b = small.getbbox()
        if b is None:
            glyphs.append((cp, None, 0, 0, adv))
            continue
        im = small.crop(b)
        glyphs.append((cp, im, b[0] - P, b[1] - 2 * P, adv))
    # Shelf packing at the width that stores the fewest bytes.  Only the rows
    # used are stored: the GE is told the next power-of-two height, but never
    # samples past a glyph's rectangle (one empty row/column pads each).
    def pack(aw):
        x = y = shelf = 0
        place = []
        for g in glyphs:
            im = g[1]
            if im is None:
                place.append((0, 0, 0, 0))
                continue
            w, h = im.size
            if x + w + 1 > aw:
                x = 0
                y += shelf + 1
                shelf = 0
            place.append((x, y, w, h))
            x += w + 1
            shelf = max(shelf, h)
        return place, y + shelf + 1
    best = None
    for aw in (128, 256, 512):
        place, need = pack(aw)
        if need <= 512 and (best is None or aw * need < best[0] * best[2]):
            best = (aw, place, need)
    aw, place, ah = best
    atlas = Image.new('L', (aw, ah), 0)
    for g, p in zip(glyphs, place):
        if g[1] is not None:
            atlas.paste(g[1], (p[0], p[1]))
    kern = gpos_pairs(tt, cmap, KERN_CPS)
    kern = {k: v * scale for k, v in kern.items() if abs(v * scale) >= 1 / 64}
    return dict(name=name, weight=weight, px=px, asc=asc, desc=desc, glyphs=glyphs, place=place,
                atlas=atlas, kern=kern, cap=tt['OS/2'].sCapHeight * scale,
                xh=tt['OS/2'].sxHeight * scale)


def c_bytes(b, per=24):
    return ',\n'.join('  ' + ','.join(str(x) for x in b[i:i + per]) for i in range(0, len(b), per))


def main():
    faces = [bake_face(*f) for f in FACES]
    o = io.StringIO()
    o.write('/* generated by tools/bake_fonts.py from fonts/InterVariable.ttf -- do not edit */\n')
    o.write('#ifndef XMB_FONTS_H\n#define XMB_FONTS_H\n#include "../xf.h"\n\n')
    total = 0
    for f in faces:
        n = f['name']
        raw = f['atlas'].tobytes()
        total += len(raw)
        o.write(f'static const unsigned char __attribute__((aligned(16))) xfa_{n}[{len(raw)}] = {{\n{c_bytes(raw)}\n}};\n')
        o.write(f'static const xf_glyph xfg_{n}[] = {{\n')
        for g, p in zip(f['glyphs'], f['place']):
            cp, im, xo, yo, adv = g
            o.write(f'  {{{cp},{p[0]},{p[1]},{p[2]},{p[3]},{xo},{yo},{int(round(adv * 64))}}},\n')
        o.write('};\n')
        ks = sorted((a << 16 | b, int(round(v * 64))) for (a, b), v in f['kern'].items())
        ks = [k for k in ks if k[1]]
        o.write(f'static const xf_kern xfk_{n}[] = {{\n')
        for i in range(0, len(ks), 8):
            o.write('  ' + ','.join(f'{{{k}u,{v}}}' for k, v in ks[i:i + 8]) + ',\n')
        if not ks:
            o.write('  {0,0}\n')
        o.write('};\n\n')
    o.write('#include "xmb_font_ids.h"\n')
    o.write('static const xf_face xf_faces[XF_COUNT] = {\n')
    for f in faces:
        n = f['name']
        ng = len(f['glyphs'])
        nk = len([1 for v in f['kern'].values() if int(round(v * 64))])
        o.write(f'  {{xfa_{n},{f["atlas"].size[0]},{f["atlas"].size[1]},xfg_{n},{ng},xfk_{n},{nk},'
                f'{f["asc"]:.4f}f,{f["desc"]:.4f}f,{f["cap"]:.4f}f,{float(f["px"])}f,{f["weight"]}}},\n')
    o.write('};\n\n#endif\n')
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w', encoding='utf-8') as fh:
        fh.write(o.getvalue())
    # the face ids alone, for every file that names a face (the data above
    # is included by gfx.c only)
    with open(os.path.join(os.path.dirname(OUT), 'xmb_font_ids.h'), 'w') as fh:
        fh.write('/* generated by tools/bake_fonts.py -- do not edit */\n#ifndef XMB_FONT_IDS_H\n#define XMB_FONT_IDS_H\n')
        fh.write('enum {\n' + ''.join(f'  XF_{f["name"][1:]},\n' for f in faces) + '  XF_COUNT\n};\n#endif\n')
    print(f'{len(faces)} faces, {total // 1024} KiB of atlas -> {OUT}')
    if len(sys.argv) > 1:     # debug sheet
        sheet = Image.new('L', (512, sum(f['atlas'].size[1] + 4 for f in faces)), 40)
        y = 0
        for f in faces:
            sheet.paste(f['atlas'], (0, y))
            y += f['atlas'].size[1] + 4
        sheet.save(sys.argv[1])


if __name__ == '__main__':
    main()
