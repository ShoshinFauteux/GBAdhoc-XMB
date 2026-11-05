/* upnp.c -- the UPnP-IGD client of the Online tab.  See upnp.h. */
#include "upnp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_MAX (128 * 1024)

static void seterr(char *err, size_t cap, const char *fmt, const char *a)
{
   if (err && cap)
      snprintf(err, cap, fmt, a ? a : "");
}

/* ---- addresses --------------------------------------------------------- */

int upnp_ip_class(const char *ip)
{
   unsigned a, b, c, d;
   char tail;
   if (!ip || sscanf(ip, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 ||
       c > 255 || d > 255)
      return IPC_INVALID;
   if (a == 0 || a >= 224)
      return IPC_INVALID;
   if (a == 127)
      return IPC_LOOPBACK;
   if (a == 10 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) ||
       (a == 192 && b == 0 && c == 0) || (a == 198 && (b == 18 || b == 19)))
      return IPC_PRIVATE;
   if (a == 100 && b >= 64 && b < 128)
      return IPC_CGNAT;
   if (a == 169 && b == 254)
      return IPC_LINKLOCAL;
   return IPC_PUBLIC;
}

const char *upnp_ip_class_name(int c)
{
   switch (c)
   {
   case IPC_PUBLIC: return "public";
   case IPC_PRIVATE: return "private";
   case IPC_CGNAT: return "cgnat";
   case IPC_LOOPBACK: return "loopback";
   case IPC_LINKLOCAL: return "linklocal";
   default: return "invalid";
   }
}

/* http://a.b.c.d[:port]/path, host private or loopback only */
static int parse_url(const char *url, char host[16], unsigned short *port, char *path, size_t pcap)
{
   const char *p, *slash, *colon;
   size_t hl;
   int cls;
   if (strncmp(url, "http://", 7))
      return -1;
   p = url + 7;
   slash = strchr(p, '/');
   if (!slash)
      slash = p + strlen(p);
   colon = memchr(p, ':', (size_t)(slash - p));
   hl = (size_t)((colon ? colon : slash) - p);
   if (!hl || hl > 15)
      return -1;
   memcpy(host, p, hl);
   host[hl] = '\0';
   cls = upnp_ip_class(host);
   if (cls != IPC_PRIVATE && cls != IPC_LOOPBACK)
      return -1;
   *port = 80;
   if (colon)
   {
      long v = strtol(colon + 1, NULL, 10);
      if (v <= 0 || v > 65535)
         return -1;
      *port = (unsigned short)v;
   }
   snprintf(path, pcap, "%s", *slash ? slash : "/");
   return 0;
}

/* ---- HTTP ----------------------------------------------------------------- */

static int wait_sock(SOCKET s, int write, unsigned ms)
{
   fd_set f;
   struct timeval tv;
   FD_ZERO(&f);
   FD_SET(s, &f);
   tv.tv_sec = (long)(ms / 1000);
   tv.tv_usec = (long)(ms % 1000) * 1000;
   return select(0, write ? NULL : &f, write ? &f : NULL, NULL, &tv) > 0;
}

static char *find_ci(const char *h, const char *needle)
{
   size_t n = strlen(needle);
   for (; *h; h++)
      if (!_strnicmp(h, needle, n))
         return (char *)h;
   return NULL;
}

/* chunked transfer coding, in place; returns the new length */
static size_t dechunk(char *b, size_t n)
{
   size_t r = 0, w = 0;
   while (r < n)
   {
      char *end;
      unsigned long len = strtoul(b + r, &end, 16);
      char *nl = strstr(b + r, "\r\n");
      if (!nl || end == b + r)
         break;
      r = (size_t)(nl - b) + 2;
      if (len == 0 || r + len > n)
         break;
      memmove(b + w, b + r, len);
      w += len;
      r += len + 2;
   }
   b[w] = '\0';
   return w;
}

/* One request; *body malloc'd (NUL-terminated).  Returns the HTTP status or
 * -1 on a network error / timeout. */
static int http_req(const char *host, unsigned short port, const char *method, const char *path,
                    const char *extra_hdr, const char *body, unsigned ms, char **out,
                    char local_ip[16])
{
   SOCKET s;
   struct sockaddr_in a;
   u_long nb = 1;
   char *req, *buf = NULL;
   size_t n = 0, rl, bl = body ? strlen(body) : 0;
   int status = -1, r;
   DWORD t0 = GetTickCount();
   *out = NULL;
   memset(&a, 0, sizeof(a));
   a.sin_family = AF_INET;
   a.sin_port = htons(port);
   a.sin_addr.s_addr = inet_addr(host);
   s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (s == INVALID_SOCKET)
      return -1;
   ioctlsocket(s, FIONBIO, &nb);
   connect(s, (struct sockaddr *)&a, sizeof(a));
   if (!wait_sock(s, 1, ms))
      goto done;
   {
      int e = 0, el = sizeof(e);
      getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &el);
      if (e)
         goto done;
   }
   if (local_ip)
   {
      struct sockaddr_in l;
      int ll = sizeof(l);
      local_ip[0] = '\0';
      if (getsockname(s, (struct sockaddr *)&l, &ll) == 0)
         snprintf(local_ip, 16, "%s", inet_ntoa(l.sin_addr));
   }
   rl = strlen(path) + strlen(host) + (extra_hdr ? strlen(extra_hdr) : 0) + bl + 256;
   req = (char *)malloc(rl);
   if (!req)
      goto done;
   r = snprintf(req, rl,
                "%s %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\nUser-Agent: GBAdhoc-XMB UPnP/1.1\r\n"
                "Content-Length: %u\r\n%s\r\n%s",
                method, path, host, port, (unsigned)bl, extra_hdr ? extra_hdr : "", body ? body : "");
   {
      int sent = 0;
      while (sent < r)
      {
         int k;
         if (!wait_sock(s, 1, ms))
            break;
         k = send(s, req + sent, r - sent, 0);
         if (k <= 0)
            break;
         sent += k;
      }
      free(req);
      if (sent < r)
         goto done;
   }
   buf = (char *)malloc(HTTP_MAX + 1);
   if (!buf)
      goto done;
   for (;;)
   {
      int k;
      DWORD el = GetTickCount() - t0;
      if (el >= ms * 2 || !wait_sock(s, 0, ms * 2 - el))
         break;
      k = recv(s, buf + n, (int)(HTTP_MAX - n), 0);
      if (k <= 0)
         break;
      n += (size_t)k;
      if (n >= HTTP_MAX)
         break;
   }
   buf[n] = '\0';
   if (n > 12 && !strncmp(buf, "HTTP/1.", 7))
   {
      char *hdr_end = strstr(buf, "\r\n\r\n");
      status = atoi(buf + 9);
      if (hdr_end)
      {
         size_t off = (size_t)(hdr_end + 4 - buf), bn = n - off;
         char *te, *ch;
         int chunked;
         *hdr_end = '\0';
         /* read the header before the body is moved over it */
         te = find_ci(buf, "\r\ntransfer-encoding:");
         ch = te ? find_ci(te, "chunked") : NULL;
         chunked = ch && ch < te + 40;
         memmove(buf, hdr_end + 4, bn);
         buf[bn] = '\0';
         if (chunked)
            bn = dechunk(buf, bn);
         (void)bn;
         *out = buf;
         buf = NULL;
      }
      else
         status = -1;
   }
done:
   free(buf);
   closesocket(s);
   return status;
}

/* ---- XML (just enough) ------------------------------------------------------ */

/* the text of the first <[prefix:]tag> in [p, end), copied to out */
static const char *xml_text(const char *p, const char *end, const char *tag, char *out, size_t cap)
{
   size_t tl = strlen(tag);
   out[0] = '\0';
   for (; p && p < end && (p = strchr(p, '<')) != NULL && p < end; p++)
   {
      const char *q = p + 1, *name = q, *gt, *close;
      size_t n;
      if (*q == '/' || *q == '?' || *q == '!')
         continue;
      while (*q && *q != '>' && *q != ' ' && *q != '/')
      {
         if (*q == ':')
            name = q + 1;
         q++;
      }
      if ((size_t)(q - name) != tl || strncmp(name, tag, tl))
         continue;
      gt = strchr(q, '>');
      if (!gt || gt >= end)
         return NULL;
      if (gt[-1] == '/')
         return gt + 1;            /* <tag/> */
      close = strstr(gt + 1, "</");
      if (!close || close > end)
         return NULL;
      n = (size_t)(close - gt - 1);
      if (n >= cap)
         n = cap - 1;
      memcpy(out, gt + 1, n);
      out[n] = '\0';
      /* trim */
      while (n && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == '\t'))
         out[--n] = '\0';
      {
         size_t lead = strspn(out, " \r\n\t");
         if (lead)
            memmove(out, out + lead, n - lead + 1);
      }
      return close;
   }
   return NULL;
}

static const char *const WAN_SERVICES[] = {
   "urn:schemas-upnp-org:service:WANIPConnection:1",
   "urn:schemas-upnp-org:service:WANIPConnection:2",
   "urn:schemas-upnp-org:service:WANPPPConnection:1",
};

/* the WAN connection service of a description; 0 = found */
static int parse_desc(const char *xml, const char *loc_host, unsigned short loc_port, upnp_igd *igd)
{
   const char *end = xml + strlen(xml), *p;
   char base[256], st[128], ctl[512];
   size_t i;
   xml_text(xml, end, "friendlyName", igd->friendly, sizeof(igd->friendly));
   xml_text(xml, end, "URLBase", base, sizeof(base));
   for (i = 0; i < sizeof(WAN_SERVICES) / sizeof(WAN_SERVICES[0]); i++)
      for (p = xml; (p = strstr(p, "<service>")) != NULL; p++)
      {
         const char *se = strstr(p, "</service>");
         if (!se)
            break;
         xml_text(p, se, "serviceType", st, sizeof(st));
         if (strcmp(st, WAN_SERVICES[i]))
            continue;
         xml_text(p, se, "controlURL", ctl, sizeof(ctl));
         if (!ctl[0])
            continue;
         snprintf(igd->service, sizeof(igd->service), "%s", st);
         if (!strncmp(ctl, "http://", 7))
         {
            if (parse_url(ctl, igd->host, &igd->port, igd->control, sizeof(igd->control)))
               return -1;
         }
         else
         {
            if (base[0])
            {
               char path[512];
               if (parse_url(base, igd->host, &igd->port, path, sizeof(path)))
                  return -1;
            }
            else
            {
               snprintf(igd->host, sizeof(igd->host), "%s", loc_host);
               igd->port = loc_port;
            }
            snprintf(igd->control, sizeof(igd->control), "%s%s", ctl[0] == '/' ? "" : "/", ctl);
         }
         return 0;
      }
   return -1;
}

/* ---- discovery --------------------------------------------------------------- */

int upnp_discover(const upnp_opts *o, upnp_igd *igd, char *err, size_t cap)
{
   static const char *const sts[] = {
      "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
      "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
      "urn:schemas-upnp-org:service:WANIPConnection:1",
   };
   char target[64], locs[8][300];
   int nloc = 0, i;
   unsigned wait = o && o->wait_ms ? o->wait_ms : 2500;
   unsigned hms = o && o->http_ms ? o->http_ms : 3000;
   struct sockaddr_in to, me;
   SOCKET s;
   DWORD t0;
   char *colon;
   memset(igd, 0, sizeof(*igd));
   snprintf(target, sizeof(target), "%s", o && o->ssdp[0] ? o->ssdp : "239.255.255.250:1900");
   colon = strchr(target, ':');
   memset(&to, 0, sizeof(to));
   to.sin_family = AF_INET;
   to.sin_port = htons(colon ? (unsigned short)atoi(colon + 1) : 1900);
   if (colon)
      *colon = '\0';
   to.sin_addr.s_addr = inet_addr(target);
   s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s == INVALID_SOCKET)
   {
      seterr(err, cap, "no UDP socket", NULL);
      return UPNP_NONE;
   }
   memset(&me, 0, sizeof(me));
   me.sin_family = AF_INET;
   /* a loopback target (the tests' fake IGD) is answered on loopback only */
   me.sin_addr.s_addr = upnp_ip_class(target) == IPC_LOOPBACK ? inet_addr("127.0.0.1") : INADDR_ANY;
   if (bind(s, (struct sockaddr *)&me, sizeof(me)) != 0)
   {
      closesocket(s);
      seterr(err, cap, "no UDP socket", NULL);
      return UPNP_NONE;
   }
   {
      DWORD ttl = 2;
      setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl));
   }
   for (i = 0; i < 3; i++)
   {
      char m[400];
      int n = snprintf(m, sizeof(m),
                       "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                       "MX: 2\r\nST: %s\r\n\r\n", sts[i]);
      sendto(s, m, n, 0, (struct sockaddr *)&to, sizeof(to));
   }
   t0 = GetTickCount();
   while (nloc < 8)
   {
      char b[1500], *l, *e;
      int n, k, dup = 0;
      DWORD el = GetTickCount() - t0;
      if (el >= wait || !wait_sock(s, 0, wait - el))
         break;
      n = recv(s, b, sizeof(b) - 1, 0);
      if (n <= 0)
         continue;
      b[n] = '\0';
      if (strncmp(b, "HTTP/1.1 200", 12) && strncmp(b, "HTTP/1.0 200", 12))
         continue;
      l = find_ci(b, "\nlocation:");
      if (!l)
         continue;
      l += 10;
      while (*l == ' ')
         l++;
      e = strpbrk(l, "\r\n");
      if (e)
         *e = '\0';
      for (k = 0; k < nloc; k++)
         dup |= !strcmp(locs[k], l);
      if (!dup && strlen(l) < sizeof(locs[0]))
         snprintf(locs[nloc++], sizeof(locs[0]), "%s", l);
      /* the first answer is usually the router: give stragglers 300 ms */
      if (nloc == 1 && wait - (GetTickCount() - t0) > 300)
         wait = (GetTickCount() - t0) + 300;
   }
   closesocket(s);
   if (!nloc)
   {
      seterr(err, cap, "No UPnP router answered on this network.", NULL);
      return UPNP_NONE;
   }
   for (i = 0; i < nloc; i++)
   {
      char host[16], path[300], *xml = NULL, lip[16];
      unsigned short port;
      int st;
      if (parse_url(locs[i], host, &port, path, sizeof(path)))
         continue;
      st = http_req(host, port, "GET", path, NULL, NULL, hms, &xml, lip);
      if (st == 200 && xml && parse_desc(xml, host, port, igd) == 0)
      {
         snprintf(igd->local_ip, sizeof(igd->local_ip), "%s", lip);
         igd->http_ms = hms;
         free(xml);
         return UPNP_OK;
      }
      free(xml);
   }
   memset(igd, 0, sizeof(*igd));
   seterr(err, cap, "A UPnP device answered, but it offers no internet connection service "
                    "(it is not the router, or its UPnP is limited).", NULL);
   return UPNP_NONE;
}

/* ---- SOAP --------------------------------------------------------------------- */

static int soap(const upnp_igd *igd, const char *action, const char *args, char **resp, int *code,
                char *err, size_t cap)
{
   char hdr[300], *body, *xml = NULL;
   size_t bl = strlen(args) + 600;
   int st;
   if (code)
      *code = 0;
   *resp = NULL;
   body = (char *)malloc(bl);
   if (!body)
      return UPNP_HTTP;
   snprintf(body, bl,
            "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s>"
            "</s:Body></s:Envelope>\r\n",
            action, igd->service, args, action);
   snprintf(hdr, sizeof(hdr), "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\n",
            igd->service, action);
   st = http_req(igd->host, igd->port, "POST", igd->control, hdr, body, igd->http_ms ? igd->http_ms : 3000,
                 &xml, NULL);
   free(body);
   if (st < 0)
   {
      free(xml);
      seterr(err, cap, "The router did not answer (%s).", action);
      return UPNP_HTTP;
   }
   if (st != 200)
   {
      char c[16], d[128];
      if (xml)
      {
         xml_text(xml, xml + strlen(xml), "errorCode", c, sizeof(c));
         xml_text(xml, xml + strlen(xml), "errorDescription", d, sizeof(d));
      }
      else
         c[0] = d[0] = '\0';
      if (code)
         *code = atoi(c);
      if (err && cap)
         snprintf(err, cap, "The router refused %s (UPnP error %s%s%s).", action, c[0] ? c : "?",
                  d[0] ? ": " : "", d);
      free(xml);
      return UPNP_SOAP;
   }
   *resp = xml;
   return UPNP_OK;
}

int upnp_external_ip(const upnp_igd *igd, char ip[16], char *err, size_t cap)
{
   char *x;
   int r = soap(igd, "GetExternalIPAddress", "", &x, NULL, err, cap);
   ip[0] = '\0';
   if (r)
      return r;
   xml_text(x, x + strlen(x), "NewExternalIPAddress", ip, 16);
   free(x);
   if (!ip[0])
   {
      seterr(err, cap, "The router did not say its internet address.", NULL);
      return UPNP_SOAP;
   }
   return UPNP_OK;
}

static int safe_text(const char *s)
{
   for (; *s; s++)
      if (*s == '<' || *s == '>' || *s == '&')
         return 0;
   return 1;
}

int upnp_add(const upnp_igd *igd, unsigned short ext, unsigned short in, const char *proto,
             unsigned lease_s, const char *desc, int *soap_code, char *err, size_t cap)
{
   char a[700], *x = NULL;
   int r;
   if (!safe_text(desc) || !safe_text(proto))
      return UPNP_SOAP;
   snprintf(a, sizeof(a),
            "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>"
            "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient>"
            "<NewEnabled>1</NewEnabled><NewPortMappingDescription>%s</NewPortMappingDescription>"
            "<NewLeaseDuration>%u</NewLeaseDuration>",
            ext, proto, in, igd->local_ip, desc, lease_s);
   r = soap(igd, "AddPortMapping", a, &x, soap_code, err, cap);
   free(x);
   return r;
}

int upnp_delete(const upnp_igd *igd, unsigned short ext, const char *proto, int *soap_code,
                char *err, size_t cap)
{
   char a[300], *x = NULL;
   int r;
   snprintf(a, sizeof(a),
            "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>",
            ext, proto);
   r = soap(igd, "DeletePortMapping", a, &x, soap_code, err, cap);
   free(x);
   return r;
}

int upnp_get_mapping(const upnp_igd *igd, unsigned short ext, const char *proto, char client[16],
                     unsigned short *in_port, char *err, size_t cap)
{
   char a[300], *x = NULL, p[8];
   int r;
   snprintf(a, sizeof(a),
            "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>%s</NewProtocol>",
            ext, proto);
   client[0] = '\0';
   *in_port = 0;
   r = soap(igd, "GetSpecificPortMappingEntry", a, &x, NULL, err, cap);
   if (r)
      return r;
   xml_text(x, x + strlen(x), "NewInternalClient", client, 16);
   xml_text(x, x + strlen(x), "NewInternalPort", p, sizeof(p));
   *in_port = (unsigned short)atoi(p);
   free(x);
   return UPNP_OK;
}
