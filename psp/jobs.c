/* jobs.c -- see jobs.h. */
#include "jobs.h"

#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "net.h"
#include "util.h"
#include "../shared/xw_core.h"
#include "gen/launcher_pbp.h"

task_t g_task;
static lock_t g_lock;            /* g_task text, the request queue, retired buffers */
static int g_sema;               /* wakes the worker */
static volatile int g_net_state; /* NET_OFF / CONNECTING / ONLINE */
static volatile int g_net_retry;
static char g_ssid[64];

void task_lock(void) { lock_take(&g_lock); }
void task_unlock(void) { lock_give(&g_lock); }

/* ---- retired buffers ---- */

static void *g_retired[256];
static int g_nretired;

static void retire(void *p)
{
   if (!p) return;
   lock_take(&g_lock);
   if (g_nretired < 256)
      g_retired[g_nretired++] = p;
   else
      free(p);                     /* never happens at 60 fps */
   lock_give(&g_lock);
}

void jobs_frame_end(void)
{
   void *batch[256];
   int n, i;
   lock_take(&g_lock);
   n = g_nretired;
   memcpy(batch, g_retired, sizeof(void *) * (size_t)n);
   g_nretired = 0;
   lock_give(&g_lock);
   for (i = 0; i < n; i++)
      free(batch[i]);
}

/* ---- steps ---- */

static void step(int st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void step(int st, const char *fmt, ...)
{
   va_list ap;
   char b[160];
   va_start(ap, fmt);
   vsnprintf(b, sizeof(b), fmt, ap);
   va_end(ap);
   lock_take(&g_lock);
   if (g_task.nsteps < MAX_STEPS)
   {
      snprintf(g_task.steps[g_task.nsteps].msg, sizeof(g_task.steps[0].msg), "%s", b);
      g_task.steps[g_task.nsteps].st = st;
      g_task.nsteps++;
   }
   lock_give(&g_lock);
   LOG("step %d %s", st, b);
}

/* replace the last step (a pending one) */
static void step_end(int st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void step_end(int st, const char *fmt, ...)
{
   va_list ap;
   char b[160];
   va_start(ap, fmt);
   vsnprintf(b, sizeof(b), fmt, ap);
   va_end(ap);
   lock_take(&g_lock);
   if (g_task.nsteps > 0)
   {
      snprintf(g_task.steps[g_task.nsteps - 1].msg, sizeof(g_task.steps[0].msg), "%s", b);
      g_task.steps[g_task.nsteps - 1].st = st;
   }
   lock_give(&g_lock);
   LOG("step* %d %s", st, b);
}

/* ---- requests (low priority) ---- */

enum { RQ_ICON = 1, RQ_PIC, RQ_THUMB };
typedef struct { int type, game; char path[PATHCAP]; int sect, w, h, cover; } req_t;
#define NREQ 32
static req_t g_req[NREQ];
static int g_nreq;

typedef struct { int game, ver; img_slot s; int used; } pic_slot;
#define NPIC 3
static pic_slot g_pics[NPIC];

typedef struct { char path[PATHCAP]; int sect, w, h, cover; img_slot s; int used; } thumb_slot;
#define NTHUMB 24
static thumb_slot g_thumbs[NTHUMB];
static int g_clock;

static void request(const req_t *r)
{
   int i;
   lock_take(&g_lock);
   for (i = 0; i < g_nreq; i++)
      if (g_req[i].type == r->type && g_req[i].game == r->game && strcmp(g_req[i].path, r->path) == 0 &&
          g_req[i].w == r->w && g_req[i].sect == r->sect)
      {
         lock_give(&g_lock);
         return;
      }
   if (g_nreq == NREQ)
   {
      memmove(g_req, g_req + 1, sizeof(req_t) * (NREQ - 1));   /* drop the oldest */
      g_nreq--;
   }
   g_req[g_nreq++] = *r;
   lock_give(&g_lock);
   sceKernelSignalSema(g_sema, 1);
}

const img_slot *jobs_icon(int gi)
{
   game *g = &g_games[gi];
   if (g->icon_img.state == IMG_READY && g->icon_img.ver == g->ver)
      return &g->icon_img;
   if (g->icon_img.state != IMG_LOADING || g->icon_img.ver != g->ver)
   {
      req_t r;
      memset(&r, 0, sizeof(r));
      r.type = RQ_ICON;
      r.game = gi;
      request(&r);
   }
   return g->icon_img.state == IMG_READY ? &g->icon_img : NULL;   /* the stale one meanwhile */
}

const img_slot *jobs_pic(int gi)
{
   game *g = &g_games[gi];
   int i;
   if (g->bg.kind == ART_NONE)
      return NULL;
   for (i = 0; i < NPIC; i++)
      if (g_pics[i].game == gi && g_pics[i].ver == g->ver && g_pics[i].s.state != IMG_NONE)
      {
         g_pics[i].used = ++g_clock;
         return g_pics[i].s.state == IMG_READY ? &g_pics[i].s : NULL;
      }
   {
      req_t r;
      memset(&r, 0, sizeof(r));
      r.type = RQ_PIC;
      r.game = gi;
      request(&r);
   }
   /* while it loads, the previous picture of the same game if there is one */
   for (i = 0; i < NPIC; i++)
      if (g_pics[i].game == gi && g_pics[i].s.state == IMG_READY)
         return &g_pics[i].s;
   return NULL;
}

const img_slot *jobs_thumb(const char *path, int sect, int w, int h, int cover)
{
   int i;
   for (i = 0; i < NTHUMB; i++)
      if (g_thumbs[i].s.state != IMG_NONE && g_thumbs[i].sect == sect && g_thumbs[i].w == w && g_thumbs[i].h == h &&
          g_thumbs[i].cover == cover && strcmp(g_thumbs[i].path, path) == 0)
      {
         g_thumbs[i].used = ++g_clock;
         return g_thumbs[i].s.state == IMG_READY ? &g_thumbs[i].s : NULL;
      }
   {
      req_t r;
      memset(&r, 0, sizeof(r));
      r.type = RQ_THUMB;
      snprintf(r.path, sizeof(r.path), "%s", path);
      r.sect = sect;
      r.w = w;
      r.h = h;
      r.cover = cover;
      request(&r);
   }
   return NULL;
}

static unsigned fnv(const char *s);

static void publish(img_slot *s, image *im, int ver)
{
   void *old = s->px;
   if (im && im->px)
   {
      sceKernelDcacheWritebackRange(im->px, (unsigned)(im->w * im->h * 4));
      s->w = im->w;
      s->h = im->h;
      s->px = im->px;
      s->ver = ver;
      s->state = IMG_READY;
   }
   else
   {
      s->px = NULL;
      s->ver = ver;
      s->state = IMG_FAIL;
   }
   retire(old);
}

static void run_request(const req_t *r)
{
   image im = { 0 };
   if (r->type == RQ_ICON)
   {
      game *g = &g_games[r->game];
      int ver = g->ver;
      unsigned t0 = now_ms();
      if (g->icon_img.state == IMG_READY && g->icon_img.ver == ver)
         return;
      g->icon_img.state = g->icon_img.px ? IMG_READY : IMG_LOADING;
      {
         /* composed icons are kept between launches: cache/i<key>.raw, the
          * key covering everything the icon is made from (sources and their
          * sizes, fit, title, system) */
         char key[3 * PATHCAP + 200], cp[PATHCAP];
         int hit = 0;
         snprintf(key, sizeof(key), "%d|%s|%ld|%d|%s|%ld|%d|%s|%d", g->icon.kind, g->icon.path, file_size(g->icon.path),
                  g->bg.kind, g->bg.path, file_size(g->bg.path), g->fit, g->title, g->sys);
         snprintf(cp, sizeof(cp), "%s/cache/i%08x%08x.raw", g_st.app_dir, fnv(key), fnv(key + 1));
         {
            unsigned char *d;
            size_t n;
            if (read_all(cp, &d, &n) == 0)
            {
               if (n == 144 * 80 * 4)
               {
                  im.px = (uint32_t *)d;
                  im.w = 144;
                  im.h = 80;
                  hit = 1;
               }
               else
                  free(d);
            }
         }
         if (!hit)
         {
            if (art_icon0(g, &im) != 0)
               im.px = NULL;
            if (im.px && (im.w != 144 || im.h != 80))
            {
               image f;
               if (img_fit(&im, 144, 80, 0, &f) == 0) { img_free(&im); im = f; }
            }
            if (im.px)
            {
               char cd[PATHCAP];
               snprintf(cd, sizeof(cd), "%s/cache", g_st.app_dir);
               mkdirs(cd);
               write_all(cp, im.px, 144 * 80 * 4);
            }
         }
         publish(&g->icon_img, &im, ver);
         LOG("icon %d %s %ums%s", r->game, g->stem, now_ms() - t0, hit ? " (cached)" : "");
      }
   }
   else if (r->type == RQ_PIC)
   {
      game *g = &g_games[r->game];
      int i, best = 0, ver = g->ver;
      unsigned t0 = now_ms();
      for (i = 0; i < NPIC; i++)
         if (g_pics[i].game == r->game && g_pics[i].ver == ver && g_pics[i].s.state == IMG_READY)
            return;
      for (i = 1; i < NPIC; i++)
         if (g_pics[i].used < g_pics[best].used)
            best = i;
      /* never evict the slot the screen is drawing this game from */
      g_pics[best].game = r->game;
      g_pics[best].ver = ver;
      g_pics[best].used = ++g_clock;
      g_pics[best].s.state = IMG_LOADING;
      if (art_pic1(g, &im) != 0)
         im.px = NULL;
      publish(&g_pics[best].s, &im, ver);
      LOG("pic %d %ums", r->game, now_ms() - t0);
   }
   else
   {
      int i, best = 0;
      for (i = 0; i < NTHUMB; i++)
         if (g_thumbs[i].s.state == IMG_READY && strcmp(g_thumbs[i].path, r->path) == 0 && g_thumbs[i].w == r->w &&
             g_thumbs[i].sect == r->sect && g_thumbs[i].cover == r->cover)
            return;
      for (i = 1; i < NTHUMB; i++)
         if (g_thumbs[i].used < g_thumbs[best].used)
            best = i;
      {
         thumb_slot *t = &g_thumbs[best];
         snprintf(t->path, sizeof(t->path), "%s", r->path);
         t->sect = r->sect;
         t->w = r->w;
         t->h = r->h;
         t->cover = r->cover;
         t->used = ++g_clock;
         t->s.state = IMG_LOADING;
         if (art_thumb(r->path, r->sect, r->w, r->h, r->cover, &im) != 0)
            im.px = NULL;
         publish(&t->s, &im, 0);
      }
   }
}

/* ---- network ---- */

static int net_thread(SceSize a, void *b)
{
   (void)a; (void)b;
   for (;;)
   {
      if (g_net_state != NET_ONLINE && net_init() == NET_OK && net_wlan_switch())
      {
         char ip[32];
         g_net_state = NET_CONNECTING;
         if (net_connect(1, NULL) == NET_OK)
         {
            net_info(g_ssid, sizeof(g_ssid), ip, sizeof(ip));
            g_net_state = NET_ONLINE;
            LOG("net online %s %s", g_ssid, ip);
         }
         else
         {
            g_net_state = NET_OFF;
            LOG("net connect failed");
         }
      }
      /* until asked again; and keep the state honest (an AP that went away) */
      while (!g_net_retry)
      {
         sceKernelDelayThread(500 * 1000);
         if (g_net_state == NET_ONLINE && net_state() != NET_ONLINE)
            g_net_state = NET_OFF;
      }
      g_net_retry = 0;
   }
   return 0;
}

int jobs_online(void)
{
   return g_net_state == NET_ONLINE ? 1 : g_net_state == NET_CONNECTING ? -1 : 0;
}

void jobs_net_retry(void)
{
   if (net_state() == NET_ONLINE)
   {
      char ip[32];
      net_info(g_ssid, sizeof(g_ssid), ip, sizeof(ip));
      g_net_state = NET_ONLINE;
      return;
   }
   g_net_retry = 1;
}

void jobs_net_info(char *ssid, int cap)
{
   snprintf(ssid, (size_t)cap, "%s", g_ssid);
}

/* ---- art lookup ---- */

static const char *sys_dir(char s)
{
   return s == 'B' ? "Nintendo - Game Boy" : s == 'C' ? "Nintendo - Game Boy Color" : "Nintendo - Game Boy Advance";
}

static unsigned fnv(const char *s)
{
   unsigned h = 2166136261u;
   while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
   return h;
}

static int http_to_file(const char *url, const char *file, int *status, char *err, size_t errcap)
{
   net_resp r;
   int rc = net_get(url, &r, 6 * 1024 * 1024, &g_task.cancel, NULL, NULL);
   *status = r.status;
   if (err) snprintf(err, errcap, "%s", r.err);
   LOG("GET %s -> rc %d status %d %u B %u ms (tls %u)", url, rc, r.status, (unsigned)r.len, r.ms_total, r.ms_tls);
   if (rc == NET_OK && r.status == 200 && r.len)
   {
      if (write_all(file, r.data, r.len) != 0)
         rc = -100;
   }
   else if (rc == NET_OK)
      rc = -r.status;
   net_resp_free(&r);
   return rc;
}

/* get one box-art candidate into the cache; 1 = have it */
static int fetch_box(box_cand *c, int *rate, int *offline)
{
   char url[700];
   int status = 0, rc;
   if (c->cache[0] && file_exists(c->cache))
      return 1;
   snprintf(c->cache, sizeof(c->cache), "%s/cache/b%08x.png", g_st.app_dir, fnv(c->name));
   if (file_exists(c->cache))
      return 1;
   {
      char e1[160], e2[400];
      char sysc = c->sys ? c->sys : 'A';
      net_urlencode_path(sys_dir(sysc), e1, sizeof(e1));
      net_urlencode_path(c->name, e2, sizeof(e2));
      snprintf(url, sizeof(url), "http://thumbnails.libretro.com/%s/Named_Boxarts/%s.png", e1, e2);
      rc = http_to_file(url, c->cache, &status, NULL, 0);
      if (rc == NET_E_DNS || rc == NET_E_CONNECT || rc == NET_E_TIMEOUT || (rc < 0 && rc != -404 && status >= 500))
      {
         /* the HTTPS mirror */
         char sd[96], *p;
         snprintf(sd, sizeof(sd), "%s", sys_dir(sysc));
         for (p = sd; *p; p++)
            if (*p == ' ') *p = '_';
         snprintf(url, sizeof(url), "https://raw.githubusercontent.com/libretro-thumbnails/%s/master/Named_Boxarts/%s.png", sd, e2);
         rc = http_to_file(url, c->cache, &status, NULL, 0);
      }
   }
   if (rc == NET_OK)
      return 1;
   c->cache[0] = 0;
   if (status == 429 || status == 403) *rate = 1;
   if (rc == NET_E_OFFLINE) *offline = 1;
   return 0;
}

static void fill_cands(game *g)
{
   int i;
   if (g->ncand)
      return;
   for (i = 0; i < g->m.nbox && i < NAMES_MAXCAND; i++)
   {
      box_cand *c = &g->cand[i];
      memset(c, 0, sizeof(*c));
      snprintf(c->name, sizeof(c->name), "%s", g->m.box[i].name);
      names_region(c->name, c->region, 60);
      c->sys = g->m.box[i].sys;
      c->conf = g->m.box[i].conf;
      c->below = g->m.box[i].below;
   }
   g->ncand = i;
}

/* the hero master -> 480x272 PNG in the cache.  1 found, 0 none, -1 rate, -2 offline/failed */
static int fetch_hero(game *g)
{
   const char *name = g->m.hero ? g->m.hero : g->m.nointro;
   char enc[400], url[600], raw[PATHCAP];
   int status = 0, rc;
   image src = { 0 }, out = { 0 };
   if (!name)
      return 0;
   snprintf(g->hero_cache, sizeof(g->hero_cache), "%s/cache/h%08x.png", g_st.app_dir, fnv(name));
   if (file_exists(g->hero_cache))
      return 1;
   /* names in the repo use '&' where Named_Boxarts has '_' */
   {
      char nm[200], *p;
      snprintf(nm, sizeof(nm), "%s", name);
      if (!g->m.hero)
         for (p = nm; *p; p++)
            if (*p == '_') *p = '&';
      net_urlencode_path(nm, enc, sizeof(enc));
   }
   snprintf(url, sizeof(url), "https://raw.githubusercontent.com/ShoshinFauteux/GBAdhoc-heroart/main/masters/%s.png", enc);
   snprintf(raw, sizeof(raw), "%s/cache/hero.tmp", g_st.app_dir);
   rc = http_to_file(url, raw, &status, NULL, 0);
   if (rc != NET_OK)
   {
      g->hero_cache[0] = 0;
      if (status == 404) return 0;
      if (status == 429 || status == 403) return -1;
      return -2;
   }
   {
      unsigned t0 = now_ms();
      unsigned char *png;
      size_t len;
      int ok = 0;
      if (img_load(raw, &src, 0) == 0 && img_fit(&src, 480, 272, 1, &out) == 0 && img_png(&out, &png, &len) == 0)
      {
         ok = write_all(g->hero_cache, png, len) == 0;
         free(png);
      }
      LOG("hero resize+png %ux%u -> 480x272 in %ums", src.w, src.h, now_ms() - t0);
      img_free(&src);
      img_free(&out);
      sceIoRemove(raw);
      if (!ok)
      {
         g->hero_cache[0] = 0;
         return -2;
      }
   }
   return 1;
}

/* apply what was found where the game has nothing (data.js applyFetch) */
static void apply_fetch(game *g)
{
   int i;
   if (g->icon.kind == ART_NONE)
      for (i = 0; i < g->ncand; i++)
         if (!g->cand[i].below && g->cand[i].cache[0])
         {
            g->icon.kind = ART_NET;
            snprintf(g->icon.path, PATHCAP, "%s", g->cand[i].cache);
            snprintf(g->icon.label, sizeof(g->icon.label), "%s", g->cand[i].region);
            g->icon.conf = g->cand[i].conf;
            break;
         }
   if (g->bg.kind == ART_NONE && g->hero_found)
   {
      g->bg.kind = ART_NET;
      snprintf(g->bg.path, PATHCAP, "%s", g->hero_cache);
      g->bg.label[0] = 0;
   }
   game_touch(g);
}

/* one game's lookup.  quiet = a batch (best candidate only, no steps) */
static int do_fetch(int gi, int quiet, int *nbox, int *hero)
{
   game *g = &g_games[gi];
   int i, good = 0, rate = 0, offline = 0, want, hr = 0;
   char cachedir[PATHCAP];
   *nbox = 0;
   *hero = 0;
   g->fetching = 1;
   snprintf(cachedir, sizeof(cachedir), "%s/cache", g_st.app_dir);
   mkdirs(cachedir);
   if (g_net_state != NET_ONLINE)
   {
      if (!quiet)
      {
         if (!net_wlan_switch())
            step(STEP_BAD, "The Wi-Fi switch is off. Turn it on, then choose Network settings.");
         else
            step(STEP_BAD, "No connection. Connect to a wireless network and try again.");
      }
      g->fetch = FETCH_OFFLINE;
      g->fetching = 0;
      return RES_OFFLINE;
   }
   if (!g->m.recognised)
   {
      if (!quiet) step(STEP_BAD, "Nothing to look up: this ROM was not matched to a title.");
      g->fetch = FETCH_NOTFOUND;
      g->fetching = 0;
      return RES_NOTFOUND;
   }
   fill_cands(g);
   if (!quiet) step(STEP_PEND, "Asking libretro-thumbnails\xE2\x80\xA6");
   want = quiet ? 1 : 3;
   for (i = 0; i < g->ncand && !g_task.cancel; i++)
   {
      box_cand *c = &g->cand[i];
      if (c->below && good)
         continue;
      if (good >= want && !c->below)
         continue;
      if (fetch_box(c, &rate, &offline))
      {
         if (!c->below) good++;
      }
      if (rate || offline)
         break;
   }
   if (rate)
   {
      if (!quiet) step_end(STEP_BAD, "libretro-thumbnails is rate-limiting us (HTTP 429). Try again later.");
      g->fetch = FETCH_RATE;
      g->fetching = 0;
      return RES_RATE;
   }
   if (offline)
   {
      if (!quiet) step_end(STEP_BAD, "The connection dropped. Connect again and retry.");
      g->fetch = FETCH_OFFLINE;
      g->fetching = 0;
      return RES_OFFLINE;
   }
   if (!quiet)
   {
      int nearest = 0;
      for (i = 0; i < g->ncand; i++)
         nearest |= g->cand[i].below && g->cand[i].cache[0];
      if (good)
         step_end(STEP_OK, "%d box art %s found", good, good == 1 ? "match" : "matches");
      else
         step_end(STEP_BAD, nearest ? "No box art above the match line (nearest shown)" : "No box art found for this title");
   }
   *nbox = good;
   if (g_prefs.heroes && !g_task.cancel)
   {
      if (!quiet) step(STEP_PEND, "Asking GBAdhoc-heroart\xE2\x80\xA6");
      hr = fetch_hero(g);
      g->hero_found = hr == 1;
      if (!quiet)
      {
         if (hr == 1) step_end(STEP_OK, "Hero art found (480\xC3\x97" "272)");
         else if (hr == 0) step_end(STEP_BAD, "No hero art in the repository for this ROM");
         else if (hr == -1) step_end(STEP_BAD, "GitHub is rate-limiting us (HTTP 429). Box art was kept; hero art will need another try later.");
         else step_end(STEP_BAD, "Could not reach GitHub for hero art. Box art was kept.");
      }
      *hero = hr == 1;
   }
   apply_fetch(g);
   g->fetching = 0;
   if (hr == -1)
   {
      g->fetch = FETCH_RATE;
      return RES_RATE;
   }
   g->fetch = (good || *hero) ? FETCH_OK : FETCH_NOTFOUND;
   return g->fetch == FETCH_OK ? RES_OK : RES_NOTFOUND;
}

/* ---- entries ---- */

#define ENTRY_NEED (420 * 1024)    /* launcher + art, with room to spare */

static int copy_file(const char *from, const char *to)
{
   unsigned char *d;
   size_t n;
   int rc;
   if (read_all(from, &d, &n) != 0)
      return -1;
   rc = write_all(to, d, n);
   free(d);
   return rc;
}

static int do_make(int gi, int quiet, char *why, size_t whycap)
{
   game *g = &g_games[gi];
   char folder[64], root[16], made[PATHCAP], err[256], icon_tmp[PATHCAP], pic_tmp[PATHCAP];
   image ic = { 0 }, pic = { 0 };
   unsigned char *icon_png = NULL, *pic_png = NULL;
   size_t icon_len = 0, pic_len = 0;
   int rc, title_card;
   xw_entry e;
   unsigned t0;
   why[0] = 0;
   model_refresh_free();
   if (g_st.free_bytes >= 0 && g_st.free_bytes < ENTRY_NEED)
   {
      char fb[32];
      fmt_mb(g_st.free_bytes, fb, sizeof(fb));
      if (!quiet) step(STEP_BAD, "Not enough space on the %s: an entry needs about 400 KB and %s is free.", g_st.label, fb);
      snprintf(why, whycap, "No space");
      return RES_NOSPACE;
   }
   if (g->entry >= 0)
      snprintf(folder, sizeof(folder), "%s", g_entries[g->entry].folder);
   else
   {
      char base[48], p[PATHCAP];
      int k;
      xw_folder_from_title(g->title, base, sizeof(base));
      snprintf(folder, sizeof(folder), "%s", base);
      for (k = 2; k < 20; k++)
      {
         snprintf(p, sizeof(p), "%s/PSP/GAME/%s", g_st.dev, folder);
         if (!dir_exists(p))
            break;
         snprintf(folder, sizeof(folder), "%.40s_%d", base, k);
      }
   }
   if (!quiet) step(STEP_OK, "Making PSP/GAME/%s/", folder);
   /* ICON0 */
   t0 = now_ms();
   title_card = !game_has_icon(g);
   if (art_icon0(g, &ic) != 0 || img_png(&ic, &icon_png, &icon_len) != 0)
   {
      if (!quiet) step(STEP_BAD, "Could not make the icon (out of memory?)");
      img_free(&ic);
      snprintf(why, whycap, "Icon failed");
      return RES_FAIL;
   }
   LOG("ICON0 compose+png %ums, %u B", now_ms() - t0, (unsigned)icon_len);
   img_free(&ic);
   if (!quiet)
   {
      if (title_card)
         step(STEP_WARN, "ICON0.PNG (144\xC3\x97" "80 title card: no art for this game yet)");
      else
         step(STEP_OK, "ICON0.PNG (144\xC3\x97" "80, %s)", g->fit == FIT_TILE ? "tile" : g->fit == FIT_FIT ? "fitted" : "filled");
   }
   /* PIC1 */
   if (g->bg.kind != ART_NONE)
   {
      t0 = now_ms();
      /* a 480x272 PNG already (GBAdhoc's hero folder, the hero cache) goes
       * in as it is: no decode, no re-encode */
      if (g->bg.kind == ART_OWN || g->bg.kind == ART_NET || g->bg.kind == ART_PIC)
      {
         unsigned char *d;
         size_t n;
         if (read_all(g->bg.path, &d, &n) == 0)
         {
            if (n > 24 && memcmp(d, "\x89PNG", 4) == 0 && d[16] == 0 && d[17] == 0 && d[18] == 0x01 && d[19] == 0xE0 &&
                d[20] == 0 && d[21] == 0 && d[22] == 0x01 && d[23] == 0x10)
            {
               pic_png = d;
               pic_len = n;
               LOG("PIC1 as is, %u B", (unsigned)n);
            }
            else
               free(d);
         }
      }
      if (!pic_png && art_pic1(g, &pic) == 0 && img_png(&pic, &pic_png, &pic_len) == 0)
         LOG("PIC1 compose+png %ums, %u B", now_ms() - t0, (unsigned)pic_len);
      img_free(&pic);
      if (!quiet)
      {
         if (pic_png) step(STEP_OK, "PIC1.PNG (480\xC3\x97" "272)");
         else step(STEP_WARN, "The background could not be read: the XMB wave shows through");
      }
   }
   else if (!quiet)
      step(STEP_WARN, "No background chosen: the XMB wave shows through");
   /* The entry itself: xw_core's writer, the Windows maker's, as is.  The
    * two PNGs above are already 144x80 and 480x272, which xw_image_png
    * keeps byte for byte (no decode, no double-precision resample). */
   snprintf(icon_tmp, sizeof(icon_tmp), "%s/cache/icon0.tmp.png", g_st.app_dir);
   snprintf(pic_tmp, sizeof(pic_tmp), "%s/cache/pic1.tmp.png", g_st.app_dir);
   {
      char cd[PATHCAP];
      snprintf(cd, sizeof(cd), "%s/cache", g_st.app_dir);
      mkdirs(cd);
   }
   memset(&e, 0, sizeof(e));
   snprintf(root, sizeof(root), "%s/", g_st.dev);
   e.template_pbp = xw_launcher_pbp;
   e.template_len = xw_launcher_pbp_len;
   e.title = g->title;
   e.rom = g->rel;
   e.gbadhoc = g_st.install_rel;
   e.root = root;
   e.folder = folder;
   t0 = now_ms();
   rc = write_all(icon_tmp, icon_png, icon_len);
   if (rc == 0 && pic_png)
      rc = write_all(pic_tmp, pic_png, pic_len);
   if (rc == 0)
   {
      e.icon_image = icon_tmp;
      e.bg_image = pic_png ? pic_tmp : NULL;
      rc = xw_make_entry(&e, made, sizeof(made), err, sizeof(err));
   }
   else
      snprintf(err, sizeof(err), "Could not write to the %s.", g_st.label);
   sceIoRemove(icon_tmp);
   sceIoRemove(pic_tmp);
   free(icon_png);
   free(pic_png);
   LOG("entry written rc %d %ums %s", rc, now_ms() - t0, rc ? err : made);
   if (rc != 0)
   {
      model_refresh_free();
      if (!quiet) step(STEP_BAD, "%s", err);
      snprintf(why, whycap, g_st.free_bytes >= 0 && g_st.free_bytes < ENTRY_NEED ? "No space" : "Write failed");
      return g_st.free_bytes >= 0 && g_st.free_bytes < ENTRY_NEED ? RES_NOSPACE : RES_FAIL;
   }
   {
      char ep[PATHCAP];
      long sz;
      snprintf(ep, sizeof(ep), "%s/EBOOT.PBP", made);
      sz = file_size(ep);
      if (!quiet) step(STEP_OK, "Writing EBOOT.PBP (%ld KB: launcher, PARAM.SFO and art)", sz > 0 ? (sz + 512) / 1024 : 0);
   }
   if (!quiet) step(STEP_OK, "Writing launch.ini \xE2\x86\x92 rom = %s", g->rel);
   /* art from the internet is kept in GBAdhoc's own folders, once */
   if (g_st.found)
   {
      char p[PATHCAP], d[PATHCAP];
      int kept = 0;
      if (g->icon.kind == ART_NET && !g->own_box[0])
      {
         snprintf(d, sizeof(d), "%s/boxart", g_st.install);
         mkdirs(d);
         snprintf(p, sizeof(p), "%s/%s.png", d, g->stem);
         if (copy_file(g->icon.path, p) == 0) { snprintf(g->own_box, PATHCAP, "%s", p); kept++; }
      }
      if (g->bg.kind == ART_NET && !g->own_hero[0])
      {
         snprintf(d, sizeof(d), "%s/hero", g_st.install);
         mkdirs(d);
         snprintf(p, sizeof(p), "%s/%s.png", d, g->stem);
         if (copy_file(g->bg.path, p) == 0) { snprintf(g->own_hero, PATHCAP, "%s", p); kept++; }
      }
      if (kept && !quiet)
         step(STEP_OK, "Kept the art in GBAdhoc's %s", kept == 2 ? "boxart and hero folders" : g->icon.kind == ART_NET ? "boxart folder" : "hero folder");
   }
   model_scan_entries();
   model_refresh_free();
   if (!quiet) step(STEP_OK, "Done: \"%s\" is in the XMB's Game column.", g->title);
   return RES_OK;
}

static int do_remove(int gi)
{
   game *g = &g_games[gi];
   entry_t *en;
   char ini[PATHCAP];
   if (g->entry < 0)
      return RES_FAIL;
   en = &g_entries[g->entry];
   snprintf(ini, sizeof(ini), "%s/launch.ini", en->dir);
   if (!file_exists(ini))
   {
      step(STEP_BAD, "PSP/GAME/%s is not an entry (no launch.ini); left alone.", en->folder);
      return RES_FAIL;
   }
   if (remove_tree(en->dir) != 0)
   {
      step(STEP_BAD, "Could not remove PSP/GAME/%s", en->folder);
      return RES_FAIL;
   }
   model_scan_entries();
   model_refresh_free();
   /* its art was the entry's own: back to what the card has */
   if (g->icon.kind == ART_ENTRY || g->bg.kind == ART_ENTRY)
      model_default_art(g);
   game_touch(g);
   return RES_OK;
}

/* ---- the worker ---- */

static void scan_prog(const char *msg, const char *sub, float frac)
{
   lock_take(&g_lock);
   snprintf(g_task.msg, sizeof(g_task.msg), "%s", msg);
   snprintf(g_task.sub, sizeof(g_task.sub), "%s", sub ? sub : "");
   g_task.frac = frac;
   lock_give(&g_lock);
}

static void run_task(void)
{
   int kind = g_task.kind, i;
   unsigned t0 = now_ms();
   switch (kind)
   {
   case TASK_SCAN:
   {
      scan_prog("Looking for GBAdhoc\xE2\x80\xA6", "", 0.02f);
      names_init();
      model_find_install();
      if (g_st.found)
         scan_prog("GBAdhoc found", g_st.install, 0.15f);
      sceKernelDelayThread(300 * 1000);
      for (i = 0; i < g_ngames; i++)
         retire(g_games[i].icon_img.px);
      model_scan(scan_prog);
      g_task.result = RES_OK;
      LOG("scan: %d ROMs, %d entries, %ums", g_ngames, g_nentries, now_ms() - t0);
      break;
   }
   case TASK_FETCH:
   {
      int nb, he;
      g_games[g_task.game].ncand = 0;
      g_task.result = do_fetch(g_task.game, 0, &nb, &he);
      g_task.nbox_found = nb;
      g_task.hero_found = he;
      break;
   }
   case TASK_MAKE:
   {
      char why[64];
      game *g = &g_games[g_task.game];
      /* look up art first for a game with none (data.js makeFlow) */
      if ((!game_has_icon(g) || g->bg.kind == ART_NONE) && g->fetch == FETCH_NONE && g->m.recognised &&
          g_net_state == NET_ONLINE)
      {
         int nb, he, r;
         step(STEP_PEND, "Looking up art first\xE2\x80\xA6");
         r = do_fetch(g_task.game, 1, &nb, &he);
         if (r == RES_OK)
            step_end(STEP_OK, "Art found: %s, %s", nb ? "box art" : "no box art", he ? "hero art" : "no hero art");
         else if (r == RES_NOTFOUND)
            step_end(STEP_WARN, "No art found: a title card is used");
         else
            step_end(STEP_WARN, "Art lookup failed (%s): a title card is used", r == RES_RATE ? "rate-limited" : "offline");
      }
      g_task.result = do_make(g_task.game, 0, why, sizeof(why));
      break;
   }
   case TASK_REMOVE:
      g_task.result = do_remove(g_task.game);
      break;
   case TASK_BATCH_MAKE:
   case TASK_BATCH_ART:
   {
      for (i = 0; i < g_task.nitems; i++)
      {
         batch_item *it = &g_task.items[i];
         game *g = &g_games[it->game];
         int nb = 0, he = 0, r;
         const char *note = "";
         g_task.cur = i;
         if (g_task.cancel)
         {
            it->st = BI_SKIP;
            snprintf(it->msg, sizeof(it->msg), "Skipped");
            continue;
         }
         if (kind == TASK_BATCH_ART ||
             ((!game_has_icon(g) || g->bg.kind == ART_NONE) && g->fetch == FETCH_NONE && g->m.recognised && g_net_state == NET_ONLINE))
         {
            it->st = BI_PEND;
            snprintf(it->msg, sizeof(it->msg), "Getting art\xE2\x80\xA6");
            r = do_fetch(it->game, 1, &nb, &he);
            if (kind == TASK_BATCH_ART)
            {
               it->st = r == RES_OK ? BI_OK : r == RES_NOTFOUND ? BI_WARN : BI_BAD;
               if (r == RES_OK)
                  snprintf(it->msg, sizeof(it->msg), "%d box art \xC2\xB7 %s", nb, he ? "hero" : "no hero");
               else
                  snprintf(it->msg, sizeof(it->msg), "%s", r == RES_OFFLINE ? "Offline" : r == RES_RATE ? "Rate-limited" :
                           g->m.recognised ? "Nothing found" : "Not recognised");
               if (r == RES_RATE || r == RES_OFFLINE)
                  g_task.cancel = 1;
               continue;
            }
            note = r == RES_OFFLINE ? "offline" : r == RES_RATE ? "rate-limited" : r == RES_NOTFOUND ? "no art found" : "";
         }
         it->st = BI_PEND;
         snprintf(it->msg, sizeof(it->msg), "Writing\xE2\x80\xA6");
         {
            char why[64];
            int had_icon = game_has_icon(g);
            r = do_make(it->game, 1, why, sizeof(why));
            if (r == RES_OK)
            {
               it->st = had_icon ? BI_OK : BI_WARN;
               if (had_icon)
                  snprintf(it->msg, sizeof(it->msg), "Made");
               else
                  snprintf(it->msg, sizeof(it->msg), "Made with a title card (%s)", note[0] ? note : "no art");
               g->marked = 0;
            }
            else
            {
               it->st = BI_BAD;
               snprintf(it->msg, sizeof(it->msg), "%s", why);
               if (r == RES_NOSPACE)
               {
                  g_task.result = RES_NOSPACE;
                  g_task.cancel = 1;
               }
            }
         }
      }
      g_task.cur = g_task.nitems;
      if (g_task.result != RES_NOSPACE)
         g_task.result = RES_OK;
      break;
   }
   default:
      break;
   }
   LOG("task %d done result %d in %ums", kind, g_task.result, now_ms() - t0);
   g_task.done = 1;
}

static int worker(SceSize a, void *b)
{
   (void)a; (void)b;
   for (;;)
   {
      req_t r;
      int have = 0;
      if (g_task.kind != TASK_NONE && !g_task.done)
      {
         run_task();
         continue;
      }
      lock_take(&g_lock);
      if (g_nreq)
      {
         r = g_req[g_nreq - 1];       /* newest first: what the screen shows now */
         g_nreq--;
         have = 1;
      }
      lock_give(&g_lock);
      if (have)
      {
         run_request(&r);
         continue;
      }
      sceKernelWaitSema(g_sema, 1, NULL);
   }
   return 0;
}

void jobs_start(void)
{
   int t;
   lock_init(&g_lock);
   g_sema = sceKernelCreateSema("xmbwork", 0, 0, 1000, NULL);
   memset(g_pics, 0, sizeof(g_pics));
   {
      int i;
      for (i = 0; i < NPIC; i++)
         g_pics[i].game = -1;
   }
   t = sceKernelCreateThread("xmbworker", worker, 0x30, 256 * 1024, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
   sceKernelStartThread(t, 0, NULL);
   t = sceKernelCreateThread("xmbnet", net_thread, 0x34, 64 * 1024, PSP_THREAD_ATTR_USER, NULL);
   sceKernelStartThread(t, 0, NULL);
}

int jobs_busy(void)
{
   return g_task.kind != TASK_NONE && !g_task.done;
}

static void begin(int kind, int game)
{
   lock_take(&g_lock);
   memset(&g_task, 0, sizeof(g_task));
   g_task.game = game;
   g_task.kind = kind;
   lock_give(&g_lock);
   sceKernelSignalSema(g_sema, 1);
}

void jobs_scan(void) { begin(TASK_SCAN, -1); }
void jobs_fetch(int game) { begin(TASK_FETCH, game); }
void jobs_remove(int game) { begin(TASK_REMOVE, game); }

void jobs_make(int game, int replace)
{
   lock_take(&g_lock);
   memset(&g_task, 0, sizeof(g_task));
   g_task.game = game;
   g_task.replace = replace;
   g_task.kind = TASK_MAKE;
   lock_give(&g_lock);
   sceKernelSignalSema(g_sema, 1);
}

void jobs_batch(const int *games, int n, int make)
{
   int i;
   lock_take(&g_lock);
   memset(&g_task, 0, sizeof(g_task));
   for (i = 0; i < n && i < MAX_GAMES; i++)
   {
      g_task.items[i].game = games[i];
      g_task.items[i].st = BI_WAIT;
      snprintf(g_task.items[i].msg, sizeof(g_task.items[i].msg), "Waiting");
   }
   g_task.nitems = i;
   g_task.kind = make ? TASK_BATCH_MAKE : TASK_BATCH_ART;
   lock_give(&g_lock);
   sceKernelSignalSema(g_sema, 1);
}

void jobs_ack(void)
{
   if (!jobs_busy())
      g_task.kind = TASK_NONE;
}
