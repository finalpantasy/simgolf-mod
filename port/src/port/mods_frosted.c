// The Mods screen, presenter side (simgolf mod, card task-22), see mods_frosted.h.
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#include "port/mods_frosted.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mods/modmgr.h"
#include "mods/mods.h"
#include "port/hd_art.h"
#include "port/ui/ui_mods.h"

static volatile int g_open, g_close_req, g_open_req;

// keys: game thread -> render thread (single producer, single consumer)
#define KQ 32
static struct { SDL_Keycode k; int mods; } g_kq[KQ];
static volatile unsigned g_kq_head, g_kq_tail;

// render thread
static ModMgr *g_mgr;
static ModsUi g_ui;
static JsonVal *g_config;
static SDL_Renderer *g_r;
#define MAXMODS 256
static SDL_Texture *g_icon[MAXMODS];
static unsigned char g_icon_tried[MAXMODS];

static const char *setting_value(const char *key) {
  const char *value = mods_get(key);
  if (value && *value) return value;
  char fallback[160];
  if (!strncmp(key, "display.", 8)) snprintf(fallback, sizeof fallback, "golf.%s", key + 8);
  else if (!strcmp(key, "map.scale")) snprintf(fallback, sizeof fallback, "golf.map_scale");
  else return value;
  return mods_get(fallback);
}

static int setting_save(const char *key, const char *value) {
  if (!g_mgr || g_ui.selected < 0) return 0;
  const ModManifest *mf = modmgr_manifest(g_mgr, g_ui.selected);
  return mf && mods_config_set(mf->id, key, value);
}

void modsp_open(void) { g_open_req = 1; }
void modsp_close(void) { g_close_req = 1; }
int modsp_is_open(void) { return g_open || g_open_req; }

int modsp_key(const SDL_Event *e) {
  if (!modsp_is_open()) return 0;
  unsigned h = g_kq_head;
  if (h - g_kq_tail >= KQ) return 1;   // full: drop
  g_kq[h % KQ].k = e->key.keysym.sym;
  g_kq[h % KQ].mods = (int)e->key.keysym.mod;
  g_kq_head = h + 1;
  return 1;
}

// ------------------------------------------------------------------ icons

static SDL_Texture *icon_for(void *user, int mi) {
  (void)user;
  if (!g_mgr || mi < 0 || mi >= MAXMODS || mi >= modmgr_count(g_mgr) || !g_r) return NULL;
  if (g_icon[mi] || g_icon_tried[mi]) return g_icon[mi];
  g_icon_tried[mi] = 1;
  const ModManifest *mf = modmgr_manifest(g_mgr, mi);
  if (!mf->dir) return NULL;
  char path[800];
  if (mf->icon && *mf->icon && !strstr(mf->icon, "..")) snprintf(path, sizeof path, "%s/%s", mf->dir, mf->icon);
  else snprintf(path, sizeof path, "%s/preview.png", mf->dir);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fclose(f);
  int w = 0, h = 0;
  uint32_t *px = hd_art_load_png(path, &w, &h);
  if (!px) return NULL;
  if (w < 4 || h < 4 || w > 2048 || h > 2048) {
    free(px);
    return NULL;
  }
  SDL_Texture *t = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
  if (t) {
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
  }
  free(px);
  g_icon[mi] = t;
  return t;
}

static void free_icons(void) {
  for (int i = 0; i < MAXMODS; i++) {
    if (g_icon[i]) SDL_DestroyTexture(g_icon[i]);
    g_icon[i] = NULL;
    g_icon_tried[i] = 0;
  }
}

// ------------------------------------------------------------------ opening a folder or a link

static int shell_allowed(void) {
  static int v = -1;
  if (v < 0) v = !(getenv("SIMGOLF_NO_SHELL") || getenv("SIMGOLF_MUTE"));   // automated runs never open windows
  return v;
}

static void do_requests(void) {
  if (g_ui.open_path[0]) {
    DWORD at = GetFileAttributesA(g_ui.open_path);
    if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) {
      if (shell_allowed()) ShellExecuteA(NULL, "explore", g_ui.open_path, NULL, NULL, SW_SHOWNORMAL);
      else fprintf(stderr, "modsui: (automated run) would open folder %s\n", g_ui.open_path);
    } else fprintf(stderr, "modsui: folder %s does not exist\n", g_ui.open_path);
    g_ui.open_path[0] = 0;
  }
  if (g_ui.open_url[0]) {
    if (!strncmp(g_ui.open_url, "https://", 8) || !strncmp(g_ui.open_url, "http://", 7)) {
      if (shell_allowed()) ShellExecuteA(NULL, "open", g_ui.open_url, NULL, NULL, SW_SHOWNORMAL);
      else fprintf(stderr, "modsui: (automated run) would open link %s\n", g_ui.open_url);
    } else fprintf(stderr, "modsui: refused link %s (only http and https)\n", g_ui.open_url);
    g_ui.open_url[0] = 0;
  }
}

// ------------------------------------------------------------------ the layout log (tests)

static void log_layout(int w, int h, double now) {
  static int on = -1;
  static double last;
  if (on < 0) on = getenv("SIMGOLF_FRONTEND_LOG") != NULL || getenv("SIMGOLF_OPTIONS_LOG") != NULL || getenv("SIMGOLF_MODS_LOG") != NULL;
  if (!on || now - last < 700) return;
  last = now;
  char line[2800];
  int o = snprintf(line, sizeof line, "modsui: layout target %dx%d sel %s profile %s dirty %d restart %d dlg %d", w, h,
                   g_ui.selected >= 0 ? modmgr_manifest(g_mgr, g_ui.selected)->id : "-", modmgr_profile(g_mgr),
                   modmgr_dirty(g_mgr), modmgr_restart_needed(g_mgr), g_ui.dialog);
#define PUT(nm, r) \
  if ((r).w > 0) o += snprintf(line + o, sizeof line - (size_t)o, " %s %.0f,%.0f", nm, (r).x + (r).w / 2, (r).y + (r).h / 2)
  PUT("close", g_ui.log_close);
  PUT("apply", g_ui.log_apply);
  PUT("revert", g_ui.log_revert);
  PUT("pillbtn", g_ui.log_profile);
  PUT("new", g_ui.log_new);
  PUT("dup", g_ui.log_dup);
  PUT("ren", g_ui.log_ren);
  PUT("del", g_ui.log_del);
  PUT("configure", g_ui.log_configure);
  for (int i = 0; i < g_ui.log_nsettings && o < 2100; i++) {
    char name[160];
    snprintf(name, sizeof name, "setting_%s", g_ui.log_setting_key[i]);
    for (char *p = name; *p; p++) if (*p == '.') *p = '_';
    PUT(name, g_ui.log_setting[i]);
  }
  for (int i = 0; i < g_ui.log_nrows && o < 2200; i++) {
    const char *id = modmgr_manifest(g_mgr, g_ui.log_row_mod[i])->id;
    char nm[96];
    snprintf(nm, sizeof nm, "row_%s", id);
    PUT(nm, g_ui.log_row[i]);
    snprintf(nm, sizeof nm, "toggle_%s", id);
    PUT(nm, g_ui.log_toggle[i]);
    snprintf(nm, sizeof nm, "up_%s", id);
    PUT(nm, g_ui.log_up[i]);
    snprintf(nm, sizeof nm, "down_%s", id);
    PUT(nm, g_ui.log_down[i]);
  }
#undef PUT
  snprintf(line + o, sizeof line - (size_t)o, " end");
  fprintf(stderr, "%s\n", line);
}

// ------------------------------------------------------------------ frame

void modsp_draw(UiCtx *c, SDL_Renderer *r, int w, int h, double now, int in_game) {
  g_r = r;
  if (g_close_req) {
    g_close_req = 0;
    g_open = 0;
    g_open_req = 0;
    if (g_mgr) {
      modmgr_close(g_mgr);
      g_mgr = NULL;
    }
    free_icons();
    json_free(g_config);
    g_config = NULL;
  }
  if (g_open_req) {   // opened: read the mods folder and the running set now
    g_open_req = 0;
    g_open = 1;
    if (g_mgr) modmgr_close(g_mgr);
    free_icons();
    const char *run[MAXMODS];
    int n = mods_count();
    if (n > MAXMODS) n = MAXMODS;
    for (int i = 0; i < n; i++) run[i] = mods_id_at(i);
    g_mgr = modmgr_open(mods_root(), run, n);
    ui_mods_init(&g_ui, g_mgr);
    json_free(g_config);
    char *base = SDL_GetBasePath();
    char config_path[1000], err[300] = "";
    snprintf(config_path, sizeof config_path, "%sui/mod_settings.json", base ? base : "");
    SDL_free(base);
    g_config = modconfig_load(config_path, err, sizeof err);
    if (!g_config) fprintf(stderr, "modsui: settings unavailable: %s\n", err);
    g_ui.config = g_config;
    g_ui.setting_get = setting_value;
    g_ui.setting_save = setting_save;
    g_ui.icon_fn = icon_for;
    g_ui.in_game = in_game;
    fprintf(stderr, "modsui: opened (%s), %d mods installed, %d loaded, profile %s, folder %s\n", in_game ? "in a game" : "start menu",
            modmgr_count(g_mgr), n, modmgr_profile(g_mgr), mods_root() ? mods_root() : "?");
    g_kq_tail = g_kq_head;
  }
  if (!g_open || !g_mgr) return;
  g_ui.in_game = in_game;
  while (g_kq_tail != g_kq_head) {
    unsigned t = g_kq_tail;
    ui_mods_key(&g_ui, g_kq[t % KQ].k, g_kq[t % KQ].mods);
    g_kq_tail = t + 1;
  }
  ui_mods_draw(c, &g_ui, now);
  do_requests();
  log_layout(w, h, now);
  if (g_ui.req_close) {
    g_ui.req_close = 0;
    g_open = 0;
    fprintf(stderr, "modsui: closed\n");
    fflush(stderr);
    modmgr_close(g_mgr);
    g_mgr = NULL;
    free_icons();
    json_free(g_config);
    g_config = NULL;
  }
}

void modsf_device_reset(void) {
  for(int i=0;i<MAXMODS;i++) {
    if(g_icon[i])SDL_DestroyTexture(g_icon[i]);
    g_icon[i]=NULL;g_icon_tried[i]=0;
  }
}
