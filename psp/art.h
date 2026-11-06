/* art.h -- images on the CPU: decode, resample, blur, and the two pictures
 * an entry is made of (ICON0 144x80, PIC1 480x272), composed exactly as the
 * design draws them (data.js iconHTML / titleCard), so the preview and the
 * file written are the same pixels. */
#ifndef XMBAPP_ART_H
#define XMBAPP_ART_H

#include <stdint.h>
#include <stddef.h>
#include "model.h"

typedef struct
{
   uint32_t *px;     /* RGBA8888 (R in the low byte, as the GE and stb want) */
   int w, h;
} image;

void img_free(image *im);
int img_alloc(image *im, int w, int h);
/* any file stb reads (by content, not extension); an EBOOT.PBP's ICON0 or
 * PIC1 when `pbp_sect` is 1 (ICON0) or 4 (PIC1).  0 = ok */
int img_load(const char *path, image *out, int pbp_sect);
int img_load_mem(const unsigned char *d, size_t n, image *out);
/* cover (fill and crop) or contain (fit, transparent margins) into w x h */
int img_fit(const image *src, int w, int h, int cover, image *out);
/* draw src scaled into (x,y,w,h) of dst, src-over */
void img_draw(image *dst, const image *src, float x, float y, float w, float h);
void img_blur(image *im, float sigma);
void img_saturate(image *im, float s);
int img_png(const image *im, unsigned char **png, size_t *len);     /* malloc'd */

/* The ICON0 for a game's current choices and layout (g->fit, FIT_*), a
 * title card when there is no art.  A 144x80 custom PNG goes in as it is. */
int art_icon0(const game *g, image *out);
/* The PIC1, or -1 when the game has no background chosen: a 480x272 picture
 * as it is (hero art keeps its framing, subject right), anything else
 * covered. */
int art_pic1(const game *g, image *out);
/* A picture the owner supplies (custom folder, PICTURE): readable, decodable
 * and of a size the PSP can resample.  0 = ok, else why = "is too large...". */
#define ART_MAX_FILE (8 << 20)
/* Decoding takes ~7 bytes a pixel (inflate buffer + RGBA) on a heap of
 * ~10 MB beside the art caches: 1280x720 runs out of memory in PPSSPP,
 * 960x544 and 800x800 decode. */
#define ART_MAX_PX (960L * 640L)
int art_check(const char *path, char *why, size_t cap);
/* the title card alone */
int art_title_card(const char *title, int sys, image *out);
/* a small preview of any picture (picker candidates, PICTURE folder):
 * contain or cover into w x h */
int art_thumb(const char *path, int pbp_sect, int w, int h, int cover, image *out);

#endif
