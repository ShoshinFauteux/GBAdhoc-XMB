#!/usr/bin/env bash
# desktop/build.sh -- GBAdhoc XMB for Windows, in docker with mingw-w64.
#   desktop/dist/GBAdhoc-XMB.exe   one exe: the WebView2 host, the UI inside it,
#                                  the launcher inside it.  Nothing to install
#                                  (WebView2 is part of Windows 10/11).
#   desktop/dist/xmbwrap.exe       the command-line entry maker (tools/xw_cli.c),
#                                  used by test_app.sh to set up existing entries.
#   The Online tab's relay host (online.c, upnp.c) links shared/relay/relay_core.c,
#   GBAdhoc's GBR1 relay core and relay_dirclient.c (the directory registration).
# Runs from Git Bash on Windows or from WSL/Linux.  Needs launcher/EBOOT.PBP
# (build it first: see launcher/Makefile, or run ./build.sh at the top).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TREE="$(cd "$HERE/.." && pwd)"
[ -f "$TREE/launcher/EBOOT.PBP" ] || { echo "build the launcher first (launcher/Makefile)"; exit 2; }
export MSYS_NO_PATHCONV=1
# docker on Windows wants a Windows path for the mount
if command -v cygpath >/dev/null 2>&1; then MNT="$(cygpath -m "$TREE")"; else MNT="$TREE"; fi
CTX="$MNT/desktop"
docker image inspect gbadhoc-xmb-mingw >/dev/null 2>&1 || \
  docker build -q -t gbadhoc-xmb-mingw -f "$CTX/Dockerfile" "$CTX" >/dev/null
mkdir -p "$HERE/dist" "$HERE/build"
docker run --rm -v "$MNT":/g -w /g/desktop gbadhoc-xmb-mingw sh -c '
  set -e
  python3 ../tools/embed_launcher.py ../launcher/EBOOT.PBP build/launcher_pbp.h
  python3 mkicon.py build/app.ico
  python3 embed.py ui build/ui_res.c
  x86_64-w64-mingw32-windres app.rc -O coff -o build/app_res.o
  x86_64-w64-mingw32-gcc -O2 -Wall -Wno-unknown-pragmas -Wno-missing-braces -municode -mwindows -static \
    -o dist/GBAdhoc-XMB.exe host.c wv2.c json.c util.c net.c psp.c art.c online.c upnp.c \
    ../shared/xw_core.c ../shared/xw_match.c ../shared/relay/relay_core.c ../shared/relay/relay_dirclient.c ../shared/relay/relay_obs.c build/ui_res.c build/app_res.o \
    -lole32 -luuid -lshlwapi -lshell32 -lcomdlg32 -ldwmapi -lwinhttp -ladvapi32 -lws2_32 -lbcrypt -lm 2>&1 | grep -v "initialized and declared .extern." | grep -v "EXTERN_C __declspec" | grep -v "^ *|" | grep -v "In file included" | grep -v "^ *from " || true
  x86_64-w64-mingw32-gcc -O2 -Wall -municode -static -DXW_EMBED_TEMPLATE -I../shared -Ibuild \
    -o dist/xmbwrap.exe ../tools/xw_cli.c ../shared/xw_core.c -lm
  x86_64-w64-mingw32-strip dist/GBAdhoc-XMB.exe dist/xmbwrap.exe'
ls -la "$HERE/dist"
