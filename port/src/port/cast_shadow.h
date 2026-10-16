// Independently implemented silhouette projection. See docs/SPRITE_SHADOWS.md.
#ifndef SIMGOLF_CAST_SHADOW_H
#define SIMGOLF_CAST_SHADOW_H
#include <SDL.h>
#include <stdint.h>
#include <stddef.h>
#include "port/course_relief.h"

#define CAST_SHADOW_CACHE_LIMIT (16u * 1024u * 1024u)
#define CAST_SHADOW_CACHE_SLOTS 256
typedef struct {
  float x, y, radius, coverage, thickness;
} CastShadowSlice;
typedef struct {
  CastShadowSlice *slices;
  SDL_Texture *silhouette;
  size_t bytes;
  uint64_t key;
  unsigned used;
  int count;
} CastShadowMask;
typedef struct {
  SDL_Texture *target, *disc;
  CastShadowMask *masks;
  size_t bytes;
  unsigned tick;
  int width, height, builds, active;
  SDL_Renderer *renderer;
  SDL_Texture *previous_target;
  SDL_Rect viewport, clip, destination;
  SDL_bool clipped;
  float scale_x, scale_y;
  SDL_BlendMode blend;
  Uint8 red, green, blue, alpha;
  double factor, slope_x, slope_y, opacity, ground_aspect;
  double softness, contact;
  int linear;
  int image_valid;
  double pixel_scale;
  uint8_t *software_pixels;size_t software_bytes;int software;
  const CourseRelief *relief; // borrowed current source geometry, reapplied at composite
} CastShadowRenderer;

// Above-ground rows project onto the ground; rows below the anchor retain
// the building's ground footprint. All coordinates use the same pixel units.
void cast_shadow_project(double x, double y, double anchor_y, double slope_x,
                         double slope_y, double *out_x, double *out_y);
// begin/end preserve renderer target, viewport, clipping, scale and draw state.
// Disabled destroys all owned resources and does not touch renderer state.
int cast_shadow_begin(CastShadowRenderer *state, SDL_Renderer *renderer,
                      SDL_Rect clip, double pixel_scale, int enabled,
                      double slope_x, double slope_y, double opacity, double ground_aspect);
// Caller proves caster geometry/light shape unchanged. Ownership is still
// applied fresh by end, so cached shadows cannot cover new object/UI pixels.
int cast_shadow_reuse(CastShadowRenderer *state, SDL_Renderer *renderer,
                      SDL_Rect clip, double pixel_scale, double opacity);
int cast_shadow_draw(CastShadowRenderer *state, uint64_t key, SDL_Texture *body,
                     int width, int height, double anchor_x, double anchor_y,
                     SDL_FRect destination, int flip);
int cast_shadow_building(CastShadowRenderer *state, double anchor_x, double anchor_y,
                         SDL_FRect destination, int flip);
int cast_shadow_end(CastShadowRenderer *state, const uint8_t *ground, int width,
                     int height, double ox, double oy, double scale);
void cast_shadow_destroy(CastShadowRenderer *state);
#endif
