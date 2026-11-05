#!/usr/bin/env bash
# test_app.sh [OUT_DIR] -- GBAdhoc XMB (desktop/dist/GBAdhoc-XMB.exe) end to end, on Windows
# (Git Bash).  Every case runs the real app -- host, WebView2, UI -- on a fake PSP
# made by make_fakepsp.py (placeholder ROMs, no games), offline (XMB_NET=offline) so
# nothing depends on GitHub, with its own app-data folder.  The app is never pointed
# at a real drive: --fake-psp makes it ignore every drive letter.
#   main       scan + recognition, make all (one write "unplugged" -> the queue
#              stops there and resumes there), fix a moved ROM's entry, remove one;
#              every entry then checked by check_entry.py --card
#   eboot      unplugged after EBOOT.PBP: nothing reaches PSP/GAME; next scan clears it
#   move       unplugged after the write, before the move: next scan finishes it
#   full       0 MB free: refused, banner, buttons off
#   exists     another program owns the folder name: never replaced
#   nopsp      no PSP: the waiting screen
#   go         a PSP Go's internal storage (ef0:)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
EXE="$HERE/dist/GBAdhoc-XMB.exe"
TPL="$HERE/../launcher/EBOOT.PBP"
OUT="${1:-$(mktemp -d)}"
mkdir -p "$OUT"
win() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
PY=python; command -v python >/dev/null 2>&1 || PY=python3
[ -x "$EXE" ] || { echo "build first: desktop/build.sh"; exit 2; }
[ -f "$TPL" ] || { echo "build the launcher first"; exit 2; }
rc=0

fresh() {   # fresh NAME [--go] -> $CARD
  CARD="$OUT/$1"
  rm -rf "$CARD"
  $PY "$HERE/tests/make_fakepsp.py" "$CARD" ${2:-} >/dev/null
  "$HERE/dist/xmbwrap.exe" --rom-file "$CARD/PSP/GAME/GBAdhoc/roms/Pokemon - Emerald Version (USA, Europe).gba" \
     --title "Pokemon Emerald" --folder XMB_Pokemon_Emerald >/dev/null
  "$HERE/dist/xmbwrap.exe" --root "$CARD" --rom "Mario Kart - Super Circuit (USA).gba" \
     --title "Mario Kart Super Circuit" --folder XMB_Mario_Kart_Super_Circuit --no-art >/dev/null
}

app() {     # app CASE TEST [fake spec] -- one run of the real app; result in $OUT/CASE.json
  local spec="${3:-$CARD}"
  rm -f "$OUT/$1.json"
  mkdir -p "$OUT/appdata-$1/cache/dat" && cp "$OUT"/dats/*.dat "$OUT/appdata-$1/cache/dat/"
  XMB_NET=offline XMB_APPDATA="$(win "$OUT/appdata-$1")" timeout 120 "$EXE" --fake-psp "$(win "$spec")" \
     --ui-dir "$(win "$HERE/ui")" --script "$(win "$HERE/tests/e2e.js")" --open "?test=$2" \
     --result "$(win "$OUT/$1.json")"
  [ -f "$OUT/$1.json" ] || { echo "FAIL: $1: the app gave no result"; rc=1; return 1; }
}

result() { $PY "$HERE/tests/result.py" "$@" || rc=1; }
cardcheck() { $PY "$HERE/../tools/check_entry.py" --card "$TPL" "$CARD" "$@" || rc=1; }

[ -x "$HERE/dist/xmbwrap.exe" ] || { echo "build dist/xmbwrap.exe first"; exit 2; }

# The No-Intro lists, once (the app runs offline, as it does with lists it
# cached on an earlier day).  XMB_TEST_DATS=DIR uses DATs already on disk.
mkdir -p "$OUT/dats"
for s in "Game Boy Advance:GBA" "Game Boy Color:GBC" "Game Boy:GB"; do
  n="${s%%:*}"; k="${s##*:}"
  [ -s "$OUT/dats/$k.dat" ] && continue
  if [ -n "${XMB_TEST_DATS:-}" ] && [ -s "$XMB_TEST_DATS/$k.dat" ]; then cp "$XMB_TEST_DATS/$k.dat" "$OUT/dats/"; continue; fi
  curl -sf -m 120 -o "$OUT/dats/$k.dat" \
    "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/Nintendo%20-%20${n// /%20}.dat" ||
    { echo "need the No-Intro DATs: online once, or XMB_TEST_DATS=DIR"; exit 2; }
done

echo "== main: scan, make all (unplug at the 3rd, resume), fix a moved ROM, remove one"
fresh main
XMB_FAULT=eboot@3 app main main && result "$OUT/main.json"
cat > "$OUT/main-expect.json" <<'EOF'
{"folders": ["XMB_Pokemon_Emerald", "XMB_Mario_Kart_Super_Circuit", "XMB_Kirby_The_Amazing_Mirror",
             "XMB_Golden_Sun", "XMB_Pokemon_MonoMyth", "XMB_Fire_Emblem_The_Morrow_s", "XMB_Pokemon_Crystal"],
 "absent": ["XMB_Tetris"],
 "XMB_Mario_Kart_Super_Circuit": {"title": "Mario Kart Super Circuit", "rom": "racing/Mario Kart - Super Circuit (USA).gba"},
 "XMB_Pokemon_Crystal": {"title": "Pokemon Crystal", "rom": "GBC/Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc"},
 "XMB_Kirby_The_Amazing_Mirror": {"rom": "GBA/Kirby & The Amazing Mirror (USA).gba"},
 "XMB_Pokemon_MonoMyth": {"title": "Pokemon MonoMyth", "rom": "hacks/Pokemon MonoMyth v1.2.gba"}}
EOF
cardcheck "$OUT/main-expect.json"
ls "$CARD/PSP/GAME/GBAdhoc/xmb/stage" 2>/dev/null | grep -q . && { echo "FAIL: stage not empty"; rc=1; }

echo "== eboot: unplugged after EBOOT.PBP"
fresh eboot
XMB_FAULT=eboot app eboot1 makeone && result "$OUT/eboot1.json" code=disconnected
[ -d "$CARD/PSP/GAME/XMB_Kirby_The_Amazing_Mirror" ] && { echo "FAIL: half an entry reached PSP/GAME"; rc=1; }
app eboot2 scanonly && result "$OUT/eboot2.json" recovered=discarded:XMB_Kirby_The_Amazing_Mirror
echo '{"romMissing": ["XMB_Mario_Kart_Super_Circuit"]}' > "$OUT/eboot-expect.json"; cardcheck "$OUT/eboot-expect.json"

echo "== move: unplugged after writing, before the move"
fresh move
XMB_FAULT=move app move1 makeone && result "$OUT/move1.json" code=disconnected
app move2 scanonly && result "$OUT/move2.json" recovered=finished:XMB_Kirby_The_Amazing_Mirror
echo '{"folders": ["XMB_Kirby_The_Amazing_Mirror"], "romMissing": ["XMB_Mario_Kart_Super_Circuit"]}' > "$OUT/move-expect.json"
cardcheck "$OUT/move-expect.json"

echo "== unplug: the cable pulled during a queue, then put back"
fresh unplug
( XMB_SLOW=1200 app unplug unplug ) &
pid=$!
for i in $(seq 1 300); do   # first new entry written -> pull it
  [ "$(ls -d "$CARD"/PSP/GAME/XMB_* 2>/dev/null | wc -l)" -ge 3 ] && break
  sleep 0.1
done
mv "$CARD/PSP" "$CARD/PSP.unplugged"; sleep 6; mv "$CARD/PSP.unplugged" "$CARD/PSP"
wait $pid
[ -f "$OUT/unplug.json" ] && result "$OUT/unplug.json"
echo '{"romMissing": ["XMB_Mario_Kart_Super_Circuit"], "folders": ["XMB_Kirby_The_Amazing_Mirror", "XMB_Golden_Sun", "XMB_Pokemon_Crystal", "XMB_Tetris"]}' > "$OUT/unplug-expect.json"
cardcheck "$OUT/unplug-expect.json"

echo "== full: no space"
fresh full
XMB_FAKE_FREE_MB=0 app full full && result "$OUT/full.json"

echo "== exists: another program's folder"
fresh exists
mkdir -p "$CARD/PSP/GAME/XMB_Golden_Sun" && cp "$CARD/PSP/GAME/SOMEAPP/EBOOT.PBP" "$CARD/PSP/GAME/XMB_Golden_Sun/"
app exists exists && result "$OUT/exists.json"
cmp -s "$CARD/PSP/GAME/SOMEAPP/EBOOT.PBP" "$CARD/PSP/GAME/XMB_Golden_Sun/EBOOT.PBP" || { echo "FAIL: the other program was touched"; rc=1; }

echo "== nopsp: nothing connected"
app nopsp nopsp "$OUT/no-such-psp" && result "$OUT/nopsp.json"

echo "== go: a PSP Go's internal storage"
fresh go --go
app go makeone "$CARD,ef0,go" && result "$OUT/go.json" code=ok
echo '{"folders": ["XMB_Kirby_The_Amazing_Mirror"], "romMissing": ["XMB_Mario_Kart_Super_Circuit"]}' > "$OUT/go-expect.json"
cardcheck "$OUT/go-expect.json"

[ $rc = 0 ] && echo "ALL PASS" || echo "FAILED"
exit $rc
