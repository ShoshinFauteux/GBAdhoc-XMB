#!/usr/bin/env python3
"""result.py RESULT.json [KEY=VALUE ...] -- print an e2e.js result; exit 1 on a failed check.
KEY=VALUE pairs are extra checks on top-level fields (code=disconnected)."""
import json
import sys

r = json.load(open(sys.argv[1], encoding="utf-8"))
bad = 0
for c in r.get("checks", []):
    print("  %s %s%s" % ("ok  " if c["ok"] else "FAIL", c["name"], "" if c["ok"] else "  (got %r)" % (c.get("got"),)))
    bad += not c["ok"]
for kv in sys.argv[2:]:
    k, v = kv.split("=", 1)
    got = r.get(k)
    if k == "recovered":
        got = ",".join("%s:%s" % (x["did"], x["folder"]) for x in r.get("recovered") or [])
    ok = str(got) == v
    print("  %s %s == %s%s" % ("ok  " if ok else "FAIL", k, v, "" if ok else "  (got %r)" % (got,)))
    bad += not ok
sys.exit(1 if bad else 0)
