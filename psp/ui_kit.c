/* ui_kit.c -- the pieces every screen shares; see ui.h.  Numbers are the
 * design's (psp.css, data.js previewCSS), measured off the page in Chrome. */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "jobs.h"
#include "util.h"
#include "gen/xmb_art.h"

static gtex g_ground;      /* .app background, 480x272 */
static gtex g_wave;        /* the XMB's own wave (no PIC1) */
static uint32_t *g_ground_px;
static gtex g_art[XART_COUNT];
static uint32_t g_ground_override, g_panel_mid;

/* ---- colour maths for the precomputed grounds ---- */

typedef struct { float r, g, b, a; } rgbaf;

static rgbaf hexc(uint32_t c, float a)
{
   rgbaf o = { (float)(c >> 16 & 0xFF), (float)(c >> 8 & 0xFF), (float)(c & 0xFF), a };
   return o;
}

static rgbaf stops(const rgbaf *c, const float *at, int n, float t)
{
   int i;
   if (t <= at[0]) return c[0];
   for (i = 1; i < n; i++)
      if (t <= at[i])
      {
         float k = (t - at[i - 1]) / (at[i] - at[i - 1]);
         /* premultiplied, as CSS interpolates gradients */
         float a = c[i - 1].a + (c[i].a - c[i - 1].a) * k;
         rgbaf o;
         o.r = a > 0 ? (c[i - 1].r * c[i - 1].a + (c[i].r * c[i].a - c[i - 1].r * c[i - 1].a) * k) / a : 0;
         o.g = a > 0 ? (c[i - 1].g * c[i - 1].a + (c[i].g * c[i].a - c[i - 1].g * c[i - 1].a) * k) / a : 0;
         o.b = a > 0 ? (c[i - 1].b * c[i - 1].a + (c[i].b * c[i].a - c[i - 1].b * c[i - 1].a) * k) / a : 0;
         o.a = a;
         return o;
      }
   return c[n - 1];
}

static uint32_t pack(rgbaf c)
{
   return (uint32_t)clampf(c.r + 0.5f, 0, 255) | (uint32_t)clampf(c.g + 0.5f, 0, 255) << 8 |
          (uint32_t)clampf(c.b + 0.5f, 0, 255) << 16 | 0xFF000000u;
}

static rgbaf over_f(rgbaf d, rgbaf s)
{
   rgbaf o;
   o.r = s.r * s.a + d.r * (1 - s.a);
   o.g = s.g * s.a + d.g * (1 - s.a);
   o.b = s.b * s.a + d.b * (1 - s.a);
   o.a = 1;
   return o;
}

static void make_ground(void)
{
   /* radial-gradient(90% 70% at 100% 0,#262266 0%,#141740 42%,#0c0e26 100%)
    * + ::before: a 300 px circle at (420,30), rgba(141,123,243,.28) fading
    * to transparent at 62% of its farthest-corner radius */
   rgbaf c[3] = { hexc(g_th->app_bg[0], 1), hexc(g_th->app_bg[1], 1), hexc(g_th->app_bg[2], 1) };
   float at[3] = { 0, 0.42f, 1 };
   rgbaf v[2] = { hexc(g_th->glow_rgb, g_th->glow_a), hexc(g_th->glow_rgb, 0) };
   float vat[2] = { 0, 0.62f };
   int x, y;
   if (!g_ground.px) gtex_alloc(&g_ground, SCR_W, SCR_H);
   g_ground_px = (uint32_t *)g_ground.px;
   for (y = 0; y < SCR_H; y++)
      for (x = 0; x < SCR_W; x++)
      {
         float dx = (x + 0.5f - 480) / 432.0f, dy = (y + 0.5f) / 190.4f;
         rgbaf p = stops(c, at, 3, sqrtf(dx * dx + dy * dy));
         float ex = x + 0.5f - 420, ey = y + 0.5f - 30, d = sqrtf(ex * ex + ey * ey) / 212.13f;
         if (d < 0.62f)
            p = over_f(p, stops(v, vat, 2, d));
         g_ground_px[y * g_ground.bw + x] = pack(p);
      }
   gtex_flush(&g_ground);
}

uint32_t kit_ground_at(int x, int y)
{
   uint32_t p;
   if (x < 0) x = 0;
   if (y < 0) y = 0;
   if (x >= SCR_W) x = SCR_W - 1;
   if (y >= SCR_H) y = SCR_H - 1;
   p = g_ground_px[y * g_ground.bw + x];
   return 0xFF000000u | (p & 0xFF) << 16 | (p & 0xFF00) | (p >> 16 & 0xFF);
}

static void make_wave(void)
{
   /* linear-gradient(170deg,#4a3ab0,#1e1a6e 50%,#0b0a2a), and the crest:
    * a 672 x 163 box at (-96, 125) rotated -5deg, radial-gradient(55% 45% at
    * 50% 0, rgba(255,255,255,.28), transparent 70%) */
   rgbaf c[3] = { hexc(g_th->xmb_wave[0], 1), hexc(g_th->xmb_wave[1], 1), hexc(g_th->xmb_wave[2], 1) };
   float at[3] = { 0, 0.5f, 1 };
   float dx = sinf(170 * 3.14159265f / 180), dy = -cosf(170 * 3.14159265f / 180);
   float len = fabsf(480 * dx) + fabsf(272 * dy);
   float ca = cosf(5 * 3.14159265f / 180), sa = sinf(5 * 3.14159265f / 180);
   int x, y;
   if (!g_wave.px) gtex_alloc(&g_wave, SCR_W, SCR_H);
   for (y = 0; y < SCR_H; y++)
      for (x = 0; x < SCR_W; x++)
      {
         float px = x + 0.5f, py = y + 0.5f;
         float t = ((px - 240) * dx + (py - 136) * dy) / len + 0.5f;
         rgbaf p = stops(c, at, 3, t);
         /* into the crest box's own frame (undo its -5deg rotation) */
         float qx = px - 240, qy = py - 206.7f;
         float lx = ca * qx - sa * qy + 336, ly = sa * qx + ca * qy + 81.6f;
         if (lx >= 0 && lx <= 672 && ly >= 0 && ly <= 163.2f)
         {
            float ex = (lx - 336) / 369.6f, ey = ly / 73.44f, d = sqrtf(ex * ex + ey * ey);
            if (d < 0.7f)
            {
               rgbaf w = hexc(0xffffff, 0.28f * (1 - d / 0.7f));
               p = over_f(p, w);
            }
         }
         ((uint32_t *)g_wave.px)[y * g_wave.bw + x] = pack(p);
      }
   gtex_flush(&g_wave);
}

static uint32_t panel_mid(void)
{
   uint32_t c[4];
   kit_lin(160, 456, 200, g_th->panel_a, g_th->panel_b, c);
   return col_mix(c[0], c[3], 0.5f);
}

void kit_theme(int i)
{
   int g;
   theme_set(i);
   make_ground();
   make_wave();
   g_panel_mid = panel_mid();
   /* the title cards are drawn in the theme's colours: make them again */
   for (g = 0; g < g_ngames; g++)
      if (g_games[g].icon.kind == ART_NONE)
         g_games[g].icon_img.ver = -1;
}

void kit_init(void)
{
   int i;
   make_ground();
   make_wave();
   g_panel_mid = panel_mid();
   for (i = 0; i < XART_COUNT; i++)
   {
      gtex *t = &g_art[i];
      memset(t, 0, sizeof(*t));
      t->px = (void *)xart_table[i].px;
      t->w = xart_table[i].w;
      t->h = xart_table[i].h;
      t->tw = xart_table[i].tw;
      t->th = xart_table[i].th;
      t->bw = xart_table[i].tw;
      t->psm = 3; /* GU_PSM_8888 */
   }
}

void kit_background(void)
{
   gfx_tex_opaque(&g_ground, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 0);
}

void kit_logo(int which, float x, float y, float w, float h, float alpha)
{
   int id = which == LOGO_TOP ? XART_LOGO_TOP : which == LOGO_BOOT ? XART_LOGO_BOOT : XART_LOGO_ITEM;
   const gtex *t = &g_art[id];
   gfx_tex(t, 0, 0, (float)t->w, (float)t->h, x, y, w, h, col_alpha(C_WHITE, alpha), w != t->w);
}

/* ---- chrome ---- */

void kit_top(const char *dim, const char *bold)
{
   char right[96], fb[32];
   int hh, mm, on = jobs_online();
   float x, rx;
   kit_logo(LOGO_TOP, 8, 4, 66, 24, 1);
   /* XMB plate: 37.9 x 16 at (82,8), r4 */
   gfx_rrect(82, 8, 37.91f, 16, 4, C_ACCENT);
   gfx_text_n(XF_800_10, 87, 8 + face_baseline(XF_800_10, 12.5f) + 1.75f, "XMB", -1, 1.4f, C_ACCENT_INK);
   /* the right cluster, laid out from the right edge (468) */
   wall_hm(&hh, &mm);
   snprintf(right, sizeof(right), "%02d:%02d", hh, mm);
   rx = 468;
   {
      float by = 9.13f + face_baseline(XF_600_11, 13.75f);
      float w = text_w(XF_600_11, right);
      gfx_text(XF_600_11, rx - w, by, right, C_FG3);
      rx -= w + 8;
      if (g_st.free_bytes >= 0)
      {
         char fr[48];
         fmt_mb(g_st.free_bytes, fb, sizeof(fb));
         snprintf(fr, sizeof(fr), "%s free", fb);
         w = text_w(XF_600_11, fr);
         gfx_text(XF_600_11, rx - w, by, fr, g_st.free_bytes < 1024 * 1024 ? C_BAD : C_FG3);
         rx -= w + 8;
      }
      {
         const char *nt = on == 1 ? "Wi-Fi" : on < 0 ? "Connecting" : "Offline";
         w = text_w(XF_600_11, nt);
         gfx_text(XF_600_11, rx - w, by, nt, C_FG3);
         rx -= w + 4 + 7;
         if (on == 1)
         {
            gfx_shadow(rx, 12.13f, 7, 7, 3.5f, 0, 0, 6, 0, col_alpha(C_OK, 1));
            gfx_circle(rx + 3.5f, 15.63f, 3.5f, C_OK);
         }
         else
            gfx_circle(rx + 3.5f, 15.63f, 3.5f, on < 0 ? C_WARN : C_FG4);
      }
   }
   /* the crumb: 133.9 to the right cluster, cut to fit */
   x = 133.91f;
   {
      float by = 8.5f + face_baseline(XF_600_12, 15);
      float maxw = rx - 12 - x;
      if (dim && dim[0])
      {
         float w = gfx_text_ell(XF_600_12, x, by, maxw * 0.6f, dim, C_FG3);
         x += w;
         x += gfx_text(XF_600_12, x, by, " \xE2\x80\xBA ", C_FG3);
      }
      if (bold)
         gfx_text_ell(XF_600_12, x, by, rx - 12 - x, bold, C_FG2);
   }
}

static const uint32_t GLYPH_COL[4] = { 0, 0xFFFF7D9Fu, 0xFFA6DF4Au, 0xFFFF9AB3u };   /* [0]: the theme's cross */

float kit_glyph(int g, float x, float cy, uint32_t col)
{
   if (g <= HG_SQ)
   {
      static const char *sym[4] = { "\xE2\x9C\x95", "\xE2\x97\x8B", "\xE2\x96\xB3", "\xE2\x96\xA1" };
      uint32_t c = col ? col : g == HG_CROSS ? g_th->cross : GLYPH_COL[g];
      const xf_face *f = face_get(XF_800_10);
      float w = text_w(XF_800_10, sym[g]);
      gfx_ring(x + 7.5f, cy, 7.5f, 1.5f, c);
      /* the symbol's ink centred in the ring */
      {
         const char *s = sym[g];
         unsigned cp = xf_utf8(&s);
         const xf_glyph *gl = xf_find(f, cp);
         float gy = gl ? cy - (gl->yo + gl->h / 2.0f) : cy + 3;
         float gx = gl ? x + 7.5f - (gl->xo + gl->w / 2.0f) : x + 7.5f - w / 2;
         gfx_text(XF_800_10, gx, gy, sym[g], c);
      }
      return 15;
   }
   {
      const char *t = g == HG_LR ? "L R" : "START";
      uint32_t c = col ? col : C_FG2;
      float tw = text_wt(XF_800_10, t, 0.4f), w = tw + 10 + 3;
      gfx_rring(x, cy - 7.5f, w, 15, 7, 1.5f, c);
      gfx_text_n(XF_800_10, x + 6.5f, cy + 3.6f, t, -1, 0.4f, c);
      return w;
   }
}

void kit_hints(const hint *h, int n)
{
   int i;
   float x = 12;
   gfx_rect(0, 247, SCR_W, 25, 0x8C05060Fu);
   gfx_rect(0, 247, SCR_W, 1, 0x0FFFFFFFu);
   for (i = 0; i < n; i++)
   {
      float gw = kit_glyph(h[i].g, x, 260, 0);
      x += gw + 5;
      x += gfx_text(XF_600_11, x, 252.5f + face_baseline(XF_600_11, 15), h[i].label, C_FG2);
      x += 14;
   }
}

void kit_toast(const char *msg, uint32_t dot, float t)
{
   float k = ease_out(clampf(t / 0.22f, 0, 1));
   float y = 248 + (1 - k) * 24;
   gfx_rect(0, y, SCR_W, 24, g_th->toast);
   gfx_rect(0, y, SCR_W, 1, 0x0FFFFFFFu);
   gfx_circle(11.5f, y + 11.5f, 3.5f, dot);
   gfx_text_ell(XF_600_115, 20, y + face_baseline(XF_600_115, 24), SCR_W - 32, msg, C_FG);
}

float kit_chip_w(const char *text, int dot)
{
   return text_w(XF_700_105, text) + 14 + (dot ? 11 : 0);
}

float kit_chip(float x, float y, const char *text, uint32_t dot)
{
   float w = kit_chip_w(text, dot != 0);
   gfx_rrect(x, y, w, 16.5f, 5, 0x14FFFFFFu);
   if (dot)
      gfx_circle(x + 10, y + 8.25f, 3, dot);
   gfx_text(XF_700_105, x + 7 + (dot ? 11 : 0), y + 3 + face_baseline(XF_700_105, 10.5f), text, C_FG2);
   return w;
}

void kit_row_bg(float x, float y, float w, float h, int on)
{
   if (!on)
      return;
   gfx_rrect(x, y, w, h, 6, g_th->sel);
   gfx_rring(x, y, w, h, 6, 1.5f, C_FOCUS);
}

void kit_toggle(float x, float y, int on)
{
   gfx_rrect(x, y, 26, 14, 7, on ? C_OK : 0x24FFFFFFu);
   gfx_circle(x + (on ? 14 : 2) + 5, y + 7, 5, on ? C_INK0 : C_FG2);
}

void kit_bar(float x, float y, float w, float frac)
{
   gfx_rrect(x, y, w, 4, 2, 0x1AFFFFFFu);
   if (frac > 0)
   {
      gfx_clip(x, y, w * clampf(frac, 0, 1), 4);
      gfx_rrect(x, y, w, 4, 2, C_ACCENT);
      gfx_unclip();
   }
}

void kit_bar_indeterminate(float x, float y, float w)
{
   /* .bar2 b.ind: 40 % wide, translateX -100% <-> 250%, 1.2 s alternate */
   double t = now_s() / 1.2;
   float k = (float)(t - floor(t));
   float p;
   if (((long)floor(t)) & 1) k = 1 - k;
   k = bezier(0.42f, 0, 0.58f, 1, k);
   p = -1.0f + k * 3.5f;
   gfx_rrect(x, y, w, 4, 2, 0x1AFFFFFFu);
   gfx_clip(x, y, w, 4);
   gfx_rect(x + p * w * 0.4f, y, w * 0.4f, 4, C_ACCENT);
   gfx_unclip();
}

void kit_spinner(float cx, float cy, float r, uint32_t col)
{
   /* a ring with its top quarter missing, turning once per 0.8 s */
   double t = now_s() / 0.8;
   float a0 = (float)((t - floor(t)) * 2 * 3.14159265);
   int i, seg = 18;
   for (i = 0; i < seg; i++)
   {
      float a = a0 + i * (3.14159265f * 1.5f / seg);
      gfx_circle(cx + cosf(a) * (r - 1), cy + sinf(a) * (r - 1), 1.0f, col);
   }
}

void kit_step_icon(float x, float y, int st)
{
   /* 12 px circle, 2 px border; filled when settled */
   uint32_t c = st == STEP_OK ? C_OK : st == STEP_BAD ? C_BAD : st == STEP_WARN ? C_WARN : C_ACCENT;
   if (st == STEP_PEND)
      kit_spinner(x + 6, y + 6, 6, c);
   else
      gfx_circle(x + 6, y + 6, 6, c);
}

void kit_scrim(float a)
{
   gfx_rect(0, 0, SCR_W, SCR_H, col_alpha(0x9E05060Fu, a));
   if (a > 0.5f) gfx_text_cover(0, 0, SCR_W, SCR_H);
}

void kit_panel(float x, float y, float w, float h, float pop)
{
   /* .panel: linear-gradient(160deg,#1f2352,#11132f), r11, shadows, 2 px accent top */
   float k = ease_out(clampf(pop, 0, 1));
   float s = 0.97f + 0.03f * k, ty = (1 - k) * 10;
   gfx_push(x + w / 2 * (1 - s), y + ty + h / 2 * (1 - s), s, 1);
   gfx_shadow(0, 0, w, h, 11, 0, 20, 60, 0, 0xB3000000u);
   gfx_shadow(0, 0, w, h, 11, 0, 0, 0, 1, 0x24FFFFFFu);
   {
      uint32_t c[4];
      kit_lin(160, w, h, g_th->panel_a, g_th->panel_b, c);
      gfx_rrect_lin(0, 0, w, h, 11, c[0], c[1], c[2], c[3]);
      gfx_text_cover(0, 0, w, h);
      /* the 2 px accent line along the rounded top: the top 2 rows of an
       * accent rounded rect */
      gfx_clip(0, 0, w, 2);
      gfx_rrect(0, 0, w, 24, 11, C_ACCENT);
      gfx_unclip();
      g_panel_mid = col_mix(c[0], c[3], 0.5f);
   }
}

void kit_lin(float deg, float w, float h, uint32_t a, uint32_t b, uint32_t *c)
{
   /* CSS linear-gradient(<deg>, a, b) over w x h: its colour at the corners */
   float dx = sinf(deg * 3.14159265f / 180), dy = -cosf(deg * 3.14159265f / 180);
   float len = fabsf(w * dx) + fabsf(h * dy);
   c[0] = col_mix(a, b, ((-w / 2) * dx + (-h / 2) * dy) / len + 0.5f);
   c[1] = col_mix(a, b, ((w / 2) * dx + (-h / 2) * dy) / len + 0.5f);
   c[2] = col_mix(a, b, ((-w / 2) * dx + (h / 2) * dy) / len + 0.5f);
   c[3] = col_mix(a, b, ((w / 2) * dx + (h / 2) * dy) / len + 0.5f);
}

void kit_set_ground(uint32_t c)
{
   g_ground_override = c;
}

uint32_t kit_panel_colour(void)
{
   return g_panel_mid;
}

void kit_panel_end(void)
{
   gfx_pop();
}

float kit_button_w(const char *label)
{
   return text_w(XF_700_115, label) + 20 + 3;
}

float kit_button(float x, float y, const char *label, int on, int danger)
{
   float w = kit_button_w(label) + (on ? 21 : 0);
   if (on)
   {
      gfx_rrect(x, y, w, 26, 7, danger ? C_BAD : C_ACCENT);
      gfx_rring(x, y, w, 26, 7, 1.5f, danger ? 0xFFFFB3C6u : g_th->accent_edge);
      kit_glyph(HG_CROSS, x + 11.5f, y + 13, danger ? C_WHITE : C_ACCENT_INK);
      gfx_text(XF_700_115, x + 11.5f + 21, y + face_baseline(XF_700_115, 26), label, danger ? C_WHITE : C_ACCENT_INK);
   }
   else
   {
      gfx_rrect(x, y, w, 26, 7, 0x14FFFFFFu);
      gfx_text(XF_700_115, x + 11.5f, y + face_baseline(XF_700_115, 26), label, C_FG);
   }
   return w;
}

void kit_corners(float x, float y, float w, float h, float r)
{
   gfx_corner_mask(x, y, w, h, r, kit_ground_at((int)x, (int)y), kit_ground_at((int)(x + w - 1), (int)y),
                   kit_ground_at((int)x, (int)(y + h - 1)), kit_ground_at((int)(x + w - 1), (int)(y + h - 1)));
}

/* ---- icons ---- */

static void wrap_slot(gtex *t, const img_slot *s)
{
   int tw = 8, th = 8;
   while (tw < s->w) tw <<= 1;
   while (th < s->h) th <<= 1;
   memset(t, 0, sizeof(*t));
   t->px = s->px;
   t->w = s->w;
   t->h = s->h;
   t->tw = tw;
   t->th = th;
   t->bw = s->w;
   t->psm = 3;
}

void kit_icon(int gi, float x, float y, float w, float h, float radius, float alpha)
{
   const img_slot *s = jobs_icon(gi);
   if (s && s->px)
   {
      gtex t;
      wrap_slot(&t, s);
      gfx_push(0, 0, 1, alpha);
      gfx_rrect(x, y, w, h, radius, C_INK0);         /* .ic0 ground (a fitted icon's margins) */
      gfx_tex(&t, 0, 0, 144, 80, x, y, w, h, C_WHITE, 1);
      gfx_pop();
      if (radius >= 1)
      {
         if (g_ground_override)
            gfx_corner_mask(x, y, w, h, radius, g_ground_override, g_ground_override, g_ground_override, g_ground_override);
         else
            kit_corners(x, y, w, h, radius);
      }
   }
   else
      gfx_rrect(x, y, w, h, radius, col_alpha(C_INK0, alpha));
}

/* ---- the XMB preview ---- */

static const char *CAT_ART[6] = { "set", "photo", "music", "video", "game", "net" };

static int cat_art(int i, int big)
{
   static const int small[6] = { XART_CAT_SET_22, XART_CAT_PHOTO_22, XART_CAT_MUSIC_22, XART_CAT_VIDEO_22,
                                 XART_CAT_GAME_22, XART_CAT_NET_22 };
   static const int large[6] = { XART_CAT_SET_34, XART_CAT_PHOTO_34, XART_CAT_MUSIC_34, XART_CAT_VIDEO_34,
                                 XART_CAT_GAME_34, XART_CAT_NET_34 };
   (void)CAT_ART;
   return big ? large[i] : small[i];
}

static void xmb_text_shadowed(int face, float x, float y, const char *s, float maxw, float sa)
{
   gfx_push(0, 0, 1, sa);
   gfx_text_ell(face, x, y + 1, maxw, s, 0xCC000000u);
   gfx_text_ell(face, x + 0.5f, y + 1.5f, maxw, s, 0x55000000u);
   gfx_pop();
   gfx_text_ell(face, x, y, maxw, s, C_WHITE);
}

/* one item of the column: kind 0 = a game icon, 1 = Memory Stick, 2 = GBAdhoc */
static void xmb_item(int kind, int gi, float top, float op, int sel, float t, int arrive, const char *label)
{
   float s = sel ? 1.0f : 0.58f;
   float cx = 24 + 72, cy = top + 40;
   float ix = cx - 72 * s, iy = cy - 40 * s, iw = 144 * s, ih = 80 * s;
   float icon_scale = 1, bright = 0;
   if (sel && arrive)
   {
      /* xa-ic: 1 s after .2 s: scale .94 -> 1.03 (55 %) -> 1, brightness 1.25 -> 1 */
      float k = clampf((t - 0.2f) / 1.0f, 0, 1), e;
      if (k < 0.55f) { e = bezier(0.2f, 0.9f, 0.2f, 1, k / 0.55f); icon_scale = 0.94f + 0.09f * e; bright = 0.25f * (1 - k / 0.55f); }
      else { e = bezier(0.2f, 0.9f, 0.2f, 1, (k - 0.55f) / 0.45f); icon_scale = 1.03f - 0.03f * e; }
      if (t < 0.2f) { icon_scale = 0.94f; bright = 0.25f; }
      ix = cx - 72 * icon_scale;
      iy = cy - 40 * icon_scale;
      iw = 144 * icon_scale;
      ih = 80 * icon_scale;
   }
   gfx_push(0, 0, 1, op);
   /* drop-shadow(0 2px 5px rgba(0,0,0,.7)) */
   if (kind != 1)
      gfx_shadow(ix, iy, iw, ih, 3 * s, 0, 2 * s, 5 * s, 0, 0xB3000000u);
   if (kind == 0)
      kit_icon(gi, ix, iy, iw, ih, 0, 1);
   else if (kind == 1)
   {
      float mw = 58 * s;
      gfx_shadow(cx - mw / 2, iy, mw, ih, 3 * s, 0, 2 * s, 5 * s, 0, 0xB3000000u);
      gfx_rrect(cx - mw / 2, iy, mw, ih, 3 * s, 0xFF1B3F8Au);
      gfx_clip(cx - mw / 2, iy, mw, ih);
      gfx_hgrad(cx - mw / 2 + 1, iy, mw - 2, ih, 0xFF1B3F8Au, 0xFF2F6FD1u);
      gfx_unclip();
   }
   else
   {
      gfx_rrect(ix, iy, iw, ih, 3 * s, C_INK0);
      kit_logo(LOGO_ITEM, cx - 52 * s, cy - 19 * s, 104 * s, 38 * s, op);
   }
   if (bright > 0)
      gfx_rrect(ix, iy, iw, ih, 3, col_alpha(C_WHITE, bright * 0.5f));
   gfx_pop();
   if (sel && label)
   {
      float lx = 24 + 144 + 16, la = 1;
      if (arrive)
      {
         float k = clampf((t - 0.6f) / 0.7f, 0, 1);
         lx += 8 * (1 - bezier(0.2f, 0.9f, 0.2f, 1, k));
         la = t < 0.6f ? 1 : 1;
      }
      gfx_push(0, 0, 1, la);
      xmb_text_shadowed(XF_600_16, lx, cy + face_baseline(XF_600_16, 20) - 10, label, 280, 1);
      gfx_pop();
   }
}

void kit_xmb(int gi, float x, float y, float s, int flags, float t, float shift, const int *nb, int nn)
{
   game *g = &g_games[gi];
   const img_slot *pic = (flags & XP_NOBG) ? NULL : jobs_pic(gi);
   int i, hh, mm;
   char clock[8];
   gfx_push(x, y, s, 1);
   gfx_clip(0, 0, SCR_W, SCR_H);
   /* background: PIC1 or the XMB's wave */
   if (!(flags & XP_NOBG) && g->bg.kind != ART_NONE)
   {
      gfx_rect(0, 0, SCR_W, SCR_H, g_th->xmb_bg);
      if (pic && pic->px)
      {
         gtex pt;
         float k = 1;
         wrap_slot(&pt, pic);
         if (flags & XP_ARRIVE)
            k = 1.04f - 0.04f * bezier(0, 0, 0.58f, 1, clampf(t / 2.4f, 0, 1));
         gfx_push(240 * (1 - k), 136 * (1 - k), k, 1);
         gfx_tex_opaque(&pt, 0, 0, (float)pt.w, (float)pt.h, 0, 0, SCR_W, SCR_H, 1);
         gfx_pop();
      }
   }
   else
      gfx_tex_opaque(&g_wave, 0, 0, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 1);
   /* the category row */
   for (i = 0; i < 6; i++)
   {
      float cx = 96 + (i - 4) * (i < 4 ? 44 : 60);
      int big = i == 4;
      const gtex *a = &g_art[cat_art(i, big)];
      float sz = big ? 34 : 22, top = big ? 18 : 28;
      float op = big ? 1 : 0.6f;
      gfx_push(0, 0, 1, op);
      gfx_tex(a, 0, 0, sz, sz, cx - sz / 2, top + 1, sz, sz, 0x99000000u, 1);
      gfx_tex(a, 0, 0, sz, sz, cx - sz / 2, top, sz, sz, C_WHITE, 1);
      gfx_pop();
      if (big)
      {
         float w = text_w(XF_600_11, "Game");
         xmb_text_shadowed(XF_600_11, cx - w / 2, 54 + face_baseline(XF_600_11, 13.75f), "Game", 80, 1);
      }
   }
   /* clock and battery: right 14, top 10 */
   wall_hm(&hh, &mm);
   snprintf(clock, sizeof(clock), "%02d:%02d", hh, mm);
   {
      float bw = 21, bx = 466 - bw, w = text_w(XF_600_13, clock), by = 10 + face_baseline(XF_600_13, 16.25f);
      xmb_text_shadowed(XF_600_13, bx - 8 - w, by, clock, 80, 1);
      gfx_rring(bx, 12.1f, 21, 12, 2, 1.5f, C_WHITE);
      gfx_rect(bx + 1.5f, 13.6f, 18 * 0.7f, 9, C_WHITE);
   }
   /* the column */
   gfx_push(0, shift * -48, 1, 1);
   {
      static const float tops[5] = { -6, 50, 110, 196, 246 };
      static const float ops[5] = { 0.28f, 0.5f, 1, 0.5f, 0.32f };
      int arrive = (flags & XP_ARRIVE) != 0;
      for (i = 0; i < 5; i++)
      {
         if (i == 2)
            xmb_item(0, gi, tops[i], 1, 1, t, arrive, g->title);
         else if (i == 0)
            xmb_item(1, -1, tops[i], ops[i], 0, t, 0, NULL);
         else if (i == 1)
            xmb_item(2, -1, tops[i], ops[i], 0, t, 0, NULL);
         else
         {
            int k = i - 3;
            if (k < nn && nb[k] >= 0)
               xmb_item(0, nb[k], tops[i], ops[i], 0, t, 0, NULL);
         }
      }
   }
   gfx_pop();
   gfx_unclip();
   gfx_pop();
}
