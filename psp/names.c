/* names.c -- see names.h. */
#include "names.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#include "gen/xmb_names.h"

#define MAXTOK 10

typedef struct
{
   const char *name;
   unsigned key;            /* hash of the folded title (tags cut) */
   unsigned char sys;       /* 'A' 'B' 'C' 'H' */
   unsigned char ntok;
   unsigned char region;    /* region class, see region_class() */
   unsigned char variant;   /* beta/proto/demo/virtual console... */
   unsigned tok[MAXTOK];
   float tw[MAXTOK];
   float wsum;
} rec;

static char *g_blob;
static rec *g_rec;
static int g_n;

/* ---- folding ---- */

static const char *STOP[] = { "the", "of", "and", "a", "an", "version", "edition", "game", "in", "to", "de", "no", NULL };

static int is_stop(const char *w, size_t n)
{
   int i;
   for (i = 0; STOP[i]; i++)
      if (strlen(STOP[i]) == n && memcmp(STOP[i], w, n) == 0)
         return 1;
   return 0;
}

/* fold a title to "lowercase words separated by one space": tags cut,
 * accents and & / _ folded, ", The" moved to the front */
static void fold(const char *s, char *out, size_t cap)
{
   char t[256];
   size_t n = 0, i;
   const unsigned char *p = (const unsigned char *)s;
   int space = 1;
   while (*p && n + 6 < sizeof(t))
   {
      unsigned c = *p;
      if (c == '(' || c == '[')
         break;
      if (c == 0xC3 && p[1])
      {
         /* Latin-1 supplement: fold the common accents */
         unsigned d = p[1] | 0x40;   /* 0xC3 0x80.. -> 0xC0.. */
         char f = 0;
         if (d >= 0xC0 && d <= 0xC5) f = 'a';
         else if (d == 0xC7) f = 'c';
         else if (d >= 0xC8 && d <= 0xCB) f = 'e';
         else if (d >= 0xCC && d <= 0xCF) f = 'i';
         else if (d == 0xD1) f = 'n';
         else if (d >= 0xD2 && d <= 0xD6) f = 'o';
         else if (d >= 0xD9 && d <= 0xDC) f = 'u';
         else if (d >= 0xE0 && d <= 0xE5) f = 'a';
         else if (d == 0xE7) f = 'c';
         else if (d >= 0xE8 && d <= 0xEB) f = 'e';
         else if (d >= 0xEC && d <= 0xEF) f = 'i';
         else if (d == 0xF1) f = 'n';
         else if (d >= 0xF2 && d <= 0xF6) f = 'o';
         else if (d >= 0xF9 && d <= 0xFC) f = 'u';
         p += 2;
         if (f) { t[n++] = f; space = 0; }
         continue;
      }
      if (c == '&' || c == '_' || c == '+')
      {
         if (!space) t[n++] = ' ';
         memcpy(t + n, "and ", 4);
         n += 4;
         space = 1;
         p++;
         continue;
      }
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
      {
         t[n++] = (char)c;
         space = 0;
      }
      else if (c >= 'A' && c <= 'Z')
      {
         t[n++] = (char)(c + 32);
         space = 0;
      }
      else if (c == '\'' )
         ;                                  /* "morrow's" = "morrows" */
      else if (!space)
      {
         t[n++] = ' ';
         space = 1;
      }
      p++;
   }
   while (n && t[n - 1] == ' ')
      n--;
   t[n] = 0;
   /* "legend of zelda the" (from ", The") -> "the legend of zelda" */
   if (n > 4 && strcmp(t + n - 4, " the") == 0)
   {
      t[n - 4] = 0;
      snprintf(out, cap, "the %s", t);
   }
   else
      snprintf(out, cap, "%s", t);
   for (i = 0; out[i]; i++)
      ;
}

static unsigned hash(const char *s, size_t n)
{
   unsigned h = 2166136261u;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ (unsigned char)s[i]) * 16777619u;
   return h ? h : 1;
}

static int has_digit(const char *w, size_t n)
{
   size_t i;
   for (i = 0; i < n; i++)
      if (w[i] >= '0' && w[i] <= '9')
         return 1;
   return 0;
}

static void tokens(const char *folded, rec *r)
{
   const char *p = folded;
   r->ntok = 0;
   r->wsum = 0;
   while (*p && r->ntok < MAXTOK)
   {
      const char *e = strchr(p, ' ');
      size_t n = e ? (size_t)(e - p) : strlen(p);
      float w = is_stop(p, n) ? 0.3f : has_digit(p, n) ? 1.2f : 1.0f;
      r->tok[r->ntok] = (hash(p, n) & ~1u) | (has_digit(p, n) ? 1u : 0u);
      r->tw[r->ntok] = w;
      r->wsum += w;
      r->ntok++;
      p = e ? e + 1 : p + n;
   }
}

/* region class of the first tag group: 1 USA/World/USA+Europe, 2 Europe and
 * its countries, 3 Japan, 4 other, 0 none */
static int region_class(const char *name)
{
   const char *t = strchr(name, '(');
   char g[96];
   size_t n;
   const char *e;
   if (!t)
      return 0;
   e = strchr(t, ')');
   n = e ? (size_t)(e - t - 1) : 0;
   if (n >= sizeof(g)) n = sizeof(g) - 1;
   memcpy(g, t + 1, n);
   g[n] = 0;
   if (strstr(g, "USA") || strstr(g, "World")) return 1;
   if (strstr(g, "Europe")) return 2;
   if (strstr(g, "Japan")) return 3;
   if (strstr(g, "Germany") || strstr(g, "France") || strstr(g, "Spain") || strstr(g, "Italy") ||
       strstr(g, "Netherlands") || strstr(g, "Sweden") || strstr(g, "UK"))
      return 2;
   return 4;
}

static int variant_of(const char *name)
{
   static const char *V[] = { "(Beta", "(Proto", "(Demo", "(Sample", "(Virtual Console", "(Kiosk", "(Promo",
                              "(Aftermarket", "(Unl", "(Pirate", "(Alt", NULL };
   int i;
   for (i = 0; V[i]; i++)
      if (strstr(name, V[i]))
         return 1 + i;
   return 0;
}

int names_init(void)
{
   uLongf n = XMB_NAMES_RAW + 1;
   char *p, fold_buf[256];
   int i = 0;
   if (g_rec)
      return 0;
   g_blob = (char *)malloc(n);
   g_rec = (rec *)calloc(XMB_NAMES_COUNT + 1, sizeof(rec));
   if (!g_blob || !g_rec)
      return -1;
   if (uncompress((Bytef *)g_blob, &n, xmb_names_z, sizeof(xmb_names_z)) != Z_OK)
      return -1;
   g_blob[n] = 0;
   p = g_blob;
   while (*p && i < XMB_NAMES_COUNT)
   {
      char *e = strchr(p, '\n');
      if (!e)
         break;
      *e = 0;
      if (p[1] == '\t')
      {
         rec *r = &g_rec[i++];
         r->sys = (unsigned char)p[0];
         r->name = p + 2;
         fold(r->name, fold_buf, sizeof(fold_buf));
         r->key = hash(fold_buf, strlen(fold_buf));
         tokens(fold_buf, r);
         r->region = (unsigned char)region_class(r->name);
         r->variant = (unsigned char)variant_of(r->name);
      }
      p = e + 1;
   }
   g_n = i;
   return 0;
}

int names_count(void)
{
   return g_n;
}

static float similarity(const rec *a, const rec *b)
{
   float common = 0;
   int i, j, digits_a = 0, digits_b = 0, digits_common = 0;
   for (i = 0; i < a->ntok; i++)
   {
      if (a->tok[i] & 1) digits_a++;
      for (j = 0; j < b->ntok; j++)
         if (a->tok[i] == b->tok[j])
         {
            common += a->tw[i];
            if (a->tok[i] & 1) digits_common++;
            break;
         }
   }
   for (j = 0; j < b->ntok; j++)
      if (b->tok[j] & 1) digits_b++;
   if (a->wsum + b->wsum <= 0)
      return 0;
   {
      float s = 2 * common / (a->wsum + b->wsum);
      /* "Advance Wars 2" names "Advance Wars 2 - Black Hole Rising": every
       * word of a (two-word or longer) query inside the title counts nearly
       * as a match; the overlap still ranks the shortest such title first */
      if (a->wsum >= 1.9f && common >= a->wsum - 0.01f)
         s = 0.88f + 0.1f * s;
      if (digits_a != digits_common || digits_b != digits_common)
         s *= 0.72f;          /* "Advance Wars" is not "Advance Wars 2" */
      return s;
   }
}

static int sys_char(int sys)
{
   return sys == SYS_GBA ? 'A' : sys == SYS_GB ? 'B' : 'C';
}

static int region_rank(int rc, int rom_rc, int pref)
{
   /* lower is better */
   static const int order[3][5] = {
      { 9, 0, 1, 2, 3 },     /* USA/Europe first: none, usa, eur, jpn, other */
      { 9, 1, 0, 2, 3 },     /* Europe first */
      { 9, 1, 2, 0, 3 },     /* Japan first */
   };
   if (rom_rc && rc == rom_rc)
      return -1;
   return order[pref < 0 || pref > 2 ? 0 : pref][rc > 4 ? 4 : rc];
}

void names_match(const char *stem, int sys, int pref, name_match *m)
{
   char fb[256];
   rec q;
   int i, best = -1, best_hero = -1, pass;
   float bs = 0, hs = 0;
   int want = sys_char(sys), rom_rc = region_class(stem);
   memset(m, 0, sizeof(*m));
   if (!g_n)
      return;
   memset(&q, 0, sizeof(q));
   fold(stem, fb, sizeof(fb));
   q.key = hash(fb, strlen(fb));
   tokens(fb, &q);
   /* the ROM's own system first; GB and GBC titles are each other's fallback */
   for (pass = 0; pass < 2 && best < 0; pass++)
   {
      int w = pass == 0 ? want : (want == 'A' ? 0 : want == 'B' ? 'C' : 'B');
      if (!w)
         break;
      for (i = 0; i < g_n; i++)
      {
         const rec *r = &g_rec[i];
         float s;
         if (r->sys != w)
            continue;
         if (strcmp(r->name, stem) == 0)
         {
            best = i;
            bs = 1.01f;
            break;
         }
         s = r->key == q.key ? 1.0f : similarity(&q, r);
         /* among equal scores prefer the clean release in the right region */
         s -= r->variant ? 0.002f : 0;
         s -= (region_rank(r->region, rom_rc, pref) + 1) * 0.0002f;
         if (s > bs)
         {
            bs = s;
            best = i;
         }
      }
      if (bs < 0.5f && pass == 0)
         best = -1, bs = 0;
   }
   for (i = 0; i < g_n; i++)
   {
      const rec *r = &g_rec[i];
      float s;
      if (r->sys != 'H')
         continue;
      if (strcmp(r->name, stem) == 0) { best_hero = i; hs = 1.01f; break; }
      s = r->key == q.key ? 1.0f : similarity(&q, r);
      if (s > hs) { hs = s; best_hero = i; }
   }
   if (best_hero >= 0 && hs >= 0.86f)
      m->hero = g_rec[best_hero].name;
   if (best >= 0)
   {
      const rec *b = &g_rec[best];
      int conf = bs >= 1.0f ? (bs > 1.005f ? 100 : 97) : (int)(bs * 100 + 0.5f);
      int n = 0;
      m->nointro = b->name;
      m->conf = conf;
      /* the candidates: the title's names, best region first */
      for (i = 0; i < g_n && n < NAMES_MAXCAND * 4; i++)
      {
         const rec *r = &g_rec[i];
         int c, rr, k, slot;
         if (r->sys != b->sys || r->key != b->key)
            continue;
         rr = region_rank(r->region, rom_rc, pref);
         c = conf;
         if (r != b)
            c -= rr <= 0 ? 2 : rr == 1 ? 7 : rr == 2 ? 24 : 27;
         if (r->variant)
            c -= 30;
         if (c < 1) c = 1;
         /* insert sorted by c */
         slot = m->nbox < NAMES_MAXCAND ? m->nbox : NAMES_MAXCAND - 1;
         if (m->nbox == NAMES_MAXCAND && m->box[slot].conf >= c)
            continue;
         for (k = slot; k > 0 && m->box[k - 1].conf < c; k--)
            m->box[k] = m->box[k - 1];
         m->box[k].name = r->name;
         m->box[k].conf = c;
         m->box[k].below = c < 60;
         m->box[k].sys = (char)r->sys;
         if (m->nbox < NAMES_MAXCAND)
            m->nbox++;
         n++;
      }
      if (conf < MATCH_LINE)
      {
         /* nearest only: keep one candidate, marked below the line */
         m->nbox = 1;
         m->box[0].name = b->name;
         m->box[0].conf = conf;
         m->box[0].below = 1;
         m->box[0].sys = (char)b->sys;
      }
      m->recognised = conf >= MATCH_LINE;
   }
   if (!m->recognised && m->hero)
   {
      m->recognised = 1;
      m->hack = 1;
      m->conf = hs > 1.005f ? 100 : (int)(hs * 100 + 0.5f);
   }
}

void names_plain_title(const char *name, char *out, unsigned cap)
{
   unsigned n = 0;
   const char *p = name;
   while (*p && n + 1 < cap)
   {
      if (*p == '(' || *p == '[')
         break;
      out[n++] = *p == '_' ? '&' : *p;
      p++;
   }
   while (n && out[n - 1] == ' ')
      n--;
   out[n] = 0;
   /* ", The" back to the front */
   {
      char *c = strstr(out, ", The");
      if (c && (c[5] == 0 || c[5] == ' '))
      {
         char t[256];
         *c = 0;
         snprintf(t, sizeof(t), "The %s%s", out, c + 5);
         snprintf(out, cap, "%s", t);
      }
   }
}

void names_short_title(const char *name, char *out, unsigned cap)
{
   char t[256], *d;
   size_t n;
   names_plain_title(name, t, sizeof(t));
   n = strlen(t);
   d = strstr(t, " - ");
   /* "Pokemon - Emerald Version" -> "Pokemon Emerald" */
   if (d && n > 8 && strcmp(t + n - 8, " Version") == 0)
   {
      t[n - 8] = 0;
      memmove(d + 1, d + 3, strlen(d + 3) + 1);
      snprintf(out, cap, "%s", t);
      return;
   }
   if (d)
   {
      /* "Mario Kart - Super Circuit" -> "Mario Kart Super Circuit" when the
       * whole is short; longer ones keep a colon to stay readable */
      if (n <= 30)
         memmove(d + 1, d + 3, strlen(d + 3) + 1);
      else
      {
         d[0] = ':';
         memmove(d + 1, d + 2, strlen(d + 2) + 1);
      }
   }
   snprintf(out, cap, "%s", t);
}

void names_region(const char *name, char *out, unsigned cap)
{
   const char *t = strchr(name, '('), *e;
   out[0] = 0;
   if (!t)
      return;
   e = strchr(t, ')');
   if (!e)
      return;
   snprintf(out, cap, "%.*s", (int)(e - t - 1), t + 1);
}
