#ifndef SIMGOLF_WEATHER_CLIMATE_H
#define SIMGOLF_WEATHER_CLIMATE_H
#include <stdint.h>

enum { WEATHER_CLIMATE_ROWS = 64 };
typedef struct {
  char place[32];
  int season, weight[4];
  double rain_min, rain_max, year_swing;
} WeatherClimateRow;
typedef struct { WeatherClimateRow rows[WEATHER_CLIMATE_ROWS]; int count; } WeatherClimateTable;
typedef struct {
  int initialized, kind, season;
  uint32_t front, identity;
  double last, next, rain, cloud, storm, target_rain, target_cloud, target_storm, year_rain_scale;
  double front_started, lead_seconds;
} WeatherClimate;

// Game-thread only. Table errors leave the caller's existing table untouched.
int weather_climate_load(WeatherClimateTable *table, const char *path);
// tick is the original eight-month March..October calendar, 1024 ticks/month.
// No game RNG, original fields or save bytes are changed.
void weather_climate_update(WeatherClimate *state, const WeatherClimateTable *table,
                           const char *place, int theme, uint32_t tick,
                           uint32_t seed, double seconds, double minutes);

// Lightweight authored display climate. Does not affect gameplay or save data.
enum { WEATHER_AIR_ROWS=32 };
typedef struct { char place[32]; double c[3], rh[3], day_swing, spell_swing; } WeatherAirRow;
typedef struct { WeatherAirRow rows[WEATHER_AIR_ROWS]; int count; } WeatherAirTable;
typedef struct { int initialized; uint32_t identity; double last, c, rh; } WeatherAir;
int weather_air_load(WeatherAirTable *table,const char *path);
void weather_air_update(WeatherAir *air,const WeatherAirTable *table,const char *place,
                        int theme,uint32_t tick,uint32_t seed,uint32_t front,
                        double seconds,double hour,double rain,double cloud);
#endif
