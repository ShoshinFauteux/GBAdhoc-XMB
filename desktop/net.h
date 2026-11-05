/* net.h -- HTTPS GETs over WinHTTP for GBAdhoc XMB, with GitHub's rate
 * limits read from the response. */
#ifndef XMB_NET_H
#define XMB_NET_H

#include "../shared/xw_core.h"

enum { NET_OK = 0, NET_OFFLINE, NET_TIMEOUT, NET_HTTP, NET_RATE };

typedef struct
{
   int err;              /* NET_* */
   int status;           /* HTTP status (200, 304, 404, 403, 429...) */
   xw_buf body;
   char etag[128];
   long long rl_remaining;   /* X-RateLimit-Remaining, -1 when absent */
   long long rl_reset;       /* X-RateLimit-Reset (unix time), 0 when absent */
   long long retry_after;    /* seconds, 0 when absent */
} net_resp;

void net_init(void);
/* GET `url`.  `etag` (may be NULL) is sent as If-None-Match.  A 403/429
 * from GitHub with no requests left comes back as NET_RATE. */
int net_get(const char *url, const char *etag, net_resp *r);
void net_resp_free(net_resp *r);

/* Unix time when GitHub's API will answer again (0 = not limited). */
long long net_rate_reset(void);
long long net_now(void);

#endif
