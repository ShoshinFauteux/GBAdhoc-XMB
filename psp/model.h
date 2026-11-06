/* model.h -- GBAdhoc XMB's library: the ROMs found, what they were
 * recognised as, the art chosen for each, and the XMB entries on the card.
 *
 * Threads: the UI thread owns every field below; the worker (jobs.c) writes
 * only the fields marked [W], and publishes them by setting a state field
 * last.  Images handed to the UI are RGBA buffers the UI wraps as textures. */
#ifndef XMBAPP_MODEL_H
#define XMBAPP_MODEL_H

#include <stdint.h>
#include "names.h"

#define MAX_GAMES 600
#define MAX_ENTRIES 400
#define MAX_FOLDERS 16
#define PATHCAP 256
#define MAX_CUSTOM 256

enum { ART_NONE = 0, ART_OWN, ART_NET, ART_PIC, ART_ENTRY, ART_CUSTOM };
/* ICON0 layouts (art.c): box art over a blurred, darkened copy of the hero or
 * itself; box art left + title right; filled, anchored at the top (title)
 * band; filled with a mild horizontal stretch; fitted on transparency */
enum { FIT_TILE = 0, FIT_SIDE, FIT_SMART, FIT_STRETCH, FIT_FIT, FIT_COUNT };
const char *fit_name(int fit);           /* "Blur frame" */
enum { IMG_NONE = 0, IMG_LOADING, IMG_READY, IMG_FAIL };
enum { FETCH_NONE = 0, FETCH_OK, FETCH_OFFLINE, FETCH_RATE, FETCH_NOTFOUND, FETCH_FAIL };

typedef struct
{
   int kind;
   char path[PATHCAP];       /* the image file (own / net cache / picture) */
   char label[96];           /* "USA, Europe", "IMG_0412.JPG" */
   int conf;
} art_choice;

typedef struct
{
   char name[160];           /* Named_Boxarts name */
   char region[64];
   int conf, below;
   char cache[PATHCAP];      /* downloaded file, "" until fetched */
   char sys;                 /* libretro system: A GBA, B GB, C GBC */
} box_cand;

typedef struct
{
   int state;                /* IMG_* */
   uint32_t *px;             /* w x h RGBA, stride w */
   int w, h;
   int ver;                  /* the choice version it was made from */
} img_slot;

typedef struct game
{
   char path[PATHCAP];       /* where the ROM is, "ms0:/PSP/GAME/GBAdhoc/roms/x.gba" */
   char rel[PATHCAP];        /* as launch.ini names it (relative to roms/, or absolute) */
   char file[160];           /* shown file name */
   char stem[160];
   char folder[96];          /* "roms", "roms/racing", "ROMS/GBA" */
   int sys;
   long long size;
   name_match m;
   char nointro_title[128];  /* the No-Intro title without tags */
   char short_title[128];
   char title[128];          /* the entry's XMB title (edited) */
   char own_box[PATHCAP], own_hero[PATHCAP];
   art_choice icon, bg;
   int fit;                  /* FIT_* */
   char custom_msg[128];     /* why a custom/ picture of its was not used, "" */
   int ver;                  /* bumped on every art/title change */
   /* art lookup [W] */
   int fetch;                /* FETCH_* */
   int fetching;
   char fetch_msg[160];
   box_cand cand[NAMES_MAXCAND];
   int ncand;
   int hero_found;
   char hero_cache[PATHCAP];
   int marked;
   int entry;                /* index into g_entries, -1 none */
   /* images [W] */
   img_slot icon_img;        /* the ICON0 as it will be written, 144x80 */
} game;

typedef struct
{
   char dir[PATHCAP];        /* "ms0:/PSP/GAME/XMB_Pokemon_Emerald" */
   char folder[64];
   char title[128];
   char rom[PATHCAP];        /* launch.ini rom = */
   char gbadhoc[128];
   long made;                /* unix seconds (file time) */
   int game;                 /* -1: its ROM is not on the card */
   int rom_moved;
   int has_icon, has_pic;
} entry_t;

typedef struct
{
   char dev[8];              /* "ms0:" or "ef0:" -- where GBAdhoc is */
   char label[32];           /* "Memory Stick", "internal storage" */
   int is_go;
   char install[PATHCAP];    /* "ms0:/PSP/GAME/GBAdhoc" */
   char install_rel[96];     /* "PSP/GAME/GBAdhoc" */
   char version[48];         /* "4.5.0" from its PARAM.SFO */
   int found;
   char app_dir[PATHCAP];    /* this app's own folder */
   char folders[MAX_FOLDERS][PATHCAP];   /* extra folders to scan (absolute) */
   int nfolders;
   long long free_bytes;
} storage_t;

typedef struct
{
   int prefer_own, region, heroes;
   int theme;                /* TH_* (theme.h); ini: theme = mono|graphite|slate|teal|classic */
} prefs_t;

extern game g_games[MAX_GAMES];
extern int g_ngames;
extern entry_t g_entries[MAX_ENTRIES];
extern int g_nentries;
extern storage_t g_st;
extern prefs_t g_prefs;
extern int g_scan_ver;            /* bumped by every finished scan */

/* status of a game for chips, dots and filters */
enum { ST_UNKNOWN, ST_MOVED, ST_INXMB, ST_NOART, ST_READY, ST_MISSING };
int game_status(const game *g);
const char *status_label(int st);
uint32_t status_colour(int st);
int game_has_icon(const game *g);      /* an icon other than the title card */
const char *sys_name(int sys);         /* "Game Boy Advance" */
void art_label(const art_choice *a, char *out, unsigned cap);   /* "GBAdhoc's own", "Internet · USA" */
void game_touch(game *g);              /* art/title changed: icon is stale */

/* settings: <app dir>/xmbapp.ini */
void prefs_load(void);
void prefs_save(void);

/* The scan (runs on the worker).  progress(msg, sub, frac). */
typedef void (*scan_progress)(const char *msg, const char *sub, float frac);
int model_find_install(void);
void model_scan(scan_progress p);
void model_scan_entries(void);
void model_refresh_free(void);
/* default art choices for a game from what is on the card */
void model_default_art(game *g);

/* The owner's pictures in <app dir>/custom: <ROM stem>.png = icon,
 * <ROM stem>.bg.png = background.  model_scan_custom lists the folder
 * (making it when missing); model_custom_for finds a game's file. */
extern char g_custom[MAX_CUSTOM][128];
extern int g_ncustom;
void model_scan_custom(void);
int model_custom_for(const game *g, int bg, char *path, unsigned cap);

#endif
