/* net.c -- see net.h.  Wi-Fi bring-up after psp/mgift_net.c (proven on
 * hardware), then a small HTTP/1.1 GET client over raw sceNetInet sockets,
 * with mbedTLS 2.28 for https://. */
#include "net.h"

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <pspwlan.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <psprtc.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/sha256.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>   /* MBEDTLS_ERR_NET_* codes only */

#include "net_ca.h"

#define NET_POOL        (128 * 1024)
#define NET_PRIO        0x2A
#define NET_STACK       4096
#define APCTL_STACK     0x1400
#define APCTL_PRIO      0x30
#define CONNECT_WAIT_US (20u * 1000000u)

#define TCP_CONNECT_MS  10000u   /* SYN to established */
#define IO_IDLE_MS      15000u   /* no byte for this long = timeout */
#define SLICE_MS        250u     /* cancel is polled this often */
#define DNS_TIMEOUT_S   5
#define DNS_RETRY       2

#define PSO_SOL_SOCKET  0xffff
#define PSO_NBIO        0x1009
#define PSO_ERROR       0x1007
#define PSO_RCVBUF      0x1002
#define PERR_EAGAIN     11
#define PERR_EINPROGRESS 119

/* ---- bring-up ------------------------------------------------------------ */

enum { ST_DOWN, ST_MOD_COMMON, ST_MOD_INET, ST_NET, ST_INET, ST_RESOLVER, ST_APCTL };
static int g_progress = ST_DOWN;

static unsigned now_ms(void)
{
   return (unsigned)(sceKernelGetSystemTimeWide() / 1000);
}

int net_wlan_switch(void)
{
   return sceWlanGetSwitchState() != 0;
}

int net_init(void)
{
   int rc;
   if (g_progress == ST_APCTL)
      return NET_OK;
   if (g_progress < ST_MOD_COMMON)
   {
      rc = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
      if (rc < 0 && rc != (int)0x80110F02)     /* already loaded */
         return NET_E_INIT;
      g_progress = ST_MOD_COMMON;
   }
   if (g_progress < ST_MOD_INET)
   {
      rc = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
      if (rc < 0 && rc != (int)0x80110F02)
         return NET_E_INIT;
      g_progress = ST_MOD_INET;
   }
   if (g_progress < ST_NET)
   {
      if (sceNetInit(NET_POOL, NET_PRIO, NET_STACK, NET_PRIO, NET_STACK) < 0)
         return NET_E_INIT;
      g_progress = ST_NET;
   }
   if (g_progress < ST_INET)
   {
      if (sceNetInetInit() < 0)
         return NET_E_INIT;
      g_progress = ST_INET;
   }
   if (g_progress < ST_RESOLVER)
   {
      if (sceNetResolverInit() < 0)
         return NET_E_INIT;
      g_progress = ST_RESOLVER;
   }
   if (g_progress < ST_APCTL)
   {
      if (sceNetApctlInit(APCTL_STACK, APCTL_PRIO) < 0)
         return NET_E_INIT;
      g_progress = ST_APCTL;
   }
   return NET_OK;
}

int net_state(void)
{
   int st = 0;
   if (g_progress < ST_APCTL || sceNetApctlGetState(&st) < 0)
      return NET_OFF;
   if (st == PSP_NET_APCTL_STATE_GOT_IP)
      return NET_ONLINE;
   return st == PSP_NET_APCTL_STATE_DISCONNECTED ? NET_OFF : NET_CONNECTING;
}

int net_connect(int config, volatile int *cancel)
{
   unsigned t0;
   int rc;
   if (!net_wlan_switch())
      return NET_E_OFFLINE;
   if ((rc = net_init()) != NET_OK)
      return rc;
   if (net_state() == NET_ONLINE)
      return NET_OK;
   if (sceNetApctlConnect(config < 1 ? 1 : config) < 0)
      return NET_E_OFFLINE;
   t0 = (unsigned)sceKernelGetSystemTimeLow();
   for (;;)
   {
      int st = 0;
      if (sceNetApctlGetState(&st) < 0)
         return NET_E_OFFLINE;
      if (st == PSP_NET_APCTL_STATE_GOT_IP)
         return NET_OK;
      if (cancel && *cancel)
      {
         sceNetApctlDisconnect();
         return NET_E_CANCEL;
      }
      if ((unsigned)sceKernelGetSystemTimeLow() - t0 > CONNECT_WAIT_US)
      {
         sceNetApctlDisconnect();
         return NET_E_TIMEOUT;
      }
      sceKernelDelayThread(50000);
   }
}

void net_info(char *ssid, size_t ssid_cap, char *ip, size_t ip_cap)
{
   union SceNetApctlInfo in;
   if (ssid_cap) ssid[0] = '\0';
   if (ip_cap) ip[0] = '\0';
   if (net_state() != NET_ONLINE)
      return;
   memset(&in, 0, sizeof(in));
   if (ssid_cap && sceNetApctlGetInfo(PSP_NET_APCTL_INFO_SSID, &in) >= 0)
      snprintf(ssid, ssid_cap, "%.*s", (int)sizeof(in.ssid), in.ssid);
   memset(&in, 0, sizeof(in));
   if (ip_cap && sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &in) >= 0)
      snprintf(ip, ip_cap, "%.*s", (int)sizeof(in.ip), in.ip);
}

void net_disconnect(void)
{
   int i;
   if (g_progress < ST_APCTL)
      return;
   sceNetApctlDisconnect();
   /* let the radio leave the AP before anything is torn down
    * (mgift_net.c: terminating under a live association wedges the next
    * bring-up) */
   for (i = 0; i < 100; i++)
   {
      int st = 0;
      if (sceNetApctlGetState(&st) < 0 || st == PSP_NET_APCTL_STATE_DISCONNECTED)
         break;
      sceKernelDelayThread(50000);
   }
}

void net_term(void)
{
   if (g_progress == ST_DOWN)
      return;
   net_disconnect();
   if (g_progress >= ST_APCTL) sceNetApctlTerm();
   if (g_progress >= ST_RESOLVER) sceNetResolverTerm();
   if (g_progress >= ST_INET) sceNetInetTerm();
   if (g_progress >= ST_NET) sceNetTerm();
   if (g_progress >= ST_MOD_INET) sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
   if (g_progress >= ST_MOD_COMMON) sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
   g_progress = ST_DOWN;
}

/* ---- URL ----------------------------------------------------------------- */

int net_urlencode_path(const char *in, char *out, size_t cap)
{
   static const char hex[] = "0123456789ABCDEF";
   size_t n = 0;
   const unsigned char *p = (const unsigned char *)in;
   for (; *p; p++)
   {
      unsigned c = *p;
      int keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                 c == '~' || c == '/';
      if (n + (keep ? 1 : 3) + 1 > cap)
      {
         if (cap) out[0] = '\0';
         return -1;
      }
      if (keep)
         out[n++] = (char)c;
      else
      {
         out[n++] = '%';
         out[n++] = hex[c >> 4];
         out[n++] = hex[c & 15];
      }
   }
   if (!cap)
      return -1;
   out[n] = '\0';
   return (int)n;
}

typedef struct
{
   int tls;
   char host[128];
   int port;
   char path[1024];
} url_t;

static int url_parse(const char *u, url_t *o)
{
   const char *h, *e, *colon;
   size_t hl;
   memset(o, 0, sizeof(*o));
   if (strncmp(u, "https://", 8) == 0) { o->tls = 1; o->port = 443; h = u + 8; }
   else if (strncmp(u, "http://", 7) == 0) { o->tls = 0; o->port = 80; h = u + 7; }
   else return -1;
   e = h;
   while (*e && *e != '/' && *e != '?' && *e != '#')
      e++;
   colon = memchr(h, ':', (size_t)(e - h));
   hl = (size_t)((colon ? colon : e) - h);
   if (!hl || hl >= sizeof(o->host))
      return -1;
   memcpy(o->host, h, hl);
   if (colon)
      o->port = atoi(colon + 1);
   if (o->port <= 0 || o->port > 65535)
      return -1;
   snprintf(o->path, sizeof(o->path), "%s", *e ? e : "/");
   if (o->path[0] != '/')
   {
      memmove(o->path + 1, o->path, strlen(o->path) + 1 < sizeof(o->path) - 1 ? strlen(o->path) + 1 : sizeof(o->path) - 2);
      o->path[0] = '/';
   }
   return strlen(*e ? e : "/") < sizeof(o->path) ? 0 : -1;
}

/* ---- sockets ------------------------------------------------------------- */

typedef struct
{
   int fd;
   volatile int *cancel;
   int why;                 /* NET_E_TIMEOUT / NET_E_CANCEL / NET_E_CONNECT */
} conn_t;

static int cancelled(conn_t *c)
{
   return c->cancel && *c->cancel;
}

/* Wait until fd is readable (want_write 0) or writable (1).  1 = ready,
 * 0 = idle for idle_ms, -1 = cancelled / select error (c->why set). */
static int wait_fd(conn_t *c, int want_write, unsigned idle_ms)
{
   unsigned t0 = now_ms();
   for (;;)
   {
      /* NOT newlib's fd_set: that is FD_SETSIZE 64 = 8 bytes, while the
       * firmware's set is 256 bits and select writes all 32 bytes back.
       * With fd_set on the stack PPSSPP's select smashed this frame
       * (first run: a NULL jump right after connect). */
      uint32_t fs[8];
      struct SceNetInetTimeval tv;
      int r;
      if (cancelled(c))
      {
         c->why = NET_E_CANCEL;
         return -1;
      }
      memset(fs, 0, sizeof(fs));
      fs[(unsigned)c->fd >> 5] |= 1u << ((unsigned)c->fd & 31);
      tv.tv_sec = 0;
      tv.tv_usec = SLICE_MS * 1000;
      r = sceNetInetSelect(c->fd + 1, want_write ? NULL : (fd_set *)fs,
                           want_write ? (fd_set *)fs : NULL, NULL, &tv);
      if (r > 0)
         return 1;
      if (r < 0)
      {
         c->why = NET_E_CONNECT;
         return -1;
      }
      if (now_ms() - t0 >= idle_ms)
      {
         c->why = NET_E_TIMEOUT;
         return 0;
      }
   }
}

/* >0 bytes, 0 = peer closed, <0 = error/timeout/cancel (c->why). */
static int sock_recv(conn_t *c, unsigned char *buf, size_t len)
{
   for (;;)
   {
      int w = wait_fd(c, 0, IO_IDLE_MS), n;
      if (w <= 0)
         return -1;
      n = (int)sceNetInetRecv(c->fd, buf, len, 0);
      if (n >= 0)
         return n;
      if (sceNetInetGetErrno() != PERR_EAGAIN)
      {
         c->why = NET_E_CONNECT;
         return -1;
      }
   }
}

static int sock_send_all(conn_t *c, const unsigned char *buf, size_t len)
{
   size_t off = 0;
   while (off < len)
   {
      int w = wait_fd(c, 1, IO_IDLE_MS), n;
      if (w <= 0)
         return -1;
      n = (int)sceNetInetSend(c->fd, buf + off, len - off, 0);
      if (n < 0)
      {
         if (sceNetInetGetErrno() == PERR_EAGAIN)
            continue;
         c->why = NET_E_CONNECT;
         return -1;
      }
      off += (size_t)n;
   }
   return 0;
}

void (*net_trace)(const char *msg);

static void tr(const char *fmt, ...)
{
   char b[200];
   va_list ap;
   if (!net_trace)
      return;
   va_start(ap, fmt);
   vsnprintf(b, sizeof(b), fmt, ap);
   va_end(ap);
   net_trace(b);
}

static int resolve(const char *host, struct in_addr *addr, char *err, size_t errcap)
{
   char buf[1024];
   int rid, rc;
   tr("dns %s", host);
   if (sceNetInetInetAton(host, addr) != 0)
      return NET_OK;
   if (sceNetResolverCreate(&rid, buf, sizeof(buf)) < 0)
   {
      snprintf(err, errcap, "Could not start the name lookup.");
      return NET_E_DNS;
   }
   rc = sceNetResolverStartNtoA(rid, host, addr, DNS_TIMEOUT_S, DNS_RETRY);
   sceNetResolverDelete(rid);
   tr("dns rc=%08X", (unsigned)rc);
   if (rc < 0)
   {
      snprintf(err, errcap, "Could not find %s (DNS error %08X).", host, (unsigned)rc);
      return NET_E_DNS;
   }
   return NET_OK;
}

static int tcp_open(conn_t *c, const url_t *u, net_resp *r)
{
   struct in_addr a;
   struct sockaddr_in sa;
   int one = 1, rc, soerr = 0;
   socklen_t sl = sizeof(soerr);
   unsigned t0 = now_ms();
   rc = resolve(u->host, &a, r->err, sizeof(r->err));
   r->ms_dns += now_ms() - t0;
   if (rc != NET_OK)
      return rc;
   c->fd = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
   if (c->fd < 0)
   {
      snprintf(r->err, sizeof(r->err), "Could not open a socket (%d).", sceNetInetGetErrno());
      return NET_E_CONNECT;
   }
   sceNetInetSetsockopt(c->fd, PSO_SOL_SOCKET, PSO_NBIO, &one, sizeof(one));
   memset(&sa, 0, sizeof(sa));
   sa.sin_len = sizeof(sa);
   sa.sin_family = AF_INET;
   sa.sin_port = htons((unsigned short)u->port);
   sa.sin_addr = a;
   { const unsigned char *ip = (const unsigned char *)&a;
     tr("connect %s -> %u.%u.%u.%u:%d fd=%d", u->host, ip[0], ip[1], ip[2], ip[3], u->port, c->fd); }
   rc = sceNetInetConnect(c->fd, (struct sockaddr *)&sa, sizeof(sa));
   tr("connect rc=%d errno=%d", rc, rc < 0 ? sceNetInetGetErrno() : 0);
   if (rc < 0)
   {
      int e = sceNetInetGetErrno();
      if (e != PERR_EINPROGRESS && e != PERR_EAGAIN && e != 0)
      {
         snprintf(r->err, sizeof(r->err), "Could not connect to %s (errno %d).", u->host, e);
         return NET_E_CONNECT;
      }
      rc = wait_fd(c, 1, TCP_CONNECT_MS);
      tr("connect wait=%d why=%d", rc, c->why);
      if (rc <= 0)
      {
         if (c->why == NET_E_CANCEL)
            return NET_E_CANCEL;
         snprintf(r->err, sizeof(r->err), "%s did not answer.", u->host);
         return c->why == NET_E_TIMEOUT ? NET_E_TIMEOUT : NET_E_CONNECT;
      }
      if (sceNetInetGetsockopt(c->fd, PSO_SOL_SOCKET, PSO_ERROR, &soerr, &sl) >= 0 && soerr)
      {
         snprintf(r->err, sizeof(r->err), "Could not connect to %s (error %d).", u->host, soerr);
         return NET_E_CONNECT;
      }
   }
   return NET_OK;
}

/* ---- TLS ----------------------------------------------------------------- */

static mbedtls_x509_crt g_ca;
static int g_ca_state;              /* 0 not parsed, 1 ok, -1 failed */
static SceUID g_ca_sema = -1;

/* Entropy for the DRBG.  The PSP has no user-mode hardware RNG; this mixes
 * the 1 MHz system clock sampled across scheduler yields (jitter), the RTC
 * tick, the Wi-Fi MAC and a counter through SHA-256.  Good enough for what is
 * at stake here -- downloading public images, no secrets are sent -- and it
 * never repeats between boots because the RTC tick does not. */
static int psp_entropy(void *ctx, unsigned char *out, size_t len)
{
   static unsigned counter;
   unsigned char pool[128], h[32];
   size_t done = 0;
   (void)ctx;
   while (done < len)
   {
      int i;
      u64 tick = 0;
      unsigned char mac[8] = { 0 };
      memset(pool, 0, sizeof(pool));
      for (i = 0; i < 16; i++)
      {
         u64 t = sceKernelGetSystemTimeWide();
         memcpy(pool + i * 4, &t, 4);
         sceKernelDelayThread(1 + (i & 3));
      }
      sceRtcGetCurrentTick(&tick);
      memcpy(pool + 64, &tick, 8);
      sceWlanGetEtherAddr(mac);
      memcpy(pool + 72, mac, 6);
      counter++;
      memcpy(pool + 80, &counter, 4);
      memcpy(pool + 84, &out, sizeof(out));
      mbedtls_sha256_ret(pool, sizeof(pool), h, 0);
      for (i = 0; i < 32 && done < len; i++)
         out[done++] = h[i];
   }
   return 0;
}

static int ca_ready(void)
{
   if (g_ca_sema < 0)
      g_ca_sema = sceKernelCreateSema("xmbnet_ca", 0, 1, 1, NULL);
   sceKernelWaitSema(g_ca_sema, 1, NULL);
   if (g_ca_state == 0)
   {
      mbedtls_x509_crt_init(&g_ca);
      g_ca_state = mbedtls_x509_crt_parse(&g_ca, (const unsigned char *)net_ca_pem,
                                          sizeof(net_ca_pem)) == 0 ? 1 : -1;
   }
   sceKernelSignalSema(g_ca_sema, 1);
   return g_ca_state == 1;
}

typedef struct
{
   int clock_unset;
} vrfy_ctx;

static int x509_cmp_now(const mbedtls_x509_time *t, const ScePspDateTime *n)
{
   long a[6] = { t->year, t->mon, t->day, t->hour, t->min, t->sec };
   long b[6] = { n->year, n->month, n->day, n->hour, n->minute, n->second };
   int i;
   for (i = 0; i < 6; i++)
      if (a[i] != b[i])
         return a[i] < b[i] ? -1 : 1;
   return 0;
}

/* mbedTLS judged the dates with time(), which on hardware is ~uptime.
 * Redo that one check with the real clock. */
static int verify_cb(void *p, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
   vrfy_ctx *v = (vrfy_ctx *)p;
   ScePspDateTime now;
   (void)depth;
   *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
   memset(&now, 0, sizeof(now));
   if (sceRtcGetCurrentClock(&now, 0) < 0 || now.year < 2025)
   {
      v->clock_unset = 1;    /* the chain must still verify; only dates are skipped */
      return 0;
   }
   if (x509_cmp_now(&crt->valid_to, &now) < 0)
      *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
   if (x509_cmp_now(&crt->valid_from, &now) > 0)
      *flags |= MBEDTLS_X509_BADCERT_FUTURE;
   return 0;
}

static int bio_send(void *p, const unsigned char *buf, size_t len)
{
   conn_t *c = (conn_t *)p;
   int w = wait_fd(c, 1, IO_IDLE_MS), n;
   if (w <= 0)
      return MBEDTLS_ERR_SSL_TIMEOUT;
   n = (int)sceNetInetSend(c->fd, buf, len, 0);
   if (n < 0)
   {
      if (sceNetInetGetErrno() == PERR_EAGAIN)
         return MBEDTLS_ERR_SSL_WANT_WRITE;
      c->why = NET_E_CONNECT;
      return MBEDTLS_ERR_NET_SEND_FAILED;
   }
   return n;
}

static int bio_recv(void *p, unsigned char *buf, size_t len)
{
   conn_t *c = (conn_t *)p;
   int n = sock_recv(c, buf, len);
   if (n < 0)
      return c->why == NET_E_CONNECT ? MBEDTLS_ERR_NET_RECV_FAILED : MBEDTLS_ERR_SSL_TIMEOUT;
   return n;                 /* 0 = EOF */
}

/* RSA certificates FIRST, by offering only RSA suites on the first try.
 * Verifying RSA is a cheap public-exponent operation; ECDSA verify is a full
 * scalar multiplication.  api.github.com serves an ECDSA chain (P-256 leaf,
 * P-384 intermediates) to a client that offers ECDSA, and its RSA chain to
 * one that does not: measured in PPSSPP 638 ms vs ~200 ms of handshake.
 * A server with only an ECDSA certificate refuses the first try; the
 * second offers everything.  ChaCha20-Poly1305 before AES-GCM: 3.7x faster
 * in software here (64 KB: 7.9 ms vs 29.5 ms). */
static const int g_suites_rsa[] = {
   MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
   MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
   MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
   0
};
static const int g_suites_all[] = {
   MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
   MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
   MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
   MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
   MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
   MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
   0
};

/* Session resumption: a batch fetches dozens of files from one host, each
 * on its own connection (Connection: close keeps the client simple).  A
 * resumed handshake skips every public-key operation.  One slot per host,
 * for the few hosts this app talks to. */
#define SESS_SLOTS 4
static struct { char host[64]; int used; mbedtls_ssl_session s; unsigned age; } g_sess[SESS_SLOTS];
static unsigned g_sess_clock;
static SceUID g_sess_sema = -1;

static void sess_lock(int on)
{
   if (g_sess_sema < 0)
      g_sess_sema = sceKernelCreateSema("xmbnet_sess", 0, 1, 1, NULL);
   if (on) sceKernelWaitSema(g_sess_sema, 1, NULL);
   else sceKernelSignalSema(g_sess_sema, 1);
}
static int sess_find(const char *host)
{
   int i;
   for (i = 0; i < SESS_SLOTS; i++)
      if (g_sess[i].used && strcmp(g_sess[i].host, host) == 0)
         return i;
   return -1;
}
static void sess_load(mbedtls_ssl_context *ssl, const char *host)
{
   int i;
   sess_lock(1);
   if ((i = sess_find(host)) >= 0)
      mbedtls_ssl_set_session(ssl, &g_sess[i].s);
   sess_lock(0);
}
static void sess_save(mbedtls_ssl_context *ssl, const char *host)
{
   int i, old = 0;
   sess_lock(1);
   if ((i = sess_find(host)) < 0)
   {
      for (i = 0; i < SESS_SLOTS; i++)
      {
         if (!g_sess[i].used)
            break;
         if (g_sess[i].age < g_sess[old].age)
            old = i;
      }
      if (i == SESS_SLOTS)
         i = old;
   }
   if (g_sess[i].used)
      mbedtls_ssl_session_free(&g_sess[i].s);
   mbedtls_ssl_session_init(&g_sess[i].s);
   if (mbedtls_ssl_get_session(ssl, &g_sess[i].s) == 0)
   {
      snprintf(g_sess[i].host, sizeof(g_sess[i].host), "%s", host);
      g_sess[i].used = 1;
      g_sess[i].age = ++g_sess_clock;
   }
   else
   {
      mbedtls_ssl_session_free(&g_sess[i].s);
      g_sess[i].used = 0;
   }
   sess_lock(0);
}
static void sess_drop(const char *host)
{
   int i;
   sess_lock(1);
   if ((i = sess_find(host)) >= 0)
   {
      mbedtls_ssl_session_free(&g_sess[i].s);
      g_sess[i].used = 0;
   }
   sess_lock(0);
}
static const mbedtls_ecp_group_id g_curves[] = {
   MBEDTLS_ECP_DP_CURVE25519, MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_SECP384R1,
   MBEDTLS_ECP_DP_NONE
};

typedef struct
{
   int on;
   mbedtls_ssl_context ssl;
   mbedtls_ssl_config conf;
   mbedtls_ctr_drbg_context drbg;
   vrfy_ctx v;
} tls_t;

static void tls_free(tls_t *t)
{
   if (!t->on)
      return;
   mbedtls_ssl_free(&t->ssl);
   mbedtls_ssl_config_free(&t->conf);
   mbedtls_ctr_drbg_free(&t->drbg);
   t->on = 0;
}

static int tls_open(tls_t *t, conn_t *c, const char *host, int rsa_only, net_resp *r)
{
   int rc;
   long heap0 = (long)mallinfo().uordblks;
   unsigned t0 = now_ms();
   if (!ca_ready())
   {
      snprintf(r->err, sizeof(r->err), "The built-in certificates could not be read.");
      return NET_E_TLS;
   }
   memset(t, 0, sizeof(*t));
   t->on = 1;
   mbedtls_ssl_init(&t->ssl);
   mbedtls_ssl_config_init(&t->conf);
   mbedtls_ctr_drbg_init(&t->drbg);
   if (mbedtls_ctr_drbg_seed(&t->drbg, psp_entropy, NULL,
                             (const unsigned char *)"GBAdhoc-XMB", 11) != 0 ||
       mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM,
                                   MBEDTLS_SSL_PRESET_DEFAULT) != 0)
   {
      snprintf(r->err, sizeof(r->err), "Could not set up TLS.");
      return NET_E_TLS;
   }
   mbedtls_ssl_conf_min_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                MBEDTLS_SSL_MINOR_VERSION_3);     /* TLS 1.2 */
   mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
   mbedtls_ssl_conf_ca_chain(&t->conf, &g_ca, NULL);
   mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
   mbedtls_ssl_conf_verify(&t->conf, verify_cb, &t->v);
   mbedtls_ssl_conf_ciphersuites(&t->conf, rsa_only ? g_suites_rsa : g_suites_all);
   mbedtls_ssl_conf_curves(&t->conf, g_curves);
   if (mbedtls_ssl_setup(&t->ssl, &t->conf) != 0 ||
       mbedtls_ssl_set_hostname(&t->ssl, host) != 0)
   {
      snprintf(r->err, sizeof(r->err), "Could not set up TLS.");
      return NET_E_TLS;
   }
   mbedtls_ssl_set_bio(&t->ssl, c, bio_send, bio_recv, NULL);
   sess_load(&t->ssl, host);
   tr("tls handshake %s%s", host, rsa_only ? " (rsa suites)" : " (all suites)");
   while ((rc = mbedtls_ssl_handshake(&t->ssl)) != 0)
   {
      if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
         continue;
      if (c->why == NET_E_CANCEL)
         return NET_E_CANCEL;
      if (rc == MBEDTLS_ERR_SSL_TIMEOUT)
      {
         snprintf(r->err, sizeof(r->err), "%s stopped answering during the secure handshake.", host);
         return NET_E_TIMEOUT;
      }
      if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
      {
         char vi[96];
         uint32_t fl = mbedtls_ssl_get_verify_result(&t->ssl);
         mbedtls_x509_crt_verify_info(vi, sizeof(vi), "", fl);
         {
            char *nl = strchr(vi, '\n');
            if (nl) *nl = '\0';
         }
         snprintf(r->err, sizeof(r->err), "%s's certificate was refused: %s%s", host, vi,
                  (fl & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE))
                     ? " (check the PSP's date and time)" : "");
      }
      else
      {
         char e[80];
         mbedtls_strerror(rc, e, sizeof(e));
         snprintf(r->err, sizeof(r->err), "Secure connection to %s failed (-0x%04X %s).",
                  host, (unsigned)-rc, e);
      }
      sess_drop(host);
      tr("tls fail -0x%04X", (unsigned)-rc);
      /* refused before any certificate was judged: retryable with ECDSA */
      return (rsa_only && rc != MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) ? NET_E_TLS - 100 : NET_E_TLS;
   }
   r->ms_tls += now_ms() - t0;
   tr("tls ok %s %s %u ms", mbedtls_ssl_get_version(&t->ssl), mbedtls_ssl_get_ciphersuite(&t->ssl), now_ms() - t0);
   sess_save(&t->ssl, host);
   r->heap_tls = (long)mallinfo().uordblks - heap0;
   if (t->v.clock_unset)
      snprintf(r->err, sizeof(r->err), "note: the PSP clock is not set, certificate dates were not checked");
   return NET_OK;
}

/* ---- one HTTP exchange --------------------------------------------------- */

typedef struct
{
   conn_t c;
   tls_t t;
   int tls;
   unsigned char buf[4096];
   size_t have, pos;
} stream_t;

/* Refill: >0 bytes now buffered, 0 EOF, <0 error (NET_E_*). */
static int st_fill(stream_t *s)
{
   int n;
   s->pos = 0;
   s->have = 0;
   if (s->tls)
   {
      for (;;)
      {
         n = mbedtls_ssl_read(&s->t.ssl, s->buf, sizeof(s->buf));
         if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
         break;
      }
      if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0)
         return 0;
      if (n < 0)
         return s->c.why == NET_E_CANCEL ? NET_E_CANCEL
              : n == MBEDTLS_ERR_SSL_TIMEOUT ? NET_E_TIMEOUT
              : s->c.why == NET_E_CONNECT ? 0 /* reset after the body: EOF */ : NET_E_TLS;
   }
   else
   {
      n = sock_recv(&s->c, s->buf, sizeof(s->buf));
      if (n < 0)
         return s->c.why == NET_E_CANCEL ? NET_E_CANCEL
              : s->c.why == NET_E_TIMEOUT ? NET_E_TIMEOUT : 0;
   }
   s->have = (size_t)n;
   return n;
}

static int st_write(stream_t *s, const char *p, size_t len)
{
   if (!s->tls)
      return sock_send_all(&s->c, (const unsigned char *)p, len) == 0 ? 0 : -1;
   while (len)
   {
      int n = mbedtls_ssl_write(&s->t.ssl, (const unsigned char *)p, len);
      if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
         continue;
      if (n < 0)
         return -1;
      p += n;
      len -= (size_t)n;
   }
   return 0;
}

/* One line (CRLF stripped) into out.  0 ok, <0 error. */
static int st_line(stream_t *s, char *out, size_t cap)
{
   size_t n = 0;
   for (;;)
   {
      unsigned char ch;
      if (s->pos >= s->have)
      {
         int f = st_fill(s);
         if (f <= 0)
            return f < 0 ? f : NET_E_PROTO;
      }
      ch = s->buf[s->pos++];
      if (ch == '\n')
         break;
      if (n + 1 < cap)
         out[n++] = (char)ch;
   }
   if (n && out[n - 1] == '\r')
      n--;
   out[n] = '\0';
   return 0;
}

/* Up to len body bytes: >0 copied, 0 EOF, <0 error. */
static int st_read(stream_t *s, unsigned char *dst, size_t len)
{
   size_t k;
   if (s->pos >= s->have)
   {
      int f = st_fill(s);
      if (f <= 0)
         return f;
   }
   k = s->have - s->pos;
   if (k > len)
      k = len;
   memcpy(dst, s->buf + s->pos, k);
   s->pos += k;
   return (int)k;
}

static void st_close(stream_t *s)
{
   if (s->tls && s->t.on)
      mbedtls_ssl_close_notify(&s->t.ssl);
   tls_free(&s->t);
   if (s->c.fd >= 0)
      sceNetInetClose(s->c.fd);
   s->c.fd = -1;
}

static int hdr_is(const char *line, const char *name)
{
   size_t n = strlen(name);
   return strncasecmp(line, name, n) == 0 && line[n] == ':';
}
static const char *hdr_val(const char *line)
{
   const char *v = strchr(line, ':');
   v = v ? v + 1 : line;
   while (*v == ' ' || *v == '\t')
      v++;
   return v;
}

/* Grow r->data to hold `need` bytes plus a NUL. */
static int body_reserve(net_resp *r, size_t *cap, size_t need)
{
   unsigned char *n;
   size_t nc;
   if (need + 1 <= *cap)
      return 0;
   nc = *cap ? *cap : 16384;
   while (nc < need + 1)
      nc *= 2;
   n = (unsigned char *)realloc(r->data, nc);
   if (!n)
      return -1;
   r->data = n;
   *cap = nc;
   return 0;
}

static int http_once(const url_t *u, net_resp *r, size_t max_bytes, volatile int *cancel,
                     void (*progress)(void *, size_t, size_t), void *ctx,
                     char *location, size_t loc_cap)
{
   stream_t *s;
   char line[1024], req[1400];
   int rc, chunked = 0, have_len = 0, n;
   size_t clen = 0, cap = 0;

   s = (stream_t *)calloc(1, sizeof(*s));
   if (!s)
      return NET_E_NOMEM;
   s->c.fd = -1;
   s->c.cancel = cancel;
   s->tls = u->tls;
   location[0] = '\0';
   rc = tcp_open(&s->c, u, r);
   if (rc == NET_OK && u->tls)
   {
      rc = tls_open(&s->t, &s->c, u->host, 1, r);
      if (rc == NET_E_TLS - 100)
      {
         /* the server would not take an RSA suite: again, offering ECDSA */
         st_close(s);
         s->c.why = 0;
         rc = tcp_open(&s->c, u, r);
         if (rc == NET_OK)
            rc = tls_open(&s->t, &s->c, u->host, 0, r);
      }
   }
   if (rc != NET_OK)
      goto out;

   n = snprintf(req, sizeof(req),
                "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: GBAdhoc-XMB/0.1.0-beta.1\r\n"
                "Accept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                u->path, u->host);
   if (n <= 0 || (size_t)n >= sizeof(req) || st_write(s, req, (size_t)n) != 0)
   {
      rc = s->c.why == NET_E_CANCEL ? NET_E_CANCEL : NET_E_CONNECT;
      snprintf(r->err, sizeof(r->err), "Could not send the request to %s.", u->host);
      goto out;
   }
   /* status line */
   if ((rc = st_line(s, line, sizeof(line))) != 0 || strncmp(line, "HTTP/1.", 7) != 0)
   {
      if (rc == 0 || rc == NET_E_PROTO)
         snprintf(r->err, sizeof(r->err), "%s sent no HTTP answer.", u->host);
      rc = rc ? rc : NET_E_PROTO;
      goto out;
   }
   r->status = atoi(line + 9);
   tr("status %d", r->status);
   /* headers */
   for (;;)
   {
      if ((rc = st_line(s, line, sizeof(line))) != 0)
         goto out;
      if (!line[0])
         break;
      if (hdr_is(line, "Content-Length"))
      {
         clen = (size_t)strtoul(hdr_val(line), NULL, 10);
         have_len = 1;
      }
      else if (hdr_is(line, "Transfer-Encoding") && strstr(hdr_val(line), "chunked"))
         chunked = 1;
      else if (hdr_is(line, "Location"))
         snprintf(location, loc_cap, "%s", hdr_val(line));
      else if (hdr_is(line, "X-RateLimit-Remaining"))
         r->rl_remaining = atoi(hdr_val(line));
      else if (hdr_is(line, "X-RateLimit-Reset"))
         r->rl_reset = atol(hdr_val(line));
   }
   if (chunked)
      have_len = 0;
   if (have_len && clen > max_bytes)
   {
      snprintf(r->err, sizeof(r->err), "The file is too large (%u KB).", (unsigned)(clen / 1024));
      rc = NET_E_TOOBIG;
      goto out;
   }
   if (have_len)
   {
      /* exact: doubling would hold 2 MB for a 1.1 MB hero master */
      free(r->data);
      r->data = (unsigned char *)malloc(clen + 1);
      cap = r->data ? clen + 1 : 0;
   }
   if (have_len && !r->data)
   {
      rc = NET_E_NOMEM;
      goto out;
   }
   /* body */
   for (;;)
   {
      size_t want;
      if (chunked)
      {
         size_t chunk;
         if ((rc = st_line(s, line, sizeof(line))) != 0)
            goto out;
         chunk = (size_t)strtoul(line, NULL, 16);
         if (!chunk)
            break;
         if (r->len + chunk > max_bytes)
         {
            snprintf(r->err, sizeof(r->err), "The file is too large.");
            rc = NET_E_TOOBIG;
            goto out;
         }
         if (body_reserve(r, &cap, r->len + chunk) != 0)
         {
            rc = NET_E_NOMEM;
            goto out;
         }
         while (chunk)
         {
            n = st_read(s, r->data + r->len, chunk);
            if (n <= 0)
            {
               rc = n < 0 ? n : NET_E_PROTO;
               goto out;
            }
            r->len += (size_t)n;
            chunk -= (size_t)n;
            if (progress)
               progress(ctx, r->len, 0);
         }
         if ((rc = st_line(s, line, sizeof(line))) != 0)   /* CRLF after chunk */
            goto out;
         continue;
      }
      if (have_len && r->len >= clen)
         break;
      want = have_len ? clen - r->len : 16384;
      if (!have_len)
      {
         if (r->len + want > max_bytes)
            want = max_bytes - r->len;
         if (!want)
         {
            unsigned char probe;
            if (st_read(s, &probe, 1) > 0)
            {
               snprintf(r->err, sizeof(r->err), "The file is too large.");
               rc = NET_E_TOOBIG;
               goto out;
            }
            break;
         }
         if (body_reserve(r, &cap, r->len + want) != 0)
         {
            rc = NET_E_NOMEM;
            goto out;
         }
      }
      n = st_read(s, r->data + r->len, want);
      if (n < 0)
      {
         rc = n;
         if (rc == NET_E_TIMEOUT)
            snprintf(r->err, sizeof(r->err), "%s stopped sending.", u->host);
         goto out;
      }
      if (n == 0)
      {
         if (have_len)
         {
            snprintf(r->err, sizeof(r->err), "The download from %s was cut short.", u->host);
            rc = NET_E_PROTO;
            goto out;
         }
         break;
      }
      r->len += (size_t)n;
      if (progress)
         progress(ctx, r->len, have_len ? clen : 0);
   }
   if (r->data)
      r->data[r->len] = '\0';
   rc = NET_OK;
   if (r->status == 403 || r->status == 429)
   {
      if (r->rl_reset)
         snprintf(r->err, sizeof(r->err), "Rate-limited by %s (HTTP %d). Resets at %ld.",
                  u->host, r->status, r->rl_reset);
      else
         snprintf(r->err, sizeof(r->err), "Rate-limited by %s (HTTP %d).", u->host, r->status);
   }
out:
   if (rc == NET_E_CANCEL)
      snprintf(r->err, sizeof(r->err), "Stopped.");
   st_close(s);
   free(s);
   return rc;
}

int net_get(const char *url, net_resp *r, size_t max_bytes, volatile int *cancel,
            void (*progress)(void *ctx, size_t got, size_t total), void *ctx)
{
   url_t *u;
   char loc[1024];
   int hop, rc = NET_E_PROTO;
   unsigned t0 = now_ms();
   memset(r, 0, sizeof(*r));
   r->rl_remaining = -1;
   if (net_state() != NET_ONLINE)
   {
      snprintf(r->err, sizeof(r->err), "No connection. Connect to a wireless network and try again.");
      return NET_E_OFFLINE;
   }
   u = (url_t *)malloc(sizeof(*u));
   if (!u)
      return NET_E_NOMEM;
   if (url_parse(url, u) != 0)
   {
      snprintf(r->err, sizeof(r->err), "Bad address: %.120s", url);
      free(u);
      return NET_E_PROTO;
   }
   for (hop = 0; hop < 4; hop++)
   {
      rc = http_once(u, r, max_bytes, cancel, progress, ctx, loc, sizeof(loc));
      if (rc != NET_OK || !(r->status == 301 || r->status == 302 || r->status == 303 ||
                            r->status == 307 || r->status == 308) || !loc[0])
         break;
      if (hop == 3)
      {
         snprintf(r->err, sizeof(r->err), "Too many redirects.");
         rc = NET_E_PROTO;
         break;
      }
      /* follow: absolute, or a path on the same host */
      free(r->data);
      r->data = NULL;
      r->len = 0;
      r->status = 0;
      if (loc[0] == '/')
         snprintf(u->path, sizeof(u->path), "%s", loc);
      else if (url_parse(loc, u) != 0)
      {
         snprintf(r->err, sizeof(r->err), "Bad redirect.");
         rc = NET_E_PROTO;
         break;
      }
   }
   free(u);
   r->ms_total = now_ms() - t0;
   if (rc != NET_OK)
   {
      free(r->data);
      r->data = NULL;
      r->len = 0;
   }
   return rc;
}

void net_resp_free(net_resp *r)
{
   if (!r)
      return;
   free(r->data);
   r->data = NULL;
   r->len = 0;
}
