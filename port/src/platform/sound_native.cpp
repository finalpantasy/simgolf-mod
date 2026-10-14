// Native Windows back end for src/platform/sound.cpp (simgolf mod, task-2):
// the same snd_js_* calls sound_web.js implements, over an SDL2 audio device.
// See sound.h for the behaviour (from sound.dll) and sound_web.js for the
// browser version this mirrors.
//
// - The device is 44.1 kHz stereo float, like sound.dll's mixer.
// - Each registered index keeps the original WAV's file path. The file loads
//   on its first play (8/16-bit PCM RIFF, the first `smpl` loop) and stays
//   decoded until shutdown.
// - A voice plays at rate * 2^(cents/1200) with linear interpolation, gain
//   (volume + 1) / 128, sound.dll's linear pan law, an optional start delay, a
//   loop between the smpl points, and a linear fade for snd_fade_out.
// - Up to 16 voices per index; one for dedicated (flag 0x10) and streamed
//   (data over 128 KB) sounds, a play while it is playing is ignored.

// intrin.h first: SDL 2.32's SDL_endian.h otherwise redefines clang 23's
// _m_prefetch builtin in C++.
#include <intrin.h>

#include <SDL.h>
#include <math.h>

#include "port/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace {

constexpr int kRate = 44100;
constexpr int kMaxVoices = 16;
constexpr unsigned kStreamBytes = 128 * 1024;
constexpr int kMaxSounds = 300;

struct Sound {
  std::string path;
  bool registered = false, single = false, loaded = false, failed = false;
  int rate = kRate, channels = 1;
  std::vector<float> data;          // interleaved
  size_t frames = 0;
  bool loop = false;
  size_t loopStart = 0, loopEnd = 0;
};

struct Voice {
  int index;
  double pos;                      // in source frames
  double step;                     // source frames per output frame
  float gainL, gainR;
  long delay;                      // output frames before it starts
  long fadeLeft = -1, fadeLen = 0; // fade out: frames left / total
};

// Options > Audio (card task-24): master, music (the dedicated-buffer sounds) and effects gains, 0..1.
volatile float g_vol_master = 1.0f, g_vol_music = 1.0f, g_vol_effects = 1.0f;

Sound g_sounds[kMaxSounds];
std::vector<Voice> g_voices;
SDL_AudioDeviceID g_dev;
bool g_logged_first;

// Proof that sound works without hearing it (muted test runs): running totals
// logged every 10 s of mixed audio, and SIMGOLF_MIXWAV=<file> records the
// first two minutes of the mix as a 16-bit WAV.
uint64_t g_mixed, g_loud, g_started, g_clipped, g_ignored;
bool g_trace;   // SIMGOLF_SOUND_LOG=1: one "snd:" line per play call (index, volume, started or why not)
float g_peak;
FILE *g_wav;
uint32_t g_wav_frames;
constexpr uint32_t kWavMaxFrames = 120u * kRate;

void wav_header(FILE *f, uint32_t frames) {
  uint32_t data = frames * 4, riff = 36 + data, fmt = 16, rate = kRate, bps = kRate * 4;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  fseek(f, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
  fwrite(&bps, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fseek(f, 0, SEEK_END);
}

void account(const float *out, int frames) {
  for (int i = 0; i < frames; i++) {
    float a = fabsf(out[i * 2]) > fabsf(out[i * 2 + 1]) ? fabsf(out[i * 2]) : fabsf(out[i * 2 + 1]);
    if (a > 1e-4f) g_loud++;
    if (a > g_peak) g_peak = a;
  }
  uint64_t before = g_mixed;
  g_mixed += (uint64_t)frames;
  if (g_mixed / (10u * kRate) != before / (10u * kRate))
    fprintf(stderr, "sound: mixed %.1f s, %.1f s of it not silent, peak %.3f, %llu voices started, %llu ignored, %llu clipped samples\n",
            g_mixed / (double)kRate, g_loud / (double)kRate, g_peak, (unsigned long long)g_started,
            (unsigned long long)g_ignored, (unsigned long long)g_clipped);
  if (g_wav && g_wav_frames < kWavMaxFrames) {
    int n = frames;
    if (g_wav_frames + (uint32_t)n > kWavMaxFrames) n = (int)(kWavMaxFrames - g_wav_frames);
    for (int i = 0; i < n * 2; i++) {
      int16_t v = (int16_t)lrintf(out[i] * 32767.0f);
      fwrite(&v, 2, 1, g_wav);
    }
    g_wav_frames += (uint32_t)n;
    wav_header(g_wav, g_wav_frames);
    fflush(g_wav);
  }
}

uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool load(Sound &s) {
  FILE *f = fopen(s.path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> b(n > 0 ? (size_t)n : 0);
  size_t got = fread(b.data(), 1, b.size(), f);
  fclose(f);
  if (got < 12 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
  int tag = 0, bits = 0;
  size_t dataOff = 0, dataLen = 0;
  bool haveFmt = false, haveLoop = false;
  uint32_t ls = 0, le = 0;
  for (size_t p = 12; p <= got - 8;) {
    uint64_t size = rd32(&b[p + 4]);   // 64 bits: a damaged size must not wrap the walk back to the start (task-114)
    size_t body = p + 8;
    if (!memcmp(&b[p], "fmt ", 4) && size >= 16 && size <= got - body) {
      tag = rd16(&b[body]);
      s.channels = rd16(&b[body + 2]);
      uint32_t hz = rd32(&b[body + 4]);
      if (!hz || hz > 384000) return false;
      s.rate = (int)hz;
      bits = rd16(&b[body + 14]);
      haveFmt = true;
    } else if (!memcmp(&b[p], "data", 4)) {
      dataOff = body;
      dataLen = size < got - body ? (size_t)size : got - body;
    } else if (!memcmp(&b[p], "smpl", 4) && size >= 60 && body + 60 <= got && rd32(&b[body + 28]) > 0) {
      // 36-byte header, then 24-byte loops: id, type, start, end (inclusive)
      ls = rd32(&b[body + 44]);
      le = rd32(&b[body + 48]) + 1;
      haveLoop = true;
    }
    if (size > got - body) break;   // the rest is cut off; a data chunk seen so far still plays
    p = body + (size_t)size + (size_t)(size & 1);
  }
  if (!haveFmt || !dataOff || tag != 1 || (bits != 8 && bits != 16) || s.channels < 1 || s.channels > 2) return false;
  int bytes = bits / 8;
  s.frames = dataLen / (size_t)(bytes * s.channels);
  s.data.resize(s.frames * s.channels);
  const uint8_t *src = &b[dataOff];
  for (size_t i = 0; i < s.frames * s.channels; i++)
    s.data[i] = bytes == 2 ? (int16_t)rd16(src + i * 2) / 32768.0f : (src[i] - 128) / 128.0f;
  if (dataLen > kStreamBytes) s.single = true;
  if (haveLoop && le > ls && ls < s.frames) {
    s.loop = true;
    s.loopStart = ls;
    s.loopEnd = le < s.frames ? le : s.frames;
  }
  return true;
}

// Audio thread: mixes every voice into the device buffer.
void SDLCALL mix(void *, Uint8 *stream, int len) {
  float *out = (float *)stream;
  int frames = len / (int)(2 * sizeof(float));
  memset(stream, 0, (size_t)len);
  const float master = g_vol_master, music = g_vol_music, effects = g_vol_effects;
  for (size_t v = 0; v < g_voices.size();) {
    Voice &vo = g_voices[v];
    Sound &s = g_sounds[vo.index];
    const float cat = master * (s.single ? music : effects);
    bool done = false;
    for (int i = 0; i < frames; i++) {
      if (vo.delay > 0) { vo.delay--; continue; }
      if (s.loop) {
        while (vo.pos >= (double)s.loopEnd) vo.pos -= (double)(s.loopEnd - s.loopStart);
      } else if (vo.pos >= (double)s.frames) {
        done = true;
        break;
      }
      size_t i0 = (size_t)vo.pos;
      size_t i1 = i0 + 1;
      if (s.loop && i1 >= s.loopEnd) i1 = s.loopStart;
      if (i1 >= s.frames) i1 = i0;
      float t = (float)(vo.pos - (double)i0);
      float l, r;
      if (s.channels == 1) {
        l = r = s.data[i0] + (s.data[i1] - s.data[i0]) * t;
      } else {
        l = s.data[i0 * 2] + (s.data[i1 * 2] - s.data[i0 * 2]) * t;
        r = s.data[i0 * 2 + 1] + (s.data[i1 * 2 + 1] - s.data[i0 * 2 + 1]) * t;
      }
      float g = 1.0f;
      if (vo.fadeLeft >= 0) {
        if (vo.fadeLeft == 0) { done = true; break; }
        g = (float)vo.fadeLeft / (float)vo.fadeLen;
        vo.fadeLeft--;
      }
      out[i * 2] += l * vo.gainL * g * cat;
      out[i * 2 + 1] += r * vo.gainR * g * cat;
      vo.pos += vo.step;
    }
    if (done) g_voices.erase(g_voices.begin() + (long)v);
    else v++;
  }
  for (int i = 0; i < frames * 2; i++) {
    if (out[i] > 1.0f || out[i] < -1.0f) g_clipped++;
    out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
  }
  account(out, frames);
}

int voices_of(int index) {
  int n = 0;
  for (const Voice &v : g_voices) n += v.index == index && v.fadeLeft != 0;
  return n;
}

}  // namespace

extern "C" {

// Opens the default output device (SDL picks the one the system uses now). Returns 0 when there is none.
static SDL_AudioDeviceID open_device(SDL_AudioSpec *have) {
  SDL_AudioSpec want = {};
  want.freq = kRate;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = mix;
  return SDL_OpenAudioDevice(nullptr, 0, &want, have, 0);
}

void snd_js_init(void) {
  if (g_dev) return;
  // Muted (tests, scripted runs, mute=1): SDL's dummy driver pulls the mix on
  // its own thread at real-time pace and throws it away; no sound card is
  // opened, so nothing can reach the speakers.
  const bool muted = host_muted() != 0;
  if (muted) SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "sound: SDL audio init failed: %s\n", SDL_GetError());
    return;
  }
  SDL_AudioSpec have = {};
  g_dev = open_device(&have);
  if (!g_dev) {
    fprintf(stderr, "sound: no audio device: %s\n", SDL_GetError());
    return;
  }
  const char *name = SDL_GetCurrentAudioDriver();
  if (muted && (!name || strcmp(name, "dummy"))) {
    fprintf(stderr, "sound: muted but SDL picked the %s driver; sound off\n", name ? name : "?");
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    return;
  }
  g_trace = getenv("SIMGOLF_SOUND_LOG") != nullptr;
  const char *wav = getenv("SIMGOLF_MIXWAV");
  if (wav && *wav && (g_wav = fopen(wav, "wb"))) {
    wav_header(g_wav, 0);
    fprintf(stderr, "sound: recording the mix to %s\n", wav);
  }
  fprintf(stderr, "sound: device open (%s driver, %d Hz, %d channels, %d-frame buffer)\n",
          name ? name : "?", have.freq, have.channels, have.samples);
  SDL_PauseAudioDevice(g_dev, 0);
}

// task-114c: the output device went away (headphones unplugged, a USB or Bluetooth device off, the default device
// changed) or a new one arrived. The original opened one device at start and stayed silent after it was removed.
// With [fixes] audio_recover=1 (default) the dead device is closed and the system's default one opened again; the
// registered sounds and their decoded samples stay, only the voices that were playing are dropped. A device that
// arrives while none is open is taken. removed: 1 removed (which = the device id), 0 added (which = a device index).
int snd_native_device_id(void) { return (int)g_dev; }
void snd_native_device_event(int removed, unsigned which, int recover) {
  if (removed ? (!g_dev || which != g_dev) : g_dev != 0) return;
  if (removed) {
    fprintf(stderr, "sound: the audio device was removed%s\n", recover ? "; opening the default one again" : " (audio_recover=0: sound stays off)");
    if (!recover) return;
    SDL_CloseAudioDevice(g_dev);   // the mixer thread is stopped: no lock needed below
    g_dev = 0;
    g_voices.clear();
  } else if (!recover) {
    return;
  }
  SDL_AudioSpec have = {};
  g_dev = open_device(&have);
  if (!g_dev) {
    fprintf(stderr, "sound: no audio device to switch to (%s); waiting for one\n", SDL_GetError());
    return;
  }
  const char *name = SDL_GetCurrentAudioDriver();
  fprintf(stderr, "sound: device open again (%s driver, %d Hz, %d channels, %d-frame buffer)\n", name ? name : "?",
          have.freq, have.channels, have.samples);
  SDL_PauseAudioDevice(g_dev, 0);
}

void snd_js_register(int index, const char *path, int single) {
  if (index < 0 || index >= kMaxSounds) return;
  SDL_LockAudioDevice(g_dev);
  Sound &s = g_sounds[index];
  s = Sound();
  s.registered = true;
  s.failed = !path;
  if (path) s.path = path;
  s.single = single != 0;
  SDL_UnlockAudioDevice(g_dev);
}

void snd_native_set_volumes(float master, float music, float effects) {
  g_vol_master = master < 0 ? 0 : master > 1 ? 1 : master;
  g_vol_music = music < 0 ? 0 : music > 1 ? 1 : music;
  g_vol_effects = effects < 0 ? 0 : effects > 1 ? 1 : effects;
}

void snd_js_play(int index, int volume, int pan, int cents, int delayMs) {
  if (!g_dev || index < 0 || index >= kMaxSounds) return;
  Sound &s = g_sounds[index];
  if (!s.registered || s.failed) return;
  if (!s.loaded) {
    // Decode outside the lock; the audio thread never touches unloaded sounds.
    Sound tmp = s;
    const Uint32 t0 = SDL_GetTicks();
    if (!load(tmp)) {
      fprintf(stderr, "sound: #%d %s: cannot decode\n", index, s.path.c_str());
      s.failed = true;
      return;
    }
    tmp.loaded = true;
    if (g_trace)
      fprintf(stderr, "snd: #%d decoded on the game thread in %u ms (%zu frames) %s\n", index, (unsigned)(SDL_GetTicks() - t0),
              tmp.frames, tmp.path.c_str());
    SDL_LockAudioDevice(g_dev);
    s = std::move(tmp);
    SDL_UnlockAudioDevice(g_dev);
  }
  if (volume < 0) volume = 0;
  if (volume > 127) volume = 127;
  if (pan < -64) pan = -64;
  if (pan > 63) pan = 63;
  float gain = (float)(volume + 1) / 128.0f;
  Voice v;
  v.index = index;
  v.pos = 0;
  v.step = (double)s.rate / kRate * pow(2.0, cents / 1200.0);
  v.gainL = gain * (pan > 0 ? fmaxf(0.0f, 1.0f - pan / 63.0f) : 1.0f);
  v.gainR = gain * (pan < 0 ? fmaxf(0.0f, 1.0f + pan / 63.0f) : 1.0f);
  v.delay = delayMs > 0 ? (long)delayMs * kRate / 1000 : 0;
  SDL_LockAudioDevice(g_dev);
  bool start = voices_of(index) < (s.single ? 1 : kMaxVoices);
  if (start) g_voices.push_back(v);
  SDL_UnlockAudioDevice(g_dev);
  if (start) g_started++;
  else g_ignored++;
  if (g_trace)
    fprintf(stderr, "snd: %u #%d vol %d pan %d cents %d delay %d %s %s\n", (unsigned)SDL_GetTicks(), index, volume, pan, cents,
            delayMs, start ? "started" : "ignored", s.path.c_str());
  if (start && !g_logged_first) {
    g_logged_first = true;
    fprintf(stderr, "sound: first sample playing: #%d %s (%d Hz, %d ch, %zu frames%s)\n", index,
            s.path.c_str(), s.rate, s.channels, s.frames, s.loop ? ", looped" : "");
  }
}

void snd_js_fade_out(int index, int ms) {
  if (!g_dev) return;
  long len = (long)(ms > 1 ? ms : 1) * kRate / 1000;
  SDL_LockAudioDevice(g_dev);
  for (Voice &v : g_voices)
    if (v.index == index && v.fadeLeft < 0) {
      v.fadeLeft = len;
      v.fadeLen = len;
    }
  SDL_UnlockAudioDevice(g_dev);
}

void snd_js_stop(int index) {
  if (!g_dev) return;
  SDL_LockAudioDevice(g_dev);
  for (Voice &v : g_voices)
    if (v.index == index) v.fadeLeft = 0;
  SDL_UnlockAudioDevice(g_dev);
}

void snd_js_shutdown(void) {
  if (!g_dev) return;
  SDL_CloseAudioDevice(g_dev);
  g_dev = 0;
  g_voices.clear();
  for (Sound &s : g_sounds) s = Sound();
  if (g_wav) {
    wav_header(g_wav, g_wav_frames);
    fclose(g_wav);
    g_wav = nullptr;
    g_wav_frames = 0;
  }
}

}  // extern "C"
