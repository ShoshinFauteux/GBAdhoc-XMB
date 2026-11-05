/* ui.h -- GBAdhoc XMB's screens (design A: psp.js and psp.css of the design).
 * ui_kit.c draws the pieces every screen shares; ui.c holds the screens,
 * the dialogs and the input. */
#ifndef XMBAPP_UI_H
#define XMBAPP_UI_H

#include "gfx.h"
#include "model.h"
#include "gen/xmb_font_ids.h"

/* design tokens (brand.css) */
#define C_INK0 0xFF0A0C1Eu
#define C_FG 0xFFF2F1ECu
#define C_FG2 0xFFC3C5D8u
#define C_FG3 0xFF8E92B2u
#define C_FG4 0xFF5D6188u
#define C_ACCENT 0xFF8D7BF3u
#define C_ACCENT_INK 0xFF0C0B20u
#define C_OK 0xFF8AC32Au
#define C_WARN 0xFFF4BB20u
#define C_BAD 0xFFEF3F6Du
#define C_INFO 0xFF1FB5B0u
#define C_WHITE 0xFFFFFFFFu
#define C_LINE 0x1AFFFFFFu
#define C_LINE2 0x2EFFFFFFu
#define C_GOOD_TXT 0xFFB7EA5Cu
#define C_LOW_TXT 0xFFFFD45Au
#define C_BAD_TXT 0xFFFF8AA8u
#define C_PEND_TXT 0xFFC9C0FFu

enum { K_UP = 1, K_DOWN, K_LEFT, K_RIGHT, K_CROSS, K_CIRCLE, K_TRI, K_SQ, K_L, K_R, K_START, K_SELECT };

/* hint glyph kinds */
enum { HG_CROSS, HG_CIRCLE, HG_TRI, HG_SQ, HG_LR, HG_START };
typedef struct { int g; const char *label; } hint;

void kit_init(void);
void kit_background(void);                 /* the app's ground */
void kit_top(const char *crumb_dim, const char *crumb_bold);
void kit_hints(const hint *h, int n);
void kit_toast(const char *msg, uint32_t dot, float t);   /* t = seconds shown */
float kit_glyph(int g, float x, float cy, uint32_t col);  /* returns width */
float kit_chip(float x, float y, const char *text, uint32_t dot);   /* returns width */
float kit_chip_w(const char *text, int dot);
void kit_row_bg(float x, float y, float w, float h, int on);
void kit_toggle(float x, float y, int on);
void kit_bar(float x, float y, float w, float frac);
void kit_bar_indeterminate(float x, float y, float w);
void kit_spinner(float cx, float cy, float r, uint32_t col);
void kit_step_icon(float x, float y, int st);
/* panel frame: returns content box via *cx,*cy,*cw; pop = 0..1 entrance */
void kit_panel(float x, float y, float w, float h, float pop);   /* pushes: content at (0,0) */
void kit_panel_end(void);
float kit_button(float x, float y, const char *label, int on, int danger);   /* returns width */
float kit_button_w(const char *label);
void kit_scrim(float a);
/* the GBAdhoc logo (baked) */
void kit_logo(int which, float x, float y, float w, float h, float alpha);
enum { LOGO_TOP, LOGO_BOOT, LOGO_ITEM };

/* the ICON0 of a game (or the title card while it is made) at any size */
void kit_icon(int game, float x, float y, float w, float h, float radius, float alpha);
/* the faithful XMB preview of game `gi` at (x,y) scaled by s (1 = 480x272).
 * flags: XP_NOBG (hide the background), XP_ARRIVE (the arrival motion,
 * t = seconds since it started); shift = the column moved up/down by items */
enum { XP_NOBG = 1, XP_ARRIVE = 2 };
void kit_xmb(int gi, float x, float y, float s, int flags, float t, float shift, const int *neighbours, int nn);
/* page-ground colour at a screen point (for rounding a frame's corners) */
uint32_t kit_ground_at(int x, int y);
void kit_corners(float x, float y, float w, float h, float r);
void kit_lin(float deg, float w, float h, uint32_t a, uint32_t b, uint32_t *c);
void kit_set_ground(uint32_t c);   /* rounded images corner colour; 0 = the page */
uint32_t kit_panel_colour(void);

#endif
