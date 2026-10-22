// Modern camera controls (simgolf mod, task-15): drag to pan with the middle
// or right mouse button, W/A/S/D to pan, the mouse wheel to zoom, and the
// game's own edge scrolling switchable. Settings: [camera] in golf.ini next to
// the exe (camera_input.c lists them); every one off gives the original.
//
// win32.c's SDL event pump hands each event to camera_input_event() first and
// calls camera_input_tick() once per yield of the game loop.

#ifndef SIMGOLF_PORT_GOLF_CAMERA_INPUT_H
#define SIMGOLF_PORT_GOLF_CAMERA_INPUT_H

#include <SDL.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 when the camera took the event (the game must not see it). May
// adjust the event in place (pointer kept off the window edge).
int camera_input_event(SDL_Event *e);
// Modal entry and events consumed before camera_input_event must retire real
// pointer ownership without injecting a click. Safe to call repeatedly.
void camera_input_cancel_gesture(void);
void camera_input_consumed_event(const SDL_Event *e);
void camera_input_tick(void);
// Copied into a finished course frame so the presenter can distinguish a
// fast manual drag from a game-directed camera jump, even after release.
uint32_t camera_input_drag_serial(void);
// The key bindings (golf/keymap.c, card task-24): 1 while the course view is in charge of the keys
// (dialogs, menus and text entry get them as typed); the profile (camera.wasd); a pan key pressed or released
// by its binding (dir 0 up, 1 left, 2 down, 3 right), whatever camera.wasd says.
int camera_input_active(void);
int camera_input_wasd(void);
void camera_input_pan_key(int dir, int down);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_GOLF_CAMERA_INPUT_H
