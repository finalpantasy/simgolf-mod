#ifndef SIMGOLF_PATH_STYLE_CATALOG_H
#define SIMGOLF_PATH_STYLE_CATALOG_H

enum { PATH_STYLE_SERIAL_STRIDE = 32, PATH_STYLE_COUNT = 0
#define PATH_STYLE(id, key, name, theme) + 1
#include "../../../data/path-styles.def"
#undef PATH_STYLE
};
typedef char PathStyleCapacity[(PATH_STYLE_COUNT <= PATH_STYLE_SERIAL_STRIDE) ? 1 : -1];

static inline const char *path_style_id(int style) {
  switch(style) {
#define PATH_STYLE(id, key, name, theme) case id: return #key;
#include "../../../data/path-styles.def"
#undef PATH_STYLE
    default: return "original";
  }
}
static inline const char *path_style_name(int style) {
  switch(style) {
#define PATH_STYLE(id, key, name, theme) case id: return name;
#include "../../../data/path-styles.def"
#undef PATH_STYLE
    default: return "Original";
  }
}
#endif
