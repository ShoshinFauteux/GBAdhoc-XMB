/* xw_match.h -- recognise a ROM by its No-Intro name (GBAdhoc XMB).
 * Platform-free C beside xw_core: the desktop app uses it, and the PSP app
 * can.  The data is a No-Intro DAT in clrmamepro form (libretro-database's
 * metadat/no-intro/ DATs), which names each game, its CRC-32 and, for the
 * GBA, its four-letter game code.  Art repositories (libretro-thumbnails,
 * GBAdhoc-heroart) name their files after the same No-Intro names. */
#ifndef XW_MATCH_H
#define XW_MATCH_H

#include <stddef.h>

typedef struct
{
   char name[200];       /* "Pokemon - Emerald Version (USA, Europe)" */
   char serial[8];       /* GBA game code "BPEE", or "" */
   unsigned long crc;
   unsigned long size;
} xw_dat_game;

typedef struct
{
   xw_dat_game *g;
   int n, cap;
} xw_dat;

/* Parse a clrmamepro DAT (appends to `dat`).  0 = ok. */
int xw_dat_parse(const char *text, size_t len, xw_dat *dat);
void xw_dat_free(xw_dat *dat);

/* Name helpers. */
/* The title without its tags: "Pokemon - Emerald Version (USA, Europe)"
 * -> "Pokemon - Emerald Version". */
void xw_nointro_base(const char *name, char *out, size_t cap);
/* The first tag group, "USA, Europe", or "". */
void xw_nointro_region(const char *name, char *out, size_t cap);
/* A title fit for the XMB: "Pokemon - Emerald Version (USA)" -> "Pokemon
 * Emerald", "Legend of Zelda, The - The Minish Cap" -> "The Legend of
 * Zelda: The Minish Cap", "Mario Kart - Super Circuit" -> "Mario Kart:
 * Super Circuit". */
void xw_nointro_display(const char *name, char *out, size_t cap);
/* libretro-thumbnails' file name for a No-Intro name: &*\/:`<>?\| -> _. */
void xw_thumb_name(const char *name, char *out, size_t cap);
/* A comparison key: lower case ASCII letters and digits, Latin-1 accents
 * folded ("Pokémon" -> "pokemon"), "&" -> "and", single spaces. */
void xw_norm_key(const char *s, char *out, size_t cap);
/* How alike two names are, 0..100 (on their keys, tags removed). */
int xw_name_score(const char *a, const char *b);

/* The cartridge header. */
typedef struct
{
   char sys[4];          /* "GBA", "GBC", "GB" */
   char title[17];       /* the header's internal name */
   char code[5];         /* GBA game code, "BPEE" */
   int valid;            /* 1 = the header checks out (GBA 0x96, GB logo) */
} xw_rom_header;

/* `head` is the ROM's first bytes (0x150 is enough for both); `ext` the
 * file's extension ("gba", "gbc", "gb") as a fallback. */
void xw_rom_header_parse(const unsigned char *head, size_t len,
                         const char *ext, xw_rom_header *h);

/* How a ROM was recognised. */
enum
{
   XW_MATCH_NONE = 0,
   XW_MATCH_CRC,       /* its checksum is a No-Intro dump: certain */
   XW_MATCH_NAME,      /* its file name is a No-Intro name */
   XW_MATCH_SERIAL,    /* its game code is one No-Intro game's */
   XW_MATCH_FUZZY,     /* its file name is close to a No-Intro title */
   XW_MATCH_HACK       /* game code of a No-Intro game but not its dump:
                          a ROM hack or a translation of that game */
};

typedef struct
{
   int idx;        /* into dat->g, or -1 */
   int score;      /* 0..100 */
   int how;        /* XW_MATCH_* */
   int need_crc;   /* 1 = a checksum would settle it: call again with one */
} xw_match_res;

/* Match a ROM: `stem` is its file name without extension.  Pass have_crc 0
 * first; if res->need_crc comes back 1, read the file and call again with
 * its CRC-32.  Returns res->idx. */
int xw_dat_match(const xw_dat *dat, const char *stem, const xw_rom_header *h,
                 int have_crc, unsigned long crc, xw_match_res *res);

#endif
