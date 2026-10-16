// Key bindings, see keymap.h (simgolf mod, card task-24). Native build only.
#ifndef __EMSCRIPTEN__

// an undeclared function returning a double leaves the x87 stack unbalanced: never allow it (the build passes -w)
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#include "port/golf/keymap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/golf/camera_input.h"
#include "port/host.h"
#include "port/runtime.h"

void win_key_char(int ch);          // win32.c: the game gets this character as typed
void win_key_vk(int vk, int down);  // win32.c: and this virtual key

enum { M_SHIFT = 1, M_CTRL = 2, M_ALT = 4 };
enum { K_CHAR, K_VK, K_PAN, K_OPT, K_STORE };
typedef struct { SDL_Keycode sym; int mods; } Combo;

typedef struct {
  const char *id, *label, *group;
  int kind, arg;                // CHAR: the character, VK: the virtual key, PAN: 0 up 1 left 2 down 3 right
  const char *orig[KM_SLOTS];   // the original game's keys (the original profile)
  const char *modern[KM_SLOTS]; // the modern profile where it differs (NULL: the same)
} Act;

#define NONE ""
#define A(id, label, group, kind, arg, o0, o1, ...) {id, label, group, kind, arg, {o0, o1}, {__VA_ARGS__}}
#define SAME
// Order matters: when two actions share a key (a user's choice), the earlier one answers.
static const Act k_act[] = {
    // camera
    A("pan_up", "Pan up", "Camera", K_PAN, 0, NONE, NONE, "W", NONE),
    A("pan_left", "Pan left", "Camera", K_PAN, 1, NONE, NONE, "A", NONE),
    A("pan_down", "Pan down", "Camera", K_PAN, 2, NONE, NONE, "S", NONE),
    A("pan_right", "Pan right", "Camera", K_PAN, 3, NONE, NONE, "D", NONE),
    A("zoom_in", "Zoom in", "Camera", K_CHAR, 'z', "Z", NONE, SAME),
    A("zoom_out", "Zoom out", "Camera", K_CHAR, 'x', "X", NONE, SAME),
    A("rotate_left", "Rotate left", "Camera", K_VK, 0x24, "Home", NONE, SAME),
    A("rotate_right", "Rotate right", "Camera", K_VK, 0x21, "PageUp", NONE, SAME),
    A("scroll_up", "Scroll up (game steps)", "Camera", K_VK, 0x26, "Up", NONE, SAME),
    A("scroll_down", "Scroll down (game steps)", "Camera", K_VK, 0x28, "Down", NONE, SAME),
    A("scroll_left", "Scroll left (game steps)", "Camera", K_VK, 0x25, "Left", NONE, SAME),
    A("scroll_right", "Scroll right (game steps)", "Camera", K_VK, 0x27, "Right", NONE, SAME),
    A("clubhouse", "Camera to the clubhouse", "Camera", K_CHAR, '0', "0", NONE, SAME),
    // build and tools
    A("tool_fairway", "Fairway (fade shot)", "Build and tools", K_CHAR, 'f', "F", NONE, SAME),
    A("tool_green", "Green and tee", "Build and tools", K_CHAR, 'g', "G", NONE, SAME),
    A("tool_rough", "Rough", "Build and tools", K_CHAR, 'r', "R", NONE, SAME),
    A("tool_sand", "Sand trap (straight shot)", "Build and tools", K_CHAR, 's', "S", NONE, "C", NONE),
    A("tool_tree", "Tree", "Build and tools", K_CHAR, 't', "T", NONE, SAME),
    A("tool_stream", "Stream", "Build and tools", K_CHAR, 'v', "V", NONE, SAME),
    A("tool_water", "Water", "Build and tools", K_CHAR, 'w', "W", NONE, "Q", NONE),
    A("tool_bench", "Bench", "Build and tools", K_CHAR, 'b', "B", NONE, SAME),
    A("tool_path", "Cart path", "Build and tools", K_CHAR, 'p', "P", NONE, SAME),
    A("toggle_elevation", "Elevation tray", "Build and tools", K_CHAR, 'e', "E", NONE, SAME),
    A("shot_draw", "Draw shot", "Build and tools", K_CHAR, 'd', "D", NONE, "K", NONE),
    A("shot_high", "Open the hole (high shot)", "Build and tools", K_CHAR, 'h', "H", NONE, SAME),
    A("shot_low", "Low shot", "Build and tools", K_CHAR, 'l', "L", NONE, SAME),
    A("raise_terrain", "Raise the ground", "Build and tools", K_CHAR, '=', "=", NONE, SAME),
    A("lower_terrain", "Lower the ground", "Build and tools", K_CHAR, '-', "-", NONE, SAME),
    A("analyze_shot", "Analyze a shot", "Build and tools", K_CHAR, '.', ".", "/", SAME),
    A("next_shape", "Next shape", "Build and tools", K_CHAR, 9, "Tab", NONE, SAME),
    A("clear_tool", "Put the tool away", "Build and tools", K_CHAR, 13, "Return", "Escape", SAME),
    A("close_dock", "Close the tray", "Build and tools", K_CHAR, ' ', "Space", NONE, NONE, NONE),
    // holes
    A("hole_1", "Hole 1", "Holes", K_CHAR, '1', "1", NONE, SAME),
    A("hole_2", "Hole 2", "Holes", K_CHAR, '2', "2", NONE, SAME),
    A("hole_3", "Hole 3", "Holes", K_CHAR, '3', "3", NONE, SAME),
    A("hole_4", "Hole 4", "Holes", K_CHAR, '4', "4", NONE, SAME),
    A("hole_5", "Hole 5", "Holes", K_CHAR, '5', "5", NONE, SAME),
    A("hole_6", "Hole 6", "Holes", K_CHAR, '6', "6", NONE, SAME),
    A("hole_7", "Hole 7", "Holes", K_CHAR, '7', "7", NONE, SAME),
    A("hole_8", "Hole 8", "Holes", K_CHAR, '8', "8", NONE, SAME),
    A("hole_9", "Hole 9", "Holes", K_CHAR, '9', "9", NONE, SAME),
    A("hole_10", "Hole 10", "Holes", K_CHAR, '!', "Shift+1", NONE, SAME),
    A("hole_11", "Hole 11", "Holes", K_CHAR, '@', "Shift+2", NONE, SAME),
    A("hole_12", "Hole 12", "Holes", K_CHAR, '#', "Shift+3", NONE, SAME),
    A("hole_13", "Hole 13", "Holes", K_CHAR, '$', "Shift+4", NONE, SAME),
    A("hole_14", "Hole 14", "Holes", K_CHAR, '%', "Shift+5", NONE, SAME),
    A("hole_15", "Hole 15", "Holes", K_CHAR, '^', "Shift+6", NONE, SAME),
    A("hole_16", "Hole 16", "Holes", K_CHAR, '&', "Shift+7", NONE, SAME),
    A("hole_17", "Hole 17", "Holes", K_CHAR, '*', "Shift+8", NONE, SAME),
    A("hole_18", "Hole 18", "Holes", K_CHAR, '(', "Shift+9", NONE, SAME),
    // reports and maps
    A("course_report", "Course report", "Reports and maps", K_VK, 0x70, "F1", NONE, SAME),
    A("player_comments", "Player comments", "Reports and maps", K_VK, 0x71, "F2", NONE, SAME),
    A("histograph", "Histograph", "Reports and maps", K_VK, 0x72, "F3", NONE, SAME),
    A("financial_report", "Financial report", "Reports and maps", K_VK, 0x73, "F4", NONE, SAME),
    A("routing_map", "Routing map", "Reports and maps", K_VK, 0x74, "F5", "Shift+R", SAME),
    A("world_map", "World map", "Reports and maps", K_VK, 0x75, "F6", "Shift+W", SAME),
    A("sga_evaluation", "SGA evaluation", "Reports and maps", K_VK, 0x76, "F7", NONE, SAME),
    A("shortcuts_screen", "Keyboard shortcuts screen", "Reports and maps", K_VK, 0x77, "F8", NONE, SAME),
    A("membership_roster", "Membership roster", "Reports and maps", K_VK, 0x78, "F9", NONE, SAME),
    A("accomplishments", "Professional accomplishments", "Reports and maps", K_VK, 0x79, "F10", NONE, SAME),
    A("repeat_message", "Repeat the last message", "Reports and maps", K_CHAR, '?', "Shift+/", NONE, SAME),
    // the game
    A("pause", "Pause", "Game", K_CHAR, 'P', "Shift+P", NONE, "Space", "Shift+P"),
    A("save_game", "Save the game", "Game", K_CHAR, 'S', "Shift+S", NONE, SAME),
    A("load_game", "Load a game", "Game", K_CHAR, 'L', "Shift+L", NONE, SAME),
    A("save_course", "Save the course for championship", "Game", K_CHAR, 'C', "Shift+C", NONE, SAME),
    A("tournament", "Begin a tournament", "Game", K_CHAR, 'J', "Shift+J", NONE, SAME),
    A("hide_trees", "Hide or show trees", "Game", K_CHAR, 'T', "Shift+T", NONE, SAME),
    A("toggle_names", "Golfer names on or off", "Game", K_CHAR, 'N', "Shift+N", NONE, SAME),
    A("cancel_match", "Cancel the match or tournament", "Game", K_CHAR, 'n', "N", NONE, SAME),
    // the mod's own
    A("hud_hide", "Hide or show the HUD", "This mod", K_STORE, 0, "U", NONE, SAME),
    A("path_view", "Cart path view (what joins the clubhouse)", "This mod", K_STORE, 1, "Y", NONE, SAME),
    A("textures_cycle", "Switch Textures (HD, Pixel HD, Original)", "This mod", K_OPT, 0, "F11", NONE, SAME),
    A("opt_page", "Open the Options page", "This mod", K_OPT, 0, "F12", NONE, SAME),
    A("mods_page", "Open the Mods screen", "This mod", K_OPT, 0, "Shift+F12", NONE, SAME),
};
#define NACT ((int)(sizeof k_act / sizeof k_act[0]))

// ------------------------------------------------------------------ combos

static int is_mod_key(SDL_Keycode k) {
  return k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LALT ||
         k == SDLK_RALT || k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_CAPSLOCK || k == SDLK_NUMLOCKCLEAR ||
         k == SDLK_SCROLLLOCK || k == SDLK_MODE;
}

static SDL_Keycode norm_sym(SDL_Keycode k) { return k >= 'A' && k <= 'Z' ? k - 'A' + 'a' : k; }

static int parse(const char *s, Combo *c) {
  c->sym = 0;
  c->mods = 0;
  while (*s == ' ') s++;
  if (!*s || !strcasecmp(s, "none") || !strcasecmp(s, "default")) return 0;
  for (;;) {
    if (!strncasecmp(s, "ctrl+", 5)) c->mods |= M_CTRL, s += 5;
    else if (!strncasecmp(s, "shift+", 6)) c->mods |= M_SHIFT, s += 6;
    else if (!strncasecmp(s, "alt+", 4)) c->mods |= M_ALT, s += 4;
    else break;
  }
  char name[48];
  snprintf(name, sizeof name, "%s", s);
  for (int n = (int)strlen(name); n > 0 && name[n - 1] == ' '; n--) name[n - 1] = 0;
  SDL_Keycode k = SDL_GetKeyFromName(name);
  if (k == SDLK_UNKNOWN) return 0;
  c->sym = norm_sym(k);
  return 1;
}

static void format(const Combo *c, char *out, int n) {
  if (!c->sym) {
    snprintf(out, (size_t)n, "%s", "");
    return;
  }
  snprintf(out, (size_t)n, "%s%s%s%s", c->mods & M_CTRL ? "Ctrl+" : "", c->mods & M_SHIFT ? "Shift+" : "",
           c->mods & M_ALT ? "Alt+" : "", SDL_GetKeyName(c->sym));
}

static int same(const Combo *a, const Combo *b) { return a->sym && a->sym == b->sym && a->mods == b->mods; }

static int event_combo(const SDL_Event *e, Combo *c) {
  c->sym = 0;
  c->mods = 0;
  if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return 0;
  SDL_Keycode k = e->key.keysym.sym;
  if (k == SDLK_UNKNOWN || is_mod_key(k)) return 0;
  c->sym = norm_sym(k);
  int m = e->key.keysym.mod;
  c->mods = (m & KMOD_SHIFT ? M_SHIFT : 0) | (m & KMOD_CTRL ? M_CTRL : 0) | (m & KMOD_ALT ? M_ALT : 0);
  return 1;
}

int km_event_combo(const SDL_Event *e, char *out, int n) {
  Combo c;
  if (!event_combo(e, &c)) {
    if (n > 0) out[0] = 0;
    return 0;
  }
  format(&c, out, n);
  return 1;
}

// ------------------------------------------------------------------ bindings

int km_modern(void) { return camera_input_wasd(); }

static const char *default_text(const Act *a, int slot) {
  const char *m = km_modern() ? a->modern[slot] : NULL;
  return m ? m : a->orig[slot];
}

// The setting a K_STORE action keeps its key in (arg 0: the HUD hide key, 1: the cart path view key)
static const char *store_key(const Act *a) { return a->arg == 1 ? "ui.paths_key" : "ui.hud_hide_key"; }

// The binding of slot: from the store ("keys.<id>" = "primary | secondary"), else the profile's default.
static void binding(int ai, int slot, Combo *c) {
  const Act *a = &k_act[ai];
  if (a->kind == K_STORE) {   // the HUD hide key and the path view key keep their own settings
    const char *v = mods_get(store_key(a));
    if (slot == 0) parse(v && *v ? v : a->orig[0], c);
    else c->sym = 0, c->mods = 0;
    return;
  }
  char key[64];
  snprintf(key, sizeof key, "keys.%s", a->id);
  const char *v = mods_get(key);
  if (v && *v && strcasecmp(v, "default")) {
    char buf[96];
    snprintf(buf, sizeof buf, "%s", v);
    char *bar = strstr(buf, " | ");
    if (slot == 1) {
      if (bar) parse(bar + 3, c);
      else c->sym = 0, c->mods = 0;
    } else {
      if (bar) *bar = 0;
      parse(buf, c);
    }
    return;
  }
  parse(default_text(a, slot), c);
}

int km_count(void) { return NACT; }
const char *km_id(int a) { return a >= 0 && a < NACT ? k_act[a].id : ""; }
const char *km_label(int a) { return a >= 0 && a < NACT ? k_act[a].label : ""; }
const char *km_group(int a) { return a >= 0 && a < NACT ? k_act[a].group : ""; }

void km_binding(int a, int slot, char *out, int n) {
  Combo c;
  if (a < 0 || a >= NACT || slot < 0 || slot >= KM_SLOTS) {
    if (n > 0) out[0] = 0;
    return;
  }
  binding(a, slot, &c);
  format(&c, out, n);
}

int km_set(int a, int slot, const char *combo) {
  if (a < 0 || a >= NACT || slot < 0 || slot >= KM_SLOTS) return 0;
  Combo c;
  int ok = parse(combo, &c);
  if (k_act[a].kind == K_STORE) {   // the HUD key: an SDL key name
    if (slot != 0) return 0;
    return mods_option_set(store_key(&k_act[a]), ok ? SDL_GetKeyName(c.sym) : "none");
  }
  char txt[2][48];
  for (int s = 0; s < KM_SLOTS; s++) {
    Combo cur;
    binding(a, s, &cur);
    format(&cur, txt[s], sizeof txt[s]);
  }
  if (slot == 0) format(&c, txt[0], sizeof txt[0]);
  else format(&c, txt[1], sizeof txt[1]);
  if (!ok && combo && *combo) return 0;
  char val[110], key[64];
  snprintf(val, sizeof val, "%s | %s", txt[0][0] ? txt[0] : "none", txt[1][0] ? txt[1] : "none");
  snprintf(key, sizeof key, "keys.%s", k_act[a].id);
  return mods_option_set(key, val);
}

int km_find(const char *combo, int except, int *slot) {
  Combo c, b;
  if (!parse(combo, &c)) return -1;
  for (int a = 0; a < NACT; a++) {
    if (a == except) continue;
    for (int s = 0; s < KM_SLOTS; s++) {
      binding(a, s, &b);
      if (same(&c, &b)) {
        if (slot) *slot = s;
        return a;
      }
    }
  }
  return -1;
}

int km_event_is(const SDL_Event *e, const char *id) {
  Combo c, b;
  if (!event_combo(e, &c)) return 0;
  for (int a = 0; a < NACT; a++) {
    if (strcmp(k_act[a].id, id)) continue;
    for (int s = 0; s < KM_SLOTS; s++) {
      binding(a, s, &b);
      if (same(&c, &b)) return 1;
    }
  }
  return 0;
}

void km_reset(int modern) {
  mods_option_set("camera.wasd", modern ? "1" : "0");
  for (int a = 0; a < NACT; a++) {
    if (k_act[a].kind == K_STORE) {
      mods_option_set(store_key(&k_act[a]), k_act[a].orig[0]);
      continue;
    }
    char key[64];
    snprintf(key, sizeof key, "keys.%s", k_act[a].id);
    const char *v = mods_get(key);
    if (v && *v && strcasecmp(v, "default")) mods_option_set(key, "default");
  }
}

// ------------------------------------------------------------------ the event pump

// Keys we took on their way down: their release is ours too.
static struct { SDL_Keycode sym; int kind, arg; } g_down[16];
static int g_ndown;
static double g_eat_text_until;

static void track(SDL_Keycode sym, int kind, int arg) {
  for (int i = 0; i < g_ndown; i++)
    if (g_down[i].sym == sym) {
      g_down[i].kind = kind, g_down[i].arg = arg;
      return;
    }
  if (g_ndown < 16) g_down[g_ndown].sym = sym, g_down[g_ndown].kind = kind, g_down[g_ndown++].arg = arg;
}

static int orig_has(const Act *a, const Combo *c) {
  Combo o;
  for (int s = 0; s < KM_SLOTS; s++)
    if (parse(a->orig[s], &o) && same(&o, c)) return 1;
  return 0;
}

int keymap_event(SDL_Event *e) {
  double now = port_now_ms();
  if (e->type == SDL_TEXTINPUT) {
    if (g_eat_text_until > now) {
      g_eat_text_until = 0;
      return 1;
    }
    return 0;
  }
  if (e->type == SDL_WINDOWEVENT) {
    if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {   // keys released elsewhere never come back
      for (int i = 0; i < g_ndown; i++) {
        if (g_down[i].kind == K_VK) win_key_vk(g_down[i].arg, 0);
        if (g_down[i].kind == K_PAN) camera_input_pan_key(g_down[i].arg, 0);
      }
      g_ndown = 0;
    }
    return 0;
  }
  if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return 0;
  SDL_Keycode sym = norm_sym(e->key.keysym.sym);
  if (e->type == SDL_KEYUP) {
    for (int i = 0; i < g_ndown; i++)
      if (g_down[i].sym == sym) {
        if (g_down[i].kind == K_VK) win_key_vk(g_down[i].arg, 0);
        else if (g_down[i].kind == K_PAN) camera_input_pan_key(g_down[i].arg, 0);
        g_down[i] = g_down[--g_ndown];
        return 1;
      }
    return 0;
  }
  Combo ev;
  if (!event_combo(e, &ev) || !camera_input_active()) return 0;
  // a key bound to an action
  for (int a = 0; a < NACT; a++) {
    const Act *act = &k_act[a];
    for (int s = 0; s < KM_SLOTS; s++) {
      Combo b;
      binding(a, s, &b);
      if (!same(&ev, &b)) continue;
      if (act->kind == K_OPT || act->kind == K_STORE) return 0;   // the page and the HUD read these themselves
      if (orig_has(act, &ev)) return 0;                           // the very key the game reads for it
      g_eat_text_until = now + 80;
      if (!strcmp(act->id, "pause") && e->key.repeat) return 1;
      if (act->kind == K_CHAR) {
        win_key_char(act->arg);
        track(sym, K_CHAR, act->arg);
      } else if (act->kind == K_VK) {
        win_key_vk(act->arg, 1);
        track(sym, K_VK, act->arg);
      } else if (!e->key.repeat) {
        camera_input_pan_key(act->arg, 1);
        track(sym, K_PAN, act->arg);
      }
      return 1;
    }
  }
  // not bound: but the original key of an action that now has other keys does nothing any more
  for (int a = 0; a < NACT; a++) {
    const Act *act = &k_act[a];
    if (act->kind == K_OPT || act->kind == K_STORE || !orig_has(act, &ev)) continue;
    g_eat_text_until = now + 80;
    track(sym, K_CHAR, 0);
    return 1;
  }
  return 0;
}

#endif  // __EMSCRIPTEN__
