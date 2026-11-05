/* wv2.c -- see wv2.h. */
#include "wv2.h"

#include <stdio.h>
#include <wchar.h>

/* The Evergreen WebView2 runtime's EdgeUpdate client id. */
#define WV2_CLIENT L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"

static int reg_str(HKEY root, const wchar_t *key, REGSAM extra, const wchar_t *val,
                   wchar_t *out, DWORD cap_chars)
{
   HKEY k;
   DWORD type = 0, bytes = cap_chars * sizeof(wchar_t);
   LONG r;
   out[0] = 0;
   if (RegOpenKeyExW(root, key, 0, KEY_READ | extra, &k) != ERROR_SUCCESS)
      return -1;
   r = RegQueryValueExW(k, val, NULL, &type, (BYTE *)out, &bytes);
   RegCloseKey(k);
   if (r != ERROR_SUCCESS || type != REG_SZ)
   {
      out[0] = 0;
      return -1;
   }
   out[cap_chars - 1] = 0;
   return 0;
}

static int file_exists(const wchar_t *p)
{
   DWORD a = GetFileAttributesW(p);
   return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int try_dir(const wchar_t *dir, wchar_t *dll, size_t cap)
{
   swprintf(dll, cap, L"%ls\\EBWebView\\x64\\EmbeddedBrowserWebView.dll", dir);
   return file_exists(dll);
}

int wv2_find_runtime(wchar_t *dll, size_t dll_cap, wchar_t *ver, size_t ver_cap)
{
   static const struct { HKEY root; REGSAM sam; } where[] = {
      { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY },
      { HKEY_CURRENT_USER, 0 },
      { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY },
   };
   wchar_t pv[64], loc[MAX_PATH], dir[MAX_PATH * 2], env[MAX_PATH];
   int i;
   /* the documented override, for a fixed-version runtime */
   if (GetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", env, MAX_PATH) &&
       try_dir(env, dll, dll_cap))
   {
      swprintf(ver, ver_cap, L"fixed");
      return 0;
   }
   for (i = 0; i < 3; i++)
   {
      if (reg_str(where[i].root, WV2_CLIENT, where[i].sam, L"pv", pv, 64) != 0 ||
          !pv[0] || !wcscmp(pv, L"0.0.0.0"))
         continue;
      if (reg_str(where[i].root, WV2_CLIENT, where[i].sam, L"location", loc, MAX_PATH) == 0 && loc[0])
      {
         swprintf(dir, MAX_PATH * 2, L"%ls\\%ls", loc, pv);
         if (try_dir(dir, dll, dll_cap))
            goto found;
      }
      {
         wchar_t base[MAX_PATH];
         if (where[i].root == HKEY_CURRENT_USER)
            GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
         else
            GetEnvironmentVariableW(L"ProgramFiles(x86)", base, MAX_PATH);
         swprintf(dir, MAX_PATH * 2, L"%ls\\Microsoft\\EdgeWebView\\Application\\%ls", base, pv);
         if (try_dir(dir, dll, dll_cap))
            goto found;
      }
      continue;
found:
      swprintf(ver, ver_cap, L"%ls", pv);
      return 0;
   }
   return -1;
}

/* EmbeddedBrowserWebView.dll's export, as WebView2Loader calls it:
 * (check for a running instance, runtime kind, user data folder, options,
 * completion handler). */
typedef HRESULT(STDMETHODCALLTYPE *create_internal_fn)(
   BOOL check_running, int runtime_type, PCWSTR user_data, IUnknown *options,
   ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *h);

HRESULT wv2_create_environment(const wchar_t *dll, const wchar_t *user_data,
                               ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *h)
{
   HMODULE m = LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
   create_internal_fn fn;
   if (!m)
      return HRESULT_FROM_WIN32(GetLastError());
   fn = (create_internal_fn)(void *)GetProcAddress(m, "CreateWebViewEnvironmentWithOptionsInternal");
   if (!fn)
      return E_NOINTERFACE;
   /* runtime type 1: an installed (Evergreen or fixed) WebView2 runtime */
   return fn(TRUE, 1, user_data, NULL, h);
}
