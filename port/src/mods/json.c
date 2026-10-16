// Small JSON reader and writer with line numbers (simgolf mod, task-19).
// See json.h.

#include "mods/json.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/mods_core.h"

typedef struct {
  const char *p;
  int line;
  char *err;
  size_t errlen;
  int failed;
  int depth;
} Parser;

static void perr(Parser *ps, const char *fmt, ...) {
  if (ps->failed) return;
  ps->failed = 1;
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  if (ps->err && ps->errlen) snprintf(ps->err, ps->errlen, "line %d: %s", ps->line, msg);
}

JsonVal *json_new(int type) {
  JsonVal *v = (JsonVal *)calloc(1, sizeof *v);
  if (v) v->type = type;
  return v;
}

JsonVal *json_num(double n) {
  JsonVal *v = json_new(JS_NUM);
  if (v) v->n = n;
  return v;
}

JsonVal *json_str(const char *s) {
  JsonVal *v = json_new(JS_STR);
  if (v) v->s = mods_strdup(s ? s : "");
  return v;
}

JsonVal *json_bool(int b) {
  JsonVal *v = json_new(JS_BOOL);
  if (v) v->b = b != 0;
  return v;
}

void json_free(JsonVal *v) {
  if (!v) return;
  for (int i = 0; i < v->len; i++) {
    json_free(v->items[i]);
    if (v->keys) free(v->keys[i]);
  }
  free(v->items);
  free(v->keys);
  free(v->s);
  free(v);
}

JsonVal *json_clone(const JsonVal *v) {
  if (!v) return NULL;
  JsonVal *c = json_new(v->type);
  c->line = v->line;
  c->b = v->b;
  c->n = v->n;
  if (v->s) c->s = mods_strdup(v->s);
  for (int i = 0; i < v->len; i++) {
    if (v->type == JS_OBJ) json_set(c, v->keys[i], json_clone(v->items[i]));
    else json_push(c, json_clone(v->items[i]));
    c->items[c->len - 1]->line = v->items[i]->line;
  }
  return c;
}

static void grow(JsonVal *v) {
  if (v->len < v->cap) return;
  v->cap = v->cap ? v->cap * 2 : 8;
  v->items = (JsonVal **)realloc(v->items, (size_t)v->cap * sizeof *v->items);
  if (v->type == JS_OBJ) v->keys = (char **)realloc(v->keys, (size_t)v->cap * sizeof *v->keys);
}

void json_push(JsonVal *arr, JsonVal *val) {
  grow(arr);
  if (arr->keys) arr->keys[arr->len] = NULL;
  arr->items[arr->len++] = val;
}

int json_index(const JsonVal *obj, const char *key) {
  if (!obj || obj->type != JS_OBJ || !key) return -1;
  for (int i = 0; i < obj->len; i++)
    if (!strcmp(obj->keys[i], key)) return i;
  return -1;
}

JsonVal *json_get(const JsonVal *obj, const char *key) {
  int i = json_index(obj, key);
  return i < 0 ? NULL : obj->items[i];
}

void json_set(JsonVal *obj, const char *key, JsonVal *val) {
  int i = json_index(obj, key);
  if (i >= 0) {
    json_free(obj->items[i]);
    obj->items[i] = val;
    return;
  }
  grow(obj);
  obj->keys[obj->len] = mods_strdup(key);
  obj->items[obj->len++] = val;
}

int json_del(JsonVal *obj, const char *key) {
  int i = json_index(obj, key);
  if (i < 0) return 0;
  json_free(obj->items[i]);
  free(obj->keys[i]);
  memmove(&obj->items[i], &obj->items[i + 1], (size_t)(obj->len - i - 1) * sizeof *obj->items);
  memmove(&obj->keys[i], &obj->keys[i + 1], (size_t)(obj->len - i - 1) * sizeof *obj->keys);
  obj->len--;
  return 1;
}

int json_is_int(const JsonVal *v) {
  return v && v->type == JS_NUM && isfinite(v->n) && floor(v->n) == v->n && fabs(v->n) < 9007199254740992.0;
}

const char *json_type_name(int type) {
  static const char *n[] = {"null", "boolean", "number", "string", "array", "object"};
  return type >= 0 && type <= JS_OBJ ? n[type] : "?";
}

int json_equal(const JsonVal *a, const JsonVal *b) {
  if (!a || !b) return a == b;
  if (a->type != b->type) return 0;
  switch (a->type) {
    case JS_NULL: return 1;
    case JS_BOOL: return a->b == b->b;
    case JS_NUM: return a->n == b->n;
    case JS_STR: return !strcmp(a->s, b->s);
    case JS_ARR:
      if (a->len != b->len) return 0;
      for (int i = 0; i < a->len; i++)
        if (!json_equal(a->items[i], b->items[i])) return 0;
      return 1;
    case JS_OBJ:
      if (a->len != b->len) return 0;
      for (int i = 0; i < a->len; i++)
        if (!json_equal(a->items[i], json_get(b, a->keys[i]))) return 0;
      return 1;
  }
  return 0;
}

// ------------------------------------------------------------------ reader

static void skip_ws(Parser *ps) {
  for (;;) {
    char c = *ps->p;
    if (c == '\n') ps->line++, ps->p++;
    else if (c == ' ' || c == '\t' || c == '\r') ps->p++;
    else break;
  }
}

static void put_utf8(char **o, unsigned cp) {
  char *q = *o;
  if (cp < 0x80) *q++ = (char)cp;
  else if (cp < 0x800) { *q++ = (char)(0xc0 | cp >> 6); *q++ = (char)(0x80 | (cp & 0x3f)); }
  else if (cp < 0x10000) {
    *q++ = (char)(0xe0 | cp >> 12); *q++ = (char)(0x80 | (cp >> 6 & 0x3f)); *q++ = (char)(0x80 | (cp & 0x3f));
  } else {
    *q++ = (char)(0xf0 | cp >> 18); *q++ = (char)(0x80 | (cp >> 12 & 0x3f));
    *q++ = (char)(0x80 | (cp >> 6 & 0x3f)); *q++ = (char)(0x80 | (cp & 0x3f));
  }
  *o = q;
}

static int hex4(const char *p, unsigned *out) {
  unsigned v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
    else return 0;
  }
  *out = v;
  return 1;
}

static char *parse_string(Parser *ps) {
  // ps->p at the opening quote
  const char *s = ++ps->p;
  size_t n = 0;
  while (s[n] && s[n] != '"') {
    if (s[n] == '\\' && s[n + 1]) n++;
    if (s[n] == '\n') break;
    n++;
  }
  char *out = (char *)malloc(n * 2 + 8), *o = out;
  for (;;) {
    char c = *ps->p;
    if (!c || c == '\n') { perr(ps, "a string is not closed (missing \")"); free(out); return NULL; }
    if ((unsigned char)c < 0x20) { perr(ps, "control character in a string"); free(out); return NULL; }
    ps->p++;
    if (c == '"') break;
    if (c != '\\') { *o++ = c; continue; }
    c = *ps->p++;
    switch (c) {
      case '"': *o++ = '"'; break;
      case '\\': *o++ = '\\'; break;
      case '/': *o++ = '/'; break;
      case 'b': *o++ = '\b'; break;
      case 'f': *o++ = '\f'; break;
      case 'n': *o++ = '\n'; break;
      case 'r': *o++ = '\r'; break;
      case 't': *o++ = '\t'; break;
      case 'u': {
        unsigned cp, lo;
        if (!hex4(ps->p, &cp)) { perr(ps, "bad \\u escape"); free(out); return NULL; }
        ps->p += 4;
        // JsonVal strings and object keys are NUL-terminated. Accepting an
        // embedded NUL would silently discard the rest of a value or key.
        if (cp == 0) { perr(ps, "embedded NUL is not supported in strings"); free(out); return NULL; }
        if (cp >= 0xd800 && cp < 0xdc00 && ps->p[0] == '\\' && ps->p[1] == 'u' && hex4(ps->p + 2, &lo) &&
            lo >= 0xdc00 && lo < 0xe000) {
          cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
          ps->p += 6;
        }
        put_utf8(&o, cp);
        break;
      }
      default: perr(ps, "bad escape \\%c in a string", c ? c : '?'); free(out); return NULL;
    }
  }
  *o = 0;
  return out;
}

static JsonVal *parse_value(Parser *ps);

static JsonVal *parse_container(Parser *ps, int type) {
  JsonVal *v = json_new(type);
  v->line = ps->line;
  char close = type == JS_OBJ ? '}' : ']';
  ps->p++;
  if (++ps->depth > 64) { perr(ps, "nested too deeply"); json_free(v); return NULL; }
  skip_ws(ps);
  if (*ps->p == close) { ps->p++; ps->depth--; return v; }
  for (;;) {
    skip_ws(ps);
    char *key = NULL;
    int key_line = ps->line;
    if (type == JS_OBJ) {
      if (*ps->p != '"') { perr(ps, "expected a \"key\" in an object (keys and strings need \"double quotes\")"); json_free(v); return NULL; }
      key = parse_string(ps);
      if (!key) { json_free(v); return NULL; }
      if (json_index(v, key) >= 0) {
        perr(ps, "duplicate key \"%s\"", key);
        free(key); json_free(v); return NULL;
      }
      skip_ws(ps);
      if (*ps->p != ':') { perr(ps, "expected ':' after \"%s\"", key); free(key); json_free(v); return NULL; }
      ps->p++;
    }
    (void)key_line;
    JsonVal *item = parse_value(ps);
    if (!item) { free(key); json_free(v); return NULL; }
    if (type == JS_OBJ) { json_set(v, key, item); free(key); }
    else json_push(v, item);
    skip_ws(ps);
    if (*ps->p == ',') {
      ps->p++;
      skip_ws(ps);
      if (*ps->p == close) { perr(ps, "trailing comma before '%c'", close); json_free(v); return NULL; }
      continue;
    }
    if (*ps->p == close) { ps->p++; ps->depth--; return v; }
    perr(ps, *ps->p ? "expected ',' or '%c'" : "the file ends before '%c'", close);
    json_free(v);
    return NULL;
  }
}

static JsonVal *parse_value(Parser *ps) {
  skip_ws(ps);
  const char *p = ps->p;
  JsonVal *v = NULL;
  int line = ps->line;
  if (*p == '{') return parse_container(ps, JS_OBJ);
  if (*p == '[') return parse_container(ps, JS_ARR);
  if (*p == '"') {
    char *s = parse_string(ps);
    if (!s) return NULL;
    v = json_new(JS_STR);
    v->s = s;
  } else if (!strncmp(p, "true", 4)) { v = json_bool(1); ps->p += 4; }
  else if (!strncmp(p, "false", 5)) { v = json_bool(0); ps->p += 5; }
  else if (!strncmp(p, "null", 4)) { v = json_new(JS_NULL); ps->p += 4; }
  else if (*p == '-' || (*p >= '0' && *p <= '9')) {
    const char *q = p;
    if (*q == '-') q++;
    if (!(*q >= '0' && *q <= '9')) { perr(ps, "bad number"); return NULL; }
    if (*q == '0' && q[1] >= '0' && q[1] <= '9') { perr(ps, "numbers cannot start with 0"); return NULL; }
    // task-114o: only the JSON grammar -?int(.digits)?([eE][+-]?digits)?; strtod alone also takes 0x10, 1. and 1e
    while (*q >= '0' && *q <= '9') q++;
    if (*q == '.') {
      q++;
      if (!(*q >= '0' && *q <= '9')) { perr(ps, "a number needs a digit after the point"); return NULL; }
      while (*q >= '0' && *q <= '9') q++;
    }
    if (*q == 'e' || *q == 'E') {
      q++;
      if (*q == '+' || *q == '-') q++;
      if (!(*q >= '0' && *q <= '9')) { perr(ps, "an exponent needs digits"); return NULL; }
      while (*q >= '0' && *q <= '9') q++;
    }
    size_t len = (size_t)(q - p);
    char num[64];
    if (len >= sizeof num) { perr(ps, "number too long"); return NULL; }
    memcpy(num, p, len);
    num[len] = 0;
    double n = strtod(num, NULL);
    if (n > 1.7e308 || n < -1.7e308) { perr(ps, "number out of range"); return NULL; }
    v = json_num(n);
    ps->p = q;
  } else if (!*p) {
    perr(ps, "the file ends where a value was expected");
    return NULL;
  } else {
    perr(ps, "unexpected '%c' (strings need \"double quotes\")", *p);
    return NULL;
  }
  v->line = line;
  return v;
}

JsonVal *json_parse(const char *text, char *err, size_t errlen) {
  Parser ps = {text ? text : "", 1, err, errlen, 0, 0};
  if (err && errlen) err[0] = 0;
  // A UTF-8 byte order mark is allowed (Windows editors write one).
  if ((unsigned char)ps.p[0] == 0xef && (unsigned char)ps.p[1] == 0xbb && (unsigned char)ps.p[2] == 0xbf) ps.p += 3;
  JsonVal *v = parse_value(&ps);
  if (!v) {
    if (!ps.failed) perr(&ps, "not JSON");
    return NULL;
  }
  skip_ws(&ps);
  if (*ps.p) {
    perr(&ps, "unexpected text after the end of the value");
    json_free(v);
    return NULL;
  }
  return v;
}

JsonVal *json_load(const char *path, char *err, size_t errlen) {
  size_t len;
  char *text = mods_read_file(path, &len);
  if (!text) {
    if (err && errlen) snprintf(err, errlen, "%s: cannot read the file", path);
    return NULL;
  }
  const char *nul = memchr(text, 0, len);
  if (nul) {
    int line = 1;
    for (const char *p = text; p < nul; p++) if (*p == '\n') line++;
    if (err && errlen) snprintf(err, errlen, "%s:%d: embedded NUL in JSON file", path, line);
    free(text);
    return NULL;
  }
  char e[300];
  JsonVal *v = json_parse(text, e, sizeof e);
  free(text);
  if (!v && err && errlen) {
    // "line N: what" -> "path:N: what"
    int line = 0;
    const char *msg = e;
    if (sscanf(e, "line %d:", &line) == 1 && (msg = strchr(e, ':'))) msg += 2;
    snprintf(err, errlen, "%s:%d: %s", path, line, msg);
  }
  return v;
}

// ------------------------------------------------------------------ writer

typedef struct {
  char *b;
  size_t n, cap;
} Buf;

static void put(Buf *b, const char *s, size_t n) {
  if (b->n + n + 1 > b->cap) {
    b->cap = (b->n + n + 1) * 2;
    b->b = (char *)realloc(b->b, b->cap);
  }
  memcpy(b->b + b->n, s, n);
  b->n += n;
  b->b[b->n] = 0;
}

static void puts_(Buf *b, const char *s) { put(b, s, strlen(s)); }

static void put_str(Buf *b, const char *s) {
  puts_(b, "\"");
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    char e[8];
    if (*p == '"') puts_(b, "\\\"");
    else if (*p == '\\') puts_(b, "\\\\");
    else if (*p == '\n') puts_(b, "\\n");
    else if (*p == '\r') puts_(b, "\\r");
    else if (*p == '\t') puts_(b, "\\t");
    else if (*p < 0x20) { snprintf(e, sizeof e, "\\u%04x", *p); puts_(b, e); }
    else put(b, (const char *)p, 1);
  }
  puts_(b, "\"");
}

static int is_flat(const JsonVal *v) {
  for (int i = 0; i < v->len; i++)
    if (v->items[i]->type == JS_ARR || v->items[i]->type == JS_OBJ) return 0;
  return 1;
}

static void write_val(Buf *b, const JsonVal *v, int indent, int compact) {
  char tmp[64];
  switch (v->type) {
    case JS_NULL: puts_(b, "null"); return;
    case JS_BOOL: puts_(b, v->b ? "true" : "false"); return;
    case JS_NUM:
      if (json_is_int(v)) snprintf(tmp, sizeof tmp, "%.0f", v->n);
      else snprintf(tmp, sizeof tmp, "%.17g", v->n);
      puts_(b, tmp);
      return;
    case JS_STR: put_str(b, v->s); return;
  }
  int obj = v->type == JS_OBJ;
  if (!v->len) { puts_(b, obj ? "{}" : "[]"); return; }
  // Short flat containers stay on one line.
  int one_line = compact || (is_flat(v) && v->len <= 8);
  puts_(b, obj ? "{" : "[");
  for (int i = 0; i < v->len; i++) {
    if (i) puts_(b, one_line ? ", " : ",");
    if (!one_line) {
      puts_(b, "\n");
      for (int k = 0; k < indent + 2; k++) puts_(b, " ");
    }
    if (obj) { put_str(b, v->keys[i]); puts_(b, ": "); }
    write_val(b, v->items[i], indent + 2, compact);
  }
  if (!one_line) {
    puts_(b, "\n");
    for (int k = 0; k < indent; k++) puts_(b, " ");
  }
  puts_(b, obj ? "}" : "]");
}

char *json_write(const JsonVal *v) {
  Buf b = {0};
  write_val(&b, v, 0, 0);
  puts_(&b, "\n");
  return b.b;
}

char *json_write_compact(const JsonVal *v) {
  Buf b = {0};
  write_val(&b, v, 0, 1);
  return b.b;
}
