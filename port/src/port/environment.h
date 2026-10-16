// Optional presentation effects. No simulation, save, input or audio changes.
#ifndef SIMGOLF_ENVIRONMENT_H
#define SIMGOLF_ENVIRONMENT_H
#include <SDL.h>
#include "port/atmosphere.h"

typedef struct {
  int enabled, day_night, weather; // weather: 0 clear, 1 cloudy, 2 rain
  double hour, cycle_minutes, cloud_strength, rain_strength;
  int lighting, cast_shadows, terrain_shadows, occlusion, quality;
  int shader, glowbugs, night_sky, lightning, reduced_flashes, sound, gameplay;
  double weather_volume;
  int regional_weather;
  double regional_rain, regional_cloud, regional_storm;
  int overlay_suppressed; // Internal: the shader owns clouds/rain, grading still uses weather.
  int black_background;
  int wet_surfaces, splashes;
  double wet_seconds, dry_seconds;
  uint32_t cloud_seed;
  int mist, haze, gusts, golden_hour, distant_thunder;
  int air_valid, season;
  double celsius, humidity, approaching_storm, approach_age;
  uint32_t approach_id;
  AtmosphereTuning atmosphere;
  double day_minutes, night_minutes, moon_strength;
  int weather_wildlife, linear_light;
  int shadow_cache;
  double lighting_hz;
  double edge_aa;
  double ambient_fill, surface_light, contact_shadow, shadow_softness, night_lights, light_radius;
  int relief_shadows,weather_shadows;
  double enclosure_shade,material_wetness,valley_mist;
} EnvironmentConfig;

typedef struct {
  double hour, cloud, rain;
  Uint8 red, green, blue;
  double sun_x, sun_y, sun_z, sun_strength; // normalized world direction, Y up
  double daylight; // sky fill, independent of whether the key light is sun or moon
} EnvironmentSample;

typedef struct { SDL_Texture *cloud; } EnvironmentRenderer;

// Pure sampling makes captures reproducible and independent of display FPS.
EnvironmentConfig environment_defaults(void);
// Solar/lunar directions change slowly over the30minute cycle. Reuse their
// evaluation between bounded updates; rain, camera and animation stay realtime.
double environment_light_seconds(const EnvironmentConfig *config,double seconds);
// Active presentation seconds, independent of game ticks/speed. Default20min day/10min night.
double environment_cycle_hour(const EnvironmentConfig *config, double seconds);
EnvironmentSample environment_sample(const EnvironmentConfig *config, double seconds);
// Camera axes describe scene movement per camera tile. Returns no shadow
// below the horizon or for a degenerate camera, and bounds low-sun length.
int environment_shadow_slope(const EnvironmentSample *sun, double ax, double ay,
                             double bx, double by, double *dx, double *dy);
void environment_draw(EnvironmentRenderer *state, SDL_Renderer *renderer,
                      SDL_Rect course, const EnvironmentConfig *config, double seconds,
                      double camera_x, double camera_y, double scale);
double environment_shadow_transmission(const EnvironmentConfig *config,const EnvironmentSample *sun);
double environment_shadow_softness(const EnvironmentConfig *config,const EnvironmentSample *sun);
void environment_destroy(EnvironmentRenderer *state);
#endif
