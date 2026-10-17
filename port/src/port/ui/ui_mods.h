// Frosted Mint "Mods" screen (simgolf mod, card task-24 style, task-22): the draw side.
//
// One page over a ModMgr (mods/modmgr.h): a list of installed mods with badges, search, sort and filter
// chips, a toggle and up/down/drag for the load order, a profile menu with create, duplicate, rename and
// delete, a detail panel for the selected mod, and Apply / Revert / Close. Every decision (conflicts,
// dependencies, order, restart) is the model's, which asks the platform's resolver; this file only draws
// and turns clicks into model calls. Profile edits reach disk with Apply;
// configuration choices save automatically through the presenter's callback.
//
// Immediate mode like the rest of the kit: ui_mods_draw is called between ui_begin and ui_end each frame.
// It never touches the OS: "open the folder" and "open the link" are requests in ModsUi (open_path,
// open_url) for the presenter (mods_frosted.c) to carry out. The gallery draws the same page offline.
#pragma once

#include <SDL.h>

#include "mods/modmgr.h"
#include "mods/modconfig.h"
#include "port/ui/ui_kit.h"

enum { MUI_DLG_NONE, MUI_DLG_NEW, MUI_DLG_RENAME, MUI_DLG_DELETE, MUI_DLG_CLOSE };

typedef struct ModsUi {
  ModMgr *m;
  ModView view;
  int selected;            // mod index, -1 none
  int focus;               // 0 nothing, 1 search box, 2 dialog field
  int dd_open;             // profile menu open
  int dialog;              // MUI_DLG_*
  char field[40];          // the dialog's text field
  int dlg_copy;            // New profile: start from the current mods
  char dlg_err[160];
  char msg[200];           // status line in the footer
  double msg_until;        // host clock (ms)
  double clock;            // the host clock at the last draw (ms)
  int in_game;             // opened over a running course (Esc menu): the footer says changes need a restart
  int drag;                // mod index being dragged, -1
  int drag_target;         // mod index it would drop on
  // Settings save immediately, independently of profile Apply / Revert.
  const JsonVal *config;
  const char *(*setting_get)(const char *key);
  int (*setting_save)(const char *key, const char *value);
  int configure;
  int config_selected;
  UiRect log_configure;
  UiRect log_setting[24];
  const char *log_setting_key[24];
  int log_nsettings;
  // requests for the host, cleared by it
  int req_close;
  char open_path[600];
  char open_url[300];
  // art: the host's icon cache (mod index -> texture or NULL)
  SDL_Texture *(*icon_fn)(void *user, int mod_index);
  void *icon_user;
  // layout log (rectangles of the last frame, target pixels), for the hidden test run
  UiRect log_list, log_detail, log_apply, log_close, log_profile;
  UiRect log_row[24];
  UiRect log_toggle[24];
  UiRect log_up[24], log_down[24];
  int log_row_mod[24];
  int log_nrows;
  UiRect log_new, log_dup, log_ren, log_del, log_revert;
} ModsUi;

void ui_mods_init(ModsUi *s, ModMgr *m);
// Called once per frame inside a ui_begin / ui_end pair; draws the whole page. now: host clock in ms.
void ui_mods_draw(UiCtx *c, ModsUi *s, double now);
// A key press for the page (SDL keycode, SDL_Keymod). Returns 1 when the page used it. Text typed into
// the search box or a dialog field comes through here too.
int ui_mods_key(ModsUi *s, SDL_Keycode key, int mods);
// Ask to close (shows the unapplied-changes question when the draft differs from disk).
void ui_mods_request_close(ModsUi *s);
// A message in the footer for a few seconds.
void ui_mods_say(ModsUi *s, double now, const char *fmt, ...);
