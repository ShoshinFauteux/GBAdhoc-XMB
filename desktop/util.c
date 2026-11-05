/* util.c -- see util.h. */
#include "util.h"

#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wchar_t *u_wide(const char *s)
{
   int n = MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, NULL, 0);
   wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
   if (w)
      MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, w, n);
   return w;
}

char *u_utf8(const wchar_t *w)
{
   int n = WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, NULL, 0, NULL, NULL);
   char *s = n > 0 ? (char *)malloc((size_t)n) : NULL;
   if (s)
      WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, s, n, NULL, NULL);
   return s;
}

void u_slashes(char *p)
{
   for (; *p; p++)
      if (*p == '\\')
         *p = '/';
}

void u_join(char *out, size_t cap, const char *a, const char *b)
{
   size_t n = strlen(a);
   int sep = n && a[n - 1] != '/' && a[n - 1] != '\\';
   while (*b == '/' || *b == '\\')
      b++;
   snprintf(out, cap, "%s%s%s", a, sep ? "/" : "", b);
}

static char lc(char c)
{
   return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c == '\\' ? '/' : c;
}

int u_ieq(const char *a, const char *b)
{
   for (; *a && *b; a++, b++)
      if (lc(*a) != lc(*b))
         return 0;
   return *a == *b;
}

int u_istarts(const char *s, const char *p)
{
   for (; *p; s++, p++)
      if (!*s || lc(*s) != lc(*p))
         return 0;
   return 1;
}

const char *u_ext(const char *path)
{
   const char *b = u_base(path), *d = strrchr(b, '.');
   return d && d > b ? d + 1 : "";
}

const char *u_base(const char *path)
{
   const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
   if (b > a)
      a = b;
   return a ? a + 1 : path;
}

void u_stem(const char *path, char *out, size_t cap)
{
   const char *b = u_base(path), *d = strrchr(b, '.');
   size_t n = d && d > b ? (size_t)(d - b) : strlen(b);
   if (n >= cap)
      n = cap - 1;
   memcpy(out, b, n);
   out[n] = '\0';
}

static DWORD attrs(const char *p)
{
   wchar_t *w = u_wide(p);
   DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
   free(w);
   return a;
}

int u_is_dir(const char *p)
{
   DWORD a = attrs(p);
   return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int u_is_file(const char *p)
{
   DWORD a = attrs(p);
   return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

long long u_file_size(const char *p)
{
   WIN32_FILE_ATTRIBUTE_DATA d;
   wchar_t *w = u_wide(p);
   BOOL ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, &d);
   free(w);
   return ok ? ((long long)d.nFileSizeHigh << 32 | d.nFileSizeLow) : -1;
}

int u_mkdirs(const char *path)
{
   char tmp[2048];
   size_t i, n = strlen(path);
   if (n >= sizeof(tmp))
      return -1;
   memcpy(tmp, path, n + 1);
   for (i = 1; i <= n; i++)
      if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == '\0')
      {
         char c = tmp[i];
         if (tmp[i - 1] == ':')
            continue;
         tmp[i] = '\0';
         if (!u_is_dir(tmp))
         {
            wchar_t *w = u_wide(tmp);
            BOOL ok = w && CreateDirectoryW(w, NULL);
            DWORD e = GetLastError();
            free(w);
            if (!ok && e != ERROR_ALREADY_EXISTS)
               return -1;
         }
         tmp[i] = c;
      }
   return 0;
}

int u_rmtree(const char *path)
{
   char pat[2100], sub[2100];
   wchar_t *w;
   WIN32_FIND_DATAW fd;
   HANDLE h;
   int rc = 0;
   if (!u_is_dir(path))
      return u_is_file(path) ? (DeleteFileW((w = u_wide(path))) ? (free(w), 0) : (free(w), -1)) : 0;
   u_join(pat, sizeof(pat), path, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h != INVALID_HANDLE_VALUE)
   {
      do
      {
         char *n;
         if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
         n = u_utf8(fd.cFileName);
         u_join(sub, sizeof(sub), path, n ? n : "");
         free(n);
         if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            rc |= u_rmtree(sub);
         else
         {
            wchar_t *ws = u_wide(sub);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
               SetFileAttributesW(ws, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileW(ws))
               rc = -1;
            free(ws);
         }
      } while (FindNextFileW(h, &fd));
      FindClose(h);
   }
   w = u_wide(path);
   if (!RemoveDirectoryW(w))
      rc = -1;
   free(w);
   return rc;
}

int u_move(const char *from, const char *to)
{
   wchar_t *a = u_wide(from), *b = u_wide(to);
   BOOL ok = a && b && MoveFileExW(a, b, MOVEFILE_WRITE_THROUGH);
   DWORD e = GetLastError();
   free(a);
   free(b);
   return ok ? 0 : (int)(e ? e : 1);
}

DWORD u_write_durable(const char *path, const void *data, size_t len)
{
   wchar_t *w = u_wide(path);
   HANDLE h;
   DWORD wr = 0, e = 0;
   if (!w)
      return ERROR_OUTOFMEMORY;
   h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
   free(w);
   if (h == INVALID_HANDLE_VALUE)
      return GetLastError();
   if (!WriteFile(h, data, (DWORD)len, &wr, NULL) || wr != len)
      e = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
   if (!e && !FlushFileBuffers(h))
      e = GetLastError();
   if (!CloseHandle(h) && !e)
      e = GetLastError();
   return e;
}

int u_read_head(const char *path, unsigned char *buf, size_t cap, size_t *got)
{
   wchar_t *w = u_wide(path);
   HANDLE h = w ? CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)
                : INVALID_HANDLE_VALUE;
   DWORD n = 0;
   free(w);
   *got = 0;
   if (h == INVALID_HANDLE_VALUE)
      return -1;
   ReadFile(h, buf, (DWORD)cap, &n, NULL);
   CloseHandle(h);
   *got = n;
   return 0;
}

int u_file_crc(const char *path, unsigned long *crc)
{
   wchar_t *w = u_wide(path);
   HANDLE h = w ? CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, NULL)
                : INVALID_HANDLE_VALUE;
   static __thread unsigned char *buf;
   DWORD n;
   unsigned long c = 0;
   free(w);
   if (h == INVALID_HANDLE_VALUE)
      return -1;
   if (!buf)
      buf = (unsigned char *)malloc(1 << 20);
   if (!buf)
   {
      CloseHandle(h);
      return -1;
   }
   while (ReadFile(h, buf, 1 << 20, &n, NULL) && n)
      c = xw_crc32(c, buf, n);
   CloseHandle(h);
   *crc = c;
   return 0;
}

void u_appdata(const char *sub, char *out, size_t cap)
{
   wchar_t base[MAX_PATH];
   char *b;
   /* XMB_APPDATA: the tests keep everything in a scratch folder */
   if (GetEnvironmentVariableW(L"XMB_APPDATA", base, MAX_PATH))
   {
      b = u_utf8(base);
      u_slashes(b);
      snprintf(out, cap, "%s%s%s", b ? b : ".", sub && sub[0] ? "/" : "", sub ? sub : "");
      free(b);
      u_mkdirs(out);
      return;
   }
   if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, base)))
      GetTempPathW(MAX_PATH, base);
   b = u_utf8(base);
   u_slashes(b);
   snprintf(out, cap, "%s/GBAdhoc XMB%s%s", b ? b : ".", sub && sub[0] ? "/" : "", sub ? sub : "");
   free(b);
   u_mkdirs(out);
}

void u_urlenc(const char *s, char *out, size_t cap, int keep_slash)
{
   static const char hx[] = "0123456789ABCDEF";
   size_t n = 0;
   const unsigned char *p = (const unsigned char *)s;
   for (; *p && n + 4 < cap; p++)
   {
      unsigned c = *p;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '-' || c == '_' || c == '.' || c == '~' || (keep_slash && c == '/'))
         out[n++] = (char)c;
      else
      {
         out[n++] = '%';
         out[n++] = hx[c >> 4];
         out[n++] = hx[c & 15];
      }
   }
   out[n] = '\0';
}

static int hv(char c)
{
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 10;
   if (c >= 'A' && c <= 'F') return c - 'A' + 10;
   return -1;
}

void u_urldec(char *s)
{
   char *o = s;
   for (; *s; s++)
   {
      if (*s == '%' && hv(s[1]) >= 0 && hv(s[2]) >= 0)
      {
         *o++ = (char)(hv(s[1]) << 4 | hv(s[2]));
         s += 2;
      }
      else if (*s == '+')
         *o++ = ' ';
      else
         *o++ = *s;
   }
   *o = '\0';
}

int u_b64dec(const char *s, xw_buf *out)
{
   size_t n = strlen(s), o = 0;
   unsigned v = 0;
   int bits = 0;
   const char *comma = strchr(s, ',');
   if (comma && !strncmp(s, "data:", 5))
   {
      s = comma + 1;
      n = strlen(s);
   }
   out->data = (unsigned char *)malloc(n * 3 / 4 + 4);
   out->len = 0;
   if (!out->data)
      return -1;
   for (; *s; s++)
   {
      int c = *s;
      int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 :
              c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
      if (d < 0)
         continue;
      v = v << 6 | (unsigned)d;
      bits += 6;
      if (bits >= 8)
      {
         bits -= 8;
         out->data[o++] = (unsigned char)(v >> bits);
      }
   }
   out->len = o;
   return 0;
}

void u_log(const char *fmt, ...)
{
   static CRITICAL_SECTION cs;
   static LONG init;
   static char path[1024];
   FILE *f;
   va_list ap;
   SYSTEMTIME t;
   if (InterlockedCompareExchange(&init, 1, 0) == 0)
   {
      InitializeCriticalSection(&cs);
      u_appdata("", path, sizeof(path));
      strncat(path, "/log.txt", sizeof(path) - strlen(path) - 1);
      InterlockedExchange(&init, 2);
   }
   while (init != 2)
      Sleep(1);
   EnterCriticalSection(&cs);
   {
      wchar_t *w = u_wide(path);
      f = w ? _wfopen(w, L"a") : NULL;
      free(w);
   }
   if (f)
   {
      GetLocalTime(&t);
      fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
      va_start(ap, fmt);
      vfprintf(f, fmt, ap);
      va_end(ap);
      fputc('\n', f);
      fclose(f);
   }
   LeaveCriticalSection(&cs);
}
