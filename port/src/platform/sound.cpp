// sound.h over Web Audio. This side keeps golf's registration rules and maps
// the game's DOS paths to converted asset files; src/platform/sound_web.js
// does the loading and playback.
//
// The game names files like "sounds\Golf sfx\Iron.wav", relative to the
// install folder. The asset tree differs in case and has some spaces turned
// into underscores ("Sounds/Golf_Sfx/Iron.wav"), so paths are matched through
// a list of the real files (/assets/sound-files.txt, written by CMake) after
// folding case, separators and underscores.

#include "platform/sound.h"

#include <stdio.h>

#ifndef __EMSCRIPTEN__
#include "port/fs.h"
#include "mods/mods.h"
#endif

#include <string>
#include <unordered_map>

namespace {

const char *kManifest = "/assets/sound-files.txt";
const char *kUrlBase = "assets/";   // served next to simgolf.html

bool g_ready;
bool g_registered[SND_MAX_SOUNDS];
std::unordered_map<std::string, std::string> g_files;   // folded path -> real path

std::string fold(const char *path) {
  std::string out;
  for (const char *p = path; *p; p++) {
    char c = *p;
    if (c == '\\') c = '/';
    else if (c == '_') c = ' ';
    else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c == '/' && out.empty()) continue;
    out += c;
  }
  if (out.compare(0, 2, "./") == 0) out.erase(0, 2);
  return out;
}

void loadManifest() {
  FILE *f = fopen(kManifest, "r");
  if (!f) {
    printf("sound: %s missing, no sounds will play\n", kManifest);
    return;
  }
  char line[512];
  while (fgets(line, sizeof line, f)) {
    std::string path(line);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    if (!path.empty()) g_files.emplace(fold(path.c_str()), path);
  }
  fclose(f);
}


#ifndef __EMSCRIPTEN__
// task-114: the install names a few sounds a little differently from the way golf registers them
// ("sounds\effects\lake 1.wav" is LAKE1.wav, "Alligator.wav" is "Alligator .wav", "Marshal Male" is
// "Marshall Male", "sound\Celebs" is sounds\Celebs), so the original played nothing for them. When the exact
// path is missing, each part of it may match one entry of its folder that differs only in case, spaces or
// underscores, or by one letter, as long as that entry is the only such match. fixes.audio_paths=0 turns it off.
std::string squash(const std::string &n) {
  std::string o;
  for (char c : n) {
    if (c == ' ' || c == '_') continue;
    o += (c >= 'A' && c <= 'Z') ? (char)(c + 'a' - 'A') : c;
  }
  return o;
}

bool within_one(const std::string &a, const std::string &b) {
  if (a == b) return true;
  size_t la = a.size(), lb = b.size();
  if ((la > lb ? la - lb : lb - la) > 1) return false;
  size_t i = 0;
  while (i < la && i < lb && a[i] == b[i]) i++;
  if (la == lb) return false;   // a different letter is a different sound (Sad / Sid): only a missing or extra letter counts
  const std::string &lng = la > lb ? a : b, &sht = la > lb ? b : a;
  return lng.compare(i + 1, std::string::npos, sht, i, std::string::npos) == 0;
}

struct Match {
  std::string want, exact, near;
  int exactN = 0, nearN = 0;
};

void collect(void *ctx, const char *name, int, unsigned, uint64_t) {
  Match *m = (Match *)ctx;
  std::string n = squash(name);
  if (n == m->want) { m->exact = name; m->exactN++; }
  else if (within_one(n, m->want)) { m->near = name; m->nearN++; }
}

bool resolve_fuzzy(const char *dosPath, char *out, int outlen) {
  std::string path = dosPath, built;
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t e = path.find('\\', pos);
    if (e == std::string::npos) e = path.size();
    std::string part = path.substr(pos, e - pos);
    if (!part.empty()) {
      std::string tryPath = built.empty() ? part : built + "\\" + part;
      char tmp[512];
      if (fs_resolve(tryPath.c_str(), tmp, sizeof tmp, 0)) {
        built = tryPath;
      } else {
        Match m;
        m.want = squash(part);
        std::string pattern = built.empty() ? std::string("*") : built + "\\*";
        fs_list(pattern.c_str(), collect, &m);
        if (m.exactN == 1) built = built.empty() ? m.exact : built + "\\" + m.exact;
        else if (m.exactN == 0 && m.nearN == 1) built = built.empty() ? m.near : built + "\\" + m.near;
        else return false;
      }
    }
    pos = e + 1;
  }
  return fs_resolve(built.c_str(), out, outlen, 0) != 0;
}
#endif

}  // namespace

extern "C" {
void snd_js_init(void);
void snd_js_register(int index, const char *url, int single);
void snd_js_play(int index, int volume, int pan, int pitchCents, int delayMs);
void snd_js_fade_out(int index, int ms);
void snd_js_stop(int index);
void snd_js_shutdown(void);
}

void snd_init(void) {
  if (g_ready) return;
#ifdef __EMSCRIPTEN__
  loadManifest();
#endif
  snd_js_init();
  g_ready = true;
}

void snd_register(int index, const char *dosPath, unsigned flags) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS || !dosPath) return;
  // The DLL object comes from the first registration; later ones for the same
  // index never reach it, so the first file is the one that plays.
  if (g_registered[index]) return;
  g_registered[index] = true;

#ifndef __EMSCRIPTEN__
  // Native: the original WAVs straight from the game folder
  // (platform/sound_native.cpp implements the snd_js_* calls).
  char real[512];
  if (!fs_resolve(dosPath, real, sizeof real, 0) &&
      !(mods_get_int("fixes.audio_paths", 1) && resolve_fuzzy(dosPath, real, sizeof real))) {
    printf("sound: #%d %s not found in the game folder\n", index, dosPath);
    snd_js_register(index, nullptr, 0);
    return;
  }
  snd_js_register(index, real, (flags & SND_FLAG_DEDICATED) != 0);
  return;
#endif
  auto it = g_files.find(fold(dosPath));
  if (it == g_files.end()) {
    printf("sound: #%d %s not found in the assets\n", index, dosPath);
    snd_js_register(index, nullptr, 0);
    return;
  }
  // Each flag 0x10 sound had one dedicated DirectSound buffer, so (inferred)
  // one instance at a time. Big files get the same rule in JS once their size
  // is known, because sound.dll streamed them.
  std::string url = kUrlBase + it->second;
  snd_js_register(index, url.c_str(), (flags & SND_FLAG_DEDICATED) != 0);
}

void snd_play(int index, int volume, int pan, int pitchCents, int delayMs) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS) return;
  snd_js_play(index, volume, pan, pitchCents, delayMs);
}

void snd_fade_out(int index, int ms) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS) return;
  snd_js_fade_out(index, ms);
}

void snd_stop(int index) {
  if (!g_ready || index < 0 || index >= SND_MAX_SOUNDS) return;
  snd_js_stop(index);
}

void snd_shutdown(void) {
  if (!g_ready) return;
  snd_js_shutdown();
  g_files.clear();
  for (bool &r : g_registered) r = false;
  g_ready = false;
}
