#!/usr/bin/env python3
"""test_themes.py -- every theme defines every token, in both apps.

    python tools/test_themes.py

desktop/ui/brand.css: each [data-theme=...] block must define the same set of
custom properties (the union of all of them), and the four themes must be
graphite, slate, teal and classic.  psp/theme.c: each g_themes[] entry must
give a value for every field of theme_t (psp/theme.h) -- a missing trailing
initialiser would silently be 0, i.e. transparent black -- and the colours
both apps share must agree with brand.css.  Exit 0 on success.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THEMES = ["graphite", "slate", "teal", "classic"]
fail = []


def strip_comments(s):
    return re.sub(r"/\*.*?\*/", "", s, flags=re.S)


# ---- brand.css
css = strip_comments(open(os.path.join(ROOT, "desktop/ui/brand.css"), encoding="utf-8").read())
blocks = {}
for sel, body in re.findall(r"([^{}]+)\{([^{}]*)\}", css):
    names = re.findall(r"data-theme=(\w+)\]", sel)
    toks = dict(re.findall(r"(--[\w-]+)\s*:\s*([^;]+);", body))
    for n in names:
        blocks.setdefault(n, {}).update(toks)
if sorted(blocks) != sorted(THEMES):
    fail.append(f"brand.css themes are {sorted(blocks)}, want {sorted(THEMES)}")
every = set().union(*[set(b) for b in blocks.values()]) if blocks else set()
for t, b in blocks.items():
    miss = sorted(every - set(b))
    if miss:
        fail.append(f"brand.css [{t}] is missing {', '.join(miss)}")
# every token a stylesheet uses is defined (by a theme, by :root, or locally in desktop.css/app.css)
root_toks = set()
for sel, body in re.findall(r"([^{}]+)\{([^{}]*)\}", css):
    if sel.strip() == ":root":
        root_toks |= set(re.findall(r"(--[\w-]+)\s*:", body))
local = set()
used = set()
for f in ["desktop/ui/desktop.css", "desktop/ui/app.css", "desktop/ui/model.js", "desktop/ui/desktop.js", "desktop/ui/online.js"]:
    s = open(os.path.join(ROOT, f), encoding="utf-8").read()
    local |= set(re.findall(r"(--[\w-]+)\s*:", s))
    used |= set(re.findall(r"var\((--[\w-]+)", s))
undefined = sorted(used - every - root_toks - local)
if undefined:
    fail.append(f"used but defined nowhere: {', '.join(undefined)}")
print(f"brand.css: {len(blocks)} themes x {len(every)} tokens; {len(used)} tokens used by the UI")

# ---- psp/theme.h + theme.c
h = strip_comments(open(os.path.join(ROOT, "psp/theme.h"), encoding="utf-8").read())
struct = re.search(r"typedef struct\s*\{(.*?)\}\s*theme_t;", h, re.S).group(1)
fields = []
for decl in struct.split(";"):
    decl = decl.strip()
    if not decl:
        continue
    names = decl.split(None, 1)[1] if not decl.startswith("const char") else decl[len("const char"):]
    for n in names.split(","):
        n = n.strip().lstrip("*").strip()
        m = re.match(r"(\w+)\s*\[(\d+)\]", n)
        if m:
            fields += [f"{m.group(1)}[{i}]" for i in range(int(m.group(2)))]
        else:
            fields.append(n)
c = strip_comments(open(os.path.join(ROOT, "psp/theme.c"), encoding="utf-8").read())
table = re.search(r"g_themes\[TH_COUNT\]\s*=\s*\{(.*)\};", c, re.S).group(1)
entries, depth, cur = [], 0, ""
for ch in table:
    if ch == "{":
        depth += 1
        if depth == 1:
            cur = ""
            continue
    if ch == "}":
        depth -= 1
        if depth == 0:
            entries.append(cur)
            continue
    if depth >= 1:
        cur += ch if ch not in "{}" else " "
psp = {}
for e in entries:
    vals = [v.strip() for v in e.replace("\n", " ").split(",") if v.strip()]
    tid = vals[0].strip('"')
    if len(vals) != len(fields):
        fail.append(f"psp/theme.c [{tid}] has {len(vals)} values for {len(fields)} fields")
    psp[tid] = dict(zip(fields, vals))
if sorted(psp) != sorted(THEMES):
    fail.append(f"psp/theme.c themes are {sorted(psp)}, want {sorted(THEMES)}")
print(f"psp/theme.c: {len(psp)} themes x {len(fields)} fields")


def css_hex(v, toks):
    v = v.strip()
    m = re.match(r"var\((--[\w-]+)\)", v)
    if m:
        v = {"--r-lime": "#8ac32a", "--r-amber": "#f4bb20", "--r-pink": "#ef3f6d", "--r-teal": "#1fb5b0", "--r-violet": "#8d7bf3"}.get(m.group(1)) or toks.get(m.group(1), "")
    m = re.match(r"#([0-9a-f]{6})$", v.strip(), re.I)
    return m.group(1).upper() if m else None


# the colours both apps share: PSP field <- CSS token (rgb only; alpha is the PSP's own)
SHARED = {"ink0": "--ink-0", "fg": "--fg", "fg2": "--fg-2", "fg3": "--fg-3", "fg4": "--fg-4",
          "accent": "--accent", "accent_ink": "--accent-ink", "accent_edge": "--accent-edge",
          "focus": "--focus", "cross": "--cross", "ok": "--ok", "warn": "--warn", "bad": "--bad", "info": "--info",
          "ok_text": "--ok-text", "warn_text": "--warn-text", "warn_text2": "--warn-text-2", "bad_text": "--bad-text",
          "info_text2": "--info-text-2", "strip_fade": "--strip-fade", "empty_a": "--empty-a", "empty_b": "--empty-b",
          "xmb_bg": "--xmb-bg", "xmb_wave[0]": "--xmb-wave-a", "xmb_wave[1]": "--xmb-wave-b", "xmb_wave[2]": "--xmb-wave-c"}
# Classic keeps 0.1.0-beta.1's PSP pending-text tint; the desktop's was the accent itself
for t in THEMES:
    if t not in psp or t not in blocks:
        continue
    for f, tok in list(SHARED.items()) + ([("accent_text", "--accent-text")] if t != "classic" else []):
        want = css_hex(blocks[t].get(tok, ""), blocks[t])
        got = psp[t].get(f, "")
        m = re.match(r"0x([0-9A-Fa-f]{8})u?$", got)
        if not want or not m:
            fail.append(f"[{t}] {f}/{tok}: cannot compare {got!r} with {blocks[t].get(tok)!r}")
        elif m.group(1)[2:].upper() != want:
            fail.append(f"[{t}] psp {f} = #{m.group(1)[2:]} but brand.css {tok} = #{want}")
    for f, tok in [("card_hue", "--card-gba-hue"), ("card_sat", "--card-gba-sat"), ("card_light", "--card-gba-light"), ("card_spread", "--card-spread")]:
        if float(psp[t].get(f, "nan").rstrip("f")) != float(blocks[t].get(tok, "nan")):
            fail.append(f"[{t}] psp {f} = {psp[t].get(f)} but brand.css {tok} = {blocks[t].get(tok)}")

for f in fail:
    print("FAIL:", f)
print("themes: PASS" if not fail else f"themes: {len(fail)} failure(s)")
sys.exit(1 if fail else 0)
