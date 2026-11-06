/* model.c -- see model.h. */
#include "model.h"
#include "theme.h"

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "art.h"
#include "../shared/text_fold.h"
#include "../shared/xw_core.h"

game g_games[MAX_GAMES];
int g_ngames;
entry_t g_entries[MAX_ENTRIES];
int g_nentries;
storage_t g_st;
prefs_t g_prefs = { 1, 0, 1, TH_GRAPHITE };
int g_scan_ver;

/* ---- status ---- */

int game_has_icon(const game *g)
{
   return g->icon.kind != ART_NONE || (g->fit != FIT_FIT && g->bg.kind != ART_NONE);
}

int game_status(const game *g)
{
   if (g->entry >= 0 && g_entries[g->entry].game < 0)
      return ST_MISSING;
   if (!g->m.recognised)
      return ST_UNKNOWN;
   if (g->entry >= 0 && g_entries[g->entry].rom_moved)
      return ST_MOVED;
   if (g->entry >= 0)
      return ST_INXMB;
   if (!game_has_icon(g))
      return ST_NOART;
   return ST_READY;
}

const char *status_label(int st)
{
   switch (st)
   {
   case ST_UNKNOWN: return "Not recognised";
   case ST_MOVED: return "ROM moved";
   case ST_INXMB: return "In XMB";
   case ST_NOART: return "No art yet";
   case ST_MISSING: return "ROM missing";
   default: return "Ready";
   }
}

uint32_t status_colour(int st)
{
   switch (st)
   {
   case ST_UNKNOWN: return g_th->warn;
   case ST_MOVED: case ST_MISSING: return g_th->bad;
   case ST_INXMB: return g_th->ok;
   case ST_NOART: return g_th->fg3;         /* muted */
   default: return g_th->info;
   }
}

const char *sys_name(int sys)
{
   return sys == SYS_GBA ? "Game Boy Advance" : sys == SYS_GBC ? "Game Boy Color" : "Game Boy";
}

void art_label(const art_choice *a, char *out, unsigned cap)
{
   switch (a->kind)
   {
   case ART_NONE: snprintf(out, cap, "None"); break;
   case ART_OWN: snprintf(out, cap, "GBAdhoc's own"); break;
   case ART_ENTRY: snprintf(out, cap, "The entry's own"); break;
   case ART_CUSTOM: snprintf(out, cap, "Your picture \xC2\xB7 %s", a->label); break;
   case ART_NET:
      if (a->label[0])
         snprintf(out, cap, "Internet \xC2\xB7 %s", a->label);
      else
         snprintf(out, cap, "Internet");
      break;
   default: snprintf(out, cap, "%s", a->label); break;
   }
}

void game_touch(game *g)
{
   g->ver++;
}

/* ---- prefs ---- */

void prefs_load(void)
{
   char p[PATHCAP], v[PATHCAP];
   unsigned char *buf;
   size_t n;
   path_join(p, sizeof(p), g_st.app_dir, "xmbapp.ini");
   g_st.nfolders = 0;
   if (read_all(p, &buf, &n) != 0)
      return;
   if (ini_get((char *)buf, "prefer_own", v, sizeof(v)) == 0) g_prefs.prefer_own = atoi(v);
   if (ini_get((char *)buf, "region", v, sizeof(v)) == 0) g_prefs.region = atoi(v) % 3;
   if (ini_get((char *)buf, "heroes", v, sizeof(v)) == 0) g_prefs.heroes = atoi(v);
   if (ini_get((char *)buf, "theme", v, sizeof(v)) == 0 && theme_find(v) >= 0) g_prefs.theme = theme_find(v);
   {
      char *line = (char *)buf;
      while (line && *line && g_st.nfolders < MAX_FOLDERS)
      {
         char *eol = strchr(line, '\n');
         if (eol) *eol = 0;
         if (strncmp(line, "folder", 6) == 0 && ini_get(line, "folder", v, sizeof(v)) == 0 && v[0])
            snprintf(g_st.folders[g_st.nfolders++], PATHCAP, "%s", v);
         line = eol ? eol + 1 : NULL;
      }
   }
   free(buf);
}

void prefs_save(void)
{
   char p[PATHCAP], b[4096];
   int n, i;
   path_join(p, sizeof(p), g_st.app_dir, "xmbapp.ini");
   n = snprintf(b, sizeof(b), "# GBAdhoc XMB settings\nprefer_own = %d\nregion = %d\nheroes = %d\ntheme = %s\n",
                g_prefs.prefer_own, g_prefs.region, g_prefs.heroes, g_themes[g_prefs.theme].id);
   for (i = 0; i < g_st.nfolders && n < (int)sizeof(b) - PATHCAP - 16; i++)
      n += snprintf(b + n, sizeof(b) - (size_t)n, "folder = %s\n", g_st.folders[i]);
   write_all(p, b, (size_t)n);
}

/* ---- the install ---- */

static int sfo_title(const char *eboot, char *title, size_t cap, int *has_icon, int *has_pic)
{
   SceUID fd = sceIoOpen(eboot, PSP_O_RDONLY, 0);
   unsigned char hdr[40], *sfo;
   unsigned off[9];
   int i, rc = -1;
   if (has_icon) *has_icon = 0;
   if (has_pic) *has_pic = 0;
   if (fd < 0)
      return -1;
   if (sceIoRead(fd, hdr, 40) == 40 && memcmp(hdr, "\0PBP", 4) == 0)
   {
      for (i = 0; i < 8; i++)
         off[i] = hdr[8 + 4 * i] | hdr[9 + 4 * i] << 8 | hdr[10 + 4 * i] << 16 | (unsigned)hdr[11 + 4 * i] << 24;
      if (has_icon) *has_icon = off[2] > off[1];
      if (has_pic) *has_pic = off[5] > off[4];
      if (off[1] > off[0] && off[1] - off[0] < 65536)
      {
         sfo = (unsigned char *)malloc(off[1] - off[0]);
         if (sfo)
         {
            sceIoLseek(fd, off[0], PSP_SEEK_SET);
            if (sceIoRead(fd, sfo, off[1] - off[0]) == (int)(off[1] - off[0]))
               rc = xw_sfo_get_string(sfo, off[1] - off[0], "TITLE", title, cap);
            free(sfo);
         }
      }
   }
   sceIoClose(fd);
   return rc;
}

static void dev_label(const char *dev)
{
   snprintf(g_st.dev, sizeof(g_st.dev), "%s", dev);
   snprintf(g_st.label, sizeof(g_st.label), "%s", strcmp(dev, "ef0:") == 0 ? "internal storage" : "Memory Stick");
}

int model_find_install(void)
{
   static const char *devs[] = { "ms0:", "ef0:" };
   int d, best_score = 0;
   g_st.found = 0;
   g_st.is_go = dir_exists("ef0:/");
   for (d = 0; d < 2; d++)
   {
      char gd[64];
      SceUID dh;
      SceIoDirent e;
      snprintf(gd, sizeof(gd), "%s/PSP/GAME", devs[d]);
      dh = sceIoDopen(gd);
      if (dh < 0)
         continue;
      memset(&e, 0, sizeof(e));
      while (sceIoDread(dh, &e) > 0)
      {
         char dir[PATHCAP], p[PATHCAP], title[128];
         int score = 0;
         if (e.d_name[0] == '.' || !FIO_S_ISDIR(e.d_stat.st_mode))
         {
            memset(&e, 0, sizeof(e));
            continue;
         }
         snprintf(dir, sizeof(dir), "%s/%s", gd, e.d_name);
         snprintf(p, sizeof(p), "%s/roms", dir);
         if (dir_exists(p))
         {
            score = 1;
            snprintf(p, sizeof(p), "%s/EBOOT.PBP", dir);
            title[0] = 0;
            if (file_exists(p) && sfo_title(p, title, sizeof(title), NULL, NULL) == 0 && strstr(title, "GBAdhoc"))
               score += 2;
            if (ieq_str(e.d_name, "GBAdhoc"))
               score += 4;
            if (score > best_score)
            {
               const char *v = title;
               best_score = score;
               dev_label(devs[d]);
               snprintf(g_st.install, sizeof(g_st.install), "%s", dir);
               snprintf(g_st.install_rel, sizeof(g_st.install_rel), "PSP/GAME/%s", e.d_name);
               /* the version: the first digit run in the title ("GBAdhoc 4.5.0") */
               while (*v && !(*v >= '0' && *v <= '9'))
                  v++;
               snprintf(g_st.version, sizeof(g_st.version), "%s", *v ? v : "");
               g_st.found = 1;
            }
         }
         memset(&e, 0, sizeof(e));
      }
      sceIoDclose(dh);
   }
   if (!g_st.found)
      dev_label(strncmp(g_st.app_dir, "ef0", 3) == 0 ? "ef0:" : "ms0:");
   model_refresh_free();
   return g_st.found ? 0 : -1;
}

void model_refresh_free(void)
{
   g_st.free_bytes = dev_free(g_st.dev);
}

/* ---- the scan ---- */

static union
{
   SceIoFatDirentPrivate fat;
   unsigned char raw[FAT_PRIV_BYTES + 3];
} s_fat;
static int s_fat_layout;

static int rom_sys(const char *name)
{
   if (ends_with_i(name, ".gba") || ends_with_i(name, ".agb")) return SYS_GBA;
   if (ends_with_i(name, ".gbc") || ends_with_i(name, ".cgb")) return SYS_GBC;
   if (ends_with_i(name, ".gb") || ends_with_i(name, ".sgb")) return SYS_GB;
   return -1;
}

static int pick_match(const char *n, void *ctx)
{
   (void)ctx;
   return rom_sys(n) >= 0;
}

static int pick_opens(const char *n, void *ctx)
{
   char p[PATHCAP];
   path_join(p, sizeof(p), (const char *)ctx, n);
   return file_exists(p);
}

static scan_progress s_prog;
static float s_frac0, s_frac1;

static void add_rom(const char *dir, const char *open_name, const char *show_name, long long size,
                    const char *folder_label, const char *rel_prefix)
{
   game *g;
   char stem[160];
   if (g_ngames >= MAX_GAMES)
      return;
   g = &g_games[g_ngames];
   memset(g, 0, sizeof(*g));
   path_join(g->path, sizeof(g->path), dir, open_name);
   if (rel_prefix)
   {
      if (rel_prefix[0])
         snprintf(g->rel, sizeof(g->rel), "%s/%s", rel_prefix, open_name);
      else
         snprintf(g->rel, sizeof(g->rel), "%s", open_name);
   }
   else
      snprintf(g->rel, sizeof(g->rel), "%s", g->path);
   snprintf(g->file, sizeof(g->file), "%s", show_name);
   snprintf(g->folder, sizeof(g->folder), "%s", folder_label);
   g->sys = rom_sys(show_name) >= 0 ? rom_sys(show_name) : rom_sys(open_name);
   g->size = size;
   g->entry = -1;
   stem_of(show_name, stem, sizeof(stem));
   snprintf(g->stem, sizeof(g->stem), "%s", stem);
   g_ngames++;
   if (s_prog)
   {
      char msg[96];
      snprintf(msg, sizeof(msg), "%d ROM%s found, matching titles\xE2\x80\xA6", g_ngames, g_ngames == 1 ? "" : "s");
      s_prog(msg, show_name, s_frac1);
   }
}

static void scan_dir(const char *dir, int depth, const char *folder_label, const char *rel_prefix)
{
   SceUID d = sceIoDopen(dir);
   SceIoDirent e;
   char subs[24][128];
   int nsub = 0, i;
   if (d < 0)
      return;
   memset(&e, 0, sizeof(e));
   memset(&s_fat, 0, sizeof(s_fat));
   s_fat.fat.size = sizeof(s_fat.fat);
   e.d_private = &s_fat.fat;
   while (sceIoDread(d, &e) > 0)
   {
      int is_dir = (e.d_stat.st_mode & 0x1000) != 0 || (e.d_stat.st_attr & 0x0010) != 0;
      if (e.d_name[0] != '.')
      {
         if (is_dir)
         {
            /* GBAdhoc 4.6's saves/ and states/ hold no ROMs */
            if (depth > 0 && nsub < 24 && !ieq_str(e.d_name, "saves") && !ieq_str(e.d_name, "states"))
            {
               /* the long name (d_name can come back as the 8.3 "HACKS") */
               static char dl[1024], ds[13];
               s_fat.raw[FAT_PRIV_BYTES - 1] = 0;
               fat_private_names(s_fat.raw, e.d_name, &s_fat_layout, dl, sizeof(dl), ds, sizeof(ds));
               snprintf(subs[nsub++], sizeof(subs[0]), "%s",
                        dl[0] && text_is_ascii(dl) && ieq_str(dl, e.d_name) ? dl : e.d_name);
            }
         }
         else
         {
            static char sl[1024], ss[13];
            const char *open_name, *show_name;
            s_fat.raw[FAT_PRIV_BYTES - 1] = 0;
            fat_private_names(s_fat.raw, e.d_name, &s_fat_layout, sl, sizeof(sl), ss, sizeof(ss));
            if (fat_rom_pick(e.d_name, sl, ss, pick_match, pick_opens, (void *)dir, &open_name, &show_name))
               add_rom(dir, open_name, show_name, (long long)e.d_stat.st_size, folder_label, rel_prefix);
         }
      }
      memset(&e, 0, sizeof(e));
      memset(&s_fat, 0, sizeof(s_fat));
      s_fat.fat.size = sizeof(s_fat.fat);
      e.d_private = &s_fat.fat;
   }
   sceIoDclose(d);
   for (i = 0; i < nsub; i++)
   {
      char sd[PATHCAP], fl[96], rp[PATHCAP];
      path_join(sd, sizeof(sd), dir, subs[i]);
      snprintf(fl, sizeof(fl), "%s/%s", folder_label, subs[i]);
      if (rel_prefix)
      {
         if (rel_prefix[0])
            snprintf(rp, sizeof(rp), "%s/%s", rel_prefix, subs[i]);
         else
            snprintf(rp, sizeof(rp), "%s", subs[i]);
      }
      if (s_prog)
         s_prog("Scanning for ROMs\xE2\x80\xA6", sd + 5, s_frac0);
      scan_dir(sd, depth - 1, fl, rel_prefix ? rp : NULL);
   }
}

/* ---- the owner's own pictures: <app dir>/custom ----
 * <ROM file name without extension>.png is that game's icon (box art, or a
 * finished 144x80 ICON0), <stem>.bg.png its background (any size; 480x272
 * goes in as it is).  .jpg/.jpeg/.bmp work too.  Names match without case. */

char g_custom[MAX_CUSTOM][128];
int g_ncustom;

static int is_image_name(const char *n)
{
   return ends_with_i(n, ".png") || ends_with_i(n, ".jpg") || ends_with_i(n, ".jpeg") || ends_with_i(n, ".bmp");
}

void model_scan_custom(void)
{
   char d[PATHCAP];
   SceUID dh;
   SceIoDirent e;
   g_ncustom = 0;
   snprintf(d, sizeof(d), "%s/custom", g_st.app_dir);
   dh = sceIoDopen(d);
   if (dh < 0)
   {
      /* make it, so the owner finds where to put pictures */
      mkdirs(d);
      return;
   }
   memset(&e, 0, sizeof(e));
   while (sceIoDread(dh, &e) > 0 && g_ncustom < MAX_CUSTOM)
   {
      if (!FIO_S_ISDIR(e.d_stat.st_mode) && e.d_name[0] != '.' && is_image_name(e.d_name) && strlen(e.d_name) < 128)
         snprintf(g_custom[g_ncustom++], 128, "%s", e.d_name);
      memset(&e, 0, sizeof(e));
   }
   sceIoDclose(dh);
}

int model_custom_for(const game *g, int bg, char *path, unsigned cap)
{
   int i;
   size_t sl = strlen(g->stem);
   for (i = 0; i < g_ncustom; i++)
   {
      const char *n = g_custom[i], *rest;
      if (strncasecmp(n, g->stem, sl) != 0)
         continue;
      rest = n + sl;
      if (bg ? strncasecmp(rest, ".bg.", 4) == 0 && strchr(rest + 4, '.') == NULL
             : rest[0] == '.' && strchr(rest + 1, '.') == NULL)
      {
         snprintf(path, cap, "%s/custom/%s", g_st.app_dir, n);
         return 0;
      }
   }
   return -1;
}

/* the custom picture for one side, checked; 1 = chosen */
static int use_custom(game *g, int bg)
{
   char p[PATHCAP], why[96];
   art_choice *a = bg ? &g->bg : &g->icon;
   if (model_custom_for(g, bg, p, sizeof(p)) != 0)
      return 0;
   if (art_check(p, why, sizeof(why)) != 0)
   {
      snprintf(g->custom_msg, sizeof(g->custom_msg), "custom/%s %s", base_name(p), why);
      LOG("custom: %s", g->custom_msg);
      return 0;
   }
   memset(a, 0, sizeof(*a));
   a->kind = ART_CUSTOM;
   snprintf(a->path, PATHCAP, "%s", p);
   snprintf(a->label, sizeof(a->label), "%s", base_name(p));
   return 1;
}

void model_default_art(game *g)
{
   int ci, cb;
   memset(&g->icon, 0, sizeof(g->icon));
   memset(&g->bg, 0, sizeof(g->bg));
   g->fit = FIT_SIDE;
   g->custom_msg[0] = 0;
   /* the owner's own pictures (custom folder) come before anything else */
   ci = use_custom(g, 0);
   cb = use_custom(g, 1);
   if (ci && cb)
      return;
   if (!ci && !cb && g->entry >= 0)
   {
      entry_t *e = &g_entries[g->entry];
      if (e->has_icon) { g->icon.kind = ART_ENTRY; snprintf(g->icon.path, PATHCAP, "%s/EBOOT.PBP", e->dir); }
      if (e->has_pic) { g->bg.kind = ART_ENTRY; snprintf(g->bg.path, PATHCAP, "%s/EBOOT.PBP", e->dir); }
      if (e->has_icon || e->has_pic)
         return;
   }
   if (!ci && g->own_box[0])
   {
      g->icon.kind = ART_OWN;
      snprintf(g->icon.path, PATHCAP, "%s", g->own_box);
   }
   if (!cb && g->own_hero[0])
   {
      g->bg.kind = ART_OWN;
      snprintf(g->bg.path, PATHCAP, "%s", g->own_hero);
   }
}

static void recognise(game *g)
{
   names_match(g->stem, g->sys, g_prefs.region, &g->m);
   if (g->m.nointro)
   {
      names_plain_title(g->m.nointro, g->nointro_title, sizeof(g->nointro_title));
      names_short_title(g->m.nointro, g->short_title, sizeof(g->short_title));
   }
   if (g->m.hack && g->m.hero)
   {
      names_short_title(g->m.hero, g->short_title, sizeof(g->short_title));
      names_plain_title(g->m.hero, g->nointro_title, sizeof(g->nointro_title));
   }
   if (g->m.recognised && g->short_title[0])
      snprintf(g->title, sizeof(g->title), "%s", g->short_title);
   else
      snprintf(g->title, sizeof(g->title), "%s", g->stem);
}

static int game_cmp(const void *a, const void *b)
{
   const game *x = (const game *)a, *y = (const game *)b;
   return strcasecmp(x->title, y->title);
}

void model_scan(scan_progress p)
{
   char roms[PATHCAP];
   int i, nf;
   s_prog = p;
   g_ngames = 0;
   nf = 1 + g_st.nfolders;
   if (g_st.found)
   {
      path_join(roms, sizeof(roms), g_st.install, "roms");
      s_frac0 = 0.15f;
      s_frac1 = 0.15f + 0.8f / nf;
      if (p) p("Scanning for ROMs\xE2\x80\xA6", roms + 5, s_frac0);
      scan_dir(roms, 3, "roms", "");
   }
   for (i = 0; i < g_st.nfolders; i++)
   {
      const char *f = g_st.folders[i];
      s_frac0 = 0.15f + 0.8f * (i + 1) / nf;
      s_frac1 = 0.15f + 0.8f * (i + 2) / nf;
      if (p) p("Scanning for ROMs\xE2\x80\xA6", f, s_frac0);
      /* a folder inside GBAdhoc's roms is already covered */
      if (g_st.found && strncasecmp(f, roms, strlen(roms)) == 0)
         continue;
      scan_dir(f, 2, strchr(f, '/') ? strchr(f, '/') + 1 : f, NULL);
   }
   for (i = 0; i < g_ngames; i++)
   {
      game *g = &g_games[i];
      char hero[PATHCAP], box[PATHCAP];
      recognise(g);
      if (g_st.found)
      {
         xw_default_art(g_st.install, g->rel[0] && !strchr(g->rel, ':') ? g->rel : g->file, hero, sizeof(hero),
                        box, sizeof(box));
         snprintf(g->own_hero, PATHCAP, "%s", hero);
         snprintf(g->own_box, PATHCAP, "%s", box);
      }
   }
   qsort(g_games, (size_t)g_ngames, sizeof(game), game_cmp);
   model_scan_entries();
   model_scan_custom();
   for (i = 0; i < g_ngames; i++)
      model_default_art(&g_games[i]);
   s_prog = NULL;
   g_scan_ver++;
}

/* ---- entries ---- */

static long stat_unix(const ScePspDateTime *t)
{
   long y = t->year, m = t->month, d = t->day;
   long era, yoe, doy, doe;
   if (y < 1970)
      return 0;
   y -= m <= 2;
   era = (y >= 0 ? y : y - 399) / 400;
   yoe = y - era * 400;
   doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
   doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
   return (era * 146097L + doe - 719468L) * 86400L + t->hour * 3600L + t->minute * 60L + t->second;
}

void model_scan_entries(void)
{
   static const char *devs[] = { "ms0:", "ef0:" };
   int d, i;
   g_nentries = 0;
   /* the "ROM not on the card" stand-ins are rebuilt below */
   while (g_ngames > 0 && strcmp(g_games[g_ngames - 1].folder, "not found") == 0)
      g_ngames--;
   for (i = 0; i < g_ngames; i++)
      g_games[i].entry = -1;
   for (d = 0; d < 2; d++)
   {
      char gd[64];
      SceUID dh;
      SceIoDirent e;
      snprintf(gd, sizeof(gd), "%s/PSP/GAME", devs[d]);
      dh = sceIoDopen(gd);
      if (dh < 0)
         continue;
      memset(&e, 0, sizeof(e));
      while (sceIoDread(dh, &e) > 0 && g_nentries < MAX_ENTRIES)
      {
         char ini[PATHCAP], eboot[PATHCAP], v[PATHCAP];
         unsigned char *buf;
         size_t n;
         entry_t *en = &g_entries[g_nentries];
         if (e.d_name[0] == '.' || !FIO_S_ISDIR(e.d_stat.st_mode))
         {
            memset(&e, 0, sizeof(e));
            continue;
         }
         snprintf(ini, sizeof(ini), "%s/%s/launch.ini", gd, e.d_name);
         if (read_all(ini, &buf, &n) == 0)
         {
            memset(en, 0, sizeof(*en));
            snprintf(en->dir, sizeof(en->dir), "%s/%s", gd, e.d_name);
            snprintf(en->folder, sizeof(en->folder), "%s", e.d_name);
            if (ini_get((char *)buf, "rom", v, sizeof(v)) == 0)
               snprintf(en->rom, sizeof(en->rom), "%s", v);
            if (ini_get((char *)buf, "gbadhoc", v, sizeof(v)) == 0)
               snprintf(en->gbadhoc, sizeof(en->gbadhoc), "%s", v);
            free(buf);
            snprintf(eboot, sizeof(eboot), "%s/EBOOT.PBP", en->dir);
            if (sfo_title(eboot, en->title, sizeof(en->title), &en->has_icon, &en->has_pic) != 0)
               snprintf(en->title, sizeof(en->title), "%s", e.d_name);
            {
               SceIoStat st;
               if (sceIoGetstat(ini, &st) >= 0)
                  en->made = stat_unix(&st.sce_st_mtime);
            }
            en->game = -1;
            if (en->rom[0])
               g_nentries++;
         }
         memset(&e, 0, sizeof(e));
      }
      sceIoDclose(dh);
   }
   /* tie each entry to its game: the same path, else the same file name
    * somewhere else (the ROM moved) */
   for (i = 0; i < g_nentries; i++)
   {
      entry_t *en = &g_entries[i];
      char full[PATHCAP];
      int j;
      if (strchr(en->rom, ':'))
         snprintf(full, sizeof(full), "%s", en->rom);
      else
         snprintf(full, sizeof(full), "%s/roms/%s", g_st.install, en->rom);
      for (j = 0; j < g_ngames; j++)
         if (g_games[j].entry < 0 && ieq_str(g_games[j].path, full))
         {
            en->game = j;
            break;
         }
      if (en->game < 0)
         for (j = 0; j < g_ngames; j++)
            if (g_games[j].entry < 0 && ieq_str(base_name(g_games[j].path), base_name(full)) && !file_exists(full))
            {
               en->game = j;
               en->rom_moved = 1;
               break;
            }
      if (en->game >= 0)
      {
         game *g = &g_games[en->game];
         g->entry = i;
         snprintf(g->title, sizeof(g->title), "%s", en->title);
      }
      else if (g_ngames < MAX_GAMES)
      {
         /* an entry whose ROM is not on the card: listed so it can be removed */
         game *g = &g_games[g_ngames];
         memset(g, 0, sizeof(*g));
         snprintf(g->path, sizeof(g->path), "%s", full);
         snprintf(g->rel, sizeof(g->rel), "%s", en->rom);
         snprintf(g->file, sizeof(g->file), "%s", base_name(full));
         stem_of(full, g->stem, sizeof(g->stem));
         snprintf(g->folder, sizeof(g->folder), "not found");
         snprintf(g->title, sizeof(g->title), "%s", en->title);
         g->sys = rom_sys(full) >= 0 ? rom_sys(full) : SYS_GBA;
         g->entry = i;
         names_match(g->stem, g->sys, g_prefs.region, &g->m);
         en->game = -1;
         g_ngames++;
      }
   }
}
