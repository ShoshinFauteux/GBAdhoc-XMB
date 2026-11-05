/* xw_core.c -- see xw_core.h (issue #29, docs/XMB-LAUNCH.md). */
#include "xw_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO          /* every file goes through xw_read_file (UTF-8 paths) */
#define STBI_FAILURE_USERMSG
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "third_party/stb_image_write.h"

void xw_buf_free(xw_buf *b)
{
   if (b)
   {
      free(b->data);
      b->data = NULL;
      b->len = 0;
   }
}

/* ---- files, with UTF-8 paths on every platform ------------------------- */

#ifdef _WIN32
static wchar_t *wide(const char *s)
{
   int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
   wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
   if (w)
      MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
   return w;
}
static FILE *xw_fopen(const char *path, const char *mode)
{
   wchar_t *wp = wide(path), wm[8];
   FILE *f = NULL;
   size_t i;
   for (i = 0; mode[i] && i < 7; i++)
      wm[i] = (wchar_t)mode[i];
   wm[i] = 0;
   if (wp)
      f = _wfopen(wp, wm);
   free(wp);
   return f;
}
static int xw_mkdir1(const char *path)
{
   wchar_t *wp = wide(path);
   int rc = wp ? _wmkdir(wp) : -1;
   free(wp);
   return rc == 0 || errno == EEXIST ? 0 : -1;
}
#else
#define xw_fopen fopen
static int xw_mkdir1(const char *path)
{
   return mkdir(path, 0777) == 0 || errno == EEXIST ? 0 : -1;
}
#endif

int xw_read_file(const char *path, xw_buf *out)
{
   FILE *f = xw_fopen(path, "rb");
   long n;
   out->data = NULL;
   out->len = 0;
   if (!f)
      return -1;
   if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
   {
      fclose(f);
      return -1;
   }
   out->data = (unsigned char *)malloc(n ? (size_t)n : 1);
   if (!out->data || fread(out->data, 1, (size_t)n, f) != (size_t)n)
   {
      fclose(f);
      xw_buf_free(out);
      return -1;
   }
   out->len = (size_t)n;
   fclose(f);
   return 0;
}

int xw_write_file(const char *path, const void *data, size_t len)
{
   FILE *f = xw_fopen(path, "wb");
   int ok;
   if (!f)
      return -1;
   ok = fwrite(data, 1, len, f) == len;
   ok = (fclose(f) == 0) && ok;
   return ok ? 0 : -1;
}

/* Every directory along `path` (separators / or \). */
static int xw_mkdirs(const char *path)
{
   char tmp[1024];
   size_t i, n = strlen(path);
   if (n >= sizeof(tmp))
      return -1;
   memcpy(tmp, path, n + 1);
   for (i = 1; i <= n; i++)
   {
      if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == '\0')
      {
         char c = tmp[i];
         if (i > 0 && tmp[i - 1] == ':')      /* "G:" or "ms0:" itself */
            continue;
         tmp[i] = '\0';
         if (tmp[0] && xw_mkdir1(tmp) != 0)
            return -1;
         tmp[i] = c;
      }
   }
   return 0;
}

/* ---- PBP ---------------------------------------------------------------- */

static uint32_t rd32(const unsigned char *p)
{
   return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
          (uint32_t)p[3] << 24;
}
static uint16_t rd16(const unsigned char *p)
{
   return (uint16_t)(p[0] | p[1] << 8);
}
static void wr32(unsigned char *p, uint32_t v)
{
   p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
   p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void wr16(unsigned char *p, uint16_t v)
{
   p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

#define PBP_HDR 0x28u

int xw_pbp_split(const xw_buf *pbp, const unsigned char *sect[XW_NSECT],
                 size_t len[XW_NSECT])
{
   uint32_t off[XW_NSECT + 1];
   int i;
   if (pbp->len < PBP_HDR || memcmp(pbp->data, "\0PBP", 4) != 0)
      return -1;
   for (i = 0; i < XW_NSECT; i++)
      off[i] = rd32(pbp->data + 8 + 4 * i);
   off[XW_NSECT] = (uint32_t)pbp->len;
   for (i = 0; i < XW_NSECT; i++)
   {
      if (off[i] < PBP_HDR || off[i] > off[i + 1] || off[i + 1] > pbp->len)
         return -1;
      sect[i] = pbp->data + off[i];
      len[i] = off[i + 1] - off[i];
   }
   return 0;
}

int xw_pbp_join(const unsigned char *const sect[XW_NSECT],
                const size_t len[XW_NSECT], xw_buf *out)
{
   size_t total = PBP_HDR, at;
   int i;
   for (i = 0; i < XW_NSECT; i++)
      total += len[i];
   out->data = (unsigned char *)calloc(1, total);
   if (!out->data)
      return -1;
   out->len = total;
   memcpy(out->data, "\0PBP", 4);
   wr32(out->data + 4, 0x00010000u);
   at = PBP_HDR;
   for (i = 0; i < XW_NSECT; i++)
   {
      wr32(out->data + 8 + 4 * i, (uint32_t)at);
      if (len[i])
         memcpy(out->data + at, sect[i], len[i]);
      at += len[i];
   }
   return 0;
}

/* ---- PARAM.SFO ---------------------------------------------------------
 * header (20 B): "\0PSF", version, key table offset, data table offset, count
 * index (16 B each): key offset (u16), format (u16: 0x0204 UTF-8 string,
 * 0x0404 int32), used length, max length, data offset. */

#define SFO_MAX_KEYS 32

typedef struct
{
   char key[32];
   uint16_t fmt;
   uint32_t len, max;
   const unsigned char *data;   /* `len` bytes */
   const char *repl;            /* replacement string, or NULL */
} sfo_ent;

static int sfo_parse(const unsigned char *s, size_t n, sfo_ent *e, int *count)
{
   uint32_t kt, dt, c, i;
   if (n < 20 || memcmp(s, "\0PSF", 4) != 0)
      return -1;
   kt = rd32(s + 8);
   dt = rd32(s + 12);
   c = rd32(s + 16);
   if (c > SFO_MAX_KEYS || 20 + 16 * c > n || kt > n || dt > n)
      return -1;
   for (i = 0; i < c; i++)
   {
      const unsigned char *x = s + 20 + 16 * i;
      uint32_t ko = kt + rd16(x), dofs = dt + rd32(x + 12);
      size_t kl;
      e[i].fmt = rd16(x + 2);
      e[i].len = rd32(x + 4);
      e[i].max = rd32(x + 8);
      if (ko >= n || dofs > n || e[i].len > e[i].max || dofs + e[i].max > n)
         return -1;
      kl = strnlen((const char *)s + ko, n - ko);
      if (kl >= sizeof(e[i].key))
         return -1;
      memcpy(e[i].key, s + ko, kl);
      e[i].key[kl] = '\0';
      e[i].data = s + dofs;
      e[i].repl = NULL;
   }
   *count = (int)c;
   return 0;
}

int xw_sfo_get_string(const unsigned char *sfo, size_t len, const char *key,
                      char *out, size_t cap)
{
   sfo_ent e[SFO_MAX_KEYS];
   int n, i;
   if (sfo_parse(sfo, len, e, &n) != 0)
      return -1;
   for (i = 0; i < n; i++)
      if (strcmp(e[i].key, key) == 0 && e[i].fmt == 0x0204)
      {
         size_t l = strnlen((const char *)e[i].data, e[i].len);
         if (l >= cap)
            return -1;
         memcpy(out, e[i].data, l);
         out[l] = '\0';
         return 0;
      }
   return -1;
}

int xw_sfo_set_string(const unsigned char *sfo, size_t len, const char *key,
                      const char *value, unsigned max_len, xw_buf *out)
{
   sfo_ent e[SFO_MAX_KEYS];
   int n, i, found = -1;
   size_t keys = 0, data = 0, kt, dt, total, ko, dofs;
   uint32_t vlen = (uint32_t)strlen(value) + 1;
   if (sfo_parse(sfo, len, e, &n) != 0)
      return -1;
   for (i = 0; i < n; i++)
      if (strcmp(e[i].key, key) == 0)
         found = i;
   if (found < 0 || e[found].fmt != 0x0204)
      return -1;               /* the template always has TITLE */
   if (vlen > max_len)
      return -2;               /* too long for the field */
   /* The field's room as the SDK's mksfoex sizes it: the value rounded up
    * to 4 bytes, so an entry's PARAM.SFO is byte-identical to one the SDK
    * would write for the same title (tools/test_core.sh checks). */
   max_len = (vlen + 3u) & ~3u;
   e[found].repl = value;
   e[found].len = vlen;
   e[found].max = max_len;
   for (i = 0; i < n; i++)
   {
      keys += strlen(e[i].key) + 1;
      data += e[i].max;
   }
   kt = 20 + 16 * (size_t)n;
   dt = kt + ((keys + 3) & ~(size_t)3);
   total = dt + data;
   out->data = (unsigned char *)calloc(1, total);
   if (!out->data)
      return -1;
   out->len = total;
   memcpy(out->data, sfo, 8);                  /* magic + version */
   wr32(out->data + 8, (uint32_t)kt);
   wr32(out->data + 12, (uint32_t)dt);
   wr32(out->data + 16, (uint32_t)n);
   ko = 0;
   dofs = 0;
   for (i = 0; i < n; i++)
   {
      unsigned char *x = out->data + 20 + 16 * i;
      size_t kl = strlen(e[i].key) + 1;
      wr16(x, (uint16_t)ko);
      wr16(x + 2, e[i].fmt);
      wr32(x + 4, e[i].len);
      wr32(x + 8, e[i].max);
      wr32(x + 12, (uint32_t)dofs);
      memcpy(out->data + kt + ko, e[i].key, kl);
      if (e[i].repl)
         memcpy(out->data + dt + dofs, e[i].repl, e[i].len);
      else
         memcpy(out->data + dt + dofs, e[i].data, e[i].len);
      ko += kl;
      dofs += e[i].max;
   }
   return 0;
}

/* ---- art ---------------------------------------------------------------- */

static void png_sink(void *ctx, void *data, int size)
{
   xw_buf *b = (xw_buf *)ctx;
   unsigned char *n = (unsigned char *)realloc(b->data, b->len + (size_t)size);
   if (!n)
      return;
   memcpy(n + b->len, data, (size_t)size);
   b->data = n;
   b->len += (size_t)size;
}

/* One RGBA sample of `src` (sw x sh) over the source rectangle [x0,x1) x
 * [y0,y1): an area average when shrinking, bilinear when enlarging. */
static void sample(const unsigned char *src, int sw, int sh, double x0,
                   double y0, double x1, double y1, unsigned char *px)
{
   double acc[4] = { 0, 0, 0, 0 }, wsum = 0;
   int xa, xb, ya, yb, x, y, c;
   if (x1 - x0 < 1.0 && y1 - y0 < 1.0)
   {
      /* enlarging: bilinear at the centre */
      double cx = (x0 + x1) / 2 - 0.5, cy = (y0 + y1) / 2 - 0.5, fx, fy;
      int ix, iy, ix1, iy1;
      if (cx < 0) cx = 0;
      if (cy < 0) cy = 0;
      ix = (int)cx; iy = (int)cy;
      fx = cx - ix; fy = cy - iy;
      ix1 = ix + 1 < sw ? ix + 1 : sw - 1;
      iy1 = iy + 1 < sh ? iy + 1 : sh - 1;
      if (ix >= sw) ix = sw - 1;
      if (iy >= sh) iy = sh - 1;
      for (c = 0; c < 4; c++)
      {
         double a = src[(iy * sw + ix) * 4 + c], b = src[(iy * sw + ix1) * 4 + c];
         double d = src[(iy1 * sw + ix) * 4 + c], e = src[(iy1 * sw + ix1) * 4 + c];
         double v = (a * (1 - fx) + b * fx) * (1 - fy) + (d * (1 - fx) + e * fx) * fy;
         px[c] = (unsigned char)(v + 0.5);
      }
      return;
   }
   xa = (int)x0; xb = (int)(x1 + 0.999999); ya = (int)y0; yb = (int)(y1 + 0.999999);
   if (xb > sw) xb = sw;
   if (yb > sh) yb = sh;
   for (y = ya; y < yb; y++)
   {
      double wy = (y + 1 < y1 ? y + 1 : y1) - (y > y0 ? y : y0);
      for (x = xa; x < xb; x++)
      {
         double wx = (x + 1 < x1 ? x + 1 : x1) - (x > x0 ? x : x0);
         double w = wx * wy;
         const unsigned char *s = src + (y * sw + x) * 4;
         if (w <= 0)
            continue;
         for (c = 0; c < 4; c++)
            acc[c] += s[c] * w;
         wsum += w;
      }
   }
   for (c = 0; c < 4; c++)
      px[c] = (unsigned char)(wsum > 0 ? acc[c] / wsum + 0.5 : 0);
}

static uint32_t be32(const unsigned char *p)
{
   return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A PNG that is already w x h and whole: every chunk inside the file with a
 * right CRC, IHDR first, IEND last, 8-bit RGB/RGBA/grey/palette, not
 * interlaced.  A truncated or damaged file fails here and goes through the
 * decoder below, which reports it. */
static int png_as_is(const xw_buf *f, int w, int h)
{
   size_t at = 8;
   int first = 1;
   if (f->len < 8 + 25 + 12 || memcmp(f->data, "\x89PNG\r\n\x1a\n", 8) != 0)
      return 0;
   if (memcmp(f->data + 12, "IHDR", 4) != 0 || be32(f->data + 8) != 13 ||
       be32(f->data + 16) != (uint32_t)w || be32(f->data + 20) != (uint32_t)h ||
       f->data[24] != 8 || f->data[28] != 0)       /* bit depth 8, no interlace */
      return 0;
   while (at + 12 <= f->len)
   {
      uint32_t n = be32(f->data + at);
      const unsigned char *type = f->data + at + 4;
      if (n > f->len - at - 12)
         return 0;                                  /* runs past the end */
      if (be32(f->data + at + 8 + n) != (uint32_t)xw_crc32(0, type, (size_t)n + 4))
         return 0;                                  /* damaged */
      if (first && memcmp(type, "IHDR", 4) != 0)
         return 0;
      first = 0;
      at += 12 + n;
      if (!memcmp(type, "IEND", 4))
         return at == f->len;
   }
   return 0;                                        /* no IEND: truncated */
}

int xw_image_png(const char *path, int w, int h, int cover, xw_buf *png)
{
   xw_buf file;
   unsigned char *src, *dst;
   int sw, sh, comp, x, y, ox, oy, dw, dh;
   double scale, cx0, cy0;
   png->data = NULL;
   png->len = 0;
   if (xw_read_file(path, &file) != 0)
      return -1;
   if (png_as_is(&file, w, h))
   {
      *png = file;           /* already w x h: its bytes, owned by the caller now */
      return 0;
   }
   src = stbi_load_from_memory(file.data, (int)file.len, &sw, &sh, &comp, 4);
   xw_buf_free(&file);
   if (!src || sw <= 0 || sh <= 0)
      return -2;
   dst = (unsigned char *)calloc((size_t)w * h, 4);   /* transparent */
   if (!dst)
   {
      stbi_image_free(src);
      return -1;
   }
   if (cover)
      scale = (double)w / sw > (double)h / sh ? (double)w / sw : (double)h / sh;
   else
      scale = (double)w / sw < (double)h / sh ? (double)w / sw : (double)h / sh;
   dw = (int)(sw * scale + 0.5);
   dh = (int)(sh * scale + 0.5);
   if (dw < 1) dw = 1;
   if (dh < 1) dh = 1;
   ox = (w - dw) / 2;           /* negative when cropping */
   oy = (h - dh) / 2;
   cx0 = 0;
   cy0 = 0;
   for (y = 0; y < h; y++)
   {
      int ry = y - oy;
      if (ry < 0 || ry >= dh)
         continue;
      for (x = 0; x < w; x++)
      {
         int rx = x - ox;
         if (rx < 0 || rx >= dw)
            continue;
         sample(src, sw, sh, cx0 + rx / scale, cy0 + ry / scale,
                cx0 + (rx + 1) / scale, cy0 + (ry + 1) / scale,
                dst + ((size_t)y * w + x) * 4);
      }
   }
   stbi_image_free(src);
   if (!stbi_write_png_to_func(png_sink, png, w, h, 4, dst, w * 4) || !png->data)
   {
      free(dst);
      xw_buf_free(png);
      return -1;
   }
   free(dst);
   return 0;
}

/* ---- the entry ---------------------------------------------------------- */

void xw_folder_from_title(const char *title, char *out, size_t cap)
{
   size_t n = 0;
   const unsigned char *p = (const unsigned char *)title;
   int space = 0;
   for (; *p && n + 1 < cap && n < 32; p++)
   {
      unsigned c = *p;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')
      {
         if (space && n)
            out[n++] = '_';
         space = 0;
         if (n + 1 < cap)
            out[n++] = (char)c;
      }
      else
         space = 1;
   }
   if (!n)
   {
      snprintf(out, cap, "GBAdhoc_game");
      return;
   }
   out[n] = '\0';
}

static void join(char *out, size_t cap, const char *a, const char *b)
{
   size_t n = strlen(a);
   int sep = n && a[n - 1] != '/' && a[n - 1] != '\\';
   snprintf(out, cap, "%s%s%s", a, sep ? "/" : "", b);
}

static int ieq(char a, char b)
{
   if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
   if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
   if (a == '\\') a = '/';
   if (b == '\\') b = '/';
   return a == b;
}

/* Case-insensitive, either slash: where `needle` starts in `hay`, or NULL. */
static const char *ifind(const char *hay, const char *needle)
{
   size_t n = strlen(needle);
   for (; *hay; hay++)
   {
      size_t i;
      for (i = 0; i < n && hay[i] && ieq(hay[i], needle[i]); i++)
         ;
      if (i == n)
         return hay;
   }
   return NULL;
}

int xw_infer(const char *rom_path, char *root, size_t root_cap, char *gbadhoc,
             size_t gb_cap, char *rel, size_t rel_cap)
{
   const char *g = ifind(rom_path, "/PSP/GAME/"), *f, *r;
   size_t i;
   if (!g)
      return -1;
   f = g + 10;                                  /* the install folder */
   r = f;
   while (*r && *r != '/' && *r != '\\')
      r++;
   if (r == f || !*r || !ifind(r, "/roms/") || ifind(r, "/roms/") != r)
      return -1;
   snprintf(root, root_cap, "%.*s/", (int)(g - rom_path), rom_path);
   snprintf(gbadhoc, gb_cap, "PSP/GAME/%.*s", (int)(r - f), f);
   snprintf(rel, rel_cap, "%s", r + 6);
   for (i = 0; rel[i]; i++)
      if (rel[i] == '\\')
         rel[i] = '/';
   return rel[0] ? 0 : -1;
}

int xw_rom_for_psp(const char *rom_path, const char *rel, char *out, size_t cap)
{
   size_t i;
   snprintf(out, cap, "%s", rel);
   for (i = 0; rel[i]; i++)
      if ((unsigned char)rel[i] >= 0x80)
         break;
   if (!rel[i])
      return 0;                      /* plain ASCII: the long name is fine */
#ifdef _WIN32
   {
      /* The PSP firmware may not open a non-ASCII long name ("Pokémon");
       * GBAdhoc's list opens those by their FAT 8.3 names (issue #19).  The
       * card reports the 8.3 form itself. */
      wchar_t *wl = wide(rom_path), sp[1024];
      char s8[2048];
      DWORD n = wl ? GetShortPathNameW(wl, sp, 1024) : 0;
      free(wl);
      if (n > 0 && n < 1024 &&
          WideCharToMultiByte(CP_UTF8, 0, sp, -1, s8, sizeof(s8), NULL, NULL))
      {
         const char *rr = ifind(s8, "/roms/");
         if (rr)
         {
            snprintf(out, cap, "%s", rr + 6);
            for (i = 0; out[i]; i++)
               if (out[i] == '\\')
                  out[i] = '/';
            return 0;
         }
      }
   }
#else
   (void)rom_path;
#endif
   return 1;                         /* left long: may not open on the PSP */
}

static int exists(const char *path)
{
   FILE *f = xw_fopen(path, "rb");
   if (f)
      fclose(f);
   return f != NULL;
}

void xw_title_from_rom(const char *rom_rel, char *out, size_t cap)
{
   const char *s = strrchr(rom_rel, '/'), *dot;
   s = s ? s + 1 : rom_rel;
   dot = strrchr(s, '.');
   snprintf(out, cap, "%.*s", dot && dot > s ? (int)(dot - s) : (int)strlen(s), s);
}

void xw_default_art(const char *install, const char *rom_rel, char *hero,
                    size_t hero_cap, char *box, size_t box_cap)
{
   static const char *ext[] = { "png", "bmp", "jpg", "jpeg" };
   char stem[512], p[1200];
   size_t i;
   xw_title_from_rom(rom_rel, stem, sizeof(stem));
   hero[0] = box[0] = '\0';
   snprintf(p, sizeof(p), "%s/hero/%s.png", install, stem);
   if (exists(p))
      snprintf(hero, hero_cap, "%s", p);
   for (i = 0; i < sizeof(ext) / sizeof(ext[0]); i++)
   {
      snprintf(p, sizeof(p), "%s/boxart/%s.%s", install, stem, ext[i]);
      if (exists(p))
      {
         snprintf(box, box_cap, "%s", p);
         break;
      }
   }
}

int xw_png_size(const unsigned char *png, size_t len, int *w, int *h)
{
   if (len < 24 || memcmp(png, "\x89PNG\r\n\x1a\n", 8) != 0 ||
       memcmp(png + 12, "IHDR", 4) != 0)
      return -1;
   *w = (int)((uint32_t)png[16] << 24 | (uint32_t)png[17] << 16 | (uint32_t)png[18] << 8 | png[19]);
   *h = (int)((uint32_t)png[20] << 24 | (uint32_t)png[21] << 16 | (uint32_t)png[22] << 8 | png[23]);
   return 0;
}

int xw_build_pbp(const xw_entry *e, const xw_buf *icon_png,
                 const xw_buf *pic_png, xw_buf *pbp, char *err, size_t err_cap)
{
   xw_buf tpl = { (unsigned char *)e->template_pbp, e->template_len };
   const unsigned char *sect[XW_NSECT];
   size_t len[XW_NSECT];
   xw_buf sfo = { 0, 0 }, icon = { 0, 0 }, pic = { 0, 0 };
   int rc = -1, r, w, h;

   pbp->data = NULL;
   pbp->len = 0;
   if (!e->title || !e->title[0])
   {
      snprintf(err, err_cap, "A title is needed.");
      return -1;
   }
   if (xw_pbp_split(&tpl, sect, len) != 0 || !len[XW_SFO] || !len[XW_PSP])
   {
      snprintf(err, err_cap, "The launcher template is not a valid EBOOT.PBP.");
      return -1;
   }
   r = xw_sfo_set_string(sect[XW_SFO], len[XW_SFO], "TITLE", e->title, 128, &sfo);
   if (r != 0)
   {
      snprintf(err, err_cap, r == -2 ? "The title is too long (127 bytes at most)."
                                     : "The launcher template's PARAM.SFO has no TITLE.");
      goto out;
   }
   sect[XW_SFO] = sfo.data;
   len[XW_SFO] = sfo.len;
   if (icon_png && icon_png->len)
   {
      if (xw_png_size(icon_png->data, icon_png->len, &w, &h) != 0 || w != 144 || h != 80)
      {
         snprintf(err, err_cap, "The icon is not a 144x80 PNG.");
         goto out;
      }
      sect[XW_ICON0] = icon_png->data;
      len[XW_ICON0] = icon_png->len;
   }
   else if (e->icon_image && e->icon_image[0])
   {
      if (xw_image_png(e->icon_image, 144, 80, 0, &icon) != 0)
      {
         snprintf(err, err_cap, "Cannot read the icon image: %s", e->icon_image);
         goto out;
      }
      sect[XW_ICON0] = icon.data;
      len[XW_ICON0] = icon.len;
   }
   if (pic_png && pic_png->len)
   {
      if (xw_png_size(pic_png->data, pic_png->len, &w, &h) != 0 || w != 480 || h != 272)
      {
         snprintf(err, err_cap, "The background is not a 480x272 PNG.");
         goto out;
      }
      sect[XW_PIC1] = pic_png->data;
      len[XW_PIC1] = pic_png->len;
   }
   else if (e->bg_image && e->bg_image[0])
   {
      if (xw_image_png(e->bg_image, 480, 272, 1, &pic) != 0)
      {
         snprintf(err, err_cap, "Cannot read the background image: %s", e->bg_image);
         goto out;
      }
      sect[XW_PIC1] = pic.data;
      len[XW_PIC1] = pic.len;
   }
   /* the template's own ICON1/PIC0/SND0/DATA.PSAR stay as they are (none) */
   if (xw_pbp_join(sect, len, pbp) != 0)
   {
      snprintf(err, err_cap, "Out of memory building EBOOT.PBP.");
      goto out;
   }
   rc = 0;
out:
   xw_buf_free(&sfo);
   xw_buf_free(&icon);
   xw_buf_free(&pic);
   return rc;
}

void xw_launch_ini(const xw_entry *e, char *out, size_t cap)
{
   snprintf(out, cap,
            "# A GBAdhoc game entry (made by xmbwrap; docs/XMB-LAUNCH.md).\n"
            "# rom: relative to GBAdhoc's roms folder.  gbadhoc: GBAdhoc's folder,\n"
            "# looked for on this entry's device first, then the other one.\n"
            "rom = %s\n"
            "gbadhoc = %s\n",
            e->rom, e->gbadhoc && e->gbadhoc[0] ? e->gbadhoc : "PSP/GAME/GBAdhoc");
}

int xw_make_entry(const xw_entry *e, char *made, size_t made_cap, char *err,
                  size_t err_cap)
{
   xw_buf pbp = { 0, 0 };
   char folder[64], dir[1024], path[1100], ini[1400];
   int rc = -1;

   if (!e->title || !e->title[0] || !e->rom || !e->rom[0] || !e->root)
   {
      snprintf(err, err_cap, "A title, a ROM and a destination are needed.");
      return -1;
   }
   if (xw_build_pbp(e, NULL, NULL, &pbp, err, err_cap) != 0)
      return -1;
   if (e->folder && e->folder[0])
      snprintf(folder, sizeof(folder), "%s", e->folder);
   else
      xw_folder_from_title(e->title, folder, sizeof(folder));
   join(path, sizeof(path), e->root, "PSP/GAME");
   join(dir, sizeof(dir), path, folder);
   if (xw_mkdirs(dir) != 0)
   {
      snprintf(err, err_cap, "Cannot create the folder %s", dir);
      goto out;
   }
   join(path, sizeof(path), dir, "EBOOT.PBP");
   if (xw_write_file(path, pbp.data, pbp.len) != 0)
   {
      snprintf(err, err_cap, "Cannot write %s", path);
      goto out;
   }
   xw_launch_ini(e, ini, sizeof(ini));
   join(path, sizeof(path), dir, "launch.ini");
   if (xw_write_file(path, ini, strlen(ini)) != 0)
   {
      snprintf(err, err_cap, "Cannot write %s", path);
      goto out;
   }
   snprintf(made, made_cap, "%s", dir);
   rc = 0;
out:
   xw_buf_free(&pbp);
   return rc;
}

/* "key = value" lines, '#' comments, either line ending. */
static int ini_get(const char *text, const char *key, char *out, size_t cap)
{
   size_t kl = strlen(key);
   const char *p = text;
   out[0] = '\0';
   while (p && *p)
   {
      const char *eol = strpbrk(p, "\r\n"), *q = p, *v;
      size_t n;
      while (*q == ' ' || *q == '\t')
         q++;
      if (*q != '#' && strncmp(q, key, kl) == 0)
      {
         v = q + kl;
         while (*v == ' ' || *v == '\t')
            v++;
         if (*v == '=')
         {
            v++;
            while (*v == ' ' || *v == '\t')
               v++;
            n = eol ? (size_t)(eol - v) : strlen(v);
            while (n && (v[n - 1] == ' ' || v[n - 1] == '\t'))
               n--;
            if (n >= cap)
               n = cap - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
         }
      }
      if (!eol)
         break;
      p = eol + 1;
   }
   return -1;
}

int xw_parse_launch_ini(const char *text, char *rom, size_t rom_cap,
                        char *gbadhoc, size_t gb_cap)
{
   ini_get(text, "gbadhoc", gbadhoc, gb_cap);
   return ini_get(text, "rom", rom, rom_cap) == 0 && rom[0] ? 0 : -1;
}

int xw_pbp_section_file(const char *pbp_path, int sect_i, xw_buf *out)
{
   xw_buf f;
   const unsigned char *sect[XW_NSECT];
   size_t len[XW_NSECT];
   out->data = NULL;
   out->len = 0;
   if (sect_i < 0 || sect_i >= XW_NSECT || xw_read_file(pbp_path, &f) != 0)
      return -1;
   if (xw_pbp_split(&f, sect, len) != 0)
   {
      xw_buf_free(&f);
      return -1;
   }
   if (len[sect_i])
   {
      out->data = (unsigned char *)malloc(len[sect_i]);
      if (!out->data)
      {
         xw_buf_free(&f);
         return -1;
      }
      memcpy(out->data, sect[sect_i], len[sect_i]);
      out->len = len[sect_i];
   }
   xw_buf_free(&f);
   return 0;
}

int xw_read_entry(const char *dir, const unsigned char *template_pbp,
                  size_t template_len, xw_entry_info *info)
{
   char path[1100];
   xw_buf f, ini;
   const unsigned char *sect[XW_NSECT], *tsect[XW_NSECT];
   size_t len[XW_NSECT], tlen[XW_NSECT];
   memset(info, 0, sizeof(*info));
   info->launcher = -1;
   join(path, sizeof(path), dir, "launch.ini");
   if (xw_read_file(path, &ini) != 0)
      return -1;
   {
      char *t = (char *)malloc(ini.len + 1);
      if (!t)
      {
         xw_buf_free(&ini);
         return -1;
      }
      memcpy(t, ini.data, ini.len);
      t[ini.len] = '\0';
      xw_parse_launch_ini(t, info->rom, sizeof(info->rom), info->gbadhoc,
                          sizeof(info->gbadhoc));
      free(t);
   }
   xw_buf_free(&ini);
   join(path, sizeof(path), dir, "EBOOT.PBP");
   if (xw_read_file(path, &f) != 0)
      return -1;
   info->pbp_len = f.len;
   if (xw_pbp_split(&f, sect, len) != 0)
   {
      xw_buf_free(&f);
      return -1;
   }
   xw_sfo_get_string(sect[XW_SFO], len[XW_SFO], "TITLE", info->title,
                     sizeof(info->title));
   info->has_icon = len[XW_ICON0] > 0;
   info->has_pic = len[XW_PIC1] > 0;
   if (template_pbp)
   {
      xw_buf t = { (unsigned char *)template_pbp, template_len };
      info->launcher = xw_pbp_split(&t, tsect, tlen) == 0 &&
                       tlen[XW_PSP] == len[XW_PSP] &&
                       memcmp(tsect[XW_PSP], sect[XW_PSP], len[XW_PSP]) == 0;
   }
   xw_buf_free(&f);
   return 0;
}

unsigned long xw_crc32(unsigned long crc, const void *data, size_t len)
{
   static uint32_t table[256];
   static int ready;
   const unsigned char *p = (const unsigned char *)data;
   uint32_t c = (uint32_t)crc ^ 0xffffffffu;
   if (!ready)
   {
      uint32_t i, k, v;
      for (i = 0; i < 256; i++)
      {
         v = i;
         for (k = 0; k < 8; k++)
            v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
         table[i] = v;
      }
      ready = 1;
   }
   while (len--)
      c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
   return (unsigned long)(c ^ 0xffffffffu);
}
