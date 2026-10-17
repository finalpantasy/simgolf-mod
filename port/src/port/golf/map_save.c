#include "port/course_lamps.h"
// Map size in saves (task-8). The game's saves (.sve, FUN_0040afa0) and
// course files (FUN_0040bbf0) are flat lists of fields written by
// FUN_0040af70, with no header. Both lists start with the 4 bytes at
// 0x822c78. A map other than 50 x 50 writes a 16-byte tag in front of that
// field: "SGMP", version 1, width, height. Original-size files stay byte
// for byte the original format.
//
// Reading a file resizes the map to the file's size before any per-tile
// field is read (no tag = 50 x 50), so every save opens at the size it was
// made, whatever map_scale says; map_scale picks the size of new games.

#include "golf_protos.h"
#include "port/grid.h"
#include "port/camera_tiles.h"
#include "port/staff.h"
#include "port/roster.h"

#define SAVE_FD (*(int *)0x568d08)        // DAT_00568d08: open save file
#define SAVE_READING (*(int *)0x53e638)   // DAT_0053e638: 1 load, 0 save
#define FIRST_FIELD 0x822c78
#define LAST_COMMON_FIELD 0x5a5a00

void save_camera_focus(void);
void place_drag_reset(void);
#ifndef __EMSCRIPTEN__
void path_styles_save_field(int fd, int reading);
void path_styles_loaded(void);
void living_save_field(int fd, int reading);
#endif

// The tag itself: grid_save_tag in grid.c (this file sees only the game's
// own types, which clash with the C library's).

// @override FUN_0040af70
int FUN_0040af70(int param_1, int param_2) {
  if (param_1 == FIRST_FIELD && param_2 == 4) {
    if (SAVE_READING) place_drag_reset();
    grid_save_tag(SAVE_FD, SAVE_READING);
    staff_save_tag(SAVE_FD, SAVE_READING);
    camera_tiles_save_field(SAVE_FD, SAVE_READING);
    lamps_game_save_field(SAVE_FD, SAVE_READING);
#ifndef __EMSCRIPTEN__
    path_styles_save_field(SAVE_FD, SAVE_READING);
    living_save_field(SAVE_FD, SAVE_READING);
#endif
  }
  unsigned address = roster_address((unsigned)param_1);
  if (SAVE_READING != 0) {
    int n = crt_read(SAVE_FD, address, param_2);
    if (param_1 == LAST_COMMON_FIELD && param_2 == 4 && n == 4) {
      save_camera_focus();
#ifndef __EMSCRIPTEN__
      path_styles_loaded();
#endif
    }
    return n;
  }
  return crt_write(SAVE_FD, address, param_2);
}
