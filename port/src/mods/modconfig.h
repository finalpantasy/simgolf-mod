// Player-facing choices, shared by the native Mods page and validation tests.
#pragma once
#include "mods/json.h"

// A catalog is data, never executable code. Invalid catalogs are rejected whole.
JsonVal *modconfig_load(const char *path, char *err, size_t n);
int modconfig_validate(const JsonVal *catalog, char *err, size_t n);
const char *modconfig_text(const JsonVal *obj, const char *key);
int modconfig_choice(const JsonVal *setting, const char *value);
