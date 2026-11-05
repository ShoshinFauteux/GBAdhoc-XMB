/* net.c -- see net.h. */
#include "net.h"
#include "util.h"

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static HINTERNET g_session;
static volatile long long g_rate_reset;
static int g_test_offline, g_test_rate;

long long net_now(void)
{
   return (long long)time(NULL);
}

long long net_rate_reset(void)
{
   if (g_rate_reset && g_rate_reset <= net_now())
      g_rate_reset = 0;
   return g_rate_reset;
}

void net_init(void)
{
   char v[16];
   g_session = WinHttpOpen(L"GBAdhoc-XMB/0.1.0-beta.2 (+https://github.com/ShoshinFauteux/GBAdhoc)",
                           WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
   if (!g_session) /* before Windows 8.1 */
      g_session = WinHttpOpen(L"GBAdhoc-XMB/0.1.0-beta.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
   if (g_session)
   {
      DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* TLS1_3 */;
      if (!WinHttpSetOption(g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto)))
      {
         proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
         WinHttpSetOption(g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));
      }
      WinHttpSetTimeouts(g_session, 8000, 10000, 15000, 30000);
   }
   /* test hooks: XMB_NET=offline | rate */
   if (GetEnvironmentVariableA("XMB_NET", v, sizeof(v)))
   {
      g_test_offline = !strcmp(v, "offline");
      g_test_rate = !strcmp(v, "rate");
   }
}

void net_resp_free(net_resp *r)
{
   xw_buf_free(&r->body);
}

static long long hdr_num(HINTERNET req, const wchar_t *name)
{
   wchar_t buf[64];
   DWORD n = sizeof(buf);
   if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, buf, &n, WINHTTP_NO_HEADER_INDEX))
      return -1;
   return _wtoi64(buf);
}

int net_get(const char *url, const char *etag, net_resp *r)
{
   URL_COMPONENTSW uc;
   wchar_t host[256], path[2048], *wurl;
   HINTERNET con = NULL, req = NULL;
   DWORD flags = 0, st = 0, n = sizeof(st), e;
   int rc = NET_OFFLINE;

   memset(r, 0, sizeof(*r));
   r->rl_remaining = -1;
   if (g_test_offline || !g_session)
      return r->err = NET_OFFLINE;
   if (g_test_rate && strstr(url, "github"))
   {
      r->status = 403;
      r->rl_remaining = 0;
      r->rl_reset = net_now() + 40 * 60;
      g_rate_reset = r->rl_reset;
      return r->err = NET_RATE;
   }
   wurl = u_wide(url);
   memset(&uc, 0, sizeof(uc));
   uc.dwStructSize = sizeof(uc);
   uc.lpszHostName = host;
   uc.dwHostNameLength = 256;
   uc.lpszUrlPath = path;
   uc.dwUrlPathLength = 2048;
   if (!wurl || !WinHttpCrackUrl(wurl, 0, 0, &uc))
   {
      free(wurl);
      return r->err = NET_HTTP;
   }
   free(wurl);
   con = WinHttpConnect(g_session, host, uc.nPort, 0);
   if (!con)
      goto fail;
   if (uc.nScheme == INTERNET_SCHEME_HTTPS)
      flags |= WINHTTP_FLAG_SECURE;
   req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
   if (!req)
      goto fail;
   {
      wchar_t hdr[400];
      int k = swprintf(hdr, 400, L"Accept: */*\r\n");
      if (etag && etag[0])
      {
         wchar_t *we = u_wide(etag);
         swprintf(hdr + k, 400 - k, L"If-None-Match: %ls\r\n", we ? we : L"");
         free(we);
      }
      if (!WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
          !WinHttpReceiveResponse(req, NULL))
         goto fail;
   }
   WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &st, &n, WINHTTP_NO_HEADER_INDEX);
   r->status = (int)st;
   r->rl_remaining = hdr_num(req, L"X-RateLimit-Remaining");
   r->rl_reset = hdr_num(req, L"X-RateLimit-Reset");
   if (r->rl_reset < 0)
      r->rl_reset = 0;
   r->retry_after = hdr_num(req, L"Retry-After");
   if (r->retry_after < 0)
      r->retry_after = 0;
   {
      wchar_t et[128];
      DWORD el = sizeof(et);
      if (WinHttpQueryHeaders(req, WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, et, &el,
                              WINHTTP_NO_HEADER_INDEX))
      {
         char *s = u_utf8(et);
         snprintf(r->etag, sizeof(r->etag), "%s", s ? s : "");
         free(s);
      }
   }
   for (;;)
   {
      DWORD avail = 0, got = 0;
      unsigned char *nb;
      if (!WinHttpQueryDataAvailable(req, &avail))
         goto fail;
      if (!avail)
         break;
      nb = (unsigned char *)realloc(r->body.data, r->body.len + avail + 1);
      if (!nb)
         goto fail;
      r->body.data = nb;
      if (!WinHttpReadData(req, r->body.data + r->body.len, avail, &got))
         goto fail;
      r->body.len += got;
      r->body.data[r->body.len] = 0;
   }
   WinHttpCloseHandle(req);
   WinHttpCloseHandle(con);
   if ((st == 403 && r->rl_remaining == 0) || st == 429)
   {
      long long until = r->rl_reset ? r->rl_reset : net_now() + (r->retry_after ? r->retry_after : 60);
      if (strstr(url, "api.github.com"))
         g_rate_reset = until;
      r->rl_reset = until;
      u_log("net: rate-limited %s until %lld", url, until);
      return r->err = NET_RATE;
   }
   if (strstr(url, "api.github.com") && r->rl_remaining == 0 && r->rl_reset)
      g_rate_reset = r->rl_reset;
   return r->err = (st == 200 || st == 304) ? NET_OK : NET_HTTP;
fail:
   e = GetLastError();
   rc = e == ERROR_WINHTTP_TIMEOUT ? NET_TIMEOUT : NET_OFFLINE;
   u_log("net: %s failed, error %lu", url, e);
   if (req)
      WinHttpCloseHandle(req);
   if (con)
      WinHttpCloseHandle(con);
   xw_buf_free(&r->body);
   return r->err = rc;
}
