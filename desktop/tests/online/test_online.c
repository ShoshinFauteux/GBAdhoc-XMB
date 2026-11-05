/* test_online.c -- the desktop relay host (online.c) and its UPnP client
 * (upnp.c), on loopback only.  Built by test_online.sh with mingw and run on
 * Windows; every socket binds 127.0.0.1.
 *
 *   test_online upnp SSDP_PORT MODE       the UPnP client against fake_igd.py --mode MODE
 *   test_online lifecycle N PORT [SSDP]   start/stop N times: no leaked allocation, handle,
 *                                         thread or socket; the relay answers each time;
 *                                         with SSDP, the router's mappings come and go
 *   test_online loopback PORT CLIENT OUT  the relay and two simulated consoles
 *                                         (relay_client.exe): create, join, relay, then the
 *                                         direct path; the Online tab's room list during it
 *   test_online discovery PORT CLIENT OUT the room code in a directory (in-process, on
 *                                         127.0.0.1) and the invite code: one console joins
 *                                         by LOOKUP, one by the invite, no server
 */
#include "../../online.h"
#include "../../upnp.h"
#include "../../json.h"
#include "../../../shared/relay/relay_proto.h"
#include "../../../shared/relay/relay_core.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile long g_ca_live;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else if (verbose) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int verbose = 1;

static int threads_now(void)
{
   HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
   THREADENTRY32 t;
   int n = 0;
   DWORD me = GetCurrentProcessId();
   t.dwSize = sizeof(t);
   if (Thread32First(s, &t))
      do
         n += t.th32OwnerProcessID == me;
      while (Thread32Next(s, &t));
   CloseHandle(s);
   return n;
}

static DWORD handles_now(void)
{
   DWORD n = 0;
   GetProcessHandleCount(GetCurrentProcess(), &n);
   return n;
}

static jv *status(void)
{
   sb b;
   jv *v;
   sb_init(&b);
   online_status_json(&b);
   v = jv_parse(b.s);
   if (!v)
      printf("  bad status JSON: %s\n", b.s);
   sb_free(&b);
   return v;
}

static const char *ustate(jv *st)
{
   return jv_str(jv_get(st, "upnp"), "state", "");
}

/* PROBE the relay on port and port + 1: 1 = both answered PROBED */
static int relay_answers(unsigned port)
{
   SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   struct sockaddr_in a;
   int k, got = 0;
   DWORD tmo = 1000;
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_addr.s_addr = inet_addr("127.0.0.1");
   bind(s, (struct sockaddr *)&a, sizeof(a));
   setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
   for (k = 0; k < 2; k++)
   {
      uint8_t b[RL_HDR + 11], r[64];
      int n;
      memset(b, 0, sizeof(b));
      rl_hdr(b, RL_T_PROBE, RL_SLOT_NONE, RL_SLOT_NONE, 0);
      rl_put32(b + RL_HDR, 0xC0FFEE00u + (unsigned)k);
      a.sin_port = htons((u_short)(port + k));
      sendto(s, (const char *)b, sizeof(b), 0, (struct sockaddr *)&a, sizeof(a));
      n = recv(s, (char *)r, sizeof(r), 0);
      if (n >= RL_HDR + 11 && r[5] == RL_T_PROBED && rl_get32(r + RL_HDR) == 0xC0FFEE00u + (unsigned)k &&
          r[RL_HDR + 10] == k)
         got++;
   }
   closesocket(s);
   return got == 2;
}

static int port_free(unsigned port)
{
   SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   struct sockaddr_in a;
   int ok;
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_addr.s_addr = inet_addr("127.0.0.1");
   a.sin_port = htons((u_short)port);
   ok = bind(s, (struct sockaddr *)&a, sizeof(a)) == 0;
   closesocket(s);
   return ok;
}

static void cfg_for(online_cfg *c, unsigned port, const char *ssdp, const char *shared)
{
   memset(c, 0, sizeof(*c));
   c->port = port;
   snprintf(c->bind, sizeof(c->bind), "127.0.0.1");
   c->upnp = ssdp && ssdp[0];
   if (ssdp)
      snprintf(c->ssdp, sizeof(c->ssdp), "%s", ssdp);
   snprintf(c->shared, sizeof(c->shared), "%s", shared ? shared : "off");
}

static int wait_upnp(const char *want, unsigned ms)
{
   DWORD t0 = GetTickCount();
   while (GetTickCount() - t0 < ms)
   {
      jv *st = status();
      int ok = st && !strcmp(ustate(st), want);
      jv_free(st);
      if (ok)
         return 1;
      Sleep(50);
   }
   return 0;
}

/* ---- upnp ---------------------------------------------------------------- */

static int t_upnp(const char *sport, const char *mode)
{
   upnp_opts o;
   upnp_igd igd;
   char err[300], ip[16], client[16];
   unsigned short in;
   int r, code;
   memset(&o, 0, sizeof(o));
   snprintf(o.ssdp, sizeof(o.ssdp), "127.0.0.1:%s", sport);
   o.wait_ms = 1500;
   printf("== upnp client, fake IGD mode %s\n", mode);
   r = upnp_discover(&o, &igd, err, sizeof(err));
   if (!strcmp(mode, "nossdp") || !strcmp(mode, "noservice") || !strcmp(mode, "evil"))
   {
      CHECK(r == UPNP_NONE, "%s: no IGD found (%s)", mode, err);
      return 0;
   }
   CHECK(r == 0, "discovered the IGD (%s)", r ? err : igd.friendly);
   if (r)
      return 0;
   CHECK(!strcmp(igd.local_ip, "127.0.0.1"), "this PC's address on the IGD's network: %s", igd.local_ip);
   CHECK(strstr(igd.service, !strcmp(mode, "chunked") ? "WANPPPConnection:1" : "WANIPConnection:1") != NULL,
         "service %s", igd.service);
   CHECK(!strcmp(igd.control, "/ctl/IPConn"), "control URL %s", igd.control);
   r = upnp_external_ip(&igd, ip, err, sizeof(err));
   CHECK(r == 0, "external IP %s", ip);
   if (!strcmp(mode, "cgnat"))
      CHECK(upnp_ip_class(ip) == IPC_CGNAT, "100.72.1.2 classed cgnat");
   else if (!strcmp(mode, "private"))
      CHECK(upnp_ip_class(ip) == IPC_PRIVATE, "192.168.0.5 classed private");
   else
      CHECK(upnp_ip_class(ip) == IPC_PUBLIC, "%s classed public", ip);
   r = upnp_add(&igd, 37420, 37420, "UDP", 3600, "GBAdhoc XMB relay", &code, err, sizeof(err));
   if (!strcmp(mode, "refuse"))
      CHECK(r == UPNP_SOAP && code == 501, "refused with 501 (%s)", err);
   else if (!strcmp(mode, "permanent"))
   {
      CHECK(r == UPNP_SOAP && code == 725, "a lease is refused with 725");
      r = upnp_add(&igd, 37420, 37420, "UDP", 0, "GBAdhoc XMB relay", &code, err, sizeof(err));
      CHECK(r == 0, "lease 0 accepted");
   }
   else if (!strcmp(mode, "conflict") || !strcmp(mode, "ours"))
   {
      CHECK(r == UPNP_SOAP && code == 718, "718 conflict");
      r = upnp_get_mapping(&igd, 37420, "UDP", client, &in, err, sizeof(err));
      CHECK(r == 0 && !strcmp(client, !strcmp(mode, "ours") ? "127.0.0.1" : "192.168.1.99"), "holder %s", client);
      return 0;
   }
   else
      CHECK(r == 0, "mapping added (%s)", r ? err : "UDP 37420");
   if (r == 0)
   {
      r = upnp_get_mapping(&igd, 37420, "UDP", client, &in, err, sizeof(err));
      CHECK(r == 0 && !strcmp(client, "127.0.0.1") && in == 37420, "the router maps 37420 to %s:%u", client, in);
      r = upnp_delete(&igd, 37420, "UDP", &code, err, sizeof(err));
      CHECK(r == 0, "mapping deleted");
      r = upnp_get_mapping(&igd, 37420, "UDP", client, &in, err, sizeof(err));
      CHECK(r == UPNP_SOAP, "gone after delete");
   }
   /* the classifier itself */
   CHECK(upnp_ip_class("10.1.2.3") == IPC_PRIVATE && upnp_ip_class("172.31.0.1") == IPC_PRIVATE &&
         upnp_ip_class("172.32.0.1") == IPC_PUBLIC && upnp_ip_class("100.63.255.255") == IPC_PUBLIC &&
         upnp_ip_class("100.64.0.0") == IPC_CGNAT && upnp_ip_class("100.127.255.255") == IPC_CGNAT &&
         upnp_ip_class("0.0.0.0") == IPC_INVALID && upnp_ip_class("1.2.3") == IPC_INVALID &&
         upnp_ip_class("169.254.1.1") == IPC_LINKLOCAL && upnp_ip_class("8.8.8.8") == IPC_PUBLIC,
         "address classes");
   return 0;
}

/* ---- lifecycle -------------------------------------------------------------- */

static int t_lifecycle(int n, unsigned port, const char *sport)
{
   online_cfg c;
   char ssdp[64] = "", code[16], err[300];
   int i, th0, ok_answers = 0, mapped = 0, unmapped = 0;
   DWORD h0;
   long a0;
   upnp_igd igd;
   upnp_opts o;
   if (sport)
      snprintf(ssdp, sizeof(ssdp), "127.0.0.1:%s", sport);
   printf("== lifecycle: start/stop x%d on 127.0.0.1:%u%s\n", n, port, sport ? " with UPnP (fake IGD)" : "");
   online_init();
   /* warm up once with the same configuration: Winsock, the CRT and Windows'
    * thread pool (started by the first TCP connect) allocate on first use */
   if (sport)
      snprintf(ssdp, sizeof(ssdp), "127.0.0.1:%s", sport);
   cfg_for(&c, port, sport ? ssdp : NULL, NULL);
   online_start(&c, code, sizeof(code), err, sizeof(err));
   if (sport)
      wait_upnp("mapped", 8000);
   online_stop();
   Sleep(200);
   th0 = threads_now();
   h0 = handles_now();
   a0 = g_ca_live;
   memset(&o, 0, sizeof(o));
   if (sport)
   {
      snprintf(o.ssdp, sizeof(o.ssdp), "%s", ssdp);
      upnp_discover(&o, &igd, err, sizeof(err));
   }
   for (i = 0; i < n; i++)
   {
      cfg_for(&c, port, sport ? ssdp : NULL, NULL);
      if (online_start(&c, code, sizeof(code), err, sizeof(err)) != 0)
      {
         CHECK(0, "cycle %d: start failed: %s", i, err);
         break;
      }
      ok_answers += relay_answers(port);
      if (sport)
      {
         char cl[16];
         unsigned short in;
         int m = wait_upnp("mapped", 8000);
         mapped += m && upnp_get_mapping(&igd, (unsigned short)port, "UDP", cl, &in, err, sizeof(err)) == 0 &&
                   upnp_get_mapping(&igd, (unsigned short)(port + 1), "UDP", cl, &in, err, sizeof(err)) == 0;
      }
      /* a second start while running is refused, and the port stays ours */
      if (i == 0)
      {
         online_cfg c2 = c;
         CHECK(online_start(&c2, code, sizeof(code), err, sizeof(err)) != 0 && !strcmp(code, "running"),
               "a second start is refused (%s)", code);
         CHECK(!port_free(port), "the port is held while hosting");
      }
      online_stop();
      CHECK(!online_running(), "cycle %d stopped", i);
      if (sport)
      {
         char cl[16];
         unsigned short in;
         unmapped += upnp_get_mapping(&igd, (unsigned short)port, "UDP", cl, &in, err, sizeof(err)) != 0 &&
                     upnp_get_mapping(&igd, (unsigned short)(port + 1), "UDP", cl, &in, err, sizeof(err)) != 0;
      }
      if (fails > 5)
         break;
   }
   CHECK(ok_answers == n, "the relay answered PROBE on both ports in %d/%d cycles", ok_answers, n);
   if (sport)
   {
      CHECK(mapped == n, "both ports mapped on the router in %d/%d cycles", mapped, n);
      CHECK(unmapped == n, "both mappings removed on stop in %d/%d cycles", unmapped, n);
   }
   Sleep(100);
   CHECK(online_live_objects() == 0, "host objects after the last stop: %ld", online_live_objects());
   CHECK(g_ca_live == a0, "allocations: %ld before, %ld after", a0, g_ca_live);
   CHECK(handles_now() <= h0 + 2, "handles: %lu before, %lu after", (unsigned long)h0, (unsigned long)handles_now());
   CHECK(threads_now() == th0, "threads: %d before, %d after", th0, threads_now());
   CHECK(port_free(port) && port_free(port + 1), "ports %u-%u free after stop", port, port + 1);
   /* a port held by someone else: refused with "busy", nothing leaks */
   {
      SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      struct sockaddr_in a;
      memset(&a, 0, sizeof(a));
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = inet_addr("127.0.0.1");
      a.sin_port = htons((u_short)(port + 1));
      bind(s, (struct sockaddr *)&a, sizeof(a));
      cfg_for(&c, port, NULL, NULL);
      CHECK(online_start(&c, code, sizeof(code), err, sizeof(err)) != 0 && !strcmp(code, "busy"),
            "port %u taken: refused as %s", port + 1, code);
      closesocket(s);
      CHECK(online_live_objects() == 0 && g_ca_live == a0 && port_free(port), "nothing left by the refusal");
   }
   cfg_for(&c, 80, NULL, NULL);
   CHECK(online_start(&c, code, sizeof(code), err, sizeof(err)) != 0 && !strcmp(code, "bad"), "port 80 refused");
   /* status while stopped */
   {
      jv *st = status();
      CHECK(st && !jv_bool(st, "running", 1) && jv_len(jv_get(st, "rooms")) == 0, "status says not hosting");
      jv_free(st);
   }
   return 0;
}

/* ---- loopback: the relay and two simulated consoles ------------------------------ */

static HANDLE spawn(const char *cmd, const char *out)
{
   SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
   STARTUPINFOA si;
   PROCESS_INFORMATION pi;
   HANDLE f = CreateFileA(out, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, NULL);
   char c[1024];
   memset(&si, 0, sizeof(si));
   si.cb = sizeof(si);
   si.dwFlags = STARTF_USESTDHANDLES;
   si.hStdOutput = si.hStdError = f;
   si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
   snprintf(c, sizeof(c), "%s", cmd);
   if (!CreateProcessA(NULL, c, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
   {
      CloseHandle(f);
      return NULL;
   }
   CloseHandle(f);
   CloseHandle(pi.hThread);
   return pi.hProcess;
}

static void show_result(const char *file, char *line, size_t cap)
{
   FILE *f = fopen(file, "r");
   char b[600];
   line[0] = '\0';
   if (!f)
      return;
   while (fgets(b, sizeof(b), f))
      if (!strncmp(b, "RESULT", 6))
         snprintf(line, cap, "%s", b);
   fclose(f);
   if (strchr(line, '\n'))
      *strchr(line, '\n') = '\0';
}

static int run_pair(unsigned port, const char *client, const char *outdir, const char *code, int relay_only,
                    int *max_players, int *ping_ok, int *nick_ok, char res[2][600])
{
   char cmd[2][900], out[2][600];
   HANDLE p[2];
   int k, rc = 0;
   DWORD t0 = GetTickCount(), ex;
   for (k = 0; k < 2; k++)
   {
      snprintf(out[k], sizeof(out[k]), "%s/%s-%s.txt", outdir, code, k ? "join" : "host");
      snprintf(cmd[k], sizeof(cmd[k]), "\"%s\" --server 127.0.0.1:%u %s --code %s --bind 127.0.0.1:%u --payloads 400 --secs 40%s",
               client, port, k ? "--join" : "--host", code, port + 20 + k + (relay_only ? 0 : 10),
               relay_only ? " --relay-only" : "");
      p[k] = spawn(cmd[k], out[k]);
      if (!p[k])
         return -1;
      if (!k)
         Sleep(300);       /* the host first, as players do */
   }
   *max_players = 0;
   *ping_ok = 0;
   *nick_ok = 1;
   while (WaitForMultipleObjects(2, p, TRUE, 200) == WAIT_TIMEOUT && GetTickCount() - t0 < 60000)
   {
      jv *st = status(), *rooms = jv_get(st, "rooms");
      int i;
      for (i = 0; i < jv_len(rooms); i++)
      {
         jv *r = jv_at(rooms, i), *pl = jv_get(r, "players");
         if (strcmp(jv_str(r, "code", ""), code))
            continue;
         if (jv_len(pl) > *max_players)
            *max_players = jv_len(pl);
         if (jv_len(pl) == 2)
         {
            int a = (int)jv_num(jv_at(pl, 0), "pingMs", -1), b = (int)jv_num(jv_at(pl, 1), "pingMs", -1);
            if (a >= 0 && b >= 0 && a < 200 && b < 200)
               *ping_ok = 1;
            if (strcmp(jv_str(jv_at(pl, 0), "where", ""), "this PC"))
               *nick_ok = 0;
         }
      }
      jv_free(st);
   }
   for (k = 0; k < 2; k++)
   {
      if (WaitForSingleObject(p[k], 5000) == WAIT_TIMEOUT)
         TerminateProcess(p[k], 9);
      GetExitCodeProcess(p[k], &ex);
      rc |= ex != 0;
      CloseHandle(p[k]);
      show_result(out[k], res[k], 600);
   }
   return rc;
}

static int t_loopback(unsigned port, const char *client, const char *outdir)
{
   online_cfg c;
   char code[16], err[300], res[2][600], shared[40];
   int maxp, ping_ok, where_ok, rc;
   jv *st;
   double fwd0, fwd1;
   printf("== loopback: the desktop relay on 127.0.0.1:%u and two simulated consoles\n", port);
   /* the address check is pointed at the host itself: its own PROBE comes back as PROBED */
   snprintf(shared, sizeof(shared), "127.0.0.1:%u", port);
   cfg_for(&c, port, NULL, shared);
   if (online_start(&c, code, sizeof(code), err, sizeof(err)) != 0)
   {
      CHECK(0, "start: %s", err);
      return 0;
   }
   Sleep(600);
   st = status();
   CHECK(!strcmp(jv_str(jv_get(st, "relay"), "state", ""), "ok") &&
         !strcmp(jv_str(jv_get(st, "relay"), "pub", ""), "127.0.0.1"),
         "address check: PROBED came back (%s %s)", jv_str(jv_get(st, "relay"), "state", ""),
         jv_str(jv_get(st, "relay"), "pub", ""));
   CHECK(!strcmp(jv_str(st, "verdict", ""), "lan-only"), "verdict without UPnP: %s", jv_str(st, "verdict", ""));
   jv_free(st);

   /* 1. relay only: every payload crosses this PC */
   rc = run_pair(port, client, outdir, "RLAY", 1, &maxp, &ping_ok, &where_ok, res);
   printf("     host: %s\n     join: %s\n", res[0], res[1]);
   CHECK(rc == 0, "relay-only session: both consoles got all 400 payloads in order");
   CHECK(strstr(res[0], "path=relay") && strstr(res[1], "path=relay"), "both on the relay path");
   CHECK(maxp == 2, "the Online tab listed room RLAY with 2 players (max %d)", maxp);
   CHECK(ping_ok, "the Online tab showed a ping for both players");
   CHECK(where_ok, "a console on loopback is shown as \"this PC\"");
   st = status();
   fwd0 = jv_num(jv_get(st, "counters"), "fwd", 0);
   CHECK(fwd0 >= 800, "the relay forwarded %.0f datagrams", fwd0);
   CHECK(jv_num(jv_get(st, "counters"), "roomsMade", 0) == 1 && jv_num(jv_get(st, "counters"), "joins", 0) == 1,
         "one room made, one join");
   {
      jv *log = jv_get(st, "log");
      int i, created = 0, joined = 0;
      for (i = 0; i < jv_len(log); i++)
      {
         const char *t = jv_str(jv_at(log, i), "text", "");
         created |= strstr(t, "Room RLAY opened") != NULL;
         joined |= strstr(t, "joined room RLAY as player 2") != NULL;
      }
      CHECK(created && joined, "the log says the room opened and a PSP joined");
   }
   jv_free(st);

   /* 2. direct allowed: the consoles punch through and the relay only introduces them */
   rc = run_pair(port, client, outdir, "DRCT", 0, &maxp, &ping_ok, &where_ok, res);
   printf("     host: %s\n     join: %s\n", res[0], res[1]);
   CHECK(rc == 0, "direct-allowed session completes");
   CHECK(strstr(res[0], "path=direct") && strstr(res[1], "path=direct"), "both found the direct path");
   CHECK(maxp == 2, "room DRCT listed with 2 players");
   st = status();
   fwd1 = jv_num(jv_get(st, "counters"), "fwd", 0);
   CHECK(fwd1 - fwd0 < 400, "the direct session bypassed the relay (%.0f forwarded)", fwd1 - fwd0);
   CHECK(jv_num(jv_get(st, "counters"), "cookies", 0) >= 4, "every CREATE/JOIN went through the cookie challenge");
   jv_free(st);
   online_stop();
   CHECK(online_live_objects() == 0, "stopped cleanly");
   return 0;
}

/* ---- discovery: this PC's room code in a directory, and the invite code -----------
 * The directory is a relay_core with the directory on, in this process, on
 * 127.0.0.1 (the shared server's role).  The desktop host registers its room code
 * there; one console finds the room by LOOKUP of that code, the other by the LAN
 * invite code with no server at all; both play through this PC. */

static struct
{
   SOCKET s[2];
   rl_core *core;
   volatile LONG stop;
   HANDLE t;
} D;

static void d_send(void *u, int sock, const rl_addr *to, const void *buf, size_t len)
{
   struct sockaddr_in a;
   (void)u;
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_addr.s_addr = to->ip_be;
   a.sin_port = htons(to->port);
   sendto(D.s[sock], (const char *)buf, (int)len, 0, (struct sockaddr *)&a, sizeof(a));
}

static uint64_t d_rand(void *u)
{
   static uint64_t x = 0x9E3779B97F4A7C15ull;
   (void)u;
   x ^= x << 13; x ^= x >> 7; x ^= x << 17;
   return x;
}

static DWORD WINAPI d_thread(LPVOID p)
{
   uint8_t b[700];
   uint64_t last = 0;
   (void)p;
   while (!D.stop)
   {
      fd_set f;
      struct timeval tv = { 0, 20000 };
      int k;
      FD_ZERO(&f);
      FD_SET(D.s[0], &f);
      FD_SET(D.s[1], &f);
      select(0, &f, NULL, NULL, &tv);
      for (k = 0; k < 2; k++)
         for (;;)
         {
            struct sockaddr_in a;
            int al = sizeof(a), n = recvfrom(D.s[k], (char *)b, sizeof(b), 0, (struct sockaddr *)&a, &al);
            rl_addr from;
            if (n < 0)
               break;
            from.ip_be = a.sin_addr.s_addr;
            from.port = ntohs(a.sin_port);
            relay_core_input(D.core, k, &from, b, (size_t)n, GetTickCount64());
         }
      if (GetTickCount64() - last >= 100)
      {
         last = GetTickCount64();
         relay_core_tick(D.core, last);
      }
   }
   return 0;
}

static int d_start(unsigned port)
{
   rl_core_cfg cc;
   int k;
   u_long nb = 1;
   memset(&cc, 0, sizeof(cc));
   cc.send = d_send;
   cc.random64 = d_rand;
   cc.directory = 1;
   D.core = relay_core_create(&cc);
   for (k = 0; k < 2; k++)
   {
      struct sockaddr_in a;
      D.s[k] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      memset(&a, 0, sizeof(a));
      a.sin_family = AF_INET;
      a.sin_port = htons((u_short)(port + k));
      a.sin_addr.s_addr = inet_addr("127.0.0.1");
      if (bind(D.s[k], (struct sockaddr *)&a, sizeof(a)) != 0)
         return -1;
      ioctlsocket(D.s[k], FIONBIO, &nb);
   }
   D.stop = 0;
   D.t = CreateThread(NULL, 0, d_thread, NULL, 0, NULL);
   return 0;
}

static void d_stats(rl_core_stats *st)
{
   Sleep(150);                 /* let the directory thread drain */
   relay_core_stats(D.core, st);
}

static void d_stop(void)
{
   InterlockedExchange(&D.stop, 1);
   WaitForSingleObject(D.t, INFINITE);
   CloseHandle(D.t);
   closesocket(D.s[0]);
   closesocket(D.s[1]);
   relay_core_destroy(D.core);
}

/* two consoles with their own arguments; RESULT lines into res */
static int run_two(const char *client, const char *outdir, const char *tag, const char *arg[2],
                   unsigned bind0, char res[2][600])
{
   char cmd[2][900], out[2][600];
   HANDLE p[2];
   int k, rc = 0;
   DWORD ex;
   for (k = 0; k < 2; k++)
   {
      snprintf(out[k], sizeof(out[k]), "%s/%s-%s.txt", outdir, tag, k ? "join" : "host");
      snprintf(cmd[k], sizeof(cmd[k]), "\"%s\" %s %s --bind 127.0.0.1:%u --payloads 400 --secs 40",
               client, arg[k], k ? "--join" : "--host", bind0 + (unsigned)k);
      p[k] = spawn(cmd[k], out[k]);
      if (!p[k])
         return -1;
      if (!k)
         Sleep(300);
   }
   WaitForMultipleObjects(2, p, TRUE, 60000);
   for (k = 0; k < 2; k++)
   {
      if (WaitForSingleObject(p[k], 5000) == WAIT_TIMEOUT)
         TerminateProcess(p[k], 9);
      GetExitCodeProcess(p[k], &ex);
      rc |= ex != 0;
      CloseHandle(p[k]);
      show_result(out[k], res[k], 600);
   }
   return rc;
}

static int t_discovery(unsigned port, const char *client, const char *outdir)
{
   online_cfg c;
   char code[16], err[300], res[2][600], shared[40], room[8] = "", inv_lan[20] = "", want[40], a0[120], a1[120];
   const char *arg[2];
   unsigned dport = port + 50;
   int i, rc, registered = 0, room_seen = 0, label_ok = 0;
   jv *st;
   rl_core_stats ds;
   printf("== discovery: the desktop relay on 127.0.0.1:%u, a directory on 127.0.0.1:%u\n", port, dport);
   if (d_start(dport) != 0)
   {
      CHECK(0, "the test directory could not bind 127.0.0.1:%u", dport);
      return 0;
   }
   snprintf(shared, sizeof(shared), "127.0.0.1:%u", dport);
   cfg_for(&c, port, NULL, shared);
   c.directory = 1;
   if (online_start(&c, code, sizeof(code), err, sizeof(err)) != 0)
   {
      CHECK(0, "start: %s", err);
      d_stop();
      return 0;
   }
   for (i = 0; i < 50 && !registered; i++)
   {
      Sleep(100);
      st = status();
      registered = jv_bool(jv_get(st, "codes"), "roomRegistered", 0);
      if (registered)
      {
         jv *cd = jv_get(st, "codes");
         snprintf(room, sizeof(room), "%s", jv_str(cd, "room", ""));
         snprintf(inv_lan, sizeof(inv_lan), "%s", jv_str(cd, "inviteLan", ""));
         CHECK(!strcmp(jv_str(jv_get(cd, "directory"), "state", ""), "registered"), "directory state: registered");
         CHECK(!strcmp(jv_str(cd, "invite", "?"), ""), "no public invite code on loopback (no public address)");
      }
      jv_free(st);
   }
   CHECK(registered, "the room code was registered with the directory within 5 s");
   CHECK(strlen(room) == 4, "room code %s: four letters", room);
   /* 127.0.0.1 on a non-default port: the 11-character long form */
   CHECK(strlen(inv_lan) == 13, "LAN invite code %s: the long form (port %u)", inv_lan, port);
   d_stats(&ds);
   CHECK(ds.dir_now == 1, "the directory holds one entry (%u)", ds.dir_now);

   /* one console by the room code (LOOKUP at the directory), one by the invite code */
   snprintf(a0, sizeof(a0), "--server 127.0.0.1:%u --lookup --code %s", dport, room);
   snprintf(a1, sizeof(a1), "--invite %s", inv_lan);
   arg[0] = a0;
   arg[1] = a1;
   rc = run_two(client, outdir, "disc", arg, port + 60, res);
   printf("     code:   %s\n     invite: %s\n", res[0], res[1]);
   CHECK(rc == 0, "both consoles got all 400 payloads in order");
   snprintf(want, sizeof(want), "server=127.0.0.1:%u", port);
   CHECK(strstr(res[0], want) && strstr(res[0], "located=2"), "the code found this PC through the directory");
   CHECK(strstr(res[1], want), "the invite code reached this PC with no server");
   st = status();
   {
      jv *rooms = jv_get(st, "rooms");
      for (i = 0; i < jv_len(rooms); i++)
         if (!strcmp(jv_str(jv_at(rooms, i), "code", ""), room))
            room_seen = 1;
   }
   CHECK(jv_num(jv_get(st, "counters"), "roomsMade", 0) == 1, "one room was made on this PC");
   jv_free(st);
   (void)room_seen;
   (void)label_ok;
   d_stats(&ds);
   CHECK(ds.rooms_made == 0 && ds.fwd == 0, "the directory made no room and forwarded nothing (rooms %u, fwd %u)",
         ds.rooms_made, ds.fwd);
   CHECK(ds.lookups >= 1 && ds.located >= 1, "the directory answered %u lookup(s)", ds.lookups);
   printf("     directory bytes for the session: %u (UDP payload)\n", ds.dir_rx_bytes + ds.dir_tx_bytes);
   online_stop();
   d_stats(&ds);
   CHECK(ds.dir_now == 0, "stop unregistered the code at once");
   CHECK(online_live_objects() == 0, "stopped cleanly");

   /* XMB_DIRECTORY=off: nothing registered, the invite code still offered */
   _putenv("XMB_DIRECTORY=off");
   if (online_start(&c, code, sizeof(code), err, sizeof(err)) == 0)
   {
      Sleep(800);
      st = status();
      CHECK(!strcmp(jv_str(jv_get(jv_get(st, "codes"), "directory"), "state", ""), "off"), "XMB_DIRECTORY=off: no registration");
      CHECK(jv_str(jv_get(st, "codes"), "inviteLan", "")[0] != 0, "XMB_DIRECTORY=off: the invite code is still there");
      jv_free(st);
      online_stop();
      d_stats(&ds);
      CHECK(ds.dir_now == 0, "the directory holds nothing");
   }
   _putenv("XMB_DIRECTORY=");
   d_stop();
   return 0;
}

int main(int argc, char **argv)
{
   WSADATA w;
   setvbuf(stdout, NULL, _IONBF, 0);
   WSAStartup(MAKEWORD(2, 2), &w);
   _putenv("XMB_SHARED_RELAY=off");     /* never the internet from a test */
   if (argc >= 4 && !strcmp(argv[1], "upnp"))
      t_upnp(argv[2], argv[3]);
   else if (argc >= 4 && !strcmp(argv[1], "lifecycle"))
   {
      verbose = 0;
      t_lifecycle(atoi(argv[2]), (unsigned)atoi(argv[3]), argc > 4 ? argv[4] : NULL);
      verbose = 1;
      if (!fails)
         printf("  ok   all lifecycle checks\n");
   }
   else if (argc >= 5 && !strcmp(argv[1], "loopback"))
      t_loopback((unsigned)atoi(argv[2]), argv[3], argv[4]);
   else if (argc >= 5 && !strcmp(argv[1], "discovery"))
      t_discovery((unsigned)atoi(argv[2]), argv[3], argv[4]);
   else
   {
      printf("usage: see test_online.c\n");
      return 2;
   }
   printf("%s\n", fails ? "FAILED" : "PASS");
   return fails ? 1 : 0;
}
