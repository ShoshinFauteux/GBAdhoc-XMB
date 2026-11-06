/* preview_art.c -- host build of art.c: renders every ICON0 layout and the
 * PIC1 (the hero as GBAdhoc has it, and the PIC1 the app writes) for
 * a few games, with the very code the PSP runs.  In any gcc image:
 *   cc -O2 -I.. -o preview_art preview_art.c ../art.c ../theme.c ../../shared/xw_core.c -lz -lm
 *   ./preview_art <boxart dir> <hero dir> <out dir> "Title|ROM stem" ...
 * Writes <out>/<n>_icon_<layout>.png, <n>_hero_src.png, <n>_pic1.png. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../art.h"
#include "../gfx.h"
#include "../util.h"
#include "../gen/xmb_fonts.h"

/* the slice of gfx.c / util.c art.c needs */
unsigned xf_utf8(const char **ps)
{
   const unsigned char *s = (const unsigned char *)*ps;
   unsigned c = *s++, n = 0, cp;
   if (c < 0x80) { *ps = (const char *)s; return c; }
   if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
   else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
   else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
   else { *ps = (const char *)s; return '?'; }
   while (n--)
   {
      if ((*s & 0xC0) != 0x80) { *ps = (const char *)s; return '?'; }
      cp = cp << 6 | (*s++ & 0x3F);
   }
   *ps = (const char *)s;
   return cp;
}

const xf_glyph *xf_find(const xf_face *f, unsigned cp)
{
   int lo = 0, hi = f->ng - 1;
   while (lo <= hi)
   {
      int m = (lo + hi) / 2;
      if (f->g[m].cp == cp) return &f->g[m];
      if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
   }
   return cp == '?' ? NULL : xf_find(f, '?');
}

int xf_kerning(const xf_face *f, unsigned a, unsigned b)
{
   unsigned key = a << 16 | b;
   int lo = 0, hi = f->nk - 1;
   if (a > 0xFFFF || b > 0xFFFF) return 0;
   while (lo <= hi)
   {
      int m = (lo + hi) / 2;
      if (f->k[m].pair == key) return f->k[m].k;
      if (f->k[m].pair < key) lo = m + 1; else hi = m - 1;
   }
   return 0;
}

float xf_width(const xf_face *f, const char *s, int n, float track)
{
   const char *end = n < 0 ? s + strlen(s) : s + n;
   float w = 0;
   unsigned prev = 0;
   while (s < end && *s)
   {
      unsigned cp = xf_utf8(&s);
      const xf_glyph *g = xf_find(f, cp);
      if (prev) w += xf_kerning(f, prev, cp) / 64.0f;
      if (g) w += g->adv / 64.0f + track;
      prev = cp;
   }
   return w;
}

const xf_face *face_get(int face) { return &xf_faces[face]; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

int read_all(const char *p, unsigned char **data, size_t *len)
{
   FILE *f = fopen(p, "rb");
   long n;
   if (!f) return -1;
   fseek(f, 0, SEEK_END);
   n = ftell(f);
   fseek(f, 0, SEEK_SET);
   *data = (unsigned char *)malloc((size_t)n + 1);
   if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*data); return -1; }
   (*data)[n] = 0;
   *len = (size_t)n;
   fclose(f);
   return 0;
}

void logf_(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
   fputc('\n', stderr);
}

static void save(const image *im, const char *path)
{
   unsigned char *png;
   size_t n;
   FILE *f;
   if (img_png(im, &png, &n) != 0) { fprintf(stderr, "png failed %s\n", path); return; }
   f = fopen(path, "wb");
   if (f) { fwrite(png, 1, n, f); fclose(f); }
   free(png);
}

int main(int argc, char **argv)
{
   int i, k;
   if (argc < 5) { fprintf(stderr, "usage: preview_art boxdir herodir outdir \"Title|stem\"...\n"); return 2; }
   for (i = 4; i < argc; i++)
   {
      static game g;
      char title[128], stem[256], p[700];
      const char *bar = strchr(argv[i], '|');
      image im;
      memset(&g, 0, sizeof(g));
      snprintf(title, sizeof(title), "%.*s", (int)(bar - argv[i]), argv[i]);
      snprintf(stem, sizeof(stem), "%s", bar + 1);
      snprintf(g.title, sizeof(g.title), "%s", title);
      g.sys = SYS_GBA;
      g.icon.kind = ART_OWN;
      snprintf(g.icon.path, PATHCAP, "%s/%s.png", argv[1], stem);
      g.bg.kind = ART_OWN;
      snprintf(g.bg.path, PATHCAP, "%s/%s.png", argv[2], stem);
      for (k = 0; k < FIT_COUNT; k++)
      {
         g.fit = k;
         if (art_icon0(&g, &im) == 0)
         {
            snprintf(p, sizeof(p), "%s/%d_icon_%d.png", argv[3], i - 4, k);
            save(&im, p);
            img_free(&im);
         }
      }
      if (img_load(g.bg.path, &im, 0) == 0)
      {
         snprintf(p, sizeof(p), "%s/%d_hero_src.png", argv[3], i - 4);
         save(&im, p);
         img_free(&im);
      }
      if (art_pic1(&g, &im) == 0)
      {
         snprintf(p, sizeof(p), "%s/%d_pic1.png", argv[3], i - 4);
         save(&im, p);
         img_free(&im);
      }
   }
   return 0;
}
