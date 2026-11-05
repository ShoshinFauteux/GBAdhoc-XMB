/* theme.c -- the four themes; values from desktop/ui/brand.css (the design's
 * tokens, builds of the designer's mockup), see theme.h. */
#include "theme.h"

#include <string.h>
#include <strings.h>

const theme_t g_themes[TH_COUNT] = {
   /* Graphite (the default): neutral graphite, white focus, the logo's amber accent;
    * warn moves to orange so it is never the accent */
   { "graphite", "Graphite",
     0xFF0A0A0Bu, 0xFFF1F1F0u, 0xFFC8C8C6u, 0xFFA1A19Fu, 0xFF858589u,
     0xFFA1A19Fu,
     0x17FFFFFFu, 0x2EFFFFFFu,
     0xFFF5C518u, 0xFF141004u, 0xFFF7D45Cu, 0xFFFFE596u, 0x24F5C518u,
     0x1AFFFFFFu, 0xFFFFFFFFu, 0xFF0A0A0Bu,
     0xFF8FC1EEu,
     0xFF8AC32Au, 0xFFF2842Bu, 0xFFEF3F6Du, 0xFF1FB5B0u,
     0xFFB7EA5Cu, 0xFFFFB36Bu, 0xFFFFD3A8u, 0xFFFF8AA8u, 0xFFC8F3F1u,
     0x24F2842Bu, 0x80F2842Bu, 0x241FB5B0u, 0x731FB5B0u,
     0xF50E0E0Fu,
     { 0xFF2A2A2Eu, 0xFF171719u, 0xFF0D0D0Eu },
     0xFFFFFFu, 0.07f,
     0xFF28282Cu, 0xFF161618u,
     0xFF141415u,
     0xFF303034u, 0xFF19191Bu,
     0xFF0E1A3Au, { 0xFF2E63C9u, 0xFF163A8Au, 0xFF081433u },
     216, 28, 36, 50 },
   /* Slate: cool slate greys, a steel-blue accent, the logo's status colours */
   { "slate", "Slate",
     0xFF0B0E13u, 0xFFEEF1F5u, 0xFFC4CBD5u, 0xFF9AA4B2u, 0xFF7F8998u,
     0xFF9AA4B2u,
     0x17FFFFFFu, 0x2BFFFFFFu,
     0xFF5B94D6u, 0xFF0B0E13u, 0xFF9CC4EFu, 0xFFBCD6F3u, 0x2E5B94D6u,
     0x335B94D6u, 0xFF5B94D6u, 0xFFFFFFFFu,
     0xFF8FC1EEu,
     0xFF8AC32Au, 0xFFF4BB20u, 0xFFEF3F6Du, 0xFF1FB5B0u,
     0xFFB7EA5Cu, 0xFFFFD45Au, 0xFFFFE3A0u, 0xFFFF8AA8u, 0xFFC8F3F1u,
     0x24F4BB20u, 0x80F4BB20u, 0x241FB5B0u, 0x731FB5B0u,
     0xF50D1117u,
     { 0xFF28313Du, 0xFF171C24u, 0xFF0F1319u },
     0x5B94D6u, 0.14f,
     0xFF242C36u, 0xFF161B22u,
     0xFF141A21u,
     0xFF2A3340u, 0xFF171C24u,
     0xFF0E1A3Au, { 0xFF2E63C9u, 0xFF163A8Au, 0xFF081433u },
     214, 48, 40, 60 },
   /* Teal (Slate + logo): the slate ground, the logo's teal accent; info moves to steel blue */
   { "teal", "Teal",
     0xFF0B0E13u, 0xFFEEF1F5u, 0xFFC4CBD5u, 0xFF9AA4B2u, 0xFF7F8998u,
     0xFF9AA4B2u,
     0x17FFFFFFu, 0x2BFFFFFFu,
     0xFF1FB8B0u, 0xFF06201Eu, 0xFF7FE2DDu, 0xFFA8ECE8u, 0x291FB8B0u,
     0x2E1FB8B0u, 0xFF1FB8B0u, 0xFFFFFFFFu,
     0xFF8FC1EEu,
     0xFF8AC32Au, 0xFFF4BB20u, 0xFFEF3F6Du, 0xFF5B94D6u,
     0xFFB7EA5Cu, 0xFFFFD45Au, 0xFFFFE3A0u, 0xFFFF8AA8u, 0xFFCFE1F6u,
     0x24F4BB20u, 0x80F4BB20u, 0x245B94D6u, 0x735B94D6u,
     0xF50D1117u,
     { 0xFF28313Du, 0xFF171C24u, 0xFF0F1319u },
     0x1FB8B0u, 0.12f,
     0xFF242C36u, 0xFF161B22u,
     0xFF141A21u,
     0xFF2A3340u, 0xFF171C24u,
     0xFF0E1A3Au, { 0xFF2E63C9u, 0xFF163A8Au, 0xFF081433u },
     202, 46, 38, 50 },
   /* Classic: 0.1.0-beta.1's deep indigo and violet, exactly */
   { "classic", "Classic",
     0xFF0A0C1Eu, 0xFFF2F1ECu, 0xFFC3C5D8u, 0xFF8E92B2u, 0xFF5D6188u,
     0xFF5D6188u,
     0x1AFFFFFFu, 0x2EFFFFFFu,
     0xFF8D7BF3u, 0xFF0C0B20u, 0xFFC9C0FFu, 0xFFC9C0FFu, 0x248D7BF3u,
     0x388D7BF3u, 0xFF8D7BF3u, 0xFFFFFFFFu,
     0xFFA99CFFu,
     0xFF8AC32Au, 0xFFF4BB20u, 0xFFEF3F6Du, 0xFF1FB5B0u,
     0xFFB7EA5Cu, 0xFFFFD45Au, 0xFFFFE3A0u, 0xFFFF8AA8u, 0xFFC8F3F1u,
     0x24F4BB20u, 0x80F4BB20u, 0x241FB5B0u, 0x731FB5B0u,
     0xF50A0C1Eu,
     { 0xFF262266u, 0xFF141740u, 0xFF0C0E26u },
     0x8D7BF3u, 0.28f,
     0xFF1F2352u, 0xFF11132Fu,
     0xFF11143Au,
     0xFF2A2F5Eu, 0xFF161936u,
     0xFF15153Au, { 0xFF4A3AB0u, 0xFF1E1A6Eu, 0xFF0B0A2Au },
     270, 62, 44, 110 },
};

int g_theme = TH_GRAPHITE;
const theme_t *g_th = &g_themes[TH_GRAPHITE];

int theme_set(int i)
{
   if (i < 0 || i >= TH_COUNT)
      i = TH_GRAPHITE;
   g_theme = i;
   g_th = &g_themes[i];
   return i;
}

int theme_find(const char *id)
{
   int i;
   for (i = 0; i < TH_COUNT; i++)
      if (strcasecmp(id, g_themes[i].id) == 0)
         return i;
   return -1;
}
