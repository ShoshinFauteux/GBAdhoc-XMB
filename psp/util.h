/* util.h -- GBAdhoc XMB: time, easing, files, the log, a lock. */
#ifndef XMBAPP_UTIL_H
#define XMBAPP_UTIL_H

#include <stddef.h>
#include <stdint.h>

double now_s(void);                       /* seconds since boot, monotonic */
unsigned now_ms(void);

/* CSS cubic-bezier(x1,y1,x2,y2) at progress t in [0,1] */
float bezier(float x1, float y1, float x2, float y2, float t);
/* the design's one curve: cubic-bezier(.2,.7,.2,1) */
float ease_out(float t);
float clampf(float v, float lo, float hi);
float lerpf(float a, float b, float t);

/* A value that eases to its target over `dur` seconds (CSS transition). */
typedef struct { float from, to, cur; double t0; float dur; } tween;
void tw_set(tween *t, float v);                    /* jump */
void tw_go(tween *t, float to, float dur);         /* transition */
float tw_val(tween *t);                            /* evaluate now */
int tw_busy(const tween *t);

/* files */
int file_exists(const char *p);
int dir_exists(const char *p);
long file_size(const char *p);
int read_all(const char *p, unsigned char **data, size_t *len);  /* malloc'd + NUL */
int write_all(const char *p, const void *d, size_t n);           /* via .tmp + rename */
int mkdirs(const char *p);
int remove_tree(const char *p);           /* files then the folder; depth 1 */
const char *base_name(const char *p);
void stem_of(const char *p, char *out, size_t cap);   /* base name without extension */
const char *ext_of(const char *p);        /* ".gba" or "" */
int ieq_str(const char *a, const char *b);
int ends_with_i(const char *s, const char *suf);
void path_join(char *out, size_t cap, const char *a, const char *b);
/* free bytes on "ms0:" / "ef0:", -1 when unknown */
long long dev_free(const char *dev);

/* key = value ini (first match); 0 = found */
int ini_get(const char *buf, const char *key, char *out, size_t cap);

/* log: <app dir>/log/xmbapp.log (harness builds) */
void log_open(const char *dir);
void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) logf_(__VA_ARGS__)

/* a mutex (sceKernel semaphore) */
typedef struct { int id; } lock_t;
void lock_init(lock_t *l);
void lock_take(lock_t *l);
void lock_give(lock_t *l);

/* local wall clock (sceRtc), and "12 Oct" for a unix time */
void wall_hm(int *h, int *m);
long wall_unix(void);
void fmt_day(long t, char *out, size_t cap);
void fmt_mb(long long bytes, char *out, size_t cap);   /* "212 MB", "1.2 GB", "880 KB" */

#endif
