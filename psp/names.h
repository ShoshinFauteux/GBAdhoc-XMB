/* names.h -- recognising a ROM by its file name (gen/xmb_names.h).
 *
 * The names are libretro-thumbnails' Named_Boxarts file names -- No-Intro
 * titles with & * / : ` < > ? \ | " written as '_' -- for GBA, GB and GBC, plus the
 * names hero art exists for in GBAdhoc-heroart.  So a match IS the URL of
 * the art: nothing else is needed to look it up.
 *
 * Matching, most certain first:
 *   1. the file's stem is a name exactly                         100 %
 *   2. same title once tags, case, punctuation and accents are
 *      folded ("pokemon emerald version")                         96-100 %
 *   3. word overlap, the generic words weighted down, a number
 *      that differs ("Advance Wars 2") costing a lot              its score
 * A title counts as recognised at the match line (80 %) or above.  Box-art
 * candidates are that title's names in every region, the ROM's own region
 * (or the preferred one) first. */
#ifndef XMBAPP_NAMES_H
#define XMBAPP_NAMES_H

enum { SYS_GBA = 0, SYS_GB = 1, SYS_GBC = 2 };
#define MATCH_LINE 80
#define NAMES_MAXCAND 6

typedef struct
{
   const char *name;     /* Named_Boxarts name (file-safe) */
   int conf;
   int below;            /* under the match line: shown, never picked */
   char sys;             /* the libretro system it is under: A GBA, B GB, C GBC */
} name_cand;

typedef struct
{
   int recognised;       /* conf >= MATCH_LINE (No-Intro) or hero art matched */
   int conf;
   int hack;             /* recognised by the hero repo only */
   const char *nointro;  /* best No-Intro name, or NULL */
   const char *hero;     /* hero repo name, or NULL */
   name_cand box[NAMES_MAXCAND];
   int nbox;
} name_match;

int names_init(void);                 /* inflate once; 0 = ok */
int names_count(void);
/* region preference: 0 USA/Europe first, 1 Europe first, 2 Japan first */
void names_match(const char *rom_stem, int sys, int region_pref, name_match *m);
/* "Pokemon - Emerald Version (USA, Europe)" -> "Pokemon Emerald"; '_' back to '&' */
void names_short_title(const char *name, char *out, unsigned cap);
/* the title part with tags cut and '_' back to '&' */
void names_plain_title(const char *name, char *out, unsigned cap);
/* the first tag group's text, "USA, Europe" ("" if none) */
void names_region(const char *name, char *out, unsigned cap);

#endif
