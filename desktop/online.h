/* online.h -- "Host online room": GBAdhoc's GBR1 relay (shared/relay) on a
 * background thread of GBAdhoc XMB for Windows, with UPnP-IGD port mapping
 * and the status the Online tab shows.  GBAdhoc docs/ONLINE-TUNNEL.md 6.
 *
 * One host at a time.  online_start binds UDP port and port + 1 at once
 * (so a busy port is an immediate error) and returns; the relay thread runs
 * until online_stop, which deletes every port mapping it made and frees
 * everything (the start/stop cycle leaks nothing: desktop/tests).
 *
 * Environment (tests and experts):
 *   XMB_ONLINE_BIND=IP       bind address (default 0.0.0.0; the tests use 127.0.0.1)
 *   XMB_UPNP_SSDP=IP:PORT    where M-SEARCH goes (default 239.255.255.250:1900)
 *   XMB_SHARED_RELAY=H:P|off the shared relay used for the address check
 *                            (default relay.rayquaza.ca:27420)
 */
#ifndef XMB_ONLINE_H
#define XMB_ONLINE_H

#include "json.h"

typedef struct
{
   unsigned port;          /* 0 = 27420 (RL_DEFAULT_PORT); port + 1 is the probe port */
   int upnp;               /* 1 = ask the router for the two port mappings */
   char bind[64];          /* "" = XMB_ONLINE_BIND or 0.0.0.0 */
   char ssdp[64];          /* "" = XMB_UPNP_SSDP or the multicast group */
   char shared[128];       /* "" = XMB_SHARED_RELAY or the default; "off" = no check */
   unsigned max_rooms;     /* 0 = 16 */
} online_cfg;

void online_init(void);
/* 0 = hosting.  On failure *code is "busy" (port in use), "bad" or "running". */
int online_start(const online_cfg *cfg, char *code, size_t ccap, char *err, size_t ecap);
/* Stops the relay, deletes the mappings it made.  Blocks (bounded by the
 * router's HTTP timeouts, a few seconds at most).  Safe when not hosting. */
void online_stop(void);
int online_running(void);
/* The Online tab's whole state, as one JSON object. */
void online_status_json(sb *out);
/* The network check, hosting or not: this PC's addresses, the router (UPnP,
 * its internet address and what kind it is), the shared relay's view of
 * this PC.  Blocks a few seconds. */
void online_check_json(const online_cfg *cfg, sb *out);

/* tests: how many objects the host owns right now (0 after a stop) */
long online_live_objects(void);

#endif
