/* relay_obs.h -- what a relay host can measure about each console without
 * changing a byte: packets per second, a round-trip estimate and a loss
 * estimate, taken from the datagrams the relay core already receives and
 * forwards.  GBAdhoc XMB's own code (not upstream): the Online tab's player
 * list.
 *
 * The relay core never parses the netdrv frame inside DATA, and neither does
 * forwarding here.  The observer READS the frame's 24-byte header (type, ids,
 * seq, ack) for statistics only, after the core decided what to forward:
 *
 *   ping   the relay forwards netdrv DATA seq S to console X at time t; the
 *          first frame from X whose cumulative ack passes S closes the
 *          sample: now - t is the round trip relay -> X -> relay (plus X's
 *          ack delay, at most a frame or two).  Retransmitted sequence
 *          numbers give no sample (Karn's rule).
 *   loss   of the reliable DATA X sends, the share that repeats a sequence
 *          number already seen: netdrv retransmits when a frame or its ack
 *          was lost anywhere on the path, so this is the session's
 *          end-to-end loss as X experiences it.  Over the last ~10 s.
 *   nick   netdrv's JOIN carries the player's nickname; kept for display.
 *
 * Bounded: a fixed table of OBS_N consoles, the stalest recycled.  No
 * allocation, no threads, no I/O.
 */
#ifndef RELAY_OBS_H
#define RELAY_OBS_H

#include <stddef.h>
#include <stdint.h>

#include "relay_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBS_N 64
#define OBS_PEND 16
#define OBS_STREAMS 4

typedef struct
{
   uint8_t used, src, dst;
   uint16_t hi;            /* highest seq seen in this stream */
   uint64_t seen;          /* bit i = seq hi - i was seen */
} obs_stream;

typedef struct
{
   uint8_t used, src, dst, retx;
   uint16_t seq;
   uint64_t t_us;
} obs_pend;

typedef struct
{
   int used;
   rl_addr addr;
   uint64_t first_us, last_us;
   char nick[17];
   /* totals */
   uint32_t rx, tx, rx_data, rx_retx;
   /* the current and the previous 5 s window */
   uint64_t win_us;
   uint32_t w_rx, w_tx, w_data, w_retx;
   uint32_t p_rx, p_tx, p_data, p_retx;
   uint32_t p_len_us;      /* length of the previous window */
   /* round trip, smoothed (0 = no sample yet) */
   uint32_t srtt_us, rtt_samples;
   int direct;             /* the console reported a direct path (PATH) */
   obs_stream st[OBS_STREAMS];
   obs_pend pend[OBS_PEND];
} obs_ent;

typedef struct
{
   obs_ent e[OBS_N];
} relay_obs;

void relay_obs_init(relay_obs *o);
/* a datagram the host received from `from` (before relay_core_input) */
void relay_obs_rx(relay_obs *o, const rl_addr *from, const void *buf, size_t len, uint64_t now_us);
/* a datagram the core sent to `to` */
void relay_obs_tx(relay_obs *o, const rl_addr *to, const void *buf, size_t len, uint64_t now_us);
/* NULL when nothing was seen from that address */
const obs_ent *relay_obs_find(const relay_obs *o, const rl_addr *a);

/* Derived figures for display.  -1 = not known yet. */
int relay_obs_ping_ms(const obs_ent *e);
int relay_obs_loss_permille(const obs_ent *e, uint64_t now_us);
int relay_obs_pps_in(const obs_ent *e, uint64_t now_us);
int relay_obs_pps_out(const obs_ent *e, uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_OBS_H */
