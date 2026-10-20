// Frosted Mint HUD, game-thread side (simgolf mod, task-30). See
// port/hud_frosted.h for the split and docs/ui/HUD_DISPATCH.md for every
// address used here.
//
// Hiding the old HUD without touching its logic: translator patches in the
// main loop FUN_0040f5c0 (tools/translate/golf.json) call hud_save() just
// before the old HUD draws (chip strip, banner, readouts, wheel, trays) and
// hud_restore() right after; hud_save keeps the back buffer's pixels under the
// old HUD's rectangles, hud_restore puts them back. Every draw still runs, so
// the game logic inside the draw code (ratings, the hire, design picks, list
// rebuilds) is unchanged; only its pixels are gone. The old wheel and trays
// get a mouse parked off screen (no hover tooltips), the "Paused" text and the
// news strip are not drawn (the new HUD shows both), and the main loop's
// literal hit-test gates (y > 440, the chip strip) only let through the clicks
// the new HUD injects. Those injected clicks go in through FUN_004657e0, the
// game's own left-click entry, at the original control's position, so the
// original handlers run exactly as for a real click.
//
// The native versions of hud_save, hud_restore and the gates are in
// port/hud_game.c; the browser build gets the no-op versions below. With
// ui.hud=original (or no native presenter) they keep the original behaviour.

#include "golf_protos.h"
#include "port/runtime.h"


#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile int8_t *)(uintptr_t)(a))

#define FLAGS 0x59e7b8
#define DOCK_MODE 0x567afc
#define SCREEN_STATE 0x569498
#define TEXT_BUF 0x51a068
#define NEWS_TEXT 0x5a6d40
#define MOUSE_X 0x822d60
#define MOUSE_Y 0x822d64
#define MOUSE_BTN 0x822d68

// ------------------------------------------------------------------ news strip (both builds)

int hud_on(void);

// @override FUN_0040d6a0
// The news strip tick (golf_01.c, called every course frame while a message
// shows or slides): the original, line for line, with its one draw call
// skipped while the Frosted Mint HUD shows the message as a toast instead.
uint FUN_0040d6a0(void) {
  if (G32(0x5694a4) != 0) {
    G32(0x5694a4) = G32(0x5694a4) - 1;
    return (uint)G32(0x5694a4);
  }
  {
    char *d = (char *)(uintptr_t)TEXT_BUF;
    const char *src = (const char *)(uintptr_t)NEWS_TEXT;
    while ((*d++ = *src++) != 0) {}
  }
  int slide = -1;
  if (G8(SCREEN_STATE) == 1) {
    if (G32(0x5a9ccc) < 3) slide = G32(0x5a9ccc) = G32(0x5a9ccc) + 1;
  } else if (G32(0x5a9ccc) != 0) {
    slide = G32(0x5a9ccc);
    G32(0x5a9ccc) = slide - 1;
  }
  if (!hud_on()) FUN_0040d320(0xf0, -8, (undefined4)G32(0x5a34ec), slide);
  uint r = (uint)G32(FLAGS);
  if ((G32(FLAGS) & 0x8000) == 0) {
    int tick = G32(0x834170);
    r = (uint)(tick / 3);
    if (tick % 3 != 0) {
      r = (uint)(G32(0x5a7144) - 1);
      G32(0x5a7144) = (int)r;
      if ((int)r < 1) G8(SCREEN_STATE) = 0;
    }
  }
  return r;
}

#ifdef __EMSCRIPTEN__

void hud_save(void) {}
void hud_restore(void) {}
void hud_news_posted(void) {}
void hud_departure_notice(int subject) { FUN_0040cb00(0x80000000u, 0, subject); }
void hud_note_script_click(int x, int y) { (void)x, (void)y; }
int hud_on(void) { return 0; }
int hud_mx(int x) { return x; }
int hud_my(int y) { return y; }
int hud_old_click(void) { return 1; }
int hud_strip_click(void) { return 1; }
int hud_ylim(int y) { return y; }
int hud_injecting(void) { return 0; }

#endif
