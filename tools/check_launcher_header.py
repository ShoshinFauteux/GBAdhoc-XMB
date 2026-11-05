#!/usr/bin/env python3
"""check_launcher_header.py LAUNCHER.PBP HEADER.h -- exit 0 if the header embeds exactly
this launcher (psp/gen/launcher_pbp.h is committed; build.sh checks it is not stale)."""
import re
import sys

want = open(sys.argv[1], "rb").read()
src = open(sys.argv[2]).read()
have = bytes(int(x) for x in re.findall(r"\d+", src.split("{", 1)[1].split("}", 1)[0]))
sys.exit(0 if have == want else 1)
