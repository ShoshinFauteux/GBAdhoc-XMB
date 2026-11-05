/* art.c -- see art.h. */
#include "art.h"
#include "net.h"
#include "util.h"
#include "../shared/xw_core.h"
#include "../shared/xw_match.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const struct
{
   const char *sys, *dat, *repo, *tn;
} SYS[3] = {
   { "GBA", "Nintendo%20-%20Game%20Boy%20Advance.dat", "Nintendo_-_Game_Boy_Advance", "Nintendo%20-%20Game%20Boy%20Advance" },
   { "GBC", "Nintendo%20-%20Game%20Boy%20Color.dat", "Nintendo_-_Game_Boy_Color", "Nintendo%20-%20Game%20Boy%20Color" },
   { "GB", "Nintendo%20-%20Game%20Boy.dat", "Nintendo_-_Game_Boy", "Nintendo%20-%20Game%20Boy" },
};

#define HERO_REPO "ShoshinFauteux/GBAdhoc-heroart"

static CRITICAL_SECTION g_cs;
static xw_dat g_dat[3];
static int g_dat_loaded[3];

/* a list of names (thumbnail stems, hero masters) */
typedef struct
{
   char **s;
   int n;
   int loaded;
} names;

static names g_thumbs[3], g_heroes;

static int sys_index(const char *sys)
{
   int i;
   for (i = 0; i < 3; i++)
      if (!strcmp(SYS[i].sys, sys))
         return i;
   return 0;
}

void art_init(void)
{
   InitializeCriticalSection(&g_cs);
}

void art_cache_dir(char *out, size_t cap)
{
   u_appdata("cache", out, cap);
}

static void cache_path(const char *sub, char *out, size_t cap)
{
   char c[1024];
   art_cache_dir(c, sizeof(c));
   u_join(out, cap, c, sub);
}

static long long file_age(const char *path)
{
   WIN32_FILE_ATTRIBUTE_DATA d;
   wchar_t *w = u_wide(path);
   BOOL ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, &d);
   ULARGE_INTEGER t;
   free(w);
   if (!ok)
      return -1;
   t.LowPart = d.ftLastWriteTime.dwLowDateTime;
   t.HighPart = d.ftLastWriteTime.dwHighDateTime;
   return net_now() - (long long)(t.QuadPart / 10000000ULL - 11644473600ULL);
}

static void touch(const char *path)
{
   wchar_t *w = u_wide(path);
   HANDLE h = w ? CreateFileW(w, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)
                : INVALID_HANDLE_VALUE;
   free(w);
   if (h != INVALID_HANDLE_VALUE)
   {
      FILETIME ft;
      GetSystemTimeAsFileTime(&ft);
      SetFileTime(h, NULL, NULL, &ft);
      CloseHandle(h);
   }
}

static void read_text(const char *path, char *out, size_t cap)
{
   xw_buf b;
   out[0] = '\0';
   if (xw_read_file(path, &b) == 0)
   {
      size_t n = b.len < cap - 1 ? b.len : cap - 1;
      memcpy(out, b.data, n);
      out[n] = '\0';
      while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
         out[--n] = '\0';
      xw_buf_free(&b);
   }
}

/* GET into a cache file, revalidated with its ETag.  Returns NET_*; on
 * NET_OK the file is fresh (200) or still right (304). */
static int fetch_cached(const char *url, const char *path, net_resp *keep)
{
   char etp[1100], etag[160];
   net_resp r;
   int rc;
   snprintf(etp, sizeof(etp), "%s.etag", path);
   read_text(etp, etag, sizeof(etag));
   rc = net_get(url, u_is_file(path) ? etag : NULL, &r);
   if (rc == NET_OK && r.status == 200 && r.body.len)
   {
      char dir[1100], *sl;
      snprintf(dir, sizeof(dir), "%s", path);
      sl = strrchr(dir, '/');
      if (sl)
      {
         *sl = '\0';
         u_mkdirs(dir);
      }
      xw_write_file(path, r.body.data, r.body.len);
      if (r.etag[0])
         xw_write_file(etp, r.etag, strlen(r.etag));
   }
   else if (rc == NET_OK && r.status == 304)
      touch(path);
   if (keep)
      *keep = r;
   else
      net_resp_free(&r);
   return rc;
}

/* ---- No-Intro DATs ------------------------------------------------------ */

static int load_dat(int i)
{
   char p[1100];
   xw_buf b;
   if (g_dat_loaded[i])
      return 1;
   snprintf(p, sizeof(p), "dat/%s.dat", SYS[i].sys);
   {
      char full[1100];
      cache_path(p, full, sizeof(full));
      if (xw_read_file(full, &b) != 0)
         return 0;
   }
   xw_dat_parse((const char *)b.data, b.len, &g_dat[i]);
   xw_buf_free(&b);
   g_dat_loaded[i] = g_dat[i].n > 0;
   u_log("dat %s: %d games", SYS[i].sys, g_dat[i].n);
   return g_dat_loaded[i];
}

int art_prepare_dats(int want_gba, int want_gbc, int want_gb, psp_prog cb, void *ctx)
{
   int want[3] = { want_gba, want_gbc, want_gb }, i, have = 0;
   for (i = 0; i < 3; i++)
   {
      char rel[64], full[1100], url[400];
      long long age;
      if (!want[i])
         continue;
      snprintf(rel, sizeof(rel), "dat/%s.dat", SYS[i].sys);
      cache_path(rel, full, sizeof(full));
      age = file_age(full);
      if (age < 0 || age > 14 * 86400)
      {
         if (cb)
         {
            char m[200];
            snprintf(m, sizeof(m), "{\"msg\":\"Getting the No-Intro list for the %s…\",\"file\":\"libretro-database\"}",
                     i == 0 ? "Game Boy Advance" : i == 1 ? "Game Boy Color" : "Game Boy");
            cb(ctx, m);
         }
         snprintf(url, sizeof(url), "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/%s", SYS[i].dat);
         if (fetch_cached(url, full, NULL) == NET_OK)
         {
            EnterCriticalSection(&g_cs);
            if (g_dat_loaded[i])
            {
               xw_dat_free(&g_dat[i]);
               g_dat_loaded[i] = 0;
            }
            LeaveCriticalSection(&g_cs);
         }
      }
      EnterCriticalSection(&g_cs);
      have += load_dat(i);
      LeaveCriticalSection(&g_cs);
   }
   return have;
}

/* ---- the checksum cache (a big ROM is read over USB once) ---------------- */

typedef struct
{
   char key[1200];
   unsigned long crc;
} crc_rec;

static crc_rec *g_crcs;
static int g_ncrc, g_crc_loaded;

static void crc_key(const char *path, long long size, char *out, size_t cap)
{
   WIN32_FILE_ATTRIBUTE_DATA d;
   wchar_t *w = u_wide(path);
   unsigned long long mt = 0;
   if (w && GetFileAttributesExW(w, GetFileExInfoStandard, &d))
      mt = (unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32 | d.ftLastWriteTime.dwLowDateTime;
   free(w);
   snprintf(out, cap, "%s|%lld|%llu", path, size, mt);
}

static void crc_load(void)
{
   char p[1100];
   xw_buf b;
   char *s, *line;
   if (g_crc_loaded)
      return;
   g_crc_loaded = 1;
   cache_path("crc.txt", p, sizeof(p));
   if (xw_read_file(p, &b) != 0)
      return;
   s = (char *)realloc(b.data, b.len + 1);
   if (!s)
   {
      xw_buf_free(&b);
      return;
   }
   s[b.len] = '\0';
   for (line = strtok(s, "\n"); line; line = strtok(NULL, "\n"))
   {
      char *t = strrchr(line, '\t');
      crc_rec *nr;
      if (!t)
         continue;
      *t = '\0';
      nr = (crc_rec *)realloc(g_crcs, (size_t)(g_ncrc + 1) * sizeof(*nr));
      if (!nr)
         break;
      g_crcs = nr;
      snprintf(g_crcs[g_ncrc].key, sizeof(g_crcs[0].key), "%s", line);
      g_crcs[g_ncrc].crc = strtoul(t + 1, NULL, 16);
      g_ncrc++;
   }
   free(s);
}

static int crc_of(const char *path, long long size, unsigned long *crc)
{
   char key[1200], p[1100];
   int i;
   EnterCriticalSection(&g_cs);
   crc_load();
   crc_key(path, size, key, sizeof(key));
   for (i = 0; i < g_ncrc; i++)
      if (!strcmp(g_crcs[i].key, key))
      {
         *crc = g_crcs[i].crc;
         LeaveCriticalSection(&g_cs);
         return 0;
      }
   LeaveCriticalSection(&g_cs);
   if (u_file_crc(path, crc) != 0)
      return -1;
   EnterCriticalSection(&g_cs);
   {
      crc_rec *nr = (crc_rec *)realloc(g_crcs, (size_t)(g_ncrc + 1) * sizeof(*nr));
      if (nr)
      {
         FILE *f;
         wchar_t *w;
         g_crcs = nr;
         snprintf(g_crcs[g_ncrc].key, sizeof(g_crcs[0].key), "%s", key);
         g_crcs[g_ncrc].crc = *crc;
         g_ncrc++;
         cache_path("crc.txt", p, sizeof(p));
         w = u_wide(p);
         f = w ? _wfopen(w, L"ab") : NULL;
         free(w);
         if (f)
         {
            fprintf(f, "%s\t%08lx\n", key, *crc);
            fclose(f);
         }
      }
   }
   LeaveCriticalSection(&g_cs);
   return 0;
}

/* ---- recognising a ROM -------------------------------------------------- */

static const char *how_name(int how)
{
   switch (how)
   {
   case XW_MATCH_CRC: return "crc";
   case XW_MATCH_NAME: return "name";
   case XW_MATCH_SERIAL: return "serial";
   case XW_MATCH_FUZZY: return "fuzzy";
   case XW_MATCH_HACK: return "hack";
   default: return "none";
   }
}

/* "Pokemon MonoMyth v1.2" -> "Pokemon MonoMyth"; "mygame_final2 (1)" ->
 * "mygame_final2 (1)" stays (it is the player's own name). */
static void clean_stem(const char *stem, char *out, size_t cap, int strip_tags)
{
   char t[512];
   size_t n;
   if (strip_tags)
      xw_nointro_base(stem, t, sizeof(t));
   else
      snprintf(t, sizeof(t), "%s", stem);
   n = strlen(t);
   /* a trailing version: " v1.2", " V2", " 1.0" */
   {
      char *sp = strrchr(t, ' ');
      if (sp && sp != t)
      {
         const char *q = sp + 1;
         if (*q == 'v' || *q == 'V')
            q++;
         if (*q >= '0' && *q <= '9')
         {
            while ((*q >= '0' && *q <= '9') || *q == '.')
               q++;
            if (!*q)
               *sp = '\0';
         }
      }
   }
   (void)n;
   snprintf(out, cap, "%s", t);
}

static int nointro_like(const char *stem)
{
   static const char *regions[] = { "(USA", "(Europe", "(Japan", "(World", "(Germany", "(France",
                                    "(Spain", "(Italy", "(Australia", "(Korea", "(Asia" };
   size_t i;
   for (i = 0; i < sizeof(regions) / sizeof(regions[0]); i++)
      if (strstr(stem, regions[i]))
         return 1;
   return 0;
}

void art_identify(const char *path, long long size, art_ident *id)
{
   unsigned char head[0x150];
   size_t got = 0;
   xw_rom_header h;
   xw_match_res r;
   char stem[512];
   int si, tries[2], nt = 0, t, best_dat = -1;
   xw_match_res best = { -1, 0, XW_MATCH_NONE, 0 };

   memset(id, 0, sizeof(*id));
   u_stem(path, stem, sizeof(stem));
   u_read_head(path, head, sizeof(head), &got);
   xw_rom_header_parse(head, got, u_ext(path), &h);
   snprintf(id->sys, sizeof(id->sys), "%s", h.sys);
   snprintf(id->code, sizeof(id->code), "%s", h.code);
   si = sys_index(h.sys);
   tries[nt++] = si;
   if (si == 1) tries[nt++] = 2;         /* GBC games filed as GB, and back */
   if (si == 2) tries[nt++] = 1;

   for (t = 0; t < nt; t++)
   {
      int i = tries[t];
      EnterCriticalSection(&g_cs);
      load_dat(i);
      LeaveCriticalSection(&g_cs);
      if (!g_dat_loaded[i])
         continue;
      xw_dat_match(&g_dat[i], stem, &h, 0, 0, &r);
      if (r.need_crc && size > 0 && size <= 64LL * 1048576)
      {
         unsigned long crc;
         if (crc_of(path, size, &crc) == 0)
            xw_dat_match(&g_dat[i], stem, &h, 1, crc, &r);
      }
      if (r.idx >= 0 && (best.idx < 0 || r.score > best.score ||
                         (r.how != XW_MATCH_NONE && best.how == XW_MATCH_NONE)))
      {
         best = r;
         best_dat = i;
      }
      if (best.how == XW_MATCH_CRC || best.how == XW_MATCH_NAME)
         break;
   }

   if (best_dat >= 0 && best.how != XW_MATCH_NONE)
   {
      const xw_dat_game *g = &g_dat[best_dat].g[best.idx];
      snprintf(id->how, sizeof(id->how), "%s", how_name(best.how));
      if (best.how == XW_MATCH_HACK)
      {
         char c[256];
         id->hack = 1;
         id->recognised = 1;
         id->confidence = 85;
         snprintf(id->base, sizeof(id->base), "%s", g->name);
         clean_stem(stem, c, sizeof(c), 1);
         /* "Fire Emblem - The Morrow's..." reads as the XMB titles do */
         xw_nointro_display(c, id->title, sizeof(id->title));
         snprintf(id->nointro, sizeof(id->nointro), "%s", stem);
      }
      else
      {
         id->recognised = 1;
         id->confidence = best.score;
         snprintf(id->nointro, sizeof(id->nointro), "%s", g->name);
         xw_nointro_display(g->name, id->title, sizeof(id->title));
      }
      /* the DAT's system wins over the extension (a .gb that is GBC) */
      snprintf(id->sys, sizeof(id->sys), "%s", SYS[best_dat].sys);
      if (!strcmp(h.sys, "GBC") && best_dat == 2)
         snprintf(id->sys, sizeof(id->sys), "GBC");
      return;
   }
   if (!g_dat_loaded[si] && nointro_like(stem))
   {
      /* no list yet (offline on first run): a No-Intro-style name is
       * trusted, the rest waits for the list */
      id->recognised = 1;
      id->confidence = 90;
      snprintf(id->how, sizeof(id->how), "name");
      snprintf(id->nointro, sizeof(id->nointro), "%s", stem);
      xw_nointro_display(stem, id->title, sizeof(id->title));
      return;
   }
   snprintf(id->how, sizeof(id->how), "none");
   clean_stem(stem, id->title, sizeof(id->title), 0);
}

/* ---- name lists: libretro thumbnails, hero masters ---------------------- */

static void names_free(names *l)
{
   int i;
   for (i = 0; i < l->n; i++)
      free(l->s[i]);
   free(l->s);
   l->s = NULL;
   l->n = l->loaded = 0;
}

static void names_from_text(names *l, const char *text)
{
   const char *p = text;
   names_free(l);
   while (*p)
   {
      const char *e = strchr(p, '\n');
      size_t n = e ? (size_t)(e - p) : strlen(p);
      if (n)
      {
         char **ns = (char **)realloc(l->s, (size_t)(l->n + 1) * sizeof(char *));
         if (!ns)
            break;
         l->s = ns;
         l->s[l->n] = (char *)malloc(n + 1);
         memcpy(l->s[l->n], p, n);
         l->s[l->n][n] = '\0';
         l->n++;
      }
      if (!e)
         break;
      p = e + 1;
   }
   l->loaded = 1;
}

/* every "path": "<prefix>X.png" in a GitHub tree listing -> "X\n" */
static void tree_names(const char *json, const char *prefix, sb *out)
{
   const char *p = json;
   size_t pl = strlen(prefix);
   while ((p = strstr(p, "\"path\"")) != NULL)
   {
      const char *q = strchr(p + 6, '"'), *e;
      if (!q)
         break;
      q++;
      e = q;
      while (*e && *e != '"')
      {
         if (*e == '\\' && e[1])
            e++;
         e++;
      }
      if ((size_t)(e - q) > pl + 4 && strncmp(q, prefix, pl) == 0 && strncmp(e - 4, ".png", 4) == 0)
      {
         /* unescape \" and \\ ; \uXXXX left to the JSON parser below */
         sb_addn(out, q + pl, (size_t)(e - q - pl - 4));
         sb_add(out, "\n");
      }
      p = e;
   }
}

/* JSON strings in tree listings may hold \u escapes; decode the list. */
static void decode_list(sb *raw, sb *out)
{
   char *line = raw->s, *nl;
   while (line && *line)
   {
      nl = strchr(line, '\n');
      if (nl)
         *nl = '\0';
      if (strchr(line, '\\'))
      {
         sb q;
         jv *v;
         sb_init(&q);
         sb_add(&q, "\"");
         sb_add(&q, line);
         sb_add(&q, "\"");
         v = jv_parse(q.s);
         sb_add(out, v && v->t == JV_STR ? v->s : line);
         jv_free(v);
         sb_free(&q);
      }
      else
         sb_add(out, line);
      sb_add(out, "\n");
      if (!nl)
         break;
      line = nl + 1;
   }
}

/* libretro-thumbnails' Named_Boxarts list for one system.  0 = loaded;
 * else a NET_* why not. */
static int load_thumbs(int i, int *from_mirror)
{
   char rel[64], full[1100], url[400];
   long long age;
   int rc = NET_OK;
   *from_mirror = 0;
   if (g_thumbs[i].loaded)
      return 0;
   snprintf(rel, sizeof(rel), "index/%s.boxarts.txt", SYS[i].sys);
   cache_path(rel, full, sizeof(full));
   age = file_age(full);
   if (age < 0 || age > 7 * 86400)
   {
      /* GitHub: the repository's tree, then Named_Boxarts' own tree */
      char tp[1100];
      net_resp r;
      snprintf(rel, sizeof(rel), "index/%s.root.json", SYS[i].sys);
      cache_path(rel, tp, sizeof(tp));
      snprintf(url, sizeof(url), "https://api.github.com/repos/libretro-thumbnails/%s/git/trees/master", SYS[i].repo);
      rc = net_rate_reset() ? NET_RATE : fetch_cached(url, tp, &r);
      if (rc == NET_OK)
      {
         xw_buf b;
         net_resp_free(&r);
         if (xw_read_file(tp, &b) == 0)
         {
            char *t = (char *)realloc(b.data, b.len + 1), *at, sha[64] = "";
            if (t)
            {
               t[b.len] = '\0';
               at = strstr(t, "\"path\": \"Named_Boxarts\"");
               if (!at)
                  at = strstr(t, "\"path\":\"Named_Boxarts\"");
               if (at && (at = strstr(at, "\"sha\"")) != NULL && (at = strchr(at + 5, '"')) != NULL)
               {
                  char *e = strchr(at + 1, '"');
                  if (e && e - at - 1 < (long)sizeof(sha))
                  {
                     memcpy(sha, at + 1, (size_t)(e - at - 1));
                     sha[e - at - 1] = '\0';
                  }
               }
               free(t);
            }
            if (sha[0])
            {
               char lp[1100];
               snprintf(rel, sizeof(rel), "index/%s.boxarts.json", SYS[i].sys);
               cache_path(rel, lp, sizeof(lp));
               snprintf(url, sizeof(url), "https://api.github.com/repos/libretro-thumbnails/%s/git/trees/%s", SYS[i].repo, sha);
               rc = fetch_cached(url, lp, NULL);
               if (rc == NET_OK && xw_read_file(lp, &b) == 0)
               {
                  char *t2 = (char *)realloc(b.data, b.len + 1);
                  if (t2)
                  {
                     sb raw, dec;
                     t2[b.len] = '\0';
                     sb_init(&raw);
                     sb_init(&dec);
                     sb_add(&raw, "");
                     sb_add(&dec, "");
                     tree_names(t2, "", &raw);
                     decode_list(&raw, &dec);
                     if (dec.n > 10)
                        xw_write_file(full, dec.s, dec.n);
                     sb_free(&raw);
                     sb_free(&dec);
                     free(t2);
                  }
               }
            }
         }
      }
      if (rc != NET_OK && !u_is_file(full))
      {
         /* GitHub said no (rate limit) or is unreachable: libretro's own
          * thumbnail server mirrors the same repository */
         snprintf(url, sizeof(url), "https://thumbnails.libretro.com/%s/Named_Boxarts/", SYS[i].tn);
         if (net_get(url, NULL, &r) == NET_OK && r.status == 200 && r.body.len)
         {
            sb raw;
            const char *p = (const char *)r.body.data;
            sb_init(&raw);
            sb_add(&raw, "");
            while ((p = strstr(p, "href=\"")) != NULL)
            {
               const char *q = p + 6, *e = strchr(q, '"');
               if (!e)
                  break;
               if (e - q > 4 && !strncmp(e - 4, ".png", 4))
               {
                  char nm[600];
                  size_t n = (size_t)(e - q - 4);
                  if (n < sizeof(nm))
                  {
                     memcpy(nm, q, n);
                     nm[n] = '\0';
                     u_urldec(nm);
                     /* the mirror's index is HTML: &amp; */
                     {
                        char *a;
                        while ((a = strstr(nm, "&amp;")) != NULL)
                           memmove(a + 1, a + 5, strlen(a + 5) + 1);
                     }
                     sb_add(&raw, nm);
                     sb_add(&raw, "\n");
                  }
               }
               p = e;
            }
            if (raw.n > 10)
            {
               xw_write_file(full, raw.s, raw.n);
               *from_mirror = 1;
               rc = NET_OK;
            }
            sb_free(&raw);
         }
         net_resp_free(&r);
      }
   }
   if (u_is_file(full))
   {
      xw_buf b;
      if (xw_read_file(full, &b) == 0)
      {
         char *t = (char *)realloc(b.data, b.len + 1);
         if (t)
         {
            t[b.len] = '\0';
            EnterCriticalSection(&g_cs);
            names_from_text(&g_thumbs[i], t);
            LeaveCriticalSection(&g_cs);
            free(t);
         }
      }
      return 0;
   }
   return rc == NET_OK ? NET_HTTP : rc;
}

static int load_heroes(void)
{
   char full[1100], js[1100], url[300];
   long long age;
   int rc = NET_OK;
   if (g_heroes.loaded)
      return 0;
   cache_path("index/heroes.txt", full, sizeof(full));
   cache_path("index/heroes.json", js, sizeof(js));
   age = file_age(full);
   if (age < 0 || age > 86400)
   {
      snprintf(url, sizeof(url), "https://api.github.com/repos/%s/git/trees/HEAD?recursive=1", HERO_REPO);
      rc = net_rate_reset() ? NET_RATE : fetch_cached(url, js, NULL);
      if (rc == NET_OK)
      {
         xw_buf b;
         if (xw_read_file(js, &b) == 0)
         {
            char *t = (char *)realloc(b.data, b.len + 1);
            if (t)
            {
               sb raw, dec;
               t[b.len] = '\0';
               sb_init(&raw);
               sb_init(&dec);
               sb_add(&raw, "");
               sb_add(&dec, "");
               tree_names(t, "masters/", &raw);
               decode_list(&raw, &dec);
               xw_write_file(full, dec.s, dec.n);
               sb_free(&raw);
               sb_free(&dec);
               free(t);
            }
         }
      }
   }
   if (u_is_file(full))
   {
      xw_buf b;
      if (xw_read_file(full, &b) == 0)
      {
         char *t = (char *)realloc(b.data, b.len + 1);
         if (t)
         {
            t[b.len] = '\0';
            EnterCriticalSection(&g_cs);
            names_from_text(&g_heroes, t);
            LeaveCriticalSection(&g_cs);
            free(t);
         }
      }
      return 0;
   }
   return rc == NET_OK ? NET_HTTP : rc;
}

/* ---- candidates --------------------------------------------------------- */

typedef struct
{
   char name[400];       /* thumbnail stem */
   char region[160];
   int conf;
   int below;
} cand;

static int region_rank(const char *region, const char *pref)
{
   /* lower is better */
   const char *order[3][4] = {
      { "USA", "World", "Europe", "Japan" },
      { "Europe", "World", "USA", "Japan" },
      { "Japan", "World", "USA", "Europe" },
   };
   int o = 0, k;
   if (pref && strstr(pref, "Europe first")) o = 1;
   else if (pref && strstr(pref, "Japan")) o = 2;
   for (k = 0; k < 4; k++)
      if (strstr(region, order[o][k]))
         return k;
   return 4;
}

static int cand_cmp(const void *a, const void *b)
{
   return ((const cand *)b)->conf - ((const cand *)a)->conf;
}

/* every tag of a No-Intro name, "USA · Rev 1 · Virtual Console" */
static void tags_label(const char *name, char *out, size_t cap)
{
   const char *p = name;
   size_t n = 0;
   out[0] = '\0';
   while ((p = strpbrk(p, "([")) != NULL)
   {
      const char *e = strchr(p, *p == '(' ? ')' : ']');
      size_t l;
      if (!e)
         break;
      l = (size_t)(e - p - 1);
      if (n && n + 4 < cap)
      {
         memcpy(out + n, " \xc2\xb7 ", 4);
         n += 4;
      }
      if (n + l + 1 < cap)
      {
         memcpy(out + n, p + 1, l);
         n += l;
      }
      out[n] = '\0';
      p = e + 1;
   }
}

static int box_candidates(int si, const char *nointro, int below, const char *pref,
                          cand *out, int cap)
{
   char want[400], base[256], kb[256], kt[256], tb[256];
   int i, n = 0;
   names *l = &g_thumbs[si];
   xw_thumb_name(nointro, want, sizeof(want));
   xw_nointro_base(want, base, sizeof(base));
   xw_norm_key(base, kb, sizeof(kb));
   for (i = 0; i < l->n && n < cap; i++)
   {
      const char *t = l->s[i];
      xw_nointro_base(t, tb, sizeof(tb));
      xw_norm_key(tb, kt, sizeof(kt));
      if (strcmp(kt, kb) != 0)
         continue;
      snprintf(out[n].name, sizeof(out[n].name), "%s", t);
      tags_label(t, out[n].region, sizeof(out[n].region));
      if (!out[n].region[0])
         snprintf(out[n].region, sizeof(out[n].region), "No region");
      if (!strcmp(t, want))
         out[n].conf = 98;
      else
      {
         int rk = region_rank(out[n].region, pref);
         out[n].conf = rk == 0 ? 90 - (int)(strlen(t) > strlen(want) ? 2 : 0) : 76 - 2 * rk;
      }
      out[n].below = below;
      if (below)
         out[n].conf = out[n].conf * 45 / 100;
      n++;
   }
   qsort(out, (size_t)n, sizeof(*out), cand_cmp);
   return n;
}

static int nearest_candidate(int si, const char *name, cand *out)
{
   int i, best = -1, bs = 0;
   names *l = &g_thumbs[si];
   for (i = 0; i < l->n; i++)
   {
      int s = xw_name_score(name, l->s[i]);
      if (s > bs)
      {
         bs = s;
         best = i;
      }
   }
   if (best < 0 || bs < 55)
      return 0;
   snprintf(out->name, sizeof(out->name), "%s", l->s[best]);
   xw_nointro_region(l->s[best], out->region, sizeof(out->region));
   out->conf = bs * 45 / 100;
   out->below = 1;
   return 1;
}

static void step_json(psp_prog cb, void *ctx, sb *steps, const char *src, int ok, int pending,
                      const char *msg)
{
   sb b;
   sb_init(&b);
   sb_add(&b, "{");
   sb_kv_str(&b, "src", src);
   sb_kv_str(&b, "msg", msg);
   if (pending)
      sb_kv_bool(&b, "pending", 1);
   else
      sb_kv_bool(&b, "ok", ok);
   sb_add(&b, "}");
   if (cb)
      cb(ctx, b.s);
   if (!pending && steps)
   {
      if (steps->n > 1)
         sb_add(steps, ",");
      sb_add(steps, b.s);
   }
   sb_free(&b);
}

static void url_of(const char *path, char *out, size_t cap)
{
   char enc[2400];
   u_urlenc(path, enc, sizeof(enc), 0);
   snprintf(out, cap, "https://xmb.gbadhoc/fs?p=%s", enc);
}

static void reset_clock(long long t, char *out, size_t cap)
{
   time_t tt = (time_t)t;
   struct tm *lt = localtime(&tt);
   if (lt)
      strftime(out, cap, "%H:%M", lt);
   else
      snprintf(out, cap, "later");
}

/* Download one image to the cache (raw GitHub, else the mirror). */
static int get_box(int si, const char *name, char *path, size_t cap, long long *rate_until)
{
   char rel[700], url[2400], enc[2000];
   net_resp r;
   int rc;
   snprintf(rel, sizeof(rel), "box/%s/%s.png", SYS[si].sys, name);
   cache_path(rel, path, cap);
   if (u_is_file(path))
      return NET_OK;
   u_urlenc(name, enc, sizeof(enc), 0);
   snprintf(url, sizeof(url), "https://raw.githubusercontent.com/libretro-thumbnails/%s/master/Named_Boxarts/%s.png", SYS[si].repo, enc);
   rc = net_get(url, NULL, &r);
   if (rc != NET_OK || r.status != 200 || r.body.len < 8)
   {
      if (rc == NET_RATE && rate_until)
         *rate_until = r.rl_reset;
      net_resp_free(&r);
      snprintf(url, sizeof(url), "https://thumbnails.libretro.com/%s/Named_Boxarts/%s.png", SYS[si].tn, enc);
      if (rc == NET_OFFLINE)
         return rc;
      rc = net_get(url, NULL, &r);
   }
   if (rc == NET_OK && r.status == 200 && r.body.len >= 8)
   {
      char dir[1100], *sl;
      snprintf(dir, sizeof(dir), "%s", path);
      sl = strrchr(dir, '/');
      if (sl)
      {
         *sl = '\0';
         u_mkdirs(dir);
      }
      xw_write_file(path, r.body.data, r.body.len);
      net_resp_free(&r);
      return NET_OK;
   }
   net_resp_free(&r);
   return rc == NET_OK ? NET_HTTP : rc;
}

static int get_hero(const char *name, char *path, size_t cap, long long *rate_until)
{
   char rel[700], url[2400], enc[2000], tmp[1200];
   net_resp r;
   int rc;
   snprintf(rel, sizeof(rel), "hero/%s.png", name);
   cache_path(rel, path, cap);
   if (u_is_file(path))
      return NET_OK;
   u_urlenc(name, enc, sizeof(enc), 0);
   snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/HEAD/masters/%s.png", HERO_REPO, enc);
   rc = net_get(url, NULL, &r);
   if (rc == NET_OK && r.status == 200 && r.body.len > 8)
   {
      xw_buf png;
      char dir[1100], *sl;
      snprintf(tmp, sizeof(tmp), "%s.master", path);
      snprintf(dir, sizeof(dir), "%s", path);
      sl = strrchr(dir, '/');
      if (sl)
      {
         *sl = '\0';
         u_mkdirs(dir);
      }
      xw_write_file(tmp, r.body.data, r.body.len);
      net_resp_free(&r);
      /* the masters are 1376x768: PIC1 is 480x272, filled */
      if (xw_image_png(tmp, 480, 272, 1, &png) == 0)
      {
         xw_write_file(path, png.data, png.len);
         xw_buf_free(&png);
      }
      {
         wchar_t *w = u_wide(tmp);
         DeleteFileW(w);
         free(w);
      }
      return u_is_file(path) ? NET_OK : NET_HTTP;
   }
   if (rc == NET_RATE && rate_until)
      *rate_until = r.rl_reset;
   net_resp_free(&r);
   return rc == NET_OK ? NET_HTTP : rc;
}

static int find_hero(const char *stem, const char *nointro, char *out, size_t cap)
{
   char ks[300], kn[300], kh[300];
   int i, best = -1, bs = 0;
   xw_norm_key(stem, ks, sizeof(ks));
   xw_norm_key(nointro, kn, sizeof(kn));
   for (i = 0; i < g_heroes.n; i++)
   {
      xw_norm_key(g_heroes.s[i], kh, sizeof(kh));
      if (!strcmp(kh, ks) || (kn[0] && !strcmp(kh, kn)))
      {
         snprintf(out, cap, "%s", g_heroes.s[i]);
         return 100;
      }
   }
   for (i = 0; i < g_heroes.n; i++)
   {
      int s = xw_name_score(stem, g_heroes.s[i]);
      if (nointro[0])
      {
         int s2 = xw_name_score(nointro, g_heroes.s[i]);
         if (s2 > s)
            s = s2;
      }
      if (s > bs)
      {
         bs = s;
         best = i;
      }
   }
   if (best >= 0 && bs >= 92)
   {
      snprintf(out, cap, "%s", g_heroes.s[best]);
      return bs;
   }
   return 0;
}

int art_fetch(const jv *args, psp_prog cb, void *ctx, sb *out)
{
   const char *sys = jv_str(args, "system", "GBA"), *nointro = jv_str(args, "nointro", "");
   const char *base = jv_str(args, "base", ""), *stem = jv_str(args, "stem", "");
   const char *pref = jv_str(args, "region", "USA, Europe first");
   int hack = jv_bool(args, "hack", 0), recognised = jv_bool(args, "recognised", 0);
   int want_hero = jv_bool(args, "heroes", 1);
   int si = sys_index(sys), mirror = 0, rc, i, n = 0, good = 0;
   const char *status = "ok";
   long long rate_until = 0;
   cand c[32];
   sb steps, box;
   char msg[600], hero_name[400] = "", hero_path[1200] = "";

   sb_init(&steps);
   sb_init(&box);
   sb_add(&steps, "[");
   sb_add(&box, "[");

   if (!recognised)
   {
      step_json(cb, ctx, &steps, "libretro", 0, 0, "Nothing to look up: this ROM was not matched to a title.");
      status = "none";
      goto done;
   }
   step_json(cb, ctx, &steps, "libretro", 0, 1, "Asking libretro-thumbnails…");
   rc = load_thumbs(si, &mirror);
   if (rc == NET_OFFLINE || rc == NET_TIMEOUT)
   {
      step_json(cb, ctx, &steps, "network", 0, 0, "No connection. Check this PC is online and try again.");
      status = "offline";
      goto done;
   }
   if (rc == NET_RATE)
   {
      char at[16];
      rate_until = net_rate_reset();
      reset_clock(rate_until, at, sizeof(at));
      snprintf(msg, sizeof(msg), "GitHub is rate-limiting us (HTTP 403). It answers again at %s; finish the rest then.", at);
      step_json(cb, ctx, &steps, "libretro", 0, 0, msg);
      status = "rate";
      goto done;
   }
   if (!g_thumbs[si].loaded)
   {
      step_json(cb, ctx, &steps, "libretro", 0, 0, "libretro-thumbnails did not answer. Try again in a minute.");
      status = "offline";
      goto done;
   }
   EnterCriticalSection(&g_cs);
   n = box_candidates(si, hack ? base : nointro, hack, pref, c, 32);
   if (n > 6)
      n = 6;
   if (!n && !hack)
      n = nearest_candidate(si, nointro[0] ? nointro : stem, c);
   if (!n && hack)
      n = nearest_candidate(si, stem, c);
   LeaveCriticalSection(&g_cs);
   for (i = 0; i < n; i++)
   {
      char path[1200], url[2600];
      int r = get_box(si, c[i].name, path, sizeof(path), &rate_until);
      if (r == NET_RATE)
      {
         status = "rate";
         break;
      }
      if (r != NET_OK)
         continue;
      if (box.n > 1)
         sb_add(&box, ",");
      url_of(path, url, sizeof(url));
      sb_add(&box, "{");
      sb_kv_str(&box, "name", c[i].name);
      if (c[i].below)
      {
         char d[200], lab[400];
         xw_nointro_display(c[i].name, d, sizeof(d));
         snprintf(lab, sizeof(lab), "nearest: %s (%s)", d, c[i].region);
         sb_kv_str(&box, "region", lab);
      }
      else
         sb_kv_str(&box, "region", c[i].region);
      sb_kv_num(&box, "conf", c[i].conf);
      sb_kv_bool(&box, "below", c[i].below || c[i].conf < 80);
      sb_kv_str(&box, "url", url);
      sb_kv_str(&box, "path", path);
      sb_add(&box, "}");
      if (!c[i].below && c[i].conf >= 80)
         good++;
   }
   if (!strcmp(status, "rate"))
   {
      step_json(cb, ctx, &steps, "libretro", 0, 0,
                "GitHub is rate-limiting us (HTTP 429). Try again later, or finish the rest later.");
      goto done;
   }
   if (good)
      snprintf(msg, sizeof(msg), "%d box art %s found%s", good, good == 1 ? "match" : "matches",
               mirror ? " (thumbnails.libretro.com)" : "");
   else
      snprintf(msg, sizeof(msg), "%s", box.n > 1 ? "No box art above the match line (nearest shown)" : "No box art found for this title");
   step_json(cb, ctx, &steps, "libretro", good > 0, 0, msg);

   if (want_hero)
   {
      step_json(cb, ctx, &steps, "heroart", 0, 1, "Asking GBAdhoc-heroart…");
      rc = load_heroes();
      if (rc == NET_RATE)
      {
         step_json(cb, ctx, &steps, "heroart", 0, 0,
                   "GitHub is rate-limiting us. Box art was kept; hero art will need another try later.");
         rate_until = net_rate_reset();
         status = "rate";
         goto done;
      }
      if (rc != NET_OK)
      {
         step_json(cb, ctx, &steps, "heroart", 0, 0, "GBAdhoc-heroart did not answer; box art was kept.");
         goto done;
      }
      EnterCriticalSection(&g_cs);
      find_hero(stem, nointro, hero_name, sizeof(hero_name));
      LeaveCriticalSection(&g_cs);
      if (hero_name[0] && get_hero(hero_name, hero_path, sizeof(hero_path), &rate_until) == NET_OK)
         step_json(cb, ctx, &steps, "heroart", 1, 0, "Hero art found (480×272)");
      else
      {
         hero_path[0] = '\0';
         step_json(cb, ctx, &steps, "heroart", 0, 0, "No hero art in the repository for this ROM");
      }
   }
   if (!good && !hero_path[0])
      status = "none";
done:
   sb_add(&steps, "]");
   sb_add(&box, "]");
   sb_add(out, "{");
   sb_kv_str(out, "status", status);
   sb_key(out, "steps");
   sb_add(out, steps.s);
   sb_key(out, "box");
   sb_add(out, box.s);
   if (hero_path[0])
   {
      char url[2600];
      url_of(hero_path, url, sizeof(url));
      sb_key(out, "hero");
      sb_add(out, "{");
      sb_kv_str(out, "name", hero_name);
      sb_kv_str(out, "url", url);
      sb_kv_str(out, "path", hero_path);
      sb_add(out, "}");
   }
   if (rate_until)
   {
      char at[16];
      reset_clock(rate_until, at, sizeof(at));
      sb_kv_str(out, "rateReset", at);
   }
   sb_add(out, "}");
   sb_free(&steps);
   sb_free(&box);
   return 0;
}

/* ---- cache and network status ------------------------------------------- */

static void count_tree(const char *dir, int *files, long long *bytes)
{
   char pat[1200];
   wchar_t *w;
   WIN32_FIND_DATAW fd;
   HANDLE h;
   u_join(pat, sizeof(pat), dir, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h == INVALID_HANDLE_VALUE)
      return;
   do
   {
      if (fd.cFileName[0] == '.')
         continue;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
         char *n = u_utf8(fd.cFileName), sub[1300];
         u_join(sub, sizeof(sub), dir, n ? n : "");
         free(n);
         count_tree(sub, files, bytes);
      }
      else
      {
         (*files)++;
         *bytes += (long long)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
      }
   } while (FindNextFileW(h, &fd));
   FindClose(h);
}

void art_cache_stats(sb *out)
{
   char d[1100];
   int files = 0;
   long long bytes = 0;
   cache_path("box", d, sizeof(d));
   count_tree(d, &files, &bytes);
   cache_path("hero", d, sizeof(d));
   count_tree(d, &files, &bytes);
   art_cache_dir(d, sizeof(d));
   sb_add(out, "{");
   sb_kv_str(out, "dir", d);
   sb_kv_num(out, "files", files);
   sb_kv_num(out, "mb", (double)(long long)(bytes / 104857.6) / 10);
   sb_add(out, "}");
}

void art_cache_clear(void)
{
   char d[1100];
   int i;
   cache_path("box", d, sizeof(d));
   u_rmtree(d);
   cache_path("hero", d, sizeof(d));
   u_rmtree(d);
   cache_path("index", d, sizeof(d));
   u_rmtree(d);
   EnterCriticalSection(&g_cs);
   for (i = 0; i < 3; i++)
      names_free(&g_thumbs[i]);
   names_free(&g_heroes);
   LeaveCriticalSection(&g_cs);
}

void art_net_status(sb *out)
{
   net_resp r;
   /* /rate_limit itself is free: it does not count against the limit */
   int rc = net_get("https://api.github.com/rate_limit", NULL, &r);
   sb_add(out, "{");
   sb_kv_bool(out, "online", rc == NET_OK || rc == NET_RATE || rc == NET_HTTP);
   if (rc == NET_OK && r.body.data)
   {
      jv *v = jv_parse((const char *)r.body.data);
      jv *core = jv_get(jv_get(v, "resources"), "core");
      if (core)
      {
         long long rem = (long long)jv_num(core, "remaining", -1);
         long long reset = (long long)jv_num(core, "reset", 0);
         char at[16];
         sb_kv_num(out, "remaining", (double)rem);
         sb_kv_num(out, "limit", jv_num(core, "limit", 60));
         reset_clock(reset, at, sizeof(at));
         sb_kv_str(out, "reset", at);
         sb_kv_bool(out, "limited", rem == 0);
      }
      jv_free(v);
   }
   else if (rc == NET_RATE)
   {
      char at[16];
      reset_clock(net_rate_reset() ? net_rate_reset() : r.rl_reset, at, sizeof(at));
      sb_kv_bool(out, "limited", 1);
      sb_kv_str(out, "reset", at);
   }
   sb_add(out, "}");
   net_resp_free(&r);
}
