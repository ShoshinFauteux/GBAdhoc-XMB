#!/usr/bin/env python3
"""Snap the GBAdhoc logo's rainbow underline to whole pixel rows.

    python tools/snap_rule.py            (from psp/: patches gen/xmb_art.h in place)

bake_art.py box-filters the 2x logo down to 176x64 / 104x38 / 66x24, so the
underline (a thin multi-colour bar) lands across two rows at partial alpha,
and the two rows carry DIFFERENT colours (the source bar's top highlight and
its body).  On the PSP screen each colour segment then reads at a different
height -- the owner saw a stepped, ragged underline on the Go.  This keeps
the bar's colours and its total weight but draws it as N fully opaque rows
(N = the summed coverage, rounded, >= 1) with each column's colour the
alpha-weighted mix of the rows it came from, and drops the faint end columns.

A "rule row" is a row whose non-zero alpha spans >= 80% of the image width
with near-constant alpha (letters vary; a bar does not).  bake_art.py calls
snap(); run alone it patches the committed header the same way.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HDR = os.path.join(os.path.dirname(HERE), 'gen', 'xmb_art.h')


def _is_rule(row, w):
    a = [p[3] for p in row]
    lit = [i for i, x in enumerate(a) if x]
    if not lit or (lit[-1] - lit[0] + 1) < 0.8 * w:
        return False
    core = a[lit[0] + 2:lit[-1] - 1]
    return bool(core) and max(core) - min(core) <= 8 and min(core) > 0


def snap(px, w, h):
    """px: list of rows, each a list of (r, g, b, a).  Modified in place."""
    y = h - 1
    while y >= 0:
        if not _is_rule(px[y], w):
            y -= 1
            continue
        y1 = y
        while y - 1 >= 0 and _is_rule(px[y - 1], w):
            y -= 1
        rows = list(range(y, y1 + 1))
        weight = sum(max(px[r][x][3] for x in range(w)) for r in rows) / 255.0
        n = max(1, int(weight + 0.5))
        # the n rows kept: the strongest contiguous window (bottom-most on a tie)
        if n >= len(rows):
            keep = list(range(y, min(h, y + n)))
        else:
            best = max(range(y, y1 - n + 2),
                       key=lambda s: (sum(p[3] for r in range(s, s + n) for p in px[r]), s))
            keep = list(range(best, best + n))
        peak = max(max(p[3] for p in px[r]) for r in rows)
        out = []
        for x in range(w):
            a = sum(px[r][x][3] for r in rows)
            if max(px[r][x][3] for r in rows) < peak / 2:
                out.append((0, 0, 0, 0))
                continue
            c = tuple(sum(px[r][x][k] * px[r][x][3] for r in rows) // max(1, a) for k in range(3))
            out.append(c + (255,))
        for r in set(rows) | set(keep):
            px[r] = [out[x] if r in keep else (0, 0, 0, 0) for x in range(w)]
        y -= 1
    return px


def _patch_header(path):
    src = open(path).read()
    for name in ('logo_top', 'logo_boot', 'logo_item'):
        m = re.search(r'/\* %s: (\d+)x(\d+) in (\d+)x(\d+) \*/\n(static const unsigned int [^{]*\{)(.*?)(\};)' % name, src, re.S)
        w, h, tw, th = (int(m.group(i)) for i in range(1, 5))
        words = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', m.group(6))]
        px = [[(words[y * tw + x] & 255, words[y * tw + x] >> 8 & 255, words[y * tw + x] >> 16 & 255,
                words[y * tw + x] >> 24) for x in range(w)] for y in range(h)]
        snap(px, w, h)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[y][x]
                words[y * tw + x] = r | g << 8 | b << 16 | a << 24
        body = '\n' + ''.join('  ' + ','.join('0x%08x' % v for v in words[i:i + 12]) + ',\n'
                              for i in range(0, len(words), 12))
        src = src[:m.start(6)] + body + src[m.end(6):]
    open(path, 'w', newline='\n').write(src)
    print('snapped the underline in', path)


if __name__ == '__main__':
    _patch_header(sys.argv[1] if len(sys.argv) > 1 else HDR)
