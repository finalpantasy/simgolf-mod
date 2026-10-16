// Updates: the news list (data/updates.json), the version compare and the release feed reader behind the
// start menu's Updates panel and "is out" notice (simgolf mod, task-80). Plain C, no SDL, no game code.
#pragma once

#include <stddef.h>

typedef struct UpdRelease {
  char version[16], date[16], name[96];
  int n;
  char **note;   // plain bullet points
} UpdRelease;

// Everything the start menu shows about updates. Owned by the presenter (frontend_frosted.c); the
// front end screen reads and changes the UI part. The check thread fills the notice part.
typedef struct UpdState {
  int n_rel;
  UpdRelease *rel;           // newest first
  int open;                  // the panel is up
  int expanded;              // release row open, -1 none
  volatile int close_req;    // Esc (game thread)
  int show_dot;              // an update was installed and the player has not opened the panel yet
  // the notice
  volatile int avail;        // a newer release exists and the player has not pressed Later for it
  char latest[16];
  char zip_url[512], sha_url[512];
  volatile int busy;         // 0 idle, 1 downloading/installing, 2 failed (status_text says why)
  char status_text[120];
  volatile int download_per_mille; // -1 when the server has no content length
  volatile int download_kb, download_total_kb;
} UpdState;

// 1 when version b is newer than a, 0 when equal, -1 when older ("v" prefix and missing parts allowed)
int upd_version_cmp(const char *a, const char *b);

// Reads data/updates.json (see tools/release/changelog_to_json.mjs). 0 on failure (err filled).
int upd_load_news(UpdState *s, const char *path, char *err, size_t errlen);
int upd_parse_news(UpdState *s, const char *json, char *err, size_t errlen);
void upd_free(UpdState *s);

// GitHub's /releases/latest answer: the version (no "v"), the windows zip and its .sha256 asset.
// 0 when the text is not a release with both assets.
int upd_parse_feed(const char *json, char *version, size_t vlen, char *zip_url, size_t zlen, char *sha_url, size_t slen);
