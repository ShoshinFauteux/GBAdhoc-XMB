#!/usr/bin/env python3
"""swap_launcher.py <entry EBOOT.PBP> <demo launcher EBOOT.PBP> -- for the README's
launch recording only: the entry keeps its PARAM.SFO and art, and its DATA.PSP (the
launcher) becomes the DEMO launcher's (launcher/Makefile XMB_DEMO=1), which starts
GBAdhoc with sceKernelLoadExec, a call PPSSPP emulates (systemctrl it does not)."""
import struct, sys


def split(b):
    assert b[:4] == b'\0PBP', 'not a PBP'
    off = list(struct.unpack('<8I', b[8:40])) + [len(b)]
    return [b[off[i]:off[i + 1]] for i in range(8)]


entry, demo = (open(p, 'rb').read() for p in sys.argv[1:3])
s, d = split(entry), split(demo)
s[6] = d[6]
out = bytearray(b'\0PBP' + entry[4:8])
pos = 40
offs = []
for sec in s:
    offs.append(pos)
    pos += len(sec)
out += struct.pack('<8I', *offs)
for sec in s:
    out += sec
open(sys.argv[1], 'wb').write(out)
print('launcher swapped:', sys.argv[1], len(out))
