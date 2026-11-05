#!/usr/bin/env python3
"""mkicon.py OUT.ico -- the app icon, drawn in code (no image files).

Design A's mark: a rounded violet plate (#8d7bf3) holding a rounded ink
square (#0f1230), with the GBAdhoc rainbow as a bar across its foot."""
import struct
import sys
import zlib

VIOLET = (0x8d, 0x7b, 0xf3)
INK = (0x0f, 0x12, 0x30)
RAINBOW = [(0x8d, 0x7b, 0xf3, .14), (0x8a, 0xc3, 0x2a, .22), (0xef, 0x3f, 0x6d, .14), (0xf4, 0xbb, 0x20, .11),
           (0x8a, 0xc3, 0x2a, .11), (0x1f, 0xb5, 0xb0, .13), (0x7f, 0x55, 0xd6, .15)]


def rr_cover(x, y, x0, y0, x1, y1, r):
    """coverage (0..1) of pixel (x,y) by a rounded rect, 4x4 supersampled"""
    hit = 0
    for sy in range(4):
        for sx in range(4):
            px, py = x + (sx + .5) / 4, y + (sy + .5) / 4
            if px < x0 or px > x1 or py < y0 or py > y1:
                continue
            cx = min(max(px, x0 + r), x1 - r)
            cy = min(max(py, y0 + r), y1 - r)
            if (px - cx) ** 2 + (py - cy) ** 2 <= r * r:
                hit += 1
    return hit / 16


def draw(n):
    px = []
    for y in range(n):
        row = []
        for x in range(n):
            a = rr_cover(x, y, 0, 0, n, n, n * .22)
            col = VIOLET
            ins = n * .2
            b = rr_cover(x, y, ins, ins, n - ins, n - ins, n * .1)
            col = tuple(int(VIOLET[i] * (1 - b) + INK[i] * b) for i in range(3))
            # the rainbow bar inside the ink square, near its foot
            if n >= 32:
                bx0, bx1 = ins + n * .08, n - ins - n * .08
                by0, by1 = n - ins - n * .2, n - ins - n * .12
                if bx0 <= x + .5 <= bx1 and by0 <= y + .5 <= by1:
                    f = (x + .5 - bx0) / (bx1 - bx0)
                    acc = 0
                    for (r_, g_, b_, w_) in RAINBOW:
                        acc += w_
                        if f <= acc:
                            col = (r_, g_, b_)
                            break
            row.append(col + (int(255 * a),))
        px.append(row)
    return px


def png(px):
    n = len(px)
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in px)

    def ch(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0)) +
            ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))


sizes = [16, 24, 32, 48, 64, 256]
imgs = [png(draw(s)) for s in sizes]
hdr = struct.pack("<HHH", 0, 1, len(sizes))
off = 6 + 16 * len(sizes)
dirs = b""
for s, d in zip(sizes, imgs):
    dirs += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(d), off)
    off += len(d)
open(sys.argv[1], "wb").write(hdr + dirs + b"".join(imgs))
print("icon:", sys.argv[1])
