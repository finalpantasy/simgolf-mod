#ifndef SIMGOLF_PATH_STYLES_H
#define SIMGOLF_PATH_STYLES_H
#include "port/path_style_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
int path_styles_selected(void);
void path_styles_select(int style);
int path_styles_at(int x, int y);
void path_styles_frame(void);
void path_styles_reset(void);
void path_styles_loaded(void);
void path_styles_save_field(int fd, int reading);
int path_styles_written(int fd, const char *path);
void path_styles_closed(int fd);
int path_styles_get(int x, int y);
int path_styles_paint(int x, int y, int style);
#ifdef __cplusplus
}
#endif
#endif
