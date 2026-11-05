/* xf.h -- baked faces (gen/xmb_fonts.h, tools/bake_fonts.py): glyph and
 * kerning records, and the text layout both the GU renderer and the CPU
 * title-card painter use, so a line measures the same on either. */
#ifndef XMBAPP_XF_H
#define XMBAPP_XF_H

typedef struct
{
   unsigned short cp;           /* code point */
   unsigned short x, y;         /* in the atlas */
   unsigned char w, h;
   signed char xo, yo;          /* bitmap's top-left from the pen on the baseline */
   unsigned short adv;          /* advance, 1/64 px, unhinted */
} xf_glyph;

typedef struct
{
   unsigned int pair;           /* cpA << 16 | cpB */
   int k;                       /* 1/64 px */
} xf_kern;

typedef struct
{
   const unsigned char *atlas;
   int aw, ah;
   const xf_glyph *g;
   int ng;
   const xf_kern *k;
   int nk;
   float asc, desc, cap, px;
   int weight;
} xf_face;

/* Decode one UTF-8 code point; advances *s.  Bad bytes come back as '?'. */
unsigned xf_utf8(const char **s);
const xf_glyph *xf_find(const xf_face *f, unsigned cp);
int xf_kerning(const xf_face *f, unsigned a, unsigned b);   /* 1/64 px */
/* Width in px of `s` (UTF-8), with `track` px of letter-spacing after every
 * glyph (CSS letter-spacing).  n < 0 = whole string, else first n bytes. */
float xf_width(const xf_face *f, const char *s, int n, float track);

#endif
