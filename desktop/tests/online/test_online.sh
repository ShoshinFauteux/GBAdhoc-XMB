#!/usr/bin/env bash
# desktop/tests/online/test_online.sh [OUT_DIR] -- every test of "Host online room", on
# Windows (Git Bash), loopback only: nothing listens on a public interface and nothing
# reaches the internet (XMB_SHARED_RELAY is a loopback port or "off", the router is
# fake_igd.py on 127.0.0.1).
#   1. the relay core's own tests (NAT model, fuzz) and relay_obs's, ASan/UBSan host build
#   2. the same three built with mingw and run on Windows
#   3. the Online tab's states (node, ui/online.js on fixtures)
#   4. the UPnP client against fake_igd.py in every mode
#   5. the host thread: start/stop x50 with no leaks; x5 with UPnP mapping and unmapping
#   6. loopback: the desktop relay and two simulated consoles (relay_client.exe, GBAdhoc's
#      test client): create, join, relay, then the direct path
#   7. the real app (GBAdhoc-XMB.exe, WebView2): host, a room with two players, network
#      check, stop; and the CGNAT and no-UPnP messages
# Needs docker (gcc:13, gbadhoc-xmb-mingw), python, node, and desktop/dist/GBAdhoc-XMB.exe.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
DESK="$(cd "$HERE/../.." && pwd)"
TREE="$(cd "$DESK/.." && pwd)"
OUT="${1:-$(mktemp -d)}"
mkdir -p "$OUT"
export MSYS_NO_PATHCONV=1
win() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
PY=python; command -v python >/dev/null 2>&1 || PY=python3
MNT="$(win "$TREE")"
BIN="$OUT/bin"; mkdir -p "$BIN"
rc=0
step() { echo; echo "== $*"; }
run() { "$@" || { echo "FAILED: $*"; rc=1; }; }
runt() { local o s; o=$("$@" 2>&1); s=$?; echo "$o" | tail -1; [ $s = 0 ] || { echo "$o" | grep FAIL; echo "FAILED: $*"; rc=1; }; }

step "1. relay core + relay_obs, host build (ASan, UBSan)"
run bash "$TREE/shared/relay/tests/run_host_tests.sh" 1000000

step "2. mingw builds"
docker run --rm --network none -v "$MNT":/g:ro -v "$(win "$BIN")":/out -w /g gbadhoc-xmb-mingw sh -c '
  set -e; O=/tmp/o; mkdir -p $O; CC=x86_64-w64-mingw32-gcc
  R=shared/relay; T=desktop/tests/online
  W="-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -static"
  $CC $W -I$R -I$R/client -o $O/test_relay_nat.exe $R/tests/test_relay_nat.c $R/relay_core.c $R/client/transport_relay.c $R/client/netdrv.c
  $CC $W -I$R -I$R/client -o $O/fuzz_relay.exe $R/tests/fuzz_relay.c $R/relay_core.c $R/client/transport_relay.c
  $CC $W -I$R -o $O/test_obs.exe $R/tests/test_obs.c $R/relay_obs.c
  $CC $W -include $T/count_alloc.h -o $O/test_online.exe $T/test_online.c desktop/online.c desktop/upnp.c \
      desktop/json.c $R/relay_core.c $R/relay_obs.c -lws2_32 -lbcrypt
  $CC $W -I$T/wincompat -include $T/wincompat/wincompat.h -I$R -I$R/client -o $O/relay_client.exe \
      $R/tests/relay_client.c $R/client/transport_relay.c $R/client/netdrv.c -lws2_32 -lwinmm -lpthread
  cp $O/*.exe /out/' \
  || { echo "FAILED: mingw build"; exit 1; }
for t in test_relay_nat test_obs; do echo "-- $t.exe"; runt "$BIN/$t.exe"; done
echo "-- fuzz_relay.exe 1000000"; runt "$BIN/fuzz_relay.exe" 1000000

step "3. Online tab states (node)"
runt node "$(win "$HERE/online_ui.js")"

igd() {   # igd MODE -> $SSDP (port), $IGD_HTTP; fake_igd.py in the background
  rm -f "$OUT/igd.ports"
  "$PY" -I "$(win "$HERE/fake_igd.py")" --mode "$1" --ports "$(win "$OUT/igd.ports")" &
  IGD_PID=$!
  for i in $(seq 1 100); do [ -s "$OUT/igd.ports" ] && break; sleep 0.1; done
  read -r SSDP IGD_HTTP < "$OUT/igd.ports"; IGD_HTTP="$(printf %s "$IGD_HTTP" | tr -d '\r')"
}
igd_stop() {   # ask it to quit; kill it if it has not gone within 5 s
  curl -s -m 3 "http://127.0.0.1:$IGD_HTTP/quit" >/dev/null 2>&1
  for i in $(seq 1 50); do kill -0 "$IGD_PID" 2>/dev/null || break; sleep 0.1; done
  kill "$IGD_PID" 2>/dev/null; wait "$IGD_PID" 2>/dev/null
}

step "4. UPnP client vs fake IGD"
for m in ok permanent conflict ours cgnat private refuse chunked nossdp noservice evil; do
  igd "$m"
  "$BIN/test_online.exe" upnp "$SSDP" "$m" > "$OUT/upnp-$m.txt" 2>&1 || rc=1
  hits=$(curl -s -m 3 "http://127.0.0.1:$IGD_HTTP/state")
  igd_stop
  grep -E "FAIL" "$OUT/upnp-$m.txt"; printf '  %-10s %s\n' "$m" "$(tail -1 "$OUT/upnp-$m.txt")"
  [ "$m" = evil ] && { echo "$hits" | grep -q '"location_hits": 0' && echo "  evil       the LOCATION outside the LAN was never fetched" || { echo "  FAIL evil: $hits"; rc=1; }; }
done

step "5. host thread lifecycle"
run "$BIN/test_online.exe" lifecycle 50 37430
igd ok
run "$BIN/test_online.exe" lifecycle 5 37430 "$SSDP"
echo "  router state after: $(curl -s -m 3 "http://127.0.0.1:$IGD_HTTP/state")"
igd_stop

step "6. loopback: desktop relay + two simulated consoles"
run "$BIN/test_online.exe" loopback 37450 "$(win "$BIN/relay_client.exe")" "$(win "$OUT")"

step "7. the real app"
EXE="$DESK/dist/GBAdhoc-XMB.exe"
app() {   # app CASE TEST MODE [clients]
  local case=$1 test=$2 mode=$3
  igd "$mode"
  rm -f "$OUT/$case.json"
  mkdir -p "$OUT/appdata-$case"
  if [ "${4:-}" = clients ]; then
    ( sleep 6
      "$BIN/relay_client.exe" --server 127.0.0.1:37440 --host --code KQTR --bind 127.0.0.1:37460 --relay-only --payloads 1500 --secs 40 > "$OUT/$case-host.txt" 2>&1 &
      sleep 0.3
      "$BIN/relay_client.exe" --server 127.0.0.1:37440 --join --code KQTR --bind 127.0.0.1:37461 --relay-only --payloads 1500 --secs 40 > "$OUT/$case-join.txt" 2>&1
      wait ) &
    CL=$!
  fi
  XMB_NET=offline XMB_APPDATA="$(win "$OUT/appdata-$case")" XMB_ONLINE_BIND=127.0.0.1 \
    XMB_UPNP_SSDP="127.0.0.1:$SSDP" XMB_SHARED_RELAY=off \
    timeout 150 "$EXE" --fake-psp "$(win "$OUT/no-such-psp")" --ui-dir "$(win "$DESK/ui")" \
      --script "$(win "$DESK/tests/e2e.js")" --open "?test=$test&port=37440" --result "$(win "$OUT/$case.json")"
  [ -n "${CL:-}" ] && { wait "$CL"; CL=; }
  echo "  router after: $(curl -s -m 3 "http://127.0.0.1:$IGD_HTTP/state")"
  igd_stop
  [ -f "$OUT/$case.json" ] || { echo "FAIL: $case: the app gave no result"; rc=1; return; }
  $PY -I "$(win "$DESK/tests/result.py")" "$(win "$OUT/$case.json")" || rc=1
}
echo "-- host with UPnP, two consoles join"; app online online ok clients
echo "-- carrier-grade NAT"; app onlinecgnat onlinecgnat cgnat
echo "-- no UPnP router"; app onlinenoupnp onlinenoupnp nossdp

echo
[ $rc = 0 ] && echo "ALL PASS" || echo "FAILED"
exit $rc
