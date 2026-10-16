#ifndef SIMGOLF_WEATHER_STATE_H
#define SIMGOLF_WEATHER_STATE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Shared deterministic presentation/gameplay sample. Weather: clear/cloudy/rain/storm=0..3.
typedef struct WeatherState {
  double seconds, hour;
  double rain, storm, wind, night, cloud;
  double lightning, strike_age, thunder_age;
  double wind_x, wind_z;
  uint32_t event_id;
  int enabled;
  int black_background; // Keep all weather off the space beyond the course.
  double wetness, puddles, evaporation;
  double impact_strength, flash_gain;
  double cloud_size, cloud_shade, cloud_feather;
  uint32_t cloud_seed;
  double cloud_morph_seconds;
  double mist, haze, golden, wind_drift_x, wind_drift_z, distant_thunder;
  int wind_advection;
  uint32_t distant_event_id;
  double distant_age;
  double edge_aa; // Optional native course edge resolve, no UI or temporal history.
  double material_wetness,valley_mist;
  double light[3],eye[3],light_strength;
  double material_gloss[4],material_power[4],material_absorb[4];
  int material_tuning_valid;
  int wildlife_legacy; // Internal A/B: preserve old night-only insects when requested.
} WeatherState;
// Shared weather shelter for visual and audible night insects. The exposure
// curve is continuous over normalized rain, storm and wind. No random state,
// temperature simulation or independent clock. Invalid conditions shelter them.
static inline double weather_night_activity(const WeatherState *s) {
  if(!s||!s->enabled)return 0;
  double n=s->night>=0?(s->night<=1?s->night:1):0;
  if(s->wildlife_legacy)return n;
  double rain=s->rain>=0?(s->rain<=1?s->rain:1):1;
  double storm=s->storm>=0?(s->storm<=1?s->storm:1):1;
  double wind=s->wind>=0?(s->wind<=1?s->wind:1):1;
  double exposure=(1-rain)*(1-storm)*(1-wind);
  return n*exposure*exposure;
}
// Presentation condition: clear, partly cloudy, cloudy, light rain, rain, storm.
int weather_condition(const WeatherState *weather);
typedef struct WeatherSurfaceState { double wetness, puddles, evaporation; } WeatherSurfaceState;
typedef struct WeatherSurfaceTuning {
  double wet_seconds, dry_seconds, puddle_seconds;
  double flash_gain, splash_strength, evaporation_strength;
  double cloud_size, cloud_shade, cloud_feather;
  double cloud_morph_seconds;
  double puddle_drain;
  double material_gloss[4],material_power[4],material_absorb[4];
} WeatherSurfaceTuning;
WeatherSurfaceTuning weather_surface_defaults(void);
int weather_surface_load(WeatherSurfaceTuning *tuning, const char *path);
void weather_surface_update(WeatherSurfaceState *surface, const WeatherSurfaceTuning *tuning,
                            double rain, double cloud, double night, double wind, double dt);
WeatherState weather_state_sample(int enabled, int weather, double intensity,
                                 double seconds, double hour, uint32_t seed);
#ifdef __cplusplus
}
#endif
#endif
