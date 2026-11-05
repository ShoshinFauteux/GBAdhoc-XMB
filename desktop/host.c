/* host.c -- GBAdhoc XMB for Windows: one window holding the app's own
 * HTML/CSS/JS in WebView2, and the native side it talks to (the PSP over
 * USB, the network, the entry writer xw_core).
 *
 * The UI is served from inside the exe at https://xmb.gbadhoc/ (WebView2's
 * WebResourceRequested), so it is the same origin as the art it composes on
 * a canvas.  The UI sends {id, cmd, args} as web messages; the host answers
 * {id, ok, result|error, code}, sends {id, progress} while it works, and
 * {event, data} when something changes (a PSP plugged in or pulled).
 *
 *   GBAdhoc-XMB.exe [--fake-psp DIR[,ef0][,go]]... [--ui-dir DIR] [--dev]
 *                   [--open QUERY] [--script FILE.js] [--shot OUT.png]
 *                   [--wait MS] [--result FILE]
 */
#include "wv2.h"
#include "json.h"
#include "util.h"
#include "net.h"
#include "psp.h"
#include "art.h"

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XMB_VERSION "0.1.0-beta.2"
#define HOST L"https://xmb.gbadhoc/"

typedef struct
{
   const char *path, *mime;
   const unsigned char *data;
   unsigned len;
} ui_res;
extern const ui_res g_ui[];
extern const int g_nui;

enum { WM_APP_JSON = WM_APP + 1, WM_APP_SHOT };
enum { T_DEVICES = 1, T_POLL, T_SHOT };

static HWND g_hwnd;
static ICoreWebView2Environment *g_env;
static ICoreWebView2Controller *g_ctrl;
static ICoreWebView2 *g_wv;
static char g_ui_dir[1024], g_open[1024], g_script[1024], g_shot[1024], g_result[1024];
static int g_dev, g_wait = 3000, g_ready;
static volatile LONG g_writing;
static sb g_last_devices;
static wchar_t g_runtime[64];

/* ---- posting JSON to the page ------------------------------------------- */

static void post_json(const char *utf8)
{
   char *copy = _strdup(utf8);
   if (copy && !PostMessageW(g_hwnd, WM_APP_JSON, 0, (LPARAM)copy))
      free(copy);
}

static void reply(double id, int ok, const char *result_json, const char *code, const char *err)
{
   sb b;
   sb_init(&b);
   sb_add(&b, "{");
   sb_kv_num(&b, "id", id);
   sb_kv_bool(&b, "ok", ok);
   if (ok)
   {
      sb_key(&b, "result");
      sb_add(&b, result_json && result_json[0] ? result_json : "null");
   }
   else
   {
      sb_kv_str(&b, "code", code ? code : "error");
      sb_kv_str(&b, "error", err ? err : "Something went wrong.");
   }
   sb_add(&b, "}");
   post_json(b.s);
   sb_free(&b);
}

typedef struct
{
   double id;
} prog_ctx;

static void progress(void *ctx, const char *obj)
{
   sb b;
   sb_init(&b);
   sb_add(&b, "{");
   sb_kv_num(&b, "id", ((prog_ctx *)ctx)->id);
   sb_key(&b, "progress");
   sb_add(&b, obj);
   sb_add(&b, "}");
   post_json(b.s);
   sb_free(&b);
}

/* ---- the store (prefs and the resumable queue) -------------------------- */

static CRITICAL_SECTION g_store_cs;

static void store_path(char *out, size_t cap)
{
   u_appdata("", out, cap);
   strncat(out, "/prefs.json", cap - strlen(out) - 1);
}

static char *store_read(void)
{
   char p[1100];
   xw_buf b;
   char *s;
   store_path(p, sizeof(p));
   if (xw_read_file(p, &b) != 0)
      return _strdup("{}");
   s = (char *)realloc(b.data, b.len + 1);
   if (!s)
      return _strdup("{}");
   s[b.len] = '\0';
   {
      jv *v = jv_parse(s);
      int ok = v && v->t == JV_OBJ;
      jv_free(v);
      if (!ok)
      {
         free(s);
         return _strdup("{}");
      }
   }
   return s;
}

static void store_set(const char *key, const jv *value)
{
   char *s, p[1100];
   jv *o, *m, **link;
   sb b;
   EnterCriticalSection(&g_store_cs);
   s = store_read();
   o = jv_parse(s);
   free(s);
   if (!o)
      o = jv_parse("{}");
   for (link = &o->kid; *link; link = &(*link)->next)
      if ((*link)->key && !strcmp((*link)->key, key))
      {
         m = *link;
         *link = m->next;
         m->next = NULL;
         jv_free(m);
         break;
      }
   sb_init(&b);
   sb_json(&b, o);
   /* add the new member at the end */
   if (value)
   {
      sb t;
      sb_init(&t);
      sb_addn(&t, b.s, b.n - 1);           /* drop '}' */
      if (o->kid)
         sb_add(&t, ",");
      sb_jstr(&t, key);
      sb_add(&t, ":");
      sb_json(&t, value);
      sb_add(&t, "}");
      sb_free(&b);
      b = t;
   }
   store_path(p, sizeof(p));
   xw_write_file(p, b.s, b.n);
   sb_free(&b);
   jv_free(o);
   LeaveCriticalSection(&g_store_cs);
}

/* ---- commands that work in the background ------------------------------- */

typedef struct
{
   double id;
   char *cmd;
   jv *args;
} job;

static DWORD WINAPI job_thread(LPVOID p)
{
   job *j = (job *)p;
   prog_ctx pc = { j->id };
   sb out;
   char code[32] = "", err[1600] = "";
   int ok = 1;
   CoInitializeEx(NULL, COINIT_MULTITHREADED);
   sb_init(&out);
   sb_add(&out, "");
   if (!strcmp(j->cmd, "scan"))
      ok = psp_scan(j->args, progress, &pc, &out, err, sizeof(err)) == 0;
   else if (!strcmp(j->cmd, "fetch"))
      ok = art_fetch(j->args, progress, &pc, &out) == 0;
   else if (!strcmp(j->cmd, "make"))
   {
      InterlockedIncrement(&g_writing);
      ok = psp_make(j->args, progress, &pc, &out, code, err, sizeof(err)) == 0;
      InterlockedDecrement(&g_writing);
   }
   else if (!strcmp(j->cmd, "remove"))
   {
      InterlockedIncrement(&g_writing);
      ok = psp_remove(j->args, &out, code, err, sizeof(err)) == 0;
      InterlockedDecrement(&g_writing);
   }
   else if (!strcmp(j->cmd, "pictures"))
      psp_pictures(j->args, &out);
   else if (!strcmp(j->cmd, "dirs"))
      psp_dirs(j->args, &out);
   else if (!strcmp(j->cmd, "netStatus"))
      art_net_status(&out);
   else if (!strcmp(j->cmd, "cacheStats"))
      art_cache_stats(&out);
   else if (!strcmp(j->cmd, "cacheClear"))
   {
      art_cache_clear();
      art_cache_stats(&out);
   }
   else
   {
      ok = 0;
      strcpy(code, "unknown");
      snprintf(err, sizeof(err), "Unknown command %s", j->cmd);
   }
   reply(j->id, ok, out.s, code[0] ? code : "error", err);
   sb_free(&out);
   free(j->cmd);
   jv_free(j->args);
   free(j);
   CoUninitialize();
   return 0;
}

static void start_job(double id, const char *cmd, const jv *args)
{
   job *j = (job *)calloc(1, sizeof(job));
   sb b;
   HANDLE t;
   if (!j)
      return;
   j->id = id;
   j->cmd = _strdup(cmd);
   sb_init(&b);
   sb_json(&b, args);
   j->args = jv_parse(b.s);
   sb_free(&b);
   t = CreateThread(NULL, 0, job_thread, j, 0, NULL);
   if (t)
      CloseHandle(t);
}

/* ---- commands answered at once ------------------------------------------ */

static int system_dark(void)
{
   DWORD v = 1, n = sizeof(v);
   RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &n);
   return v == 0;
}

static void dark_titlebar(int dark)
{
   BOOL d = dark ? TRUE : FALSE;
   if (FAILED(DwmSetWindowAttribute(g_hwnd, 20, &d, sizeof(d))))
      DwmSetWindowAttribute(g_hwnd, 19, &d, sizeof(d));
   /* repaint the frame */
   SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static void pick_image(double id)
{
   OPENFILENAMEW of;
   wchar_t file[1024] = L"";
   memset(&of, 0, sizeof(of));
   of.lStructSize = sizeof(of);
   of.hwndOwner = g_hwnd;
   of.lpstrFilter = L"Pictures (PNG, JPEG, BMP)\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
   of.lpstrFile = file;
   of.nMaxFile = 1024;
   of.lpstrTitle = L"Choose a picture";
   of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
   if (GetOpenFileNameW(&of))
   {
      char *p = u_utf8(file), enc[2400], url[2600];
      sb b;
      u_slashes(p);
      psp_allow_path(p);
      u_urlenc(p, enc, sizeof(enc), 0);
      snprintf(url, sizeof(url), "https://xmb.gbadhoc/fs?p=%s", enc);
      sb_init(&b);
      sb_add(&b, "{");
      sb_kv_str(&b, "path", p);
      sb_kv_str(&b, "name", u_base(p));
      sb_kv_str(&b, "url", url);
      sb_add(&b, "}");
      reply(id, 1, b.s, NULL, NULL);
      sb_free(&b);
      free(p);
   }
   else
      reply(id, 1, "null", NULL, NULL);
}

static void test_result(const jv *args)
{
   sb b;
   sb_init(&b);
   sb_json(&b, args);
   if (g_result[0])
      xw_write_file(g_result, b.s, b.n);
   sb_free(&b);
   PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}

static void on_message(const char *text)
{
   jv *m = jv_parse(text);
   double id;
   const char *cmd;
   const jv *args;
   if (!m)
      return;
   id = jv_num(m, "id", 0);
   cmd = jv_str(m, "cmd", "");
   args = jv_get(m, "args");
   if (!strcmp(cmd, "hello"))
   {
      sb b;
      char *store = store_read(), ad[1024], *rt = u_utf8(g_runtime);
      u_appdata("", ad, sizeof(ad));
      sb_init(&b);
      sb_add(&b, "{");
      sb_kv_str(&b, "version", XMB_VERSION);
      sb_kv_str(&b, "runtime", rt);
      sb_kv_bool(&b, "fake", psp_fake_mode());
      sb_kv_bool(&b, "dark", system_dark());
      sb_kv_str(&b, "appdata", ad);
      sb_kv_bool(&b, "dev", g_dev);
      sb_key(&b, "store");
      sb_add(&b, store);
      sb_add(&b, "}");
      reply(id, 1, b.s, NULL, NULL);
      sb_free(&b);
      free(store);
      free(rt);
      g_ready = 1;
   }
   else if (!strcmp(cmd, "devices"))
   {
      sb b;
      sb_init(&b);
      psp_devices_json(&b);
      reply(id, 1, b.s, NULL, NULL);
      sb_free(&g_last_devices);
      g_last_devices = b;
   }
   else if (!strcmp(cmd, "store"))
   {
      store_set(jv_str(args, "key", "misc"), jv_get(args, "value"));
      reply(id, 1, "true", NULL, NULL);
   }
   else if (!strcmp(cmd, "pickImage"))
      pick_image(id);
   else if (!strcmp(cmd, "title"))
   {
      wchar_t *w = u_wide(jv_str(args, "text", "GBAdhoc XMB"));
      SetWindowTextW(g_hwnd, w);
      free(w);
      reply(id, 1, "true", NULL, NULL);
   }
   else if (!strcmp(cmd, "theme"))
   {
      dark_titlebar(jv_bool(args, "dark", 1));
      reply(id, 1, "true", NULL, NULL);
   }
   else if (!strcmp(cmd, "openUrl"))
   {
      const char *u = jv_str(args, "url", "");
      if (u_istarts(u, "https://github.com/") || u_istarts(u, "https://go.microsoft.com/") ||
          u_istarts(u, "https://thumbnails.libretro.com/"))
      {
         wchar_t *w = u_wide(u);
         ShellExecuteW(g_hwnd, L"open", w, NULL, NULL, SW_SHOWNORMAL);
         free(w);
      }
      reply(id, 1, "true", NULL, NULL);
   }
   else if (!strcmp(cmd, "testResult"))
   {
      reply(id, 1, "true", NULL, NULL);
      test_result(args);
   }
   else if (!strcmp(cmd, "log"))
   {
      u_log("ui: %s", jv_str(args, "msg", ""));
      reply(id, 1, "true", NULL, NULL);
   }
   else
      start_job(id, cmd, args ? args : NULL);
   jv_free(m);
}

/* ---- serving the UI and files -------------------------------------------- */

static const char *mime_of(const char *path)
{
   const char *e = u_ext(path);
   if (u_ieq(e, "html")) return "text/html; charset=utf-8";
   if (u_ieq(e, "css")) return "text/css; charset=utf-8";
   if (u_ieq(e, "js")) return "text/javascript; charset=utf-8";
   if (u_ieq(e, "png")) return "image/png";
   if (u_ieq(e, "jpg") || u_ieq(e, "jpeg")) return "image/jpeg";
   if (u_ieq(e, "bmp")) return "image/bmp";
   if (u_ieq(e, "svg")) return "image/svg+xml";
   if (u_ieq(e, "woff2")) return "font/woff2";
   if (u_ieq(e, "json")) return "application/json";
   return "application/octet-stream";
}

static void respond(ICoreWebView2WebResourceRequestedEventArgs *args, int status,
                    const char *mime, const void *data, size_t len)
{
   IStream *st = SHCreateMemStream((const BYTE *)data, (UINT)len);
   ICoreWebView2WebResourceResponse *resp = NULL;
   wchar_t hdr[256];
   swprintf(hdr, 256, L"Content-Type: %hs\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: https://xmb.gbadhoc",
            mime);
   if (SUCCEEDED(ICoreWebView2Environment_CreateWebResourceResponse(
          g_env, st, status, status == 200 ? L"OK" : L"Not Found", hdr, &resp)))
   {
      ICoreWebView2WebResourceRequestedEventArgs_put_Response(args, resp);
      ICoreWebView2WebResourceResponse_Release(resp);
   }
   if (st)
      IStream_Release(st);
}

static const char *query_param(const char *q, const char *name, char *out, size_t cap)
{
   size_t nl = strlen(name);
   const char *p = q;
   while (p && *p)
   {
      if (!strncmp(p, name, nl) && p[nl] == '=')
      {
         const char *v = p + nl + 1, *e = strchr(v, '&');
         size_t n = e ? (size_t)(e - v) : strlen(v);
         if (n >= cap)
            n = cap - 1;
         memcpy(out, v, n);
         out[n] = '\0';
         u_urldec(out);
         return out;
      }
      p = strchr(p, '&');
      if (p)
         p++;
   }
   out[0] = '\0';
   return NULL;
}

static void serve(const char *url, ICoreWebView2WebResourceRequestedEventArgs *args)
{
   char path[2400], *q;
   int i;
   snprintf(path, sizeof(path), "%s", url + strlen("https://xmb.gbadhoc/"));
   q = strchr(path, '?');
   if (q)
      *q++ = '\0';
   {
      char *h = strchr(path, '#');
      if (h)
         *h = '\0';
   }
   if (!path[0])
      snprintf(path, sizeof(path), "desktop.html");
   if (!strcmp(path, "fs") || !strcmp(path, "pbp"))
   {
      char p[2400], s[8];
      xw_buf b;
      query_param(q ? q : "", "p", p, sizeof(p));
      u_slashes(p);
      if (!p[0] || !psp_path_allowed(p))
      {
         respond(args, 404, "text/plain", "no", 2);
         return;
      }
      if (!strcmp(path, "pbp"))
      {
         query_param(q ? q : "", "s", s, sizeof(s));
         if (xw_pbp_section_file(p, atoi(s), &b) == 0 && b.len)
         {
            respond(args, 200, "image/png", b.data, b.len);
            xw_buf_free(&b);
            return;
         }
      }
      else if (xw_read_file(p, &b) == 0)
      {
         respond(args, 200, mime_of(p), b.data, b.len);
         xw_buf_free(&b);
         return;
      }
      respond(args, 404, "text/plain", "missing", 7);
      return;
   }
   if (g_ui_dir[0])
   {
      char full[2400];
      xw_buf b;
      u_join(full, sizeof(full), g_ui_dir, path);
      if (!strstr(path, "..") && xw_read_file(full, &b) == 0)
      {
         respond(args, 200, mime_of(full), b.data, b.len);
         xw_buf_free(&b);
         return;
      }
   }
   for (i = 0; i < g_nui; i++)
      if (!strcmp(g_ui[i].path, path))
      {
         respond(args, 200, g_ui[i].mime, g_ui[i].data, g_ui[i].len);
         return;
      }
   respond(args, 404, "text/plain", "not found", 9);
}

/* ---- WebView2 handlers (static COM objects: they live as long as we do) -- */

static HRESULT STDMETHODCALLTYPE h_qi(void *t, REFIID r, void **o)
{
   (void)r;
   *o = t;
   return S_OK;
}
static ULONG STDMETHODCALLTYPE h_ref(void *t)
{
   (void)t;
   return 1;
}
#define HANDLER(Type, inv)                                                     \
   static Type##Vtbl inv##_vt = { (void *)h_qi, (void *)h_ref, (void *)h_ref, inv }; \
   static Type inv##_h = { &inv##_vt };

static HRESULT STDMETHODCALLTYPE on_web_message(ICoreWebView2WebMessageReceivedEventHandler *t,
                                                ICoreWebView2 *s, ICoreWebView2WebMessageReceivedEventArgs *a)
{
   LPWSTR m = NULL;
   (void)t;
   (void)s;
   if (SUCCEEDED(ICoreWebView2WebMessageReceivedEventArgs_TryGetWebMessageAsString(a, &m)) && m)
   {
      char *u = u_utf8(m);
      if (u)
         on_message(u);
      free(u);
      CoTaskMemFree(m);
   }
   return S_OK;
}
HANDLER(ICoreWebView2WebMessageReceivedEventHandler, on_web_message)

static HRESULT STDMETHODCALLTYPE on_resource(ICoreWebView2WebResourceRequestedEventHandler *t,
                                             ICoreWebView2 *s, ICoreWebView2WebResourceRequestedEventArgs *a)
{
   ICoreWebView2WebResourceRequest *req = NULL;
   LPWSTR uri = NULL;
   (void)t;
   (void)s;
   if (SUCCEEDED(ICoreWebView2WebResourceRequestedEventArgs_get_Request(a, &req)) && req &&
       SUCCEEDED(ICoreWebView2WebResourceRequest_get_Uri(req, &uri)) && uri)
   {
      char *u = u_utf8(uri);
      if (u && u_istarts(u, "https://xmb.gbadhoc/"))
         serve(u, a);
      free(u);
      CoTaskMemFree(uri);
   }
   if (req)
      ICoreWebView2WebResourceRequest_Release(req);
   return S_OK;
}
HANDLER(ICoreWebView2WebResourceRequestedEventHandler, on_resource)

static HRESULT STDMETHODCALLTYPE on_new_window(ICoreWebView2NewWindowRequestedEventHandler *t,
                                               ICoreWebView2 *s, ICoreWebView2NewWindowRequestedEventArgs *a)
{
   (void)t;
   (void)s;
   /* links never open a second browser window inside the app */
   ICoreWebView2NewWindowRequestedEventArgs_put_Handled(a, TRUE);
   return S_OK;
}
HANDLER(ICoreWebView2NewWindowRequestedEventHandler, on_new_window)

static HRESULT STDMETHODCALLTYPE on_process_failed(ICoreWebView2ProcessFailedEventHandler *t,
                                                   ICoreWebView2 *s, ICoreWebView2ProcessFailedEventArgs *a)
{
   (void)t;
   (void)a;
   u_log("webview process failed; reloading");
   ICoreWebView2_Reload(s);
   return S_OK;
}
HANDLER(ICoreWebView2ProcessFailedEventHandler, on_process_failed)

static HRESULT STDMETHODCALLTYPE on_nav_done(ICoreWebView2NavigationCompletedEventHandler *t,
                                             ICoreWebView2 *s, ICoreWebView2NavigationCompletedEventArgs *a)
{
   (void)t;
   (void)s;
   (void)a;
   if (g_shot[0])
      SetTimer(g_hwnd, T_SHOT, (UINT)g_wait, NULL);
   return S_OK;
}
HANDLER(ICoreWebView2NavigationCompletedEventHandler, on_nav_done)

static HRESULT STDMETHODCALLTYPE on_shot_done(ICoreWebView2CapturePreviewCompletedHandler *t, HRESULT hr)
{
   (void)t;
   u_log("shot %s: %08lx", g_shot, hr);
   if (!g_result[0])
      PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
   return S_OK;
}
HANDLER(ICoreWebView2CapturePreviewCompletedHandler, on_shot_done)

static void add_script(const char *path)
{
   xw_buf b;
   if (xw_read_file(path, &b) == 0)
   {
      char *s = (char *)realloc(b.data, b.len + 1);
      if (s)
      {
         wchar_t *w;
         s[b.len] = '\0';
         w = u_wide(s);
         ICoreWebView2_AddScriptToExecuteOnDocumentCreated(g_wv, w, NULL);
         free(w);
         free(s);
      }
   }
}

static HRESULT STDMETHODCALLTYPE on_controller(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *t,
                                               HRESULT hr, ICoreWebView2Controller *c)
{
   ICoreWebView2Settings *set = NULL;
   ICoreWebView2Controller2 *c2 = NULL;
   EventRegistrationToken tok;
   RECT rc;
   wchar_t url[1200];
   (void)t;
   if (FAILED(hr) || !c)
   {
      u_log("controller failed %08lx", hr);
      MessageBoxW(g_hwnd, L"GBAdhoc XMB could not start its window (WebView2 failed to load).\n\n"
                          L"Restart the PC, or repair the Microsoft Edge WebView2 Runtime from "
                          L"Settings > Apps, then try again.", L"GBAdhoc XMB", MB_ICONERROR);
      PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
      return S_OK;
   }
   g_ctrl = c;
   ICoreWebView2Controller_AddRef(c);
   ICoreWebView2Controller_get_CoreWebView2(c, &g_wv);
   if (SUCCEEDED(ICoreWebView2Controller_QueryInterface(c, &IID_ICoreWebView2Controller2, (void **)&c2)) && c2)
   {
      COREWEBVIEW2_COLOR bg = { 255, 0x0f, 0x12, 0x30 };
      ICoreWebView2Controller2_put_DefaultBackgroundColor(c2, bg);
      ICoreWebView2Controller2_Release(c2);
   }
   if (SUCCEEDED(ICoreWebView2_get_Settings(g_wv, &set)) && set)
   {
      ICoreWebView2Settings_put_AreDevToolsEnabled(set, g_dev);
      ICoreWebView2Settings_put_AreDefaultContextMenusEnabled(set, g_dev);
      ICoreWebView2Settings_put_IsStatusBarEnabled(set, FALSE);
      ICoreWebView2Settings_put_IsZoomControlEnabled(set, FALSE);
      ICoreWebView2Settings_Release(set);
   }
   GetClientRect(g_hwnd, &rc);
   ICoreWebView2Controller_put_Bounds(c, rc);
   ICoreWebView2_AddWebResourceRequestedFilter(g_wv, L"https://xmb.gbadhoc/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
   ICoreWebView2_add_WebResourceRequested(g_wv, &on_resource_h, &tok);
   ICoreWebView2_add_WebMessageReceived(g_wv, &on_web_message_h, &tok);
   ICoreWebView2_add_NewWindowRequested(g_wv, &on_new_window_h, &tok);
   ICoreWebView2_add_ProcessFailed(g_wv, &on_process_failed_h, &tok);
   ICoreWebView2_add_NavigationCompleted(g_wv, &on_nav_done_h, &tok);
   if (g_script[0])
      add_script(g_script);
   {
      wchar_t *o = u_wide(g_open);
      swprintf(url, 1200, HOST L"desktop.html%ls", o ? o : L"");
      free(o);
   }
   ICoreWebView2_Navigate(g_wv, url);
   return S_OK;
}
HANDLER(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, on_controller)

static HRESULT STDMETHODCALLTYPE on_environment(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *t,
                                                HRESULT hr, ICoreWebView2Environment *e)
{
   (void)t;
   if (FAILED(hr) || !e)
   {
      u_log("environment failed %08lx", hr);
      MessageBoxW(g_hwnd, L"GBAdhoc XMB could not start the WebView2 runtime.\n\n"
                          L"Repair \"Microsoft Edge WebView2 Runtime\" in Settings > Apps, or "
                          L"reinstall it from go.microsoft.com/fwlink/p/?LinkId=2124703.",
                  L"GBAdhoc XMB", MB_ICONERROR);
      PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
      return S_OK;
   }
   g_env = e;
   ICoreWebView2Environment_AddRef(e);
   ICoreWebView2Environment_CreateCoreWebView2Controller(e, g_hwnd, &on_controller_h);
   return S_OK;
}
HANDLER(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, on_environment)

/* ---- the window --------------------------------------------------------- */

static void check_devices(void)
{
   sb b;
   if (!g_ready)
      return;
   sb_init(&b);
   psp_devices_json(&b);
   if (!g_last_devices.s || strcmp(b.s, g_last_devices.s) != 0)
   {
      sb ev;
      sb_init(&ev);
      sb_add(&ev, "{\"event\":\"devices\",\"data\":");
      sb_add(&ev, b.s);
      sb_add(&ev, "}");
      post_json(ev.s);
      sb_free(&ev);
      sb_free(&g_last_devices);
      g_last_devices = b;
   }
   else
      sb_free(&b);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
   switch (m)
   {
   case WM_SIZE:
      if (g_ctrl)
      {
         RECT rc;
         GetClientRect(h, &rc);
         ICoreWebView2Controller_put_Bounds(g_ctrl, rc);
      }
      return 0;
   case WM_GETMINMAXINFO:
   {
      MINMAXINFO *mm = (MINMAXINFO *)l;
      UINT dpi = GetDpiForWindow(h);
      mm->ptMinTrackSize.x = MulDiv(1100, dpi ? dpi : 96, 96);
      mm->ptMinTrackSize.y = MulDiv(680, dpi ? dpi : 96, 96);
      return 0;
   }
   case WM_DPICHANGED:
   {
      RECT *r = (RECT *)l;
      SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
   }
   case WM_DEVICECHANGE:
      /* a volume came or went: look again shortly (letters settle late) */
      SetTimer(h, T_DEVICES, 800, NULL);
      return TRUE;
   case WM_TIMER:
      if (w == T_DEVICES)
         KillTimer(h, T_DEVICES);
      if (w == T_DEVICES || w == T_POLL)
         check_devices();
      if (w == T_SHOT)
      {
         IStream *st = NULL;
         wchar_t *p = u_wide(g_shot);
         KillTimer(h, T_SHOT);
         if (p && SUCCEEDED(SHCreateStreamOnFileEx(p, STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL, TRUE, NULL, &st)))
         {
            ICoreWebView2_CapturePreview(g_wv, COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, st, &on_shot_done_h);
            IStream_Release(st);
         }
         free(p);
      }
      return 0;
   case WM_APP_JSON:
   {
      char *s = (char *)l;
      if (g_wv && s)
      {
         wchar_t *wj = u_wide(s);
         ICoreWebView2_PostWebMessageAsJson(g_wv, wj);
         free(wj);
      }
      free(s);
      return 0;
   }
   case WM_CLOSE:
      if (g_writing > 0 && !g_shot[0] && !g_result[0] &&
          MessageBoxW(h, L"GBAdhoc XMB is writing to the PSP.\n\nClose anyway? The entry being "
                         L"written is finished or undone the next time the PSP is connected; "
                         L"nothing is left half-written.",
                      L"GBAdhoc XMB", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
         return 0;
      DestroyWindow(h);
      return 0;
   case WM_DESTROY:
      if (g_ctrl)
         ICoreWebView2Controller_Close(g_ctrl);
      PostQuitMessage(0);
      return 0;
   }
   return DefWindowProcW(h, m, w, l);
}

static void no_runtime(void)
{
   if (MessageBoxW(NULL,
                   L"GBAdhoc XMB needs the Microsoft Edge WebView2 Runtime, which is part of "
                   L"Windows 10 and 11 but is missing or was removed on this PC.\n\n"
                   L"Install it from Microsoft (free, about 2 MB to download), then start "
                   L"GBAdhoc XMB again.\n\nOpen Microsoft's download page now?",
                   L"GBAdhoc XMB: WebView2 Runtime needed", MB_YESNO | MB_ICONINFORMATION) == IDYES)
      ShellExecuteW(NULL, L"open", L"https://go.microsoft.com/fwlink/p/?LinkId=2124703", NULL, NULL, SW_SHOWNORMAL);
}

static void arg_utf8(const wchar_t *w, char *out, size_t cap)
{
   char *s = u_utf8(w);
   snprintf(out, cap, "%s", s ? s : "");
   free(s);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmdline, int show)
{
   WNDCLASSEXW wc;
   MSG msg;
   int argc = 0, i, w, h, x, y;
   wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc), dll[1024], udf[1100];
   char ad[1024];
   HANDLE mutex;
   UINT dpi;
   HRESULT hr;
   (void)prev;
   (void)cmdline;

   SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
   for (i = 1; argv && i < argc; i++)
   {
      char v[1100] = "";
      if (i + 1 < argc)
         arg_utf8(argv[i + 1], v, sizeof(v));
      if (!wcscmp(argv[i], L"--fake-psp") && i + 1 < argc) { psp_add_fake(v); i++; }
      else if (!wcscmp(argv[i], L"--ui-dir") && i + 1 < argc) { snprintf(g_ui_dir, sizeof(g_ui_dir), "%s", v); u_slashes(g_ui_dir); i++; }
      else if (!wcscmp(argv[i], L"--open") && i + 1 < argc) { snprintf(g_open, sizeof(g_open), "%s", v); i++; }
      else if (!wcscmp(argv[i], L"--script") && i + 1 < argc) { snprintf(g_script, sizeof(g_script), "%s", v); i++; }
      else if (!wcscmp(argv[i], L"--shot") && i + 1 < argc) { snprintf(g_shot, sizeof(g_shot), "%s", v); i++; }
      else if (!wcscmp(argv[i], L"--result") && i + 1 < argc) { snprintf(g_result, sizeof(g_result), "%s", v); i++; }
      else if (!wcscmp(argv[i], L"--wait") && i + 1 < argc) { g_wait = atoi(v); i++; }
      else if (!wcscmp(argv[i], L"--dev")) g_dev = 1;
   }
   LocalFree(argv);

   /* one copy at a time on the real PSP: two would write the same card */
   if (!psp_fake_mode())
   {
      mutex = CreateMutexW(NULL, TRUE, L"Local\\GBAdhocXMB.Desktop");
      if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
      {
         HWND other = FindWindowW(L"GBAdhocXMB", NULL);
         if (other)
         {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
         }
         return 0;
      }
   }

   CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
   InitializeCriticalSection(&g_store_cs);
   sb_init(&g_last_devices);
   net_init();
   art_init();
   u_log("GBAdhoc XMB %s starting%s", XMB_VERSION, psp_fake_mode() ? " (test folders)" : "");

   if (wv2_find_runtime(dll, 1024, g_runtime, 64) != 0)
   {
      u_log("no WebView2 runtime");
      no_runtime();
      return 1;
   }

   memset(&wc, 0, sizeof(wc));
   wc.cbSize = sizeof(wc);
   wc.lpfnWndProc = wndproc;
   wc.hInstance = hi;
   wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
   wc.hIconSm = LoadIconW(hi, MAKEINTRESOURCEW(1));
   wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
   wc.hbrBackground = CreateSolidBrush(RGB(0x12, 0x12, 0x14));
   wc.lpszClassName = L"GBAdhocXMB";
   RegisterClassExW(&wc);

   dpi = GetDpiForSystem();
   w = MulDiv(1280, dpi, 96) + GetSystemMetricsForDpi(SM_CXFRAME, dpi) * 2;
   h = MulDiv(800, dpi, 96) + GetSystemMetricsForDpi(SM_CYCAPTION, dpi) + GetSystemMetricsForDpi(SM_CYFRAME, dpi) * 2;
   {
      RECT wa;
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
      if (w > wa.right - wa.left) w = wa.right - wa.left;
      if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
      x = wa.left + (wa.right - wa.left - w) / 2;
      y = wa.top + (wa.bottom - wa.top - h) / 2;
   }
   g_hwnd = CreateWindowExW(0, L"GBAdhocXMB", L"GBAdhoc XMB", WS_OVERLAPPEDWINDOW, x, y, w, h,
                            NULL, NULL, hi, NULL);
   dark_titlebar(1);
   ShowWindow(g_hwnd, g_shot[0] || g_result[0] ? SW_SHOWNOACTIVATE : show);
   UpdateWindow(g_hwnd);
   SetTimer(g_hwnd, T_POLL, 2500, NULL);

   u_appdata("WebView2", ad, sizeof(ad));
   {
      wchar_t *wa = u_wide(ad);
      swprintf(udf, 1100, L"%ls", wa ? wa : L"");
      free(wa);
   }
   hr = wv2_create_environment(dll, udf, &on_environment_h);
   if (FAILED(hr))
   {
      u_log("create environment %08lx", hr);
      no_runtime();
      return 1;
   }
   while (GetMessageW(&msg, NULL, 0, 0) > 0)
   {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
   }
   return 0;
}
