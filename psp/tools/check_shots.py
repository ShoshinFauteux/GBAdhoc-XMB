#!/usr/bin/env python3
"""check_shots.py -- contrast, size and overlap of the text in harness shots.

    python tools/check_shots.py <shots dir> [<shots dir> ...]

run_ppsspp.sh leaves <name>.png and <name>.txt per shot; the .txt lists the
text drawn on that frame (main.c, gfx_text_log): box, colour, size, string.
For each piece of text that is really on screen (its colour is found in its
box: text under a dialog's scrim is not), this checks
  - size: nothing under 10 px;
  - contrast: WCAG AA, 4.5:1 against the background measured in the shot
    (text faded on purpose, alpha < 0.9, is a disabled item and is skipped);
  - overlap: no two pieces of text overlap by more than 1 px.
Text on a game's own art (the XMB's labels on a hero picture) is reported
as a note: its contrast depends on the art.  Exit 1 if anything fails.
"""
import os, sys
from PIL import Image


def lum(c):
    def ch(x):
        x /= 255
        return x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4
    return 0.2126 * ch(c[0]) + 0.7152 * ch(c[1]) + 0.0722 * ch(c[2])


def ratio(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def load(txt):
    recs = []
    for ln in open(txt, encoding="utf-8", errors="replace"):
        p = ln.rstrip("\n").split(" ", 6)
        if len(p) < 7:
            continue
        x0, y0, x1, y1 = map(int, map(float, p[:4]))
        c = int(p[4], 16)
        recs.append({"box": (x0, y0, x1, y1), "a": (c >> 24) / 255, "rgb": ((c >> 16) & 255, (c >> 8) & 255, c & 255),
                     "px": float(p[5]), "s": p[6]})
    return recs


def check(png, txt, on_art=()):
    im = Image.open(png).convert("RGB")
    W, H = im.size
    px = im.load()
    fails, notes, vis = [], [], []
    for r in load(txt):
        if r["a"] < 0.9 or r["rgb"] == (0, 0, 0):
            continue                      # a disabled item, or a text shadow
        x0, y0, x1, y1 = r["box"]
        x0, y0, x1, y1 = max(0, x0), max(0, y0), min(W, x1), min(H, y1)
        cols = [px[x, y] for y in range(y0, y1) for x in range(x0, x1)]
        if not cols:
            continue
        near = [c for c in cols if ratio(c, r["rgb"]) < 1.25]
        if len(near) < max(2, len(cols) // 60):
            continue                      # not on screen (under a scrim or a panel)
        vis.append(r)
        if r["px"] < 9.95:
            fails.append(f"{r['px']:.1f} px text {r['s']!r} at {r['box']}")
        # the ground: the box and a 2 px ring around it (a thin glyph like '!' fills its own box)
        ring = [px[x, y] for y in range(max(0, y0 - 2), min(H, y1 + 2)) for x in range(max(0, x0 - 2), min(W, x1 + 2))]
        far = sorted(ring, key=lambda c: ratio(c, r["rgb"]))
        bg = far[len(far) * 3 // 5:]          # the 40 % least like the text: the ground
        bg = tuple(sorted(c[i] for c in bg)[len(bg) // 2] for i in range(3))
        cr = ratio(r["rgb"], bg)
        if cr < 4.5:
            msg = f"{cr:.2f}:1 {r['s']!r} #{'%02x%02x%02x' % r['rgb']} on #{'%02x%02x%02x' % bg} at {r['box']} ({r['px']:.1f} px)"
            (notes if any(a in r["s"] for a in on_art) else fails).append(msg)
    for i, a in enumerate(vis):
        for b in vis[i + 1:]:
            if a["s"] == b["s"] and a["box"][1] == b["box"][1]:
                continue                  # one text reported twice (a wrapped line's two boxes)
            ax0, ay0, ax1, ay1 = a["box"]
            bx0, by0, bx1, by1 = b["box"]
            ox, oy = min(ax1, bx1) - max(ax0, bx0), min(ay1, by1) - max(ay0, by0)
            if ox > 1 and oy > 1:
                fails.append(f"overlap {a['s']!r} {a['box']} and {b['s']!r} {b['box']}")
    return fails, notes, len(vis)


def main():
    sys.stdout.reconfigure(errors="replace")
    bad = 0
    # the XMB's own labels sit on a game's hero art (the arrival screen, the full preview)
    on_art = ("Golden Sun", "Pokemon", "Kirby", "Fire Emblem", "Mario Kart", "Tetris", "Game")
    for d in sys.argv[1:]:
        for f in sorted(os.listdir(d)):
            if not f.endswith(".txt") or not os.path.exists(os.path.join(d, f[:-4] + ".png")):
                continue
            name = f[:-4]
            fails, notes, n = check(os.path.join(d, name + ".png"), os.path.join(d, f), on_art if name in ("arrive", "full") else ())
            print(f"{os.path.basename(d.rstrip('/'))}/{name}: {n} texts, {'PASS' if not fails else 'FAIL'}"
                  + (f", {len(notes)} on art" if notes else ""))
            for m in fails:
                print("   FAIL", m)
            for m in notes:
                print("   note", m)
            bad += bool(fails)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
