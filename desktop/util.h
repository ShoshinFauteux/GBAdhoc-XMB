/* util.h -- strings, paths and files for the GBAdhoc XMB host (Windows).
 * Paths inside the app are UTF-8 with '/' separators; they become UTF-16
 * only at the Windows API. */
#ifndef XMB_UTIL_H
#define XMB_UTIL_H

#include <windows.h>
#include <stddef.h>
#include "../shared/xw_core.h"

wchar_t *u_wide(const char *s);                 /* malloc'd; NULL on failure */
char *u_utf8(const wchar_t *w);                 /* malloc'd */
void u_slashes(char *p);                        /* '\\' -> '/' */
void u_join(char *out, size_t cap, const char *a, const char *b);
int u_ieq(const char *a, const char *b);        /* ASCII case-insensitive */
int u_istarts(const char *s, const char *prefix);
const char *u_ext(const char *path);            /* "gba", "" */
const char *u_base(const char *path);           /* file name */
void u_stem(const char *path, char *out, size_t cap);

int u_is_dir(const char *path);
int u_is_file(const char *path);
long long u_file_size(const char *path);
int u_mkdirs(const char *path);
int u_rmtree(const char *path);                 /* a folder and all in it */
int u_move(const char *from, const char *to);   /* same volume; 0 = ok */
/* Write a file straight through to the device and flush it.  0 = ok, else
 * the Windows error code. */
DWORD u_write_durable(const char *path, const void *data, size_t len);
int u_read_head(const char *path, unsigned char *buf, size_t cap, size_t *got);
/* CRC-32 of a whole file (streamed). 0 = ok */
int u_file_crc(const char *path, unsigned long *crc);

/* %LOCALAPPDATA%/GBAdhoc XMB[/sub], created. */
void u_appdata(const char *sub, char *out, size_t cap);

void u_urlenc(const char *s, char *out, size_t cap, int keep_slash);
void u_urldec(char *s);
int u_b64dec(const char *s, xw_buf *out);

void u_log(const char *fmt, ...);               /* appdata/log.txt */

#endif
