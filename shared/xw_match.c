/* xw_match.c -- see xw_match.h. */
#include "xw_match.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void xw_dat_free(xw_dat *d)
{
   free(d->g);
   d->g = NULL;
   d->n = d->cap = 0;
}

/* The quoted string after `key` inside [p, end), or NULL. */
static const char *dat_q(const char *p, const char *end, const char *key,
                         char *out, size_t cap)
{
   size_t kl = strlen(key);
   for (; p + kl < end; p++)
   {
      if (memcmp(p, key, kl) == 0 && (p[kl] == ' ' || p[kl] == '\t'))
      {
         const char *q = p + kl, *e;
         size_t n;
         while (q < end && (*q == ' ' || *q == '\t'))
            q++;
         if (q >= end || *q != '"')
            continue;
         q++;
         e = q;
         while (e < end && *e != '"')
            e++;
         n = (size_t)(e - q);
         if (n >= cap)
            n = cap - 1;
         memcpy(out, q, n);
         out[n] = '\0';
         return e;
      }
   }
   return NULL;
}

static const char *dat_word(const char *p, const char *end, const char *key,
                            char *out, size_t cap)
{
   size_t kl = strlen(key);
   for (; p + kl < end; p++)
   {
      if (memcmp(p, key, kl) == 0 && p[kl] == ' ' && (p[-1] == ' ' || p[-1] == '('))
      {
         const char *q = p + kl + 1;
         size_t n = 0;
         while (q < end && *q == ' ')
            q++;
         while (q + n < end && q[n] != ' ' && q[n] != ')' && q[n] != '\n')
            n++;
         if (n >= cap)
            n = cap - 1;
         memcpy(out, q, n);
         out[n] = '\0';
         return q + n;
      }
   }
   return NULL;
}

int xw_dat_parse(const char *text, size_t len, xw_dat *d)
{
   const char *p = text, *end = text + len;
   while (p < end)
   {
      const char *g = NULL, *ge, *q;
      int depth = 0;
      xw_dat_game x;
      char tmp[32];
      for (q = p; q + 6 < end; q++)
         if ((q == text || q[-1] == '\n') && memcmp(q, "game (", 6) == 0)
         {
            g = q + 6;
            break;
         }
      if (!g)
         break;
      /* the block's end: the matching ')' */
      for (ge = g, depth = 1; ge < end && depth; ge++)
      {
         if (*ge == '"')
         {
            ge++;
            while (ge < end && *ge != '"')
               ge++;
         }
         else if (*ge == '(')
            depth++;
         else if (*ge == ')')
            depth--;
      }
      memset(&x, 0, sizeof(x));
      if (dat_q(g, ge, "name", x.name, sizeof(x.name)))
      {
         const char *rom = strstr(g, "rom (");
         dat_q(g, ge, "serial", x.serial, sizeof(x.serial));
         if (rom && rom < ge)
         {
            if (dat_word(rom, ge, "crc", tmp, sizeof(tmp)))
               x.crc = strtoul(tmp, NULL, 16);
            if (dat_word(rom, ge, "size", tmp, sizeof(tmp)))
               x.size = strtoul(tmp, NULL, 10);
         }
         if (d->n == d->cap)
         {
            int c = d->cap ? d->cap * 2 : 1024;
            xw_dat_game *ng = (xw_dat_game *)realloc(d->g, (size_t)c * sizeof(*ng));
            if (!ng)
               return -1;
            d->g = ng;
            d->cap = c;
         }
         d->g[d->n++] = x;
      }
      p = ge;
   }
   return 0;
}

/* ---- names -------------------------------------------------------------- */

void xw_nointro_base(const char *name, char *out, size_t cap)
{
   const char *a = strstr(name, " ("), *b = strstr(name, " [");
   size_t n;
   if (!a || (b && b < a))
      a = b;
   n = a ? (size_t)(a - name) : strlen(name);
   while (n && name[n - 1] == ' ')
      n--;
   if (n >= cap)
      n = cap - 1;
   memcpy(out, name, n);
   out[n] = '\0';
}

void xw_nointro_region(const char *name, char *out, size_t cap)
{
   const char *a = strchr(name, '('), *b;
   size_t n;
   out[0] = '\0';
   if (!a)
      return;
   b = strchr(a, ')');
   if (!b)
      return;
   n = (size_t)(b - a - 1);
   if (n >= cap)
      n = cap - 1;
   memcpy(out, a + 1, n);
   out[n] = '\0';
}

/* "Legend of Zelda, The" -> "The Legend of Zelda" (in place, one segment) */
static void article_front(char *seg, size_t cap)
{
   static const char *arts[] = { ", The", ", A", ", An" };
   size_t i, n = strlen(seg);
   for (i = 0; i < 3; i++)
   {
      size_t al = strlen(arts[i]);
      if (n > al && strcmp(seg + n - al, arts[i]) == 0)
      {
         char tmp[256];
         seg[n - al] = '\0';
         snprintf(tmp, sizeof(tmp), "%s %s", arts[i] + 2, seg);
         snprintf(seg, cap, "%s", tmp);
         return;
      }
   }
}

void xw_nointro_display(const char *name, char *out, size_t cap)
{
   char base[256], seg[2][256];
   char *dash;
   size_t l;
   xw_nointro_base(name, base, sizeof(base));
   dash = strstr(base, " - ");
   if (!dash)
   {
      snprintf(seg[0], sizeof(seg[0]), "%s", base);
      article_front(seg[0], sizeof(seg[0]));
      snprintf(out, cap, "%s", seg[0]);
      return;
   }
   *dash = '\0';
   snprintf(seg[0], sizeof(seg[0]), "%s", base);
   snprintf(seg[1], sizeof(seg[1]), "%s", dash + 3);
   article_front(seg[0], sizeof(seg[0]));
   article_front(seg[1], sizeof(seg[1]));
   l = strlen(seg[1]);
   /* "Pokemon - Emerald Version" -> "Pokemon Emerald" */
   if (l > 8 && strcmp(seg[1] + l - 8, " Version") == 0)
   {
      seg[1][l - 8] = '\0';
      if (!strchr(seg[1], ' '))
      {
         snprintf(out, cap, "%s %s", seg[0], seg[1]);
         return;
      }
      seg[1][l - 8] = ' ';
   }
   snprintf(out, cap, "%s: %s", seg[0], seg[1]);
}

void xw_thumb_name(const char *name, char *out, size_t cap)
{
   size_t i;
   snprintf(out, cap, "%s", name);
   for (i = 0; out[i]; i++)
      if (strchr("&*/:`<>?\\|\"", out[i]))
         out[i] = '_';
}

/* Latin-1 supplement (U+00C0..U+00FF) folded to ASCII. */
static const char fold[65] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

void xw_norm_key(const char *s, char *out, size_t cap)
{
   const unsigned char *p = (const unsigned char *)s;
   size_t n = 0;
   int sp = 0;
   if (!cap)
      return;
   for (; *p && n + 4 < cap; p++)
   {
      unsigned c = *p;
      char o = 0;
      if (c == 0xc3 && p[1] >= 0x80 && p[1] < 0xc0)
      {
         o = fold[p[1] - 0x80];      /* U+00C0 + (byte - 0x80) */
         p++;
      }
      else if (c >= 0x80)
      {
         while ((p[1] & 0xc0) == 0x80)
            p++;
         sp = 1;
         continue;
      }
      else
         o = (char)c;
      if (o >= 'A' && o <= 'Z')
         o = (char)(o - 'A' + 'a');
      if (o == '&')
      {
         if (n) { out[n++] = ' '; }
         if (n + 4 < cap) { memcpy(out + n, "and", 3); n += 3; }
         sp = 1;
         continue;
      }
      if (o == '\'')
         continue;                      /* "Morrow's" == "Morrows" */
      if ((o >= 'a' && o <= 'z') || (o >= '0' && o <= '9'))
      {
         if (sp && n)
            out[n++] = ' ';
         sp = 0;
         out[n++] = o;
      }
      else
         sp = 1;
   }
   out[n] = '\0';
}

/* Dice coefficient of character bigrams (spaces kept as word breaks). */
static int dice(const char *a, const char *b)
{
   size_t la = strlen(a), lb = strlen(b), i, j, hits = 0;
   unsigned char *used;
   if (la < 2 || lb < 2)
      return strcmp(a, b) == 0 ? 100 : 0;
   used = (unsigned char *)calloc(lb, 1);
   if (!used)
      return 0;
   for (i = 0; i + 1 < la; i++)
      for (j = 0; j + 1 < lb; j++)
         if (!used[j] && a[i] == b[j] && a[i + 1] == b[j + 1])
         {
            used[j] = 1;
            hits++;
            break;
         }
   free(used);
   return (int)(200.0 * hits / (double)((la - 1) + (lb - 1)) + 0.5);
}

int xw_name_score(const char *a, const char *b)
{
   char ba[256], bb[256], ka[256], kb[256];
   xw_nointro_base(a, ba, sizeof(ba));
   xw_nointro_base(b, bb, sizeof(bb));
   xw_norm_key(ba, ka, sizeof(ka));
   xw_norm_key(bb, kb, sizeof(kb));
   if (!ka[0] || !kb[0])
      return 0;
   if (strcmp(ka, kb) == 0)
      return 100;
   return dice(ka, kb);
}

/* ---- the cartridge header ----------------------------------------------- */

static void hdr_text(const unsigned char *p, size_t n, char *out)
{
   size_t i, k = 0;
   for (i = 0; i < n && p[i]; i++)
      if (p[i] >= 0x20 && p[i] < 0x7f)
         out[k++] = (char)p[i];
   while (k && out[k - 1] == ' ')
      k--;
   out[k] = '\0';
}

void xw_rom_header_parse(const unsigned char *head, size_t len,
                         const char *ext, xw_rom_header *h)
{
   static const unsigned char nlogo[8] = { 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B };
   memset(h, 0, sizeof(*h));
   if (ext && (!strcmp(ext, "gba") || !strcmp(ext, "GBA") || !strcmp(ext, "agb")))
      strcpy(h->sys, "GBA");
   else if (ext && (!strcmp(ext, "gbc") || !strcmp(ext, "GBC") || !strcmp(ext, "cgb")))
      strcpy(h->sys, "GBC");
   else
      strcpy(h->sys, "GB");
   if (len >= 0xC0 && head[0xB2] == 0x96)
   {
      strcpy(h->sys, "GBA");
      hdr_text(head + 0xA0, 12, h->title);
      hdr_text(head + 0xAC, 4, h->code);
      h->valid = 1;
      return;
   }
   if (len >= 0x150 && memcmp(head + 0x104, nlogo, 8) == 0)
   {
      unsigned cgb = head[0x143];
      hdr_text(head + 0x134, cgb & 0x80 ? 15 : 16, h->title);
      strcpy(h->sys, cgb & 0x80 ? "GBC" : "GB");
      h->valid = 1;
   }
}

/* ---- matching ----------------------------------------------------------- */

static int same_base(const char *a, const char *b)
{
   char ba[256], bb[256], ka[256], kb[256];
   xw_nointro_base(a, ba, sizeof(ba));
   xw_nointro_base(b, bb, sizeof(bb));
   xw_norm_key(ba, ka, sizeof(ka));
   xw_norm_key(bb, kb, sizeof(kb));
   return ka[0] && strcmp(ka, kb) == 0;
}

/* lower = more the game itself: a release, then revisions, then the rest */
static int release_rank(const char *name)
{
   static const char *worse[] = { "(Virtual Console)", "(Beta", "(Proto", "(Demo", "(Sample",
                                  "(Kiosk", "(Debug", "(Unl)", "(Pirate", "[b]" };
   size_t i;
   int r = (int)strlen(name) / 16;
   for (i = 0; i < sizeof(worse) / sizeof(worse[0]); i++)
      if (strstr(name, worse[i]))
         r += 100;
   return r;
}

int xw_dat_match(const xw_dat *d, const char *stem, const xw_rom_header *h,
                 int have_crc, unsigned long crc, xw_match_res *r)
{
   char ks[256], kn[256];
   int i, best = -1, bests = 0, serial = -1;
   r->idx = -1;
   r->score = 0;
   r->how = XW_MATCH_NONE;
   r->need_crc = 0;
   if (!d || !d->n)
      return -1;
   if (have_crc)
      for (i = 0; i < d->n; i++)
         if (d->g[i].crc == crc)
         {
            r->idx = i; r->score = 100; r->how = XW_MATCH_CRC;
            return i;
         }
   xw_norm_key(stem, ks, sizeof(ks));
   for (i = 0; i < d->n; i++)
   {
      xw_norm_key(d->g[i].name, kn, sizeof(kn));
      if (ks[0] && strcmp(ks, kn) == 0)
      {
         r->idx = i; r->score = 100; r->how = XW_MATCH_NAME;
         return i;
      }
   }
   if (h && h->valid && h->code[0])
   {
      for (i = 0; i < d->n; i++)
         if (!strcmp(d->g[i].serial, h->code))
         {
            /* the plain release over re-releases, betas and demos */
            if (serial < 0 || release_rank(d->g[i].name) < release_rank(d->g[serial].name))
               serial = i;
            if (same_base(stem, d->g[i].name))
            {
               r->idx = i; r->score = have_crc ? 95 : 98; r->how = XW_MATCH_SERIAL;
               return i;
            }
         }
      if (serial >= 0)
      {
         if (!have_crc)
         {
            r->need_crc = 1;
            r->idx = serial; r->score = 90; r->how = XW_MATCH_SERIAL;
            return serial;
         }
         /* the game code of a No-Intro game, not its dump, and not its name:
          * a hack or a translation built on it */
         r->idx = serial;
         r->score = xw_name_score(stem, d->g[serial].name);
         if (r->score > 79)
         {
            r->how = XW_MATCH_SERIAL;
            return serial;
         }
         r->how = XW_MATCH_HACK;
         return serial;
      }
   }
   if (!have_crc && h && strcmp(h->sys, "GBA") != 0)
      r->need_crc = 1;                 /* small: a checksum is cheap */
   for (i = 0; i < d->n; i++)
   {
      int s = xw_name_score(stem, d->g[i].name);
      if (s > bests)
      {
         bests = s;
         best = i;
      }
   }
   r->idx = best;
   r->score = bests;
   r->how = bests >= 80 ? XW_MATCH_FUZZY : XW_MATCH_NONE;
   return best;
}
