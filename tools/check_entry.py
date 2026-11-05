#!/usr/bin/env python3
"""check_entry.py -- check one GBAdhoc XMB entry (tools/test_core.sh, desktop/test_app.sh).

  check_entry.py TEMPLATE.PBP DIR TITLE ROM GBADHOC ICON_WxH|- PIC1_WxH|-
  check_entry.py --sfo-ref REF.SFO ENTRY/EBOOT.PBP     (PARAM.SFO byte-identical)
  check_entry.py --card TEMPLATE.PBP CARD [EXPECT.json] (every entry on a card)

--card checks every PSP/GAME/<folder>/ with a launch.ini: the PBP's layout,
DATA.PSP is the launcher's, PARAM.SFO is the template's but for TITLE, ICON0
is a 144x80 PNG and PIC1 (if any) a 480x272 one, launch.ini's ROM and GBAdhoc
exist on the card, and nothing half-written is left in PSP/GAME.  EXPECT.json
(optional) maps folder -> {"title": ..., "rom": ...} that must match, and
lists the folders there must be ("folders").
"""
import json, os, struct, sys, zlib


def pbp(path):
    d = open(path, "rb").read()
    assert d[:4] == b"\0PBP", "not a PBP"
    offs = list(struct.unpack("<8I", d[8:40])) + [len(d)]
    return [d[offs[i]:offs[i + 1]] for i in range(8)]


def sfo(d):
    assert d[:4] == b"\0PSF"
    kt, dt, n = struct.unpack("<III", d[8:20])
    out = {}
    for i in range(n):
        ko, fmt, ln, mx, do = struct.unpack("<HHIII", d[20 + 16 * i:36 + 16 * i])
        key = d[kt + ko:d.index(b"\0", kt + ko)].decode()
        v = d[dt + do:dt + do + ln]
        out[key] = (fmt, v.rstrip(b"\0").decode("utf-8") if fmt == 0x0204 else struct.unpack("<I", v)[0], mx)
    return out


def png_size(b):
    if not b:
        return "-"
    assert b[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    w, h = struct.unpack(">II", b[16:24])
    return "%dx%d" % (w, h)


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def ini_of(path):
    kv = {}
    for l in open(path, encoding="utf-8").read().splitlines():
        if " = " in l and not l.lstrip().startswith("#"):
            k, v = l.split(" = ", 1)
            kv[k.strip()] = v.strip()
    return kv


def card(tpl_path, root, expect):
    t = pbp(tpl_path)
    st = sfo(t[0])
    game = os.path.join(root, "PSP", "GAME")
    bad, seen = 0, []
    for name in sorted(os.listdir(game)):
        d = os.path.join(game, name)
        if not os.path.isdir(d):
            continue
        if not os.path.isfile(os.path.join(d, "EBOOT.PBP")):
            if os.path.isfile(os.path.join(d, "launch.ini")) or name.endswith((".part", ".tmp")):
                print("FAIL: %s: an entry in PSP/GAME with no EBOOT.PBP (half-written)" % name)
                bad += 1
            continue
        if not os.path.isfile(os.path.join(d, "launch.ini")):
            continue                                  # GBAdhoc itself, other homebrew
        seen.append(name)
        why = []
        try:
            e = pbp(os.path.join(d, "EBOOT.PBP"))
            se = sfo(e[0])
            if e[6] != t[6]:
                why.append("DATA.PSP is not the launcher's")
            for k in st:
                if k != "TITLE" and st[k] != se.get(k):
                    why.append("SFO key %s changed" % k)
            if png_size(e[1]) not in ("144x80", "-"):
                why.append("ICON0 %s" % png_size(e[1]))
            if png_size(e[4]) not in ("-", "480x272"):
                why.append("PIC1 %s" % png_size(e[4]))
            for i in (2, 3, 5, 7):
                if e[i]:
                    why.append("section %d not empty" % i)
            kv = ini_of(os.path.join(d, "launch.ini"))
            gb = kv.get("gbadhoc", "")
            rom = kv.get("rom", "")
            if not os.path.isdir(os.path.join(root, gb)):
                why.append("gbadhoc %r not on the card" % gb)
            rp = os.path.join(root, rom.split(":/", 1)[1]) if ":/" in rom else os.path.join(root, gb, "roms", rom)
            if not os.path.isfile(rp) and name not in expect.get("romMissing", []):
                why.append("rom %r not on the card" % rom)
            x = expect.get(name)
            if x and x.get("title") is not None and se["TITLE"][1] != x["title"]:
                why.append("TITLE %r, want %r" % (se["TITLE"][1], x["title"]))
            if x and x.get("rom") is not None and rom != x["rom"]:
                why.append("rom %r, want %r" % (rom, x["rom"]))
            line = "%s  title=%r icon=%s pic1=%s rom=%r" % (name, se["TITLE"][1], png_size(e[1]), png_size(e[4]), rom)
        except Exception as ex:                       # a broken PBP is a failure, not a crash
            why.append("unreadable: %s" % ex)
            line = name
        if why:
            print("FAIL:", line, "--", "; ".join(why))
            bad += 1
        else:
            print("ok:", line)
    for f in expect.get("folders", []):
        if f not in seen:
            print("FAIL: expected entry %s is missing" % f)
            bad += 1
    for f in expect.get("absent", []):
        if f in seen:
            print("FAIL: entry %s should not be there" % f)
            bad += 1
    print("%d entries, %d bad" % (len(seen), bad))
    return bad


if sys.argv[1] == "--card":
    exp = json.load(open(sys.argv[4], encoding="utf-8")) if len(sys.argv) > 4 else {}
    sys.exit(1 if card(sys.argv[2], sys.argv[3], exp) else 0)

if sys.argv[1] == "--sfo-ref":
    ref = open(sys.argv[2], "rb").read()
    got = pbp(sys.argv[3])[0]
    if got != ref:
        fail("PARAM.SFO differs from mksfoex (%d vs %d bytes): %r vs %r" % (len(got), len(ref), sfo(got), sfo(ref)))
    print("ok: PARAM.SFO == mksfoex,", sfo(got)["TITLE"][1])
    sys.exit(0)

tpl, d, title, rom, gb, icon, pic = sys.argv[1:8]
t = pbp(tpl)
e = pbp(d + "/EBOOT.PBP")
if e[6] != t[6]:
    fail("DATA.PSP is not the launcher's")
st, se = sfo(t[0]), sfo(e[0])
if se["TITLE"][1] != title:
    fail("TITLE %r, want %r" % (se["TITLE"][1], title))
for k in st:
    if k != "TITLE" and st[k] != se[k]:
        fail("SFO key %s changed: %r -> %r" % (k, st[k], se[k]))
if png_size(e[1]) != icon:
    fail("ICON0 %s, want %s" % (png_size(e[1]), icon))
if png_size(e[4]) != pic:
    fail("PIC1 %s, want %s" % (png_size(e[4]), pic))
for i in (2, 3, 5, 7):
    if e[i]:
        fail("section %d not empty" % i)
ini = open(d + "/launch.ini", encoding="utf-8").read()
kv = dict(l.split(" = ", 1) for l in ini.splitlines() if " = " in l and not l.startswith("#"))
if kv.get("rom") != rom or kv.get("gbadhoc") != gb:
    fail("launch.ini %r" % kv)
print("ok: %s  title=%r icon=%s pic1=%s rom=%r" % (d.rsplit("/", 1)[-1], title, icon, pic, rom))
