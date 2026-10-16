// Bounded 2.5D lighting for the original sprite artwork. No source art is changed.
#ifndef SIMGOLF_COURSE_LIGHT_H
#define SIMGOLF_COURSE_LIGHT_H
#include <SDL.h>
#include <stdint.h>
typedef struct {
  double ambient, surfaces, contact, softness, lamps, lamp_radius;
  double daylight, strength, elevation, light_x, light_y;
  int linear;
  double enclosure_left,enclosure_right;
} CourseLight;
typedef struct { char asset[100]; int view; float x,y,w,h,ground_x,ground_y; } CourseWindow;
#define COURSE_WINDOW_LIMIT 128
typedef struct { CourseWindow windows[COURSE_WINDOW_LIMIT]; int count; } CourseWindows;
typedef struct {int cols,rows;double cell;float density[128*128],ground[128*128];} CourseEnclosure;
typedef struct {
  SDL_Texture *texture;SDL_Renderer *renderer;
  float ground[128*128];uint8_t pixels[128*128*4];
  int cols,rows,linear,valid;double cell,strength;unsigned uploads;
} CourseEnclosureDraw;
void course_enclosure_draw_destroy(CourseEnclosureDraw *cache);
// Cached low-frequency shelter field, with current ground ownership every draw.
int course_enclosure_draw(CourseEnclosureDraw *cache,SDL_Renderer *renderer,
 const CourseEnclosure *field,const uint8_t *ground,int width,int height,
 double ox,double oy,double scale,double strength,int linear);
void course_enclosure_begin(CourseEnclosure *field,int width,int height);
void course_enclosure_add(CourseEnclosure *field,double x,double y,double rx,double ry);
double course_enclosure_sample(const CourseEnclosure *field,double x,double y,double self_x,double self_y,double rx,double ry);
// Atomic, strict profile loading. Coordinates are normalized in the source image.
int course_windows_load(CourseWindows *out,const char *path);
double course_light_night(const CourseLight *light);
void course_light_color(const CourseLight *light,int building,double u,double v,Uint8 rgb[3]);
int course_light_sprite(SDL_Renderer *r,SDL_Texture *tx,SDL_FRect d,int flip,int building,const CourseLight *light);
// Build an emissive mask only inside authored window regions and opaque dark glass.
uint8_t *course_light_windows(const CourseWindows *profiles,const char *asset,int view,
                             const uint8_t *rgba,int w,int h,int *lit_pixels);
// Additive ground spill clips against land receiver cells and the caller's clip.
int course_light_spill(SDL_Renderer *r,const uint8_t *receiver,int w,int h,double ox,double oy,double scale,
                       double x,double y,double radius,double gain);
#endif
