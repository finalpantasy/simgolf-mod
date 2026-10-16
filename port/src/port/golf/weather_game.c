#include "port/golf/weather_game.h"

typedef struct WeatherGameSample {
  int enabled;
  double rain;
  double storm;
  WeatherGameConfig config;
} WeatherGameSample;

static WeatherGameSample g_weather_game;

static int clamp_setting(int value) {
  if (value < 0) return 0;
  return value > 2 ? 2 : value;
}

static double unit(double value) {
  if (!(value >= 0.0)) return 0.0;
  return value > 1.0 ? 1.0 : value;
}

static int intensity_level(double value) {
  if (value >= 0.75) return 2;
  if (value >= 0.25) return 1;
  return 0;
}

static int live_golfer(int slot) { return slot >= 0 && slot < 152; }

static int rollable_terrain(int terrain) {
  return (terrain >= 0 && terrain <= 12) || terrain == 18 || terrain == 19;
}

void weather_game_publish(const WeatherState *state, const WeatherGameConfig *config) {
  g_weather_game.enabled = 0;
  g_weather_game.rain = 0.0;
  g_weather_game.storm = 0.0;
  g_weather_game.config = (WeatherGameConfig){0, 0, 0, 0, 0};
  if (!state || !config || !state->enabled || !config->gameplay_enabled) return;

  g_weather_game.enabled = 1;
  g_weather_game.rain = unit(state->rain);
  g_weather_game.storm = unit(state->storm);
  g_weather_game.config.gameplay_enabled = 1;
  g_weather_game.config.rain_roll_steps = clamp_setting(config->rain_roll_steps);
  g_weather_game.config.storm_roll_bonus = clamp_setting(config->storm_roll_bonus);
  g_weather_game.config.rain_mood_penalty = clamp_setting(config->rain_mood_penalty);
  g_weather_game.config.storm_mood_bonus = clamp_setting(config->storm_mood_bonus);
}

int weather_game_roll_friction(int original, int golfer_slot, int terrain_id) {
  if (!g_weather_game.enabled || !live_golfer(golfer_slot) ||
      !rollable_terrain(terrain_id) || original < 0 || original > 99) return original;

  int steps = intensity_level(g_weather_game.rain) * g_weather_game.config.rain_roll_steps +
              intensity_level(g_weather_game.storm) * g_weather_game.config.storm_roll_bonus;
  if (steps <= 0) return original;
  if (steps > 3) steps = 3;

  // The original >=5 path has a fixed drag formula. Cross its boundary, then
  // let stronger weather move down one additional exponent per step.
  int adjusted = original >= 5 ? 5 - steps : original - steps;
  return adjusted < 0 ? 0 : adjusted;
}

int weather_game_arrival_mood(int original, int golfer_slot) {
  if (!g_weather_game.enabled || !live_golfer(golfer_slot) || original < -10 || original > 10)
    return original;

  int penalty = intensity_level(g_weather_game.rain) * g_weather_game.config.rain_mood_penalty +
                intensity_level(g_weather_game.storm) * g_weather_game.config.storm_mood_bonus;
  if (penalty > 2) penalty = 2;
  int adjusted = original - penalty;
  return adjusted < -10 ? -10 : adjusted;
}
