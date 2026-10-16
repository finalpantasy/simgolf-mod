#include "port/weather_audio.h"

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEATHER_AUDIO_RATE 44100.0f
#define WEATHER_AUDIO_PI 3.14159265358979323846f

typedef struct WeatherAudioInput {
  float seconds;
  float rain;
  float storm;
  float wind;
  float night;
  float thunder_age;
  float wind_x;
  float volume;
  float distant;
  uint32_t event_id;
  uint32_t enabled;
  uint32_t active;
} WeatherAudioInput;

// Each published field is atomic so the bounded sequence retry is data-race free
// even if a publisher updates while the callback is taking its snapshot.
static atomic_uint g_sequence;
static atomic_uint g_seconds;
static atomic_uint g_rain;
static atomic_uint g_storm;
static atomic_uint g_wind;
static atomic_uint g_night;
static atomic_uint g_thunder_age;
static atomic_uint g_wind_x;
static atomic_uint g_volume;
static atomic_uint g_distant;
static atomic_uint g_event_id;
static atomic_uint g_enabled;
static atomic_uint g_active;
static atomic_uint g_cue_sequence,g_cue_event,g_cue_variant;
static uint32_t g_reported_cue;

int weather_audio_last_thunder_variant(uint32_t *event_id) {
  for(int attempt=0;attempt<3;attempt++) {
    unsigned before=atomic_load_explicit(&g_cue_sequence,memory_order_acquire);
    if(before&1u)continue;
    unsigned event=atomic_load_explicit(&g_cue_event,memory_order_relaxed);
    unsigned variant=atomic_load_explicit(&g_cue_variant,memory_order_relaxed);
    unsigned after=atomic_load_explicit(&g_cue_sequence,memory_order_acquire);
    if(before==after){if(event_id)*event_id=event;return (int)variant-1;}
  }
  if(event_id)*event_id=0;return -1;
}

typedef struct ThunderVoice {
  uint32_t rng;
  float age;
  float low;
  float body;
  float pan;
  float modulation_hz;
  float modulation_phase;
  int recorded_index;
  double position;
  double step;
  float gain;
  float distant, far_l, far_r;
  int active;
} ThunderVoice;

typedef struct WeatherAudioDsp {
  WeatherAudioInput input;
  float volume;
  float rain;
  float wind;
  float night;
  float blend;
  float rain_low_l, rain_low_r;
  float rain_mid_l, rain_mid_r;
  float recorded_low_l, recorded_low_r;
  float wind_low_l, wind_low_r;
  float wind_mid_l, wind_mid_r;
  float drop_env, drop_phase, drop_step, drop_pan;
  float thunder_focus;
  uint32_t rng;
  uint64_t sample;
  uint32_t last_event_id;
  float last_seconds;
  int have_event_id;
  int last_active;
  int thunder_pending;
  int next_thunder;
  int last_thunder_clip;
  unsigned thunder_clip_mask;
  ThunderVoice thunder[2];
} WeatherAudioDsp;

static WeatherAudioDsp g_dsp;
static int16_t *g_rain_pcm[3];
static size_t g_rain_frames[3];
static int16_t *g_thunder_pcm[4];
static size_t g_thunder_frames[4];

int weather_audio_recorded_thunder_count(void) {
  int count=0;
  for(int i=0;i<4;i++)if(g_thunder_pcm[i]&&g_thunder_frames[i])count++;
  return count;
}

static unsigned read_le(const unsigned char *p, int bytes) {
  unsigned value = 0;
  for (int i=0; i<bytes; i++) value |= (unsigned)p[i] << (i*8);
  return value;
}

int weather_audio_load_rain(const char *directory) {
  const char *names[7] = {"rain-light", "rain-steady", "rain-heavy",
                        "thunder-0", "thunder-1", "thunder-2", "thunder-3"};
  int loaded = 0;
  g_dsp.last_thunder_clip=-1;
  for (int layer=0; layer<7; layer++) {
    int16_t **slot=layer<3?&g_rain_pcm[layer]:&g_thunder_pcm[layer-3];
    size_t *frames=layer<3?&g_rain_frames[layer]:&g_thunder_frames[layer-3];
    free(*slot);*slot=NULL;*frames=0;
    if (!directory) continue;
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s.wav", directory, names[layer]);
    if (length < 0 || length >= (int)sizeof path) continue;
    FILE *file = fopen(path, "rb");
    if (!file) continue;
    unsigned char header[16];
    int valid = fread(header,1,12,file)==12 && !memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVE",4);
    int format = 0;
    // Bound both chunk count and individual allocation. Never parse in callback.
    for (int chunk=0; valid && chunk<64 && fread(header,1,8,file)==8; chunk++) {
      unsigned bytes = read_le(header+4,4);
      if (bytes > 44100u*4u*90u) break;
      if (!memcmp(header,"fmt ",4) && bytes>=16) {
        if (fread(header,1,16,file)!=16) break;
        format = read_le(header,2)==1 && read_le(header+2,2)==2 && read_le(header+4,4)==44100 &&
                 read_le(header+12,2)==4 && read_le(header+14,2)==16;
        if (fseek(file,(long)(bytes-16+(bytes&1)),SEEK_CUR)) break;
      } else if (!memcmp(header,"data",4) && format && bytes>=44100*4 && !(bytes&3)) {
        int16_t *pcm = malloc(bytes);
        if (pcm && fread(pcm,1,bytes,file)==bytes) {
          *slot=pcm;*frames=bytes/4;if(layer<3)loaded++;
        } else free(pcm);
        break;
      } else if (fseek(file,(long)(bytes+(bytes&1)),SEEK_CUR)) break;
    }
    fclose(file);
  }
  return loaded;
}

static uint32_t float_bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

static float bits_float(uint32_t bits) {
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static float finite_or(float value, float fallback) {
  return isfinite(value) ? value : fallback;
}

static float clamp01(float value) {
  value = finite_or(value, 0.0f);
  return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

static float clamp11(float value) {
  value = finite_or(value, 0.0f);
  return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

static float from_double(double value) {
  if (!isfinite(value)) return 0.0f;
  if (value > 100000000.0) value = 100000000.0;
  if (value < -100000000.0) value = -100000000.0;
  return (float)value;
}

static void store_float(atomic_uint *slot, float value) {
  atomic_store_explicit(slot, float_bits(value), memory_order_relaxed);
}

void weather_audio_publish(const WeatherState *state, double volume, int active) {
  WeatherAudioInput next;
  memset(&next, 0, sizeof next);
  if (state) {
    next.seconds = from_double(state->seconds);
    next.rain = clamp01(from_double(state->rain));
    next.storm = clamp01(from_double(state->storm));
    next.wind = clamp01(from_double(state->wind));
    next.night = clamp01(from_double(weather_night_activity(state)));
    next.thunder_age = from_double(state->thunder_age);
    next.wind_x = clamp11(from_double(state->wind_x));
    next.event_id = state->event_id;
    next.enabled = state->enabled != 0;
    next.active = active != 0;
    next.volume = clamp01(from_double(volume));
    next.distant=clamp01(from_double(state->distant_thunder));
    if(next.distant>0){next.event_id=state->distant_event_id;next.thunder_age=from_double(state->distant_age);}
  }

  atomic_fetch_add_explicit(&g_sequence, 1u, memory_order_acq_rel);
  store_float(&g_seconds, next.seconds);
  store_float(&g_rain, next.rain);
  store_float(&g_storm, next.storm);
  store_float(&g_wind, next.wind);
  store_float(&g_night, next.night);
  store_float(&g_thunder_age, next.thunder_age);
  store_float(&g_wind_x, next.wind_x);
  store_float(&g_volume, next.volume);
  store_float(&g_distant,next.distant);
  atomic_store_explicit(&g_event_id, next.event_id, memory_order_relaxed);
  atomic_store_explicit(&g_enabled, next.enabled, memory_order_relaxed);
  atomic_store_explicit(&g_active, next.active, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_sequence, 1u, memory_order_release);
  if(getenv("SIMGOLF_ATMOSPHERE_LOG")){
    static uint32_t logged_remote;
    if(next.active&&next.distant>0&&next.event_id!=logged_remote){
      fprintf(stderr,"weather: distant approach event %u seconds %.3f age %.3f gain %.3f rain %.3f\n",next.event_id,next.seconds,next.thunder_age,next.distant,next.rain);logged_remote=next.event_id;
    }
  }
  uint32_t played;int variant=weather_audio_last_thunder_variant(&played);
  if(played&&played!=g_reported_cue&&variant>=0) {
    fprintf(stderr,"weather: recorded thunder event %u variant %d\n",played,variant);
    g_reported_cue=played;
  }
}

static int read_input(WeatherAudioInput *out) {
  for (int attempt = 0; attempt < 3; attempt++) {
    unsigned before = atomic_load_explicit(&g_sequence, memory_order_acquire);
    if (before & 1u) continue;
    WeatherAudioInput next;
    next.seconds = bits_float(atomic_load_explicit(&g_seconds, memory_order_relaxed));
    next.rain = bits_float(atomic_load_explicit(&g_rain, memory_order_relaxed));
    next.storm = bits_float(atomic_load_explicit(&g_storm, memory_order_relaxed));
    next.wind = bits_float(atomic_load_explicit(&g_wind, memory_order_relaxed));
    next.night = bits_float(atomic_load_explicit(&g_night, memory_order_relaxed));
    next.thunder_age = bits_float(atomic_load_explicit(&g_thunder_age, memory_order_relaxed));
    next.wind_x = bits_float(atomic_load_explicit(&g_wind_x, memory_order_relaxed));
    next.volume = bits_float(atomic_load_explicit(&g_volume, memory_order_relaxed));
    next.distant = bits_float(atomic_load_explicit(&g_distant, memory_order_relaxed));
    next.event_id = atomic_load_explicit(&g_event_id, memory_order_relaxed);
    next.enabled = atomic_load_explicit(&g_enabled, memory_order_relaxed);
    next.active = atomic_load_explicit(&g_active, memory_order_relaxed);
    unsigned after = atomic_load_explicit(&g_sequence, memory_order_acquire);
    if (before == after && !(after & 1u)) {
      *out = next;
      return 1;
    }
  }
  return 0;
}

static uint32_t random_u32(uint32_t *state) {
  uint32_t x = *state;
  if (!x) x = 0x6d2b79f5u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static float random_signed(uint32_t *state) {
  return (float)(random_u32(state) >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

static float random_unit(uint32_t *state) {
  return (float)(random_u32(state) >> 8) * (1.0f / 16777216.0f);
}

static float approach(float value, float target, float amount) {
  return value + (target - value) * amount;
}

static uint32_t event_seed(uint32_t event_id) {
  uint32_t x = event_id ^ 0x9e3779b9u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x ? x : 0x6d2b79f5u;
}

static void start_thunder(WeatherAudioDsp *dsp, float age, uint32_t event_id,float distant) {
  ThunderVoice *voice = &dsp->thunder[dsp->next_thunder++ & 1];
  memset(voice, 0, sizeof *voice);
  voice->active = 1;
  voice->distant=distant;
  // Game-thread publications can arrive after the short attack has elapsed.
  // Play a fresh cue from its start within one bounded publication interval;
  // genuinely stale arrivals retain their tail age rather than replay a crack.
  voice->age = age < 0.25f ? 0.0f : age;
  voice->rng = event_seed(event_id);
  voice->pan = random_signed(&voice->rng) * 0.16f;
  voice->modulation_hz = 0.17f + random_unit(&voice->rng) * 0.07f;
  voice->modulation_phase = random_unit(&voice->rng) * (2.0f * WEATHER_AUDIO_PI);
  voice->recorded_index=-1;
  int available=weather_audio_recorded_thunder_count();
  if(available) {
    unsigned available_mask=0;
    for(int j=0;j<4;j++)if(g_thunder_frames[j])available_mask|=1u<<j;
    if((dsp->thunder_clip_mask&available_mask)==available_mask)dsp->thunder_clip_mask=0;
    int first=(int)(event_seed(event_id^0x35a79U)%4);
    for(int j=0;j<4;j++) {
      int index=(first+j)%4;
      if(!g_thunder_frames[index]||(available>1&&index==dsp->last_thunder_clip))continue;
      if(dsp->thunder_clip_mask&(1u<<index))continue;
      voice->recorded_index=index;dsp->last_thunder_clip=index;break;
    }
    dsp->thunder_clip_mask|=1u<<voice->recorded_index;
    voice->step=.96+random_unit(&voice->rng)*.08;
    voice->gain=.85f+random_unit(&voice->rng)*.28f;
    voice->position=voice->age*WEATHER_AUDIO_RATE*voice->step;
    atomic_fetch_add_explicit(&g_cue_sequence,1u,memory_order_acq_rel);
    atomic_store_explicit(&g_cue_variant,(unsigned)(voice->recorded_index+1),memory_order_relaxed);
    atomic_store_explicit(&g_cue_event,event_id,memory_order_relaxed);
    atomic_fetch_add_explicit(&g_cue_sequence,1u,memory_order_release);
  }
}

static void observe_event(WeatherAudioDsp *dsp, const WeatherAudioInput *input) {
  const int active = input->active && input->enabled;
  if (!dsp->have_event_id) {
    dsp->have_event_id = 1;
    dsp->last_event_id = input->event_id;
    dsp->last_seconds = input->seconds;
    dsp->last_active = active;
    return;
  }

  const int clock_advanced = input->seconds > dsp->last_seconds;
  if (input->event_id != dsp->last_event_id) {
    // The first state after entering a course or resuming a paused course is a
    // baseline. Only an id change while already active and game time advances
    // represents a new strike. The shared clock stops while paused.
    dsp->last_event_id = input->event_id;
    if (active && dsp->last_active && clock_advanced) dsp->thunder_pending = 1;
    else dsp->thunder_pending = 0;
  }
  dsp->last_seconds = input->seconds;
  dsp->last_active = active;

  if (dsp->thunder_pending && input->thunder_age >= 0.0f) {
    if (active && input->volume > 0.0f && input->thunder_age < 12.0f)
      start_thunder(dsp, input->thunder_age, input->event_id,input->distant);
    // Consume at arrival, even if the ambience is muted or the course has
    // entered pause. A later resume must not replay the same strike.
    dsp->thunder_pending = 0;
  }
}

static float insect_chirp(double time, float period, float frequency, float offset) {
  double local = time + (double)offset;
  double cycle = fmod(local, (double)period);
  if (cycle < 0.0) cycle += period;
  if (cycle >= 0.34) return 0.0f;
  double pulse = fmod(cycle, 0.112);
  if (pulse >= 0.043) return 0.0f;
  float x = (float)(pulse / 0.043);
  float envelope = x < 0.5f ? x * 2.0f : (1.0f - x) * 2.0f;
  float sweep = 1.0f + 0.075f * x;
  return sinf(2.0f * WEATHER_AUDIO_PI * frequency * sweep * (float)pulse) * envelope;
}

static void thunder_sample(ThunderVoice *voice, float *left, float *right) {
  if (!voice->active) return;
  if(voice->recorded_index>=0) {
    int index=voice->recorded_index;
    size_t a=(size_t)voice->position;
    if(a>=g_thunder_frames[index]){voice->active=0;return;}
    size_t b=a+1<g_thunder_frames[index]?a+1:a;
    float t=(float)(voice->position-a);
    const int16_t *pcm=g_thunder_pcm[index];
    float l=((1-t)*pcm[a*2]+t*pcm[b*2])*(voice->gain/32768.0f)*(1-voice->pan);
    float r=((1-t)*pcm[a*2+1]+t*pcm[b*2+1])*(voice->gain/32768.0f)*(1+voice->pan);
    if(voice->distant>0){
      voice->far_l+=.025f*(l-voice->far_l);voice->far_r+=.025f*(r-voice->far_r);
      float attack=clamp01(voice->age/.7f)*voice->distant;
      l=voice->far_l*attack;r=voice->far_r*attack;
    }
    *left+=l;*right+=r;
    voice->position+=voice->step;voice->age+=1.0f/WEATHER_AUDIO_RATE;
    return;
  }
  const float age = voice->age;
  if (age >= 9.0f) {
    voice->active = 0;
    return;
  }
  float noise = random_signed(&voice->rng);
  voice->body += 0.035f * (noise - voice->body);
  voice->low += 0.0075f * (voice->body - voice->low);
  float broad = voice->body - voice->low;
  float rumble = 0.8f * voice->low + 0.47f * broad;
  float decay = expf(-age * 0.62f);
  // Under natural rain, retain variation without allowing a seeded trough
  // to swallow the entire first rumble. The procedural fallback is unchanged.
  float depth = g_rain_pcm[0] && g_rain_pcm[1] && g_rain_pcm[2] ? 0.18f : 0.38f;
  float swell = 1-depth + depth * sinf(2.0f * WEATHER_AUDIO_PI * voice->modulation_hz * age + voice->modulation_phase);
  float crack = 0.0f;
  if (age < 0.055f) {
    float t = age / 0.055f;
    crack = random_signed(&voice->rng) * (1.0f - t) * (1.0f - t) * 0.12f;
  }
  float sample = (rumble * 0.82f * decay * swell + crack) * 0.52f;
  if(voice->distant>0)sample=voice->low*.42f*decay*voice->distant*clamp01(age/.7f);
  float pan = voice->pan;
  *left += sample * (1.0f - pan);
  *right += sample * (1.0f + pan);
  voice->age += 1.0f / WEATHER_AUDIO_RATE;
}

void weather_audio_mix(float *stereo, int frames, float master, float effects) {
  if (!stereo || frames <= 0) return;
  WeatherAudioInput latest;
  if (read_input(&latest)) g_dsp.input = latest;
  const WeatherAudioInput input = g_dsp.input;
  observe_event(&g_dsp, &input);

  const float target_blend = input.active && input.enabled ? 1.0f : 0.0f;
  const int recorded_thunder=weather_audio_recorded_thunder_count();
  master = clamp01(master);
  effects = clamp01(effects);
  float wind_target = input.wind > input.storm * 0.35f ? input.wind : input.storm * 0.35f;
  for (int i = 0; i < frames; i++, g_dsp.sample++) {
    g_dsp.blend = approach(g_dsp.blend, target_blend, 0.00006450f); // 350 ms crossfade
    g_dsp.volume = approach(g_dsp.volume, input.volume, 0.00006450f);
    g_dsp.rain = approach(g_dsp.rain, input.rain, 0.00012588f);
    g_dsp.wind = approach(g_dsp.wind, wind_target, 0.00004535f);
    g_dsp.night = approach(g_dsp.night, input.night, 0.00002267f);
    float master_effects = master * effects * g_dsp.volume * g_dsp.blend;
    double time = (double)g_dsp.sample / (double)WEATHER_AUDIO_RATE;
    float center_noise = random_signed(&g_dsp.rng);
    float side_noise = random_signed(&g_dsp.rng);
    float nl = 0.78f * center_noise + 0.35f * side_noise;
    float nr = 0.78f * center_noise - 0.35f * side_noise;

    // Dense rain is a soft, stereo-correlated noise bed with a few rounded
    // droplet tones. Its broad spectrum stays below dialogue and golf effects.
    g_dsp.rain_low_l += 0.035f * (nl - g_dsp.rain_low_l);
    g_dsp.rain_low_r += 0.035f * (nr - g_dsp.rain_low_r);
    g_dsp.rain_mid_l += 0.19f * (nl - g_dsp.rain_mid_l);
    g_dsp.rain_mid_r += 0.19f * (nr - g_dsp.rain_mid_r);
    float rain_l = 0.45f * nl + 0.42f * (nl - g_dsp.rain_mid_l) +
                   0.20f * (g_dsp.rain_mid_l - g_dsp.rain_low_l);
    float rain_r = 0.45f * nr + 0.42f * (nr - g_dsp.rain_mid_r) +
                   0.20f * (g_dsp.rain_mid_r - g_dsp.rain_low_r);
    if ((random_u32(&g_dsp.rng) & 0xffffu) < 5u && g_dsp.rain > 0.001f) {
      g_dsp.drop_env = 0.12f + random_unit(&g_dsp.rng) * 0.22f;
      g_dsp.drop_phase = 0.0f;
      g_dsp.drop_step = (2.0f * WEATHER_AUDIO_PI) * (2600.0f + random_unit(&g_dsp.rng) * 2200.0f) / WEATHER_AUDIO_RATE;
      g_dsp.drop_pan = random_signed(&g_dsp.rng) * 0.7f;
    }
    float drop = sinf(g_dsp.drop_phase) * g_dsp.drop_env;
    g_dsp.drop_phase += g_dsp.drop_step;
    g_dsp.drop_env *= 0.99915f;
    rain_l += drop * (1.0f - g_dsp.drop_pan) * 0.24f;
    rain_r += drop * (1.0f + g_dsp.drop_pan) * 0.24f;
    rain_l *= 0.074f * g_dsp.rain;
    rain_r *= 0.074f * g_dsp.rain;
    if (g_rain_pcm[0] && g_rain_pcm[1] && g_rain_pcm[2]) {
      float level = g_dsp.rain;
      int first = level <= 0.6f ? 0 : 1, second = first+1;
      float blend = first==0 ? (level-0.25f)/0.35f : (level-0.6f)/0.4f;
      blend = clamp01(blend);
      float gain = clamp01(level/0.25f);
      size_t a=(size_t)(g_dsp.sample%g_rain_frames[first])*2;
      size_t b=(size_t)(g_dsp.sample%g_rain_frames[second])*2;
      rain_l=(g_rain_pcm[first][a]*(1-blend)+g_rain_pcm[second][b]*blend)*(gain/32768.0f);
      rain_r=(g_rain_pcm[first][a+1]*(1-blend)+g_rain_pcm[second][b+1]*blend)*(gain/32768.0f);
      // The recordings carry strong low-mid rumble. A gentle low shelf leaves
      // rainfall detail above it and reserves the bass band for distant thunder.
      if(!recorded_thunder) {
        g_dsp.recorded_low_l += 0.065f*(rain_l-g_dsp.recorded_low_l);
        g_dsp.recorded_low_r += 0.065f*(rain_r-g_dsp.recorded_low_r);
        rain_l -= 0.80f*g_dsp.recorded_low_l;
        rain_r -= 0.80f*g_dsp.recorded_low_r;
      }
    }

    // Wind uses a low, slow filtered bed with a restrained directional image.
    g_dsp.wind_mid_l += 0.035f * (nl - g_dsp.wind_mid_l);
    g_dsp.wind_mid_r += 0.035f * (nr - g_dsp.wind_mid_r);
    g_dsp.wind_low_l += 0.010f * (g_dsp.wind_mid_l - g_dsp.wind_low_l);
    g_dsp.wind_low_r += 0.010f * (g_dsp.wind_mid_r - g_dsp.wind_low_r);
    float gust = 0.74f + 0.18f * sinf(2.0f * WEATHER_AUDIO_PI * 0.075f * (float)time + 0.4f) +
                 0.08f * sinf(2.0f * WEATHER_AUDIO_PI * 0.19f * (float)time + 1.3f);
    float wind_pan = input.wind_x * 0.22f;
    float wind_l = (1.55f * g_dsp.wind_low_l + 0.28f * (g_dsp.wind_mid_l - g_dsp.wind_low_l)) *
                   0.12f * g_dsp.wind * gust * (1.0f - wind_pan);
    float wind_r = (1.55f * g_dsp.wind_low_r + 0.28f * (g_dsp.wind_mid_r - g_dsp.wind_low_r)) *
                   0.12f * g_dsp.wind * gust * (1.0f + wind_pan);

    // Sparse cricket-like chirp groups establish nighttime without adding a
    // continuous high tone. Channel offsets create width without random jumps.
    float insect_period = 2.15f + (1.0f - g_dsp.night) * 1.05f;
    float insect_l = insect_chirp(time, insect_period, 3350.0f, 0.0f) * 0.034f * g_dsp.night;
    float insect_r = insect_chirp(time, insect_period, 3670.0f, 0.73f) * 0.034f * g_dsp.night;

    float thunder_l = 0.0f, thunder_r = 0.0f;
    thunder_sample(&g_dsp.thunder[0], &thunder_l, &thunder_r);
    thunder_sample(&g_dsp.thunder[1], &thunder_l, &thunder_r);
    if (g_rain_pcm[0] && g_rain_pcm[1] && g_rain_pcm[2]) {
      // Natural rain has more body than the fallback. Preserve the distant
      // thunder cue with a short, smooth bed dip and modest transient gain.
      float focus = 0;
      for (int voice=0;voice<2;voice++) {
        ThunderVoice *v=&g_dsp.thunder[voice];
        float tail=v->active ? clamp01(1-v->age/3.5f) : 0;
        if(tail>focus)focus=tail;
      }
      g_dsp.thunder_focus=approach(g_dsp.thunder_focus,focus,
                                 focus>g_dsp.thunder_focus ? 0.00055f : 0.000015f);
      rain_l*=1-0.35f*g_dsp.thunder_focus;rain_r*=1-0.35f*g_dsp.thunder_focus;
      if(!recorded_thunder){thunder_l*=8.0f;thunder_r*=8.0f;}
    }

    float weather_l = rain_l + wind_l + insect_l + thunder_l;
    float weather_r = rain_r + wind_r + insect_r + thunder_r;
    // Bound the ambience submix before adding it to golf voices.
    if (weather_l > 0.30f) weather_l = 0.30f;
    if (weather_l < -0.30f) weather_l = -0.30f;
    if (weather_r > 0.30f) weather_r = 0.30f;
    if (weather_r < -0.30f) weather_r = -0.30f;
    weather_l *= master_effects;
    weather_r *= master_effects;

    float base_l = finite_or(stereo[i * 2], 0.0f);
    float base_r = finite_or(stereo[i * 2 + 1], 0.0f);
    float min_l = -1.0f - base_l, max_l = 1.0f - base_l;
    float min_r = -1.0f - base_r, max_r = 1.0f - base_r;
    if (weather_l < min_l) weather_l = min_l;
    if (weather_l > max_l) weather_l = max_l;
    if (weather_r < min_r) weather_r = min_r;
    if (weather_r > max_r) weather_r = max_r;
    stereo[i * 2] = base_l + weather_l;
    stereo[i * 2 + 1] = base_r + weather_r;
  }
}

void weather_audio_reset(void) {
  atomic_store_explicit(&g_sequence, 1u, memory_order_release);
  store_float(&g_seconds, 0.0f);
  store_float(&g_rain, 0.0f);
  store_float(&g_storm, 0.0f);
  store_float(&g_wind, 0.0f);
  store_float(&g_night, 0.0f);
  store_float(&g_thunder_age, -1.0f);
  store_float(&g_wind_x, 0.0f);
  store_float(&g_volume, 0.0f);
  store_float(&g_distant,0.0f);
  atomic_store_explicit(&g_event_id, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_enabled, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_active, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_sequence, 2u, memory_order_release);
  memset(&g_dsp, 0, sizeof g_dsp);
  g_dsp.input.thunder_age = -1.0f;
  g_dsp.rng = 0x1badb002u;
  g_dsp.last_thunder_clip=-1;
  atomic_store(&g_cue_sequence,0);atomic_store(&g_cue_event,0);atomic_store(&g_cue_variant,0);
  g_reported_cue=0;
}

void weather_audio_shutdown(void) {
  weather_audio_reset();
  for (int i=0;i<3;i++) {free(g_rain_pcm[i]);g_rain_pcm[i]=NULL;g_rain_frames[i]=0;}
  for (int i=0;i<4;i++) {free(g_thunder_pcm[i]);g_thunder_pcm[i]=NULL;g_thunder_frames[i]=0;}
}
