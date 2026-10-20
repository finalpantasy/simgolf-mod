// Frosted Mint UI kit (task-26): line icons.
//
// The mockups' icon set (docs/ui/mockups/round2.js ICONS): 24 px grid, 1.8 px
// round strokes. Each icon is rasterised from its path at the exact pixel size
// it is drawn at (a distance field to the flattened path, so caps and joins are
// round and edges anti-aliased), white with coverage in alpha, then tinted.
// Plain C apart from the texture cache in ui_kit.c: unit tested.
#pragma once

#include <stdint.h>

typedef enum {
  UI_ICON_NONE, UI_ICON_PLAY, UI_ICON_PAUSE, UI_ICON_FAST, UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_ROTL, UI_ICON_ROTR,
  UI_ICON_UNDO, UI_ICON_REDO, UI_ICON_FLAG, UI_ICON_HOUSE, UI_ICON_MOUNTAIN, UI_ICON_USERS, UI_ICON_CHART,
  UI_ICON_GEAR, UI_ICON_COINS, UI_ICON_SMILE, UI_ICON_TROPHY, UI_ICON_STAR, UI_ICON_FOLDER, UI_ICON_SPARKLE,
  UI_ICON_PALETTE, UI_ICON_CUP, UI_ICON_DOOR, UI_ICON_BACK, UI_ICON_CHEV, UI_ICON_LOCK, UI_ICON_X, UI_ICON_EYE,
  UI_ICON_CAMERA, UI_ICON_BELL, UI_ICON_INFO, UI_ICON_PATH, UI_ICON_TREE, UI_ICON_PIN, UI_ICON_TRASH, UI_ICON_CHECK, UI_ICON_UP, UI_ICON_DOWN,
  UI_ICON_WEATHER_SUN, UI_ICON_WEATHER_MOON, UI_ICON_WEATHER_PARTLY, UI_ICON_WEATHER_CLOUD, UI_ICON_WEATHER_RAIN, UI_ICON_WEATHER_STORM,
  UI_ICON_GOLF_BALL, UI_ICON_HEART, UI_ICON_COUNT
} UiIcon;

const char *ui_icon_name(UiIcon i);
UiIcon ui_icon_by_name(const char *name);   // UI_ICON_NONE when unknown

// Rasterises icon i into a size x size alpha mask (row-major, size*size bytes).
// stroke is in grid units (1.8 for the design). Returns the number of
// segments drawn (0: unknown icon).
int ui_icon_raster(UiIcon i, int size, float stroke, uint8_t *alpha);
