/* json.h -- a small JSON reader and writer for the app's bridge (GBAdhoc XMB).
 * The UI and the host talk in JSON over WebView2's web messages. */
#ifndef XMB_JSON_H
#define XMB_JSON_H

#include <stddef.h>

typedef enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ } jv_type;

typedef struct jv
{
   jv_type t;
   double n;            /* JV_NUM, JV_BOOL (0/1) */
   char *s;             /* JV_STR (UTF-8), the key of an object member */
   char *key;
   struct jv *kid;      /* first child (array items, object members) */
   struct jv *next;
} jv;

jv *jv_parse(const char *text);          /* NULL on a syntax error */
void jv_free(jv *v);
jv *jv_get(const jv *obj, const char *key);
const char *jv_str(const jv *obj, const char *key, const char *def);
double jv_num(const jv *obj, const char *key, double def);
int jv_bool(const jv *obj, const char *key, int def);
int jv_len(const jv *arr);
jv *jv_at(const jv *arr, int i);

/* A growing string. */
typedef struct
{
   char *s;
   size_t n, cap;
} sb;

void sb_init(sb *b);
void sb_free(sb *b);
void sb_add(sb *b, const char *s);
void sb_addn(sb *b, const char *s, size_t n);
void sb_printf(sb *b, const char *fmt, ...);
void sb_jstr(sb *b, const char *s);       /* a quoted, escaped JSON string */
/* "key":"value", with a leading comma unless the object just opened */
void sb_kv_str(sb *b, const char *k, const char *v);
void sb_kv_num(sb *b, const char *k, double v);
void sb_kv_bool(sb *b, const char *k, int v);
void sb_key(sb *b, const char *k);
void sb_json(sb *b, const jv *v);         /* re-serialise a value */

#endif
