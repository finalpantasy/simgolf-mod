// Mix synthetic PCM directly; never open an audio device or play sound.
#define SDL_MAIN_HANDLED
#include "../src/platform/sound_native.cpp"
extern "C" int host_muted(void) { return 1; }
extern "C" int weather_audio_load_rain(const char *) { return 0; }
extern "C" int weather_audio_recorded_thunder_count(void) { return 0; }
extern "C" void weather_audio_mix(float *, int, float, float) {}
extern "C" void weather_audio_shutdown(void) {}

static int failures;
static void check_category(int index, int single, bool music, const char *fixture) {
  snd_js_register(index, fixture, single);
  Sound &s = g_sounds[index];
  if (fixture) {
    if (!load(s) || !s.single) { ++failures; return; }
  } else {
    s.data.assign(8, 0.5f); s.frames = 8;
  }
  for (int muted = 0; muted < 2; ++muted) {
    snd_native_set_volumes(1, muted ? 0.0f : 1.0f, muted ? 1.0f : 0.0f);
    g_voices.clear();
    g_voices.push_back(Voice{index, 0, 1, 1, 1, 0});
    float out[2];
    mix(nullptr, (Uint8 *)out, sizeof out);
    bool audible = out[0] != 0;
    if (audible != (music != (muted != 0))) {
      std::fprintf(stderr, "category mismatch index=%d single=%d streamed=%d musicMuted=%d\n", index, single, fixture != nullptr, muted);
      ++failures;
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 2) return 3;
  FILE *file = std::fopen(argv[1], "wb");
  if (!file) return 3;
  const int frames = 40000; // 160 KB, greater than the single-voice threshold
  wav_header(file, frames);
  int16_t sample = 16384;
  for (int i = 0; i < frames * 2; ++i) std::fwrite(&sample, sizeof sample, 1, file);
  std::fclose(file);
  check_category(129, 0, true, nullptr);  // credits is lazy, not dedicated
  check_category(45, 0, false, argv[1]);  // streamed ambience is still an effect
  check_category(0, 0, false, argv[1]);   // large golf effect
  check_category(1, 1, false, nullptr);   // exclusivity alone is not a category
  check_category(111, 1, true, nullptr);
  check_category(44, 1, true, nullptr);   // preserve musical purchase jingle
  std::remove(argv[1]);
  std::printf("Audio categories: 12 silent mixer checks, %d failures\n", failures);
  return failures ? 1 : 0;
}
