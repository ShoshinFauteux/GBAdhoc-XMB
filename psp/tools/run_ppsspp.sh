#!/usr/bin/env bash
# run_ppsspp.sh -- GBAdhoc XMB in a private PPSSPP sandbox (WSL), driven by a
# test script (main.c: xmbapp-harness.ini); collects log/xmbapp.log and
# every shot as PNG.  PPSSPP=path to a PPSSPPSDL build (default
# $HOME/ppsspp/build/PPSSPPSDL); needs xvfb-run and Pillow.
#
#   bash tools/run_ppsspp.sh <fixture ms dir> <out dir> "<script>" [keep] [offline]
#
# <fixture ms dir> comes from tools/make_fixture.py (the card's root).  With
# `keep`, the sandbox card of the previous run is reused (entries made stay),
# so a later run sees "In XMB" and "ROM moved" for real.  `offline` turns
# PPSSPP's WLAN off.  The sandbox is $HOME/xmbapp-e2e/app.
# Only the processes started here are stopped.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/.." && pwd)"
FIX="$1"; OUT="$2"; SCRIPT="$3"; KEEP="${4:-}"; OFFLINE="${5:-}"
SB="$HOME/xmbapp-e2e/app"
MS="$SB/ppsspp"
GAME="$MS/PSP/GAME/GBAdhocXMB"
PPSSPP="${PPSSPP:-$HOME/ppsspp/build/PPSSPPSDL}"
TIMEOUT_S="${TIMEOUT_S:-240}"
mkdir -p "$OUT"
if [ "$KEEP" != keep ] || [ ! -d "$MS" ]; then
  rm -rf "$SB"
  mkdir -p "$MS/PSP/SYSTEM"
  cp -r "$FIX"/. "$MS/"
fi
mkdir -p "$GAME"
rm -rf "$GAME/log"
cp "$APP/EBOOT.PBP" "$GAME/EBOOT.PBP"
printf 'script = %s\n' "$SCRIPT" > "$GAME/xmbapp-harness.ini"
WLAN=True; [ "$OFFLINE" = offline ] && WLAN=False
cat > "$MS/PSP/SYSTEM/ppsspp.ini" <<EOF
[General]
FirstRun = False
CheckForNewVersion = False
[Graphics]
GraphicsBackend = 3 (VULKAN)
SoftwareRenderer = True
FrameSkip = 0
[Network]
EnableWlan = $WLAN
EnableAdhocServer = False
EnableUPnP = False
[SystemParam]
NickName = XMBAPP
EOF
( cd "$(dirname "$PPSSPP")" && XDG_CONFIG_HOME="$SB" SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 \
  exec setsid timeout "$TIMEOUT_S" xvfb-run -a -s "-screen 0 1280x720x24 +extension GLX +render -noreset" \
  "$PPSSPP" --windowed "$GAME/EBOOT.PBP" > "$OUT/emu.log" 2>&1 ) &
PID=$!
for i in $(seq 1 "$TIMEOUT_S"); do
  grep -q " exit$" "$GAME/log/xmbapp.log" 2>/dev/null && break
  kill -0 $PID 2>/dev/null || break
  sleep 1
done
sleep 1
# stop only what this script started: its own session (setsid above)
PGID=$(ps -o pgid= -p $PID 2>/dev/null | tr -d ' ')
[ -n "$PGID" ] && kill -- -"$PGID" 2>/dev/null
wait $PID 2>/dev/null
cp "$GAME/log/xmbapp.log" "$OUT/" 2>/dev/null
for b in "$GAME"/log/*.bmp; do
  [ -f "$b" ] || continue
  python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$b" "$OUT/$(basename "${b%.bmp}").png" 2>/dev/null || cp "$b" "$OUT/"
done
# the card's PSP/GAME after the run (entries made)
ls -la "$MS/PSP/GAME" > "$OUT/psp_game.txt" 2>&1
tail -5 "$OUT/xmbapp.log" 2>/dev/null || { echo "no app log"; tail -20 "$OUT/emu.log"; }
