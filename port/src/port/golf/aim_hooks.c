// Shot aim arrows, game thread (simgolf mod, task-115). golf's course frame (FUN_0040f5c0) draws the aim of the
// player's pro: a ground track and a chain of segments through six or seven points of the shot's flight. The
// translator patches (tools/translate/golf.aim.json) send those draws here. With ui.aim=original (and in the
// browser build) every hook makes golf's own call. With ui.aim=frosted_mint the points are only noted (golf's own
// positions, golf's own curve for fade and draw) and hd_ui_course_done publishes them as a note for the Frosted
// Mint arrow (port/aim_frosted.c, ui/ui_aim.c).
#include "golf_protos.h"
#include "port/runtime.h"


#ifdef __EMSCRIPTEN__

void aim_start(int x, int y) { FUN_0040bf00(x, y); }
void aim_dot(int x, int y, int col, int w, int a) { FUN_0040bf20(x, y, col, w, a); }
void aim_seg(int x0, int y0, int x1, int y1, int col, int w, int a) { FUN_00406220(x0, y0, x1, y1, col, w, a); }
void aim_frame_begin(void) {}
void aim_course_done(void) {}

#else

#include "port/aim_frosted.h"
#include "port/aim_note.h"

void hd_ui_note3_set(const void *data, int size);   // port/hd_native.c (its header pulls in SDL)

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))

static AimNote g_note;
static int g_on;        // this frame is collecting (ui.aim=frosted_mint and the layer is up)
static uint32_t g_serial;
static int g_started;   // aim_start ran this frame

static int sat16(int v) { return v < -32000 ? -32000 : v > 32000 ? 32000 : v; }

void aim_frame_begin(void) {
  g_on = aimf_enabled();   // registers the world layer before the golfers' (their tags stay over the arrow)
  g_started = 0;
  g_note.n = g_note.ng = 0;
}

void aim_start(int x, int y) {
  if (!g_on) {
    FUN_0040bf00(x, y);
    return;
  }
  g_started = 1;
  g_note.n = 1;
  g_note.ng = 0;
  g_note.ex[0] = (int16_t)sat16(x);
  g_note.ey[0] = (int16_t)sat16(y);
  g_note.shot = G32(0x58f330);
  g_note.zoom = G32(0x4c2844);
}

void aim_dot(int x, int y, int col, int w, int a) {
  if (!g_on) {
    FUN_0040bf20(x, y, col, w, a);
    return;
  }
  if (g_started && g_note.n < AIM_PTS) {
    g_note.ex[g_note.n] = (int16_t)sat16(x);
    g_note.ey[g_note.n] = (int16_t)sat16(y);
    g_note.n++;
    // Golf builds this preview after hd_ui_course_done, alongside its shot
    // controls. Publish each completed sample so the final one is attached
    // to the upcoming present instead of being cleared at the next frame.
    g_note.magic = AIM_MAGIC;
    g_note.serial = ++g_serial;
    hd_ui_note3_set(&g_note, (int)sizeof g_note);
  }
}

// golf's black ground track: a segment from the last ground point to this one, drawn one row down
void aim_seg(int x0, int y0, int x1, int y1, int col, int w, int a) {
  if (!g_on) {
    FUN_00406220(x0, y0, x1, y1, col, w, a);
    return;
  }
  if (!g_started || g_note.ng >= AIM_PTS) return;
  if (g_note.ng == 0) {
    g_note.gx[0] = (int16_t)sat16(x0);
    g_note.gy[0] = (int16_t)sat16(y0 - 1);
    g_note.ng = 1;
  }
  g_note.gx[g_note.ng] = (int16_t)sat16(x1);
  g_note.gy[g_note.ng] = (int16_t)sat16(y1 - 1);
  g_note.ng++;
}

// End of the course frame: hand over this frame's arrow (none when golf drew none)
void aim_course_done(void) {
  if (!g_on) return;
  if (g_started && g_note.n >= 2) {
    g_note.magic = AIM_MAGIC;
    g_note.serial = ++g_serial;
    hd_ui_note3_set(&g_note, (int)sizeof g_note);
  }
  g_started = 0;
}

#endif
