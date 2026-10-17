#pragma once
#include <SDL.h>
#include "port/course_lamps.h"
int lamps_game_event(SDL_Event *e);
void lamps_render(SDL_Renderer *r,const LampFrame *f,SDL_Rect clip,const uint8_t *land,int w,int h,double ox,double oy,double scale,double night);
