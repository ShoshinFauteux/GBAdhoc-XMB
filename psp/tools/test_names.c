/* test_names.c -- host check of the ROM matcher (names.c).
 *   cc -O2 -I.. -o test_names test_names.c ../names.c -lz && ./test_names */
#include <stdio.h>
#include <string.h>
#include "../names.h"

static const struct { const char *stem; int sys; int want_rec; const char *want; } T[] = {
   { "Pokemon - Emerald Version (USA, Europe)", SYS_GBA, 1, "Pokemon - Emerald Version (USA, Europe)" },
   { "Kirby & The Amazing Mirror (USA)", SYS_GBA, 1, "Kirby _ The Amazing Mirror (USA)" },
   { "Mario Kart - Super Circuit (USA)", SYS_GBA, 1, "Mario Kart - Super Circuit (USA)" },
   { "Golden Sun (USA, Europe)", SYS_GBA, 1, "Golden Sun (USA, Europe)" },
   { "Pokemon - Crystal Version (USA, Europe) (Rev 1)", SYS_GBC, 1, NULL },
   { "Tetris (World) (Rev 1)", SYS_GB, 1, NULL },
   { "Pokemon Emerald", SYS_GBA, 1, NULL },
   { "pokemon_emerald", SYS_GBA, 1, NULL },
   { "Advance Wars 2", SYS_GBA, 1, NULL },
   { "Pokemon MonoMyth v1.2", SYS_GBA, 0, NULL },
   { "Fire Emblem - The Morrow's Golden Country", SYS_GBA, 0, NULL },
   { "mygame_final2 (1)", SYS_GBA, 0, NULL },
   { "Legend of Zelda, The - The Minish Cap (USA)", SYS_GBA, 1, NULL },
   { "Pokemon - Unbound", SYS_GBA, -1, NULL },
};

int main(void)
{
   unsigned i;
   int bad = 0;
   if (names_init() != 0) { puts("init failed"); return 2; }
   printf("%d names\n", names_count());
   for (i = 0; i < sizeof(T) / sizeof(T[0]); i++)
   {
      name_match m;
      char st[128];
      int j;
      names_match(T[i].stem, T[i].sys, 0, &m);
      st[0] = 0;
      if (m.nointro) names_short_title(m.nointro, st, sizeof(st));
      printf("%-46s rec=%d conf=%3d hack=%d ni=[%s] short=[%s] hero=[%s]\n", T[i].stem, m.recognised, m.conf, m.hack,
             m.nointro ? m.nointro : "-", st, m.hero ? m.hero : "-");
      for (j = 0; j < m.nbox; j++)
         printf("      box %3d%s %s\n", m.box[j].conf, m.box[j].below ? " below" : "", m.box[j].name);
      if (T[i].want_rec >= 0 && m.recognised != T[i].want_rec) { printf("  ^^ FAIL recognised\n"); bad++; }
      if (T[i].want && (!m.nointro || strcmp(m.nointro, T[i].want))) { printf("  ^^ FAIL name\n"); bad++; }
   }
   printf(bad ? "%d FAILED\n" : "all ok\n", bad);
   return bad != 0;
}
