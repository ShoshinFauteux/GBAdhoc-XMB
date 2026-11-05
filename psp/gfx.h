/* gfx.h -- GBAdhoc XMB's 2D renderer on the GE (sceGu), 480x272 native.
 *
 * Coordinates are CSS pixels of the design (design A, psp.css):
 * the PSP screen IS the 480x272 CSS box, so a rect the design puts at
 * (24,60) is drawn at (24,60).  Colours are 0xAARRGGBB in the API (written
 * like CSS hex) and converted to the GE's ABGR at the vertex.
 *
 * Rounded rectangles, rings, soft shadows and circles are 9-slices of small
 * coverage tiles computed on the CPU once per (radius, stroke, blur) and
 * cached as T8 textures; text comes from the baked atlases (xf.h).  A
 * transform stack (translate + uniform scale + alpha) carries view
 * transitions and the pop of panels. */
#ifndef XMBAPP_GFX_H
#define XMBAPP_GFX_H

#include <stdint.h>
#include "xf.h"

#define SCR_W 480
#define SCR_H 272

/* 0xAARRGGBB helpers */
#define ARGB(a, r, g, b) ((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define RGBf(hex, af) (((uint32_t)((af) * 255.0f + 0.5f) << 24) | ((hex) & 0xFFFFFF))
uint32_t col_mix(uint32_t a, uint32_t b, float t);
uint32_t col_alpha(uint32_t c, float k);   /* multiply alpha */

typedef struct
{
   void *px;           /* 16-byte aligned; VRAM or RAM */
   int w, h;           /* the image */
   int tw, th;         /* power-of-two size told to the GE */
   int bw;             /* buffer width in pixels (multiple of 4 for 8888) */
   int psm;            /* GU_PSM_8888 or GU_PSM_T8 */
   int owned;          /* px is malloc'd here */
} gtex;

void gfx_init(void);
void gfx_frame_begin(uint32_t clear);
void gfx_frame_end(void);         /* waits for vblank, swaps */
/* the same, calling `between` after the GE finished and before the swap
 * (a firmware utility dialog draws there) */
void gfx_frame_end_with(void (*between)(void));
/* Offscreen 480x272 target in VRAM (the XMB preview); the texture is valid
 * after gfx_target_end(). */
void gfx_target_begin(void);
void gfx_target_end(void);
const gtex *gfx_target_tex(void);
/* The frame just shown, as 32-bit RGBA rows (for harness screenshots). */
const uint32_t *gfx_front_pixels(int *stride);

/* transform: x' = tx + s*x; alpha multiplies every colour */
void gfx_push(float tx, float ty, float s, float alpha);
void gfx_pop(void);
void gfx_clip(float x, float y, float w, float h);   /* intersected; pop restores */
void gfx_unclip(void);

void gfx_rect(float x, float y, float w, float h, uint32_t c);
/* four corner colours: tl tr bl br */
void gfx_grad(float x, float y, float w, float h, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br);
void gfx_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
void gfx_hgrad(float x, float y, float w, float h, uint32_t left, uint32_t right);
void gfx_rrect(float x, float y, float w, float h, float r, uint32_t c);
/* the rounded rect with a vertical gradient (panels) */
void gfx_rrect_grad(float x, float y, float w, float h, float r, uint32_t top, uint32_t bottom);
/* stroke drawn INSIDE the box (CSS border / inset box-shadow) */
void gfx_rring(float x, float y, float w, float h, float r, float stroke, uint32_t c);
/* CSS box-shadow: offset (ox,oy), blur, spread, of the box (x,y,w,h,r) */
void gfx_shadow(float x, float y, float w, float h, float r, float ox, float oy, float blur, float spread, uint32_t c);
void gfx_circle(float cx, float cy, float rad, uint32_t c);
void gfx_ring(float cx, float cy, float rad, float stroke, uint32_t c);

/* textures */
int gtex_alloc(gtex *t, int w, int h);                /* 8888, RAM */
void gtex_free(gtex *t);
void gtex_flush(const gtex *t);                       /* after CPU writes */
void gtex_wrap(gtex *t, const void *px, int w, int h, int tw, int th);  /* static 8888 */
/* draw part (sx,sy,sw,sh) of a texture into (x,y,w,h); linear filter if
 * `smooth`; colour modulates (0xFFFFFFFF = as is).  opaque = ignore alpha. */
void gfx_tex(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
             uint32_t c, int smooth);
void gfx_tex_opaque(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
                    int smooth);
/* a texture with rounded corners (the corners masked by a background colour
 * drawn over them is not possible on a gradient, so the image is clipped by
 * drawing it in strips with per-row insets) */
void gfx_tex_rounded(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
                     float r, uint32_t c, int smooth, uint32_t ground);   /* corners in `ground` */
void gfx_rrect_lin(float x, float y, float w, float h, float r, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br);

/* text: y is the BASELINE.  Returns the advance width. */
float gfx_text(int face, float x, float y, const char *s, uint32_t c);
float gfx_text_n(int face, float x, float y, const char *s, int n, float track, uint32_t c);
/* text in a CSS line box: top of the box, its height (line-height); the
 * baseline is placed as CSS does (half-leading). */
float gfx_text_box(int face, float x, float top, float lh, const char *s, uint32_t c);
/* clipped to maxw with an ellipsis */
float gfx_text_ell(int face, float x, float y, float maxw, const char *s, uint32_t c);
float gfx_text_center(int face, float cx, float y, const char *s, uint32_t c);
float gfx_text_right(int face, float rx, float y, const char *s, uint32_t c);
float text_w(int face, const char *s);
/* harness: the text of the frame last shown (screen px, its colour with the
 * fade applied, its size), for the contrast / overlap / size check.  Off
 * unless gfx_text_log(1). */
typedef struct { short x0, y0, x1, y1; uint32_t c; float px; char s[44]; } gfx_trec;
void gfx_text_log(int on);
/* an opaque layer (a dialog, a scrim) now covers (x,y,w,h): the text under it is not on screen */
void gfx_text_cover(float x, float y, float w, float h);
int gfx_text_records(const gfx_trec **out);
float text_wt(int face, const char *s, float track);
float face_baseline(int face, float lh);   /* baseline offset in a line box of height lh */
/* Wrap into at most `maxl` lines of width maxw; returns lines; starts[i]/lens[i]
 * index into s.  The last line is cut with an ellipsis if text remains (the
 * caller draws it with gfx_text_ell). */
int text_wrap(int face, const char *s, float maxw, int maxl, int *starts, int *lens);
const xf_face *face_get(int face);

/* the area OUTSIDE a rounded box's corners (r x r each), in one colour per
 * corner: rounds the corners of something drawn square on a known ground */
void gfx_corner_mask(float x, float y, float w, float h, float r, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br);
/* a texture whose alpha fades from a_top to a_bot down its height */
void gfx_tex_vfade(const gtex *t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
                   uint32_t c, float a_top, float a_bot);

/* the shape-tile cache, for the stats line */
int gfx_tiles_used(void);

#endif
