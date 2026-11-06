#!/usr/bin/env python3
"""make_fakepsp.py DIR [--go] -- a fake PSP for testing GBAdhoc XMB (no real games).

DIR/PSP/GAME/GBAdhoc   a GBAdhoc install (a stand-in EBOOT.PBP titled "GBAdhoc")
  roms/...             placeholder ROMs: correct headers and No-Intro names,
                       64 KB of filler, so nothing in here is a game
  boxart/, hero/       art GBAdhoc "already has" for some, drawn here
DIR/PSP/GAME/SOMEAPP   another homebrew, which the app must leave alone
DIR/PICTURE            two pictures
DIR/PSP/GAME/GBAdhocXMB/custom   Golden Sun's own icon and background (custom folder)
The design's sample library: Emerald, Kirby, Mario Kart (in roms/racing),
Golden Sun, two ROM hacks (roms/hacks), Crystal (GBC), Tetris (GB) and one
ROM no list knows.  Existing entries are made by the test with xmbwrap."""
import os
import struct
import sys
import zlib

NLOGO = bytes([0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
               0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
               0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E])


def gba(title, code):
    d = bytearray(65536)
    d[0:4] = b"\x2e\x00\x00\xea"            # b 0xc0, as every cartridge starts
    d[0xA0:0xAC] = title.encode().ljust(12, b"\0")[:12]
    d[0xAC:0xB0] = code.encode()
    d[0xB0:0xB2] = b"01"
    d[0xB2] = 0x96
    d[0xBD] = (-(sum(d[0xA0:0xBD]) + 0x19)) & 0xff
    for i in range(0xC0, len(d)):          # filler, not code
        d[i] = (i * 37) & 0xff
    return bytes(d)


def gb(title, cgb):
    d = bytearray(65536)
    d[0x100:0x104] = b"\x00\xc3\x50\x01"
    d[0x104:0x134] = NLOGO
    t = title.encode()[:15 if cgb else 16]
    d[0x134:0x134 + len(t)] = t
    if cgb:
        d[0x143] = 0xC0
    x = 0
    for i in range(0x134, 0x14D):
        x = (x - d[i] - 1) & 0xff
    d[0x14D] = x
    return bytes(d)


def png(path, w, h, top, bottom, label_band=None):
    rows = []
    for y in range(h):
        f = y / max(1, h - 1)
        c = bytes(int(top[i] * (1 - f) + bottom[i] * f) for i in range(3))
        row = bytearray(c * w)
        if label_band and label_band[0] <= y < label_band[1]:
            for x in range(w // 6, w - w // 6):
                row[x * 3:x * 3 + 3] = b"\xf2\xf1\xec"
        rows.append(b"\0" + bytes(row))
    raw = b"".join(rows)

    def ch(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


def sfo(title):
    keys = [("APP_VER", 0x0204, b"01.00\0", 8), ("BOOTABLE", 0x0404, struct.pack("<I", 1), 4),
            ("CATEGORY", 0x0204, b"MG\0", 4), ("DISC_ID", 0x0204, b"UCJS10041\0", 16),
            ("DISC_VERSION", 0x0204, b"1.00\0", 8), ("PARENTAL_LEVEL", 0x0404, struct.pack("<I", 1), 4),
            ("PSP_SYSTEM_VER", 0x0204, b"1.00\0", 8), ("REGION", 0x0404, struct.pack("<I", 0x8000), 4),
            ("TITLE", 0x0204, title.encode() + b"\0", 128)]
    kt = 20 + 16 * len(keys)
    names = b"".join(k[0].encode() + b"\0" for k in keys)
    names += b"\0" * ((4 - len(names) % 4) % 4)
    dt = kt + len(names)
    idx, data, ko, do = b"", b"", 0, 0
    for k, fmt, v, mx in keys:
        idx += struct.pack("<HHIII", ko, fmt, len(v), mx, do)
        data += v.ljust(mx, b"\0")
        ko += len(k) + 1
        do += mx
    return b"\0PSF" + struct.pack("<IIII", 0x0101, kt, dt, len(keys)) + idx + names + data


def pbp(title):
    s = sfo(title)
    body = b"\x7fELF" + b"\0" * 60              # a stand-in DATA.PSP
    offs = [0x28 + len(s)] * 6
    hdr = b"\0PBP" + struct.pack("<I", 0x10000) + struct.pack("<I", 0x28) + b"".join(struct.pack("<I", o) for o in offs) + \
        struct.pack("<I", 0x28 + len(s) + len(body))
    return hdr + s + body


def main():
    root = sys.argv[1]
    g = os.path.join(root, "PSP", "GAME", "GBAdhoc")
    roms = os.path.join(g, "roms")
    for d in ("", "racing", "hacks", "GBA", "GB", "GBC"):   # GBAdhoc 4.6: roms/GBA, roms/GB, roms/GBC
        os.makedirs(os.path.join(roms, d), exist_ok=True)
    open(os.path.join(g, "EBOOT.PBP"), "wb").write(pbp("GBAdhoc"))
    os.makedirs(os.path.join(root, "PSP", "GAME", "SOMEAPP"), exist_ok=True)
    open(os.path.join(root, "PSP", "GAME", "SOMEAPP", "EBOOT.PBP"), "wb").write(pbp("Some other homebrew"))
    files = {
        "Pokemon - Emerald Version (USA, Europe).gba": gba("POKEMON EMER", "BPEE"),
        "GBA/Kirby & The Amazing Mirror (USA).gba": gba("KIRBY AM", "B8KE"),
        "racing/Mario Kart - Super Circuit (USA).gba": gba("MARIOKART", "AMKE"),
        "Golden Sun (USA, Europe).gba": gba("Golden_Sun_A", "AGSE"),
        "hacks/Pokemon MonoMyth v1.2.gba": gba("POKEMON EMER", "BPEE"),
        "hacks/Fire Emblem - The Morrow's Golden Country.gba": gba("FIREEMBLEM2E", "BE8E"),
        "GBC/Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc": gb("PM_CRYSTAL", True),
        "GB/Tetris (World) (Rev 1).gb": gb("TETRIS", False),
        "mygame_final2 (1).gba": gba("MYGAME", "ZQZQ"),
    }
    for rel, data in files.items():
        open(os.path.join(roms, rel), "wb").write(data)
    open(os.path.join(roms, "Pokemon - Emerald Version (USA, Europe).sav"), "wb").write(b"\xff" * 131072)
    # GBAdhoc 4.6: saves/ and states/ (never scanned as ROMs, even a .gba in there)
    for d, f, n in (("saves", "Kirby & The Amazing Mirror (USA).sav", 131072), ("states", "Kirby & The Amazing Mirror (USA).st0", 4096),
                    ("states", "Not a game (USA).gba", 512), ("saves", "Tetris (World) (Rev 1).gb.sav", 8192)):
        os.makedirs(os.path.join(g, d), exist_ok=True)
        open(os.path.join(g, d, f), "wb").write(gba("DECOY", "BPEE") if f.endswith(".gba") else b"0" * n)
    os.makedirs(os.path.join(roms, "saves"), exist_ok=True)
    open(os.path.join(roms, "saves", "Decoy (USA).gba"), "wb").write(gba("DECOY", "BPEE"))
    # art GBAdhoc already has
    png(os.path.join(g, "boxart", "Pokemon - Emerald Version (USA, Europe).png"), 256, 256, (31, 143, 95), (11, 61, 44), (190, 220))
    png(os.path.join(g, "hero", "Pokemon - Emerald Version (USA, Europe).png"), 480, 272, (11, 61, 44), (31, 143, 95))
    png(os.path.join(g, "boxart", "Kirby & The Amazing Mirror (USA).png"), 256, 256, (255, 122, 184), (91, 29, 87), (190, 220))
    png(os.path.join(g, "boxart", "Mario Kart - Super Circuit (USA).png"), 256, 256, (240, 90, 40), (58, 16, 48), (190, 220))
    png(os.path.join(g, "hero", "Mario Kart - Super Circuit (USA).png"), 480, 272, (58, 16, 48), (240, 90, 40))
    png(os.path.join(g, "boxart", "Pokemon - Crystal Version (USA, Europe) (Rev 1).png"), 224, 224, (57, 198, 232), (21, 58, 110), (170, 196))
    # the owner's own pictures in the XMB app's custom folder: Golden Sun's
    # icon (box-art shaped) and background (not 480x272, so it is scaled)
    cust = os.path.join(root, "PSP", "GAME", "GBAdhocXMB", "custom")
    png(os.path.join(cust, "Golden Sun (USA, Europe).png"), 200, 240, (250, 200, 60), (120, 40, 10), (150, 180))
    png(os.path.join(cust, "Golden Sun (USA, Europe).bg.png"), 640, 360, (120, 40, 10), (250, 200, 60))
    png(os.path.join(root, "PICTURE", "sunset.png"), 480, 272, (240, 140, 60), (60, 20, 70))
    png(os.path.join(root, "PICTURE", "wallpaper-forest.png"), 480, 272, (60, 140, 80), (10, 40, 20))
    if "--go" in sys.argv:
        open(os.path.join(root, ".fake-go"), "w").write("a PSP Go's internal storage\n")
    print("fake PSP in", root)


main()
