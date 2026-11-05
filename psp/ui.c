/* ui.c -- GBAdhoc XMB's screens, dialogs and input: design A's psp.js, one
 * view per function, on a stack.  Copy is the design's, sentence case. */
#include "ui.h"

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "jobs.h"
#include "util.h"

void ui_netconf_open(void);          /* main.c: the firmware's network dialog */
void ui_request_exit(void);          /* main.c */

/* ------------------------------------------------------------ state ---- */

enum { V_BOOT, V_HOME, V_GAME, V_PICK, V_TITLE, V_FULL, V_ARRIVE, V_BATCH, V_SETTINGS, V_FOLDERS, V_BROWSE };
enum { O_NONE, O_CONFIRM, O_SHEET, O_FETCH, O_MAKE, O_BATCHCONFIRM, O_TASK };

typedef struct
{
   int kind;
   double t0;
   int back;               /* entered by popping back to it */
   int game, which, sel, inited;
   /* title */
   char text[72];
   int zone, si, r, c, upper;
   char sugg[3][128];
   int nsugg;
   /* full / arrive */
   int bg_on;
   float shift;
   double hint_t;
   int list[64], nlist, idx;
   /* batch */
   int mode;
   int arrived;
   /* lists */
   tween track;
   int prev_sel;
   double sel_t;
   char path[PATHCAP];
} view;

#define MAXV 10
static view g_v[MAXV];
static int g_nv;

typedef void (*act_fn)(int arg);
typedef struct { char label[64]; act_fn fn; int arg; int danger; int dim; char small[160]; char why[160]; } btn_t;

typedef struct
{
   int kind;
   double t0;
   char title[96], sub[160], body[400], who[96];
   btn_t b[6];
   int nb, sel;
   act_fn cancel;
   int cancel_arg;
   int game;
   int list[MAX_GAMES], nlist;    /* batch confirm */
   int mode;
   double done_at;
} overlay;

static overlay g_o;
static char g_toast[160];
static uint32_t g_toast_col;
static double g_toast_t = -100;

static view *cur(void) { return g_nv ? &g_v[g_nv - 1] : NULL; }

static void toast(const char *msg, uint32_t col)
{
   snprintf(g_toast, sizeof(g_toast), "%s", msg);
   g_toast_col = col;
   g_toast_t = now_s();
   LOG("toast %s", msg);
}
#define TOAST_OK C_OK
#define TOAST_WARN C_WARN
#define TOAST_BAD C_BAD

static view *push(int kind)
{
   view *v;
   if (g_nv >= MAXV)
      return cur();
   v = &g_v[g_nv++];
   memset(v, 0, sizeof(*v));
   v->kind = kind;
   v->t0 = now_s();
   v->game = -1;
   v->prev_sel = -1;
   LOG("push view %d", kind);
   return v;
}

static void pop(void)
{
   if (g_nv > 1)
   {
      g_nv--;
      cur()->t0 = now_s();
      cur()->back = 1;
      LOG("pop to view %d", cur()->kind);
   }
}

static void replace_top(int kind)
{
   if (g_nv)
      g_nv--;
   push(kind);
}

static void close_overlay(void)
{
   g_o.kind = O_NONE;
}

/* ------------------------------------------------------------ library ---- */

enum { F_ALL, F_NEW, F_INXMB, F_ATTN, F_COUNT };
static const char *FILTER_LABEL[F_COUNT] = { "All", "Not in XMB", "In XMB", "Needs attention" };
static int g_filter, g_hsel;

static int pass_filter(int f, const game *g)
{
   int st = game_status(g);
   switch (f)
   {
   case F_NEW: return g->entry < 0;
   case F_INXMB: return g->entry >= 0;
   case F_ATTN: return st == ST_UNKNOWN || st == ST_MOVED || st == ST_NOART || st == ST_MISSING;
   default: return 1;
   }
}

static int lib_list(int f, int *out)
{
   int i, n = 0;
   for (i = 0; i < g_ngames; i++)
      if (pass_filter(f, &g_games[i]))
      {
         if (out) out[n] = i;
         n++;
      }
   return n;
}

static int marked_count(void)
{
   int i, n = 0;
   for (i = 0; i < g_ngames; i++)
      n += g_games[i].marked;
   return n;
}

/* neighbours in the XMB preview: other games that have entries, else any */
static int neighbours(int gi, int *nb)
{
   int i, n = 0;
   for (i = 0; i < g_ngames && n < 2; i++)
      if (i != gi && g_games[i].entry >= 0 && g_entries[g_games[i].entry].game >= 0)
         nb[n++] = i;
   for (i = 0; i < g_ngames && n < 2; i++)
      if (i != gi && (n == 0 || nb[0] != i))
         nb[n++] = i;
   return n;
}

/* ------------------------------------------------------------ the wash ---- */

static gtex g_wash;
static int g_wash_game = -1, g_wash_ver = -1;

static void wash_for(int gi)
{
   const img_slot *p;
   game *g = &g_games[gi];
   if (g_wash_game == gi && g_wash_ver == g->ver)
      return;
   p = jobs_pic(gi);
   if (!p || !p->px || p->w != 480)
   {
      if (g->bg.kind == ART_NONE) { g_wash_game = gi; g_wash_ver = g->ver; if (g_wash.px) gtex_free(&g_wash); }
      return;
   }
   if (!g_wash.px)
      gtex_alloc(&g_wash, 60, 34);
   {
      /* 8x8 box average, then blur 2 (= 16 px at full size), saturate 1.1 */
      image im = { (uint32_t *)malloc(60 * 34 * 4), 60, 34 };
      int x, y, i, j;
      if (!im.px) return;
      for (y = 0; y < 34; y++)
         for (x = 0; x < 60; x++)
         {
            unsigned r = 0, gg = 0, b = 0;
            for (j = 0; j < 8; j++)
               for (i = 0; i < 8; i++)
               {
                  uint32_t c = p->px[(y * 8 + j) * 480 + x * 8 + i];
                  r += c & 0xFF; gg += c >> 8 & 0xFF; b += c >> 16 & 0xFF;
               }
            im.px[y * 60 + x] = (r / 64) | (gg / 64) << 8 | (b / 64) << 16 | 0xFF000000u;
         }
      img_blur(&im, 2);
      img_saturate(&im, 1.1f);
      for (y = 0; y < 34; y++)
         memcpy((uint32_t *)g_wash.px + y * g_wash.bw, im.px + y * 60, 60 * 4);
      free(im.px);
      gtex_flush(&g_wash);
   }
   g_wash_game = gi;
   g_wash_ver = g->ver;
}

/* ------------------------------------------------------------ dialogs ---- */

static void dlg(const char *title, const char *sub, const char *body)
{
   memset(&g_o, 0, sizeof(g_o));
   g_o.kind = O_CONFIRM;
   g_o.t0 = now_s();
   snprintf(g_o.title, sizeof(g_o.title), "%s", title);
   snprintf(g_o.sub, sizeof(g_o.sub), "%s", sub ? sub : "");
   snprintf(g_o.body, sizeof(g_o.body), "%s", body ? body : "");
   LOG("dialog %s", title);
}

static void add_btn(const char *label, act_fn fn, int arg, int danger)
{
   btn_t *b;
   if (g_o.nb >= 6) return;
   b = &g_o.b[g_o.nb++];
   memset(b, 0, sizeof(*b));
   snprintf(b->label, sizeof(b->label), "%s", label);
   b->fn = fn;
   b->arg = arg;
   b->danger = danger;
}

static void act_close(int a) { (void)a; close_overlay(); }

/* ---- forward ---- */
static void open_game(int gi);
static void make_flow(int gi, int replace);
static void fetch_flow(int gi);
static void push_pick(int gi, int which);
static void push_arrive(const int *list, int n);
static void push_batch(const int *list, int n, int make);
static void confirm_batch(const int *list, int n);
static void no_space(void);

/* ------------------------------------------------------------ views ---- */

static float view_dx(const view *v)
{
   /* vin/vback: translateX(+-22px) -> 0 over .28 s */
   float k = ease_out(clampf((float)((now_s() - v->t0) / 0.28), 0, 1));
   return (v->back ? -22.0f : 22.0f) * (1 - k);
}

/* ---- boot ---- */

static void draw_boot(view *v)
{
   float t = (float)(now_s() - v->t0);
   float k1 = ease_out(clampf(t / 0.6f, 0, 1)), k2 = ease_out(clampf((t - 0.25f) / 0.6f, 0, 1));
   float k3 = ease_out(clampf((t - 0.45f) / 0.6f, 0, 1));
   char msg[160], sub[200];
   float frac;
   task_lock();
   snprintf(msg, sizeof(msg), "%s", g_task.msg);
   snprintf(sub, sizeof(sub), "%s", g_task.sub);
   frac = g_task.frac;
   task_unlock();
   if (g_task.kind == TASK_SCAN && g_task.done)
   {
      int rec = 0, i, n = 0;
      for (i = 0; i < g_ngames; i++)
         if (strcmp(g_games[i].folder, "not found"))
         {
            n++;
            rec += g_games[i].m.recognised;
         }
      if (!g_st.found)
         snprintf(msg, sizeof(msg), "GBAdhoc was not found on the %s", g_st.is_go ? "internal storage or the Memory Stick" : g_st.label);
      else
         snprintf(msg, sizeof(msg), "%d ROM%s \xC2\xB7 %d recognised \xC2\xB7 %d not recognised", n, n == 1 ? "" : "s", rec, n - rec);
      sub[0] = 0;
      frac = 1;
   }
   /* the column: logo 176x64, word, scan text, bar; centred, gap 8 */
   {
      float total = 64 + 8 + 19 + 8 + 30 + 8 + 4, y0 = floorf((SCR_H - total) / 2);
      kit_logo(LOGO_BOOT, 152, y0 + floorf(8 * (1 - k1) + 0.5f), 176, 64, 1);
      {
         float ww = text_wt(XF_800_12, "XMB", 4.08f) + 8 + 11 - 4.08f, wy = y0 + 72 + 8 * (1 - k2);
         gfx_push(0, 0, 1, k2 > 0 ? 1 : 0);
         gfx_rrect(240 - ww / 2, wy, ww, 21, 5, C_ACCENT);
         gfx_text_n(XF_800_12, 240 - ww / 2 + 11, wy + 3 + face_baseline(XF_800_12, 15), "XMB", -1, 4.08f, C_ACCENT_INK);
         gfx_pop();
      }
      {
         float sy = y0 + 72 + 21 + 16 + 8 * (1 - k3);
         gfx_push(0, 0, 1, k3 > 0 ? 1 : 0);
         gfx_text_center(XF_400_11, 240, sy + face_baseline(XF_400_11, 13.75f), msg, C_FG3);
         if (sub[0])
         {
            float w = text_w(XF_500_105, sub);
            if (w > 270)
               gfx_text_ell(XF_500_105, 105, sy + 13.75f + face_baseline(XF_500_105, 13), 270, sub, C_FG2);
            else
               gfx_text_center(XF_500_105, 240, sy + 13.75f + face_baseline(XF_500_105, 13), sub, C_FG2);
         }
         kit_bar(105, sy + 30 + 8, 270, frac);
         gfx_pop();
      }
   }
   if (g_task.kind == TASK_SCAN && g_task.done)
   {
      if (!v->inited)
      {
         v->inited = 1;
         v->hint_t = now_s();
      }
      else if (now_s() - v->hint_t > 0.65)
      {
         jobs_ack();
         g_hsel = 0;
         replace_top(V_HOME);
      }
   }
}

/* ---- home ---- */

static void chips_for(const game *g, float x, float y, float maxw, int rows)
{
   char c[8][96], tmp[96];
   uint32_t dot[8];
   int n = 0, st = game_status(g), i, row = 0;
   float cx = x;
   snprintf(c[n], 96, "%s", status_label(st));
   dot[n++] = status_colour(st);
   if (g->m.recognised)
   {
      const char *k1 = g->icon.kind == ART_NONE ? "none" : g->icon.kind == ART_OWN ? "GBAdhoc's" :
                       g->icon.kind == ART_NET ? "internet" : g->icon.kind == ART_ENTRY ? "the entry's" : "own picture";
      const char *k2 = g->bg.kind == ART_NONE ? "none" : g->bg.kind == ART_OWN ? "GBAdhoc's" :
                       g->bg.kind == ART_NET ? "internet" : g->bg.kind == ART_ENTRY ? "the entry's" : "own picture";
      snprintf(c[n], 96, "Box art \xC2\xB7 %s", k1);
      dot[n++] = g->icon.kind != ART_NONE ? C_OK : C_FG4;
      snprintf(c[n], 96, "Hero \xC2\xB7 %s", k2);
      dot[n++] = g->bg.kind != ART_NONE ? C_OK : C_FG4;
   }
   else if (st != ST_MISSING)
   {
      snprintf(c[n], 96, "No No-Intro match for this file name");
      dot[n++] = C_WARN;
   }
   if (g->m.hack)
   {
      snprintf(c[n], 96, "ROM hack \xC2\xB7 matched %d%%", g->m.conf);
      dot[n++] = 0;
   }
   if (g->entry >= 0 && g_entries[g->entry].made)
   {
      fmt_day(g_entries[g->entry].made, tmp, sizeof(tmp));
      snprintf(c[n], 96, "Made %s", tmp);
      dot[n++] = 0;
   }
   /* wrap; a chip that would start a row past `rows` is left out (no
    * half-cut pill under the filmstrip) */
   for (i = 0; i < n; i++)
   {
      float w = kit_chip_w(c[i], dot[i] != 0);
      if (cx > x && cx + w > x + maxw)
      {
         row++;
         cx = x;
      }
      if (row >= rows)
         break;
      if (w > maxw)
      {
         /* one too long for the line on its own: cut it */
         gfx_clip(cx, y + row * 21.5f, maxw, 17);
      }
      kit_chip(cx, y + row * 21.5f, c[i], dot[i]);
      if (w > maxw)
         gfx_unclip();
      cx += w + 5;
   }
}

#define HPV_X 246.0f
#define HPV_Y 60.0f
#define HPV_W 210.0f
#define HPV_H 119.0f

static void draw_home(view *v)
{
   static int list[MAX_GAMES];
   int n = lib_list(g_filter, list), i, gi;
   float dx = view_dx(v), x;
   char cnt[48];
   if (g_hsel >= n) g_hsel = n ? n - 1 : 0;
   gi = n ? list[g_hsel] : -1;
   gfx_push(dx, 0, 1, 1);
   /* the wash: the selected game's background, blurred, faint */
   if (gi >= 0)
   {
      wash_for(gi);
      if (g_wash.px && g_wash_game == gi && g_games[gi].bg.kind != ART_NONE)
      {
         /* opacity .16, masked transparent -> 30% -> 70% -> transparent */
         gfx_tex_vfade(&g_wash, 0.5f, 0.5f, 59, 10.2f * 0 + 10.2f, 0, 32, 480, 51, C_WHITE, 0, 0.16f);
         gfx_tex_vfade(&g_wash, 0.5f, 10.7f, 59, 13.6f, 0, 83, 480, 68, C_WHITE, 0.16f, 0.16f);
         gfx_tex_vfade(&g_wash, 0.5f, 24.3f, 59, 9.2f, 0, 151, 480, 51, C_WHITE, 0.16f, 0);
      }
   }
   /* tabs: L <  All 9  Not in XMB 7 ...  > R            1 / 9 */
   x = 12;
   gfx_text(XF_700_10, x, 38.75f + face_baseline(XF_700_10, 12.5f), "L \xE2\x97\x80", C_FG_SMALL);
   x = 36.64f;
   for (i = 0; i < F_COUNT; i++)
   {
      char num[8];
      int on = i == g_filter;
      float w;
      snprintf(num, sizeof(num), "%d", lib_list(i, NULL));
      w = text_w(XF_700_11, FILTER_LABEL[i]) + 4 + text_w(XF_600_11, num) + 18;
      if (on)
         gfx_rrect(x, 35.13f, w, 19.75f, 9.875f, 0x1FFFFFFFu);
      gfx_text(XF_700_11, x + 9, 35.13f + face_baseline(XF_700_11, 19.75f), FILTER_LABEL[i], on ? C_WHITE : C_FG3);
      gfx_text(XF_600_11, x + 9 + text_w(XF_700_11, FILTER_LABEL[i]) + 4, 35.13f + face_baseline(XF_700_11, 19.75f), num,
               on ? C_FG2 : C_FG_SMALL);
      x += w + 4;
   }
   gfx_text(XF_700_10, x, 38.75f + face_baseline(XF_700_10, 12.5f), "\xE2\x96\xB6 R", C_FG_SMALL);
   {
      int mk = marked_count();
      if (n)
         snprintf(cnt, sizeof(cnt), "%s%s%d / %d", mk ? "" : "", "", g_hsel + 1, n);
      else
         cnt[0] = 0;
      if (mk)
      {
         char t2[48];
         snprintf(t2, sizeof(t2), "%d marked \xC2\xB7 %s", mk, cnt);
         snprintf(cnt, sizeof(cnt), "%s", t2);
      }
      gfx_text_right(XF_600_11, 468, 38.13f + face_baseline(XF_600_11, 13.75f), cnt, C_FG3);
   }
   if (gi < 0)
   {
      gfx_text(XF_800_15, 24, 66 + face_baseline(XF_800_15, 18), "Nothing here", C_FG3);
      gfx_text(XF_600_11, 24, 92 + face_baseline(XF_600_11, 13.75f),
               g_ngames ? "No games match this filter. L and R change the filter." :
               "No ROMs found. Put them in GBAdhoc's roms folder, or add a folder in Settings.", C_FG2);
   }
   else
   {
      game *g = &g_games[gi];
      int nb[2], nn, lines, st[2], ln[2];
      char meta[200], sz[32];
      float hw = HPV_X - 14 - 24, ty;
      /* title (800 19px, two lines, clamped), meta, chips */
      lines = text_wrap(XF_800_19, g->title, hw, 2, st, ln);
      ty = 66;
      for (i = 0; i < lines; i++)
      {
         char line[160];
         snprintf(line, sizeof(line), "%.*s", ln[i], g->title + st[i]);
         gfx_text_ell(XF_800_19, 24, ty + 1 + face_baseline(XF_800_19, 21.85f), hw, line, 0x66000000u);
         gfx_text_ell(XF_800_19, 24, ty + face_baseline(XF_800_19, 21.85f), hw, line, C_FG);
         ty += 21.85f;
      }
      if (g->size >= 1024 * 1024)
         snprintf(sz, sizeof(sz), "%d MB", (int)((g->size + 512 * 1024) / (1024 * 1024)));
      else
         snprintf(sz, sizeof(sz), "%d KB", (int)((g->size + 512) / 1024));
      snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s \xC2\xB7 %s", sys_name(g->sys), g->folder, sz);
      gfx_text_ell(XF_600_11, 24, ty + 5 + face_baseline(XF_600_11, 13.75f), hw, meta, C_FG2);
      chips_for(g, 24, ty + 5 + 13.75f + 7, hw, lines > 1 ? 2 : 3);
      /* the XMB preview (fixed: the caption sits under the frame, not on it) */
      {
         float k = ease_out(clampf((float)((now_s() - v->sel_t) / 0.3), 0, 1)), s = 0.96f + 0.04f * k;
         float px = HPV_X + HPV_W / 2 * (1 - s), py = HPV_Y + HPV_H / 2 * (1 - s);
         if (v->sel_t == 0) s = 1, px = HPV_X, py = HPV_Y;
         gfx_shadow(px, py, HPV_W * s, HPV_H * s, 7, 0, 10, 28, 0, 0x8C000000u);
         nn = neighbours(gi, nb);
         kit_xmb(gi, px, py, s * HPV_W / 480.0f, 0, 0, 0, nb, nn);
         kit_corners(px, py, HPV_W * s, HPV_H * s, 7);
         gfx_rring(px, py, HPV_W * s, HPV_H * s, 7, 1, 0x38FFFFFFu);
         gfx_text(XF_600_105, HPV_X, HPV_Y + HPV_H + 3 + face_baseline(XF_600_105, 13), "Its place in the XMB", C_FG3);
      }
   }
   /* the filmstrip: 76x42 tiles, gap 8, centred on the selection */
   {
      float tw = 76, gap = 8, sx = 24 + g_hsel * (tw + gap), tx = 240 - tw / 2 - sx;
      float total = 48 + n * (tw + gap) - gap, ttx;
      if (tx > 0) tx = 0;
      if (tx < (480 - total < 0 ? 480 - total : 0)) tx = 480 - total < 0 ? 480 - total : 0;
      if (!v->inited) { tw_set(&v->track, tx); v->inited = 1; }
      tw_go(&v->track, tx, 0.32f);
      ttx = tw_val(&v->track);
      gfx_clip(0, 194, 480, 54);
      for (i = 0; i < n; i++)
      {
         float x0 = ttx + 24 + i * (tw + gap), y0 = 199;
         float k = 0;
         int sel = i == g_hsel;
         game *g = &g_games[list[i]];
         if (x0 + tw * 1.2f < 0 || x0 - tw * 0.2f > 480)
            continue;
         if (sel) k = ease_out(clampf((float)((now_s() - v->sel_t) / 0.25), 0, 1));
         else if (i == v->prev_sel) k = 1 - ease_out(clampf((float)((now_s() - v->sel_t) / 0.25), 0, 1));
         if (v->sel_t == 0 && sel) k = 1;
         {
            float s = 1 + 0.1f * k, op = 0.55f + 0.45f * k;
            float cx = x0 + tw / 2, cy = y0 + 21, w = tw * s, h = 42 * s, xx = cx - w / 2, yy = cy - h / 2;
            gfx_shadow(xx, yy, w, h, 4, 0, 2 + 6 * k, 6 + 14 * k, 0, col_alpha(0x80000000u, 1 + 0.2f * k));
            if (k > 0.01f)
            {
               gfx_rring(xx - 4, yy - 4, w + 8, h + 8, 8, 2, col_alpha(C_FOCUS, k));
               gfx_rring(xx - 2, yy - 2, w + 4, h + 4, 6, 2, col_alpha(C_FOCUS_IN, k));
            }
            kit_icon(list[i], xx, yy, w, h, 4, op);
            if (g->marked)
            {
               gfx_rrect(xx + 4, yy + 4, 14, 14, 3, C_OK);
               gfx_text_center(XF_800_10, xx + 11, yy + 4 + 10.8f, "\xE2\x9C\x93", C_INK0);
            }
            /* status dot ON the tile's top border, not on the art */
            gfx_circle(xx + w - 9, yy, 4.75f, 0xCC05060Fu);
            gfx_circle(xx + w - 9, yy, 3.5f, status_colour(game_status(g)));
         }
      }
      gfx_unclip();
      gfx_hgrad(0, 194, 28, 54, g_th->strip_fade, g_th->strip_fade & 0xFFFFFFu);
      gfx_hgrad(452, 194, 28, 54, g_th->strip_fade & 0xFFFFFFu, g_th->strip_fade);
   }
   gfx_pop();
}

static void home_select(view *v, int to)
{
   if (to == g_hsel)
      return;
   v->prev_sel = g_hsel;
   g_hsel = to;
   v->sel_t = now_s();
}

static void lib_actions(void);

static void key_home(view *v, int k)
{
   static int list[MAX_GAMES];
   int n = lib_list(g_filter, list);
   if (k == K_LEFT && g_hsel > 0) home_select(v, g_hsel - 1);
   else if (k == K_RIGHT && g_hsel + 1 < n) home_select(v, g_hsel + 1);
   else if (k == K_L || k == K_R)
   {
      g_filter = (g_filter + (k == K_L ? -1 : 1) + F_COUNT) % F_COUNT;
      g_hsel = 0;
      v->prev_sel = -1;
      v->sel_t = now_s();
      v->inited = 0;
   }
   else if (k == K_CROSS && n) open_game(list[g_hsel]);
   else if (k == K_SQ && n) { game *g = &g_games[list[g_hsel]]; g->marked = !g->marked; }
   else if (k == K_TRI) lib_actions();
   else if (k == K_START) push(V_SETTINGS);
}

static int hints_home(hint *h)
{
   static int list[MAX_GAMES];
   int n = lib_list(g_filter, list);
   h[0].g = HG_CROSS; h[0].label = "Open";
   h[1].g = HG_SQ; h[1].label = n && g_games[list[g_hsel < n ? g_hsel : 0]].marked ? "Unmark" : "Mark";
   h[2].g = HG_TRI; h[2].label = "Actions";
   h[3].g = HG_LR; h[3].label = "Filter";
   h[4].g = HG_START; h[4].label = "Settings";
   return 5;
}

/* ---- library actions ---- */

static int g_tmp_list[MAX_GAMES];

static void act_make_marked(int a)
{
   int i, n = 0;
   (void)a;
   for (i = 0; i < g_ngames; i++)
      if (g_games[i].marked) g_tmp_list[n++] = i;
   confirm_batch(g_tmp_list, n);
}

static void act_make_all(int a)
{
   int i, n = 0;
   (void)a;
   for (i = 0; i < g_ngames; i++)
      if (g_games[i].m.recognised && g_games[i].entry < 0) g_tmp_list[n++] = i;
   confirm_batch(g_tmp_list, n);
}

static void act_art_all(int marked_only)
{
   int i, n = 0;
   for (i = 0; i < g_ngames; i++)
      if (g_games[i].m.recognised && (!marked_only || g_games[i].marked) && strcmp(g_games[i].folder, "not found"))
         g_tmp_list[n++] = i;
   push_batch(g_tmp_list, n, 0);
}

static void act_unmark(int a)
{
   int i;
   (void)a;
   for (i = 0; i < g_ngames; i++) g_games[i].marked = 0;
   toast("Marks cleared", TOAST_OK);
}

static void lib_actions(void)
{
   int mk = marked_count(), ready = 0, rec = 0, i, off = jobs_online() != 1;
   char t[160];
   for (i = 0; i < g_ngames; i++)
   {
      ready += g_games[i].m.recognised && g_games[i].entry < 0;
      rec += g_games[i].m.recognised && strcmp(g_games[i].folder, "not found") != 0;
   }
   memset(&g_o, 0, sizeof(g_o));
   g_o.kind = O_SHEET;
   g_o.t0 = now_s();
   snprintf(g_o.title, sizeof(g_o.title), "Library actions");
   if (mk)
   {
      int n = 0;
      snprintf(t, sizeof(t), "Make %d marked %s", mk, mk == 1 ? "entry" : "entries");
      add_btn(t, act_make_marked, 0, 0);
      for (i = 0; i < g_ngames && n < 150; i++)
         if (g_games[i].marked)
            n += snprintf(g_o.b[g_o.nb - 1].small + n, sizeof(g_o.b[0].small) - (size_t)n, "%s%s", n ? ", " : "", g_games[i].title);
   }
   add_btn("Make all entries", act_make_all, 0, 0);
   snprintf(g_o.b[g_o.nb - 1].small, sizeof(g_o.b[0].small), "%d recognised game%s not in the XMB yet", ready, ready == 1 ? "" : "s");
   if (!ready) { g_o.b[g_o.nb - 1].dim = 1; snprintf(g_o.b[g_o.nb - 1].why, 160, "Every recognised game is already in the XMB"); }
   add_btn("Get art for all games", act_art_all, 0, 0);
   if (off)
      snprintf(g_o.b[g_o.nb - 1].small, 160, "offline");
   else
      snprintf(g_o.b[g_o.nb - 1].small, 160, "%d games \xC2\xB7 libretro-thumbnails + GBAdhoc-heroart", rec);
   if (off) { g_o.b[g_o.nb - 1].dim = 1; snprintf(g_o.b[g_o.nb - 1].why, 160, "You are offline"); }
   if (mk)
   {
      snprintf(t, sizeof(t), "Get art for %d marked", mk);
      add_btn(t, act_art_all, 1, 0);
      if (off) { g_o.b[g_o.nb - 1].dim = 1; snprintf(g_o.b[g_o.nb - 1].why, 160, "You are offline"); }
   }
   add_btn("Unmark all", act_unmark, 0, 0);
   if (!mk) { g_o.b[g_o.nb - 1].dim = 1; snprintf(g_o.b[g_o.nb - 1].why, 160, "Nothing is marked"); }
}

/* ---- confirm a batch: the icons that will be written ---- */

static void act_batch_make(int skip)
{
   int i, n = 0;
   for (i = 0; i < g_o.nlist; i++)
   {
      game *g = &g_games[g_o.list[i]];
      if (skip && !game_has_icon(g) && !(g->fetch == FETCH_NONE && g->m.recognised && jobs_online() == 1))
         continue;
      g_tmp_list[n++] = g_o.list[i];
   }
   close_overlay();
   if (!n) { toast("Nothing left to make", TOAST_WARN); return; }
   push_batch(g_tmp_list, n, 1);
}

static void confirm_batch(const int *list, int n)
{
   int i, noart = 0, will = 0;
   memset(&g_o, 0, sizeof(g_o));
   if (!n) { toast("Nothing to make", TOAST_WARN); return; }
   g_o.kind = O_BATCHCONFIRM;
   g_o.t0 = now_s();
   for (i = 0; i < n && i < MAX_GAMES; i++)
   {
      game *g = &g_games[list[i]];
      g_o.list[i] = list[i];
      if (!game_has_icon(g))
      {
         noart++;
         will += g->fetch == FETCH_NONE && g->m.recognised && jobs_online() == 1;
      }
   }
   g_o.nlist = i;
   snprintf(g_o.title, sizeof(g_o.title), "Make %d %s?", n, n == 1 ? "entry" : "entries");
   snprintf(g_o.sub, sizeof(g_o.sub), "These icons are written to %s/PSP/GAME \xC2\xB7 about %d KB", g_st.dev, n * 200);
   if (noart)
   {
      if (will)
         snprintf(g_o.body, sizeof(g_o.body), "%d %s no art yet (outlined): art is looked up first for %s, and any still without art get a title card as the icon.",
                  noart, noart == 1 ? "has" : "have", will == noart ? "them" : "some of them");
      else
         snprintf(g_o.body, sizeof(g_o.body), "%d %s no art yet (outlined): they get a title card as the icon.", noart, noart == 1 ? "has" : "have");
   }
   else
      snprintf(g_o.body, sizeof(g_o.body), "Every icon is ready.");
   add_btn("Make them", act_batch_make, 0, 0);
   if (noart)
      add_btn("Skip those without art", act_batch_make, 1, 0);
   add_btn("Not now", act_close, 0, 0);
   LOG("dialog %s", g_o.title);
}

/* ---- game ---- */

typedef struct { const char *k; char v[160]; int muted, act, primary, danger, dim, wrap, sep; int id; char why[200]; } row_t;
enum { R_TITLE, R_BOX, R_HERO, R_ICON, R_SEP, R_FETCH, R_MAKE, R_REMOVE };

static int game_rows(const game *g, row_t *r)
{
   int n = 0, st = game_status(g), off = jobs_online() != 1;
   char lab[128];
   memset(r, 0, sizeof(row_t) * 8);
   if (st == ST_MISSING)
   {
      r[n].k = "Title"; snprintf(r[n].v, 160, "%s", g->title); r[n].wrap = 1; r[n].id = R_TITLE; r[n].dim = 1;
      snprintf(r[n].why, 200, "Its ROM is not on the %s; remove the entry, or put the ROM back and rescan.", g_st.label); n++;
      r[n].sep = 1; r[n].id = R_SEP; n++;
      r[n].act = 1; r[n].danger = 1; r[n].id = R_REMOVE; snprintf(r[n].v, 160, "Remove entry from the XMB"); n++;
      return n;
   }
   r[n].k = "Title"; snprintf(r[n].v, 160, "%s", g->title); r[n].wrap = 1; r[n].id = R_TITLE; n++;
   art_label(&g->icon, lab, sizeof(lab));
   r[n].k = "Box art"; r[n].id = R_BOX;
   if (g->icon.kind == ART_NET && g->icon.conf)
      snprintf(r[n].v, 160, "%s \xC2\xB7 %d%%", lab, g->icon.conf);
   else
      snprintf(r[n].v, 160, "%s", lab);
   r[n].muted = g->icon.kind == ART_NONE; n++;
   art_label(&g->bg, lab, sizeof(lab));
   r[n].k = "Hero"; r[n].id = R_HERO; snprintf(r[n].v, 160, "%s", lab); r[n].muted = g->bg.kind == ART_NONE; n++;
   r[n].k = "Icon"; r[n].id = R_ICON;
   if (!game_has_icon(g)) snprintf(r[n].v, 160, "Title card (no art yet)");
   else snprintf(r[n].v, 160, "%s", g->fit == FIT_TILE ? "Tile \xC2\xB7 hero + box art" : g->fit == FIT_FIT ? "Box art fitted" : "Box art filled");
   r[n].muted = !game_has_icon(g); n++;
   r[n].sep = 1; r[n].id = R_SEP; n++;
   r[n].act = 1; r[n].id = R_FETCH;
   snprintf(r[n].v, 160, "%s", off ? "Get art from the internet \xC2\xB7 offline" : !g->m.recognised ? "Get art from the internet \xC2\xB7 not recognised" : "Get art from the internet");
   r[n].dim = off || !g->m.recognised;
   snprintf(r[n].why, 200, "%s", !g->m.recognised ? "Not matched to a No-Intro title, so there is nothing to look up. Use a picture of your own." :
            "Offline: connect in Settings \xE2\x80\xBA Network to fetch art, or make the entry with the art it has.");
   n++;
   r[n].act = 1; r[n].primary = 1; r[n].id = R_MAKE;
   snprintf(r[n].v, 160, "%s", g->entry >= 0 ? (g_entries[g->entry].rom_moved ? "Update entry \xC2\xB7 fix ROM path" : "Update entry") : "Make entry");
   n++;
   if (g->entry >= 0)
   {
      r[n].act = 1; r[n].danger = 1; r[n].id = R_REMOVE; snprintf(r[n].v, 160, "Remove entry from the XMB"); n++;
   }
   return n;
}

static void remove_flow(int gi);

static void draw_game(view *v)
{
   game *g = &g_games[v->game];
   row_t r[8];
   int n = game_rows(g, r), i, nb[2], nn = neighbours(v->game, nb), st = game_status(g);
   float dx = view_dx(v), y = 40;
   if (v->sel >= n) v->sel = n - 1;
   if (r[v->sel].sep) v->sel++;
   gfx_push(dx, 0, 1, 1);
   /* the preview, 204x116 at (12,40), r6 */
   gfx_shadow(12, 40, 204, 116, 6, 0, 6, 20, 0, 0x99000000u);
   kit_xmb(v->game, 12, 40, 0.425f, 0, 0, 0, nb, nn);
   kit_corners(12, 40, 204, 116, 6);
   gfx_rring(12, 40, 204, 116, 6, 1, 0x24FFFFFFu);
   {
      char cap[300];
      uint32_t bg = 0, bd = 0, fg = C_FG3;
      cap[0] = 0;
      if (st == ST_MISSING)
      {
         snprintf(cap, sizeof(cap), "Its ROM is not on the %s any more: %s. Remove the entry, or put the ROM back.", g_st.label, g->rel);
         bg = 0x29EF3F6Du; bd = 0x73EF3F6Du; fg = 0xFFFFC4D3u;
      }
      else if (g->entry >= 0 && g_entries[g->entry].rom_moved)
      {
         snprintf(cap, sizeof(cap), "Its ROM moved to %s. Update the entry to point launch.ini there.", g->folder);
         bg = 0x29EF3F6Du; bd = 0x73EF3F6Du; fg = 0xFFFFC4D3u;
      }
      else if (!g->m.recognised)
      {
         snprintf(cap, sizeof(cap), "No No-Intro match, so art can't be looked up. Give it a title and a picture from %s/PICTURE.", g_st.dev);
         bg = g_th->warn_soft; bd = g_th->warn_line; fg = g_th->warn_text2;
      }
      else if (g->m.hack)
      {
         snprintf(cap, sizeof(cap), "ROM hack, matched to \"%s\" (%d%%). Hero art is looked up by ROM name.", g->nointro_title, g->m.conf);
         bg = g_th->info_soft; bd = g_th->info_line; fg = g_th->info_text2;
      }
      if (cap[0])
      {
         int st2[5], ln2[5], nl = text_wrap(XF_400_105, cap, 188, 4, st2, ln2), j;
         float h = nl * 13.65f + 12;
         gfx_rrect(12, 162, 204, h, 6, bg);
         gfx_rring(12, 162, 204, h, 6, 1, bd);
         for (j = 0; j < nl; j++)
         {
            char line[200];
            snprintf(line, sizeof(line), "%.*s", ln2[j], cap + st2[j]);
            gfx_text_ell(XF_400_105, 20, 168 + j * 13.65f + face_baseline(XF_400_105, 13.65f), 190, line, fg);
         }
      }
      else
      {
         char made[160];
         if (g->entry >= 0)
         {
            char day[24];
            fmt_day(g_entries[g->entry].made, day, sizeof(day));
            snprintf(made, sizeof(made), "In the XMB since %s \xC2\xB7 PSP/GAME/%s", day, g_entries[g->entry].folder);
         }
         else
            snprintf(made, sizeof(made), "Not in the XMB yet");
         {
            int st2[3], ln2[3], nl = text_wrap(XF_600_105, made, 204, 3, st2, ln2), j;
            for (j = 0; j < nl; j++)
            {
               char line[200];
               snprintf(line, sizeof(line), "%.*s", ln2[j], made + st2[j]);
               gfx_text(XF_600_105, 12, 162 + j * 13.65f + face_baseline(XF_600_105, 13.65f), line, C_FG3);
            }
         }
      }
   }
   /* the rows, x 228 w 240 */
   for (i = 0; i < n; i++)
   {
      int on = i == v->sel;
      float h = 23, ox = on ? 2 : 0;
      if (r[i].sep)
      {
         gfx_rect(236, y + 3, 224, 1, C_LINE);
         y += 7 + 2;
         continue;
      }
      if (r[i].wrap)
      {
         int st2[2], ln2[2], nl = text_wrap(XF_600_12, r[i].v, 155, 2, st2, ln2), j;
         h = nl > 1 ? 34.78f : 23;
         kit_row_bg(228 + ox, y, 240, h, on);
         gfx_push(0, 0, 1, r[i].dim ? 0.45f : 1);
         gfx_text_n(XF_800_105, 235 + ox, y + 3 + face_baseline(XF_800_105, 15.13f) + 2, "TITLE", -1, 0.42f, on ? C_FG2 : C_FG3);
         for (j = 0; j < nl; j++)
         {
            char line[200];
            snprintf(line, sizeof(line), "%.*s", ln2[j], r[i].v + st2[j]);
            gfx_text_ell(XF_600_12, 295 + ox, y + 3 + j * 14.39f + face_baseline(XF_600_12, 14.39f), 155, line, C_FG);
         }
         gfx_text(XF_400_11, 456.77f + ox, y + 3 + face_baseline(XF_400_11, 13.75f), "\xE2\x80\xBA", C_FG_SMALL);
         gfx_pop();
      }
      else
      {
         float by = y + face_baseline(XF_600_12, 23);
         kit_row_bg(228 + ox, y, 240, h, on);
         gfx_push(0, 0, 1, r[i].dim ? 0.45f : 1);
         if (r[i].act)
         {
            uint32_t dc = r[i].danger ? C_BAD : r[i].primary ? C_OK : C_ACCENT;
            gfx_circle(235 + ox + 4, y + 11.5f, 4, dc);
            gfx_text_ell(XF_600_12, 235 + ox + 16, by, 210, r[i].v, C_WHITE);
         }
         else
         {
            char up[24];
            int j;
            for (j = 0; r[i].k[j] && j < 23; j++)
               up[j] = (char)(r[i].k[j] >= 'a' && r[i].k[j] <= 'z' ? r[i].k[j] - 32 : r[i].k[j]);
            up[j] = 0;
            gfx_text_n(XF_800_105, 235 + ox, by - 0.5f, up, -1, 0.42f, on ? C_FG2 : C_FG3);
            gfx_text_ell(r[i].muted ? XF_500_12 : XF_600_12, 295 + ox, by, 155, r[i].v, r[i].muted ? C_FG3 : C_FG);
            gfx_text(XF_400_11, 456.77f + ox, by, "\xE2\x80\xBA", C_FG_SMALL);
         }
         gfx_pop();
      }
      y += h + 2;
   }
   gfx_pop();
}

static void cycle_fit(game *g)
{
   if (!game_has_icon(g))
   {
      toast("With no art the icon is a title card; add box art or a hero to change it", TOAST_WARN);
      return;
   }
   g->fit = (g->fit + 1) % 3;
   game_touch(g);
}

static void key_game(view *v, int k)
{
   game *g = &g_games[v->game];
   row_t r[8];
   int n = game_rows(g, r);
   if (k == K_UP || k == K_DOWN)
   {
      int i = v->sel;
      do { i += k == K_UP ? -1 : 1; } while (i >= 0 && i < n && r[i].sep);
      if (i >= 0 && i < n) v->sel = i;
   }
   else if (k == K_CROSS)
   {
      row_t *x = &r[v->sel];
      if (x->dim) { toast(x->why, TOAST_WARN); return; }
      switch (x->id)
      {
      case R_TITLE:
      {
         view *t = push(V_TITLE);
         t->game = v->game;
         break;
      }
      case R_BOX: push_pick(v->game, 0); break;
      case R_HERO: push_pick(v->game, 1); break;
      case R_ICON: cycle_fit(g); break;
      case R_FETCH: fetch_flow(v->game); break;
      case R_MAKE: make_flow(v->game, 0); break;
      case R_REMOVE: remove_flow(v->game); break;
      }
   }
   else if (k == K_CIRCLE) pop();
   else if (k == K_TRI)
   {
      view *f = push(V_FULL);
      f->game = v->game;
      f->bg_on = 1;
      f->hint_t = now_s();
   }
}

static void open_game(int gi)
{
   view *v = push(V_GAME);
   v->game = gi;
}

/* ---- fetch ---- */

static void act_review(int which)
{
   close_overlay();
   jobs_ack();
   push_pick(g_o.game, which);
}

static void act_make_after(int a)
{
   int gi = g_o.game;
   (void)a;
   close_overlay();
   jobs_ack();
   make_flow(gi, 0);
}

static void act_netconf(int a)
{
   (void)a;
   close_overlay();
   jobs_ack();
   ui_netconf_open();
}

static void act_close_task(int a)
{
   (void)a;
   close_overlay();
   jobs_ack();
}

static void fetch_flow(int gi)
{
   if (jobs_busy()) return;
   memset(&g_o, 0, sizeof(g_o));
   g_o.kind = O_FETCH;
   g_o.t0 = now_s();
   g_o.game = gi;
   jobs_fetch(gi);
   LOG("fetch flow %s", g_games[gi].stem);
}

/* ---- make / update / remove ---- */

static void act_replace(int gi)
{
   close_overlay();
   make_flow(gi, 1);
}

static void make_flow(int gi, int replace)
{
   game *g = &g_games[gi];
   char sub[160], body[400], day[24];
   if (!g_st.found)
   {
      dlg("GBAdhoc is not installed", NULL, g_st.is_go
          ? "An entry opens GBAdhoc, so it needs GBAdhoc in PSP/GAME/GBAdhoc on the internal storage (ef0:) or the Memory Stick (ms0:). Install it, then rescan from Settings."
          : "An entry opens GBAdhoc, so it needs GBAdhoc in PSP/GAME/GBAdhoc on the Memory Stick. Install it, then rescan from Settings.");
      add_btn("OK", act_close, 0, 0);
      return;
   }
   model_refresh_free();
   if (g_st.free_bytes >= 0 && g_st.free_bytes < 420 * 1024)
   {
      no_space();
      return;
   }
   if (g->entry >= 0 && !replace && !g_entries[g->entry].rom_moved)
   {
      entry_t *e = &g_entries[g->entry];
      fmt_day(e->made, day, sizeof(day));
      snprintf(sub, sizeof(sub), "PSP/GAME/%s/ \xC2\xB7 made %s", e->folder, day);
      snprintf(body, sizeof(body), "\"%s\" is rewritten with the title, icon and background shown here. Saves and settings are untouched: they belong to GBAdhoc, not to the entry.", e->title);
      dlg("Replace the existing entry?", sub, body);
      add_btn("Replace", act_replace, gi, 0);
      add_btn("Keep it", act_close, 0, 0);
      return;
   }
   if (!g->title[0]) { toast("Give the entry a title first", TOAST_WARN); return; }
   if (jobs_busy()) return;
   memset(&g_o, 0, sizeof(g_o));
   g_o.kind = O_MAKE;
   g_o.t0 = now_s();
   g_o.game = gi;
   g_o.mode = g->entry >= 0;
   jobs_make(gi, 1);
}

static void no_space(void)
{
   char sub[96], body[300], fb[32];
   fmt_mb(g_st.free_bytes < 0 ? 0 : g_st.free_bytes, fb, sizeof(fb));
   snprintf(sub, sizeof(sub), "%s free on the %s", fb, g_st.label);
   snprintf(body, sizeof(body), "An entry needs about 400 KB: launcher, icon and background. Free some space on %s and try again.%s",
            g_st.dev, g_st.is_go ? " Entries go on the same storage as GBAdhoc." : "");
   dlg("Not enough space", sub, body);
   add_btn("OK", act_close, 0, 0);
}

static void act_do_remove(int gi)
{
   memset(&g_o, 0, sizeof(g_o));
   g_o.kind = O_TASK;
   g_o.t0 = now_s();
   g_o.game = gi;
   jobs_remove(gi);
}

static void remove_flow(int gi)
{
   game *g = &g_games[gi];
   entry_t *e = &g_entries[g->entry];
   char sub[160], body[400];
   snprintf(sub, sizeof(sub), "PSP/GAME/%s/", e->folder);
   snprintf(body, sizeof(body), "\"%s\" disappears from the XMB's Game column. The ROM, its save and GBAdhoc's art stay. You can make the entry again any time.", e->title);
   dlg("Remove this entry?", sub, body);
   add_btn("Keep it", act_close, 0, 0);
   add_btn("Remove", act_do_remove, gi, 1);
   g_o.sel = 0;
}

/* ---- the art picker ---- */

typedef struct { int kind; char path[PATHCAP]; int sect; char label[96], meta[96]; int conf, below, cover, cand; } pick_c;

static char g_pics[16][PATHCAP];
static int g_npics = -1;

static void scan_pictures(void)
{
   char d[64];
   SceUID dh;
   SceIoDirent e;
   if (g_npics >= 0) return;
   g_npics = 0;
   snprintf(d, sizeof(d), "%s/PICTURE", g_st.dev);
   dh = sceIoDopen(d);
   if (dh < 0) return;
   memset(&e, 0, sizeof(e));
   while (sceIoDread(dh, &e) > 0 && g_npics < 16)
   {
      if (!FIO_S_ISDIR(e.d_stat.st_mode) && (ends_with_i(e.d_name, ".png") || ends_with_i(e.d_name, ".jpg") ||
                                             ends_with_i(e.d_name, ".jpeg") || ends_with_i(e.d_name, ".bmp")))
         snprintf(g_pics[g_npics++], PATHCAP, "%s/%s", d, e.d_name);
      memset(&e, 0, sizeof(e));
   }
   sceIoDclose(dh);
}

static int pick_cands(const game *g, int which, pick_c *c)
{
   int n = 0, i;
   memset(c, 0, sizeof(pick_c) * 32);
   if (g->entry >= 0 && (which ? g_entries[g->entry].has_pic : g_entries[g->entry].has_icon))
   {
      c[n].kind = ART_ENTRY;
      snprintf(c[n].path, PATHCAP, "%s/EBOOT.PBP", g_entries[g->entry].dir);
      c[n].sect = which ? 4 : 1;
      snprintf(c[n].label, 96, "The entry's own");
      snprintf(c[n].meta, 96, "PSP/GAME/%s", g_entries[g->entry].folder);
      c[n].cover = which;
      n++;
   }
   if (which ? g->own_hero[0] : g->own_box[0])
   {
      c[n].kind = ART_OWN;
      snprintf(c[n].path, PATHCAP, "%s", which ? g->own_hero : g->own_box);
      snprintf(c[n].label, 96, "GBAdhoc's own");
      snprintf(c[n].meta, 96, which ? "hero folder" : "boxart folder");
      c[n].cover = which;
      n++;
   }
   if (!which)
   {
      for (i = 0; i < g->ncand; i++)
         if (g->cand[i].cache[0])
         {
            c[n].kind = ART_NET;
            snprintf(c[n].path, PATHCAP, "%s", g->cand[i].cache);
            snprintf(c[n].label, 96, "%s", g->cand[i].below ? "nearest match" : g->cand[i].region);
            snprintf(c[n].meta, 96, "%s", g->cand[i].below ? "below the match line" : "libretro-thumbnails");
            c[n].conf = g->cand[i].conf;
            c[n].below = g->cand[i].below;
            c[n].cand = i;
            n++;
         }
   }
   else if (g->hero_found && g->hero_cache[0])
   {
      c[n].kind = ART_NET;
      snprintf(c[n].path, PATHCAP, "%s", g->hero_cache);
      snprintf(c[n].label, 96, "GBAdhoc-heroart");
      snprintf(c[n].meta, 96, "%s.png", g->m.hero ? g->m.hero : g->stem);
      c[n].cover = 1;
      n++;
   }
   scan_pictures();
   for (i = 0; i < g_npics && n < 30; i++)
   {
      c[n].kind = ART_PIC;
      snprintf(c[n].path, PATHCAP, "%s", g_pics[i]);
      snprintf(c[n].label, 96, "%s", base_name(g_pics[i]));
      snprintf(c[n].meta, 96, "%s/PICTURE", g_st.dev);
      c[n].cover = 1;
      n++;
   }
   c[n].kind = ART_NONE;
   snprintf(c[n].label, 96, "None");
   snprintf(c[n].meta, 96, "%s", which ? "the XMB wave shows" : g->bg.kind != ART_NONE ? "hero alone as icon" : "title card as icon");
   n++;
   return n;
}

static int pick_is_current(const game *g, int which, const pick_c *c)
{
   const art_choice *a = which ? &g->bg : &g->icon;
   if (a->kind != c->kind) return 0;
   if (c->kind == ART_NONE) return 1;
   return strcmp(a->path, c->path) == 0;
}

static void push_pick(int gi, int which)
{
   view *v = push(V_PICK);
   pick_c c[32];
   int n, i;
   v->game = gi;
   v->which = which;
   n = pick_cands(&g_games[gi], which, c);
   for (i = 0; i < n; i++)
      if (pick_is_current(&g_games[gi], which, &c[i]))
         v->sel = i;
}

static void draw_pick(view *v)
{
   game *g = &g_games[v->game];
   pick_c c[32];
   int n = pick_cands(g, v->which, c), i;
   float dx = view_dx(v), tx;
   char h3[200], sub[200];
   const char *foot;
   char fb[200];
   if (v->sel >= n) v->sel = n - 1;
   gfx_push(dx, 0, 1, 1);
   snprintf(h3, sizeof(h3), "%s for %s", v->which ? "Hero art" : "Box art", g->title);
   gfx_text_ell(XF_800_13, 12, 38 + face_baseline(XF_800_13, 16.25f), 456, h3, C_FG);
   snprintf(sub, sizeof(sub), "%s%s", v->which ? "PIC1, 480\xC3\x97" "272: fills the screen behind the column." : "Goes into the 144\xC3\x97" "80 icon.",
            g->fetch == FETCH_NONE && jobs_online() == 1 && g->m.recognised ? " Press \xE2\x96\xA1 to look it up on the internet." : "");
   gfx_text_ell(XF_400_11, 12, 56.25f + face_baseline(XF_400_11, 13.75f), 456, sub, C_FG3);
   /* the rail: cands 118 wide, gap 10, centred on 181 */
   {
      float x = 24 + v->sel * 128.0f, t = 181 - x, total = 48 + n * 128 - 10;
      if (t > 0) t = 0;
      if (t < (480 - total < 0 ? 480 - total : 0)) t = 480 - total < 0 ? 480 - total : 0;
      if (!v->inited) { tw_set(&v->track, t); v->inited = 1; }
      tw_go(&v->track, t, 0.32f);
      tx = tw_val(&v->track);
   }
   for (i = 0; i < n; i++)
   {
      float x0 = tx + 24 + i * 128, y0 = 82;
      int on = i == v->sel;
      float k = on ? ease_out(clampf((float)((now_s() - v->sel_t) / 0.22), 0, 1)) : 0, s, w, h, xx, yy;
      if (on && v->sel_t == 0) k = 1;
      if (x0 > 480 || x0 + 118 < 0) continue;
      s = 1 + 0.08f * k;
      w = 118 * s;
      h = 66 * s;
      xx = x0 + 59 - w / 2;
      yy = y0 + 33 - h / 2;
      gfx_shadow(xx, yy, w, h, 6, 0, 2 + 6 * k, 8 + 16 * k, 0, 0x80000000u);
      if (k > 0.01f)
      {
         gfx_rring(xx - 4, yy - 4, w + 8, h + 8, 10, 2, col_alpha(C_FOCUS, k));
         gfx_rring(xx - 2, yy - 2, w + 4, h + 4, 8, 2, col_alpha(C_FOCUS_IN, k));
      }
      gfx_push(0, 0, 1, c[i].below ? 0.55f : 1);
      if (c[i].kind == ART_NONE)
      {
         uint32_t lc[4];
         kit_lin(135, w, h, g_th->empty_a, g_th->empty_b, lc);
         gfx_rrect_lin(xx, yy, w, h, 6, lc[0], lc[1], lc[2], lc[3]);
         gfx_text_center(XF_700_11, xx + w / 2, yy + h / 2 + 4, "None", C_FG3);
      }
      else
      {
         const img_slot *th = jobs_thumb(c[i].path, c[i].sect, 120, 68, c[i].cover);
         gfx_rrect(xx, yy, w, h, 6, C_INK0);
         if (th && th->px)
         {
            gtex t;
            memset(&t, 0, sizeof(t));
            t.px = th->px; t.w = 120; t.h = 68; t.tw = 128; t.th = 128; t.bw = 120; t.psm = 3;
            gfx_tex(&t, 1, 1, 118, 66, xx, yy, w, h, C_WHITE, 1);
            if (!c[i].below)
               kit_corners(xx, yy, w, h, 6);
         }
         else
            kit_spinner(xx + w / 2, yy + h / 2, 6, C_FG4);
      }
      gfx_pop();
      if (c[i].kind == ART_NET && c[i].conf)
      {
         char pc[8];
         float pw;
         snprintf(pc, sizeof(pc), "%d%%", c[i].conf);
         pw = text_w(XF_800_10, pc) + 10;
         gfx_rrect(xx + w - 4 - pw, yy + h - 4 - 16.5f, pw, 16.5f, 4, 0xC705060Fu);
         gfx_text(XF_800_10, xx + w - 4 - pw + 5, yy + h - 4 - 16.5f + face_baseline(XF_800_10, 16.5f),
                  pc, c[i].below ? C_BAD_TXT : c[i].conf < 80 ? C_LOW_TXT : C_GOOD_TXT);
      }
      if (pick_is_current(g, v->which, &c[i]))
      {
         float pw = text_wt(XF_800_10, "IN USE", 0.6f) + 10;
         gfx_rrect(xx + 4, yy + h - 4 - 17.8f, pw, 17.8f, 4, C_OK);
         gfx_text_n(XF_800_10, xx + 9, yy + h - 4 - 17.8f + face_baseline(XF_800_10, 17.8f), "IN USE", -1, 0.6f, C_INK0);
      }
      gfx_text_center(XF_700_11, x0 + 59, 156 + face_baseline(XF_700_11, 13.75f), c[i].label, C_FG);
      gfx_text_center(XF_600_10, x0 + 59, 169.75f + face_baseline(XF_600_10, 12.5f), c[i].meta, C_FG3);
   }
   {
      pick_c *s = &c[v->sel];
      if (s->below) foot = "Below the match line: probably not this game.";
      else if (s->kind == ART_PIC) foot = v->which ? "Your own picture, scaled to fill 480\xC3\x97" "272." : "Your own picture, fitted into the icon.";
      else if (s->kind == ART_OWN) foot = "Already on the Memory Stick; nothing to download.";
      else if (s->kind == ART_ENTRY) foot = "What the entry shows now.";
      else if (s->kind == ART_NET)
      {
         snprintf(fb, sizeof(fb), "Downloaded once and kept in GBAdhoc's %s folder.", v->which ? "hero" : "boxart");
         foot = fb;
      }
      else if (!v->which) foot = g->bg.kind != ART_NONE ? "No box art: the 144\xC3\x97" "80 icon shows the hero alone." : "No art at all: the icon becomes a title card with the game's name.";
      else foot = "The XMB wave shows behind the column.";
      if (s->below)
      {
         float w = gfx_text(XF_700_11, 12, 229.7f + face_baseline(XF_400_11, 14), "Below the match line:", C_BAD_TXT);
         gfx_text(XF_400_11, 12 + w, 229.7f + face_baseline(XF_400_11, 14), " probably not this game.", C_FG3);
      }
      else
         gfx_text_ell(XF_400_11, 12, 229.7f + face_baseline(XF_400_11, 14), 456, foot, C_FG3);
   }
   gfx_pop();
}

static void key_pick(view *v, int k)
{
   game *g = &g_games[v->game];
   pick_c c[32];
   int n = pick_cands(g, v->which, c);
   if (k == K_LEFT && v->sel > 0) { v->sel--; v->sel_t = now_s(); }
   else if (k == K_RIGHT && v->sel + 1 < n) { v->sel++; v->sel_t = now_s(); }
   else if (k == K_CROSS)
   {
      pick_c *s = &c[v->sel];
      art_choice *a = v->which ? &g->bg : &g->icon;
      char lab[128], msg[200];
      memset(a, 0, sizeof(*a));
      a->kind = s->kind;
      snprintf(a->path, PATHCAP, "%s", s->path);
      if (s->kind == ART_NET && !v->which)
      {
         snprintf(a->label, sizeof(a->label), "%s", g->cand[s->cand].region);
         a->conf = s->conf;
      }
      else if (s->kind == ART_PIC)
         snprintf(a->label, sizeof(a->label), "%s", s->label);
      game_touch(g);
      art_label(a, lab, sizeof(lab));
      snprintf(msg, sizeof(msg), "%s: %s", v->which ? "Hero" : "Box art", lab);
      toast(msg, TOAST_OK);
      pop();
   }
   else if (k == K_SQ)
   {
      if (!g->m.recognised) toast("Not recognised: nothing to look up", TOAST_WARN);
      else if (jobs_online() != 1) toast("You are offline", TOAST_WARN);
      else fetch_flow(v->game);
   }
   else if (k == K_CIRCLE) pop();
}

/* ---- title editor ---- */

static const char *OSK[4][13] = {
   { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "&", "'" },
   { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "(", ")", ":" },
   { "A", "S", "D", "F", "G", "H", "J", "K", "L", "!", "?", ".", "," },
   { "Z", "X", "C", "V", "B", "N", "M", "Space", "\xE2\x8C\xAB", "aA", NULL, NULL, NULL },
};
static const int OSK_SPAN[10] = { 1, 1, 1, 1, 1, 1, 1, 3, 2, 1 };

static int osk_len(int r)
{
   int n = 0;
   while (n < 13 && OSK[r][n]) n++;
   return n;
}

static void title_init(view *v)
{
   game *g = &g_games[v->game];
   char t[128];
   int i;
   snprintf(v->text, sizeof(v->text), "%.64s", g->title);
   v->zone = 0;
   v->r = 1;
   v->upper = 1;
   v->nsugg = 0;
   if (g->short_title[0]) snprintf(v->sugg[v->nsugg++], 128, "%s", g->short_title);
   if (g->nointro_title[0]) snprintf(v->sugg[v->nsugg++], 128, "%s", g->nointro_title);
   /* the file name without its tags */
   snprintf(t, sizeof(t), "%s", g->stem);
   {
      char *p = strstr(t, " (");
      if (p) *p = 0;
   }
   snprintf(v->sugg[v->nsugg++], 128, "%s", t);
   /* unique */
   for (i = 1; i < v->nsugg; i++)
   {
      int j;
      for (j = 0; j < i; j++)
         if (strcmp(v->sugg[i], v->sugg[j]) == 0)
         {
            memmove(v->sugg[i], v->sugg[i + 1], sizeof(v->sugg[0]) * (size_t)(v->nsugg - i - 1));
            v->nsugg--;
            i--;
            break;
         }
   }
   v->inited = 1;
}

static void draw_title(view *v)
{
   float dx = view_dx(v), x, cw = (456 - 12 * 3) / 13.0f;
   int r, c;
   char n[16];
   if (!v->inited) title_init(v);
   gfx_push(dx, 0, 1, 1);
   /* field */
   gfx_rrect(12, 38, 456, 28, 7, 0x59000000u);
   gfx_rring(12, 38, 456, 28, 7, 1.5f, v->zone ? C_FOCUS : C_LINE2);
   x = 22 + gfx_text_ell(XF_700_15, 22, 38 + face_baseline(XF_700_15, 28), 380, v->text, C_FG);
   if (fmod(now_s(), 1.0) < 0.5)
      gfx_rect(x + 1, 43.5f, 2, 17, C_ACCENT);
   snprintf(n, sizeof(n), "%d/64", (int)strlen(v->text));
   gfx_text_right(XF_600_10, 458, 38 + face_baseline(XF_600_10, 28), n, C_FG4);
   /* suggestions */
   x = 12;
   x += gfx_text(XF_600_11, x, 72 + face_baseline(XF_600_11, 19.75f), "Suggestions", C_FG3) + 6;
   for (c = 0; c < v->nsugg; c++)
   {
      int on = v->zone == 0 && v->si == c;
      float w = text_w(XF_700_11, v->sugg[c]) + 16;
      if (x + w > 468) w = 468 - x;
      if (w < 30) break;
      gfx_rrect(x, 72, w, 19.75f, 5, on ? C_ACCENT : 0x12FFFFFFu);
      gfx_clip(x, 72, w - 4, 20);
      gfx_text(XF_700_11, x + 8, 72 + face_baseline(XF_700_11, 19.75f), v->sugg[c], on ? C_ACCENT_INK : C_FG2);
      gfx_unclip();
      x += w + 6;
   }
   /* the keyboard: 13 columns, gap 3, keys 22 high */
   for (r = 0; r < 4; r++)
   {
      int col = 0;
      for (c = 0; c < osk_len(r); c++)
      {
         int span = r == 3 ? OSK_SPAN[c] : 1, on = v->zone == 1 && v->r == r && v->c == c;
         const char *lab = OSK[r][c];
         char low[4];
         float kx = 12 + col * (cw + 3), kw = span * cw + (span - 1) * 3, ky = 98 + r * 25;
         int fn = r == 3 && c >= 7;
         if (!v->upper && lab[0] >= 'A' && lab[0] <= 'Z' && !lab[1])
         {
            low[0] = (char)(lab[0] + 32);
            low[1] = 0;
            lab = low;
         }
         gfx_rrect(kx, ky, kw, 22, 5, on ? C_ACCENT : 0x14FFFFFFu);
         gfx_text_center(fn ? XF_700_105 : XF_700_12, kx + kw / 2, ky + face_baseline(fn ? XF_700_105 : XF_700_12, 22),
                         lab, on ? C_ACCENT_INK : fn ? C_FG2 : C_FG);
         col += span;
      }
   }
   gfx_pop();
}

static void osk_type(view *v)
{
   const char *k = OSK[v->r][v->c];
   size_t n = strlen(v->text);
   if (!k) return;
   if (strcmp(k, "Space") == 0) { if (n < 64) { v->text[n] = ' '; v->text[n + 1] = 0; } }
   else if (strcmp(k, "aA") == 0) v->upper = !v->upper;
   else if (k[0] == '\xE2') { if (n) { /* a UTF-8 sequence ends at a lead byte */ while (n > 0 && ((unsigned char)v->text[n - 1] & 0xC0) == 0x80) n--; if (n) n--; v->text[n] = 0; } }
   else if (n < 64)
   {
      char ch = k[0];
      if (!v->upper && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
      v->text[n] = ch;
      v->text[n + 1] = 0;
   }
}

static void key_title(view *v, int k)
{
   game *g = &g_games[v->game];
   if (k == K_TRI)
   {
      char t[72];
      int a = 0, b = (int)strlen(v->text);
      while (v->text[a] == ' ') a++;
      while (b > a && v->text[b - 1] == ' ') b--;
      if (b <= a) { toast("A title cannot be empty", TOAST_WARN); return; }
      snprintf(t, sizeof(t), "%.*s", b - a, v->text + a);
      snprintf(g->title, sizeof(g->title), "%s", t);
      game_touch(g);
      toast("Title set", TOAST_OK);
      pop();
      return;
   }
   if (k == K_CIRCLE) { pop(); return; }
   if (k == K_SQ)
   {
      size_t n = strlen(v->text);
      while (n > 0 && ((unsigned char)v->text[n - 1] & 0xC0) == 0x80) n--;
      if (n) n--;
      v->text[n] = 0;
      return;
   }
   if (v->zone == 0)
   {
      if (k == K_LEFT && v->si > 0) v->si--;
      else if (k == K_RIGHT && v->si + 1 < v->nsugg) v->si++;
      else if (k == K_DOWN) v->zone = 1;
      else if (k == K_CROSS) snprintf(v->text, sizeof(v->text), "%.64s", v->sugg[v->si]);
      return;
   }
   if (k == K_LEFT && v->c > 0) v->c--;
   else if (k == K_RIGHT && v->c + 1 < osk_len(v->r)) v->c++;
   else if (k == K_UP) { if (v->r == 0) v->zone = 0; else { v->r--; if (v->c >= osk_len(v->r)) v->c = osk_len(v->r) - 1; } }
   else if (k == K_DOWN && v->r < 3) { v->r++; if (v->c >= osk_len(v->r)) v->c = osk_len(v->r) - 1; }
   else if (k == K_CROSS) osk_type(v);
}

/* ---- full preview ---- */

static void draw_full(view *v)
{
   int nb[2], nn = neighbours(v->game, nb);
   float a;
   kit_xmb(v->game, 0, 0, 1, v->bg_on ? 0 : XP_NOBG, 0, v->shift, nb, nn);
   /* the hints, shown on a key and fading after 2.2 s */
   a = now_s() - v->hint_t < 2.2 ? 1 : clampf(1 - (float)((now_s() - v->hint_t - 2.2) / 0.3), 0, 1);
   if (a > 0)
   {
      const char *t[3] = { "\xE2\x96\xB2\xE2\x96\xBC look around", v->bg_on ? "\xE2\x96\xA1 hide background" : "\xE2\x96\xA1 show background", "\xE2\x97\x8B back" };
      float w = 20, x;
      int i;
      for (i = 0; i < 3; i++) w += text_w(XF_600_105, t[i]) + (i ? 12 : 0);
      gfx_push(0, 0, 1, a);
      gfx_rrect(470 - w, 238, w, 24, 7, 0xB805060Fu);
      x = 480 - w;
      for (i = 0; i < 3; i++)
         x += gfx_text(XF_600_105, x, 238 + face_baseline(XF_600_105, 24), t[i], C_FG2) + 12;
      gfx_pop();
   }
}

static void key_full(view *v, int k)
{
   if (k == K_CIRCLE || k == K_CROSS || k == K_TRI) pop();
   else if (k == K_SQ) { v->bg_on = !v->bg_on; v->hint_t = now_s(); }
   else if (k == K_UP) { if (v->shift > -1) v->shift -= 1; v->hint_t = now_s(); }
   else if (k == K_DOWN) { if (v->shift < 1) v->shift += 1; v->hint_t = now_s(); }
   else v->hint_t = now_s();
}

/* ---- arrival ---- */

static void push_arrive(const int *list, int n)
{
   view *v = push(V_ARRIVE);
   int i;
   for (i = 0; i < n && i < 64; i++) v->list[i] = list[i];
   v->nlist = i;
   v->idx = 0;
   v->hint_t = now_s();
}

static void draw_arrive(view *v)
{
   int gi = v->list[v->idx], nb[2], nn = 0, i;
   float t = (float)(now_s() - v->hint_t);
   for (i = 0; i < v->nlist && nn < 2; i++)
      if (v->list[i] != gi) nb[nn++] = v->list[i];
   if (nn < 2)
   {
      int nb2[2], m = neighbours(gi, nb2), j;
      for (j = 0; j < m && nn < 2; j++)
         if (nn == 0 || nb[0] != nb2[j]) nb[nn++] = nb2[j];
   }
   kit_xmb(gi, 0, 0, 1, XP_ARRIVE, t, 0, nb, nn);
   {
      /* "In the XMB · 1 of 3": up .5s after 1.4 s */
      char s[48];
      float k = ease_out(clampf((t - 1.4f) / 0.5f, 0, 1)), w;
      if (v->nlist > 1) snprintf(s, sizeof(s), "In the XMB \xC2\xB7 %d of %d", v->idx + 1, v->nlist);
      else snprintf(s, sizeof(s), "In the XMB");
      w = text_w(XF_700_11, s) + 20;
      if (t >= 1.4f)
      {
         gfx_rrect(14, 236 + 8 * (1 - k), w, 24, 7, 0xB805060Fu);
         gfx_text(XF_700_11, 24, 236 + 8 * (1 - k) + face_baseline(XF_700_11, 24), s, C_GOOD_TXT);
      }
   }
   if (t > 1.8f)
   {
      const char *a = "\xE2\x96\xB2\xE2\x96\xBC next entry", *b = "\xE2\x97\x8B done";
      float k = clampf((t - 1.8f) / 0.3f, 0, 1);
      float w = 20 + (v->nlist > 1 ? text_w(XF_600_105, a) + 12 : 0) + text_w(XF_600_105, b), x;
      gfx_push(0, 0, 1, k);
      gfx_rrect(470 - w, 238, w, 24, 7, 0xB805060Fu);
      x = 480 - w;
      if (v->nlist > 1) x += gfx_text(XF_600_105, x, 238 + face_baseline(XF_600_105, 24), a, C_FG2) + 12;
      gfx_text(XF_600_105, x, 238 + face_baseline(XF_600_105, 24), b, C_FG2);
      gfx_pop();
   }
}

static void key_arrive(view *v, int k)
{
   if (k == K_DOWN && v->nlist > 1) { v->idx = (v->idx + 1) % v->nlist; v->hint_t = now_s(); }
   else if (k == K_UP && v->nlist > 1) { v->idx = (v->idx + v->nlist - 1) % v->nlist; v->hint_t = now_s(); }
   else
   {
      /* back past the batch, to where the user was */
      pop();
      if (cur()->kind == V_BATCH) pop();
   }
}

/* ---- batch ---- */

static void push_batch(const int *list, int n, int make)
{
   view *v;
   if (!n) { toast("Nothing to do", TOAST_WARN); return; }
   if (jobs_busy()) return;
   v = push(V_BATCH);
   v->mode = make;
   jobs_batch(list, n, make);
}

static void draw_batch(view *v)
{
   int i, nok = 0, nbad = 0, nwarn = 0, done = g_task.done, n = g_task.nitems, cur_i = g_task.cur;
   char h3[96];
   float dx = view_dx(v), y, top;
   for (i = 0; i < n; i++)
   {
      int s = g_task.items[i].st;
      nok += s == BI_OK;
      nbad += s == BI_BAD;
      nwarn += s == BI_WARN;
   }
   if (done)
   {
      if (v->mode)
         snprintf(h3, sizeof(h3), "%d %s made%s%s%s", nok + nwarn, nok + nwarn == 1 ? "entry" : "entries",
                  nbad ? ", " : "", nbad ? "" : "", "");
      else
         snprintf(h3, sizeof(h3), "Art found for %d of %d", nok + nwarn, n);
      if (nbad) { char t[96]; snprintf(t, sizeof(t), "%s, %d failed", h3, nbad); snprintf(h3, sizeof(h3), "%s", t); }
      if (g_task.cancel) { char t[96]; snprintf(t, sizeof(t), "%s \xC2\xB7 stopped", h3); snprintf(h3, sizeof(h3), "%s", t); }
   }
   else
      snprintf(h3, sizeof(h3), "%s\xE2\x80\xA6 %d of %d", v->mode ? "Making entries" : "Getting art", nok + nbad + nwarn, n);
   gfx_push(dx, 0, 1, 1);
   gfx_text(XF_800_13, 12, 38 + face_baseline(XF_800_13, 16.25f), h3, C_FG);
   kit_bar(12, 60.25f, 456, n ? (float)(nok + nbad + nwarn) / n : 0);
   /* rows: keep the current one in view */
   top = 0;
   {
      int vis = 7, first = cur_i - 3;
      if (first > n - vis) first = n - vis;
      if (first < 0) first = 0;
      top = first * 24.0f;
   }
   gfx_clip(0, 68.25f, 480, 7 * 24);
   y = 68.25f - top;
   for (i = 0; i < n; i++, y += 24)
   {
      batch_item *it = &g_task.items[i];
      uint32_t sc = it->st == BI_OK ? C_GOOD_TXT : it->st == BI_BAD ? C_BAD_TXT : it->st == BI_PEND ? C_PEND_TXT : it->st == BI_WARN ? C_LOW_TXT : C_FG3;
      if (y < 40 || y > 248) continue;
      if (it->st == BI_PEND) gfx_rrect(12, y, 456, 22, 6, g_th->accent_soft);
      kit_icon(it->game, 20, y + 3, 29, 16, 2, 1);
      gfx_text_right(XF_700_105, 460, y + face_baseline(XF_700_105, 22), it->msg, sc);
      gfx_text_ell(XF_600_115, 57, y + face_baseline(XF_600_115, 22), 400 - text_w(XF_700_105, it->msg) - 50,
                   g_games[it->game].title, C_FG);
   }
   gfx_unclip();
   {
      char foot[200], fb[32];
      fmt_mb(g_st.free_bytes < 0 ? 0 : g_st.free_bytes, fb, sizeof(fb));
      if (done)
      {
         if (v->mode) snprintf(foot, sizeof(foot), "Entries appear in the XMB's Game column after you quit. %s free on %s.", fb, g_st.dev);
         else snprintf(foot, sizeof(foot), "Art is kept until you make the entries; then it goes into GBAdhoc's boxart and hero folders.");
      }
      else if (v->mode)
         snprintf(foot, sizeof(foot), "Art is fetched for games without it, then each entry is written. Nothing is half-written if you stop.");
      else
         snprintf(foot, sizeof(foot), "Each game asks libretro-thumbnails, then GBAdhoc-heroart.");
      {
         int st2[2], ln2[2], nl = text_wrap(XF_400_105, foot, 456, 2, st2, ln2), j;
         for (j = 0; j < nl; j++)
         {
            char line[220];
            snprintf(line, sizeof(line), "%.*s", ln2[j], foot + st2[j]);
            gfx_text(XF_400_105, 12, 248 - 4 - (nl - j) * 13.65f + face_baseline(XF_400_105, 13.65f), line, C_FG3);
         }
      }
   }
   gfx_pop();
   if (done && !v->arrived)
   {
      if (!v->sel_t) v->sel_t = now_s();
      if (g_task.result == RES_NOSPACE)
      {
         v->arrived = 1;
         no_space();
      }
      else if (v->mode && now_s() - v->sel_t > 0.9)
      {
         int lst[64], m = 0;
         v->arrived = 1;
         for (i = 0; i < n && m < 64; i++)
            if (g_task.items[i].st == BI_OK || g_task.items[i].st == BI_WARN)
               lst[m++] = g_task.items[i].game;
         if (m) push_arrive(lst, m);
      }
   }
}

static void key_batch(view *v, int k)
{
   if (g_task.done && (k == K_CROSS || k == K_CIRCLE)) { jobs_ack(); pop(); }
   else if (g_task.done && k == K_TRI && v->mode)
   {
      int lst[64], m = 0, i;
      for (i = 0; i < g_task.nitems && m < 64; i++)
         if (g_task.items[i].st == BI_OK || g_task.items[i].st == BI_WARN)
            lst[m++] = g_task.items[i].game;
      if (m) push_arrive(lst, m);
   }
   else if (!g_task.done && k == K_CIRCLE)
   {
      if (!g_task.cancel) { g_task.cancel = 1; toast("Stopping after the current one", TOAST_WARN); }
   }
}

/* ---- settings ---- */

static const char *REGIONS[3] = { "USA, Europe first", "Europe first", "Japan first" };

static void act_about(int a)
{
   (void)a;
   dlg("GBAdhoc XMB 0.1.0-beta.2", NULL, "Gives each GBA, GB and GBC game its own entry in the XMB's Game column, opening straight into GBAdhoc. An entry is a ~200 KB launcher; ROMs are never copied.\n\nBox art: libretro-thumbnails \xC2\xB7 Hero art: GBAdhoc-heroart");
   add_btn("OK", act_close, 0, 0);
}

typedef struct { char v[64], small[160]; int tog, st, arrow, pick; uint32_t stc; char stt[16]; } srow;

#define NSET 9
#define SET_THEME 5

static int settings_rows(srow *r)
{
   char ssid[64];
   int on = jobs_online();
   memset(r, 0, sizeof(srow) * NSET);
   snprintf(r[0].v, 64, "Folders to scan");
   snprintf(r[0].small, 160, "%d folder%s \xC2\xB7 %d ROMs", 1 + g_st.nfolders, g_st.nfolders ? "s" : "", g_ngames);
   r[0].arrow = 1;
   snprintf(r[1].v, 64, "Rescan the %s", g_st.label);
   snprintf(r[1].small, 160, "after adding ROMs over USB");
   snprintf(r[2].v, 64, "Prefer GBAdhoc's own art");
   snprintf(r[2].small, 160, "use the boxart and hero folders before the internet");
   r[2].tog = 1 + g_prefs.prefer_own;
   snprintf(r[3].v, 64, "Box art region");
   snprintf(r[3].small, 160, "%s", REGIONS[g_prefs.region]);
   snprintf(r[4].v, 64, "Fetch hero art");
   snprintf(r[4].small, 160, "github.com/ShoshinFauteux/GBAdhoc-heroart");
   r[4].tog = 1 + g_prefs.heroes;
   snprintf(r[SET_THEME].v, 64, "Theme");
   snprintf(r[SET_THEME].small, 160, "the app's colours \xC2\xB7 left or right to change");
   r[SET_THEME].pick = 1;
   snprintf(r[SET_THEME].stt, 16, "%s", g_th->name);
   snprintf(r[6].v, 64, "Network");
   jobs_net_info(ssid, sizeof(ssid));
   if (on == 1) snprintf(r[6].small, 160, "Wi-Fi \"%s\" \xC2\xB7 connected", ssid);
   else snprintf(r[6].small, 160, "%s", on < 0 ? "connecting\xE2\x80\xA6" : "not connected");
   r[6].st = 1;
   snprintf(r[6].stt, 16, "%s", on == 1 ? "Online" : on < 0 ? "Connecting" : "Offline");
   r[6].stc = on == 1 ? C_GOOD_TXT : on < 0 ? C_LOW_TXT : C_BAD_TXT;
   snprintf(r[7].v, 64, "GBAdhoc");
   if (g_st.found) snprintf(r[7].small, 160, "%s%s%s", g_st.version, g_st.version[0] ? " \xC2\xB7 " : "", g_st.install);
   else snprintf(r[7].small, 160, "not found on the %s", g_st.label);
   snprintf(r[8].v, 64, "About GBAdhoc XMB");
   snprintf(r[8].small, 160, "version 0.1.0-beta.2");
   return NSET;
}

static void draw_list_rows_settings(view *v)
{
   srow r[NSET];
   int n = settings_rows(r), i, first = 0;
   float y, dx = view_dx(v);
   /* 7 rows fit above the hint bar (58..247) */
   if (v->sel > 6) first = v->sel - 6;
   gfx_push(dx, 0, 1, 1);
   gfx_text(XF_800_13, 12, 38 + face_baseline(XF_800_13, 16.25f), "Settings", C_FG);
   gfx_clip(0, 58, 480, 190);
   y = 58.25f - first * 26;
   for (i = 0; i < n; i++, y += 26)
   {
      int on = i == v->sel;
      float x = 19 + (on ? 2 : 0), by = y + face_baseline(XF_600_12, 24);
      kit_row_bg(12 + (on ? 2 : 0), y, 456, 24, on);
      x += gfx_text(XF_600_12, x, by, r[i].v, C_FG) + 6;
      gfx_text_ell(XF_500_105, x, by - 0.5f, (r[i].tog || r[i].st ? 420 : r[i].pick ? 372 : 445) - x, r[i].small, C_FG3);
      if (r[i].tog) kit_toggle(435 + (on ? 2 : 0), y + 5, r[i].tog == 2);
      else if (r[i].pick)
      {
         /* < Graphite >: the theme's accent as a swatch, its name, the arrows */
         float rx = 461 + (on ? 2 : 0), nw = text_w(XF_700_11, r[i].stt);
         gfx_text_right(XF_700_10, rx, by - 0.5f, "\xE2\x96\xB6", on ? C_FG2 : C_FG_SMALL);
         rx -= text_w(XF_700_10, "\xE2\x96\xB6") + 5;
         gfx_text_right(XF_700_11, rx, by - 0.5f, r[i].stt, C_FG);
         rx -= nw + 6;
         gfx_rrect(rx - 10, y + 7, 10, 10, 3, C_ACCENT);
         rx -= 10 + 5;
         gfx_text_right(XF_700_10, rx, by - 0.5f, "\xE2\x97\x80", on ? C_FG2 : C_FG_SMALL);
      }
      else if (r[i].st) gfx_text_right(XF_700_11, 461 + (on ? 2 : 0), by - 0.5f, r[i].stt, r[i].stc);
      else if (r[i].arrow) gfx_text(XF_400_11, 458.77f + (on ? 2 : 0), by, "\xE2\x80\xBA", C_FG_SMALL);
   }
   gfx_unclip();
   gfx_pop();
}

static void key_settings(view *v, int k)
{
   if (k == K_UP && v->sel > 0) v->sel--;
   else if (k == K_DOWN && v->sel < NSET - 1) v->sel++;
   else if (k == K_CIRCLE || k == K_START) pop();
   else if (v->sel == SET_THEME && (k == K_LEFT || k == K_RIGHT))
   {
      g_prefs.theme = (g_prefs.theme + (k == K_LEFT ? TH_COUNT - 1 : 1)) % TH_COUNT;
      kit_theme(g_prefs.theme);
      prefs_save();
   }
   else if (k == K_CROSS)
   {
      switch (v->sel)
      {
      case 0: push(V_FOLDERS); break;
      case 1: g_nv = 0; push(V_BOOT); jobs_scan(); break;
      case 2: g_prefs.prefer_own = !g_prefs.prefer_own; prefs_save(); break;
      case 3: g_prefs.region = (g_prefs.region + 1) % 3; prefs_save(); break;
      case 4: g_prefs.heroes = !g_prefs.heroes; prefs_save(); break;
      case SET_THEME:
         g_prefs.theme = (g_prefs.theme + 1) % TH_COUNT;
         kit_theme(g_prefs.theme);
         prefs_save();
         break;
      case 6:
         if (jobs_online() == 1)
         {
            char s[64], m[100];
            jobs_net_info(s, sizeof(s));
            snprintf(m, sizeof(m), "Connected to \"%s\"", s);
            toast(m, TOAST_OK);
         }
         else
            ui_netconf_open();
         break;
      case 7: toast(g_st.found ? "Entries launch this GBAdhoc; updating it updates every entry" : "Install GBAdhoc in PSP/GAME/GBAdhoc first", g_st.found ? TOAST_OK : TOAST_WARN); break;
      case 8: act_about(0); break;
      }
   }
}

/* ---- folders ---- */

static void draw_folders(view *v)
{
   int n = 1 + g_st.nfolders + 1, i;
   float y = 58.25f, dx = view_dx(v);
   gfx_push(dx, 0, 1, 1);
   gfx_text(XF_800_13, 12, 38 + face_baseline(XF_800_13, 16.25f), "Folders to scan", C_FG);
   for (i = 0; i < n; i++, y += 26)
   {
      int on = i == v->sel, c = 0, j;
      char v1[PATHCAP], sm[64];
      float x = 19 + (on ? 2 : 0), by = y + face_baseline(XF_600_12, 24);
      if (i == 0)
      {
         snprintf(v1, sizeof(v1), "%s/roms", g_st.found ? g_st.install_rel : "PSP/GAME/GBAdhoc");
         for (j = 0; j < g_ngames; j++) c += strncmp(g_games[j].folder, "roms", 4) == 0;
         snprintf(sm, sizeof(sm), "%d ROMs", c);
      }
      else if (i <= g_st.nfolders)
      {
         snprintf(v1, sizeof(v1), "%s", g_st.folders[i - 1]);
         for (j = 0; j < g_ngames; j++) c += strncasecmp(g_games[j].path, v1, strlen(v1)) == 0;
         snprintf(sm, sizeof(sm), "%d ROMs", c);
      }
      else
      {
         snprintf(v1, sizeof(v1), "Add a folder\xE2\x80\xA6");
         snprintf(sm, sizeof(sm), "anywhere on %s", g_st.dev);
      }
      kit_row_bg(12 + (on ? 2 : 0), y, 456, 24, on);
      if (i == n - 1)
      {
         gfx_circle(x + 4, y + 12, 4, C_ACCENT);
         x += 16;
      }
      x += gfx_text_ell(XF_600_12, x, by, 300, v1, i == n - 1 ? C_WHITE : C_FG) + 6;
      gfx_text(XF_500_105, x, by - 0.5f, sm, C_FG3);
      if (i == 0) gfx_text_right(XF_700_11, 461, by - 0.5f, "GBAdhoc", C_GOOD_TXT);
   }
   {
      char foot[200];
      snprintf(foot, sizeof(foot), "GBAdhoc's roms folder is always scanned. Entries name the ROM relative to it; ROMs elsewhere get an absolute %s path.", g_st.dev);
      {
         int st2[2], ln2[2], nl = text_wrap(XF_400_105, foot, 456, 2, st2, ln2), j;
         for (j = 0; j < nl; j++)
         {
            char line[220];
            snprintf(line, sizeof(line), "%.*s", ln2[j], foot + st2[j]);
            gfx_text(XF_400_105, 12, y + 4 + j * 13.65f + face_baseline(XF_400_105, 13.65f), line, C_FG3);
         }
      }
   }
   gfx_pop();
}

static void key_folders(view *v, int k)
{
   int n = 1 + g_st.nfolders + 1;
   if (k == K_UP && v->sel > 0) v->sel--;
   else if (k == K_DOWN && v->sel + 1 < n) v->sel++;
   else if (k == K_CIRCLE) pop();
   else if (k == K_CROSS)
   {
      if (v->sel == n - 1)
      {
         view *b = push(V_BROWSE);
         snprintf(b->path, sizeof(b->path), "%s", g_st.dev);
      }
      else
      {
         char m[100];
         int c = 0, j;
         for (j = 0; j < g_ngames; j++)
            c += v->sel == 0 ? strncmp(g_games[j].folder, "roms", 4) == 0 : strncasecmp(g_games[j].path, g_st.folders[v->sel - 1], strlen(g_st.folders[v->sel - 1])) == 0;
         snprintf(m, sizeof(m), "%d ROMs in this folder", c);
         toast(m, TOAST_OK);
      }
   }
   else if (k == K_SQ)
   {
      if (v->sel == 0) toast("GBAdhoc's own roms folder cannot be removed", TOAST_WARN);
      else if (v->sel <= g_st.nfolders)
      {
         memmove(g_st.folders[v->sel - 1], g_st.folders[v->sel], (size_t)(g_st.nfolders - v->sel) * PATHCAP);
         g_st.nfolders--;
         prefs_save();
         toast("Folder removed; rescan to update the library", TOAST_OK);
         v->sel = 0;
      }
   }
}

/* ---- browse: the card's folders, two levels ---- */

static char g_br[48][PATHCAP];
static int g_nbr = -1;

static void browse_list(void)
{
   static const char *devs[2] = { "ms0:", "ef0:" };
   int d;
   g_nbr = 0;
   for (d = 0; d < 2; d++)
   {
      char root[16];
      SceUID dh;
      SceIoDirent e;
      snprintf(root, sizeof(root), "%s/", devs[d]);
      dh = sceIoDopen(root);
      if (dh < 0) continue;
      memset(&e, 0, sizeof(e));
      while (sceIoDread(dh, &e) > 0 && g_nbr < 48)
      {
         if (e.d_name[0] != '.' && FIO_S_ISDIR(e.d_stat.st_mode) && !ieq_str(e.d_name, "PSP"))
         {
            char p[PATHCAP];
            SceUID sh;
            SceIoDirent s;
            snprintf(g_br[g_nbr++], PATHCAP, "%s/%s", devs[d], e.d_name);
            snprintf(p, sizeof(p), "%s/%s", devs[d], e.d_name);
            sh = sceIoDopen(p);
            if (sh >= 0)
            {
               memset(&s, 0, sizeof(s));
               while (sceIoDread(sh, &s) > 0 && g_nbr < 48)
               {
                  if (s.d_name[0] != '.' && FIO_S_ISDIR(s.d_stat.st_mode))
                     snprintf(g_br[g_nbr++], PATHCAP, "%s/%s", p, s.d_name);
                  memset(&s, 0, sizeof(s));
               }
               sceIoDclose(sh);
            }
         }
         memset(&e, 0, sizeof(e));
      }
      sceIoDclose(dh);
   }
}

static int folder_scanned(const char *p)
{
   int i;
   for (i = 0; i < g_st.nfolders; i++)
      if (ieq_str(g_st.folders[i], p)) return 1;
   return 0;
}

static void draw_browse(view *v)
{
   int i, first;
   float y, dx = view_dx(v);
   char h[32];
   if (g_nbr < 0) browse_list();
   gfx_push(dx, 0, 1, 1);
   snprintf(h, sizeof(h), "%s/", g_st.dev);
   gfx_text(XF_800_13, 12, 38 + face_baseline(XF_800_13, 16.25f), h, C_FG);
   if (!g_nbr)
      gfx_text(XF_400_11, 12, 70, "No folders outside PSP/ on the card.", C_FG3);
   first = v->sel > 6 ? v->sel - 6 : 0;
   gfx_clip(0, 58, 480, 190);
   y = 58.25f - first * 26;
   for (i = 0; i < g_nbr; i++, y += 26)
   {
      int on = i == v->sel;
      float by = y + face_baseline(XF_600_12, 24);
      kit_row_bg(12 + (on ? 2 : 0), y, 456, 24, on);
      gfx_text_ell(XF_600_12, 19 + (on ? 2 : 0), by, 360, g_br[i], C_FG);
      if (folder_scanned(g_br[i])) gfx_text_right(XF_700_11, 461, by - 0.5f, "Scanned", C_GOOD_TXT);
   }
   gfx_unclip();
   gfx_pop();
}

static void key_browse(view *v, int k)
{
   if (k == K_UP && v->sel > 0) v->sel--;
   else if (k == K_DOWN && v->sel + 1 < g_nbr) v->sel++;
   else if (k == K_CIRCLE) { g_nbr = -1; pop(); }
   else if (k == K_CROSS && g_nbr > 0)
   {
      char m[300];
      if (folder_scanned(g_br[v->sel])) { toast("Already scanned", TOAST_WARN); return; }
      if (g_st.nfolders >= MAX_FOLDERS) { toast("That is as many folders as GBAdhoc XMB keeps", TOAST_WARN); return; }
      snprintf(g_st.folders[g_st.nfolders++], PATHCAP, "%s", g_br[v->sel]);
      prefs_save();
      snprintf(m, sizeof(m), "Added %s. Rescan to pick up its ROMs.", g_br[v->sel]);
      toast(m, TOAST_OK);
      g_nbr = -1;
      pop();
   }
}

/* ------------------------------------------------------------ overlays ---- */

static void draw_steps(float x, float y, float w, int maxn)
{
   int i, n;
   task_lock();
   n = g_task.nsteps;
   for (i = n > maxn ? n - maxn : 0; i < n; i++)
   {
      int st2[2], ln2[2], nl = text_wrap(XF_400_115, g_task.steps[i].msg, w - 20, 2, st2, ln2), j;
      kit_step_icon(x, y + 1.47f, g_task.steps[i].st);
      for (j = 0; j < nl; j++)
      {
         char line[200];
         snprintf(line, sizeof(line), "%.*s", ln2[j], g_task.steps[i].msg + st2[j]);
         gfx_text_ell(XF_400_115, x + 20, y + j * 14.94f + face_baseline(XF_400_115, 14.94f), w - 20, line,
                      g_task.steps[i].st == STEP_PEND ? C_WHITE : C_FG2);
      }
      y += nl * 14.94f + 3;
   }
   task_unlock();
}

static int steps_height(float w)
{
   int i, n = 0;
   float h = 0;
   task_lock();
   for (i = 0; i < g_task.nsteps; i++)
   {
      int st2[2], ln2[2], nl = text_wrap(XF_400_115, g_task.steps[i].msg, w - 20, 2, st2, ln2);
      h += nl * 14.94f + 3;
      n++;
   }
   task_unlock();
   return (int)h;
}

static void draw_btns(float right, float y)
{
   int i;
   float w = 0, x;
   for (i = 0; i < g_o.nb; i++)
      w += kit_button_w(g_o.b[i].label) + (i == g_o.sel ? 21 : 0) + (i ? 6 : 0);
   x = right - w;
   for (i = 0; i < g_o.nb; i++)
      x += kit_button(x, y, g_o.b[i].label, i == g_o.sel, g_o.b[i].danger) + 6;
}

static void fill_fetch_buttons(void)
{
   game *g = &g_games[g_o.game];
   g_o.nb = 0;
   if (g_task.nbox_found || g_task.hero_found)
      add_btn("Review art", act_review, g_task.nbox_found ? 0 : 1, 0);
   if (g_task.result == RES_OFFLINE)
   {
      add_btn("Network settings", act_netconf, 0, 0);
      add_btn("Make without art", act_make_after, 0, 0);
   }
   if (g_task.result == RES_RATE)
      add_btn("Make with what was found", act_make_after, 0, 0);
   add_btn(g_task.result == RES_OK ? "Close" : "Later", act_close_task, 0, 0);
   (void)g;
}

static void draw_overlay(void)
{
   float pop_t = (float)((now_s() - g_o.t0) / 0.25);
   if (g_o.kind == O_NONE) return;
   kit_scrim(1);
   if (g_o.kind == O_CONFIRM)
   {
      int st2[8], ln2[8], nl = 0, j;
      float w = 380, bodyh, h;
      char body[400];
      /* paragraphs: "\n\n" splits */
      snprintf(body, sizeof(body), "%s", g_o.body);
      {
         char *nn = strstr(body, "\n\n");
         int nl1, st3[8], ln3[8];
         if (nn) *nn = 0;
         nl = text_wrap(XF_400_12, body, w - 28, 8, st2, ln2);
         bodyh = nl * 16.8f;
         if (nn)
         {
            nl1 = text_wrap(XF_400_12, nn + 2, w - 28, 4, st3, ln3);
            bodyh += 16.8f + nl1 * 16.8f;
         }
      }
      h = 11 + 17.75f + (g_o.sub[0] ? 13.75f + 6 : 6) + bodyh + 8 + 26 + 10;
      kit_panel(240 - w / 2, 136 - h / 2, w, h, pop_t);
      gfx_text(XF_800_14, 14, 11 + face_baseline(XF_800_14, 17.75f), g_o.title, C_FG);
      {
         float y = 11 + 17.75f;
         if (g_o.sub[0])
         {
            gfx_text_ell(XF_400_11, 14, y + face_baseline(XF_400_11, 13.75f), w - 28, g_o.sub, C_FG3);
            y += 13.75f + 6;
         }
         else
            y += 6;
         {
            char *nn = strstr(body, "\0");
            (void)nn;
         }
         for (j = 0; j < nl; j++)
         {
            char line[300];
            snprintf(line, sizeof(line), "%.*s", ln2[j], body + st2[j]);
            gfx_text(XF_400_12, 14, y + j * 16.8f + face_baseline(XF_400_12, 16.8f), line, C_FG2);
         }
         y += nl * 16.8f;
         {
            char *nn = strstr(g_o.body, "\n\n");
            if (nn)
            {
               int st3[4], ln3[4], m = text_wrap(XF_400_12, nn + 2, w - 28, 4, st3, ln3);
               y += 16.8f;
               for (j = 0; j < m; j++)
               {
                  char line[300];
                  snprintf(line, sizeof(line), "%.*s", ln3[j], nn + 2 + st3[j]);
                  gfx_text(XF_400_12, 14, y + j * 16.8f + face_baseline(XF_400_12, 16.8f), line, C_FG2);
               }
            }
         }
      }
      draw_btns(w - 14, h - 10 - 26);
      kit_panel_end();
   }
   else if (g_o.kind == O_SHEET)
   {
      float w = 400, h, y;
      int i;
      h = 11 + 17.75f + 6 + 4;
      for (i = 0; i < g_o.nb; i++)
      {
         int st2[2], ln2[2];
         h += 8 + 15 + (g_o.b[i].small[0] ? text_wrap(XF_500_105, g_o.b[i].small, w - 28 - 22, 2, st2, ln2) * 13.1f : 0) + 2;
      }
      h += 8 + 26 + 10;
      if (h > 250) h = 250;
      kit_panel(240 - w / 2, 136 - h / 2, w, h, pop_t);
      gfx_text(XF_800_14, 14, 11 + face_baseline(XF_800_14, 17.75f), g_o.title, C_FG);
      y = 11 + 17.75f + 6 + 2;
      for (i = 0; i < g_o.nb; i++)
      {
         int st2[2], ln2[2], nl = g_o.b[i].small[0] ? text_wrap(XF_500_105, g_o.b[i].small, w - 28 - 22, 2, st2, ln2) : 0, j;
         float rh = 8 + 15 + nl * 13.1f;
         gfx_push(0, 0, 1, g_o.b[i].dim ? 0.45f : 1);
         kit_row_bg(17, y, w - 34, rh, i == g_o.sel);
         gfx_circle(25 + 4, y + 4 + 7.5f, 4, C_ACCENT);
         gfx_text(XF_600_12, 41, y + 4 + face_baseline(XF_600_12, 15), g_o.b[i].label, C_WHITE);
         for (j = 0; j < nl; j++)
         {
            char line[200];
            snprintf(line, sizeof(line), "%.*s", ln2[j], g_o.b[i].small + st2[j]);
            gfx_text_ell(XF_500_105, 41, y + 4 + 15 + j * 13.1f + face_baseline(XF_500_105, 13.1f), w - 28 - 30, line, C_FG3);
         }
         gfx_pop();
         y += rh + 2;
      }
      kit_button(w - 14 - kit_button_w("Close"), h - 10 - 26, "Close", 0, 0);
      kit_panel_end();
   }
   else if (g_o.kind == O_FETCH || g_o.kind == O_MAKE || g_o.kind == O_TASK)
   {
      game *g = &g_games[g_o.game];
      int done = g_task.done;
      float w = 380, sh = (float)steps_height(352), h, y;
      const char *title;
      char sub[160];
      int thumbs = 0, i;
      if (g_o.kind == O_FETCH)
      {
         title = !done ? "Getting art" : g_task.result == RES_OK ? "Art found" : g_task.result == RES_OFFLINE ? "Offline" :
                 g_task.result == RES_RATE ? "Rate-limited by GitHub" : "No art found";
         snprintf(sub, sizeof(sub), "%s", done ? "libretro-thumbnails \xC2\xB7 GBAdhoc-heroart" : "Asking libretro-thumbnails and GBAdhoc-heroart\xE2\x80\xA6");
         if (done)
            for (i = 0; i < g->ncand; i++) thumbs += g->cand[i].cache[0] != 0;
         if (done && g_o.nb == 0) fill_fetch_buttons();
      }
      else if (g_o.kind == O_MAKE)
      {
         title = !done ? (g_o.mode ? "Updating entry" : "Making entry") : g_task.result == RES_OK ? "Entry made" : "Could not make the entry";
         snprintf(sub, sizeof(sub), "%s/PSP/GAME/\xE2\x80\xA6 \xC2\xB7 the ROM stays where it is", g_st.dev);
         if (done && g_task.result == RES_NOSPACE && g_o.done_at == 0)
         {
            g_o.done_at = now_s();
         }
      }
      else
      {
         title = !done ? "Removing entry" : g_task.result == RES_OK ? "Entry removed" : "Could not remove the entry";
         snprintf(sub, sizeof(sub), "PSP/GAME/\xE2\x80\xA6");
      }
      h = 11 + 17.75f + 13.75f + 6 + sh + (thumbs ? 48 : 0) + (done ? 8 + 26 : 8 + 4 + 4) + 10;
      if (h < 90) h = 90;
      if (h > 244) h = 244;
      kit_panel(240 - w / 2, 136 - h / 2, w, h, pop_t);
      gfx_text(XF_800_14, 14, 11 + face_baseline(XF_800_14, 17.75f), title, C_FG);
      gfx_text_right(XF_600_11, w - 14, 11 + 4 + face_baseline(XF_600_11, 13.75f), g->title, C_FG3);
      gfx_text_ell(XF_400_11, 14, 11 + 17.75f + face_baseline(XF_400_11, 13.75f), w - 28, sub, C_FG3);
      y = 11 + 17.75f + 13.75f + 6;
      draw_steps(14, y, 352, 9);
      y += sh;
      if (thumbs)
      {
         float tx = 14;
         y += 8;
         for (i = 0; i < g->ncand && tx < w - 80; i++)
         {
            box_cand *c = &g->cand[i];
            const img_slot *th;
            char pc[8];
            float pw;
            if (!c->cache[0]) continue;
            th = jobs_thumb(c->cache, 0, 64, 40, 0);
            gfx_push(0, 0, 1, c->below ? 0.5f : 1);
            gfx_rrect(tx, y, 64, 40, 4, 0xFF000000u);
            if (th && th->px)
            {
               gtex t;
               memset(&t, 0, sizeof(t));
               t.px = th->px; t.w = 64; t.h = 40; t.tw = 64; t.th = 64; t.bw = 64; t.psm = 3;
               gfx_tex_rounded(&t, 0, 0, 64, 40, tx, y, 64, 40, 4, C_WHITE, 1, c->below ? 0 : kit_panel_colour());
            }
            gfx_pop();
            snprintf(pc, sizeof(pc), "%d%%", c->conf);
            pw = text_w(XF_800_10, pc) + 8;
            gfx_rrect(tx + 64 - 2 - pw, y + 40 - 2 - 16, pw, 16, 3, 0xBF000000u);
            gfx_text(XF_800_10, tx + 64 - 2 - pw + 4, y + 40 - 2 - 16 + face_baseline(XF_800_10, 16), pc, c->below ? C_BAD_TXT : C_GOOD_TXT);
            tx += 70;
         }
         y += 40;
      }
      if (done)
      {
         if (g_o.kind == O_FETCH)
            draw_btns(w - 14, h - 10 - 26);
         else
            kit_button(w - 14 - kit_button_w(g_task.result == RES_OK ? "Done" : "Close") - 21, h - 10 - 26,
                       g_task.result == RES_OK ? "Done" : "Close", 1, 0);
      }
      else
         kit_bar_indeterminate(14, h - 10 - 8, w - 28);
      kit_panel_end();
      /* a made entry lands in the XMB after a beat */
      if (g_o.kind == O_MAKE && done && g_task.result == RES_OK)
      {
         if (g_o.done_at == 0) g_o.done_at = now_s();
         else if (now_s() - g_o.done_at > 0.65)
         {
            int gi = g_o.game;
            close_overlay();
            jobs_ack();
            push_arrive(&gi, 1);
         }
      }
      if (g_o.kind == O_TASK && done)
      {
         int ok = g_task.result == RES_OK;
         close_overlay();
         jobs_ack();
         toast(ok ? "Entry removed" : "Could not remove the entry", ok ? TOAST_OK : TOAST_BAD);
      }
      if (g_o.kind == O_FETCH && done && g_task.result == RES_OK && g_o.done_at == 0)
      {
         g_o.done_at = now_s();
         toast("Art fetched and applied to the entry", TOAST_OK);
      }
   }
   else if (g_o.kind == O_BATCHCONFIRM)
   {
      float w = 430, h, y;
      int i, per = 6, rows = (g_o.nlist + per - 1) / per, st2[3], ln2[3], nl;
      if (rows > 2) rows = 2;
      nl = text_wrap(XF_400_12, g_o.body, w - 28, 3, st2, ln2);
      h = 11 + 17.75f + 13.75f + 6 + rows * (32 + 3 + 24 + 4) + 2 + nl * 16.8f + 8 + 26 + 10;
      if (h > 256) h = 256;
      kit_panel(240 - w / 2, 136 - h / 2, w, h, pop_t);
      gfx_text(XF_800_14, 14, 11 + face_baseline(XF_800_14, 17.75f), g_o.title, C_FG);
      gfx_text_ell(XF_400_11, 14, 11 + 17.75f + face_baseline(XF_400_11, 13.75f), w - 28, g_o.sub, C_FG3);
      y = 11 + 17.75f + 13.75f + 6 + 4;
      for (i = 0; i < g_o.nlist && i < per * rows; i++)
      {
         int gi = g_o.list[i], col = i % per, row = i / per, s2[2], l2[2], m, j;
         float x = 14 + col * 67, yy = y + row * 63;
         game *g = &g_games[gi];
         if (i == per * rows - 1 && g_o.nlist > per * rows)
         {
            char more[16];
            snprintf(more, sizeof(more), "+%d", g_o.nlist - per * rows + 1);
            gfx_rrect(x + 1, yy, 58, 32, 3, 0x14FFFFFFu);
            gfx_text_center(XF_700_12, x + 30, yy + 20, more, C_FG2);
            break;
         }
         kit_icon(gi, x + 1, yy, 58, 32, 3, 1);
         gfx_rring(x + 1, yy, 58, 32, 3, game_has_icon(g) ? 1 : 1.5f, game_has_icon(g) ? 0x1FFFFFFFu : C_WARN);
         m = text_wrap(XF_600_10, g->title, 60, 2, s2, l2);
         for (j = 0; j < m; j++)
         {
            char line[160];
            snprintf(line, sizeof(line), "%.*s", l2[j], g->title + s2[j]);
            if (j == 1 && m == 2 && text_w(XF_600_10, line) > 60)
               gfx_text_ell(XF_600_10, x + 30 - 30, yy + 35 + j * 12 + face_baseline(XF_600_10, 12), 60, line, C_FG2);
            else
               gfx_text_center(XF_600_10, x + 30, yy + 35 + j * 12 + face_baseline(XF_600_10, 12), line, C_FG2);
         }
      }
      y += rows * 63 + 2;
      for (i = 0; i < nl; i++)
      {
         char line[300];
         snprintf(line, sizeof(line), "%.*s", ln2[i], g_o.body + st2[i]);
         gfx_text(XF_400_12, 14, y + i * 16.8f + face_baseline(XF_400_12, 16.8f), line, C_FG2);
      }
      draw_btns(w - 14, h - 10 - 26);
      kit_panel_end();
   }
}

static void key_overlay(int k)
{
   if (g_o.kind == O_FETCH || g_o.kind == O_MAKE || g_o.kind == O_TASK)
   {
      if (!g_task.done) return;
      if (g_o.kind == O_MAKE)
      {
         if (k == K_CROSS || k == K_CIRCLE)
         {
            int gi = g_o.game, ok = g_task.result == RES_OK;
            close_overlay();
            jobs_ack();
            if (ok) push_arrive(&gi, 1);
            else if (g_task.result == RES_NOSPACE) no_space();
         }
         return;
      }
      if (g_o.kind == O_FETCH)
      {
         if (k == K_CIRCLE) { act_close_task(0); return; }
      }
   }
   if (k == K_LEFT || k == K_UP) { if (g_o.sel > 0) g_o.sel--; }
   else if (k == K_RIGHT || k == K_DOWN) { if (g_o.sel + 1 < g_o.nb) g_o.sel++; }
   else if (k == K_CROSS && g_o.nb)
   {
      btn_t *b = &g_o.b[g_o.sel];
      if (b->dim) { toast(b->why[0] ? b->why : "Not available", TOAST_WARN); return; }
      {
         act_fn fn = b->fn;
         int arg = b->arg, kind = g_o.kind;
         if (kind == O_SHEET || kind == O_CONFIRM)
            close_overlay();
         if (fn) fn(arg);
      }
   }
   else if (k == K_CIRCLE || (k == K_TRI && g_o.kind == O_SHEET))
   {
      if (g_o.kind == O_FETCH) act_close_task(0);
      else close_overlay();
   }
}

/* ------------------------------------------------------------ chrome ---- */

static void crumb(view *v, char *dim, char *bold)
{
   dim[0] = bold[0] = 0;
   switch (v->kind)
   {
   case V_HOME: strcpy(bold, "Library"); break;
   case V_GAME: snprintf(bold, 160, "%s", g_games[v->game].title); break;
   case V_PICK: snprintf(dim, 160, "%s", g_games[v->game].title); strcpy(bold, v->which ? "Hero art" : "Box art"); break;
   case V_TITLE: snprintf(dim, 160, "%s", g_games[v->game].title); strcpy(bold, "Title"); break;
   case V_BATCH: snprintf(bold, 160, v->mode ? "Making %d entries" : "Getting art for %d games", g_task.nitems); break;
   case V_SETTINGS: strcpy(bold, "Settings"); break;
   case V_FOLDERS: strcpy(dim, "Settings"); strcpy(bold, "Folders to scan"); break;
   case V_BROWSE: strcpy(dim, "Folders"); strcpy(bold, "Add a folder"); break;
   }
}

static int hints_for(view *v, hint *h)
{
   switch (v->kind)
   {
   case V_HOME: return hints_home(h);
   case V_GAME:
      h[0].g = HG_CROSS; h[0].label = "Select"; h[1].g = HG_TRI; h[1].label = "Full preview"; h[2].g = HG_CIRCLE; h[2].label = "Back";
      return 3;
   case V_PICK:
      h[0].g = HG_CROSS; h[0].label = "Use this"; h[1].g = HG_SQ; h[1].label = jobs_online() == 1 ? "Look up on the internet" : "Offline";
      h[2].g = HG_CIRCLE; h[2].label = "Back";
      return 3;
   case V_TITLE:
      h[0].g = HG_CROSS; h[0].label = v->zone ? "Type" : "Use suggestion"; h[1].g = HG_TRI; h[1].label = "Done";
      h[2].g = HG_SQ; h[2].label = "Backspace"; h[3].g = HG_CIRCLE; h[3].label = "Cancel";
      return 4;
   case V_BATCH:
      if (!g_task.done) { h[0].g = HG_CIRCLE; h[0].label = "Stop after this one"; return 1; }
      if (v->mode) { h[0].g = HG_TRI; h[0].label = "See them in the XMB"; h[1].g = HG_CROSS; h[1].label = "Back to library"; return 2; }
      h[0].g = HG_CROSS; h[0].label = "Back to library";
      return 1;
   case V_SETTINGS: h[0].g = HG_CROSS; h[0].label = "Change"; h[1].g = HG_CIRCLE; h[1].label = "Back"; return 2;
   case V_FOLDERS: h[0].g = HG_CROSS; h[0].label = "Open"; h[1].g = HG_SQ; h[1].label = "Remove"; h[2].g = HG_CIRCLE; h[2].label = "Back"; return 3;
   case V_BROWSE: h[0].g = HG_CROSS; h[0].label = "Add this folder"; h[1].g = HG_CIRCLE; h[1].label = "Back"; return 2;
   }
   return 0;
}

static int immersive(int kind)
{
   return kind == V_BOOT || kind == V_FULL || kind == V_ARRIVE;
}

/* ------------------------------------------------------------ entry points ---- */

void ui_toast_ext(const char *msg, int tone)
{
   toast(msg, tone == 2 ? TOAST_BAD : tone == 1 ? TOAST_WARN : TOAST_OK);
}

void ui_init(void)
{
   kit_init();
   g_nv = 0;
   push(V_BOOT);
   jobs_scan();
}

void ui_draw(void)
{
   view *v = cur();
   char dim[160], bold[160];
   hint h[6];
   int nh;
   kit_background();
   if (!v) return;
   switch (v->kind)
   {
   case V_BOOT: draw_boot(v); break;
   case V_HOME: draw_home(v); break;
   case V_GAME: draw_game(v); break;
   case V_PICK: draw_pick(v); break;
   case V_TITLE: draw_title(v); break;
   case V_FULL: draw_full(v); break;
   case V_ARRIVE: draw_arrive(v); break;
   case V_BATCH: draw_batch(v); break;
   case V_SETTINGS: draw_list_rows_settings(v); break;
   case V_FOLDERS: draw_folders(v); break;
   case V_BROWSE: draw_browse(v); break;
   }
   v = cur();
   if (!immersive(v->kind))
   {
      crumb(v, dim, bold);
      kit_top(dim, bold);
      nh = hints_for(v, h);
      kit_hints(h, nh);
   }
   draw_overlay();
   if (now_s() - g_toast_t < 2.4 && !immersive(cur()->kind))
      kit_toast(g_toast, g_toast_col, (float)(now_s() - g_toast_t));
}

void ui_key(int k)
{
   view *v = cur();
   if (!v) return;
   if (g_o.kind != O_NONE) { key_overlay(k); return; }
   switch (v->kind)
   {
   case V_HOME: key_home(v, k); break;
   case V_GAME: key_game(v, k); break;
   case V_PICK: key_pick(v, k); break;
   case V_TITLE: key_title(v, k); break;
   case V_FULL: key_full(v, k); break;
   case V_ARRIVE: key_arrive(v, k); break;
   case V_BATCH: key_batch(v, k); break;
   case V_SETTINGS: key_settings(v, k); break;
   case V_FOLDERS: key_folders(v, k); break;
   case V_BROWSE: key_browse(v, k); break;
   }
}

/* harness hooks: open a state directly (like the design's ?demo=) */
int ui_on_home(void)
{
   return cur() && cur()->kind == V_HOME && g_o.kind == O_NONE;
}

int ui_find_game(const char *needle)
{
   int i;
   for (i = 0; i < g_ngames; i++)
      if (strstr(g_games[i].file, needle) || strstr(g_games[i].title, needle))
         return i;
   return -1;
}

void ui_demo(const char *what, const char *arg)
{
   char needle[96];
   int gi, i;
   /* '_' stands for a space (the script splits on spaces) */
   snprintf(needle, sizeof(needle), "%s", arg ? arg : "");
   for (i = 0; needle[i]; i++)
      if (needle[i] == '_') needle[i] = ' ';
   gi = needle[0] ? ui_find_game(needle) : (g_ngames ? 0 : -1);
   LOG("demo %s %s -> %d", what, arg ? arg : "", gi);
   if (strcmp(what, "game") == 0 && gi >= 0) open_game(gi);
   else if (strcmp(what, "full") == 0 && gi >= 0) { view *f = push(V_FULL); f->game = gi; f->bg_on = 1; f->hint_t = now_s(); }
   else if (strcmp(what, "pick") == 0 && gi >= 0) { open_game(gi); push_pick(gi, 0); }
   else if (strcmp(what, "hero") == 0 && gi >= 0) { open_game(gi); push_pick(gi, 1); }
   else if (strcmp(what, "title") == 0 && gi >= 0) { open_game(gi); { view *t = push(V_TITLE); t->game = gi; } }
   else if (strcmp(what, "settings") == 0) push(V_SETTINGS);
   else if (strcmp(what, "folders") == 0) { push(V_SETTINGS); push(V_FOLDERS); }
   else if (strcmp(what, "actions") == 0) lib_actions();
   else if (strcmp(what, "fetch") == 0 && gi >= 0) { open_game(gi); fetch_flow(gi); }
   else if (strcmp(what, "make") == 0 && gi >= 0) { open_game(gi); make_flow(gi, 0); }
   else if (strcmp(what, "remove") == 0 && gi >= 0 && g_games[gi].entry >= 0) { open_game(gi); remove_flow(gi); }
   else if (strcmp(what, "confirm") == 0) act_make_all(0);
   else if (strcmp(what, "arrive") == 0 && gi >= 0) push_arrive(&gi, 1);
   else if (strcmp(what, "filter") == 0) { g_filter = atoi(arg ? arg : "3") % F_COUNT; g_hsel = 0; }
   else if (strcmp(what, "select") == 0 && gi >= 0)
   {
      int list[MAX_GAMES], n = lib_list(g_filter, list), i;
      for (i = 0; i < n; i++) if (list[i] == gi) { g_hsel = i; cur()->sel_t = 0; cur()->inited = 0; }
   }
}
