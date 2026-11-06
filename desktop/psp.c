/* psp.c -- see psp.h. */
#include "psp.h"
#include "art.h"
#include "util.h"
#include "../shared/xw_core.h"
#include "../shared/xw_match.h"

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build/launcher_pbp.h"      /* xw_launcher_pbp[], xw_launcher_pbp_len */

/* ---- test folders ------------------------------------------------------- */

#define MAX_FAKE 4
static struct { char root[1024]; char kind[4]; int go; } g_fake[MAX_FAKE];
static int g_nfake;

void psp_add_fake(const char *spec)
{
   char tmp[1100], *c;
   if (g_nfake >= MAX_FAKE)
      return;
   snprintf(tmp, sizeof(tmp), "%s", spec);
   strcpy(g_fake[g_nfake].kind, "ms0");
   while ((c = strrchr(tmp, ',')) != NULL)
   {
      if (!strcmp(c + 1, "ef0")) strcpy(g_fake[g_nfake].kind, "ef0");
      if (!strcmp(c + 1, "go")) g_fake[g_nfake].go = 1;
      *c = '\0';
   }
   u_slashes(tmp);
   if (tmp[0] && tmp[strlen(tmp) - 1] != '/')
      strncat(tmp, "/", sizeof(tmp) - strlen(tmp) - 1);
   snprintf(g_fake[g_nfake].root, sizeof(g_fake[0].root), "%s", tmp);
   g_nfake++;
}

int psp_fake_mode(void)
{
   return g_nfake > 0;
}

/* ---- allowed paths for the UI's image requests -------------------------- */

static CRITICAL_SECTION g_allow_cs;
static char *g_allowed[64];
static int g_nallowed;
static LONG g_allow_init;

static void allow_init(void)
{
   if (InterlockedCompareExchange(&g_allow_init, 1, 0) == 0)
   {
      InitializeCriticalSection(&g_allow_cs);
      InterlockedExchange(&g_allow_init, 2);
   }
   while (g_allow_init != 2)
      Sleep(1);
}

void psp_allow_path(const char *path)
{
   allow_init();
   EnterCriticalSection(&g_allow_cs);
   if (g_nallowed == 64)
   {
      free(g_allowed[0]);
      memmove(g_allowed, g_allowed + 1, 63 * sizeof(char *));
      g_nallowed--;
   }
   g_allowed[g_nallowed++] = _strdup(path);
   LeaveCriticalSection(&g_allow_cs);
}

int psp_path_allowed(const char *path)
{
   psp_dev d[8];
   char cache[1024];
   int n, i, ok = 0;
   if (strstr(path, ".."))
      return 0;
   art_cache_dir(cache, sizeof(cache));
   if (u_istarts(path, cache))
      return 1;
   n = psp_devices(d, 8);
   for (i = 0; i < n; i++)
      if (u_istarts(path, d[i].root))
         return 1;
   allow_init();
   EnterCriticalSection(&g_allow_cs);
   for (i = 0; i < g_nallowed; i++)
      if (u_ieq(path, g_allowed[i]))
         ok = 1;
   LeaveCriticalSection(&g_allow_cs);
   return ok;
}

/* ---- devices ------------------------------------------------------------ */

static void trim(char *s)
{
   size_t n = strlen(s);
   while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
      s[--n] = '\0';
   while (s[0] == ' ')
      memmove(s, s + 1, strlen(s));
}

/* The USB storage's vendor, product and serial, from the volume. */
static void storage_names(char letter, char *product, size_t pcap, char *serial, size_t scap)
{
   wchar_t dev[8];
   HANDLE h;
   STORAGE_PROPERTY_QUERY q;
   unsigned char buf[1024];
   DWORD got = 0;
   product[0] = serial[0] = '\0';
   swprintf(dev, 8, L"\\\\.\\%c:", letter);
   h = CreateFileW(dev, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
   if (h == INVALID_HANDLE_VALUE)
      return;
   memset(&q, 0, sizeof(q));
   q.PropertyId = StorageDeviceProperty;
   q.QueryType = PropertyStandardQuery;
   if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &got, NULL) &&
       got >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
   {
      STORAGE_DEVICE_DESCRIPTOR *d = (STORAGE_DEVICE_DESCRIPTOR *)buf;
      char v[64] = "", p[64] = "";
      if (d->VendorIdOffset && d->VendorIdOffset < got)
         snprintf(v, sizeof(v), "%s", (char *)buf + d->VendorIdOffset);
      if (d->ProductIdOffset && d->ProductIdOffset < got)
         snprintf(p, sizeof(p), "%s", (char *)buf + d->ProductIdOffset);
      if (d->SerialNumberOffset && d->SerialNumberOffset < got)
         snprintf(serial, scap, "%s", (char *)buf + d->SerialNumberOffset);
      trim(v);
      trim(p);
      trim(serial);
      snprintf(product, pcap, "%s%s%s", v, v[0] ? " " : "", p);
   }
   CloseHandle(h);
}

/* GBAdhoc's install on a storage: a PSP/GAME folder with EBOOT.PBP and
 * roms/ whose PARAM.SFO title starts "GBAdhoc" (and is not an entry). */
static void find_install(const char *root, char *install, size_t icap, char *title, size_t tcap)
{
   char game[1100], pat[1200];
   wchar_t *w;
   WIN32_FIND_DATAW fd;
   HANDLE h;
   int best = 0;
   install[0] = title[0] = '\0';
   u_join(game, sizeof(game), root, "PSP/GAME");
   u_join(pat, sizeof(pat), game, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h == INVALID_HANDLE_VALUE)
      return;
   do
   {
      char *name, dir[1300], p[1400];
      int score = 0;
      xw_buf pbp;
      char t[128] = "";
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
         continue;
      name = u_utf8(fd.cFileName);
      if (!name)
         continue;
      u_join(dir, sizeof(dir), game, name);
      u_join(p, sizeof(p), dir, "launch.ini");
      if (u_is_file(p))
      {
         free(name);
         continue;
      }
      u_join(p, sizeof(p), dir, "roms");
      if (u_is_dir(p))
         score = 1;
      u_join(p, sizeof(p), dir, "EBOOT.PBP");
      if (score && xw_read_file(p, &pbp) == 0)
      {
         const unsigned char *s[XW_NSECT];
         size_t l[XW_NSECT];
         if (xw_pbp_split(&pbp, s, l) == 0)
            xw_sfo_get_string(s[XW_SFO], l[XW_SFO], "TITLE", t, sizeof(t));
         xw_buf_free(&pbp);
         if (u_istarts(t, "GBAdhoc"))
            score += 2;
      }
      if (score && u_ieq(name, "GBAdhoc"))
         score += 4;
      if (score > best)
      {
         best = score;
         snprintf(install, icap, "PSP/GAME/%s", name);
         snprintf(title, tcap, "%s", t);
      }
      free(name);
   } while (FindNextFileW(h, &fd));
   FindClose(h);
}

static void fill_space(psp_dev *d)
{
   ULARGE_INTEGER fr, tot;
   wchar_t *w = u_wide(d->root);
   char env[32];
   if (w && GetDiskFreeSpaceExW(w, &fr, &tot, NULL))
   {
      d->free_mb = (double)fr.QuadPart / 1048576.0;
      d->total_mb = (double)tot.QuadPart / 1048576.0;
   }
   free(w);
   if (d->fake)
   {
      d->total_mb = 7629;
      d->free_mb = 212;
      if (GetEnvironmentVariableA("XMB_FAKE_FREE_MB", env, sizeof(env)))
         d->free_mb = atof(env);
   }
}

int psp_devices(psp_dev *out, int cap)
{
   int n = 0, i;
   if (g_nfake)
   {
      for (i = 0; i < g_nfake && n < cap; i++)
      {
         char p[1100];
         psp_dev *d = &out[n];
         u_join(p, sizeof(p), g_fake[i].root, "PSP");
         if (!u_is_dir(p))
            continue;               /* "unplugged" */
         memset(d, 0, sizeof(*d));
         snprintf(d->id, sizeof(d->id), "F%d", i);
         snprintf(d->root, sizeof(d->root), "%s", g_fake[i].root);
         snprintf(d->kind, sizeof(d->kind), "%s", g_fake[i].kind);
         snprintf(d->label, sizeof(d->label), "%s", !strcmp(d->kind, "ef0") ? "Internal storage" : "Memory Stick");
         snprintf(d->product, sizeof(d->product), "test folder");
         snprintf(d->serial, sizeof(d->serial), "%s", g_fake[i].go ? "FAKEGO" : "");
         d->go = g_fake[i].go;
         d->psp = 1;
         d->fake = 1;
         fill_space(d);
         find_install(d->root, d->install, sizeof(d->install), d->install_title, sizeof(d->install_title));
         n++;
      }
      return n;
   }
   {
      DWORD mask = GetLogicalDrives();
      UINT old = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
      char sysdrv[8] = "C";
      GetEnvironmentVariableA("SystemDrive", sysdrv, sizeof(sysdrv));
      for (i = 2; i < 26 && n < cap; i++)
      {
         wchar_t r[4] = { (wchar_t)('A' + i), ':', '\\', 0 }, lab[64] = L"";
         UINT t;
         char p[16];
         psp_dev *d = &out[n];
         if (!(mask & (1u << i)) || (char)('A' + i) == sysdrv[0])
            continue;
         t = GetDriveTypeW(r);
         if (t != DRIVE_REMOVABLE && t != DRIVE_FIXED)
            continue;
         snprintf(p, sizeof(p), "%c:/PSP", 'A' + i);
         if (!u_is_dir(p))
            continue;
         memset(d, 0, sizeof(*d));
         snprintf(d->id, sizeof(d->id), "%c", 'A' + i);
         snprintf(d->root, sizeof(d->root), "%c:/", 'A' + i);
         snprintf(d->letter, sizeof(d->letter), "%c:", 'A' + i);
         if (GetVolumeInformationW(r, lab, 64, NULL, NULL, NULL, NULL, 0))
         {
            char *l = u_utf8(lab);
            snprintf(d->label, sizeof(d->label), "%s", l ? l : "");
            free(l);
         }
         storage_names((char)('A' + i), d->product, sizeof(d->product), d->serial, sizeof(d->serial));
         d->psp = strstr(d->product, "PSP") != NULL;
         /* SONY "PSP" SS is a PSP Go's internal storage (ef0:) */
         strcpy(d->kind, d->psp && strstr(d->product, "\" SS") ? "ef0" : "ms0");
         fill_space(d);
         find_install(d->root, d->install, sizeof(d->install), d->install_title, sizeof(d->install_title));
         n++;
      }
      SetErrorMode(old);
   }
   /* a PSP Go: two storages, one serial, one of them SS */
   for (i = 0; i < n; i++)
   {
      int j;
      if (!strcmp(out[i].kind, "ef0"))
         out[i].go = 1;
      for (j = 0; j < n; j++)
         if (j != i && out[i].serial[0] && !strcmp(out[i].serial, out[j].serial) &&
             !strcmp(out[j].kind, "ef0"))
            out[i].go = 1;
   }
   return n;
}

int psp_find(const char *id, psp_dev *out)
{
   psp_dev d[8];
   int n = psp_devices(d, 8), i;
   for (i = 0; i < n; i++)
      if (!strcmp(d[i].id, id))
      {
         *out = d[i];
         return 0;
      }
   return -1;
}

static void dev_json(sb *b, const psp_dev *d)
{
   sb_add(b, "{");
   sb_kv_str(b, "id", d->id);
   sb_kv_str(b, "root", d->root);
   sb_kv_str(b, "letter", d->letter);
   sb_kv_str(b, "label", d->label);
   sb_kv_str(b, "product", d->product);
   sb_kv_str(b, "serial", d->serial);
   sb_kv_str(b, "kind", d->kind);
   sb_kv_bool(b, "go", d->go);
   sb_kv_bool(b, "psp", d->psp);
   sb_kv_bool(b, "fake", d->fake);
   sb_kv_num(b, "totalMB", (double)(long long)(d->total_mb * 10) / 10);
   sb_kv_num(b, "freeMB", (double)(long long)(d->free_mb * 10) / 10);
   sb_kv_str(b, "install", d->install);
   sb_kv_str(b, "installTitle", d->install_title);
   sb_add(b, "}");
}

void psp_devices_json(sb *b)
{
   psp_dev d[8];
   int n = psp_devices(d, 8), i;
   sb_add(b, "[");
   for (i = 0; i < n; i++)
   {
      if (i)
         sb_add(b, ",");
      dev_json(b, &d[i]);
   }
   sb_add(b, "]");
}

/* ---- recovery: finish or undo what an unplug interrupted ---------------- */

/* <install>/xmb/stage/<folder>/      an entry being written
 * <install>/xmb/stage/<folder>.ready all of it is written and flushed
 * <install>/xmb/trash/<folder>/      the entry it replaces, until the swap
 * PSP/GAME only ever sees a whole entry: the stage moves in by one rename. */
static void recover(const psp_dev *d, sb *log)
{
   char inst[1300], stage[1400], trash[1400], pat[1500];
   wchar_t *w;
   WIN32_FIND_DATAW fd;
   HANDLE h;
   if (!d->install[0])
      return;
   u_join(inst, sizeof(inst), d->root, d->install);
   u_join(stage, sizeof(stage), inst, "xmb/stage");
   u_join(trash, sizeof(trash), inst, "xmb/trash");
   u_join(pat, sizeof(pat), stage, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h != INVALID_HANDLE_VALUE)
   {
      do
      {
         char *name, sdir[1600], mark[1700], fin[1600], tdir[1600];
         if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
            continue;
         name = u_utf8(fd.cFileName);
         if (!name)
            continue;
         u_join(sdir, sizeof(sdir), stage, name);
         snprintf(mark, sizeof(mark), "%s.ready", sdir);
         u_join(fin, sizeof(fin), d->root, "PSP/GAME");
         u_join(fin, sizeof(fin), fin, name);
         u_join(tdir, sizeof(tdir), trash, name);
         if (u_is_file(mark))
         {
            /* written in full: put it in place */
            if (u_is_dir(fin))
            {
               u_rmtree(tdir);
               u_move(fin, tdir);
            }
            if (u_move(sdir, fin) == 0)
            {
               DeleteFileW((w = u_wide(mark)));
               free(w);
               u_rmtree(tdir);
               if (log)
               {
                  if (log->n > 1) sb_add(log, ",");
                  sb_add(log, "{");
                  sb_kv_str(log, "folder", name);
                  sb_kv_str(log, "did", "finished");
                  sb_add(log, "}");
               }
               u_log("recover: finished %s", name);
            }
         }
         else
         {
            /* half-written: it never reached PSP/GAME; drop it */
            u_rmtree(sdir);
            if (log)
            {
               if (log->n > 1) sb_add(log, ",");
               sb_add(log, "{");
               sb_kv_str(log, "folder", name);
               sb_kv_str(log, "did", "discarded");
               sb_add(log, "}");
            }
            u_log("recover: discarded half-written %s", name);
         }
         free(name);
      } while (FindNextFileW(h, &fd));
      FindClose(h);
   }
   /* a replaced entry whose new one never arrived: put the old one back */
   u_join(pat, sizeof(pat), trash, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h != INVALID_HANDLE_VALUE)
   {
      do
      {
         char *name, tdir[1600], fin[1600];
         if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
            continue;
         name = u_utf8(fd.cFileName);
         if (!name)
            continue;
         u_join(tdir, sizeof(tdir), trash, name);
         u_join(fin, sizeof(fin), d->root, "PSP/GAME");
         u_join(fin, sizeof(fin), fin, name);
         if (u_is_dir(fin))
            u_rmtree(tdir);
         else if (u_move(tdir, fin) == 0 && log)
         {
            if (log->n > 1) sb_add(log, ",");
            sb_add(log, "{");
            sb_kv_str(log, "folder", name);
            sb_kv_str(log, "did", "restored");
            sb_add(log, "}");
         }
         free(name);
      } while (FindNextFileW(h, &fd));
      FindClose(h);
   }
   {
      /* the working folders go once empty */
      char xd[1400];
      w = u_wide(trash); RemoveDirectoryW(w); free(w);
      w = u_wide(stage); RemoveDirectoryW(w); free(w);
      u_join(xd, sizeof(xd), inst, "xmb");
      w = u_wide(xd); RemoveDirectoryW(w); free(w);
   }
}

/* ---- scanning ----------------------------------------------------------- */

typedef struct
{
   char path[1100];      /* absolute, '/' */
   long long size;
} rom_file;

typedef struct
{
   rom_file *f;
   int n, cap;
} rom_list;

static int is_rom_ext(const char *e)
{
   return u_ieq(e, "gba") || u_ieq(e, "gbc") || u_ieq(e, "gb");
}

static void walk(const char *dir, int depth, rom_list *l, psp_prog cb, void *ctx)
{
   char pat[1200];
   wchar_t *w;
   WIN32_FIND_DATAW fd;
   HANDLE h;
   if (depth > 4 || l->n >= 2000)
      return;
   u_join(pat, sizeof(pat), dir, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h == INVALID_HANDLE_VALUE)
      return;
   do
   {
      char *name, p[1200];
      if (fd.cFileName[0] == '.')
         continue;
      name = u_utf8(fd.cFileName);
      if (!name)
         continue;
      u_join(p, sizeof(p), dir, name);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
         /* GBAdhoc 4.6 keeps saves/ and states/ beside (or under) roms/: never ROMs */
         if (!u_ieq(name, "saves") && !u_ieq(name, "states") && !u_ieq(name, "xmb"))
            walk(p, depth + 1, l, cb, ctx);
      }
      else if (is_rom_ext(u_ext(name)))
      {
         if (l->n == l->cap)
         {
            int c = l->cap ? l->cap * 2 : 64;
            rom_file *nf = (rom_file *)realloc(l->f, (size_t)c * sizeof(*nf));
            if (!nf)
            {
               free(name);
               break;
            }
            l->f = nf;
            l->cap = c;
         }
         snprintf(l->f[l->n].path, sizeof(l->f[0].path), "%s", p);
         l->f[l->n].size = (long long)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
         l->n++;
      }
      free(name);
   } while (FindNextFileW(h, &fd));
   FindClose(h);
}

static void prog_kv(psp_prog cb, void *ctx, const char *k1, const char *v1, const char *k2,
                    const char *v2, double p)
{
   sb b;
   if (!cb)
      return;
   sb_init(&b);
   sb_add(&b, "{");
   if (k1) sb_kv_str(&b, k1, v1);
   if (k2) sb_kv_str(&b, k2, v2);
   if (p >= 0) sb_kv_num(&b, "p", p);
   sb_add(&b, "}");
   cb(ctx, b.s);
   sb_free(&b);
}

/* A save next to the ROM, as GBAdhoc names it (.sav, .gbc.sav, .gb.sav). */
static int has_save(const char *path, const char *sys)
{
   char p[1200], *dot;
   snprintf(p, sizeof(p), "%s", path);
   dot = strrchr(p, '.');
   if (!dot || dot < strrchr(p, '/'))
      return 0;
   *dot = '\0';
   {
      char q[1300];
      snprintf(q, sizeof(q), "%s%s", p, !strcmp(sys, "GBA") ? ".sav" : !strcmp(sys, "GBC") ? ".gbc.sav" : ".gb.sav");
      if (u_is_file(q))
         return 1;
      snprintf(q, sizeof(q), "%s.sav", p);
      return u_is_file(q);
   }
}

/* ... or in GBAdhoc 4.6's saves/ folder (saves/<stem>.sav, or by system). */
static int has_save_in(const char *inst, const char *path, const char *sys)
{
   static const char *sub[] = { "", "GBA/", "GB/", "GBC/" };
   char stem[512], q[1700];
   size_t i;
   if (!inst[0])
      return 0;
   u_stem(path, stem, sizeof(stem));
   for (i = 0; i < 4; i++)
   {
      snprintf(q, sizeof(q), "%s/saves/%s%s%s", inst, sub[i], stem,
               !strcmp(sys, "GBA") ? ".sav" : !strcmp(sys, "GBC") ? ".gbc.sav" : ".gb.sav");
      if (u_is_file(q))
         return 1;
      snprintf(q, sizeof(q), "%s/saves/%s%s.sav", inst, sub[i], stem);
      if (u_is_file(q))
         return 1;
   }
   return 0;
}

static void game_id(const char *path, char *out)
{
   char low[1200];
   size_t i;
   for (i = 0; path[i] && i < sizeof(low) - 1; i++)
      low[i] = path[i] >= 'A' && path[i] <= 'Z' ? (char)(path[i] + 32) : path[i];
   low[i] = '\0';
   sprintf(out, "g%08lx", xw_crc32(0, low, i));
}

/* The ROM as launch.ini names it, from the storage it is on. */
static void launch_rom(const psp_dev *d, const char *path, char *out, size_t cap)
{
   char roms[1300];
   u_join(roms, sizeof(roms), d->root, d->install);
   u_join(roms, sizeof(roms), roms, "roms/");
   if (d->install[0] && u_istarts(path, roms))
   {
      xw_rom_for_psp(path, path + strlen(roms), out, cap);
      return;
   }
   /* outside GBAdhoc's roms folder: an absolute ms0:/ (or ef0:/) path */
   {
      char rel[1200];
      const unsigned char *c;
      snprintf(rel, sizeof(rel), "%s", path + strlen(d->root));
      for (c = (const unsigned char *)rel; *c && *c < 0x80; c++)
         ;
      if (*c)
      {
         /* non-ASCII: the FAT 8.3 form, which the firmware always opens */
         wchar_t *w = u_wide(path), sp[1024];
         DWORD n = w ? GetShortPathNameW(w, sp, 1024) : 0;
         free(w);
         if (n > 0 && n < 1024)
         {
            char *s = u_utf8(sp);
            if (s && strlen(s) > strlen(d->root))
            {
               u_slashes(s);
               snprintf(rel, sizeof(rel), "%s", s + strlen(d->root));
            }
            free(s);
         }
      }
      snprintf(out, cap, "%s:/%s", d->kind, rel);
   }
}

typedef struct
{
   char dir[1100];
   char folder[128];
   xw_entry_info info;
   char rom_abs[1300];
   int matched;          /* a game has it */
} entry_rec;

static int scan_entries(const psp_dev *d, entry_rec **out)
{
   char game[1100], pat[1200];
   wchar_t *w;
   WIN32_FIND_DATAW fd;
   HANDLE h;
   int n = 0, cap = 0;
   *out = NULL;
   u_join(game, sizeof(game), d->root, "PSP/GAME");
   u_join(pat, sizeof(pat), game, "*");
   w = u_wide(pat);
   h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
   free(w);
   if (h == INVALID_HANDLE_VALUE)
      return 0;
   do
   {
      char *name;
      entry_rec e;
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
         continue;
      name = u_utf8(fd.cFileName);
      if (!name)
         continue;
      memset(&e, 0, sizeof(e));
      u_join(e.dir, sizeof(e.dir), game, name);
      snprintf(e.folder, sizeof(e.folder), "%s", name);
      free(name);
      if (xw_read_entry(e.dir, xw_launcher_pbp, xw_launcher_pbp_len, &e.info) != 0)
         continue;
      /* where its ROM is */
      if (strchr(e.info.rom, ':'))
      {
         const char *c = strchr(e.info.rom, ':');
         snprintf(e.rom_abs, sizeof(e.rom_abs), "%s%s", d->root, c[1] == '/' ? c + 2 : c + 1);
      }
      else
      {
         char inst[1300];
         u_join(inst, sizeof(inst), d->root, e.info.gbadhoc[0] ? e.info.gbadhoc : d->install);
         u_join(inst, sizeof(inst), inst, "roms");
         u_join(e.rom_abs, sizeof(e.rom_abs), inst, e.info.rom);
      }
      if (n == cap)
      {
         int c = cap ? cap * 2 : 16;
         entry_rec *ne = (entry_rec *)realloc(*out, (size_t)c * sizeof(*ne));
         if (!ne)
            break;
         *out = ne;
         cap = c;
      }
      (*out)[n++] = e;
   } while (FindNextFileW(h, &fd));
   FindClose(h);
   return n;
}

static void entry_json(sb *b, const entry_rec *e, const char *dev_id)
{
   char url[2600], enc[2400], eboot[1200];
   sb_add(b, "{");
   sb_kv_str(b, "dev", dev_id);
   sb_kv_str(b, "folder", e->folder);
   sb_kv_str(b, "dir", e->dir);
   sb_kv_str(b, "title", e->info.title);
   sb_kv_str(b, "rom", e->info.rom);
   sb_kv_str(b, "gbadhoc", e->info.gbadhoc);
   sb_kv_bool(b, "launcher", e->info.launcher == 1);
   sb_kv_bool(b, "romOk", u_is_file(e->rom_abs));
   sb_kv_str(b, "romAbs", e->rom_abs);
   sb_kv_num(b, "kb", (double)(e->info.pbp_len / 1024));
   u_join(eboot, sizeof(eboot), e->dir, "EBOOT.PBP");
   u_urlenc(eboot, enc, sizeof(enc), 0);
   if (e->info.has_icon)
   {
      snprintf(url, sizeof(url), "https://xmb.gbadhoc/pbp?s=1&p=%s", enc);
      sb_kv_str(b, "iconUrl", url);
   }
   if (e->info.has_pic)
   {
      snprintf(url, sizeof(url), "https://xmb.gbadhoc/pbp?s=4&p=%s", enc);
      sb_kv_str(b, "picUrl", url);
   }
   {
      WIN32_FILE_ATTRIBUTE_DATA fa;
      wchar_t *w = u_wide(eboot);
      if (w && GetFileAttributesExW(w, GetFileExInfoStandard, &fa))
      {
         ULARGE_INTEGER t;
         t.LowPart = fa.ftLastWriteTime.dwLowDateTime;
         t.HighPart = fa.ftLastWriteTime.dwHighDateTime;
         sb_kv_num(b, "made", (double)((t.QuadPart / 10000000ULL) - 11644473600ULL));
      }
      free(w);
   }
   sb_add(b, "}");
}

static void file_url(const char *path, char *out, size_t cap)
{
   char enc[2400];
   u_urlenc(path, enc, sizeof(enc), 0);
   snprintf(out, cap, "https://xmb.gbadhoc/fs?p=%s", enc);
}

int psp_scan(const jv *args, psp_prog cb, void *ctx, sb *out, char *err, size_t cap)
{
   psp_dev d, all[8];
   rom_list roms = { 0, 0, 0 };
   entry_rec *ent = NULL;
   int nent = 0, i, k, nall, ngba = 0, ngbc = 0, ngb = 0;
   char inst[1300], dir[1300], **folder_of;
   const jv *folders = jv_get(args, "folders");
   sb rec;

   if (psp_find(jv_str(args, "dev", ""), &d) != 0)
   {
      snprintf(err, cap, "The PSP is not connected.");
      return -1;
   }
   sb_init(&rec);
   sb_add(&rec, "[");
   prog_kv(cb, ctx, "msg", "Looking for GBAdhoc…", "file", d.root, 5);
   recover(&d, &rec);
   u_join(inst, sizeof(inst), d.root, d.install);
   /* the folders: GBAdhoc's roms, then the ones the player added */
   if (d.install[0])
   {
      char m[300];
      snprintf(m, sizeof(m), "%s found", d.install_title[0] ? d.install_title : "GBAdhoc");
      prog_kv(cb, ctx, "msg", m, "file", inst, 15);
      u_join(dir, sizeof(dir), inst, "roms");
      prog_kv(cb, ctx, "msg", "Scanning for ROMs…", "file", dir, 20);
      walk(dir, 0, &roms, cb, ctx);
   }
   for (i = 0; i < jv_len(folders); i++)
   {
      const jv *f = jv_at(folders, i);
      if (!f || f->t != JV_STR || !f->s[0])
         continue;
      u_join(dir, sizeof(dir), d.root, f->s);
      if (d.install[0])
      {
         char r[1300];
         u_join(r, sizeof(r), inst, "roms");
         if (u_istarts(dir, r))
            continue;                       /* already walked */
      }
      prog_kv(cb, ctx, "msg", "Scanning for ROMs…", "file", dir, 20 + 20.0 * i / (jv_len(folders) + 1));
      walk(dir, 0, &roms, cb, ctx);
   }
   for (i = 0; i < roms.n; i++)
   {
      const char *e = u_ext(roms.f[i].path);
      if (u_ieq(e, "gba")) ngba++;
      else if (u_ieq(e, "gbc")) ngbc++;
      else ngb++;
   }
   prog_kv(cb, ctx, "msg", "Getting the No-Intro lists ready…", "file", "libretro-database", 40);
   art_prepare_dats(ngba > 0, ngbc > 0 || ngb > 0, ngb > 0 || ngbc > 0, cb, ctx);

   nent = scan_entries(&d, &ent);
   /* a PSP Go: entries on its other storage too */
   nall = psp_devices(all, 8);
   for (k = 0; k < nall; k++)
      if (d.go && all[k].go && strcmp(all[k].id, d.id) && !strcmp(all[k].serial, d.serial))
      {
         entry_rec *more = NULL;
         int nm = scan_entries(&all[k], &more), j;
         entry_rec *ne = (entry_rec *)realloc(ent, (size_t)(nent + nm + 1) * sizeof(*ne));
         if (ne)
         {
            ent = ne;
            for (j = 0; j < nm; j++)
               ent[nent++] = more[j];
         }
         free(more);
      }

   sb_add(out, "{");
   sb_kv_str(out, "dev", d.id);
   sb_kv_str(out, "install", d.install);
   sb_key(out, "games");
   sb_add(out, "[");
   folder_of = NULL;
   (void)folder_of;
   for (i = 0; i < roms.n; i++)
   {
      const char *path = roms.f[i].path;
      art_ident id;
      char gid[16], lr[1300], folder[1200], stem[512], hero[1300], box[1300], url[2700];
      char m[700];
      int j;
      snprintf(m, sizeof(m), "%d ROMs found, matching No-Intro titles…", i + 1);
      prog_kv(cb, ctx, "msg", m, "file", u_base(path), 45 + 50.0 * i / roms.n);
      art_identify(path, roms.f[i].size, &id);
      game_id(path + strlen(d.root), gid);   /* the same whichever letter the PSP gets */
      launch_rom(&d, path, lr, sizeof(lr));
      /* the folder, as the design shows it: roms, roms/hacks, ROMS/GBA */
      if (d.install[0] && u_istarts(path, inst))
         snprintf(folder, sizeof(folder), "%s", path + strlen(inst) + 1);
      else
         snprintf(folder, sizeof(folder), "%s", path + strlen(d.root));
      {
         char *sl = strrchr(folder, '/');
         if (sl) *sl = '\0'; else folder[0] = '\0';
      }
      u_stem(path, stem, sizeof(stem));
      hero[0] = box[0] = '\0';
      if (d.install[0])
      {
         char rel[1200];
         char r[1300];
         u_join(r, sizeof(r), inst, "roms/");
         snprintf(rel, sizeof(rel), "%s", u_istarts(path, r) ? path + strlen(r) : u_base(path));
         xw_default_art(inst, rel, hero, sizeof(hero), box, sizeof(box));
      }
      if (i)
         sb_add(out, ",");
      sb_add(out, "{");
      sb_kv_str(out, "id", gid);
      sb_kv_str(out, "path", path);
      sb_kv_str(out, "file", u_base(path));
      sb_kv_str(out, "folder", folder);
      sb_kv_str(out, "stem", stem);
      sb_kv_str(out, "system", id.sys);
      sb_kv_num(out, "size", (double)roms.f[i].size / 1048576.0);
      sb_kv_str(out, "launchRom", lr);
      sb_kv_str(out, "nointro", id.nointro);
      sb_kv_str(out, "title", id.title);
      sb_kv_str(out, "base", id.base);
      sb_kv_str(out, "how", id.how);
      sb_kv_str(out, "code", id.code);
      sb_kv_bool(out, "recognised", id.recognised);
      sb_kv_num(out, "confidence", id.confidence);
      sb_kv_bool(out, "hack", id.hack);
      sb_kv_bool(out, "save", has_save(path, id.sys) || has_save_in(d.install[0] ? inst : "", path, id.sys));
      if (box[0]) { file_url(box, url, sizeof(url)); sb_kv_str(out, "ownBoxart", url); sb_kv_str(out, "ownBoxartPath", box); }
      if (hero[0]) { file_url(hero, url, sizeof(url)); sb_kv_str(out, "ownHero", url); sb_kv_str(out, "ownHeroPath", hero); }
      /* its entry */
      for (j = 0; j < nent; j++)
         if (!ent[j].matched && (u_ieq(ent[j].rom_abs, path) || u_ieq(ent[j].info.rom, lr)))
         {
            ent[j].matched = 1;
            sb_key(out, "entry");
            entry_json(out, &ent[j], d.id);
            break;
         }
      if (j == nent)   /* an entry whose ROM moved here? */
         for (j = 0; j < nent; j++)
            if (!ent[j].matched && !u_is_file(ent[j].rom_abs) &&
                u_ieq(u_base(ent[j].info.rom), u_base(path)))
            {
               ent[j].matched = 1;
               sb_key(out, "entry");
               entry_json(out, &ent[j], d.id);
               sb_kv_bool(out, "romMoved", 1);
               break;
            }
      sb_add(out, "}");
   }
   sb_add(out, "]");
   /* entries with no ROM on the stick */
   sb_key(out, "orphans");
   sb_add(out, "[");
   for (i = 0, k = 0; i < nent; i++)
      if (!ent[i].matched)
      {
         if (k++)
            sb_add(out, ",");
         entry_json(out, &ent[i], d.id);
      }
   sb_add(out, "]");
   sb_add(&rec, "]");
   sb_key(out, "recovered");
   sb_add(out, rec.s);
   sb_kv_num(out, "folders", jv_len(folders) + (d.install[0] ? 1 : 0));
   sb_add(out, "}");
   prog_kv(cb, ctx, "msg", "Done", NULL, NULL, 100);
   sb_free(&rec);
   free(roms.f);
   free(ent);
   return 0;
}

/* ---- writing ------------------------------------------------------------ */

/* Test hook: XMB_FAULT=eboot|move[@N] makes the Nth write (default: every
 * one) act as if the PSP was pulled at that point. */
static int fault(const char *where)
{
   static volatile LONG calls[2];
   char v[32], *at;
   int which = !strcmp(where, "move");
   long n;
   if (!GetEnvironmentVariableA("XMB_FAULT", v, sizeof(v)))
      return 0;
   at = strchr(v, '@');
   if (at)
      *at++ = '\0';
   if (strcmp(v, where))
      return 0;
   n = InterlockedIncrement(&calls[which]);
   return !at || n == atol(at);
}

static int root_gone(const psp_dev *d)
{
   char p[1100];
   u_join(p, sizeof(p), d->root, "PSP");
   return !u_is_dir(p);
}

static const char *classify(const psp_dev *d, DWORD e)
{
   if (e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL)
      return "nospace";
   if (root_gone(d) || e == ERROR_DEVICE_NOT_CONNECTED || e == ERROR_NOT_READY ||
       e == ERROR_DEV_NOT_EXIST || e == 433 /* ERROR_NO_SUCH_DEVICE */)
      return "disconnected";
   return "io";
}

static void step(psp_prog cb, void *ctx, const char *msg, int ok, int warn)
{
   sb b;
   if (!cb)
      return;
   sb_init(&b);
   sb_add(&b, "{");
   sb_kv_str(&b, "msg", msg);
   if (ok >= 0)
      sb_kv_bool(&b, "ok", ok);
   if (warn)
      sb_kv_bool(&b, "warn", 1);
   sb_add(&b, "}");
   cb(ctx, b.s);
   sb_free(&b);
}

static int valid_folder(const char *f)
{
   size_t i;
   if (!f[0] || strlen(f) > 40)
      return 0;
   for (i = 0; f[i]; i++)
      if (!((f[i] >= 'A' && f[i] <= 'Z') || (f[i] >= 'a' && f[i] <= 'z') ||
            (f[i] >= '0' && f[i] <= '9') || f[i] == '_' || f[i] == '-'))
         return 0;
   return 1;
}

static int copy_if_missing(const char *src, const char *dst)
{
   xw_buf b;
   char dir[1300], *sl;
   if (!src[0] || !dst[0] || u_is_file(dst) || !psp_path_allowed(src))
      return 0;
   if (xw_read_file(src, &b) != 0)
      return -1;
   snprintf(dir, sizeof(dir), "%s", dst);
   sl = strrchr(dir, '/');
   if (sl)
   {
      *sl = '\0';
      u_mkdirs(dir);
   }
   {
      DWORD e = u_write_durable(dst, b.data, b.len);
      xw_buf_free(&b);
      return e ? -1 : 1;
   }
}

int psp_make(const jv *args, psp_prog cb, void *ctx, sb *out, char *code,
             char *err, size_t cap)
{
   psp_dev d;
   const char *folder = jv_str(args, "folder", ""), *title = jv_str(args, "title", "");
   const char *rom = jv_str(args, "rom", ""), *gb = jv_str(args, "gbadhoc", "");
   const char *old = jv_str(args, "oldFolder", "");
   int replace = jv_bool(args, "replace", 0);
   xw_buf icon = { 0, 0 }, pic = { 0, 0 }, pbp = { 0, 0 }, back = { 0, 0 };
   xw_entry e;
   char inst[1300], stage[1400], sdir[1500], mark[1600], fin[1500], tdir[1500], p[1700];
   char ini[2000], msg[1400];
   DWORD we;
   int rc = -1;

   code[0] = err[0] = '\0';
   {
      /* test hook: XMB_SLOW=ms paces writes so a test can pull the "cable" */
      char sl[16];
      if (GetEnvironmentVariableA("XMB_SLOW", sl, sizeof(sl)))
         Sleep((DWORD)atoi(sl));
   }
   if (psp_find(jv_str(args, "dev", ""), &d) != 0)
   {
      strcpy(code, "disconnected");
      snprintf(err, cap, "The PSP was disconnected. Reconnect it in USB mode and run the queue again; nothing was half-written.");
      return -1;
   }
   if (!d.install[0])
   {
      strcpy(code, "bad");
      snprintf(err, cap, "GBAdhoc is not installed on this storage, so an entry would have nothing to open.");
      return -1;
   }
   if (!valid_folder(folder) || !title[0] || !rom[0])
   {
      strcpy(code, "bad");
      snprintf(err, cap, "An entry needs a title, a ROM and a folder name of letters, digits, - and _.");
      return -1;
   }
   /* the launcher and GBAdhoc hold these in 320-byte buffers (PATH_CAP,
    * PSP_ROM_REL_PATH_CAP) with GBAdhoc's own path in front: keep clear */
   if (strlen(rom) > 255 || strlen(gb[0] ? gb : d.install) > 200)
   {
      strcpy(code, "bad");
      snprintf(err, cap, "The ROM's path is too long for the PSP (%u characters; 255 at most). "
                         "Move it to a shorter folder or rename it, then rescan.", (unsigned)strlen(rom));
      return -1;
   }
   if (jv_str(args, "icon", NULL) && u_b64dec(jv_str(args, "icon", ""), &icon) != 0)
      goto oom;
   if (jv_str(args, "pic", NULL) && u_b64dec(jv_str(args, "pic", ""), &pic) != 0)
      goto oom;

   memset(&e, 0, sizeof(e));
   e.template_pbp = xw_launcher_pbp;
   e.template_len = xw_launcher_pbp_len;
   e.title = title;
   e.rom = rom;
   e.gbadhoc = gb[0] ? gb : d.install;
   e.root = d.root;
   e.folder = folder;
   if (xw_build_pbp(&e, icon.len ? &icon : NULL, pic.len ? &pic : NULL, &pbp, err, cap) != 0)
   {
      strcpy(code, "bad");
      goto out;
   }
   xw_launch_ini(&e, ini, sizeof(ini));

   u_join(fin, sizeof(fin), d.root, "PSP/GAME");
   u_join(fin, sizeof(fin), fin, folder);
   if (u_is_dir(fin) && !replace)
   {
      strcpy(code, "exists");
      snprintf(err, cap, "An entry called \"%s\" already exists in PSP/GAME.", folder);
      goto out;
   }
   if (u_is_dir(fin))
   {
      /* never replace something that is not an entry (another homebrew) */
      u_join(p, sizeof(p), fin, "launch.ini");
      if (!u_is_file(p))
      {
         strcpy(code, "exists");
         snprintf(err, cap, "PSP/GAME/%s is another program's folder, not a GBAdhoc entry. Choose another name.", folder);
         goto out;
      }
   }
   if (d.free_mb * 1048576.0 < (double)pbp.len + 256 * 1024)
   {
      strcpy(code, "nospace");
      snprintf(err, cap, "Not enough space on the %s: an entry needs about %u KB and %.1f MB is free.",
               d.label[0] ? d.label : "Memory Stick", (unsigned)(pbp.len / 1024 + 64), d.free_mb);
      goto out;
   }

   u_join(inst, sizeof(inst), d.root, d.install);
   u_join(stage, sizeof(stage), inst, "xmb/stage");
   u_join(sdir, sizeof(sdir), stage, folder);
   snprintf(mark, sizeof(mark), "%s.ready", sdir);
   u_join(tdir, sizeof(tdir), inst, "xmb/trash");
   u_join(tdir, sizeof(tdir), tdir, folder);

   snprintf(msg, sizeof(msg), "Making PSP/GAME/%s/", folder);
   step(cb, ctx, msg, -1, 0);
   u_rmtree(sdir);
   {
      wchar_t *w = u_wide(mark);
      DeleteFileW(w);
      free(w);
   }
   if (u_mkdirs(sdir) != 0)
   {
      we = GetLastError();
      strcpy(code, classify(&d, we));
      snprintf(err, cap, !strcmp(code, "disconnected")
                            ? "The PSP was disconnected. Reconnect it in USB mode and run the queue again; nothing was half-written."
                            : "Cannot create a folder on the PSP (error %lu).", we);
      goto out;
   }
   snprintf(msg, sizeof(msg), "Writing EBOOT.PBP (%u KB: launcher, PARAM.SFO, ICON0%s)",
            (unsigned)((pbp.len + 1023) / 1024), pic.len ? ", PIC1" : "");
   step(cb, ctx, msg, -1, 0);
   u_join(p, sizeof(p), sdir, "EBOOT.PBP");
   we = u_write_durable(p, pbp.data, pbp.len);
   if (!we && fault("eboot"))
      we = ERROR_DEVICE_NOT_CONNECTED;
   if (we)
      goto ioerr;
   snprintf(msg, sizeof(msg), "Writing launch.ini → rom = %s", rom);
   step(cb, ctx, msg, -1, 0);
   u_join(p, sizeof(p), sdir, "launch.ini");
   we = u_write_durable(p, ini, strlen(ini));
   if (we)
      goto ioerr;
   step(cb, ctx, "Checking what was written", -1, 0);
   u_join(p, sizeof(p), sdir, "EBOOT.PBP");
   if (xw_read_file(p, &back) != 0 || back.len != pbp.len ||
       xw_crc32(0, back.data, back.len) != xw_crc32(0, pbp.data, pbp.len))
   {
      we = GetLastError();
      if (root_gone(&d))
         goto ioerr;
      strcpy(code, "io");
      snprintf(err, cap, "EBOOT.PBP did not read back the same; the stick may be failing. Nothing was moved into PSP/GAME.");
      goto out;
   }
   we = u_write_durable(mark, "ready\n", 6);
   if (we)
      goto ioerr;
   if (fault("move"))
   {
      we = ERROR_DEVICE_NOT_CONNECTED;
      goto ioerr;
   }
   /* the swap: old entry aside, new one in, old one gone */
   if (u_is_dir(fin))
   {
      u_rmtree(tdir);
      u_mkdirs(tdir);
      {
         wchar_t *w = u_wide(tdir);
         RemoveDirectoryW(w);
         free(w);
      }
      if ((we = (DWORD)u_move(fin, tdir)) != 0)
         goto ioerr;
   }
   if ((we = (DWORD)u_move(sdir, fin)) != 0)
      goto ioerr;
   {
      wchar_t *w = u_wide(mark);
      DeleteFileW(w);
      free(w);
   }
   u_rmtree(tdir);
   {
      /* leave no empty working folders behind (these only go if empty) */
      char xd[1400];
      wchar_t *w;
      u_join(xd, sizeof(xd), inst, "xmb/trash");
      w = u_wide(xd); RemoveDirectoryW(w); free(w);
      w = u_wide(stage); RemoveDirectoryW(w); free(w);
      u_join(xd, sizeof(xd), inst, "xmb");
      w = u_wide(xd); RemoveDirectoryW(w); free(w);
   }
   /* an update under a new folder name: the old entry goes */
   if (old[0] && strcmp(old, folder) && valid_folder(old))
   {
      char of[1500], oi[1600];
      u_join(of, sizeof(of), d.root, "PSP/GAME");
      u_join(of, sizeof(of), of, old);
      u_join(oi, sizeof(oi), of, "launch.ini");
      if (u_is_file(oi))
         u_rmtree(of);
   }
   /* the art it used, kept in GBAdhoc's own folders (if they have none) */
   {
      const jv *keep = jv_get(args, "keep");
      int i, kept = 0;
      for (i = 0; i < jv_len(keep); i++)
      {
         const jv *k = jv_at(keep, i);
         char dst[1300];
         const char *rel = jv_str(k, "dst", "");
         if (!rel[0] || strstr(rel, ".."))
            continue;
         u_join(dst, sizeof(dst), inst, rel);
         if (copy_if_missing(jv_str(k, "src", ""), dst) > 0)
            kept++;
      }
      if (kept)
      {
         snprintf(msg, sizeof(msg), "Kept the downloaded art in GBAdhoc's %s", kept > 1 ? "boxart and hero folders" : "art folders");
         step(cb, ctx, msg, -1, 0);
      }
   }
   sb_add(out, "{");
   sb_kv_str(out, "folder", folder);
   sb_kv_str(out, "dir", fin);
   sb_kv_num(out, "kb", (double)(pbp.len / 1024));
   sb_add(out, "}");
   u_log("made %s (%u bytes) rom=%s", fin, (unsigned)pbp.len, rom);
   rc = 0;
   goto out;
ioerr:
   strcpy(code, classify(&d, we));
   if (!strcmp(code, "disconnected"))
      snprintf(err, cap, "The PSP was disconnected. Reconnect it in USB mode and run the queue again; nothing was half-written.");
   else if (!strcmp(code, "nospace"))
      snprintf(err, cap, "The %s filled up while writing. Nothing was moved into PSP/GAME.", d.label[0] ? d.label : "Memory Stick");
   else
      snprintf(err, cap, "Writing to the PSP failed (Windows error %lu). Nothing was moved into PSP/GAME.", we);
   u_log("make %s failed: %s (%lu)", folder, code, we);
   goto out;
oom:
   strcpy(code, "bad");
   snprintf(err, cap, "Out of memory.");
out:
   xw_buf_free(&icon);
   xw_buf_free(&pic);
   xw_buf_free(&pbp);
   xw_buf_free(&back);
   return rc;
}

int psp_remove(const jv *args, sb *out, char *code, char *err, size_t cap)
{
   psp_dev d;
   const char *folder = jv_str(args, "folder", "");
   char fin[1500], ini[1600], inst[1300], tdir[1500];
   code[0] = '\0';
   if (psp_find(jv_str(args, "dev", ""), &d) != 0)
   {
      strcpy(code, "disconnected");
      snprintf(err, cap, "The PSP is not connected.");
      return -1;
   }
   if (!valid_folder(folder))
   {
      strcpy(code, "bad");
      snprintf(err, cap, "That is not an entry's folder.");
      return -1;
   }
   u_join(fin, sizeof(fin), d.root, "PSP/GAME");
   u_join(fin, sizeof(fin), fin, folder);
   u_join(ini, sizeof(ini), fin, "launch.ini");
   if (!u_is_file(ini))
   {
      strcpy(code, "bad");
      snprintf(err, cap, "PSP/GAME/%s is not a GBAdhoc entry, so it was left alone.", folder);
      return -1;
   }
   /* out of PSP/GAME in one rename, then deleted */
   u_join(inst, sizeof(inst), d.root, d.install[0] ? d.install : "PSP/GAME/GBAdhoc");
   u_join(tdir, sizeof(tdir), inst, "xmb/trash");
   u_mkdirs(tdir);
   u_join(tdir, sizeof(tdir), tdir, folder);
   u_rmtree(tdir);
   if (d.install[0] && u_move(fin, tdir) == 0)
      u_rmtree(tdir);
   else if (u_rmtree(fin) != 0)
   {
      strcpy(code, root_gone(&d) ? "disconnected" : "io");
      snprintf(err, cap, "The entry could not be removed.");
      return -1;
   }
   sb_add(out, "{");
   sb_kv_str(out, "folder", folder);
   sb_add(out, "}");
   u_log("removed %s", fin);
   return 0;
}

/* ---- pictures and folders ----------------------------------------------- */

static void list_images(const char *dir, sb *out, int *n, int cap)
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
      char *name, p[1300], url[2800];
      const char *e;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
         continue;
      name = u_utf8(fd.cFileName);
      if (!name)
         continue;
      e = u_ext(name);
      if (u_ieq(e, "png") || u_ieq(e, "jpg") || u_ieq(e, "jpeg") || u_ieq(e, "bmp"))
      {
         u_join(p, sizeof(p), dir, name);
         file_url(p, url, sizeof(url));
         if ((*n)++)
            sb_add(out, ",");
         sb_add(out, "{");
         sb_kv_str(out, "name", name);
         sb_kv_str(out, "path", p);
         sb_kv_str(out, "url", url);
         sb_add(out, "}");
      }
      free(name);
   } while (*n < cap && FindNextFileW(h, &fd));
   FindClose(h);
}

void psp_pictures(const jv *args, sb *out)
{
   psp_dev d;
   char p[1200];
   int n = 0;
   sb_add(out, "[");
   if (psp_find(jv_str(args, "dev", ""), &d) == 0)
   {
      u_join(p, sizeof(p), d.root, "PICTURE");
      list_images(p, out, &n, 24);
      u_join(p, sizeof(p), d.root, "PSP/PICTURE");
      list_images(p, out, &n, 24);
   }
   sb_add(out, "]");
}

/* The owner's pictures in the PSP app's folder, PSP/GAME/GBAdhocXMB/custom:
 * <ROM stem>.png is a game's icon, <ROM stem>.bg.png its background (the
 * PSP app reads the same folder, so either side makes the same entry). */
void psp_custom(const jv *args, sb *out)
{
   psp_dev d;
   char p[1200];
   int n = 0;
   sb_add(out, "[");
   if (psp_find(jv_str(args, "dev", ""), &d) == 0)
   {
      u_join(p, sizeof(p), d.root, "PSP/GAME/GBAdhocXMB/custom");
      list_images(p, out, &n, 256);
   }
   sb_add(out, "]");
}

void psp_dirs(const jv *args, sb *out)
{
   psp_dev d;
   int n = 0;
   sb_add(out, "[");
   if (psp_find(jv_str(args, "dev", ""), &d) == 0)
   {
      char pat[1200];
      wchar_t *w;
      WIN32_FIND_DATAW fd;
      HANDLE h;
      u_join(pat, sizeof(pat), d.root, "*");
      w = u_wide(pat);
      h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
      free(w);
      if (h != INVALID_HANDLE_VALUE)
      {
         do
         {
            char *name;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' ||
                (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
               continue;
            name = u_utf8(fd.cFileName);
            if (!name)
               continue;
            if (!u_ieq(name, "PSP") && !u_ieq(name, "MP_ROOT") && !u_ieq(name, "MUSIC") &&
                !u_ieq(name, "VIDEO") && !u_ieq(name, "ISO"))
            {
               if (n++)
                  sb_add(out, ",");
               sb_jstr(out, name);
            }
            free(name);
         } while (FindNextFileW(h, &fd));
         FindClose(h);
      }
      if (d.install[0])
      {
         char roms[1300];
         u_join(roms, sizeof(roms), d.root, d.install);
         u_join(roms, sizeof(roms), roms, "roms");
         u_join(pat, sizeof(pat), roms, "*");
         w = u_wide(pat);
         h = w ? FindFirstFileW(w, &fd) : INVALID_HANDLE_VALUE;
         free(w);
         if (h != INVALID_HANDLE_VALUE)
         {
            do
            {
               char *name, rel[600];
               if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
                  continue;
               name = u_utf8(fd.cFileName);
               if (!name)
                  continue;
               snprintf(rel, sizeof(rel), "%s/roms/%s", d.install, name);
               if (n++)
                  sb_add(out, ",");
               sb_jstr(out, rel);
               free(name);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
         }
      }
   }
   sb_add(out, "]");
}
