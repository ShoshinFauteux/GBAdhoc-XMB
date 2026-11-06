/* theme.h -- GBAdhoc XMB's colour themes (desktop/ui/brand.css as a C table).
 * Every colour a screen draws with comes from g_th; ui.h maps the C_* names
 * onto it.  Colours are 0xAARRGGBB.  Classic is 0.1.0-beta.1's palette, value
 * for value.  tools/test_themes.py checks every theme sets every field. */
#ifndef XMBAPP_THEME_H
#define XMBAPP_THEME_H

#include <stdint.h>

enum { TH_MONO, TH_GRAPHITE, TH_SLATE, TH_TEAL, TH_CLASSIC, TH_COUNT };

typedef struct
{
   const char *id, *name;
   /* ink and text */
   uint32_t ink0, fg, fg2, fg3, fg4;
   uint32_t fg_small;          /* 10-11 px decorative text (tab counts, L/R, chevrons) */
   uint32_t line, line2;
   /* the accent and focus */
   uint32_t accent, accent_ink, accent_text, accent_edge, accent_soft;
   uint32_t sel, focus, focus_in;   /* selected row; 2-ring focus: focus outside, focus_in inside */
   uint32_t cross;             /* the cross glyph in the hint bar */
   /* status */
   uint32_t ok, warn, bad, info;
   uint32_t ok_text, warn_text, warn_text2, bad_text, info_text2;
   uint32_t warn_soft, warn_line, info_soft, info_line;
   /* surfaces */
   uint32_t toast;
   uint32_t app_bg[3];         /* .app: the radial ground, 0 / 42 / 100 % */
   uint32_t glow_rgb;          /* the glow at the top right, and its alpha */
   float glow_a;
   uint32_t panel_a, panel_b;  /* dialogs: linear-gradient(160deg, a, b) */
   uint32_t strip_fade;        /* the filmstrip's edge fades */
   uint32_t empty_a, empty_b;  /* "None" art tiles */
   uint32_t xmb_bg, xmb_wave[3];   /* the XMB preview with no PIC1 */
   /* generated title cards: the GBA colour family */
   float card_hue, card_sat, card_light, card_spread;
} theme_t;

extern const theme_t g_themes[TH_COUNT];
extern const theme_t *g_th;
extern int g_theme;            /* index of g_th */

/* select a theme by index; returns the index used */
int theme_set(int i);
/* "mono" -> TH_MONO; -1 if unknown */
int theme_find(const char *id);

#endif
