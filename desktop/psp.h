/* psp.h -- the PSP over USB for GBAdhoc XMB: finding it, scanning it,
 * writing and removing entries.
 *
 * Drive letters drift, so a PSP is found by what is on it: a PSP/ folder,
 * and the USB storage's own name.  A PSP reports itself as SONY "PSP" MS
 * (the Memory Stick, ms0:); a PSP Go adds SONY "PSP" SS (its internal
 * storage, ef0:) with the same serial. */
#ifndef XMB_PSP_H
#define XMB_PSP_H

#include "json.h"

typedef struct
{
   char id[16];           /* "G" for G:, "F0".. for a test folder */
   char root[1024];       /* "G:/" */
   char letter[4];        /* "G:" or "" */
   char label[64];        /* volume label */
   char product[64];      /* the USB storage's product name */
   char serial[64];
   char kind[4];          /* "ms0" or "ef0" */
   int go;                /* storage of a PSP Go */
   int psp;               /* reports itself as a PSP (not only a PSP/ folder) */
   double total_mb, free_mb;
   char install[256];     /* GBAdhoc's folder, "PSP/GAME/GBAdhoc", or "" */
   char install_title[128];
   int fake;
} psp_dev;

typedef void (*psp_prog)(void *ctx, const char *json_obj);

/* --fake-psp DIR[,ef0][,go]: test with a folder; real drives are then
 * never looked at. */
void psp_add_fake(const char *spec);
int psp_fake_mode(void);

int psp_devices(psp_dev *out, int cap);
void psp_devices_json(sb *b);
int psp_find(const char *id, psp_dev *out);    /* 0 = found */

/* Everything the app shows about one storage: ROMs (recognised against
 * No-Intro), entries in PSP/GAME, art GBAdhoc already has.  Writes a JSON
 * object to `out`. */
int psp_scan(const jv *args, psp_prog cb, void *ctx, sb *out, char *err, size_t cap);

/* Make or update one entry (staged, then moved into PSP/GAME). 0 = ok;
 * else `code` is "nospace", "disconnected", "exists", "bad", "io". */
int psp_make(const jv *args, psp_prog cb, void *ctx, sb *out, char *code,
             char *err, size_t cap);
int psp_remove(const jv *args, sb *out, char *code, char *err, size_t cap);

/* Pictures on the stick (PSP/PICTURE, PICTURE). */
void psp_pictures(const jv *args, sb *out);
/* PSP/GAME/GBAdhocXMB/custom: [{name, path, url}] */
void psp_custom(const jv *args, sb *out);
/* Folders on the stick worth scanning (for Settings). */
void psp_dirs(const jv *args, sb *out);

/* May the UI read this file?  (inside a PSP, the cache, or picked) */
int psp_path_allowed(const char *path);
void psp_allow_path(const char *path);

#endif
