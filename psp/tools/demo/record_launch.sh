#!/usr/bin/env bash
# record_launch.sh -- an entry starting GBAdhoc, recorded from PPSSPP's own (virtual)
# display: run after record_demo.sh made the entry (its sandbox card is reused).
#   bash tools/demo/record_launch.sh <entry folder, e.g. Golden_Sun> <out.mp4> [seconds]
# PPSSPP runs the entry's EBOOT.PBP (the launcher), which starts GBAdhoc with the ROM;
# The footage is PPSSPP's own frame dump (DumpFrames, lossless FFV1), not a screen grab.
set -uo pipefail
ENTRY="$1"; OUT="$2"; SECS="${3:-14}"
SB="${XMB_SANDBOX:-$HOME/xmbmotion/app}"
MS="$SB/ppsspp"
PPSSPP="${PPSSPP:-$HOME/xmbmotion/ppsspp/PPSSPPSDL}"
[ -f "$MS/PSP/GAME/$ENTRY/EBOOT.PBP" ] || { echo "no entry $ENTRY in $MS"; exit 2; }
HERE="$(cd "$(dirname "$0")" && pwd)"
# the DEMO launcher in the entry (swap_launcher.py): $DEMO_LAUNCHER, a launcher/ build with XMB_DEMO=1
[ -n "${DEMO_LAUNCHER:-}" ] && python3 "$HERE/swap_launcher.py" "$MS/PSP/GAME/$ENTRY/EBOOT.PBP" "$DEMO_LAUNCHER"
mkdir -p "$(dirname "$OUT")"; OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
cat > "$MS/PSP/SYSTEM/ppsspp.ini" <<EOF
[General]
FirstRun = False
DumpFrames = True
UseFFV1 = True
DumpVideoOutput = False
CheckForNewVersion = False
[Graphics]
GraphicsBackend = 3 (VULKAN)
SoftwareRenderer = True
FrameSkip = 0
[Network]
EnableWlan = False
[SystemParam]
NickName = GBAdhoc
EOF
rm -rf "$MS/PSP/VIDEO"
( cd "$(dirname "$PPSSPP")" && XDG_CONFIG_HOME="$SB" SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1   exec setsid timeout "$SECS" xvfb-run -a -s "-screen 0 1280x720x24 +extension GLX +render -noreset"   "$PPSSPP" --windowed "$MS/PSP/GAME/$ENTRY/EBOOT.PBP" > "$(dirname "$OUT")/launch-emu.log" 2>&1 ) &
PID=$!
wait $PID 2>/dev/null
AVI="$(ls -t "$MS"/PSP/VIDEO/*.avi 2>/dev/null | head -1)"
[ -n "$AVI" ] || { echo "no frame dump in $MS/PSP/VIDEO"; exit 1; }
ffmpeg -loglevel error -y -i "$AVI" -c:v libx264 -preset slow -crf 4 -pix_fmt yuv444p "$OUT"
ls -la "$OUT"
