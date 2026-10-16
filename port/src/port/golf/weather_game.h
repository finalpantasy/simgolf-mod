#ifndef SIMGOLF_GOLF_WEATHER_GAME_H
#define SIMGOLF_GOLF_WEATHER_GAME_H

#include "port/weather_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Values are supplied from the environment settings on the game thread.
// Each roll setting is a number of friction levels; mood settings are points.
typedef struct WeatherGameConfig {
  int gameplay_enabled;
  int rain_roll_steps;
  int storm_roll_bonus;
  int rain_mood_penalty;
  int storm_mood_bonus;
} WeatherGameConfig;

// Publish once per active course frame, outside the renderer lock. The game
// update reads this cached sample for per-golfer actions on the same thread.
void weather_game_publish(const WeatherState *state, const WeatherGameConfig *config);

// Adjust the ground-roll friction exponent for a live golfer. Smaller values
// in the original physics produce more drag. Invalid and non-ground contexts
// keep the original value.
int weather_game_roll_friction(int original, int golfer_slot, int terrain_id);

// Apply a bounded weather comfort effect once when a new golfer joins.
int weather_game_arrival_mood(int original, int golfer_slot);

#ifdef __cplusplus
}
#endif
#endif
