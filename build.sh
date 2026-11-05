#!/usr/bin/env bash
# build.sh -- everything, in docker: the launcher, the PSP app and the Windows app.
#   launcher/EBOOT.PBP            the per-game launcher (the template every entry carries)
#   psp/EBOOT.PBP                 GBAdhoc XMB for the PSP
#   desktop/dist/GBAdhoc-XMB.exe  GBAdhoc XMB for Windows (+ xmbwrap.exe, the command line)
# Runs from Git Bash on Windows or from WSL/Linux.  Needs docker; pulls pspdev/pspdev.
#   ./build.sh            all three
#   ./build.sh psp        launcher + PSP app
#   ./build.sh desktop    launcher + Windows app
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
export MSYS_NO_PATHCONV=1
if command -v cygpath >/dev/null 2>&1; then MNT="$(cygpath -m "$HERE")"; else MNT="$HERE"; fi
what="${1:-all}"

echo "== launcher"
docker run --rm -v "$MNT":/build -w /build/launcher pspdev/pspdev make
# The PSP app embeds the launcher as a committed header; say so if it is stale.
PY=python3; for c in python3 python py; do "$c" -c "" </dev/null >/dev/null 2>&1 && { PY=$c; break; }; done
if ! $PY "$HERE/tools/check_launcher_header.py" "$HERE/launcher/EBOOT.PBP" "$HERE/psp/gen/launcher_pbp.h"; then
  echo "note: psp/gen/launcher_pbp.h differs from launcher/EBOOT.PBP;"
  echo "      run 'python tools/embed_launcher.py' in psp/ and commit it"
fi

if [ "$what" = all ] || [ "$what" = psp ]; then
  echo "== PSP app"
  docker run --rm -v "$MNT":/build -w /build/psp pspdev/pspdev make
  ls -la "$HERE/psp/EBOOT.PBP"
fi
if [ "$what" = all ] || [ "$what" = desktop ]; then
  echo "== Windows app"
  bash "$HERE/desktop/build.sh"
fi
