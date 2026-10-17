#include "mods/modconfig.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *modconfig_text(const JsonVal *obj, const char *key) {
  const JsonVal *v = json_get(obj, key);
  return v && v->type == JS_STR ? v->s : "";
}

int modconfig_choice(const JsonVal *s, const char *value) {
  const JsonVal *a = json_get(s, "choices");
  if (!a || a->type != JS_ARR || !value) return -1;
  for (int i = 0; i < a->len; i++)
    if (!strcmp(modconfig_text(a->items[i], "value"), value)) return i;
  // Equivalent numeric spellings are the same choice, not a custom value.
  char *end;
  double v = strtod(value, &end);
  if (end != value && !*end) for (int i = 0; i < a->len; i++) {
    const char *s = modconfig_text(a->items[i], "value");
    double choice = strtod(s, &end);
    if (end != s && !*end && choice == v) return i;
  }
  return -1;
}

static int text_ok(const char *s, size_t max) {
  if (!s || !*s || strlen(s) > max) return 0;
  for (; *s; s++) if ((unsigned char)*s < 32 || *s == 127) return 0;
  return 1;
}

int modconfig_validate(const JsonVal *catalog, char *err, size_t n) {
  const JsonVal *ver = json_get(catalog, "version"), *a = json_get(catalog, "settings");
  const char *why = "Expected a version 1 settings catalog.";
  if (!catalog || catalog->type != JS_OBJ || !ver || !json_is_int(ver) || ver->n != 1 ||
      !a || a->type != JS_ARR || a->len > 200) goto bad;
  for (int i = 0; i < a->len; i++) {
    const JsonVal *s = a->items[i], *choices = json_get(s, "choices");
    const char *key = modconfig_text(s, "key"), *when = modconfig_text(s, "timing");
    why = "A setting needs a mod, a key, English guidance and valid choices.";
    if (!text_ok(modconfig_text(s, "mod"), 63) || !text_ok(key, 127) || !strchr(key, '.') ||
        !strncmp(key, "mods.", 5) || !strncmp(key, "config.", 7) || !text_ok(modconfig_text(s, "label"), 80) ||
        !text_ok(modconfig_text(s, "help"), 600) ||
        (strcmp(when, "live") && strcmp(when, "restart") && strcmp(when, "new_course")) ||
        !choices || choices->type != JS_ARR || choices->len < 2 || choices->len > 32) goto bad;
    for (const char *p = key; *p; p++) if (!isalnum((unsigned char)*p) && *p != '_' && *p != '.') goto bad;
    for (int j = 0; j < choices->len; j++) {
      const char *v = modconfig_text(choices->items[j], "value");
      if (!text_ok(v, 127) || !text_ok(modconfig_text(choices->items[j], "label"), 80)) goto bad;
      for (int k = 0; k < j; k++) if (!strcmp(v, modconfig_text(choices->items[k], "value"))) goto bad;
    }
    if (modconfig_choice(s, modconfig_text(s, "default")) < 0) goto bad;
    for (int j = 0; j < i; j++)
      if (!strcmp(key, modconfig_text(a->items[j], "key"))) { why = "A setting key appears more than once."; goto bad; }
  }
  if (n) *err = 0;
  return 1;
bad:
  if (n) snprintf(err, n, "%s", why);
  return 0;
}

JsonVal *modconfig_load(const char *path, char *err, size_t n) {
  JsonVal *v = json_load(path, err, n);
  if (v && !modconfig_validate(v, err, n)) { json_free(v); return NULL; }
  return v;
}
