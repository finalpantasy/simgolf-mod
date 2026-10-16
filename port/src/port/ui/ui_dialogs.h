// Frosted Mint dialogs (simgolf mod, task-27, group G4): the popup menus, the
// check lists (preferences), the text entry strips and the other small modal
// screens of the original, drawn natively. Pure functions of one DlgSnap, the
// snapshot the game thread takes of whichever original dialog is running
// (dialogs_game.c), drawn with the kit at the screen's resolution. docs/ui/KIT.md.
//
// Like the front end, a dialog only shows what the original draws and asks for
// what the player chose (DlgAction): the game thread turns each action into a
// click or a key for the ORIGINAL dialog, so the original handlers run.
// Plain data and the kit: no game code.
#pragma once

#include <SDL.h>
#include <stdint.h>

#include "port/ui/ui_kit.h"

#define DLG_MAGIC 0x31474c44u   // "DLG1"

enum {
  DLG_NONE,
  DLG_MENU,     // a list of choices (golf's popup menu); lines that are not choices are the text above
  DLG_CHECKS,   // the same list with check marks and an OK disc (preferences)
  DLG_ENTRY,    // a text field with a prompt (save name, rename)
  DLG_HIRE,     // hire an employee: four groups of two jobs with their weekly wage
  DLG_FACE,     // original/imported and authored faces, preview then apply
  DLG_HINT,     // a message with its speaker (Gary's hints, tutorial steps): text, portrait, OK
  DLG_DOCUMENT, // shared scrolling notice or selectable text list
};

enum { DLG_LINES = 24, DLG_TEXT = 112, DLG_FACE_TILES = 12 };

typedef struct DlgSnap {
  uint32_t magic;
  uint32_t serial;
  int32_t kind;
  // DLG_MENU / DLG_CHECKS: the lines of the original's text. item < 0: text, else the choice number
  int32_t n_lines;
  struct {
    int32_t item;
    char text[DLG_TEXT];
  } line[DLG_LINES];
  int32_t n_items;
  int32_t sel;          // the choice the original highlights (keyboard), -1 none
  uint32_t mask;        // DLG_CHECKS: the choices that are ticked
  uint32_t disabled;    // choices the original greys out
  // DLG_ENTRY
  char prompt[DLG_TEXT];
  char field[160];      // what is typed so far
  int32_t caret;        // position in field
  int32_t selection_anchor;
  int32_t max_chars;
  // DLG_HIRE
  char group[4][40];
  char job[8][40];
  int32_t wage[8];      // dollars a week
  int32_t hover;        // the job the original highlights, -1 none
  // DLG_HINT
  char text[480];
  int32_t speaker;      // golf's speaker code (-2 Gary, 0.. a golfer, ...)
  int32_t face_filter,face_page,face_total,face_selected,face_rows,face_male;
  int32_t face_code[DLG_FACE_TILES],face_ready[DLG_FACE_TILES];
  char face_name[DLG_FACE_TILES][40],face_preview[40];
  int32_t has_art;      // the portrait picture was published (art 0)
  int32_t face_count;
  char document[2048];
  int32_t document_first; // original first selectable source line
  uint32_t document_epoch;
} DlgSnap;

enum { DLG_A_NONE, DLG_A_ITEM, DLG_A_OK, DLG_A_CANCEL, DLG_A_FACE_PAGE, DLG_A_FACE_FILTER };
typedef struct DlgAction {
  int32_t kind;
  int32_t arg;          // DLG_A_ITEM: the choice number
} DlgAction;

// Draws the dialog (scrim, panel, rows) and fills acts with what was chosen this frame; returns how many.
// art: pictures the game thread published (portrait of a hint at 0), NULL entries when none.
enum { DLG_ART_PORTRAIT, DLG_ART_FACE_FIRST, DLG_ART_MAX = DLG_ART_FACE_FIRST+DLG_FACE_TILES };
typedef struct DlgPixels { uint32_t *px; int w,h; } DlgPixels;
int ui_dialogs_draw(UiCtx *c, const DlgSnap *s, SDL_Texture *const *art, DlgAction *acts, int max);
