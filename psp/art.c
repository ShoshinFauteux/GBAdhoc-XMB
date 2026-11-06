/* art.c -- see art.h. */
#include "art.h"
#include "theme.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "util.h"
#include "xf.h"
#include "gfx.h"
#include "gen/xmb_font_ids.h"
#include "../shared/xw_core.h"
#define STBI_NO_STDIO
#include "../shared/third_party/stb_image.h"
#include <zlib.h>

#define MAX_SRC_PX (2400 * 2400)

void img_free(image *im)
{
   free(im->px);
   im->px = NULL;
   im->w = im->h = 0;
}

int img_alloc(image *im, int w, int h)
{
   im->px = (uint32_t *)calloc((size_t)w * h, 4);
   im->w = w;
   im->h = h;
   return im->px ? 0 : -1;
}

int img_load_mem(const unsigned char *d, size_t n, image *out)
{
   int w, h, c;
   unsigned char *p;
   out->px = NULL;
   if (!stbi_info_from_memory(d, (int)n, &w, &h, &c) || (long)w * h > MAX_SRC_PX)
      return -2;
   p = stbi_load_from_memory(d, (int)n, &w, &h, &c, 4);
   if (!p)
      return -2;
   out->px = (uint32_t *)p;     /* stbi_image_free == free (default allocator) */
   out->w = w;
   out->h = h;
   return 0;
}

int img_load(const char *path, image *out, int pbp_sect)
{
   unsigned char *d;
   size_t n;
   int rc;
   out->px = NULL;
   if (read_all(path, &d, &n) != 0)
      return -1;
   if (pbp_sect)
   {
      xw_buf b = { d, n };
      const unsigned char *s[XW_NSECT];
      size_t l[XW_NSECT];
      if (xw_pbp_split(&b, s, l) != 0 || !l[pbp_sect])
      {
         free(d);
         return -1;
      }
      rc = img_load_mem(s[pbp_sect], l[pbp_sect], out);
   }
   else
      rc = img_load_mem(d, n, out);
   free(d);
   return rc;
}

/* premultiplied area-average resample of the source rect (fx0,fy0)-(fx1,fy1)
 * into dst (dw x dh) */
/* Shrinking on both axes: the same area average done separably (rows, then
 * columns) with each output column's source span and edge weights worked
 * out once -- a 1376x768 hero to 480x272 drops from ~4 s to well under one
 * on the Allegrex.  Premultiplied, so transparent edges do not darken. */
static int resample_sep(const image *s, float fx0, float fy0, float fx1, float fy1, uint32_t *dst, int dw, int dh,
                        int dstride)
{
   float kx = (fx1 - fx0) / dw, ky = (fy1 - fy0) / dh;
   int x, y, *xa, *xb;
   float *wa, *wb, *acc;
   xa = (int *)malloc(sizeof(int) * (size_t)dw * 2);
   wa = (float *)malloc(sizeof(float) * (size_t)dw * 2);
   acc = (float *)malloc(sizeof(float) * 4 * (size_t)dw);
   if (!xa || !wa || !acc)
   {
      free(xa); free(wa); free(acc);
      return -1;
   }
   xb = xa + dw;
   wb = wa + dw;
   for (x = 0; x < dw; x++)
   {
      float a0 = fx0 + x * kx, a1 = a0 + kx;
      xa[x] = (int)a0;
      xb[x] = (int)ceilf(a1);
      if (xb[x] > s->w) xb[x] = s->w;
      if (xa[x] < 0) xa[x] = 0;
      wa[x] = (xa[x] + 1) - a0;
      wb[x] = a1 - (xb[x] - 1);
   }
   for (y = 0; y < dh; y++)
   {
      float a0 = fy0 + y * ky, a1 = a0 + ky;
      int ya = (int)a0, yb = (int)ceilf(a1), i;
      if (ya < 0) ya = 0;
      if (yb > s->h) yb = s->h;
      memset(acc, 0, sizeof(float) * 4 * (size_t)dw);
      for (i = ya; i < yb; i++)
      {
         const uint32_t *row = s->px + (size_t)i * s->w;
         float wy = yb - ya == 1 ? ky : i == ya ? (ya + 1) - a0 : i == yb - 1 ? a1 - (yb - 1) : 1.0f;
         for (x = 0; x < dw; x++)
         {
            float r = 0, g = 0, b = 0, al = 0, *o = acc + x * 4;
            int j, n = xb[x] - xa[x];
            for (j = xa[x]; j < xb[x]; j++)
            {
               uint32_t q = row[j];
               float w = n == 1 ? kx : j == xa[x] ? wa[x] : j == xb[x] - 1 ? wb[x] : 1.0f;
               float aa = (float)(q >> 24) * w;
               r += (q & 0xFF) * aa; g += (q >> 8 & 0xFF) * aa; b += (q >> 16 & 0xFF) * aa; al += aa;
            }
            o[0] += r * wy; o[1] += g * wy; o[2] += b * wy; o[3] += al * wy;
         }
      }
      for (x = 0; x < dw; x++)
      {
         float *o = acc + x * 4;
         if (o[3] > 0)
         {
            float A = o[3] / (kx * ky);
            dst[y * dstride + x] = (uint32_t)fminf(255, o[0] / o[3] + 0.5f) | (uint32_t)fminf(255, o[1] / o[3] + 0.5f) << 8 |
                                   (uint32_t)fminf(255, o[2] / o[3] + 0.5f) << 16 | (uint32_t)fminf(255, A + 0.5f) << 24;
         }
         else
            dst[y * dstride + x] = 0;
      }
   }
   free(xa); free(wa); free(acc);
   return 0;
}

static void resample(const image *s, float fx0, float fy0, float fx1, float fy1, uint32_t *dst, int dw, int dh,
                     int dstride)
{
   int x, y;
   float kx = (fx1 - fx0) / dw, ky = (fy1 - fy0) / dh;
   if (kx >= 1 && ky >= 1 && resample_sep(s, fx0, fy0, fx1, fy1, dst, dw, dh, dstride) == 0)
      return;
   for (y = 0; y < dh; y++)
   {
      float sy0 = fy0 + y * ky, sy1 = sy0 + ky;
      for (x = 0; x < dw; x++)
      {
         float sx0 = fx0 + x * kx, sx1 = sx0 + kx;
         float acc[4] = { 0, 0, 0, 0 }, ws = 0;
         int ix, iy;
         if (kx < 1 && ky < 1)
         {
            /* enlarging: bilinear */
            float cx = (sx0 + sx1) / 2 - 0.5f, cy = (sy0 + sy1) / 2 - 0.5f, fx, fy;
            int x0, y0, x1, y1, c;
            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;
            x0 = (int)cx; y0 = (int)cy;
            fx = cx - x0; fy = cy - y0;
            if (x0 >= s->w) x0 = s->w - 1;
            if (y0 >= s->h) y0 = s->h - 1;
            x1 = x0 + 1 < s->w ? x0 + 1 : x0;
            y1 = y0 + 1 < s->h ? y0 + 1 : y0;
            {
               uint32_t p[4] = { s->px[y0 * s->w + x0], s->px[y0 * s->w + x1], s->px[y1 * s->w + x0], s->px[y1 * s->w + x1] };
               float w4[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
               for (c = 0; c < 4; c++)
               {
                  float a = (float)(p[c] >> 24) / 255.0f * w4[c];
                  acc[0] += (p[c] & 0xFF) * a;
                  acc[1] += (p[c] >> 8 & 0xFF) * a;
                  acc[2] += (p[c] >> 16 & 0xFF) * a;
                  acc[3] += a;
               }
               ws = 1;
            }
         }
         else
         {
            int xa = (int)sx0, xb = (int)ceilf(sx1), ya = (int)sy0, yb = (int)ceilf(sy1);
            if (xb > s->w) xb = s->w;
            if (yb > s->h) yb = s->h;
            if (xa < 0) xa = 0;
            if (ya < 0) ya = 0;
            for (iy = ya; iy < yb; iy++)
            {
               float wy = fminf(iy + 1.0f, sy1) - fmaxf((float)iy, sy0);
               const uint32_t *row = s->px + (size_t)iy * s->w;
               if (wy <= 0) continue;
               for (ix = xa; ix < xb; ix++)
               {
                  float wx = fminf(ix + 1.0f, sx1) - fmaxf((float)ix, sx0), w = wx * wy, a;
                  uint32_t p = row[ix];
                  if (w <= 0) continue;
                  a = (float)(p >> 24) / 255.0f * w;
                  acc[0] += (p & 0xFF) * a;
                  acc[1] += (p >> 8 & 0xFF) * a;
                  acc[2] += (p >> 16 & 0xFF) * a;
                  acc[3] += a;
                  ws += w;
               }
            }
         }
         if (ws > 0 && acc[3] > 0)
         {
            float A = acc[3] / ws;
            uint32_t r = (uint32_t)fminf(255, acc[0] / acc[3] + 0.5f), g = (uint32_t)fminf(255, acc[1] / acc[3] + 0.5f);
            uint32_t b = (uint32_t)fminf(255, acc[2] / acc[3] + 0.5f), a = (uint32_t)fminf(255, A * 255 + 0.5f);
            dst[y * dstride + x] = r | g << 8 | b << 16 | a << 24;
         }
         else
            dst[y * dstride + x] = 0;
      }
   }
}

int img_fit(const image *src, int w, int h, int cover, image *out)
{
   float sc, dw, dh;
   if (img_alloc(out, w, h) != 0)
      return -1;
   sc = cover ? fmaxf((float)w / src->w, (float)h / src->h) : fminf((float)w / src->w, (float)h / src->h);
   dw = src->w * sc;
   dh = src->h * sc;
   if (cover)
   {
      /* the visible source rect */
      float sw = w / sc, sh = h / sc;
      resample(src, (src->w - sw) / 2, (src->h - sh) / 2, (src->w + sw) / 2, (src->h + sh) / 2, out->px, w, h, w);
   }
   else
   {
      int iw = (int)(dw + 0.5f), ih = (int)(dh + 0.5f), ox, oy;
      if (iw < 1) iw = 1;
      if (ih < 1) ih = 1;
      if (iw > w) iw = w;
      if (ih > h) ih = h;
      ox = (w - iw) / 2;
      oy = (h - ih) / 2;
      resample(src, 0, 0, (float)src->w, (float)src->h, out->px + oy * w + ox, iw, ih, w);
   }
   return 0;
}

static inline uint32_t over(uint32_t d, uint32_t s)
{
   uint32_t sa = s >> 24, da, oa, r, g, b;
   if (sa == 255) return s;
   if (sa == 0) return d;
   da = d >> 24;
   oa = sa + da * (255 - sa) / 255;
   if (!oa) return 0;
   r = ((s & 0xFF) * sa + (d & 0xFF) * da * (255 - sa) / 255) / oa;
   g = ((s >> 8 & 0xFF) * sa + (d >> 8 & 0xFF) * da * (255 - sa) / 255) / oa;
   b = ((s >> 16 & 0xFF) * sa + (d >> 16 & 0xFF) * da * (255 - sa) / 255) / oa;
   return r | g << 8 | b << 16 | oa << 24;
}

void img_draw(image *dst, const image *src, float x, float y, float w, float h)
{
   image t;
   int iw = (int)(w + 0.5f), ih = (int)(h + 0.5f), X0 = (int)floorf(x + 0.5f), Y0 = (int)floorf(y + 0.5f), i, j;
   if (iw <= 0 || ih <= 0 || img_alloc(&t, iw, ih) != 0)
      return;
   resample(src, 0, 0, (float)src->w, (float)src->h, t.px, iw, ih, iw);
   for (j = 0; j < ih; j++)
   {
      int yy = Y0 + j;
      if (yy < 0 || yy >= dst->h) continue;
      for (i = 0; i < iw; i++)
      {
         int xx = X0 + i;
         if (xx < 0 || xx >= dst->w) continue;
         dst->px[yy * dst->w + xx] = over(dst->px[yy * dst->w + xx], t.px[j * iw + i]);
      }
   }
   img_free(&t);
}

/* Gaussian by three box passes (each way), premultiplied, edges clamped. */
static void box_pass(uint32_t *px, int w, int h, int r, int horiz)
{
   int n = horiz ? w : h, lines = horiz ? h : w, l, i;
   float *acc = (float *)malloc(sizeof(float) * 4 * (size_t)n);
   if (!acc) return;
   for (l = 0; l < lines; l++)
   {
      float s[4] = { 0, 0, 0, 0 };
      int k;
#define AT(i) px[horiz ? (size_t)l * w + (i) : (size_t)(i) * w + l]
      for (k = -r; k <= r; k++)
      {
         int ii = k < 0 ? 0 : k >= n ? n - 1 : k;
         uint32_t p = AT(ii);
         float a = (float)(p >> 24);
         s[0] += (p & 0xFF) * a; s[1] += (p >> 8 & 0xFF) * a; s[2] += (p >> 16 & 0xFF) * a; s[3] += a;
      }
      for (i = 0; i < n; i++)
      {
         int in = i + r + 1, out = i - r;
         memcpy(acc + 4 * i, s, sizeof(s));
         {
            int ii = in >= n ? n - 1 : in, oo = out < 0 ? 0 : out;
            uint32_t p = AT(ii), q = AT(oo);
            float a = (float)(p >> 24), b = (float)(q >> 24);
            s[0] += (p & 0xFF) * a - (q & 0xFF) * b;
            s[1] += (p >> 8 & 0xFF) * a - (q >> 8 & 0xFF) * b;
            s[2] += (p >> 16 & 0xFF) * a - (q >> 16 & 0xFF) * b;
            s[3] += a - b;
         }
      }
      for (i = 0; i < n; i++)
      {
         float *v = acc + 4 * i, A = v[3];
         uint32_t o = 0;
         if (A > 0.5f)
         {
            float d = 2.0f * r + 1;
            o = (uint32_t)fminf(255, v[0] / A + 0.5f) | (uint32_t)fminf(255, v[1] / A + 0.5f) << 8 |
                (uint32_t)fminf(255, v[2] / A + 0.5f) << 16 | (uint32_t)fminf(255, A / d + 0.5f) << 24;
         }
         AT(i) = o;
      }
#undef AT
   }
   free(acc);
}

void img_blur(image *im, float sigma)
{
   /* three boxes of width w approximate a Gaussian: w = sqrt(12 s^2 / 3 + 1) */
   int r = (int)((sqrtf(4 * sigma * sigma + 1) - 1) / 2 + 0.5f), k;
   if (r < 1) return;
   for (k = 0; k < 3; k++)
   {
      box_pass(im->px, im->w, im->h, r, 1);
      box_pass(im->px, im->w, im->h, r, 0);
   }
}

void img_saturate(image *im, float s)
{
   size_t i, n = (size_t)im->w * im->h;
   float m[9] = { 0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s,
                  0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s,
                  0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s };
   for (i = 0; i < n; i++)
   {
      uint32_t p = im->px[i];
      float r = (float)(p & 0xFF), g = (float)(p >> 8 & 0xFF), b = (float)(p >> 16 & 0xFF);
      float R = m[0] * r + m[1] * g + m[2] * b, G = m[3] * r + m[4] * g + m[5] * b, B = m[6] * r + m[7] * g + m[8] * b;
      im->px[i] = (uint32_t)clampf(R + 0.5f, 0, 255) | (uint32_t)clampf(G + 0.5f, 0, 255) << 8 |
                  (uint32_t)clampf(B + 0.5f, 0, 255) << 16 | (p & 0xFF000000u);
   }
}

/* PNG through zlib (in the SDK image): stb_image_write's own deflate is
 * several times slower on the Allegrex and writes files a third larger.
 * Rows are filtered adaptively (the filter with the smallest sum of
 * absolute bytes), RGB when the image is opaque (PIC1), RGBA otherwise. */
static void be32(unsigned char *p, unsigned v)
{
   p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static unsigned char *chunk(unsigned char *o, const char *type, const unsigned char *d, unsigned n)
{
   unsigned c;
   be32(o, n);
   memcpy(o + 4, type, 4);
   if (n) memcpy(o + 8, d, n);
   c = (unsigned)crc32(0, o + 4, n + 4);
   be32(o + 8 + n, c);
   return o + 12 + n;
}

static int paeth(int a, int b, int c)
{
   int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
   return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

int img_png(const image *im, unsigned char **png, size_t *len)
{
   int w = im->w, h = im->h, x, y, f, opaque = 1, bpp;
   size_t i, n = (size_t)w * h, rowb, rawn;
   unsigned char *raw, *cur, *prev, *line, *out, *o;
   uLongf zn;
   for (i = 0; i < n && opaque; i++)
      opaque = (im->px[i] >> 24) == 0xFF;
   bpp = opaque ? 3 : 4;
   rowb = (size_t)w * bpp;
   rawn = (rowb + 1) * h;
   raw = (unsigned char *)malloc(rawn);
   cur = (unsigned char *)malloc(rowb * 2 + rowb * 5);
   if (!raw || !cur) { free(raw); free(cur); return -1; }
   prev = cur + rowb;
   memset(prev, 0, rowb);
   line = prev + rowb;          /* 5 candidate rows */
   for (y = 0; y < h; y++)
   {
      const uint32_t *src = im->px + (size_t)y * w;
      long best = -1;
      int bf = 0;
      for (x = 0; x < w; x++)
      {
         uint32_t p = src[x];
         cur[x * bpp] = (unsigned char)p;
         cur[x * bpp + 1] = (unsigned char)(p >> 8);
         cur[x * bpp + 2] = (unsigned char)(p >> 16);
         if (bpp == 4) cur[x * bpp + 3] = (unsigned char)(p >> 24);
      }
      for (f = 0; f < 5; f++)
      {
         unsigned char *d = line + f * rowb;
         long sum = 0;
         size_t k;
         for (k = 0; k < rowb; k++)
         {
            int a = k >= (size_t)bpp ? cur[k - bpp] : 0, b = prev[k], c = k >= (size_t)bpp ? prev[k - bpp] : 0, v;
            switch (f)
            {
            case 0: v = cur[k]; break;
            case 1: v = cur[k] - a; break;
            case 2: v = cur[k] - b; break;
            case 3: v = cur[k] - ((a + b) >> 1); break;
            default: v = cur[k] - paeth(a, b, c); break;
            }
            d[k] = (unsigned char)v;
            sum += (signed char)d[k] < 0 ? -(signed char)d[k] : (signed char)d[k];
         }
         if (best < 0 || sum < best) { best = sum; bf = f; }
      }
      raw[y * (rowb + 1)] = (unsigned char)bf;
      memcpy(raw + y * (rowb + 1) + 1, line + bf * rowb, rowb);
      memcpy(prev, cur, rowb);
   }
   free(cur);
   zn = compressBound(rawn);
   out = (unsigned char *)malloc(zn + 128);
   if (!out) { free(raw); return -1; }
   o = out;
   memcpy(o, "\x89PNG\r\n\x1a\n", 8);
   o += 8;
   {
      unsigned char ih[13];
      be32(ih, (unsigned)w);
      be32(ih + 4, (unsigned)h);
      ih[8] = 8; ih[9] = opaque ? 2 : 6; ih[10] = ih[11] = ih[12] = 0;
      o = chunk(o, "IHDR", ih, 13);
   }
   /* IDAT written in place: its data goes at o + 8 */
   if (compress2(o + 8, &zn, raw, rawn, 6) != Z_OK)
   {
      free(raw);
      free(out);
      return -1;
   }
   free(raw);
   {
      unsigned c;
      be32(o, (unsigned)zn);
      memcpy(o + 4, "IDAT", 4);
      c = (unsigned)crc32(0, o + 4, (unsigned)zn + 4);
      be32(o + 8 + zn, c);
      o += 12 + zn;
   }
   o = chunk(o, "IEND", NULL, 0);
   *png = out;
   *len = (size_t)(o - out);
   return 0;
}

/* ---- painting on the CPU (title cards) ---- */

static uint32_t rgba(int r, int g, int b, int a)
{
   return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24;
}

static uint32_t hsl(float h, float s, float l, int a)
{
   float c, x, m, r = 0, g = 0, b = 0;
   h = fmodf(h, 360.0f);
   if (h < 0) h += 360;
   s /= 100;
   l /= 100;
   c = (1 - fabsf(2 * l - 1)) * s;
   x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
   m = l - c / 2;
   if (h < 60) { r = c; g = x; }
   else if (h < 120) { r = x; g = c; }
   else if (h < 180) { g = c; b = x; }
   else if (h < 240) { g = x; b = c; }
   else if (h < 300) { r = x; b = c; }
   else { r = c; b = x; }
   return rgba((int)((r + m) * 255 + 0.5f), (int)((g + m) * 255 + 0.5f), (int)((b + m) * 255 + 0.5f), a);
}

static void blend(image *im, int x, int y, uint32_t c, float cov)
{
   uint32_t a;
   if (x < 0 || y < 0 || x >= im->w || y >= im->h || cov <= 0)
      return;
   a = (uint32_t)((c >> 24) * (cov > 1 ? 1 : cov) + 0.5f);
   im->px[y * im->w + x] = over(im->px[y * im->w + x], (c & 0xFFFFFF) | a << 24);
}

typedef int (*inside_fn)(float x, float y, const float *p);

static void paint(image *im, inside_fn f, const float *p, float x0, float y0, float x1, float y1, uint32_t c)
{
   int X0 = (int)floorf(x0), Y0 = (int)floorf(y0), X1 = (int)ceilf(x1), Y1 = (int)ceilf(y1), x, y, i, j;
   for (y = Y0; y < Y1; y++)
      for (x = X0; x < X1; x++)
      {
         int n = 0;
         for (j = 0; j < 4; j++)
            for (i = 0; i < 4; i++)
               n += f(x + (i + 0.5f) / 4, y + (j + 0.5f) / 4, p);
         if (n)
            blend(im, x, y, c, n / 16.0f);
      }
}

static int in_circle(float x, float y, const float *p)
{
   float dx = x - p[0], dy = y - p[1];
   return dx * dx + dy * dy <= p[2] * p[2];
}

static int in_ring(float x, float y, const float *p)
{
   float dx = x - p[0], dy = y - p[1], d = sqrtf(dx * dx + dy * dy);
   return d >= p[2] - p[3] / 2 && d <= p[2] + p[3] / 2;
}

static int in_quad(float x, float y, const float *p)
{
   /* convex quad p[0..7], either winding */
   int i, pos = 0, neg = 0;
   for (i = 0; i < 4; i++)
   {
      float ax = p[2 * i], ay = p[2 * i + 1], bx = p[(2 * i + 2) % 8], by = p[(2 * i + 3) % 8];
      float cr = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
      if (cr > 0) pos++;
      else if (cr < 0) neg++;
   }
   return !(pos && neg);
}

static int in_rect(float x, float y, const float *p)
{
   return x >= p[0] && y >= p[1] && x < p[2] && y < p[3];
}

/* CPU text from the baked atlases (the same glyphs the screen draws) */
static float cpu_text(image *im, int face, float x, float y, const char *s, int n, float track, uint32_t c)
{
   const xf_face *f = face_get(face);
   const char *end = n < 0 ? s + strlen(s) : s + n;
   float pen = 0;
   unsigned prev = 0;
   while (s < end && *s)
   {
      unsigned cp = xf_utf8(&s);
      const xf_glyph *g = xf_find(f, cp);
      int i, j;
      if (prev) pen += xf_kerning(f, prev, cp) / 64.0f;
      prev = cp;
      if (!g) continue;
      {
         int gx = (int)floorf(x + pen + 0.5f) + g->xo, gy = (int)floorf(y + 0.5f) + g->yo;
         for (j = 0; j < g->h; j++)
            for (i = 0; i < g->w; i++)
            {
               unsigned char a = f->atlas[(g->y + j) * f->aw + g->x + i];
               if (a)
                  blend(im, gx + i, gy + j, c, a / 255.0f);
            }
      }
      pen += g->adv / 64.0f + track;
   }
   return pen;
}

/* faces for the title card's title, largest first (data.js layout()) */
static const struct { int face; float px; } TC_SIZES[] = {
   { XF_800_19, 19 }, { XF_800_17, 17 }, { XF_800_16, 16 }, { XF_800_15, 15 },
   { XF_800_14, 14 }, { XF_800_13, 13 }, { XF_800_12, 12 }, { XF_800_11, 11 },
};

/* greedy wrap at width maxw; returns line count or -1 (a word too wide) */
static int tc_wrap(int face, const char *t, float maxw, float track, int *st, int *ln, int cap)
{
   const xf_face *f = face_get(face);
   int n = 0;
   const char *p = t;
   while (*p)
   {
      const char *ls, *q, *brk = NULL;
      while (*p == ' ') p++;
      if (!*p) break;
      ls = q = p;
      while (*q)
      {
         const char *we = q;
         while (*we && *we != ' ') we++;
         if (xf_width(f, ls, (int)(we - ls), track) > maxw)
         {
            if (!brk)
               return -1;
            break;
         }
         brk = we;
         q = *we ? we + 1 : we;
      }
      if (!brk) brk = q;
      if (n >= cap)
         return cap + 1;
      st[n] = (int)(ls - t);
      ln[n] = (int)(brk - ls);
      n++;
      p = brk;
   }
   return n;
}

int art_title_card(const char *title, int sys, image *out)
{
   unsigned h = 0;
   const unsigned char *s;
   float fam[3], hue, light;
   unsigned spread;
   uint32_t p0, p1, p2;
   int motif, x, y, k;
   float mx, my;
   const char *sysname;
   if (img_alloc(out, 144, 80) != 0)
      return -1;
   for (s = (const unsigned char *)title; *s; s++)
      h = h * 31 + *s;
   if (sys == SYS_GBA) { fam[0] = g_th->card_hue; fam[1] = g_th->card_sat; fam[2] = g_th->card_light; }
   else if (sys == SYS_GBC) { fam[0] = 180; fam[1] = 55; fam[2] = 36; }
   else { fam[0] = 110; fam[1] = 24; fam[2] = 38; }
   /* a spread inside the family (Classic: 110 degrees) */
   spread = (unsigned)g_th->card_spread;
   hue = fmodf(fam[0] + (float)(h % (spread + 1)) - (float)(spread / 2) + 360, 360);
   light = fam[2] + (float)((h >> 5) % 5) * 4 - 6;
   p0 = hsl(hue, fam[1], light, 255);
   p1 = hsl(hue + 14, fam[1] + 8, fmaxf(10, light - 24), 255);
   p2 = hsl(hue + 40, 70, 84, 255);
   motif = (int)(h % 3);
   mx = 96 + (float)((h >> 3) % 40);
   my = 44 + (float)((h >> 7) % 30);
   /* the diagonal gradient (objectBoundingBox 0,0 -> 1,1) */
   for (y = 0; y < 80; y++)
      for (x = 0; x < 144; x++)
      {
         float t = ((x + 0.5f) / 144 + (y + 0.5f) / 80) / 2;
         uint32_t c = 0;
         for (k = 0; k < 24; k += 8)
         {
            float a = (float)(p0 >> k & 0xFF), b = (float)(p1 >> k & 0xFF);
            c |= (uint32_t)(a + (b - a) * t + 0.5f) << k;
         }
         out->px[y * 144 + x] = c | 0xFF000000u;
      }
   if (motif == 0)
   {
      float a[3] = { mx, my, 34 }, b[3] = { mx, my, 18 };
      paint(out, in_circle, a, mx - 35, my - 35, mx + 35, my + 35, rgba(255, 255, 255, 18));
      paint(out, in_circle, b, mx - 19, my - 19, mx + 19, my + 19, rgba(255, 255, 255, 15));
   }
   else if (motif == 1)
   {
      float q1[8] = { mx - 60, 80, mx, 0, mx + 26, 0, mx - 34, 80 };
      float q2[8] = { mx - 20, 80, mx + 40, 0, mx + 50, 0, mx - 10, 80 };
      paint(out, in_quad, q1, mx - 60, 0, mx + 26, 80, rgba(255, 255, 255, 18));
      paint(out, in_quad, q2, mx - 20, 0, mx + 50, 80, rgba(255, 255, 255, 13));
   }
   else
   {
      float a[4] = { mx, my, 36, 6 }, b[4] = { mx, my, 20, 6 };
      paint(out, in_ring, a, mx - 40, my - 40, mx + 40, my + 40, rgba(255, 255, 255, 23));
      paint(out, in_ring, b, mx - 24, my - 24, mx + 24, my + 24, rgba(255, 255, 255, 18));
   }
   sysname = sys == SYS_GBA ? "GAME BOY ADVANCE" : sys == SYS_GBC ? "GAME BOY COLOR" : "GAME BOY";
   cpu_text(out, XF_800_75, 12, 19, sysname, -1, 1.6f, p2);
   /* the title: the largest size that fits in three lines above the band,
    * lines balanced to even widths */
   {
      int st[4], ln[4], n = -1, i, fi;
      const float W = 122;
      for (fi = 0; fi < (int)(sizeof(TC_SIZES) / sizeof(TC_SIZES[0])); fi++)
      {
         float fs = TC_SIZES[fi].px;
         n = tc_wrap(TC_SIZES[fi].face, title, W, -0.2f, st, ln, 3);
         if (n < 1 || n > 3)
            continue;
         if (63 - (n - 1) * fs * 1.06f - fs * 0.78f < 29)
            continue;
         if (n > 1)
         {
            float lo = xf_width(face_get(TC_SIZES[fi].face), title, -1, -0.2f) / n, hi = W;
            int it, s2[4], l2[4];
            for (it = 0; it < 12; it++)
            {
               float mid = (lo + hi) / 2;
               int m2 = tc_wrap(TC_SIZES[fi].face, title, mid, -0.2f, s2, l2, 3);
               if (m2 == n)
               {
                  hi = mid;
                  memcpy(st, s2, sizeof(st));
                  memcpy(ln, l2, sizeof(ln));
               }
               else
                  lo = mid;
            }
         }
         break;
      }
      if (fi == (int)(sizeof(TC_SIZES) / sizeof(TC_SIZES[0])))
      {
         /* nothing fits: the smallest size, cut */
         fi = 7;
         n = tc_wrap(XF_800_11, title, W, -0.2f, st, ln, 3);
         if (n < 1 || n > 3) { n = 1; st[0] = 0; ln[0] = (int)strlen(title) > 20 ? 20 : (int)strlen(title); }
      }
      for (i = 0; i < n; i++)
      {
         float fs = TC_SIZES[fi].px, lh = fs * 1.06f;
         cpu_text(out, TC_SIZES[fi].face, 12, 63 - (n - 1 - i) * lh, title + st[i], ln[i], -0.2f, rgba(255, 255, 255, 255));
      }
   }
   {
      float r[4] = { 0, 77, 144, 80 };
      paint(out, in_rect, r, 0, 77, 144, 80, (p2 & 0xFFFFFF) | (uint32_t)(0.85f * 255) << 24);
   }
   return 0;
}

/* ---- composing the entry's pictures ---- */

static int load_choice(const art_choice *a, image *out)
{
   if (a->kind == ART_NONE || !a->path[0])
      return -1;
   return img_load(a->path, out, 0);
}

/* ART_ENTRY reads from the entry's EBOOT: icon = section 1, pic = section 4 */
static int load_icon_src(const game *g, image *out)
{
   if (g->icon.kind == ART_ENTRY)
      return img_load(g->icon.path, out, XW_ICON0);
   return load_choice(&g->icon, out);
}

static int load_bg_src(const game *g, image *out)
{
   if (g->bg.kind == ART_ENTRY)
      return img_load(g->bg.path, out, XW_PIC1);
   return load_choice(&g->bg, out);
}

const char *fit_name(int fit)
{
   switch (fit)
   {
   case FIT_TILE: return "Blur frame";
   case FIT_SIDE: return "Box + title";
   case FIT_SMART: return "Smart fill";
   case FIT_STRETCH: return "Stretch";
   default: return "Fitted";
   }
}

int art_check(const char *path, char *why, size_t cap)
{
   unsigned char *d;
   size_t n;
   int w, h, c;
   if (read_all(path, &d, &n) != 0)
   {
      snprintf(why, cap, "can't be read");
      return -1;
   }
   if (n > ART_MAX_FILE)
   {
      free(d);
      snprintf(why, cap, "is over %d MB", ART_MAX_FILE >> 20);
      return -1;
   }
   if (!stbi_info_from_memory(d, (int)n, &w, &h, &c))
   {
      free(d);
      snprintf(why, cap, "is not a PNG, JPEG or BMP the PSP can read");
      return -1;
   }
   free(d);
   if (w < 16 || h < 16)
   {
      snprintf(why, cap, "is too small (%dx%d)", w, h);
      return -1;
   }
   if ((long)w * h > ART_MAX_PX)
   {
      snprintf(why, cap, "is too large (%dx%d); up to 960x640 fits in the PSP's memory", w, h);
      return -1;
   }
   return 0;
}

/* the rect of src that is not (nearly) transparent */
typedef struct { float x0, y0, x1, y1; } frect;

static frect opaque_rect(const image *s)
{
   frect r = { 0, 0, (float)s->w, (float)s->h };
   int x, y, x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
   for (y = 0; y < s->h; y++)
   {
      const uint32_t *row = s->px + (size_t)y * s->w;
      for (x = 0; x < s->w; x++)
         if ((row[x] >> 24) > 24)
         {
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            y1 = y;
         }
   }
   if (x1 >= x0 && y1 >= y0)
   {
      r.x0 = (float)x0; r.y0 = (float)y0; r.x1 = (float)x1 + 1; r.y1 = (float)y1 + 1;
   }
   return r;
}

/* draw the src rect r scaled into (x, y, w, h) of dst, src-over (alpha k) */
static void draw_rect(image *dst, const image *src, frect r, int x, int y, int w, int h, float k)
{
   image t;
   int i, j;
   if (w <= 0 || h <= 0 || img_alloc(&t, w, h) != 0)
      return;
   resample(src, r.x0, r.y0, r.x1, r.y1, t.px, w, h, w);
   for (j = 0; j < h; j++)
   {
      int yy = y + j;
      if (yy < 0 || yy >= dst->h) continue;
      for (i = 0; i < w; i++)
      {
         int xx = x + i;
         uint32_t p = t.px[j * w + i];
         if (xx < 0 || xx >= dst->w) continue;
         if (k < 1)
            p = (p & 0xFFFFFF) | (uint32_t)((p >> 24) * k + 0.5f) << 24;
         dst->px[yy * dst->w + xx] = over(dst->px[yy * dst->w + xx], p);
      }
   }
   img_free(&t);
}

static void img_darken(image *im, float k)
{
   size_t i, n = (size_t)im->w * im->h;
   for (i = 0; i < n; i++)
   {
      uint32_t p = im->px[i];
      im->px[i] = (uint32_t)((p & 0xFF) * k) | (uint32_t)((p >> 8 & 0xFF) * k) << 8 |
                  (uint32_t)((p >> 16 & 0xFF) * k) << 16 | (p & 0xFF000000u);
   }
}

/* A soft backdrop: src covering w x h, blurred at quarter size (cheap: a
 * 36x20 blur, then a bilinear enlarge), saturated a little and darkened. */
static void soft_backdrop(image *out, const image *src, float dark)
{
   image s = { 0 };
   int qw = out->w / 4, qh = out->h / 4;
   if (img_fit(src, qw, qh, 1, &s) != 0)
      return;
   img_blur(&s, out->w / 80.0f);
   img_saturate(&s, 1.2f);
   img_darken(&s, dark);
   /* a little over-size so the clamped blur edges sit outside */
   img_draw(out, &s, -out->w * 0.06f, -out->h * 0.06f, out->w * 1.12f, out->h * 1.12f);
   img_free(&s);
}

/* a soft shadow under a w x h card at (x, y) */
static void drop_shadow(image *out, int x, int y, int w, int h)
{
   image s;
   int i, j, m = 6;
   if (img_alloc(&s, w + 2 * m, h + 2 * m) != 0)
      return;
   for (j = m; j < m + h; j++)
      for (i = m; i < m + w; i++)
         s.px[j * s.w + i] = 0xA0000000u;
   img_blur(&s, 2.5f);
   img_draw(out, &s, (float)(x - m), (float)(y - m + 2), (float)s.w, (float)s.h);
   img_free(&s);
}

/* the part of r that covers a w:h frame: the full width, anchored at the
 * top band (where box art puts the title) rather than the centre;
 * `stretch` (>= 1) widens the source first, so less is cropped */
static frect cover_top(frect r, float w, float h, float stretch)
{
   float sw = r.x1 - r.x0, sh = r.y1 - r.y0, a = sw * stretch / sh, want = w / h;
   if (a < want)
   {
      float ch = sw * stretch / want, y0 = r.y0 + sh * 0.03f;
      if (y0 + ch > r.y1) y0 = r.y1 - ch;
      r.y0 = y0;
      r.y1 = y0 + ch;
   }
   else
   {
      float cw = sh * want / stretch, x0 = r.x0 + (sw - cw) / 2;
      r.x0 = x0;
      r.x1 = x0 + cw;
   }
   return r;
}

/* Box art puts its logo at the top nearly always, at the bottom sometimes
 * (Castlevania: Aria of Sorrow).  Logos are big, saturated, hard-edged
 * shapes, so the window whose rows carry more saturation-weighted edge
 * energy (on a 48-wide copy) is taken, the top unless the bottom is
 * clearly ahead. */
static frect cover_title(const image *src, frect r, float w, float h, float stretch)
{
   frect top = cover_top(r, w, h, stretch), bot;
   image s = { 0 };
   float et = 0, eb = 0, k;
   int x, y, sw = 48, sh;
   if (top.y1 - top.y0 >= r.y1 - r.y0 - 1)
      return top;                          /* nothing cropped vertically */
   bot = top;
   bot.y1 = r.y1 - (r.y1 - r.y0) * 0.03f;
   bot.y0 = bot.y1 - (top.y1 - top.y0);
   sh = (int)(sw * (r.y1 - r.y0) / (r.x1 - r.x0) + 0.5f);
   if (sh < 8 || img_alloc(&s, sw, sh) != 0)
      return top;
   resample(src, r.x0, r.y0, r.x1, r.y1, s.px, sw, sh, sw);
   k = sh / (r.y1 - r.y0);
   for (y = 1; y < sh - 1; y++)
   {
      float e = 0, fy = r.y0 + (y + 0.5f) / k;
      for (x = 1; x < sw - 1; x++)
      {
         uint32_t p = s.px[y * sw + x], q = s.px[y * sw + x + 1], u = s.px[(y + 1) * sw + x];
         int R = p & 0xFF, G = p >> 8 & 0xFF, B = p >> 16 & 0xFF;
         int mx = R > G ? (R > B ? R : B) : (G > B ? G : B), mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
         float sat = mx ? (float)(mx - mn) / mx : 0;
         float gx = (float)abs((int)(q & 0xFF) - R) + abs((int)(q >> 8 & 0xFF) - G) + abs((int)(q >> 16 & 0xFF) - B);
         float gy = (float)abs((int)(u & 0xFF) - R) + abs((int)(u >> 8 & 0xFF) - G) + abs((int)(u >> 16 & 0xFF) - B);
         e += (gx + gy) * (0.4f + sat);
      }
      if (fy >= top.y0 && fy < top.y1) et += e;
      if (fy >= bot.y0 && fy < bot.y1) eb += e;
   }
   img_free(&s);
   return eb > et * 1.2f ? bot : top;
}

/* The ICON0 layouts.  `ic` = box art (may be NULL), `bg` = hero (may be NULL). */
static int icon_layout(const game *g, const image *ic, const image *bg, image *out)
{
   frect r;
   int i;
   if (!ic)
      return img_fit(bg, 144, 80, 1, out);     /* no box art: the hero alone, filled */
   if (img_alloc(out, 144, 80) != 0)
      return -1;
   r = opaque_rect(ic);
   if (g->fit == FIT_SMART || g->fit == FIT_STRETCH)
   {
      draw_rect(out, ic, cover_title(ic, r, 144, 80, g->fit == FIT_STRETCH ? 1.3f : 1), 0, 0, 144, 80, 1);
      return 0;
   }
   if (g->fit == FIT_FIT)
   {
      float sc = fminf(144 / (r.x1 - r.x0), 80 / (r.y1 - r.y0));
      int w = (int)((r.x1 - r.x0) * sc + 0.5f), h = (int)((r.y1 - r.y0) * sc + 0.5f);
      draw_rect(out, ic, r, (144 - w) / 2, (80 - h) / 2, w, h, 1);
      return 0;
   }
   for (i = 0; i < 144 * 80; i++)
      out->px[i] = 0xFF1E0C0Au;            /* #0a0c1e */
   if (g->fit == FIT_SIDE)
   {
      /* box art left (70 tall), the title right, over the art blurred */
      int bh = 70, bw, tx, fi, n = -1, st[4], ln[4], k;
      float sc;
      for (k = 0; k < 2; k++, bh -= 10)
      {
         sc = bh / (r.y1 - r.y0);
         bw = (int)((r.x1 - r.x0) * sc + 0.5f);
         if (bw > 76) { bw = 76; sc = bw / (r.x1 - r.x0); }
         tx = 5 + bw + 7;
         for (fi = 2; fi < (int)(sizeof(TC_SIZES) / sizeof(TC_SIZES[0])); fi++)
         {
            float fs = TC_SIZES[fi].px;
            n = tc_wrap(TC_SIZES[fi].face, g->title, 144 - tx - 5, -0.2f, st, ln, 4);
            if (n >= 1 && n <= 4 && n * fs * 1.08f <= 72)
               break;
         }
         if (fi < (int)(sizeof(TC_SIZES) / sizeof(TC_SIZES[0])))
            break;
      }
      if (k == 2)
      {
         /* nothing fits: the smallest size, cut to one line */
         bh += 10;
         fi = 7;
         n = 1;
         st[0] = 0;
         ln[0] = (int)strlen(g->title) > 9 ? 9 : (int)strlen(g->title);
      }
      sc = bh / (r.y1 - r.y0);
      bw = (int)((r.x1 - r.x0) * sc + 0.5f);
      if (bw > 76) { bw = 76; bh = (int)((r.y1 - r.y0) * bw / (r.x1 - r.x0) + 0.5f); }
      tx = 5 + bw + 7;
      soft_backdrop(out, bg ? bg : ic, 0.42f);
      drop_shadow(out, 5, (80 - bh) / 2, bw, bh);
      draw_rect(out, ic, r, 5, (80 - bh) / 2, bw, bh, 1);
      {
         float fs = TC_SIZES[fi].px, lh = fs * 1.08f, y0 = 40 - n * lh / 2 + fs * 0.78f;
         int j;
         for (j = 0; j < n; j++)
         {
            cpu_text(out, TC_SIZES[fi].face, (float)tx + 1, y0 + j * lh + 1, g->title + st[j], ln[j], -0.2f, rgba(0, 0, 0, 150));
            cpu_text(out, TC_SIZES[fi].face, (float)tx, y0 + j * lh, g->title + st[j], ln[j], -0.2f, rgba(255, 255, 255, 255));
         }
      }
      return 0;
   }
   /* FIT_TILE, the blur frame: the box art as large as the icon allows, over
    * a blurred, darkened copy of the hero (or of the box art itself) */
   {
      float sc = fminf(140 / (r.x1 - r.x0), 76 / (r.y1 - r.y0));
      int w = (int)((r.x1 - r.x0) * sc + 0.5f), h = (int)((r.y1 - r.y0) * sc + 0.5f);
      soft_backdrop(out, bg ? bg : ic, 0.6f);
      drop_shadow(out, (144 - w) / 2, (80 - h) / 2, w, h);
      draw_rect(out, ic, r, (144 - w) / 2, (80 - h) / 2, w, h, 1);
   }
   return 0;
}

int art_icon0(const game *g, image *out)
{
   image ic = { 0 }, bg = { 0 };
   int has_ic = load_icon_src(g, &ic) == 0, has_bg = 0, rc;
   if ((g->icon.kind == ART_ENTRY || g->icon.kind == ART_CUSTOM) && has_ic && ic.w == 144 && ic.h == 80)
   {
      /* the entry's own ICON0, or a finished 144x80 PNG of the owner's: it IS the icon */
      *out = ic;
      return 0;
   }
   if (g->fit != FIT_FIT || !has_ic)
      has_bg = load_bg_src(g, &bg) == 0;
   if (!has_ic && !has_bg)
      return art_title_card(g->title, g->sys, out);
   rc = icon_layout(g, has_ic ? &ic : NULL, has_bg ? &bg : NULL, out);
   img_free(&ic);
   img_free(&bg);
   return rc;
}

int art_pic1_is_hero(const game *g)
{
   return g->bg.kind == ART_OWN || g->bg.kind == ART_NET;
}

/* GBAdhoc's hero art keeps its bottom 44 of 272 rows as a calm, dark band
 * for the Marquee's hint bar (heroforge PROMPT.md).  The XMB has no hint
 * bar, so the band is left out: the top 228/272 of the picture is scaled
 * to fill 480x272.  The width this needs is taken from the RIGHT edge, so
 * the subject (60-95% across) stays where the art put it and whole; the
 * left edge it drops is the art's quiet side. */
#define HERO_KEEP (228.0f / 272.0f)

int art_pic1(const game *g, image *out)
{
   image bg = { 0 };
   int rc;
   if (load_bg_src(g, &bg) != 0)
      return -1;
   if (art_pic1_is_hero(g))
   {
      float uh = bg.h * HERO_KEEP, uw = uh * 480.0f / 272.0f, x0 = bg.w - uw;
      if (x0 < 0)
      {
         /* narrower than the screen's shape: full width, the top rows */
         uw = (float)bg.w;
         uh = uw * 272.0f / 480.0f;
         x0 = 0;
      }
      rc = img_alloc(out, 480, 272);
      if (rc == 0)
         resample(&bg, x0, 0, x0 + uw, uh, out->px, 480, 272, 480);
      img_free(&bg);
      return rc;
   }
   if (bg.w == 480 && bg.h == 272)
   {
      *out = bg;
      return 0;
   }
   rc = img_fit(&bg, 480, 272, 1, out);
   img_free(&bg);
   return rc;
}

int art_thumb(const char *path, int pbp_sect, int w, int h, int cover, image *out)
{
   image src = { 0 };
   int rc;
   if (img_load(path, &src, pbp_sect) != 0)
      return -1;
   rc = img_fit(&src, w, h, cover, out);
   img_free(&src);
   return rc;
}
