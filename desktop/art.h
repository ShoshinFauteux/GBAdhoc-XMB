/* art.h -- recognising ROMs and finding their art for GBAdhoc XMB.
 *
 * No-Intro DATs come from libretro-database; box art from
 * libretro-thumbnails (Named_Boxarts); hero art from GBAdhoc-heroart.  All
 * three are cached under %LOCALAPPDATA%/GBAdhoc XMB/cache, with ETags, so
 * GitHub's API is asked a handful of times a week, not once per game. */
#ifndef XMB_ART_H
#define XMB_ART_H

#include "json.h"
#include "psp.h"

typedef struct
{
   char sys[4];           /* GBA, GBC, GB */
   char nointro[200];     /* the No-Intro name matched ("" = none) */
   char title[160];       /* the XMB title suggested */
   char base[200];        /* for a hack: the No-Intro game it is built on */
   char how[8];           /* crc, name, serial, fuzzy, hack, none */
   int recognised;
   int confidence;
   int hack;
   char code[8];          /* GBA game code */
} art_ident;

void art_init(void);
/* Get the DATs for these systems ready (cache, else download).  Returns
 * how many are available. */
int art_prepare_dats(int want_gba, int want_gbc, int want_gb, psp_prog cb, void *ctx);
void art_identify(const char *path, long long size, art_ident *id);

/* Look up box art and hero art for one game; JSON result into `out`. */
int art_fetch(const jv *args, psp_prog cb, void *ctx, sb *out);

void art_cache_dir(char *out, size_t cap);
void art_cache_stats(sb *out);
void art_cache_clear(void);
void art_net_status(sb *out);

#endif
