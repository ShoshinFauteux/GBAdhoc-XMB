/* gfx.c -- see gfx.h. */
#include "gfx.h"

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gen/xmb_fonts.h"

#define BUF_W 512
#define FB_BYTES (BUF_W * SCR_H * 4)

static unsigned int __attribute__((aligned(64))) g_list[2][192 * 1024];
static int g_cur_list;
static void *g_draw_rel;        /* VRAM-relative draw buffer */
static void *g_disp_rel;
static uint32_t __attribute__((aligned(16))) g_clut[256];
static gtex g_target;
static int g_in_target;
static void free_late(void);
static float clampf_(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static const void *g_bound;
static int g_bound_filter = -1, g_bound_tcc = -1;

/* ---- colour ---------------------------------------------------------- */

static inline uint32_t to_ge(uint32_t c)
{
   /* ARGB -> ABGR */
   return (c & 0xFF00FF00u) | (c >> 16 & 0xFF) | (c & 0xFF) << 16;
}

uint32_t col_mix(uint32_t a, uint32_t b, float t)
{
   int i;
   uint32_t o = 0;
   if (t <= 0) return a;
   if (t >= 1) return b;
   for (i = 0; i < 32; i += 8)
   {
      float x = (float)(a >> i & 0xFF), y = (float)(b >> i & 0xFF);
      o |= (uint32_t)(x + (y - x) * t + 0.5f) << i;
   }
   return o;
}

uint32_t col_alpha(uint32_t c, float k)
{
   float a = (float)(c >> 24) * k;
   if (a < 0) a = 0;
   if (a > 255) a = 255;
   return (c & 0xFFFFFF) | (uint32_t)(a + 0.5f) << 24;
}

/* ---- transform stack --------------------------------------------------- */

typedef struct { float tx, ty, s, a; int clip; float cx0, cy0, cx1, cy1; } xf_state;
static xf_state g_st[24];
static int g_sp;
#define ST (g_st[g_sp])

static void apply_scissor(void)
{
   int x0 = (int)floorf(ST.cx0), y0 = (int)floorf(ST.cy0);
   int x1 = (int)ceilf(ST.cx1), y1 = (int)ceilf(ST.cy1);
   if (x0 < 0) x0 = 0;
   if (y0 < 0) y0 = 0;
   if (x1 > SCR_W) x1 = SCR_W;
   if (y1 > SCR_H) y1 = SCR_H;
   if (x1 < x0) x1 = x0;
   if (y1 < y0) y1 = y0;
   sceGuScissor(x0, y0, x1 - x0, y1 - y0);   /* pspgu: x, y, WIDTH, HEIGHT */
}

void gfx_push(float tx, float ty, float s, float alpha)
{
   xf_state n = ST;
   if (g_sp + 1 >= (int)(sizeof(g_st) / sizeof(g_st[0])))
      return;
   n.tx = ST.tx + ST.s * tx;
   n.ty = ST.ty + ST.s * ty;
   n.s = ST.s * s;
   n.a = ST.a * alpha;
   g_st[++g_sp] = n;
}

void gfx_pop(void)
{
   int clip = ST.clip;
   if (g_sp > 0)
      g_sp--;
   if (clip)
      apply_scissor();
}

void gfx_clip(float x, float y, float w, float h)
{
   float x0 = ST.tx + ST.s * x, y0 = ST.ty + ST.s * y;
   float x1 = x0 + ST.s * w, y1 = y0 + ST.s * h;
   xf_state n = ST;
   if (x0 > n.cx0) n.cx0 = x0;
   if (y0 > n.cy0) n.cy0 = y0;
   if (x1 < n.cx1) n.cx1 = x1;
   if (y1 < n.cy1) n.cy1 = y1;
   n.clip = 1;
   if (g_sp + 1 < (int)(sizeof(g_st) / sizeof(g_st[0])))
      g_st[++g_sp] = n;
   apply_scissor();
}

void gfx_unclip(void)
{
   gfx_pop();
}

#define X(v) (ST.tx + ST.s * (v))
#define Y(v) (ST.ty + ST.s * (v))
#define S(v) (ST.s * (v))
#define C(c) to_ge(col_alpha((c), ST.a))

/* ---- init / frames ------------------------------------------------------ */

void gfx_init(void)
{
   int i;
   for (i = 0; i < 256; i++)
      g_clut[i] = (uint32_t)i << 24 | 0xFFFFFF;
   sceKernelDcacheWritebackRange(g_clut, sizeof(g_clut));
   g_draw_rel = (void *)0;
   g_disp_rel = (void *)FB_BYTES;
   sceGuInit();
   sceGuStart(GU_DIRECT, g_list[0]);
   sceGuDrawBuffer(GU_PSM_8888, g_draw_rel, BUF_W);
   sceGuDispBuffer(SCR_W, SCR_H, g_disp_rel, BUF_W);
   sceGuDepthBuffer((void *)(FB_BYTES * 3), BUF_W);   /* unused: no depth test */
   sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
   sceGuViewport(2048, 2048, SCR_W, SCR_H);
   sceGuScissor(0, 0, SCR_W, SCR_H);
   sceGuEnable(GU_SCISSOR_TEST);
   sceGuDisable(GU_DEPTH_TEST);
   sceGuDisable(GU_CULL_FACE);
   sceGuEnable(GU_BLEND);
   sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
   sceGuShadeModel(GU_SMOOTH);
   sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
   sceGuTexWrap(GU_CLAMP, GU_CLAMP);
   sceGuEnable(GU_DITHER);
   sceGuFinish();
   sceGuSync(0, 0);
   sceDisplayWaitVblankStart();
   sceGuDisplay(GU_TRUE);

   g_target.px = (void *)((char *)sceGeEdramGetAddr() + FB_BYTES * 2);
   g_target.w = SCR_W;
   g_target.h = SCR_H;
   g_target.tw = 512;
   g_target.th = 512;
   g_target.bw = BUF_W;
   g_target.psm = GU_PSM_8888;
   g_st[0].tx = g_st[0].ty = 0;
   g_st[0].s = g_st[0].a = 1;
   g_st[0].cx0 = g_st[0].cy0 = 0;
   g_st[0].cx1 = SCR_W;
   g_st[0].cy1 = SCR_H;
}

static void reset_state(uint32_t clear)
{
   g_sp = 0;
   sceGuScissor(0, 0, SCR_W, SCR_H);
   sceGuClearColor(to_ge(clear));
   sceGuClear(GU_COLOR_BUFFER_BIT);
}

void gfx_frame_begin(uint32_t clear)
{
   g_cur_list ^= 1;
   sceGuStart(GU_DIRECT, g_list[g_cur_list]);
   reset_state(clear);
}

void gfx_frame_end_with(void (*between)(void))
{
   sceGuFinish();
   sceGuSync(0, 0);
   free_late();
   if (between)
      between();
   sceDisplayWaitVblankStart();
   g_disp_rel = g_draw_rel;
   g_draw_rel = sceGuSwapBuffers();
   g_bound = NULL;       /* the dialog changed the GE's texture state */
   g_bound_filter = g_bound_tcc = -1;
}

void gfx_frame_end(void)
{
   sceGuFinish();
   sceGuSync(0, 0);
   free_late();
   sceDisplayWaitVblankStart();
   g_disp_rel = g_draw_rel;
   g_draw_rel = sceGuSwapBuffers();
}

void gfx_target_begin(void)
{
   sceGuDrawBufferList(GU_PSM_8888, (void *)(FB_BYTES * 2), BUF_W);
   g_in_target = 1;
   reset_state(0xFF000000);
}

void gfx_target_end(void)
{
   sceGuDrawBufferList(GU_PSM_8888, g_draw_rel, BUF_W);
   g_in_target = 0;
   g_sp = 0;
   sceGuScissor(0, 0, SCR_W, SCR_H);
   sceGuTexFlush();
}

const gtex *gfx_target_tex(void)
{
   return &g_target;
}

const uint32_t *gfx_front_pixels(int *stride)
{
   *stride = BUF_W;
   /* uncached view: the GE wrote it, the CPU's cache knows nothing */
   return (const uint32_t *)(((uintptr_t)sceGeEdramGetAddr() + (uintptr_t)g_disp_rel) | 0x40000000u);
}

/* ---- untextured --------------------------------------------------------- */

typedef struct { uint32_t c; float x, y, z; } cv;
typedef struct { float u, v; uint32_t c; float x, y, z; } tv;

#define CV_FMT (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
#define TV_FMT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

void gfx_rect(float x, float y, float w, float h, uint32_t c)
{
   cv *v;
   if (w <= 0 || h <= 0 || ((c >> 24) == 0))
      return;
   v = (cv *)sceGuGetMemory(2 * sizeof(cv));
   v[0].c = v[1].c = C(c);
   v[0].x = X(x); v[0].y = Y(y); v[0].z = 0;
   v[1].x = X(x + w); v[1].y = Y(y + h); v[1].z = 0;
   sceGuDisable(GU_TEXTURE_2D);
   sceGuDrawArray(GU_SPRITES, CV_FMT, 2, 0, v);
}

void gfx_grad(float x, float y, float w, float h, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br)
{
   cv *v;
   if (w <= 0 || h <= 0)
      return;
   v = (cv *)sceGuGetMemory(4 * sizeof(cv));
   v[0].c = C(tl); v[0].x = X(x); v[0].y = Y(y); v[0].z = 0;
   v[1].c = C(tr); v[1].x = X(x + w); v[1].y = Y(y); v[1].z = 0;
   v[2].c = C(bl); v[2].x = X(x); v[2].y = Y(y + h); v[2].z = 0;
   v[3].c = C(br); v[3].x = X(x + w); v[3].y = Y(y + h); v[3].z = 0;
   sceGuDisable(GU_TEXTURE_2D);
   sceGuDrawArray(GU_TRIANGLE_STRIP, CV_FMT, 4, 0, v);
}

void gfx_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom)
{
   gfx_grad(x, y, w, h, top, top, bottom, bottom);
}

void gfx_hgrad(float x, float y, float w, float h, uint32_t left, uint32_t right)
{
   gfx_grad(x, y, w, h, left, right, left, right);
}

/* ---- textures ------------------------------------------------------------ */

static int pow2(int n)
{
   int p = 8;
   while (p < n)
      p <<= 1;
   return p;
}

int gtex_alloc(gtex *t, int w, int h)
{
   memset(t, 0, sizeof(*t));
   t->w = w;
   t->h = h;
   t->tw = pow2(w);
   t->th = pow2(h);
   t->bw = (w + 3) & ~3;
   if (t->bw < 8) t->bw = 8;
   t->psm = GU_PSM_8888;
   t->px = memalign(16, (size_t)t->bw * h * 4);
   t->owned = 1;
   if (!t->px)
      return -1;
   memset(t->px, 0, (size_t)t->bw * h * 4);
   return 0;
}

void gtex_free(gtex *t)
{
   if (t->owned)
      free(t->px);
   memset(t, 0, sizeof(*t));
}

void gtex_flush(const gtex *t)
{
   sceKernelDcacheWritebackRange(t->px, (unsigned)(t->bw * t->h * (t->psm == GU_PSM_T8 ? 1 : 4)));
   sceGuTexFlush();
}

void gtex_wrap(gtex *t, const void *px, int w, int h, int tw, int th)
{
   memset(t, 0, sizeof(*t));
   t->px = (void *)px;
   t->w = w;
   t->h = h;
   t->tw = tw;
   t->th = th;
   t->bw = tw;
   t->psm = GU_PSM_8888;
}


static void bind(const gtex *t, int smooth, int tcc)
{
   sceGuEnable(GU_TEXTURE_2D);
   if (g_bound != t->px)
   {
      if (t->psm == GU_PSM_T8)
      {
         sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
         sceGuClutLoad(256 / 8, g_clut);
      }
      sceGuTexMode(t->psm, 0, 0, 0);
      sceGuTexImage(0, t->tw, t->th, t->bw, t->px);
      g_bound = t->px;
   }
   if (g_bound_filter != smooth)
   {
      sceGuTexFilter(smooth ? GU_LINEAR : GU_NEAREST, smooth ? GU_LINEAR : GU_NEAREST);
      g_bound_filter = smooth;
   }
   if (g_bound_tcc != tcc)
   {
      sceGuTexFunc(GU_TFX_MODULATE, tcc);
      g_bound_tcc = tcc;
   }
}

static void quad_uv(float u0, float v0, float u1, float v1, float x, float y, float w, float h, uint32_t c)
{
   tv *v = (tv *)sceGuGetMemory(2 * sizeof(tv));
   v[0].u = u0; v[0].v = v0; v[0].c = c; v[0].x = X(x); v[0].y = Y(y); v[0].z = 0;
   v[1].u = u1; v[1].v = v1; v[1].c = c; v[1].x = X(x + w); v[1].y = Y(y + h); v[1].z = 0;
   sceGuDrawArray(GU_SPRITES, TV_FMT, 2, 0, v);
}

/* Seams, by construction (owner, PSP Go: a horizontal line through the boot
 * logo while it slid in).  Two ways a texture row can go wrong on the GE:
 *  - an unscaled NEAREST sprite at a fractional position: the texel row
 *    boundary lands exactly on a pixel centre and one row is drawn twice or
 *    skipped -- so such sprites are snapped to whole pixels;
 *  - a scaled BILINEAR sprite whose edge samples half a texel outside the
 *    image, into the power-of-two padding (uninitialised past a 272-row
 *    hero) -- so the source rect is inset half a texel where it meets the
 *    image's own edge. */
static void tex_prep(const gtex *t, float *sx, float *sy, float *sw, float *sh, float *x, float *y, float w, float h,
                     int smooth)
{
   if (!smooth)
   {
      if (ST.s > 0.9999f && ST.s < 1.0001f && w == *sw && h == *sh)
      {
         *x += floorf(X(*x) + 0.5f) - X(*x);
         *y += floorf(Y(*y) + 0.5f) - Y(*y);
      }
      return;
   }
   if (w * ST.s != *sw || h * ST.s != *sh)
   {
      if (*sx <= 0.0f) { *sx += 0.5f; *sw -= 0.5f; }
      if (*sx + *sw >= (float)t->w) *sw = (float)t->w - 0.5f - *sx;
      if (*sy <= 0.0f) { *sy += 0.5f; *sh -= 0.5f; }
      if (*sy + *sh >= (float)t->h) *sh = (float)t->h - 0.5f - *sy;
   }
}

void gfx_tex(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
             uint32_t c, int smooth)
{
   if (!t || !t->px || w <= 0 || h <= 0)
      return;
   tex_prep(t, &sx, &sy, &sw, &sh, &x, &y, w, h, smooth);
   bind(t, smooth, GU_TCC_RGBA);
   /* Large sprites are slow on the GE (texture cache): slice into 64-wide
    * columns. */
   if (w > 64 && sw > 0)
   {
      float step = 64, k = sw / w, xx;
      for (xx = 0; xx < w; xx += step)
      {
         float ww = (w - xx < step) ? w - xx : step;
         quad_uv(sx + xx * k, sy, sx + (xx + ww) * k, sy + sh, x + xx, y, ww, h, C(c));
      }
      return;
   }
   quad_uv(sx, sy, sx + sw, sy + sh, x, y, w, h, C(c));
}

void gfx_tex_opaque(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
                    int smooth)
{
   if (!t || !t->px)
      return;
   tex_prep(t, &sx, &sy, &sw, &sh, &x, &y, w, h, smooth);
   bind(t, smooth, GU_TCC_RGB);
   {
      float step = 64, k = sw / w, xx;
      for (xx = 0; xx < w; xx += step)
      {
         float ww = (w - xx < step) ? w - xx : step;
         quad_uv(sx + xx * k, sy, sx + (xx + ww) * k, sy + sh, x + xx, y, ww, h, C(0xFFFFFFFF));
      }
   }
   bind(t, smooth, GU_TCC_RGBA);
}

/* ---- shape tiles --------------------------------------------------------
 * A tile is the top-left quarter of a rounded box's coverage: size T x T
 * covering [-o, T-o) on both axes, where the box's edges are at 0 and the
 * corner circle's centre at (r, r).  Plus an "edge" column: coverage across
 * a straight edge.  Coverage is 4x4 supersampled (fill/ring) or a Gaussian
 * of the signed distance (blur), so a 1.5 px border reads as 1.5 px. */

typedef struct
{
   int key;
   gtex t;             /* T8: corner T x T at (0,0); edge profile in column T+1 */
   int T, o;
   int used;
} tile;

#define NTILES 160
static tile g_tiles[NTILES];
static int g_tile_clock;
static void *g_late[NTILES];
static int g_nlate;

static void free_late(void)
{
   while (g_nlate)
      free(g_late[--g_nlate]);
}

int gfx_tiles_used(void)
{
   int i, n = 0;
   for (i = 0; i < NTILES; i++)
      n += g_tiles[i].key != 0;
   return n;
}

static float sd_corner(float x, float y, float r)
{
   /* signed distance, positive inside, for the quadrant x,y < big */
   if (x < r && y < r)
   {
      float dx = r - x, dy = r - y;
      return r - sqrtf(dx * dx + dy * dy);
   }
   return x < y ? x : y;
}

static float erf_approx(float x)
{
   /* Abramowitz-Stegun 7.1.26 */
   float t, y, s = x < 0 ? -1.0f : 1.0f;
   x = fabsf(x);
   t = 1.0f / (1.0f + 0.3275911f * x);
   y = 1.0f - (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t * expf(-x * x);
   return s * y;
}

static float cover(float x, float y, float r, float stroke, float blur)
{
   if (blur > 0)
   {
      float sigma = blur / 2, d = sd_corner(x, y, r);
      return 0.5f + 0.5f * erf_approx(d / (sigma * 1.41421356f));
   }
   {
      int i, j, n = 0;
      for (j = 0; j < 4; j++)
         for (i = 0; i < 4; i++)
         {
            float d = sd_corner(x + (i + 0.5f) / 4, y + (j + 0.5f) / 4, r);
            if (d >= 0 && (stroke <= 0 || d < stroke))
               n++;
         }
      return n / 16.0f;
   }
}

static tile *get_tile(float r, float stroke, float blur)
{
   int kr = (int)(r * 4 + 0.5f), ks = (int)(stroke * 4 + 0.5f), kb = (int)(blur * 2 + 0.5f);
   int key = 1 + kr + (ks << 10) + (kb << 18), i, best = 0;
   tile *t;
   for (i = 0; i < NTILES; i++)
      if (g_tiles[i].key == key)
      {
         g_tiles[i].used = ++g_tile_clock;
         return &g_tiles[i];
      }
   for (i = 1; i < NTILES; i++)
      if (g_tiles[i].used < g_tiles[best].used)
         best = i;
   t = &g_tiles[best];
   if (t->key)
   {
      /* the list being built may still point at it: free after the frame */
      if (g_nlate < (int)(sizeof(g_late) / sizeof(g_late[0])))
         g_late[g_nlate++] = t->t.px;
   }
   memset(t, 0, sizeof(*t));
   r = kr / 4.0f;
   stroke = ks / 4.0f;
   blur = kb / 2.0f;
   t->o = (int)ceilf(blur * 1.6f);
   t->T = (int)ceilf((r > stroke ? r : stroke) + blur * 1.6f) + t->o + 1;
   {
      int T = t->T, o = t->o, W = T + 2, x, y;
      unsigned char *p;
      t->t.w = W;
      t->t.h = T;
      t->t.tw = pow2(W);
      t->t.th = pow2(T);
      t->t.bw = (W + 15) & ~15;
      t->t.psm = GU_PSM_T8;
      t->t.px = memalign(16, (size_t)t->t.bw * T);
      p = (unsigned char *)t->t.px;
      memset(p, 0, (size_t)t->t.bw * T);
      for (y = 0; y < T; y++)
      {
         for (x = 0; x < T; x++)
            p[y * t->t.bw + x] = (unsigned char)(cover((float)(x - o), (float)(y - o), r, stroke, blur) * 255 + 0.5f);
         /* edge profile: across a straight edge, far from any corner */
         p[y * t->t.bw + T + 1] = (unsigned char)(cover(1000.0f, (float)(y - o), r, stroke, blur) * 255 + 0.5f);
      }
      sceKernelDcacheWritebackRange(p, (unsigned)(t->t.bw * T));
   }
   t->key = key;
   t->used = ++g_tile_clock;
   g_bound = NULL;
   return t;
}

/* draw a texture region with arbitrary UVs at the quad's 4 corners */
static void quad4(float u[4], float v[4], float x, float y, float w, float h, uint32_t c)
{
   tv *q = (tv *)sceGuGetMemory(4 * sizeof(tv));
   float xs[4] = { x, x + w, x, x + w }, ys[4] = { y, y, y + h, y + h };
   int i;
   for (i = 0; i < 4; i++)
   {
      q[i].u = u[i]; q[i].v = v[i]; q[i].c = c;
      q[i].x = X(xs[i]); q[i].y = Y(ys[i]); q[i].z = 0;
   }
   sceGuDrawArray(GU_TRIANGLE_STRIP, TV_FMT, 4, 0, q);
}

/* The 9-slice: box (x,y,w,h) whose edges sit at the tile's 0 line.  `fill`
 * = draw the middle as well (fills and shadows; rings leave it empty). */
static void nine(tile *t, float x, float y, float w, float h, uint32_t c, int fill)
{
   int T = t->T, o = t->o;
   float in = (float)(T - o);          /* how far the tile reaches inward */
   float cw = in, ch = in, ge;
   uint32_t gc = C(c);
   if (cw * 2 > w) cw = w / 2;
   if (ch * 2 > h) ch = h / 2;
   bind(&t->t, 0, GU_TCC_RGBA);
   ge = (float)(T + 1) + 0.5f;         /* edge column */
   /* corners: u from 0 (outside) to o+cw */
   {
      float a = (float)o + cw, b = (float)o + ch;
      quad_uv(0, 0, a, b, x - o, y - o, a, b, gc);                                   /* tl */
      quad_uv(a, 0, 0, b, x + w - cw, y - o, a, b, gc);                              /* tr */
      quad_uv(0, b, a, 0, x - o, y + h - ch, a, b, gc);                              /* bl */
      quad_uv(a, b, 0, 0, x + w - cw, y + h - ch, a, b, gc);                         /* br */
   }
   /* edges */
   if (w - 2 * cw > 0)
   {
      float u[4] = { ge, ge, ge, ge };
      float v[4] = { 0, 0, (float)o + ch, (float)o + ch };
      quad4(u, v, x + cw, y - o, w - 2 * cw, o + ch, gc);                            /* top */
      {
         float v2[4] = { (float)o + ch, (float)o + ch, 0, 0 };
         quad4(u, v2, x + cw, y + h - ch, w - 2 * cw, o + ch, gc);                   /* bottom */
      }
   }
   if (h - 2 * ch > 0)
   {
      /* left: the profile runs along x, so rotate: u fixed (edge col), v = x */
      float u[4] = { ge, ge, ge, ge };
      float v[4] = { 0, (float)o + cw, 0, (float)o + cw };
      quad4(u, v, x - o, y + ch, o + cw, h - 2 * ch, gc);
      {
         float v2[4] = { (float)o + cw, 0, (float)o + cw, 0 };
         quad4(u, v2, x + w - cw, y + ch, o + cw, h - 2 * ch, gc);
      }
   }
   if (fill && w - 2 * cw > 0 && h - 2 * ch > 0)
   {
      /* the tile's inside is full coverage for fills (and for shadows once
       * past the blur); draw it as a plain rect */
      cv *v = (cv *)sceGuGetMemory(2 * sizeof(cv));
      v[0].c = v[1].c = gc;
      v[0].x = X(x + cw); v[0].y = Y(y + ch); v[0].z = 0;
      v[1].x = X(x + w - cw); v[1].y = Y(y + h - ch); v[1].z = 0;
      sceGuDisable(GU_TEXTURE_2D);
      sceGuDrawArray(GU_SPRITES, CV_FMT, 2, 0, v);
   }
}

/* ---- the 9-slice with an affine (linear-gradient) fill ----
 * Every sub-quad takes its vertex colours from the box's corner colours,
 * interpolated at the vertex: Gouraud shading reproduces an affine
 * gradient exactly, and the corners keep their anti-aliased coverage. */
static uint32_t g_lin[4];
static float g_lx, g_ly, g_lw, g_lh;

static uint32_t lin_at(float x, float y)
{
   float u = clampf_((x - g_lx) / g_lw), v = clampf_((y - g_ly) / g_lh);
   return col_mix(col_mix(g_lin[0], g_lin[1], u), col_mix(g_lin[2], g_lin[3], u), v);
}

static void quad4c(float u[4], float v[4], float x, float y, float w, float h)
{
   tv *q = (tv *)sceGuGetMemory(4 * sizeof(tv));
   float xs[4] = { x, x + w, x, x + w }, ys[4] = { y, y, y + h, y + h };
   int i;
   for (i = 0; i < 4; i++)
   {
      q[i].u = u[i]; q[i].v = v[i]; q[i].c = C(lin_at(xs[i], ys[i]));
      q[i].x = X(xs[i]); q[i].y = Y(ys[i]); q[i].z = 0;
   }
   sceGuDrawArray(GU_TRIANGLE_STRIP, TV_FMT, 4, 0, q);
}

void gfx_rrect_lin(float x, float y, float w, float h, float r, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br)
{
   tile *t;
   int T, o;
   float in, cw, ch, ge, a, b;
   if (w <= 0 || h <= 0)
      return;
   if (r > w / 2) r = w / 2;
   if (r > h / 2) r = h / 2;
   g_lin[0] = tl; g_lin[1] = tr; g_lin[2] = bl; g_lin[3] = br;
   g_lx = x; g_ly = y; g_lw = w; g_lh = h;
   t = get_tile(r, 0, 0);
   T = t->T; o = t->o;
   in = (float)(T - o);
   cw = in; ch = in;
   if (cw * 2 > w) cw = w / 2;
   if (ch * 2 > h) ch = h / 2;
   bind(&t->t, 0, GU_TCC_RGBA);
   ge = (float)(T + 1) + 0.5f;
   a = (float)o + cw;
   b = (float)o + ch;
   {
      float u1[4] = { 0, a, 0, a }, v1[4] = { 0, 0, b, b };
      float u2[4] = { a, 0, a, 0 }, v3[4] = { b, b, 0, 0 };
      quad4c(u1, v1, x - o, y - o, a, b);
      quad4c(u2, v1, x + w - cw, y - o, a, b);
      quad4c(u1, v3, x - o, y + h - ch, a, b);
      quad4c(u2, v3, x + w - cw, y + h - ch, a, b);
   }
   if (w - 2 * cw > 0)
   {
      float u[4] = { ge, ge, ge, ge }, v[4] = { 0, 0, b, b }, v2[4] = { b, b, 0, 0 };
      quad4c(u, v, x + cw, y - o, w - 2 * cw, b);
      quad4c(u, v2, x + cw, y + h - ch, w - 2 * cw, b);
   }
   if (h - 2 * ch > 0)
   {
      float u[4] = { ge, ge, ge, ge }, v[4] = { 0, a, 0, a }, v2[4] = { a, 0, a, 0 };
      quad4c(u, v, x - o, y + ch, a, h - 2 * ch);
      quad4c(u, v2, x + w - cw, y + ch, a, h - 2 * ch);
   }
   if (w - 2 * cw > 0 && h - 2 * ch > 0)
   {
      float x0 = x + cw, y0 = y + ch, x1 = x + w - cw, y1 = y + h - ch;
      gfx_grad(x0, y0, x1 - x0, y1 - y0, lin_at(x0, y0), lin_at(x1, y0), lin_at(x0, y1), lin_at(x1, y1));
   }
}

void gfx_rrect(float x, float y, float w, float h, float r, uint32_t c)
{
   if (w <= 0 || h <= 0 || !(c >> 24))
      return;
   if (r > w / 2) r = w / 2;
   if (r > h / 2) r = h / 2;
   if (r < 0.5f)
   {
      gfx_rect(x, y, w, h, c);
      return;
   }
   nine(get_tile(r, 0, 0), x, y, w, h, c, 1);
}

void gfx_rrect_grad(float x, float y, float w, float h, float r, uint32_t top, uint32_t bottom)
{
   /* corners take the end colours; the body is the gradient */
   float rr = r;
   if (rr > h / 2) rr = h / 2;
   gfx_rrect(x, y, w, rr * 2 + 1, r, top);
   gfx_rrect(x, y + h - rr * 2 - 1, w, rr * 2 + 1, r, bottom);
   gfx_grad(x, y + rr, w, h - 2 * rr, col_mix(top, bottom, rr / h), col_mix(top, bottom, rr / h),
            col_mix(top, bottom, 1 - rr / h), col_mix(top, bottom, 1 - rr / h));
}

void gfx_rring(float x, float y, float w, float h, float r, float stroke, uint32_t c)
{
   if (w <= 0 || h <= 0 || !(c >> 24))
      return;
   if (r > w / 2) r = w / 2;
   if (r > h / 2) r = h / 2;
   nine(get_tile(r, stroke, 0), x, y, w, h, c, 0);
}

void gfx_shadow(float x, float y, float w, float h, float r, float ox, float oy, float blur, float spread, uint32_t c)
{
   x += ox - spread;
   y += oy - spread;
   w += 2 * spread;
   h += 2 * spread;
   r += spread;
   if (r < 0) r = 0;
   if (r > w / 2) r = w / 2;
   if (r > h / 2) r = h / 2;
   if (blur < 0.5f)
   {
      gfx_rrect(x, y, w, h, r, c);
      return;
   }
   nine(get_tile(r, 0, blur), x, y, w, h, c, 1);
}

void gfx_circle(float cx, float cy, float rad, uint32_t c)
{
   gfx_rrect(cx - rad, cy - rad, rad * 2, rad * 2, rad, c);
}

void gfx_ring(float cx, float cy, float rad, float stroke, uint32_t c)
{
   gfx_rring(cx - rad, cy - rad, rad * 2, rad * 2, rad, stroke, c);
}

void gfx_corner_mask(float x, float y, float w, float h, float r, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br)
{
   /* an inverted fill tile: coverage of "outside the rounded corner" */
   tile *t;
   int kr = (int)(r * 4 + 0.5f), key = 1 + kr + (1 << 29), i;
   for (i = 0; i < NTILES; i++)
      if (g_tiles[i].key == key)
         break;
   if (i == NTILES)
   {
      t = get_tile(r, 0, 0);
      {
         /* make a private inverted copy under its own key */
         tile *n = NULL;
         int j, best = 0;
         for (j = 1; j < NTILES; j++)
            if (g_tiles[j].used < g_tiles[best].used && &g_tiles[j] != t)
               best = j;
         n = &g_tiles[best];
         if (n->key && g_nlate < NTILES)
            g_late[g_nlate++] = n->t.px;
         *n = *t;
         n->t.px = memalign(16, (size_t)t->t.bw * t->T);
         {
            unsigned char *s = (unsigned char *)t->t.px, *d = (unsigned char *)n->t.px;
            int k;
            for (k = 0; k < t->t.bw * t->T; k++)
               d[k] = (unsigned char)(255 - s[k]);
            sceKernelDcacheWritebackRange(d, (unsigned)(t->t.bw * t->T));
         }
         n->key = key;
         n->used = ++g_tile_clock;
         g_bound = NULL;
         i = (int)(n - g_tiles);
      }
   }
   t = &g_tiles[i];
   t->used = ++g_tile_clock;
   bind(&t->t, 0, GU_TCC_RGBA);
   {
      float a = r;
      quad_uv(0, 0, a, a, x, y, a, a, C(tl));
      quad_uv(a, 0, 0, a, x + w - a, y, a, a, C(tr));
      quad_uv(0, a, a, 0, x, y + h - a, a, a, C(bl));
      quad_uv(a, a, 0, 0, x + w - a, y + h - a, a, a, C(br));
   }
}

void gfx_tex_vfade(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
                   uint32_t c, float a_top, float a_bot)
{
   float u[4] = { sx, sx + sw, sx, sx + sw }, v[4] = { sy, sy, sy + sh, sy + sh };
   tv *q;
   float xs[4] = { x, x + w, x, x + w }, ys[4] = { y, y, y + h, y + h };
   int i;
   if (!t || !t->px)
      return;
   bind(t, 1, GU_TCC_RGBA);
   q = (tv *)sceGuGetMemory(4 * sizeof(tv));
   for (i = 0; i < 4; i++)
   {
      q[i].u = u[i]; q[i].v = v[i];
      q[i].c = C(col_alpha(c, i < 2 ? a_top : a_bot));
      q[i].x = X(xs[i]); q[i].y = Y(ys[i]); q[i].z = 0;
   }
   sceGuDrawArray(GU_TRIANGLE_STRIP, TV_FMT, 4, 0, q);
}

/* A rounded image.  The GE has no second texture to mask with, and the
 * frame's alpha channel is the stencil (it cannot hold a soft mask), so the
 * image is drawn square and its corners are covered in the colour of what
 * lies under it -- `ground`, which the caller knows (0 = leave square). */
void gfx_tex_rounded(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
                     float r, uint32_t c, int smooth, uint32_t ground)
{
   gfx_tex(t, sx, sy, sw, sh, x, y, w, h, c, smooth);
   if (ground && r >= 0.5f)
      gfx_corner_mask(x, y, w, h, r, ground, ground, ground, ground);
}

/* ---- text ---------------------------------------------------------------- */

unsigned xf_utf8(const char **ps)
{
   const unsigned char *s = (const unsigned char *)*ps;
   unsigned c = *s++, n = 0, cp;
   if (c < 0x80) { *ps = (const char *)s; return c; }
   if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
   else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
   else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
   else { *ps = (const char *)s; return '?'; }
   while (n--)
   {
      if ((*s & 0xC0) != 0x80) { *ps = (const char *)s; return '?'; }
      cp = cp << 6 | (*s++ & 0x3F);
   }
   *ps = (const char *)s;
   return cp;
}

const xf_glyph *xf_find(const xf_face *f, unsigned cp)
{
   int lo = 0, hi = f->ng - 1;
   while (lo <= hi)
   {
      int m = (lo + hi) / 2;
      if (f->g[m].cp == cp) return &f->g[m];
      if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
   }
   return cp == '?' ? NULL : xf_find(f, '?');
}

int xf_kerning(const xf_face *f, unsigned a, unsigned b)
{
   unsigned key = a << 16 | b;
   int lo = 0, hi = f->nk - 1;
   if (a > 0xFFFF || b > 0xFFFF)
      return 0;
   while (lo <= hi)
   {
      int m = (lo + hi) / 2;
      if (f->k[m].pair == key) return f->k[m].k;
      if (f->k[m].pair < key) lo = m + 1; else hi = m - 1;
   }
   return 0;
}

float xf_width(const xf_face *f, const char *s, int n, float track)
{
   const char *end = n < 0 ? s + strlen(s) : s + n;
   float w = 0;
   unsigned prev = 0;
   while (s < end && *s)
   {
      unsigned cp = xf_utf8(&s);
      const xf_glyph *g = xf_find(f, cp);
      if (prev) w += xf_kerning(f, prev, cp) / 64.0f;
      if (g) w += g->adv / 64.0f + track;
      prev = cp;
   }
   return w;
}

const xf_face *face_get(int face)
{
   return &xf_faces[face];
}

static gtex g_face_tex[XF_COUNT];

static const gtex *face_tex(int face)
{
   gtex *t = &g_face_tex[face];
   if (!t->px)
   {
      const xf_face *f = &xf_faces[face];
      t->px = (void *)f->atlas;
      t->w = f->aw;
      t->h = f->ah;
      t->tw = pow2(f->aw);
      t->th = pow2(f->ah);
      t->bw = f->aw;
      t->psm = GU_PSM_T8;
   }
   return t;
}

float gfx_text_n(int face, float x, float y, const char *s, int n, float track, uint32_t c)
{
   const xf_face *f = &xf_faces[face];
   const char *end = n < 0 ? s + strlen(s) : s + n;
   float pen = 0;
   unsigned prev = 0;
   uint32_t gc = C(c);
   int count = 0, cap = 0;
   tv *v;
   const char *p;
   if (!(gc >> 24))
      return xf_width(f, s, n, track);
   for (p = s; p < end && *p; p++)
      cap++;
   if (!cap)
      return 0;
   v = (tv *)sceGuGetMemory((unsigned)(cap * 2 * sizeof(tv)));
   while (s < end && *s)
   {
      unsigned cp = xf_utf8(&s);
      const xf_glyph *g = xf_find(f, cp);
      if (prev) pen += xf_kerning(f, prev, cp) / 64.0f;
      prev = cp;
      if (!g)
         continue;
      if (g->w && count < cap)
      {
         /* snap the glyph to the device pixel grid, keep the pen fractional */
         float gx = floorf(X(x + pen) + 0.5f) + g->xo * ST.s;
         float gy = floorf(Y(y) + 0.5f) + g->yo * ST.s;
         tv *q = &v[count * 2];
         q[0].u = g->x; q[0].v = g->y; q[0].c = gc; q[0].x = gx; q[0].y = gy; q[0].z = 0;
         q[1].u = g->x + g->w; q[1].v = g->y + g->h; q[1].c = gc;
         q[1].x = gx + g->w * ST.s; q[1].y = gy + g->h * ST.s; q[1].z = 0;
         count++;
      }
      pen += g->adv / 64.0f + track;
   }
   if (count)
   {
      bind(face_tex(face), ST.s != 1.0f, GU_TCC_RGBA);
      /* positions are already transformed: draw with an identity mapping */
      sceGuDrawArray(GU_SPRITES, TV_FMT, count * 2, 0, v);
   }
   return pen;
}

float gfx_text(int face, float x, float y, const char *s, uint32_t c)
{
   return gfx_text_n(face, x, y, s, -1, 0, c);
}

float text_w(int face, const char *s)
{
   return xf_width(&xf_faces[face], s, -1, 0);
}

float text_wt(int face, const char *s, float track)
{
   return xf_width(&xf_faces[face], s, -1, track);
}

float face_baseline(int face, float lh)
{
   const xf_face *f = &xf_faces[face];
   return (lh - (f->asc + f->desc)) / 2 + f->asc;
}

float gfx_text_box(int face, float x, float top, float lh, const char *s, uint32_t c)
{
   return gfx_text(face, x, top + face_baseline(face, lh), s, c);
}

float gfx_text_center(int face, float cx, float y, const char *s, uint32_t c)
{
   float w = text_w(face, s);
   return gfx_text(face, cx - w / 2, y, s, c);
}

float gfx_text_right(int face, float rx, float y, const char *s, uint32_t c)
{
   float w = text_w(face, s);
   return gfx_text(face, rx - w, y, s, c);
}

float gfx_text_ell(int face, float x, float y, float maxw, const char *s, uint32_t c)
{
   const xf_face *f = &xf_faces[face];
   float w = xf_width(f, s, -1, 0), ew;
   const char *p = s, *last = s;
   if (w <= maxw + 0.01f)
      return gfx_text(face, x, y, s, c);
   ew = xf_width(f, "\xE2\x80\xA6", -1, 0);
   while (*p)
   {
      const char *q = p;
      xf_utf8(&q);
      if (xf_width(f, s, (int)(q - s), 0) + ew > maxw)
         break;
      last = q;
      p = q;
   }
   /* no trailing space before the ellipsis */
   while (last > s && last[-1] == ' ')
      last--;
   {
      float a = gfx_text_n(face, x, y, s, (int)(last - s), 0, c);
      return a + gfx_text(face, x + a, y, "\xE2\x80\xA6", c);
   }
}

int text_wrap(int face, const char *s, float maxw, int maxl, int *starts, int *lens)
{
   const xf_face *f = &xf_faces[face];
   int n = 0;
   const char *p = s;
   while (*p && n < maxl)
   {
      const char *ls = p, *brk = NULL, *q = p;
      while (*p == ' ') p++;
      ls = p;
      q = p;
      while (*q)
      {
         const char *r = q;
         xf_utf8(&r);
         if (xf_width(f, ls, (int)(r - ls), 0) > maxw)
            break;
         if (*r == ' ' || *r == '\0')
            brk = r;
         q = r;
      }
      if (!*q)
         brk = q;
      else if (!brk)
         brk = q > ls ? q : ls + 1;          /* a word wider than the line */
      starts[n] = (int)(ls - s);
      lens[n] = (int)(brk - ls);
      n++;
      p = brk;
   }
   if (*p && n)
      lens[n - 1] = (int)strlen(s + starts[n - 1]);   /* caller ellipsises */
   return n;
}
