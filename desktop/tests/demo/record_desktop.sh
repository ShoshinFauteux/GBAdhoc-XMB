#!/usr/bin/env bash
# record_desktop.sh -- the README recordings of GBAdhoc XMB for Windows (Git Bash).
#   bash desktop/tests/demo/record_desktop.sh <card dir> <out dir> [entry|queue|online ...]
# <card dir> is a test card (psp/tools/demo/make_demo_ms.py --scrape --all-box: <out>/ms, box art
# for every game).  Each clip runs the real exe on a fresh copy of it (--fake-psp: no
# drive letter is touched), with WebView2's DevTools port open; rec.mjs plays the clip's
# actions (desktop-<clip>.js: the UI's own clicks, a drawn pointer, no desktop input),
# records every painted frame and cfr.py makes <out>/desktop-<clip>.mp4.  `queue` makes
# the third write fail as an unplug (XMB_FAULT, the test hook test_app.sh uses).
# `online` hosts on loopback like tests/online: fake_igd.py as the router, test_online's
# directory, two relay_client consoles (build them with tests/online/test_online.sh;
# ONLINE_BIN = their folder).  The window opens without focus; only its own PIDs are stopped.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CARD="$1"; OUT="$2"; shift 2
CLIPS="${*:-entry queue online}"
EXE="$REPO/desktop/dist/GBAdhoc-XMB.exe"
win() { cygpath -m "$1"; }
export MSYS_NO_PATHCONV=1
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
PY=python; command -v python >/dev/null 2>&1 || PY=python3
for c in $CLIPS; do
  W="$OUT/work-$c"; rm -rf "$W"; mkdir -p "$W/appdata"
  cp -r "$CARD" "$W/card"
  [ -d "${DATS:-}" ] && { mkdir -p "$W/appdata/cache/dat"; cp "$DATS"/*.dat "$W/appdata/cache/dat/"; }
  FAULT=""; [ "$c" = queue ] && FAULT="eboot@3"
  SIZE=1280x797; H=797; PORT=9341; EXTRA=()
  if [ "$c" = online ]; then
    SIZE=1280x900; H=900; T="$REPO/desktop/tests/online"; BIN="${ONLINE_BIN:?set ONLINE_BIN}"
    $PY -I "$(win "$T/fake_igd.py")" --mode ok --ports "$(win "$W/igd.ports")" & IGD=$!
    for i in $(seq 1 100); do [ -s "$W/igd.ports" ] && break; sleep 0.1; done
    read -r SSDP HTTPP < "$W/igd.ports"; HTTPP="$(printf %s "$HTTPP" | tr -d '\r')"
    "$BIN/test_online.exe" directory 37490 90 > "$W/dir.txt" 2>&1 & DIR=$!
    ( sleep 20
      "$BIN/relay_client.exe" --server 127.0.0.1:37440 --host --bind 127.0.0.1:37460 --payloads 3000 --secs 40 > "$W/c0.txt" 2>&1 &
      sleep 1.2
      "$BIN/relay_client.exe" --server 127.0.0.1:37440 --join --bind 127.0.0.1:37461 --payloads 3000 --secs 40 > "$W/c1.txt" 2>&1
      wait ) & CLP=$!
    export XMB_ONLINE_BIND=127.0.0.1 XMB_UPNP_SSDP="127.0.0.1:$SSDP" XMB_SHARED_RELAY=127.0.0.1:37490
  fi
  WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--remote-debugging-port=$PORT --remote-allow-origins=*" \
  XMB_APPDATA="$(win "$W/appdata")" XMB_FAULT="$FAULT" \
    "$EXE" --fake-psp "$(win "$W/card")" --ui-dir "$(win "$REPO/desktop/ui")" --open "?theme=mono" --size "$SIZE" &
  APP=$!
  REC_W=1280 REC_H=$H REC_DSF=1 node "$(win "$HERE/rec.mjs")" $PORT rec "$(win "$W/frames")" "$(win "$HERE/desktop-$c.js")" 600
  kill $APP 2>/dev/null
  if [ "$c" = online ]; then
    wait "$CLP" 2>/dev/null
    curl -s -m 3 "http://127.0.0.1:$HTTPP/quit" >/dev/null 2>&1; sleep 0.5; kill "$IGD" "$DIR" 2>/dev/null
    unset XMB_ONLINE_BIND XMB_UPNP_SSDP XMB_SHARED_RELAY
  fi
  $PY "$(win "$HERE/cfr.py")" "$(win "$W/frames")" "$(win "$OUT/desktop-$c.mp4")"
done
