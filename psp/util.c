/* util.c -- see util.h. */
#include "util.h"

#include <pspkernel.h>
#include <psprtc.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef XMB_DEMO
/* the demo's virtual clock (main.c): 1/60 s a frame, from the real time at start */
static volatile long long g_demo_t0_us = -1;
static volatile unsigned g_demo_frames;

void demo_clock_start(void)
{
   g_demo_t0_us = (long long)sceKernelGetSystemTimeWide();
}

void demo_clock_tick(void)
{
   g_demo_frames++;
}

static long long clock_us(void)
{
   if (g_demo_t0_us < 0)
      return (long long)sceKernelGetSystemTimeWide();
   return g_demo_t0_us + (long long)g_demo_frames * 1000000LL / 60;
}

double now_s(void)
{
   return (double)clock_us() / 1e6;
}

unsigned now_ms(void)
{
   return (unsigned)(clock_us() / 1000);
}
#else
double now_s(void)
{
   return (double)sceKernelGetSystemTimeWide() / 1e6;
}

unsigned now_ms(void)
{
   return (unsigned)(sceKernelGetSystemTimeWide() / 1000);
}
#endif

float clampf(float v, float lo, float hi)
{
   return v < lo ? lo : v > hi ? hi : v;
}

float lerpf(float a, float b, float t)
{
   return a + (b - a) * t;
}

float bezier(float x1, float y1, float x2, float y2, float t)
{
   /* solve x(u) = t by Newton, then y(u) */
   float u = t, i;
   if (t <= 0) return 0;
   if (t >= 1) return 1;
   for (i = 0; i < 8; i++)
   {
      float iu = 1 - u;
      float x = 3 * iu * iu * u * x1 + 3 * iu * u * u * x2 + u * u * u;
      float dx = 3 * iu * iu * x1 + 6 * iu * u * (x2 - x1) + 3 * u * u * (1 - x2);
      if (fabsf(dx) < 1e-5f)
         break;
      u -= (x - t) / dx;
      if (u < 0) u = 0;
      if (u > 1) u = 1;
   }
   {
      float iu = 1 - u;
      return 3 * iu * iu * u * y1 + 3 * iu * u * u * y2 + u * u * u;
   }
}

float ease_out(float t)
{
   return bezier(0.2f, 0.7f, 0.2f, 1.0f, t);
}

void tw_set(tween *t, float v)
{
   t->from = t->to = t->cur = v;
   t->dur = 0;
}

void tw_go(tween *t, float to, float dur)
{
   if (to == t->to)
      return;
   t->from = tw_val(t);
   t->to = to;
   t->t0 = now_s();
   t->dur = dur;
}

float tw_val(tween *t)
{
   float k;
   if (t->dur <= 0)
      return t->cur = t->to;
   k = (float)((now_s() - t->t0) / t->dur);
   if (k >= 1)
   {
      t->dur = 0;
      return t->cur = t->to;
   }
   return t->cur = lerpf(t->from, t->to, ease_out(k));
}

int tw_busy(const tween *t)
{
   return t->dur > 0 && now_s() - t->t0 < t->dur;
}

/* ---- files ---- */

int file_exists(const char *p)
{
   SceIoStat st;
   return sceIoGetstat(p, &st) >= 0 && !FIO_S_ISDIR(st.st_mode);
}

int dir_exists(const char *p)
{
   SceIoStat st;
   return sceIoGetstat(p, &st) >= 0 && FIO_S_ISDIR(st.st_mode);
}

long file_size(const char *p)
{
   SceIoStat st;
   if (sceIoGetstat(p, &st) < 0)
      return -1;
   return (long)st.st_size;
}

int read_all(const char *p, unsigned char **data, size_t *len)
{
   SceUID fd = sceIoOpen(p, PSP_O_RDONLY, 0);
   SceOff n;
   unsigned char *b;
   *data = NULL;
   *len = 0;
   if (fd < 0)
      return -1;
   n = sceIoLseek(fd, 0, PSP_SEEK_END);
   sceIoLseek(fd, 0, PSP_SEEK_SET);
   if (n < 0 || n > 64 * 1024 * 1024)
   {
      sceIoClose(fd);
      return -1;
   }
   b = (unsigned char *)malloc((size_t)n + 1);
   if (!b)
   {
      sceIoClose(fd);
      return -2;
   }
   if (sceIoRead(fd, b, (SceSize)n) != (int)n)
   {
      free(b);
      sceIoClose(fd);
      return -1;
   }
   sceIoClose(fd);
   b[n] = 0;
   *data = b;
   *len = (size_t)n;
   return 0;
}

int write_all(const char *p, const void *d, size_t n)
{
   char tmp[300];
   SceUID fd;
   int w;
   snprintf(tmp, sizeof(tmp), "%s.tmp", p);
   fd = sceIoOpen(tmp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
   if (fd < 0)
      return -1;
   w = sceIoWrite(fd, d, (SceSize)n);
   sceIoClose(fd);
   if (w != (int)n)
   {
      sceIoRemove(tmp);
      return -2;              /* most likely the stick is full */
   }
   sceIoRemove(p);
   if (sceIoRename(tmp, p) < 0)
   {
      sceIoRemove(tmp);
      return -1;
   }
   return 0;
}

int mkdirs(const char *p)
{
   char t[300];
   size_t i, n = strlen(p);
   if (n >= sizeof(t))
      return -1;
   memcpy(t, p, n + 1);
   for (i = 1; i <= n; i++)
      if (t[i] == '/' || t[i] == 0)
      {
         char c = t[i];
         if (t[i - 1] == ':')
            continue;
         t[i] = 0;
         if (!dir_exists(t))
            sceIoMkdir(t, 0777);
         t[i] = c;
      }
   return dir_exists(p) ? 0 : -1;
}

int remove_tree(const char *p)
{
   SceUID d = sceIoDopen(p);
   SceIoDirent e;
   char f[320];
   if (d < 0)
      return -1;
   memset(&e, 0, sizeof(e));
   while (sceIoDread(d, &e) > 0)
   {
      if (e.d_name[0] == '.')
         continue;
      snprintf(f, sizeof(f), "%s/%s", p, e.d_name);
      if (FIO_S_ISDIR(e.d_stat.st_mode))
         remove_tree(f);
      else
         sceIoRemove(f);
      memset(&e, 0, sizeof(e));
   }
   sceIoDclose(d);
   return sceIoRmdir(p) < 0 ? -1 : 0;
}

const char *base_name(const char *p)
{
   const char *s = strrchr(p, '/');
   return s ? s + 1 : p;
}

void stem_of(const char *p, char *out, size_t cap)
{
   const char *b = base_name(p), *d = strrchr(b, '.');
   size_t n = d && d > b ? (size_t)(d - b) : strlen(b);
   if (n >= cap) n = cap - 1;
   memcpy(out, b, n);
   out[n] = 0;
}

const char *ext_of(const char *p)
{
   const char *b = base_name(p), *d = strrchr(b, '.');
   return d ? d : "";
}

int ieq_str(const char *a, const char *b)
{
   for (; *a && *b; a++, b++)
   {
      char x = *a, y = *b;
      if (x >= 'A' && x <= 'Z') x += 32;
      if (y >= 'A' && y <= 'Z') y += 32;
      if (x != y)
         return 0;
   }
   return *a == *b;
}

int ends_with_i(const char *s, const char *suf)
{
   size_t n = strlen(s), m = strlen(suf);
   return n >= m && ieq_str(s + n - m, suf);
}

void path_join(char *out, size_t cap, const char *a, const char *b)
{
   size_t n = strlen(a);
   snprintf(out, cap, "%s%s%s", a, n && a[n - 1] != '/' ? "/" : "", b);
}

long long dev_free(const char *dev)
{
   struct { unsigned max_clusters, free_clusters, max_sectors, sector_size, sector_count; } info;
   void *ptr = &info;
   memset(&info, 0, sizeof(info));
   if (sceIoDevctl(dev, 0x02425818, &ptr, sizeof(ptr), NULL, 0) < 0)
      return -1;
   return (long long)info.free_clusters * info.sector_count * info.sector_size;
}

int ini_get(const char *buf, const char *key, char *out, size_t cap)
{
   size_t klen = strlen(key);
   const char *p = buf;
   while (p && *p)
   {
      const char *line = p, *eol = strchr(p, '\n'), *end, *v;
      p = eol ? eol + 1 : NULL;
      end = eol ? eol : line + strlen(line);
      while (line < end && (*line == ' ' || *line == '\t'))
         line++;
      if (line >= end || *line == '#' || *line == ';')
         continue;
      if ((size_t)(end - line) <= klen || strncmp(line, key, klen) != 0)
         continue;
      v = line + klen;
      while (v < end && (*v == ' ' || *v == '\t'))
         v++;
      if (v >= end || *v != '=')
         continue;
      v++;
      while (v < end && (*v == ' ' || *v == '\t'))
         v++;
      while (end > v && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
         end--;
      {
         size_t n = (size_t)(end - v);
         if (n >= cap) n = cap - 1;
         memcpy(out, v, n);
         out[n] = 0;
      }
      return 0;
   }
   return -1;
}

/* ---- log ---- */

static char g_log[256];
static SceUID g_logfd = -1;

void log_open(const char *dir)
{
   char d[256];
   snprintf(d, sizeof(d), "%s/log", dir);
   mkdirs(d);
   snprintf(g_log, sizeof(g_log), "%s/xmbapp.log", d);
   g_logfd = sceIoOpen(g_log, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
}

void logf_(const char *fmt, ...)
{
   char b[512];
   int n;
   va_list ap;
   if (g_logfd < 0)
      return;
   n = snprintf(b, sizeof(b), "%8u ", now_ms());
   va_start(ap, fmt);
   n += vsnprintf(b + n, sizeof(b) - (size_t)n - 2, fmt, ap);
   va_end(ap);
   if (n > (int)sizeof(b) - 2) n = (int)sizeof(b) - 2;
   b[n++] = '\n';
   sceIoWrite(g_logfd, b, (SceSize)n);
}

void lock_init(lock_t *l)
{
   l->id = sceKernelCreateSema("xmblock", 0, 1, 1, NULL);
}

void lock_take(lock_t *l)
{
   sceKernelWaitSema(l->id, 1, NULL);
}

void lock_give(lock_t *l)
{
   sceKernelSignalSema(l->id, 1);
}

/* ---- clock ---- */

void wall_hm(int *h, int *m)
{
   ScePspDateTime t;
   memset(&t, 0, sizeof(t));
   sceRtcGetCurrentClockLocalTime(&t);
   *h = t.hour;
   *m = t.minute;
}

static long days_from_civil(int y, unsigned m, unsigned d)
{
   int era;
   unsigned yoe, doy, doe;
   y -= m <= 2;
   era = (y >= 0 ? y : y - 399) / 400;
   yoe = (unsigned)(y - era * 400);
   doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
   doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
   return era * 146097L + (long)doe - 719468L;
}

long wall_unix(void)
{
   ScePspDateTime t;
   memset(&t, 0, sizeof(t));
   sceRtcGetCurrentClockLocalTime(&t);
   return days_from_civil(t.year, t.month, t.day) * 86400L + t.hour * 3600L + t.minute * 60L + t.second;
}

void fmt_day(long t, char *out, size_t cap)
{
   static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
   long z = t / 86400 + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
   unsigned doe = (unsigned)(z - era * 146097);
   unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
   unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
   unsigned mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
   unsigned m = mp < 10 ? mp + 3 : mp - 9;
   if (t / 86400 == wall_unix() / 86400)
      snprintf(out, cap, "today");
   else
      snprintf(out, cap, "%u %s", d, mon[(m - 1) % 12]);
}

void fmt_mb(long long b, char *out, size_t cap)
{
   double mb = (double)b / (1024.0 * 1024.0);
   if (mb >= 1024)
      snprintf(out, cap, "%.1f GB", mb / 1024);
   else if (mb < 1)
      snprintf(out, cap, "%d KB", (int)(b / 1024));
   else
      snprintf(out, cap, "%d MB", (int)(mb + 0.5));
}
