# shared/relay: the GBR1 relay core, vendored from GBAdhoc

GBAdhoc's online play (4.6.0-beta.4 and later, experimental) carries the ad hoc link over the
internet through a GBR1 relay / rendezvous server. GBAdhoc XMB for Windows can host that relay
itself (the Online tab). This folder holds the same core the public server runs, so both hosts
enforce the same rules.

## Where it comes from

| Here | Upstream (github.com/ShoshinFauteux/GBAdhoc) |
|---|---|
| `relay_core.c`, `relay_core.h`, `relay_proto.h` | `netdrv/` |
| `client/transport_relay.c/.h`, `client/netdrv.c/.h`, `client/netdrv_wire.h` | `netdrv/` |
| `tests/test_relay_nat.c`, `tests/fuzz_relay.c`, `tests/relay_client.c` | `tools/relay/` |

Upstream commit: `cb6eb47f495a4375ec7c1ac04b5ad17d200d9dd9` (branch `claude/beta4-online-zero`,
2026-11-05). The protocol and its security rules are specified in GBAdhoc's
`docs/ONLINE-TUNNEL.md`, sections 5 and 6.

Only `relay_core.c` is built into `GBAdhoc-XMB.exe`. `client/` (the console side of the
protocol and netdrv, the link-session layer) and `tests/` are built only by the tests, which
drive simulated consoles through the desktop relay.

## Licence

GBAdhoc is a fork of gpSP and is licensed **GPL-2.0-or-later** (its `COPYING`; gpSP's source
headers carry the "or (at your option) any later version" grant). These files are by the same
author as GBAdhoc XMB and come under that same licence, which matches this repository's
(`LICENSE`, GPL-2.0-or-later). No other licence applies to them.

## Local changes

Upstream files are copied unchanged except for one marked addition to the core, a read-only view
of the room table for the Online tab's room and player list:

- `relay_core.h`: `rl_slot_view`, `rl_room_view` and `relay_core_rooms()`;
- `relay_core.c`: `rl_slot.since_ms` (set when a slot is filled) and `relay_core_rooms()`.

The view copies codes, slot numbers, addresses, NAT classes and times; it never copies a token and
never changes state. Every addition is marked `XMB addition`. It is a candidate to upstream.

Updating: copy the upstream files over these, re-apply the marked addition, and run
`desktop/tests/test_online.sh` (the NAT model, the fuzz test, and the desktop host's own tests).
