#!/usr/bin/env bash
# shared/relay/tests/run_host_tests.sh -- the relay core's own tests (the NAT model, the
# desktop-host + directory model, the invite codes and the fuzz test, from GBAdhoc) and
# relay_obs's, as a host build under ASan + UBSan.
# Runs in docker (gcc:13) from Git Bash or WSL; nothing touches the network.
#   bash shared/relay/tests/run_host_tests.sh [FUZZ_ITERATIONS]
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export MSYS_NO_PATHCONV=1
if command -v cygpath >/dev/null 2>&1; then MNT="$(cygpath -m "$HERE")"; else MNT="$HERE"; fi
ITERS="${1:-2000000}"
docker run --rm --network none -v "$MNT":/r:ro -w /r gcc:13 sh -c "
  set -e; O=/tmp/o; mkdir -p \$O
  CF='-std=gnu11 -O1 -g -Wall -Wextra -I. -Iclient -fsanitize=address,undefined -fno-sanitize-recover=all'
  gcc \$CF -o \$O/test_relay_nat tests/test_relay_nat.c relay_core.c client/transport_relay.c client/netdrv.c
  gcc \$CF -o \$O/test_relay_desktop tests/test_relay_desktop.c relay_core.c relay_dirclient.c client/transport_relay.c client/netdrv.c
  gcc \$CF -o \$O/test_invite tests/test_invite.c
  gcc \$CF -o \$O/fuzz_relay tests/fuzz_relay.c relay_core.c client/transport_relay.c
  gcc \$CF -o \$O/test_obs tests/test_obs.c relay_obs.c
  echo '== NAT model (test_relay_nat)'; \$O/test_relay_nat | tail -1
  echo '== desktop host + directory (test_relay_desktop)'; \$O/test_relay_desktop | tail -1
  echo '== invite codes (test_invite)'; \$O/test_invite | tail -1
  echo '== fuzz (fuzz_relay $ITERS)'; \$O/fuzz_relay $ITERS | tail -1
  echo '== relay_obs (test_obs)'; \$O/test_obs"
