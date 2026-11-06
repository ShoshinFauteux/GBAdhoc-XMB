#!/usr/bin/env bash
# record_all.sh -- every README recording of the PSP app, from scratch (run in WSL).
#   bash tools/demo/record_all.sh <ROM folder> <GBAdhoc release's PSP/GAME/GBAdhoc> <GBAdhoc art dir> <out dir> [clip ...]
# 1. builds the DEMO app and the DEMO launcher (docker, make XMB_DEMO=1; release builds
#    are unaffected: the flag only adds code);
# 2. makes the demo card (make_demo_ms.py --scrape: your ROMs, copied to the card only);
# 3. runs each clip script (tools/demo/<clip>.txt) on a fresh copy of the card in PPSSPP;
#    after `xmb`, record_launch.sh records the new entry starting GBAdhoc.
# Clips: library game layouts make-all xmb themes.  The README's cuts and encodes are
# made from <out>/<clip>/*.mp4 (60 fps, 480x272; the launch is PPSSPP's 960x544 dump).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/../.." && pwd)"
ROMS="$1"; GBA="$2"; ART="$3"; OUT="$4"; shift 4
CLIPS="${*:-library game layouts make-all xmb themes}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
if [ -z "${EBOOT:-}" ]; then
  docker run --rm -v "$APP/..":/build pspdev/pspdev sh -c '
    cd /build/psp && make clean >/dev/null && make XMB_DEMO=1 >/dev/null && cp EBOOT.PBP /build/psp/EBOOT-demo.PBP && make clean >/dev/null
    cd /build/launcher && make clean >/dev/null && make XMB_DEMO=1 >/dev/null && cp EBOOT.PBP /build/launcher/EBOOT-demo.PBP && make clean >/dev/null && make >/dev/null'
  EBOOT="$APP/EBOOT-demo.PBP"
  DEMO_LAUNCHER="$APP/../launcher/EBOOT-demo.PBP"
fi
python3 "$HERE/make_demo_ms.py" "$OUT/card" --roms "$ROMS" --gbadhoc "$GBA" --art "$ART" --scrape
for c in $CLIPS; do
  echo "== $c"
  EBOOT="$EBOOT" bash "$HERE/record_demo.sh" "$OUT/card/ms" "$OUT/$c" "$HERE/$c.txt"
  if [ "$c" = xmb ] && [ -n "${DEMO_LAUNCHER:-}" ]; then
    DEMO_LAUNCHER="$DEMO_LAUNCHER" bash "$HERE/record_launch.sh" Golden_Sun "$OUT/launch/launch.mp4" 16
  fi
done
