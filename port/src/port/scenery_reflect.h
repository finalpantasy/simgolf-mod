#ifndef SIMGOLF_SCENERY_REFLECT_H
#define SIMGOLF_SCENERY_REFLECT_H
#include <SDL.h>
// Receiver values are the existing course classifications, with160 for water.
// No texture readback or allocation. Returns submitted water spans, or -1.
int scenery_reflect_draw(SDL_Renderer *r,SDL_Texture *sprite,SDL_FRect source,
 double anchor_y,int flip,const unsigned char *receiver,int width,int height,
 double ox,double oy,double scale,double seconds,double strength,double ripple);
#endif
