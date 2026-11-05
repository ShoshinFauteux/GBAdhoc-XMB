/* test_match.c -- xw_match's unit test (tools/test_core.sh).  No real games: a
 * three-game DAT written here, and headers made in memory. */
#include "xw_match.h"
#include <stdio.h>
#include <string.h>

static int fails;

#define EQ(got, want)                                                                \
   do {                                                                             \
      if (strcmp((got), (want)) != 0) {                                             \
         printf("FAIL %s:%d: %s -> \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got,  \
                (got), (want));                                                     \
         fails++;                                                                   \
      }                                                                             \
   } while (0)
#define IS(cond)                                                                     \
   do {                                                                             \
      if (!(cond)) {                                                                \
         printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
         fails++;                                                                   \
      }                                                                             \
   } while (0)

static const char dat[] =
   "clrmamepro (\n\tname \"Nintendo - Game Boy Advance\"\n)\n\n"
   "game (\n\tname \"Pokemon - Emerald Version (USA, Europe)\"\n\tregion \"USA\"\n\tserial \"BPEE\"\n"
   "\trom ( name \"Pokemon - Emerald Version (USA, Europe).gba\" size 16777216 crc 1F1C08FB md5 0 serial \"BPEE\" )\n)\n"
   "game (\n\tname \"Fire Emblem - The Sacred Stones (USA) (Virtual Console)\"\n\tserial \"BE8E\"\n"
   "\trom ( name \"x.gba\" size 16777216 crc 11111111 serial \"BE8E\" )\n)\n"
   "game (\n\tname \"Fire Emblem - The Sacred Stones (USA)\"\n\tserial \"BE8E\"\n"
   "\trom ( name \"y.gba\" size 16777216 crc A47246AE serial \"BE8E\" )\n)\n"
   "game (\n\tname \"Legend of Zelda, The - The Minish Cap (USA)\"\n\tserial \"BZME\"\n"
   "\trom ( name \"z.gba\" size 16777216 crc B4BD50E4 serial \"BZME\" )\n)\n";

static void gba_head(unsigned char *h, const char *code)
{
   memset(h, 0, 0x150);
   memcpy(h + 0xA0, "TESTGAME", 8);
   memcpy(h + 0xAC, code, 4);
   h[0xB2] = 0x96;
}

int main(void)
{
   char o[256];
   xw_dat d = { 0, 0, 0 };
   xw_rom_header h;
   xw_match_res r;
   unsigned char head[0x150];

   xw_nointro_display("Pokemon - Emerald Version (USA, Europe)", o, sizeof(o));
   EQ(o, "Pokemon Emerald");
   xw_nointro_display("Mario Kart - Super Circuit (USA)", o, sizeof(o));
   EQ(o, "Mario Kart: Super Circuit");
   xw_nointro_display("Legend of Zelda, The - The Minish Cap (USA)", o, sizeof(o));
   EQ(o, "The Legend of Zelda: The Minish Cap");
   xw_nointro_display("Kirby & The Amazing Mirror (USA)", o, sizeof(o));
   EQ(o, "Kirby & The Amazing Mirror");
   xw_nointro_base("Tetris (World) (Rev 1)", o, sizeof(o));
   EQ(o, "Tetris");
   xw_nointro_region("Tetris (World) (Rev 1)", o, sizeof(o));
   EQ(o, "World");
   xw_thumb_name("Mega Microgame$! & Co: A/B?", o, sizeof(o));
   EQ(o, "Mega Microgame$! _ Co_ A_B_");
   xw_norm_key("Pok\xc3\xa9mon - Morrow's  Gold & Silver", o, sizeof(o));
   EQ(o, "pokemon morrows gold and silver");
   IS(xw_name_score("Golden Sun (USA, Europe)", "Golden Sun (Japan)") == 100);
   IS(xw_name_score("Golden Sun", "Golden Sun - The Lost Age") < 80);

   IS(xw_dat_parse(dat, sizeof(dat) - 1, &d) == 0);
   IS(d.n == 4);
   IS(d.n == 4 && !strcmp(d.g[0].serial, "BPEE") && d.g[0].crc == 0x1F1C08FBul && d.g[0].size == 16777216ul);

   gba_head(head, "BPEE");
   xw_rom_header_parse(head, sizeof(head), "gba", &h);
   IS(h.valid && !strcmp(h.code, "BPEE") && !strcmp(h.sys, "GBA"));
   /* by name */
   xw_dat_match(&d, "Pokemon - Emerald Version (USA, Europe)", &h, 0, 0, &r);
   IS(r.how == XW_MATCH_NAME && r.idx == 0 && r.score == 100);
   /* renamed: the game code finds it, a checksum is asked for */
   xw_dat_match(&d, "emerald", &h, 0, 0, &r);
   IS(r.how == XW_MATCH_SERIAL && r.need_crc);
   xw_dat_match(&d, "emerald", &h, 1, 0x1F1C08FBul, &r);
   IS(r.how == XW_MATCH_CRC && r.idx == 0);
   /* same code, not the dump, not the name: a hack, of the plain release */
   gba_head(head, "BE8E");
   xw_rom_header_parse(head, sizeof(head), "gba", &h);
   xw_dat_match(&d, "Fire Emblem - The Morrow's Golden Country", &h, 1, 0x12345678ul, &r);
   IS(r.how == XW_MATCH_HACK && r.idx == 2);
   /* nothing like it */
   gba_head(head, "ZQZQ");
   xw_rom_header_parse(head, sizeof(head), "gba", &h);
   xw_dat_match(&d, "mygame_final2 (1)", &h, 0, 0, &r);
   IS(r.how == XW_MATCH_NONE);
   /* close enough by name */
   xw_dat_match(&d, "Legend of Zelda, The - The Minish Cap (U)", &h, 0, 0, &r);
   IS(r.how == XW_MATCH_FUZZY && r.idx == 3);

   xw_dat_free(&d);
   printf(fails ? "xw_match: %d FAILED\n" : "ok: xw_match (%d)\n", fails);
   return fails != 0;
}
