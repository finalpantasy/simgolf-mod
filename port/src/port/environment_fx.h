#ifndef SIMGOLF_ENVIRONMENT_FX_H
#define SIMGOLF_ENVIRONMENT_FX_H

#include <SDL.h>
#include <stddef.h>
#include <stdint.h>
#include "port/weather_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EnvironmentFxState EnvironmentFxState;

typedef enum EnvironmentFxBackend {
  ENVIRONMENT_FX_BACKEND_NONE = 0,
  ENVIRONMENT_FX_BACKEND_SDL = 1,
  ENVIRONMENT_FX_BACKEND_D3D11 = 2
} EnvironmentFxBackend;

typedef struct EnvironmentFxStatus {
  EnvironmentFxBackend backend;
  int shader_compile_attempted;
  int shader_compiled;
  int receiver_uploaded;
  char detail[160];
} EnvironmentFxStatus;

// One byte per source pixel: 0 neither, 64 objects, 127 ground, 160 water, 255 background.
// origin_x/y locate source pixel (0,0) in renderer output coordinates.
// pixel_scale is the number of output pixels covered by one source pixel.
// Keep serial stable while pixels and dimensions are unchanged. A new serial
// uploads the mask once; origin and scale may change for a smooth camera move.
typedef struct EnvironmentFxReceiver {
  const uint8_t *pixels;
  int width, height, pitch;
  uint64_t serial;
  double origin_x, origin_y, pixel_scale;
  const uint8_t *features; // optional3bytes per source pixel: normal, valley, fog-height/material
} EnvironmentFxReceiver;

// Draws the optional atmosphere into the active SDL render target. clip and
// receiver origin use active target pixel coordinates. Weather values come
// from the shared deterministic presentation sample. State is created lazily.
void environment_fx_draw(EnvironmentFxState **state, SDL_Renderer *renderer,
                         SDL_Rect clip, const EnvironmentFxReceiver *receiver,
                         const WeatherState *weather, double camera_x,
                         double camera_y, double scale, int quality,
                         int glowbugs, int night_sky, int reduced_flashes,
                         int shader_enabled);
// Destroy before the owner destroys/reuses a target used by this state.
// A new target renders one ordinary SDL frame before the shader resumes.
void environment_fx_destroy(EnvironmentFxState **state);
void environment_fx_get_status(const EnvironmentFxState *state,
                               EnvironmentFxStatus *status);

#if defined(SIMGOLF_ENVIRONMENT_FX_TESTING)
int environment_fx_test_compile_shader(char *detail, size_t detail_size);
#endif

#ifdef __cplusplus
}
#endif
#endif
