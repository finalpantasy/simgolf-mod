#pragma once
#include <SDL.h>
// Active gestures see releases before the HUD. New gestures start after it.
int place_drag_event(SDL_Event *e, int allow_start);
int place_drag_next(void);
void place_drag_pick(int *x, int *y);
void place_drag_draw(void);
int place_drag_afford(int cost);
