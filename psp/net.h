/* net.h -- GBAdhoc XMB's network: Wi-Fi bring-up and a small HTTP/1.1 client
 * with HTTPS (TLS 1.2, mbedTLS 2.28) over the firmware's sceNetInet sockets.
 *
 * WHY NOT sceHttp/sceSsl.  The firmware's SSL stops at TLS 1.0 with RC4/3DES
 * suites and a frozen root store; raw.githubusercontent.com and api.github.com
 * accept TLS 1.2+ only.  mbedTLS does the handshake on the CPU; the firmware
 * only moves bytes.
 *
 * WHERE ART COMES FROM.  Box art: thumbnails.libretro.com serves the
 * libretro-thumbnails Named_Boxarts over PLAIN HTTP (no TLS cost at all);
 * raw.githubusercontent.com/libretro-thumbnails is the HTTPS fallback.  Hero
 * art: raw.githubusercontent.com/ShoshinFauteux/GBAdhoc-heroart (HTTPS only).
 *
 * THREADS.  net_get() may run on a worker thread while the UI draws: it keeps
 * all request state on its own stack/heap.  The CA chain is parsed once (first
 * HTTPS request, under a semaphore) and only read afterwards.  net_init /
 * net_connect / net_term belong to one thread (the one that owns Wi-Fi).
 *
 * TIME.  time() on real firmware is ~uptime (memory: rtc-wallclock-psp), so
 * certificate dates are checked against sceRtcGetCurrentClock (UTC) in a
 * verify callback.  When the console clock is clearly unset (before 2025)
 * the date check is skipped and noted in net_resp.err -- the signature chain
 * to an embedded root is still required; a PSP-1000 set to 2011 would
 * otherwise reject every certificate. */
#ifndef XMBAPP_NET_H
#define XMBAPP_NET_H

#include <stddef.h>

enum
{
   NET_OK = 0,
   NET_E_INIT = -1,     /* module load / sceNet init failed */
   NET_E_OFFLINE = -2,  /* no Wi-Fi (switch off, no connection, not connected) */
   NET_E_DNS = -3,
   NET_E_CONNECT = -4,
   NET_E_TLS = -5,      /* handshake or certificate failed (err says which) */
   NET_E_TIMEOUT = -6,
   NET_E_TOOBIG = -7,   /* body larger than max_bytes */
   NET_E_CANCEL = -8,
   NET_E_PROTO = -9,    /* malformed HTTP, too many redirects, bad URL */
   NET_E_NOMEM = -10
};

enum { NET_OFF = 0, NET_CONNECTING = 1, NET_ONLINE = 2 };

/* Load the net modules and init sceNet/Inet/Resolver/Apctl.  Idempotent. */
int net_init(void);
/* Wi-Fi switch position: 1 on, 0 off (always 1 on a PSP Go / in PPSSPP). */
int net_wlan_switch(void);
/* Connect to stored connection `config` (>= 1) and wait for an IP.
 * Blocking (up to ~20 s); returns NET_OK or NET_E_*.  `cancel` may be NULL.
 * (The UI may instead run sceUtilityNetconf and then call net_state().) */
int net_connect(int config, volatile int *cancel);
int net_state(void);                 /* NET_OFF / NET_CONNECTING / NET_ONLINE */
/* The connection's name (SSID or profile name) and IP, "" when offline. */
void net_info(char *ssid, size_t ssid_cap, char *ip, size_t ip_cap);
void net_disconnect(void);
void net_term(void);

typedef struct
{
   int status;            /* HTTP status (200, 404, 403/429 = rate limited...),
                             0 when no response was read */
   unsigned char *data;   /* body (malloc'd, NUL-terminated past len), or NULL */
   size_t len;
   char err[160];         /* human-readable reason on failure, else notes */
   unsigned ms_total;     /* DNS + connect + TLS + request + body */
   unsigned ms_tls;       /* TLS handshake alone (0 for http://) */
   unsigned ms_dns;
   int rl_remaining;      /* X-RateLimit-Remaining, -1 when absent */
   long rl_reset;         /* X-RateLimit-Reset (unix seconds), 0 when absent */
   long heap_tls;         /* heap the TLS session holds once its handshake is
                             done (mallinfo delta; for measuring) */
} net_resp;

/* GET `url` (http:// or https://).  Returns NET_OK when a response was read
 * (check r->status: only 200 carries the thing asked for), else NET_E_*.
 * The body is kept for any status (GitHub's 403 JSON explains itself).
 * max_bytes: refuse bodies larger than this (NET_E_TOOBIG).  cancel: polled
 * every <=250 ms while waiting, NULL = never.  progress: called as bytes
 * arrive (total 0 = unknown length), NULL = none.  Follows up to 3
 * redirects. */
int net_get(const char *url, net_resp *r, size_t max_bytes,
            volatile int *cancel,
            void (*progress)(void *ctx, size_t got, size_t total), void *ctx);
void net_resp_free(net_resp *r);

/* Percent-encode a URL PATH component (a No-Intro name): keeps A-Z a-z 0-9
 * - . _ ~ and '/', encodes everything else (space, comma, &, parentheses,
 * apostrophes, '#', '?', '+', non-ASCII bytes) as %XX.  Returns the length,
 * or -1 if it did not fit (out is then "" ). */
int net_urlencode_path(const char *in, char *out, size_t cap);

/* Optional step trace (DNS, connect, handshake, status...) for a log file;
 * NULL = silent.  Called on the thread doing the request. */
extern void (*net_trace)(const char *msg);

#endif
