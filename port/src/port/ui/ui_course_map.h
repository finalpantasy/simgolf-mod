#pragma once
#include <stdint.h>
#include "port/ui/ui_kit.h"

#define UI_MAP_SIDE 160
#define UI_MAP_CELLS (UI_MAP_SIDE * UI_MAP_SIDE)
typedef struct {
  int valid, width, height, mode;
  uint8_t terrain[UI_MAP_CELLS], path[UI_MAP_CELLS];
  uint16_t heat[UI_MAP_CELLS];
  struct { int active, n; float x[4], y[4]; } hole[18];
} UiCourseMap;

// All shapes and labels are drawn at the target's resolution. No game-page bitmap.
void ui_course_map_draw(UiCtx *c, const UiCourseMap *map, UiRect bounds);
void ui_course_map_clear(SDL_Renderer *renderer); // before destroying the owning renderer
