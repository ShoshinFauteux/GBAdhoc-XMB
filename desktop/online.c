/* online.c -- the desktop relay host: shared/relay/relay_core.c on a
 * background thread, UPnP-IGD on a second one, and the Online tab's status.
 * See online.h.  The core enforces the server's rules unchanged (the cookie
 * challenge, no amplification, per-IP and per-slot limits, bounded tables);
 * this file adds bounded logging and the read-only statistics of relay_obs.
 */
#include "online.h"
#include "upnp.h"
#include "../shared/relay/relay_core.h"
#include "../shared/relay/relay_obs.h"
#include "../shared/relay/relay_dirclient.h"
#include "../shared/relay/invite_code.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#define LOG_N 80
#define LEASE_S 3600
#define RENEW_MS (30u * 60u * 1000u)
#define DEFAULT_SHARED "relay.rayquaza.ca:27420"
#define MAX_VIEW 32

typedef struct
{
   char t[12];
   char tone[8];
   char text[200];
} log_line;

enum { PR_OFF = 0, PR_PENDING, PR_WAIT, PR_OK, PR_NOANSWER, PR_NODNS };

static struct
{
   int inited;
   CRITICAL_SECTION cs;
   volatile LONG running, stop;
   HANDLE relay_t, upnp_t, stop_ev;
   SOCKET s[2];
   online_cfg cfg;
   rl_core *core;
   relay_obs *obs;
   uint64_t since_ms;
   /* the log ring */
   log_line log[LOG_N];
   unsigned log_n, log_head;
   unsigned err_logged;
   uint64_t err_win_ms;
   uint32_t last_rate, last_refused;
   /* UPnP */
   char up_state[16], up_detail[240], up_router[96], up_wan[16], up_local[16];
   int up_permanent, up_mapped[2];
   upnp_igd igd;
   /* the shared relay's view of this host */
   int pr_state;
   char pr_name[128], pr_pub[16];
   unsigned pr_pub_port;
   rl_addr pr_addr;
   uint32_t pr_nonce;
   uint64_t pr_sent_ms;
   char last_err[200];
   /* this PC's own room: its code (relay_core_set_home), and the directory
    * registration of that code (relay_dirclient) */
   char home[RL_CODE_LEN + 1];
   int dc_on;
   rl_dirc dc;
} G;

static volatile LONG g_live;

long online_live_objects(void)
{
   return g_live;
}

void online_init(void)
{
   WSADATA w;
   if (G.inited)
      return;
   WSAStartup(MAKEWORD(2, 2), &w);
   InitializeCriticalSection(&G.cs);
   G.s[0] = G.s[1] = INVALID_SOCKET;
   G.inited = 1;
}

static uint64_t now_ms(void)
{
   return GetTickCount64();
}

static uint64_t rnd64(void *u)
{
   uint64_t v = 0;
   (void)u;
   if (BCryptGenRandom(NULL, (PUCHAR)&v, sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
      v = GetTickCount64() * 6364136223846793005ull ^ (uint64_t)GetCurrentProcessId() * 1442695040888963407ull;
   return v;
}

/* ---- the log ------------------------------------------------------------- */

static void logf_(const char *tone, const char *fmt, ...)
#if defined(__GNUC__)
   __attribute__((format(printf, 2, 3)))
#endif
   ;
static void logf_(const char *tone, const char *fmt, ...)
{
   log_line *l;
   va_list a;
   time_t t = time(NULL);
   struct tm tm;
   EnterCriticalSection(&G.cs);
   l = &G.log[(G.log_head + G.log_n) % LOG_N];
   if (G.log_n == LOG_N)
      G.log_head = (G.log_head + 1) % LOG_N;
   else
      G.log_n++;
   localtime_s(&tm, &t);
   strftime(l->t, sizeof(l->t), "%H:%M:%S", &tm);
   snprintf(l->tone, sizeof(l->tone), "%s", tone);
   va_start(a, fmt);
   vsnprintf(l->text, sizeof(l->text), fmt, a);
   va_end(a);
   LeaveCriticalSection(&G.cs);
}

static const char *where_of(uint32_t ip_be)
{
   char s[16];
   const uint8_t *p = (const uint8_t *)&ip_be;
   snprintf(s, sizeof(s), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
   switch (upnp_ip_class(s))
   {
   case IPC_LOOPBACK: return "this PC";
   case IPC_PRIVATE: case IPC_LINKLOCAL: return "LAN";
   default: return "internet";
   }
}

/* a LAN address in full, an internet one with its last two parts hidden */
static void ip_shown(uint32_t ip_be, char out[24])
{
   const uint8_t *p = (const uint8_t *)&ip_be;
   if (!strcmp(where_of(ip_be), "internet"))
      snprintf(out, 24, "%u.%u.x.x", p[0], p[1]);
   else
      snprintf(out, 24, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static int kv(const char *line, const char *key, char *out, size_t cap)
{
   char k[24];
   const char *p;
   size_t n;
   snprintf(k, sizeof(k), "%s=", key);
   p = strstr(line, k);
   out[0] = '\0';
   if (!p)
      return 0;
   p += strlen(k);
   n = strcspn(p, " ");
   if (n >= cap)
      n = cap - 1;
   memcpy(out, p, n);
   out[n] = '\0';
   return 1;
}

static void from_ipport(const char *s, char out[48])
{
   unsigned a, b, c, d;
   uint8_t x[4];
   uint32_t v;
   char ip[24];
   if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
   {
      snprintf(out, 48, "%s", s);
      return;
   }
   x[0] = (uint8_t)a; x[1] = (uint8_t)b; x[2] = (uint8_t)c; x[3] = (uint8_t)d;
   memcpy(&v, x, 4);
   ip_shown(v, ip);
   snprintf(out, 48, "%s, %s", where_of(v), ip);
}

/* the core's event lines, in words */
static void core_log(void *u, const char *line)
{
   char code[8], slot[4], why[24], who[40], w[48], peer[4], path[12];
   (void)u;
   kv(line, "code", code, sizeof(code));
   kv(line, "slot", slot, sizeof(slot));
   kv(line, "why", why, sizeof(why));
   if (!strncmp(line, "room_create", 11))
   {
      kv(line, "host", who, sizeof(who));
      from_ipport(who, w);
      logf_("ok", "Room %s opened by a PSP (%s)", code, w);
   }
   else if (!strncmp(line, "room_join", 9))
   {
      kv(line, "from", who, sizeof(who));
      from_ipport(who, w);
      logf_("ok", "A PSP joined room %s as player %d (%s)", code, atoi(slot) + 1, w);
   }
   else if (!strncmp(line, "slot_leave", 10))
      logf_(strcmp(why, "bye") ? "warn" : "", "Player %d %s room %s", atoi(slot) + 1,
            !strcmp(why, "bye") ? "left" : "went silent for 20 s and was dropped from", code);
   else if (!strncmp(line, "room_close", 10))
      logf_("", "Room %s closed: %s", code,
            !strcmp(why, "host_left") ? "its host left" :
            !strcmp(why, "lobby_expired") ? "nobody joined within 10 minutes" :
            !strcmp(why, "idle") ? "no traffic for a minute" : why);
   else if (!strncmp(line, "slot_repin", 10))
      logf_("", "Player %d of room %s changed address (the router renewed its mapping)", atoi(slot) + 1, code);
   else if (!strncmp(line, "path", 4))
   {
      kv(line, "peer", peer, sizeof(peer));
      kv(line, "path", path, sizeof(path));
      logf_("", "Room %s: player %d and player %d now talk %s", code, atoi(slot) + 1, atoi(peer) + 1,
            !strcmp(path, "direct") ? "directly (peer to peer)" : "through this PC");
   }
   else
      logf_("", "%s", line);
}

/* ---- sockets ---------------------------------------------------------------- */

static void core_send(void *u, int sock, const rl_addr *to, const void *buf, size_t len)
{
   struct sockaddr_in a;
   const uint8_t *b = (const uint8_t *)buf;
   (void)u;
   if (sock < 0 || sock > 1 || G.s[sock] == INVALID_SOCKET || !to->port)
      return;
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_addr.s_addr = to->ip_be;
   a.sin_port = htons(to->port);
   sendto(G.s[sock], (const char *)buf, (int)len, 0, (struct sockaddr *)&a, sizeof(a));
   relay_obs_tx(G.obs, to, buf, len, now_ms() * 1000u);
   if (len > RL_HDR && b[5] == RL_T_ERROR)
   {
      /* refusals, at most 5 lines per 10 s (a flood must not wash the log out) */
      uint64_t t = now_ms();
      char ip[24], text[32];
      size_t tl = len - RL_HDR - 1 < sizeof(text) - 1 ? len - RL_HDR - 1 : sizeof(text) - 1;
      if (t - G.err_win_ms > 10000)
      {
         G.err_win_ms = t;
         G.err_logged = 0;
      }
      if (G.err_logged++ < 5)
      {
         memcpy(text, b + RL_HDR + 1, tl);
         text[tl] = '\0';
         ip_shown(to->ip_be, ip);
         logf_(b[RL_HDR] == RL_E_NO_ROOM ? "" : "warn", "Refused a PSP (%s, %s): %s%s", where_of(to->ip_be), ip,
               text, b[RL_HDR] == RL_E_VERSION ? " (both PSPs need the same GBAdhoc build)" : "");
      }
   }
}

static int resolve(const char *hp, rl_addr *out)
{
   char h[128], *c;
   struct addrinfo hints, *res = NULL;
   snprintf(h, sizeof(h), "%s", hp);
   c = strrchr(h, ':');
   out->port = RL_DEFAULT_PORT;
   if (c)
   {
      *c = '\0';
      out->port = (uint16_t)atoi(c + 1);
   }
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;
   if (getaddrinfo(h, NULL, &hints, &res) != 0 || !res)
      return -1;
   out->ip_be = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
   freeaddrinfo(res);
   return 0;
}

static void send_probe(SOCKET s, const rl_addr *to, int sockno, uint32_t nonce)
{
   uint8_t b[RL_HDR + 11];          /* padded to the reply's size: never amplifies */
   struct sockaddr_in a;
   memset(b, 0, sizeof(b));
   rl_hdr(b, RL_T_PROBE, RL_SLOT_NONE, RL_SLOT_NONE, 0);
   rl_put32(b + RL_HDR, nonce);
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_addr.s_addr = to->ip_be;
   a.sin_port = htons((u_short)(to->port + sockno));
   sendto(s, (const char *)b, sizeof(b), 0, (struct sockaddr *)&a, sizeof(a));
}

static const char *shared_name(const online_cfg *c)
{
   const char *e = getenv("XMB_SHARED_RELAY");
   if (c && c->shared[0])
      return c->shared;
   return e && e[0] ? e : DEFAULT_SHARED;
}

static SOCKET udp_bind(const char *ip, unsigned port)
{
   SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   struct sockaddr_in a;
   u_long nb = 1;
   BOOL f = FALSE, ex = TRUE;
   DWORD got;
   if (s == INVALID_SOCKET)
      return s;
   /* a second app on the same port must fail, not share it */
   setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&ex, sizeof(ex));
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_port = htons((u_short)port);
   a.sin_addr.s_addr = inet_addr(ip);
   if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0)
   {
      closesocket(s);
      return INVALID_SOCKET;
   }
   ioctlsocket(s, FIONBIO, &nb);
   /* an ICMP "port unreachable" must not turn into WSAECONNRESET on the next recv */
   WSAIoctl(s, SIO_UDP_CONNRESET, &f, sizeof(f), NULL, 0, &got, NULL, NULL);
   return s;
}

/* ---- this PC's room code and the directory ------------------------------------
 * The room code is picked here at start (four letters) and given to the core as
 * its home code, so a PSP that came by an invite code (no code of its own) lands
 * in this PC's room and invite codes work with no server.  With the directory on,
 * the same code is registered with the shared relay (relay_dirclient) from the
 * relay's own socket, so the directory sees the router's mapping of it; a code
 * the directory finds taken is replaced by its pick, and the core is told.
 * Registered whenever hosting with the directory on, whatever the verdict: a code
 * that cannot be reached from outside is harmless, and PSPs on this network can
 * use it.  The status says the verdict so the tab can say "players outside cannot
 * reach this PC". */

static void dc_send(void *u, uint32_t ip, uint16_t port, const void *buf, size_t len)
{
   struct sockaddr_in a;
   (void)u;
   if (G.s[0] == INVALID_SOCKET)
      return;
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_addr.s_addr = ip;
   a.sin_port = htons(port);
   sendto(G.s[0], (const char *)buf, (int)len, 0, (struct sockaddr *)&a, sizeof(a));
}

static void dc_log(void *u, const char *line)
{
   char code[8], why[40];
   (void)u;
   kv(line, "code", code, sizeof(code));
   if (!strncmp(line, "dir_registered", 14))
      logf_("ok", "Room %s is listed in the directory (%s): PSPs anywhere can join with these four letters", code,
            G.pr_name);
   else if (!strncmp(line, "dir_lost", 8))
      logf_("warn", "The directory no longer knew room %s (it may have restarted): registering it again", code);
   else if (!strncmp(line, "dir_code_taken", 14))
      logf_("warn", "Room code %s is taken in the directory: asking it for another", code);
   else if (!strncmp(line, "dir_refused", 11))
   {
      const char *q = strstr(line, "why=\"");
      snprintf(why, sizeof(why), "%s", q ? q + 5 : "");
      if (strchr(why, '"'))
         *strchr(why, '"') = '\0';
      logf_("warn", "The directory refused the room code (%s). The invite code still works; retrying in 60 s", why);
   }
   else
      logf_("", "%s", line);
}

/* this PC's LAN address: the bind address when one is given, else the first
 * private address of this host; 0 = none */
static uint32_t first_lan_ip(const char *bind)
{
   char host[256];
   struct addrinfo hints, *res = NULL, *r;
   uint32_t ip = 0;
   if (strcmp(bind, "0.0.0.0"))
      return inet_addr(bind);
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   if (gethostname(host, sizeof(host)) == 0 && getaddrinfo(host, NULL, &hints, &res) == 0)
   {
      for (r = res; r && !ip; r = r->ai_next)
      {
         const char *s = inet_ntoa(((struct sockaddr_in *)r->ai_addr)->sin_addr);
         if (upnp_ip_class(s) == IPC_PRIVATE)
            ip = ((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr;
      }
      freeaddrinfo(res);
   }
   return ip;
}

static void pick_home(char out[RL_CODE_LEN + 1])
{
   uint64_t x = rnd64(NULL);
   int k;
   for (k = 0; k < RL_CODE_LEN; k++, x /= RL_CODE_SYMBOLS)
      out[k] = RL_CODE_ALPHABET[x % RL_CODE_SYMBOLS];
   out[RL_CODE_LEN] = '\0';
}

static int directory_wanted(const online_cfg *c)
{
   const char *e = getenv("XMB_DIRECTORY");
   if (e && (!strcmp(e, "off") || !strcmp(e, "0")))
      return 0;
   return c->directory != 0;
}

/* ---- the relay thread --------------------------------------------------------- */

static int authenticated_type(int t)
{
   return t == RL_T_DATA || t == RL_T_KEEP || t == RL_T_BYE || t == RL_T_PATH;
}

static DWORD WINAPI relay_thread(LPVOID p)
{
   static uint8_t buf[2048];
   uint64_t last_tick = 0, last_rate = now_ms();
   (void)p;
   SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);   /* no sleep while hosting */
   while (!G.stop)
   {
      fd_set f;
      struct timeval tv = { 0, 50000 };
      int k;
      uint64_t t;
      FD_ZERO(&f);
      FD_SET(G.s[0], &f);
      FD_SET(G.s[1], &f);
      select(0, &f, NULL, NULL, &tv);
      for (k = 0; k < 2; k++)
      {
         int n, budget = 512;
         while (budget-- > 0)
         {
            struct sockaddr_in a;
            int al = sizeof(a);
            rl_addr from;
            n = recvfrom(G.s[k], (char *)buf, sizeof(buf), 0, (struct sockaddr *)&a, &al);
            if (n < 0)
            {
               if (WSAGetLastError() == WSAEMSGSIZE)
                  continue;            /* oversized: dropped, as the core would */
               break;
            }
            from.ip_be = a.sin_addr.s_addr;
            from.port = ntohs(a.sin_port);
            t = now_ms();
            EnterCriticalSection(&G.cs);
            /* the shared relay's answer to our address check */
            if (k == 0 && G.pr_state == PR_WAIT && from.ip_be == G.pr_addr.ip_be &&
                from.port == G.pr_addr.port && n >= RL_HDR + 11 && rl_check(buf, n) == 0 &&
                buf[5] == RL_T_PROBED && rl_get32(buf + RL_HDR) == G.pr_nonce)
            {
               const uint8_t *q = (const uint8_t *)&(uint32_t){ rl_get_ip(buf + RL_HDR + 4) };
               snprintf(G.pr_pub, sizeof(G.pr_pub), "%u.%u.%u.%u", q[0], q[1], q[2], q[3]);
               G.pr_pub_port = rl_get16(buf + RL_HDR + 8);
               G.pr_state = PR_OK;
               logf_("ok", "The shared relay sees this PC at %s", G.pr_pub);
               LeaveCriticalSection(&G.cs);
               continue;
            }
            /* the directory's answers to our REGISTERs: never the core's */
            if (k == 0 && G.dc_on && rl_dirc_input(&G.dc, from.ip_be, from.port, buf, (size_t)n, t))
            {
               if (G.dc.code_changed)
               {
                  G.dc.code_changed = 0;
                  if (strcmp(G.home, G.dc.code))
                     logf_("", "Room code changed to %s (the directory's pick)", G.dc.code);
                  snprintf(G.home, sizeof(G.home), "%s", G.dc.code);
                  relay_core_set_home(G.core, G.home);
               }
               LeaveCriticalSection(&G.cs);
               continue;
            }
            {
               rl_core_stats b4, af;
               relay_core_stats(G.core, &b4);
               relay_core_input(G.core, k, &from, buf, (size_t)n, t);
               relay_core_stats(G.core, &af);
               if (n >= RL_HDR && authenticated_type(buf[5]) && af.rx_token == b4.rx_token &&
                   af.rx_bad == b4.rx_bad && af.rx_rate == b4.rx_rate && af.rx_bytes_rate == b4.rx_bytes_rate)
                  relay_obs_rx(G.obs, &from, buf, (size_t)n, t * 1000u);
            }
            LeaveCriticalSection(&G.cs);
         }
      }
      t = now_ms();
      if (t - last_tick >= 100)
      {
         last_tick = t;
         EnterCriticalSection(&G.cs);
         relay_core_tick(G.core, t);
         if (G.dc_on)
            rl_dirc_tick(&G.dc, t);
         if (G.pr_state == PR_PENDING)
         {
            G.pr_nonce = (uint32_t)rnd64(NULL) | 1u;
            G.pr_sent_ms = t;
            G.pr_state = PR_WAIT;
            send_probe(G.s[0], &G.pr_addr, 0, G.pr_nonce);
         }
         else if (G.pr_state == PR_WAIT && t - G.pr_sent_ms > 3000)
         {
            G.pr_state = PR_NOANSWER;
            logf_("warn", "The shared relay (%s) did not answer the address check", G.pr_name);
         }
         LeaveCriticalSection(&G.cs);
      }
      if (t - last_rate >= 10000)
      {
         rl_core_stats st;
         last_rate = t;
         EnterCriticalSection(&G.cs);
         relay_core_stats(G.core, &st);
         if (st.rx_rate + st.rx_bytes_rate > G.last_rate)
            logf_("warn", "Rate limit: dropped %u packets in the last 10 s",
                  (unsigned)(st.rx_rate + st.rx_bytes_rate - G.last_rate));
         G.last_rate = st.rx_rate + st.rx_bytes_rate;
         LeaveCriticalSection(&G.cs);
      }
   }
   SetThreadExecutionState(ES_CONTINUOUS);
   return 0;
}

/* ---- the UPnP thread ------------------------------------------------------------- */

static void up_set(const char *state, const char *detail)
{
   EnterCriticalSection(&G.cs);
   snprintf(G.up_state, sizeof(G.up_state), "%s", state);
   snprintf(G.up_detail, sizeof(G.up_detail), "%s", detail ? detail : "");
   LeaveCriticalSection(&G.cs);
}

static int map_one(upnp_igd *igd, unsigned short port, unsigned lease, int *perm, char *err, size_t cap)
{
   int code = 0, r;
   r = upnp_add(igd, port, port, "UDP", lease, "GBAdhoc XMB relay", &code, err, cap);
   if (r == UPNP_SOAP && code == 725)
   {
      /* OnlyPermanentLeasesSupported: map without a lease (deleted on stop) */
      *perm = 1;
      r = upnp_add(igd, port, port, "UDP", 0, "GBAdhoc XMB relay", &code, err, cap);
   }
   if (r == UPNP_SOAP && code == 718)
   {
      char client[16], e2[100];
      unsigned short in = 0;
      if (upnp_get_mapping(igd, port, "UDP", client, &in, e2, sizeof(e2)) == 0)
      {
         if (!strcmp(client, igd->local_ip) && in == port)
            return 0;          /* ours, left by an earlier run: fine */
         snprintf(err, cap, "UDP %u is already forwarded to another device (%s).", port, client);
      }
      return 718;
   }
   return r ? -1 : 0;
}

static DWORD WINAPI upnp_thread(LPVOID p)
{
   upnp_opts o;
   upnp_igd igd;
   char err[240], wan[16];
   const char *env = getenv("XMB_UPNP_SSDP");
   unsigned port = G.cfg.port;
   int i, perm = 0, ok;
   (void)p;
   memset(&o, 0, sizeof(o));
   snprintf(o.ssdp, sizeof(o.ssdp), "%s", G.cfg.ssdp[0] ? G.cfg.ssdp : env ? env : "");
   up_set("searching", "Looking for a UPnP router on this network");
   if (upnp_discover(&o, &igd, err, sizeof(err)) != 0)
   {
      up_set("none", err);
      logf_("warn", "UPnP: %s", err);
      goto wait;
   }
   EnterCriticalSection(&G.cs);
   G.igd = igd;
   snprintf(G.up_router, sizeof(G.up_router), "%s", igd.friendly[0] ? igd.friendly : igd.host);
   snprintf(G.up_local, sizeof(G.up_local), "%s", igd.local_ip);
   LeaveCriticalSection(&G.cs);
   logf_("", "UPnP: found %s at %s", igd.friendly[0] ? igd.friendly : "a router", igd.host);
   if (upnp_external_ip(&igd, wan, err, sizeof(err)) == 0)
   {
      EnterCriticalSection(&G.cs);
      snprintf(G.up_wan, sizeof(G.up_wan), "%s", wan);
      LeaveCriticalSection(&G.cs);
   }
   ok = 1;
   for (i = 0; i < 2 && ok && !G.stop; i++)
   {
      int r = map_one(&igd, (unsigned short)(port + i), LEASE_S, &perm, err, sizeof(err));
      if (r == 0)
      {
         EnterCriticalSection(&G.cs);
         G.up_mapped[i] = 1;
         G.up_permanent = perm;
         LeaveCriticalSection(&G.cs);
      }
      else
      {
         ok = 0;
         up_set(r == 718 ? "conflict" : "failed", err);
         logf_("bad", "UPnP: %s", err);
      }
   }
   if (ok && !G.stop)
   {
      char d[120];
      snprintf(d, sizeof(d), "UDP %u-%u open on %s%s", port, port + 1, igd.friendly[0] ? igd.friendly : igd.host,
               perm ? " (without a lease: removed when you stop)" : "");
      up_set("mapped", d);
      logf_("ok", "UPnP: the router opened UDP %u-%u to this PC (%s)", port, port + 1, igd.local_ip);
   }
wait:
   while (WaitForSingleObject(G.stop_ev, RENEW_MS) == WAIT_TIMEOUT)
   {
      /* renew the leases (every 30 min, lease 1 h) */
      for (i = 0; i < 2; i++)
         if (G.up_mapped[i] && !G.up_permanent)
            map_one(&igd, (unsigned short)(port + i), LEASE_S, &perm, err, sizeof(err));
   }
   for (i = 0; i < 2; i++)
      if (G.up_mapped[i])
      {
         int code;
         if (upnp_delete(&igd, (unsigned short)(port + i), "UDP", &code, err, sizeof(err)) == 0)
            logf_("", "UPnP: closed UDP %u on the router", port + i);
         else
            logf_("warn", "UPnP: could not close UDP %u: %s", port + i, err);
         G.up_mapped[i] = 0;
      }
   return 0;
}

/* ---- start / stop ------------------------------------------------------------------ */

int online_running(void)
{
   return G.running != 0;
}

int online_start(const online_cfg *cfg, char *code, size_t ccap, char *err, size_t ecap)
{
   rl_core_cfg cc;
   const char *bind_env = getenv("XMB_ONLINE_BIND");
   online_init();
   if (G.running)
   {
      snprintf(code, ccap, "running");
      snprintf(err, ecap, "Already hosting.");
      return -1;
   }
   G.cfg = *cfg;
   if (!G.cfg.port)
      G.cfg.port = RL_DEFAULT_PORT;
   if (G.cfg.port < 1024 || G.cfg.port > 65534)
   {
      snprintf(code, ccap, "bad");
      snprintf(err, ecap, "Pick a port between 1024 and 65534.");
      return -1;
   }
   if (!G.cfg.bind[0])
      snprintf(G.cfg.bind, sizeof(G.cfg.bind), "%s", bind_env && bind_env[0] ? bind_env : "0.0.0.0");
   if (inet_addr(G.cfg.bind) == INADDR_NONE && strcmp(G.cfg.bind, "255.255.255.255"))
   {
      snprintf(code, ccap, "bad");
      snprintf(err, ecap, "The bind address %s is not an IPv4 address.", G.cfg.bind);
      return -1;
   }
   if (!G.cfg.max_rooms)
      G.cfg.max_rooms = 16;
   G.s[0] = udp_bind(G.cfg.bind, G.cfg.port);
   G.s[1] = G.s[0] == INVALID_SOCKET ? INVALID_SOCKET : udp_bind(G.cfg.bind, G.cfg.port + 1);
   if (G.s[0] == INVALID_SOCKET || G.s[1] == INVALID_SOCKET)
   {
      if (G.s[0] != INVALID_SOCKET)
         closesocket(G.s[0]);
      G.s[0] = G.s[1] = INVALID_SOCKET;
      snprintf(code, ccap, "busy");
      snprintf(err, ecap, "UDP port %u or %u is in use by another program (another relay, or GBAdhoc XMB "
                          "already hosting). Close it, or pick another port.", G.cfg.port, G.cfg.port + 1);
      return -1;
   }
   InterlockedAdd(&g_live, 2);
   memset(&cc, 0, sizeof(cc));
   cc.max_rooms = G.cfg.max_rooms;
   cc.send = core_send;
   cc.random64 = rnd64;
   cc.log = core_log;
   G.core = relay_core_create(&cc);
   G.obs = (relay_obs *)calloc(1, sizeof(relay_obs));
   if (!G.core || !G.obs)
   {
      relay_core_destroy(G.core);
      free(G.obs);
      G.core = NULL;
      G.obs = NULL;
      closesocket(G.s[0]);
      closesocket(G.s[1]);
      G.s[0] = G.s[1] = INVALID_SOCKET;
      InterlockedAdd(&g_live, -2);
      snprintf(code, ccap, "error");
      snprintf(err, ecap, "Out of memory.");
      return -1;
   }
   InterlockedAdd(&g_live, 2);
   relay_obs_init(G.obs);
   EnterCriticalSection(&G.cs);
   G.log_n = G.log_head = 0;
   G.err_logged = 0;
   G.last_rate = 0;
   G.since_ms = now_ms();
   G.up_router[0] = G.up_wan[0] = G.up_local[0] = '\0';
   G.up_mapped[0] = G.up_mapped[1] = G.up_permanent = 0;
   memset(&G.igd, 0, sizeof(G.igd));
   G.last_err[0] = '\0';
   snprintf(G.pr_name, sizeof(G.pr_name), "%s", shared_name(&G.cfg));
   G.pr_pub[0] = '\0';
   G.pr_state = PR_OFF;
   if (strcmp(G.pr_name, "off"))
      G.pr_state = resolve(G.pr_name, &G.pr_addr) == 0 ? PR_PENDING : PR_NODNS;
   pick_home(G.home);
   relay_core_set_home(G.core, G.home);
   G.dc_on = 0;
   memset(&G.dc, 0, sizeof(G.dc));
   if (directory_wanted(&G.cfg) && G.pr_state == PR_PENDING)
   {
      rl_dirc_cfg dc;
      memset(&dc, 0, sizeof(dc));
      dc.dir_ip_be = G.pr_addr.ip_be;
      dc.dir_port = G.pr_addr.port;
      dc.adv_port = (uint16_t)G.cfg.port;      /* UPnP maps port -> the same port */
      dc.priv_ip_be = first_lan_ip(G.cfg.bind);
      dc.priv_port = (uint16_t)G.cfg.port;
      snprintf(dc.want, sizeof(dc.want), "%s", G.home);
      dc.send = dc_send;
      dc.log = dc_log;
      rl_dirc_start(&G.dc, &dc, now_ms());
      G.dc_on = 1;
   }
   LeaveCriticalSection(&G.cs);
   logf_("ok", "Hosting on UDP %u-%u (%s)", G.cfg.port, G.cfg.port + 1,
         !strcmp(G.cfg.bind, "0.0.0.0") ? "every network of this PC" : G.cfg.bind);
   if (G.pr_state == PR_NODNS)
      logf_("warn", "The shared relay (%s) could not be found (no DNS answer)", G.pr_name);
   G.stop = 0;
   G.stop_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
   InterlockedIncrement(&g_live);
   if (G.cfg.upnp)
   {
      G.upnp_t = CreateThread(NULL, 0, upnp_thread, NULL, 0, NULL);
      if (G.upnp_t)
         InterlockedIncrement(&g_live);
   }
   else
   {
      up_set("off", "Port mapping is off: only PSPs on this network can reach this PC");
      G.upnp_t = NULL;
   }
   G.relay_t = CreateThread(NULL, 0, relay_thread, NULL, 0, NULL);
   InterlockedIncrement(&g_live);
   G.running = 1;
   snprintf(code, ccap, "ok");
   return 0;
}

void online_stop(void)
{
   if (!G.inited || !G.running)
      return;
   InterlockedExchange(&G.stop, 1);
   SetEvent(G.stop_ev);
   if (G.upnp_t)
   {
      WaitForSingleObject(G.upnp_t, INFINITE);
      CloseHandle(G.upnp_t);
      G.upnp_t = NULL;
      InterlockedDecrement(&g_live);
   }
   WaitForSingleObject(G.relay_t, INFINITE);
   CloseHandle(G.relay_t);
   G.relay_t = NULL;
   InterlockedDecrement(&g_live);
   CloseHandle(G.stop_ev);
   G.stop_ev = NULL;
   InterlockedDecrement(&g_live);
   EnterCriticalSection(&G.cs);
   if (G.dc_on)
   {
      rl_dirc_stop(&G.dc);                     /* UNREGISTER: the code is free at once */
      G.dc_on = 0;
   }
   closesocket(G.s[0]);
   closesocket(G.s[1]);
   G.s[0] = G.s[1] = INVALID_SOCKET;
   relay_core_destroy(G.core);
   free(G.obs);
   G.core = NULL;
   G.obs = NULL;
   G.running = 0;
   up_set("", "");
   LeaveCriticalSection(&G.cs);
   InterlockedAdd(&g_live, -4);
   logf_("", "Stopped hosting");
}

/* ---- status ------------------------------------------------------------------------- */

static void lan_ips(sb *b, const char *bind)
{
   char host[256];
   struct addrinfo hints, *res = NULL, *r;
   int first = 1;
   sb_add(b, "[");
   if (strcmp(bind, "0.0.0.0"))
   {
      sb_jstr(b, bind);
      sb_add(b, "]");
      return;
   }
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   if (gethostname(host, sizeof(host)) == 0 && getaddrinfo(host, NULL, &hints, &res) == 0)
   {
      for (r = res; r; r = r->ai_next)
      {
         const char *ip = inet_ntoa(((struct sockaddr_in *)r->ai_addr)->sin_addr);
         int c = upnp_ip_class(ip);
         if (c == IPC_LOOPBACK || c == IPC_INVALID || c == IPC_LINKLOCAL)
            continue;
         if (!first)
            sb_add(b, ",");
         sb_jstr(b, ip);
         first = 0;
      }
      freeaddrinfo(res);
   }
   sb_add(b, "]");
}

/* the reachability verdict, from the facts */
static const char *verdict(const char *up_state, const char *wan, int pr_state, const char *pub)
{
   int wc = wan[0] ? upnp_ip_class(wan) : -1;
   if (!strcmp(up_state, "searching"))
      return "checking";
   if (wc == IPC_CGNAT || (pr_state == PR_OK && upnp_ip_class(pub) == IPC_CGNAT))
      return "cgnat";
   if (wc == IPC_PRIVATE || wc == IPC_LINKLOCAL)
      return "double-nat";
   if (!strcmp(up_state, "mapped"))
   {
      if (pr_state == PR_OK && wan[0] && strcmp(pub, wan) && upnp_ip_class(pub) == IPC_PUBLIC)
         return "double-nat";      /* the internet sees another address than the router has */
      return "open";
   }
   return "lan-only";
}

static const char *pr_name(int s)
{
   static const char *const n[] = { "off", "checking", "checking", "ok", "noanswer", "nodns" };
   return s >= 0 && s <= PR_NODNS ? n[s] : "off";
}

void online_status_json(sb *b)
{
   uint64_t t = now_ms();
   unsigned i, n;
   int k;
   static rl_room_view v[MAX_VIEW];
   rl_core_stats st;
   online_init();
   EnterCriticalSection(&G.cs);
   sb_add(b, "{");
   sb_kv_bool(b, "running", G.running != 0);
   sb_kv_num(b, "port", G.cfg.port ? G.cfg.port : RL_DEFAULT_PORT);
   sb_kv_str(b, "bind", G.cfg.bind[0] ? G.cfg.bind : "0.0.0.0");
   sb_kv_num(b, "sinceS", G.running ? (double)((t - G.since_ms) / 1000) : 0);
   sb_key(b, "lan");
   lan_ips(b, G.cfg.bind[0] ? G.cfg.bind : "0.0.0.0");
   sb_key(b, "upnp");
   sb_add(b, "{");
   sb_kv_str(b, "state", G.running ? (G.up_state[0] ? G.up_state : "searching") : "");
   sb_kv_str(b, "detail", G.up_detail);
   sb_kv_str(b, "router", G.up_router);
   sb_kv_str(b, "wan", G.up_wan);
   sb_kv_str(b, "wanClass", G.up_wan[0] ? upnp_ip_class_name(upnp_ip_class(G.up_wan)) : "");
   sb_kv_str(b, "local", G.up_local);
   sb_kv_bool(b, "permanent", G.up_permanent);
   sb_add(b, "}");
   sb_key(b, "relay");
   sb_add(b, "{");
   sb_kv_str(b, "name", G.pr_name);
   sb_kv_str(b, "state", pr_name(G.pr_state));
   sb_kv_str(b, "pub", G.pr_state == PR_OK ? G.pr_pub : "");
   sb_add(b, "}");
   sb_kv_str(b, "verdict", G.running ? verdict(G.up_state, G.up_wan, G.pr_state, G.pr_pub) : "");
   /* the two codes a friend can type (GBAdhoc: Wireless > Online > Join with code) */
   sb_key(b, "codes");
   sb_add(b, "{");
   if (G.running)
   {
      char inv[16] = "", inv_lan[16] = "";
      const char *pub = upnp_ip_class(G.up_wan) == IPC_PUBLIC ? G.up_wan
                      : G.pr_state == PR_OK && upnp_ip_class(G.pr_pub) == IPC_PUBLIC ? G.pr_pub : "";
      uint32_t lan = first_lan_ip(G.cfg.bind[0] ? G.cfg.bind : "0.0.0.0");
      if (pub[0])
      {
         uint32_t ip = inet_addr(pub);
         if (inv_encode((const uint8_t *)&ip, G.cfg.port, 1, inv) != INV_OK)
            inv[0] = '\0';
      }
      if (lan && inv_encode((const uint8_t *)&lan, G.cfg.port, 1, inv_lan) != INV_OK)
         inv_lan[0] = '\0';
      sb_kv_str(b, "room", G.home);
      sb_kv_bool(b, "roomRegistered", G.dc_on && G.dc.state == DC_REGISTERED && !strcmp(G.dc.code, G.home));
      sb_key(b, "directory");
      sb_add(b, "{");
      sb_kv_str(b, "name", G.pr_name);
      sb_kv_str(b, "state", !G.dc_on ? "off" : G.dc.state == DC_REGISTERED ? "registered"
                          : G.dc.state == DC_FAILED ? "failed" : "registering");
      sb_kv_str(b, "error", G.dc_on ? G.dc.errtext : "");
      sb_kv_num(b, "tries", G.dc_on ? G.dc.tries : 0);
      sb_add(b, "}");
      sb_kv_str(b, "invite", inv);
      sb_kv_str(b, "inviteLan", inv_lan);
   }
   sb_add(b, "}");
   sb_key(b, "rooms");
   sb_add(b, "[");
   n = G.running && G.core ? relay_core_rooms(G.core, v, MAX_VIEW) : 0;
   for (i = 0; i < n && i < MAX_VIEW; i++)
   {
      if (i)
         sb_add(b, ",");
      sb_add(b, "{");
      sb_kv_str(b, "code", v[i].code);
      sb_kv_str(b, "mode", v[i].mode == RL_MODE_CABLE ? "cable" : v[i].mode == RL_MODE_ADAPTER ? "adapter" : "any");
      sb_kv_num(b, "ageS", (double)((t - v[i].created_ms) / 1000));
      sb_kv_bool(b, "joined", v[i].joined_ever);
      sb_key(b, "players");
      sb_add(b, "[");
      for (k = 0; k < v[i].nslots; k++)
      {
         const rl_slot_view *s = &v[i].s[k];
         const obs_ent *e = relay_obs_find(G.obs, &s->addr);
         char ip[24];
         int loss = relay_obs_loss_permille(e, t * 1000u);
         if (k)
            sb_add(b, ",");
         sb_add(b, "{");
         sb_kv_num(b, "slot", s->slot);
         sb_kv_str(b, "nick", e ? e->nick : "");
         sb_kv_str(b, "where", where_of(s->addr.ip_be));
         ip_shown(s->addr.ip_be, ip);
         sb_kv_str(b, "ip", ip);
         sb_kv_str(b, "nat", s->nat == RL_NAT_EIM ? "open" : s->nat == RL_NAT_SYMMETRIC ? "symmetric" : "unknown");
         sb_kv_num(b, "connS", (double)((t - s->since_ms) / 1000));
         sb_kv_num(b, "pingMs", relay_obs_ping_ms(e));
         sb_kv_num(b, "lossPct", loss < 0 ? -1 : loss / 10.0);
         sb_kv_num(b, "ppsIn", relay_obs_pps_in(e, t * 1000u));
         sb_kv_num(b, "ppsOut", relay_obs_pps_out(e, t * 1000u));
         sb_kv_str(b, "path", e && e->direct ? "direct" : "relay");
         sb_kv_str(b, "pathLabel", e && e->direct ? "Direct" : "Via host");   /* the mockup's words */
         sb_kv_str(b, "pathLabel", e && e->direct ? "Direct" : "Via host");
         sb_add(b, "}");
      }
      sb_add(b, "]}");
   }
   sb_add(b, "]");
   memset(&st, 0, sizeof(st));
   if (G.core)
      relay_core_stats(G.core, &st);
   sb_key(b, "counters");
   sb_add(b, "{");
   sb_kv_num(b, "rx", st.rx);
   sb_kv_num(b, "tx", st.tx);
   sb_kv_num(b, "fwd", st.fwd);
   sb_kv_num(b, "fwdBytes", st.fwd_bytes);
   sb_kv_num(b, "roomsMade", st.rooms_made);
   sb_kv_num(b, "joins", st.joins);
   sb_kv_num(b, "refused", st.refused);
   sb_kv_num(b, "rateDrops", st.rx_rate + st.rx_bytes_rate);
   sb_kv_num(b, "bad", st.rx_bad + st.rx_token);
   sb_kv_num(b, "cookies", st.cookies_sent);
   sb_add(b, "}");
   sb_key(b, "log");
   sb_add(b, "[");
   for (i = 0; i < G.log_n; i++)
   {
      const log_line *l = &G.log[(G.log_head + i) % LOG_N];
      if (i)
         sb_add(b, ",");
      sb_add(b, "{");
      sb_kv_str(b, "t", l->t);
      sb_kv_str(b, "tone", l->tone);
      sb_kv_str(b, "text", l->text);
      sb_add(b, "}");
   }
   sb_add(b, "]}");
   LeaveCriticalSection(&G.cs);
}

/* ---- the network check -------------------------------------------------------------- */

void online_check_json(const online_cfg *cfg, sb *b)
{
   upnp_opts o;
   upnp_igd igd;
   char err[240] = "", wan[16] = "", pub[2][16] = { "", "" };
   unsigned pubp[2] = { 0, 0 };
   const char *env = getenv("XMB_UPNP_SSDP"), *name, *up_state;
   int r, prs = PR_OFF, k;
   rl_addr ra;
   online_init();
   memset(&o, 0, sizeof(o));
   snprintf(o.ssdp, sizeof(o.ssdp), "%s", cfg && cfg->ssdp[0] ? cfg->ssdp : env ? env : "");
   r = upnp_discover(&o, &igd, err, sizeof(err));
   if (r == 0)
   {
      char e2[200];
      if (upnp_external_ip(&igd, wan, e2, sizeof(e2)) != 0)
         snprintf(err, sizeof(err), "%s", e2);
   }
   up_state = r == 0 ? (wan[0] ? "found" : "failed") : "none";
   /* the shared relay: what address and port it sees on both of its ports
    * (the same port twice = an "open" NAT that keeps one mapping per socket) */
   name = shared_name(cfg);
   if (strcmp(name, "off"))
   {
      if (resolve(name, &ra) != 0)
         prs = PR_NODNS;
      else
      {
         char lip[24];
         SOCKET s;
         snprintf(lip, sizeof(lip), "%s", "0.0.0.0");
         {
            const uint8_t *q = (const uint8_t *)&ra.ip_be;
            if (q[0] == 127)
               snprintf(lip, sizeof(lip), "127.0.0.1");   /* tests: loopback only */
         }
         s = udp_bind(lip, 0);
         prs = PR_NOANSWER;
         if (s != INVALID_SOCKET)
         {
            uint32_t nonce = (uint32_t)rnd64(NULL) | 1u;
            uint64_t t0 = now_ms();
            send_probe(s, &ra, 0, nonce);
            send_probe(s, &ra, 1, nonce);
            while (now_ms() - t0 < 2500 && (!pub[0][0] || !pub[1][0]))
            {
               uint8_t buf[600];
               fd_set f;
               struct timeval tv = { 0, 100000 };
               int n;
               FD_ZERO(&f);
               FD_SET(s, &f);
               if (select(0, &f, NULL, NULL, &tv) <= 0)
                  continue;
               n = recv(s, (char *)buf, sizeof(buf), 0);
               if (n >= RL_HDR + 11 && rl_check(buf, n) == 0 && buf[5] == RL_T_PROBED &&
                   rl_get32(buf + RL_HDR) == nonce && buf[RL_HDR + 10] < 2)
               {
                  uint32_t ip = rl_get_ip(buf + RL_HDR + 4);
                  const uint8_t *q = (const uint8_t *)&ip;
                  k = buf[RL_HDR + 10];
                  snprintf(pub[k], 16, "%u.%u.%u.%u", q[0], q[1], q[2], q[3]);
                  pubp[k] = rl_get16(buf + RL_HDR + 8);
                  prs = PR_OK;
               }
            }
            closesocket(s);
         }
      }
   }
   sb_add(b, "{");
   sb_key(b, "lan");
   lan_ips(b, cfg && cfg->bind[0] ? cfg->bind : (getenv("XMB_ONLINE_BIND") ? getenv("XMB_ONLINE_BIND") : "0.0.0.0"));
   sb_key(b, "upnp");
   sb_add(b, "{");
   sb_kv_str(b, "state", up_state);
   sb_kv_str(b, "detail", err);
   sb_kv_str(b, "router", r == 0 ? (igd.friendly[0] ? igd.friendly : igd.host) : "");
   sb_kv_str(b, "wan", wan);
   sb_kv_str(b, "wanClass", wan[0] ? upnp_ip_class_name(upnp_ip_class(wan)) : "");
   sb_kv_str(b, "local", r == 0 ? igd.local_ip : "");
   sb_add(b, "}");
   sb_key(b, "relay");
   sb_add(b, "{");
   sb_kv_str(b, "name", name);
   sb_kv_str(b, "state", pr_name(prs));
   sb_kv_str(b, "pub", pub[0][0] ? pub[0] : pub[1]);
   sb_kv_str(b, "nat", !pub[0][0] || !pub[1][0] ? "unknown" : pubp[0] == pubp[1] ? "open" : "symmetric");
   sb_add(b, "}");
   sb_kv_str(b, "verdict", verdict(r == 0 && wan[0] ? "mapped" : "none", wan, prs, pub[0][0] ? pub[0] : pub[1]));
   sb_add(b, "}");
}

/* ---- a busy port: is it a GBAdhoc relay? -------------------------------------------
 * When online_start says "busy", the Online tab asks this: send GBR1 PROBEs to the port
 * (and port + 1) on every address of this PC; a relay that answers PROBED is GBAdhoc's
 * (the standalone gbadhoc-relay, another copy of this app).  Nothing is bound on the port
 * and nothing is sent to the program that holds it but the two 27-byte PROBEs, which a
 * GBAdhoc relay answers with the same size (no amplification).  Also finds the next free
 * port pair, and for a relay found, the codes a friend types to reach it: the LAN invite,
 * and the internet invite when the router forwards the port to this PC (UPnP lookup). */

static int local_addrs(uint32_t *out, int cap)
{
   char host[256];
   struct addrinfo hints, *res = NULL, *r;
   int n = 0, k;
   out[n++] = inet_addr("127.0.0.1");
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   if (gethostname(host, sizeof(host)) == 0 && getaddrinfo(host, NULL, &hints, &res) == 0)
   {
      for (r = res; r && n < cap; r = r->ai_next)
      {
         uint32_t ip = ((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr;
         int c = upnp_ip_class(inet_ntoa(((struct sockaddr_in *)r->ai_addr)->sin_addr));
         if (c == IPC_INVALID || c == IPC_LOOPBACK)
            continue;
         for (k = 0; k < n && out[k] != ip; k++)
            ;
         if (k == n)
            out[n++] = ip;
      }
      freeaddrinfo(res);
   }
   return n;
}

/* the first port >= from (even steps) with port and port + 1 free on bind; 0 = none */
static unsigned free_pair(const char *bind, unsigned from)
{
   unsigned p;
   for (p = from; p <= 65534 && p < from + 64; p += 2)
   {
      SOCKET a = udp_bind(bind, p), b = a == INVALID_SOCKET ? INVALID_SOCKET : udp_bind(bind, p + 1);
      if (a != INVALID_SOCKET)
         closesocket(a);
      if (b != INVALID_SOCKET)
      {
         closesocket(b);
         return p;
      }
   }
   return 0;
}

void online_conflict_json(const online_cfg *cfg, sb *b)
{
   unsigned port = cfg && cfg->port ? cfg->port : RL_DEFAULT_PORT;
   const char *bind = cfg && cfg->bind[0] ? cfg->bind
                    : getenv("XMB_ONLINE_BIND") && getenv("XMB_ONLINE_BIND")[0] ? getenv("XMB_ONLINE_BIND") : "0.0.0.0";
   uint32_t addrs[16], at = 0;
   int n, i, answered[2] = { 0, 0 };
   unsigned freep;
   char where[24] = "", inv[16] = "", inv_lan[16] = "", wan[16] = "", fwd_to[16] = "", up_state[16] = "none";
   SOCKET s;
   online_init();
   n = local_addrs(addrs, 16);
   s = udp_bind(!strcmp(bind, "0.0.0.0") ? "0.0.0.0" : bind, 0);
   if (s != INVALID_SOCKET)
   {
      uint32_t nonce = (uint32_t)rnd64(NULL) | 1u;
      uint64_t t0 = now_ms();
      for (i = 0; i < n; i++)
      {
         rl_addr to;
         to.ip_be = addrs[i];
         to.port = (uint16_t)port;
         send_probe(s, &to, 0, nonce);
         send_probe(s, &to, 1, nonce);
      }
      while (now_ms() - t0 < 900 && !(answered[0] && answered[1]))
      {
         uint8_t buf[600];
         struct sockaddr_in from;
         int fl = sizeof(from), r;
         fd_set f;
         struct timeval tv = { 0, 50000 };
         FD_ZERO(&f);
         FD_SET(s, &f);
         if (select(0, &f, NULL, NULL, &tv) <= 0)
            continue;
         r = recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
         if (r >= RL_HDR + 11 && rl_check(buf, r) == 0 && buf[5] == RL_T_PROBED &&
             rl_get32(buf + RL_HDR) == nonce && buf[RL_HDR + 10] < 2)
         {
            answered[buf[RL_HDR + 10]] = 1;
            /* prefer a LAN address over loopback: that is what a PSP would use */
            if (!at || ((const uint8_t *)&at)[0] == 127)
               at = from.sin_addr.s_addr;
         }
      }
      closesocket(s);
   }
   freep = free_pair(bind, port + 2);
   sb_add(b, "{");
   sb_kv_num(b, "port", port);
   sb_kv_num(b, "freePort", freep);
   sb_kv_bool(b, "relay", answered[0] || answered[1]);
   sb_kv_bool(b, "probePort", answered[1]);
   if (answered[0] || answered[1])
   {
      uint32_t lan = at && ((const uint8_t *)&at)[0] != 127 ? at : first_lan_ip(bind);
      upnp_opts o;
      upnp_igd igd;
      char err[240];
      struct in_addr ia;
      ia.s_addr = at;
      snprintf(where, sizeof(where), "%s", inet_ntoa(ia));
      if (lan && inv_encode((const uint8_t *)&lan, port, 1, inv_lan) != INV_OK)
         inv_lan[0] = '\0';
      /* the internet: only when the router forwards the port to this PC */
      memset(&o, 0, sizeof(o));
      snprintf(o.ssdp, sizeof(o.ssdp), "%s", cfg && cfg->ssdp[0] ? cfg->ssdp : getenv("XMB_UPNP_SSDP") ? getenv("XMB_UPNP_SSDP") : "");
      if (upnp_discover(&o, &igd, err, sizeof(err)) == 0)
      {
         unsigned short in = 0;
         snprintf(up_state, sizeof(up_state), "found");
         if (upnp_external_ip(&igd, wan, err, sizeof(err)) != 0)
            wan[0] = '\0';
         if (upnp_get_mapping(&igd, (unsigned short)port, "UDP", fwd_to, &in, err, sizeof(err)) == 0)
         {
            if (!strcmp(fwd_to, igd.local_ip) && in == port)
               snprintf(up_state, sizeof(up_state), "mapped");
            else
               snprintf(up_state, sizeof(up_state), "other");
         }
         /* the public address: verified when the router maps the port here by UPnP; else it
          * still works if the port was forwarded by hand (the tab says which) */
         if (strcmp(up_state, "other") && upnp_ip_class(wan) == IPC_PUBLIC)
         {
            uint32_t ip = inet_addr(wan);
            if (inv_encode((const uint8_t *)&ip, port, 1, inv) != INV_OK)
               inv[0] = '\0';
         }
      }
   }
   sb_kv_str(b, "addr", where);
   sb_kv_str(b, "invite", inv);
   sb_kv_str(b, "inviteLan", inv_lan);
   sb_key(b, "upnp");
   sb_add(b, "{");
   sb_kv_str(b, "state", up_state);          /* none, found (no mapping), mapped (to this PC), other */
   sb_kv_str(b, "to", fwd_to);
   sb_kv_str(b, "wanClass", wan[0] ? upnp_ip_class_name(upnp_ip_class(wan)) : "");
   sb_add(b, "}");
   sb_add(b, "}");
}
