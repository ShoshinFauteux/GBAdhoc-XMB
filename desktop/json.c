/* json.c -- see json.h. */
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- reader ------------------------------------------------------------- */

typedef struct
{
   const char *p;
   int bad;
} jp;

static void ws(jp *q)
{
   while (*q->p == ' ' || *q->p == '\t' || *q->p == '\n' || *q->p == '\r')
      q->p++;
}

static jv *jnew(jv_type t)
{
   jv *v = (jv *)calloc(1, sizeof(jv));
   if (v)
      v->t = t;
   return v;
}

static void put_utf8(sb *b, unsigned c)
{
   char o[4];
   if (c < 0x80) { o[0] = (char)c; sb_addn(b, o, 1); }
   else if (c < 0x800) { o[0] = (char)(0xc0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); sb_addn(b, o, 2); }
   else if (c < 0x10000) { o[0] = (char)(0xe0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); sb_addn(b, o, 3); }
   else { o[0] = (char)(0xf0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63)); o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63)); sb_addn(b, o, 4); }
}

static unsigned hex4(const char *p, int *ok)
{
   unsigned v = 0;
   int i;
   for (i = 0; i < 4; i++)
   {
      char c = p[i];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
      else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
      else { *ok = 0; return 0; }
   }
   return v;
}

static char *pstr(jp *q)
{
   sb b;
   if (*q->p != '"') { q->bad = 1; return NULL; }
   q->p++;
   sb_init(&b);
   sb_add(&b, "");
   while (*q->p && *q->p != '"')
   {
      if (*q->p == '\\')
      {
         char c = *++q->p;
         int ok = 1;
         unsigned u;
         switch (c)
         {
         case 'n': sb_addn(&b, "\n", 1); break;
         case 't': sb_addn(&b, "\t", 1); break;
         case 'r': sb_addn(&b, "\r", 1); break;
         case 'b': sb_addn(&b, "\b", 1); break;
         case 'f': sb_addn(&b, "\f", 1); break;
         case 'u':
            u = hex4(q->p + 1, &ok);
            if (!ok) { q->bad = 1; sb_free(&b); return NULL; }
            q->p += 4;
            if (u >= 0xd800 && u < 0xdc00 && q->p[1] == '\\' && q->p[2] == 'u')
            {
               unsigned lo = hex4(q->p + 3, &ok);
               if (ok && lo >= 0xdc00 && lo < 0xe000)
               {
                  u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                  q->p += 6;
               }
            }
            put_utf8(&b, u);
            break;
         case 0: q->bad = 1; sb_free(&b); return NULL;
         default: sb_addn(&b, &c, 1); break;
         }
         q->p++;
      }
      else
      {
         sb_addn(&b, q->p, 1);
         q->p++;
      }
   }
   if (*q->p != '"') { q->bad = 1; sb_free(&b); return NULL; }
   q->p++;
   return b.s;
}

static jv *pval(jp *q, int depth)
{
   jv *v = NULL, **tail;
   ws(q);
   if (depth > 64) { q->bad = 1; return NULL; }
   switch (*q->p)
   {
   case '{':
      v = jnew(JV_OBJ);
      q->p++;
      tail = &v->kid;
      ws(q);
      if (*q->p == '}') { q->p++; return v; }
      for (;;)
      {
         char *k;
         jv *m;
         ws(q);
         k = pstr(q);
         if (!k) break;
         ws(q);
         if (*q->p != ':') { free(k); q->bad = 1; break; }
         q->p++;
         m = pval(q, depth + 1);
         if (!m) { free(k); break; }
         m->key = k;
         *tail = m;
         tail = &m->next;
         ws(q);
         if (*q->p == ',') { q->p++; continue; }
         if (*q->p == '}') { q->p++; return v; }
         q->bad = 1;
         break;
      }
      break;
   case '[':
      v = jnew(JV_ARR);
      q->p++;
      tail = &v->kid;
      ws(q);
      if (*q->p == ']') { q->p++; return v; }
      for (;;)
      {
         jv *m = pval(q, depth + 1);
         if (!m) break;
         *tail = m;
         tail = &m->next;
         ws(q);
         if (*q->p == ',') { q->p++; continue; }
         if (*q->p == ']') { q->p++; return v; }
         q->bad = 1;
         break;
      }
      break;
   case '"':
      v = jnew(JV_STR);
      v->s = pstr(q);
      if (!v->s) { free(v); return NULL; }
      return v;
   case 't':
      if (!strncmp(q->p, "true", 4)) { q->p += 4; v = jnew(JV_BOOL); v->n = 1; return v; }
      break;
   case 'f':
      if (!strncmp(q->p, "false", 5)) { q->p += 5; v = jnew(JV_BOOL); return v; }
      break;
   case 'n':
      if (!strncmp(q->p, "null", 4)) { q->p += 4; return jnew(JV_NULL); }
      break;
   default:
   {
      char *end;
      double d = strtod(q->p, &end);
      if (end != q->p)
      {
         q->p = end;
         v = jnew(JV_NUM);
         v->n = d;
         return v;
      }
   }
   }
   q->bad = 1;
   jv_free(v);
   return NULL;
}

jv *jv_parse(const char *text)
{
   jp q = { text, 0 };
   jv *v = pval(&q, 0);
   if (q.bad)
   {
      jv_free(v);
      return NULL;
   }
   return v;
}

void jv_free(jv *v)
{
   while (v)
   {
      jv *n = v->next;
      jv_free(v->kid);
      free(v->s);
      free(v->key);
      free(v);
      v = n;
   }
}

jv *jv_get(const jv *o, const char *key)
{
   jv *m;
   if (!o || o->t != JV_OBJ)
      return NULL;
   for (m = o->kid; m; m = m->next)
      if (m->key && !strcmp(m->key, key))
         return m;
   return NULL;
}

const char *jv_str(const jv *o, const char *key, const char *def)
{
   jv *m = jv_get(o, key);
   return m && m->t == JV_STR ? m->s : def;
}

double jv_num(const jv *o, const char *key, double def)
{
   jv *m = jv_get(o, key);
   return m && (m->t == JV_NUM || m->t == JV_BOOL) ? m->n : def;
}

int jv_bool(const jv *o, const char *key, int def)
{
   jv *m = jv_get(o, key);
   if (!m) return def;
   if (m->t == JV_BOOL || m->t == JV_NUM) return m->n != 0;
   if (m->t == JV_NULL) return 0;
   return def;
}

int jv_len(const jv *a)
{
   int n = 0;
   jv *m;
   if (!a || (a->t != JV_ARR && a->t != JV_OBJ))
      return 0;
   for (m = a->kid; m; m = m->next)
      n++;
   return n;
}

jv *jv_at(const jv *a, int i)
{
   jv *m;
   if (!a)
      return NULL;
   for (m = a->kid; m && i > 0; m = m->next)
      i--;
   return m;
}

/* ---- writer ------------------------------------------------------------- */

void sb_init(sb *b)
{
   b->s = NULL;
   b->n = b->cap = 0;
}

void sb_free(sb *b)
{
   free(b->s);
   sb_init(b);
}

void sb_addn(sb *b, const char *s, size_t n)
{
   if (b->n + n + 1 > b->cap)
   {
      size_t c = b->cap ? b->cap * 2 : 256;
      char *ns;
      while (c < b->n + n + 1)
         c *= 2;
      ns = (char *)realloc(b->s, c);
      if (!ns)
         return;
      b->s = ns;
      b->cap = c;
   }
   memcpy(b->s + b->n, s, n);
   b->n += n;
   b->s[b->n] = '\0';
}

void sb_add(sb *b, const char *s)
{
   sb_addn(b, s, strlen(s));
}

void sb_printf(sb *b, const char *fmt, ...)
{
   char tmp[1024];
   va_list ap;
   int n;
   va_start(ap, fmt);
   n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
   va_end(ap);
   if (n < 0)
      return;
   if ((size_t)n < sizeof(tmp))
   {
      sb_addn(b, tmp, (size_t)n);
      return;
   }
   {
      char *big = (char *)malloc((size_t)n + 1);
      if (!big)
         return;
      va_start(ap, fmt);
      vsnprintf(big, (size_t)n + 1, fmt, ap);
      va_end(ap);
      sb_addn(b, big, (size_t)n);
      free(big);
   }
}

void sb_jstr(sb *b, const char *s)
{
   const unsigned char *p = (const unsigned char *)(s ? s : "");
   sb_addn(b, "\"", 1);
   for (; *p; p++)
   {
      if (*p == '"') sb_addn(b, "\\\"", 2);
      else if (*p == '\\') sb_addn(b, "\\\\", 2);
      else if (*p == '\n') sb_addn(b, "\\n", 2);
      else if (*p == '\r') sb_addn(b, "\\r", 2);
      else if (*p == '\t') sb_addn(b, "\\t", 2);
      else if (*p < 0x20) sb_printf(b, "\\u%04x", *p);
      else sb_addn(b, (const char *)p, 1);
   }
   sb_addn(b, "\"", 1);
}

static void comma(sb *b)
{
   if (b->n && b->s[b->n - 1] != '{' && b->s[b->n - 1] != '[')
      sb_addn(b, ",", 1);
}

void sb_key(sb *b, const char *k)
{
   comma(b);
   sb_jstr(b, k);
   sb_addn(b, ":", 1);
}

void sb_kv_str(sb *b, const char *k, const char *v)
{
   sb_key(b, k);
   if (v)
      sb_jstr(b, v);
   else
      sb_add(b, "null");
}

void sb_kv_num(sb *b, const char *k, double v)
{
   sb_key(b, k);
   if (v == (double)(long long)v)
      sb_printf(b, "%lld", (long long)v);
   else
      sb_printf(b, "%.6g", v);
}

void sb_kv_bool(sb *b, const char *k, int v)
{
   sb_key(b, k);
   sb_add(b, v ? "true" : "false");
}

void sb_json(sb *b, const jv *v)
{
   jv *m;
   if (!v)
   {
      sb_add(b, "null");
      return;
   }
   switch (v->t)
   {
   case JV_NULL: sb_add(b, "null"); break;
   case JV_BOOL: sb_add(b, v->n ? "true" : "false"); break;
   case JV_NUM:
      if (v->n == (double)(long long)v->n) sb_printf(b, "%lld", (long long)v->n);
      else sb_printf(b, "%.17g", v->n);
      break;
   case JV_STR: sb_jstr(b, v->s); break;
   case JV_ARR:
      sb_add(b, "[");
      for (m = v->kid; m; m = m->next)
      {
         if (m != v->kid) sb_add(b, ",");
         sb_json(b, m);
      }
      sb_add(b, "]");
      break;
   case JV_OBJ:
      sb_add(b, "{");
      for (m = v->kid; m; m = m->next)
      {
         if (m != v->kid) sb_add(b, ",");
         sb_jstr(b, m->key);
         sb_add(b, ":");
         sb_json(b, m);
      }
      sb_add(b, "}");
      break;
   }
}
