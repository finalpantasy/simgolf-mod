#ifndef SIMGOLF_RAINBOW_H
#define SIMGOLF_RAINBOW_H
#include <SDL.h>
typedef struct {double rain_charge,age,intensity;unsigned episode;int armed,clearing;} RainbowState;
typedef struct {SDL_Texture *texture;SDL_Renderer *renderer;int width,height;double sun;} RainbowDraw;
void rainbow_update(RainbowState *state,double rain,double cloud,double daylight,double dt,unsigned seed);
void rainbow_destroy(RainbowDraw *draw);
int rainbow_draw(RainbowDraw *draw,SDL_Renderer *r,SDL_Rect view,const unsigned char *receiver,
 int w,int h,double ox,double oy,double scale,double strength,double anti_sun);
#endif
