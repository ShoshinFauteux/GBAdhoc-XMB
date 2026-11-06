#!/usr/bin/env bash
# record_demo.sh -- one README recording of GBAdhoc XMB in a private PPSSPP
# sandbox (WSL, Xvfb: nothing on your desktop).  Needs a DEMO build:
#   docker run --rm -v <repo>:/build -w /build/psp pspdev/pspdev make XMB_DEMO=1
# (a release build ignores xmbapp-demo.txt; main.c, DEMO).
#
#   bash tools/demo/record_demo.sh <card dir> <out dir> <script.txt> [keep] [offline]
#
# <card dir> is make_demo_ms.py's <out>/ms.  <script.txt> is a demo script
# (tools/demo/*.txt): timed presses and rec:NAME / stop.  Every rec:NAME
# becomes <out dir>/NAME.mp4 (lossless-ish H.264 master, 60 fps, 480x272).
# `keep` reuses the previous run's card (entries made stay).  `offline` turns
# PPSSPP's WLAN off.  Sandbox: $XMB_SANDBOX (default ~/xmbmotion/app);
# PPSSPP: $PPSSPP (default ~/xmbmotion/ppsspp/PPSSPPSDL, a private copy).
# Only the processes started here are stopped.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/../.." && pwd)"
CARD="$1"; OUT="$2"; SCRIPT="$3"; KEEP="${4:-}"; OFFLINE="${5:-}"
SB="${XMB_SANDBOX:-$HOME/xmbmotion/app}"
MS="$SB/ppsspp"
GAME="$MS/PSP/GAME/GBAdhocXMB"
PPSSPP="${PPSSPP:-$HOME/xmbmotion/ppsspp/PPSSPPSDL}"
EBOOT="${EBOOT:-$APP/EBOOT.PBP}"
TIMEOUT_S="${TIMEOUT_S:-600}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"; CARD="$(cd "$CARD" && pwd)"; SCRIPT="$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")"
if [ "$KEEP" != keep ] || [ ! -d "$MS" ]; then
  rm -rf "$SB"
  mkdir -p "$MS/PSP/SYSTEM"
  cp -r "$CARD"/. "$MS/"
fi
mkdir -p "$GAME"
rm -rf "$GAME/log" "$GAME/xmbapp-harness.ini"
cp "$EBOOT" "$GAME/EBOOT.PBP"
cp "$SCRIPT" "$GAME/xmbapp-demo.txt"
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
NickName = GBAdhoc
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
PGID=$(ps -o pgid= -p $PID 2>/dev/null | tr -d ' ')
[ -n "$PGID" ] && kill -- -"$PGID" 2>/dev/null
wait $PID 2>/dev/null
cp "$GAME/log/xmbapp.log" "$OUT/" 2>/dev/null
for r in "$GAME"/log/*.rgb; do
  [ -f "$r" ] || continue
  n="$(basename "${r%.rgb}")"
  ffmpeg -loglevel error -y -f rawvideo -pixel_format rgb0 -video_size 480x272 -framerate 60 -i "$r" \
    -c:v libx264 -preset slow -crf 4 -pix_fmt yuv444p "$OUT/$n.mp4" && echo "$n: $(( $(stat -c %s "$r") / 522240 )) frames -> $OUT/$n.mp4"
done
for b in "$GAME"/log/*.bmp; do
  [ -f "$b" ] || continue
  python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$b" "$OUT/$(basename "${b%.bmp}").png" 2>/dev/null
done
ls -la "$MS/PSP/GAME" > "$OUT/psp_game.txt" 2>&1
tail -4 "$OUT/xmbapp.log" 2>/dev/null || { echo "no app log"; tail -20 "$OUT/emu.log"; }
