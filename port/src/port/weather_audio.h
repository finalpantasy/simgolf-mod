#ifndef SIMGOLF_WEATHER_AUDIO_H
#define SIMGOLF_WEATHER_AUDIO_H

#include "port/weather_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Publish on the game thread. volume is the independent environment.weather_volume value.
// A single publisher is expected. The audio callback reads a bounded atomic snapshot.
void weather_audio_publish(const WeatherState *state, double volume, int active);

// Load bounded 44.1kHz stereo PCM16 loops before starting the audio callback.
// Missing/invalid files retain procedural rain. Returns loaded layer count.
int weather_audio_load_rain(const char *directory);
// Optional one-shots in the same pack. Counts stay immutable while mixing.
int weather_audio_recorded_thunder_count(void);
// Bounded atomic cue receipt; -1 means no recorded cue has started.
int weather_audio_last_thunder_variant(uint32_t *event_id);

// Add weather ambience to an interleaved float stereo buffer at 44.1 kHz.
// master and effects are the mixer's current 0..1 controls and are each applied once.
// Call from the audio callback only. The function does not allocate or lock.
void weather_audio_mix(float *stereo, int frames, float master, float effects);

// Reset all publisher and DSP state only while the audio callback is stopped.
void weather_audio_reset(void);
void weather_audio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
