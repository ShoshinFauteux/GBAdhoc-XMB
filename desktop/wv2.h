/* wv2.h -- WebView2 without WebView2Loader.dll (GBAdhoc XMB).
 *
 * The WebView2 runtime is part of Windows 10/11 (Evergreen).  The loader
 * Microsoft ships only finds the runtime's EmbeddedBrowserWebView.dll and
 * calls its one export; we do the same here, so the app stays one exe. */
#ifndef XMB_WV2_H
#define XMB_WV2_H

#define COBJMACROS
#include <windows.h>
#include "third_party/webview2/WebView2.h"

/* The installed runtime: its version ("156.0.4314.58") and the DLL.
 * 0 = found. */
int wv2_find_runtime(wchar_t *dll, size_t dll_cap, wchar_t *ver, size_t ver_cap);

/* Start creating an environment (the handler is called on this thread).
 * `user_data` is the profile folder. */
HRESULT wv2_create_environment(const wchar_t *dll, const wchar_t *user_data,
                               ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *h);

#endif
