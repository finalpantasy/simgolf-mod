// Issue62: input batching only. gameTick (0x0040f5c0) still owns every
// placement, validity check, affordability check, charge and accounting entry.
#ifdef __EMSCRIPTEN__
int place_drag_next(void) { return 0; }
void place_drag_pick(int *x,int *y) { (void)x; (void)y; }
void place_drag_draw(void) {}
void place_drag_frame(void) {}
void place_drag_reset(void) {}
extern unsigned FUN_00406c30(int);
int place_drag_afford(int cost) { return (int)FUN_00406c30(cost); }
#else
#include "port/golf/place_drag.h"
#include "port/golf/place_drag_shape.h"
#include "port/golf/camera_input.h"
#include "port/golf/hd_ui.h"
#include "port/grid.h"
#include "mods/mods.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int win_screen_w, win_screen_h;
#define G(a) (*(volatile int32_t *)(uintptr_t)(a))
extern int *FUN_00430020(int, int, int *, int *, int);
extern unsigned FUN_0042fb90(int, int, int *, int *, int);
extern unsigned FUN_00406c30(int);
extern int hud_on(void);
extern int hudf_blocks_course_point(int, int);
static struct {
  int held, pending, index, picked, x, y, terrain, building, design;
  int width, height, rotation, zoom, camera_x, camera_y;
  int joined_pot;
  int this_frame;
  PlaceDragShape shape;
} drag;

static int swallow_up;
static int enabled(void) { return mods_get_int("ui.drag_placement", 0); }
static void cancel(void) { memset(&drag, 0, sizeof drag); }
// Loading/generating clears work, but a captured physical press still owns
// its release. Otherwise that release can place a tile on the new course.
void place_drag_reset(void) { cancel(); }
static int matching(void) {
  return enabled() && drag.terrain == G(0x4c2848) && drag.building == G(0x4c2854)
    && (drag.terrain != 9 || drag.joined_pot == mods_get_int("terrain.joined_pot_bunkers",0))
    && drag.design == G(0x56a514) && drag.width == g_map_w && drag.height == g_map_h;
}
static int supported(void) {
  int b = G(0x4c2854), t = G(0x4c2848);
  if (b >= 0) return b == 0 || b == 2 || b == 16; // paths, flowers and scenic trees, never buildings/landmarks
  if(t==9) return mods_get_int("terrain.joined_pot_bunkers",0);
  // Explicitly exclude tees, mounds and special editing tools.
  // Terrain ids: terrain.md section2.4. Trees are tile terrain, not landmarks.
  return t == 1 || t == 2 || t == 3 || t == 4 || t == 5 || t == 7 || t == 8
    || t == 17 || t == 10 || t == 11 || t == 12 || t == 13 || t == 14 || t == 15;
}
// Match the established course click exclusions. Native controls receive
// first refusal at gesture start; a captured release must not build behind
// the top controls or the original news panel either.
static int course_point(int x, int y) {
  if (hudf_blocks_course_point(x,y)) return 0;
  if (x<8 || x>=win_screen_w-8 || y<75 || y>=win_screen_h-160) return 0;
  if (x<240 && y<84) return 0;
  int modern=hud_on();
  if (x>win_screen_w-(modern?400:215) && y<(modern?160:145)) return 0;
  if (!modern && *(volatile uint8_t *)(uintptr_t)0x569498==1 && x>=240 && x<=700 && y<210) return 0;
  return 1;
}
static int pick(int sx, int sy, int *x, int *y) {
  FUN_00430020(sx, sy, x, y, 0);
  return *x >= 0 && *y >= 0 && *x < g_map_w && *y < g_map_h;
}
static int view_unchanged(void) {
  return drag.rotation == G(0x5685f4) && drag.zoom == G(0x4c2844)
    && drag.camera_x == G(0x4c2ba0) && drag.camera_y == G(0x4c2ba4);
}

int place_drag_event(SDL_Event *e, int allow_start) {
  if (allow_start && e->type==SDL_MOUSEBUTTONDOWN && getenv("SIMGOLF_DRAG_TRACE"))
    fprintf(stderr,"drag_place: input %d,%d enabled=%d supported=%d active=%d flags=%x card=%d selection=%d design=%d zoom=%d\n",e->button.x,e->button.y,enabled(),supported(),camera_input_active(),G(0x59e7b8),G(0x53df54),G(0x569498),G(0x56a514),G(0x4c2844));
  if ((drag.held || drag.pending) && (!matching() || !camera_input_active())) cancel();
  if (drag.held && !view_unchanged()) cancel();
  // A fresh press takes priority over unfinished batches. Let the HUD handle
  // it first, then capture a new course gesture instead of losing its shape.
  if (drag.pending && e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) cancel();
  if (!drag.held && e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) swallow_up=0;
  if (!drag.held && swallow_up && e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT) {
    swallow_up=0; return 1;
  }
  if (e->type == SDL_WINDOWEVENT && (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST
      || e->window.event == SDL_WINDOWEVENT_LEAVE)) { cancel(); return 0; }
  if ((drag.held || drag.pending) && ((e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE)
      || (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT))) {
    cancel(); return 1;
  }
  if (drag.held) {
    if (e->type == SDL_MOUSEMOTION) {
      int x, y;
      if (pick(e->motion.x, e->motion.y, &x, &y)) { drag.shape.x1=x; drag.shape.y1=y; }
      return 0; // hover remains live, with no placement button held in the game
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
      int x, y;
      swallow_up=0;
      // Releasing over the HUD or outside the course cancels the gesture.
      if (!course_point(e->button.x,e->button.y)) { cancel(); return 1; }
      // This release was intercepted before the HUD. Apply the camera's
      // normal pointer mapping once, including an in-progress zoom glide.
      camera_input_event(e);
      if (!pick(e->button.x,e->button.y,&x,&y)) { cancel(); return 1; }
      drag.shape.x1=x; drag.shape.y1=y; drag.held=0; drag.pending=1;
      fprintf(stderr,"drag_place: release %d tiles tool=%d terrain=%d\n",
              place_drag_count(drag.shape),drag.building,drag.terrain);
      return 1;
    }
  }
  if (!allow_start || drag.pending || !enabled() || !supported() || !camera_input_active()) return 0;
  if (e->type != SDL_MOUSEBUTTONDOWN || e->button.button != SDL_BUTTON_LEFT) return 0;
  // Original course toolbar occupies the bottom160px; native HUD already got
  // first refusal. Reject special game modes and unresolved design choices.
  if (!course_point(e->button.x,e->button.y) || (G(0x59e7b8)&(2|0x8000000))
      || G(0x53df54) != 0 || *(volatile uint8_t *)(uintptr_t)0x569498 > 1 || G(0x56a514) < 0) return 0;
  int x,y;
  if (!pick(e->button.x,e->button.y,&x,&y)) return 0;
  cancel(); drag.held=1; swallow_up=1;
  drag.terrain=G(0x4c2848); drag.building=G(0x4c2854); drag.design=G(0x56a514);
  drag.joined_pot=mods_get_int("terrain.joined_pot_bunkers",0);
  drag.width=g_map_w; drag.height=g_map_h;
  drag.rotation=G(0x5685f4); drag.zoom=G(0x4c2844);
  drag.camera_x=G(0x4c2ba0); drag.camera_y=G(0x4c2ba4);
  drag.shape=(PlaceDragShape){x,y,x,y,drag.building==0};
  return 1;
}

void place_drag_frame(void) { drag.this_frame=0; }
int place_drag_next(void) {
  drag.picked=0;
  if (!drag.pending) return 0;
  int batch=mods_get_int("ui.drag_tiles_per_frame",16);
  if (batch<1) batch=1; if (batch>64) batch=64;
  if (drag.this_frame++ >= batch) return 0;
  if (!matching() || !place_drag_tile(drag.shape,drag.index++,&drag.x,&drag.y)) {
    cancel(); return 0;
  }
  drag.picked=1;
  // A course position bypasses toolbar routing. The exact tile is supplied
  // at the normal click picker. No price, unlock, tile or cash is written here.
  G(0x822d60)=win_screen_w/2; G(0x822d64)=200; G(0x822d68)=1;
  return 1;
}
void place_drag_pick(int *x,int *y) {
  if (drag.picked && drag.pending) { *x=drag.x; *y=drag.y; }
}
int place_drag_afford(int cost) {
  int allowed=(int)FUN_00406c30(cost);
  if (!allowed && drag.pending) cancel();
  return allowed;
}

// Draw a simple tile outline into the existing course buffer before it is
// published. It changes pixels only, and never marks tiles as affordable.
static void segment(uint16_t *p,int pitch,int w,int h,int x0,int y0,int x1,int y1) {
  int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1, err=dx+dy;
  for (;;) {
    if (x0>=0 && x0<w && y0>=0 && y0<h) p[y0*pitch+x0]=0x07ff;
    if (x0==x1 && y0==y1) break;
    int e=2*err; if (e>=dy) { err+=dy; x0+=sx; } if (e<=dx) { err+=dx; y0+=sy; }
  }
}
void place_drag_draw(void) {
  if (!drag.held || !matching() || !view_unchanged()) return;
  int pitch,w,h; uint16_t *p=hd_ui_back_bits(&pitch,&w,&h);
  if (!p) return;
  int count=place_drag_count(drag.shape), z=G(0x4c2844);
  for (int i=0;i<count;i++) {
    int x,y,sx,sy;
    place_drag_tile(drag.shape,i,&x,&y);
    FUN_0042fb90(x*1024+512,y*1024+512,&sx,&sy,0);
    segment(p,pitch,w,h,sx-8*z,sy,sx,sy-5*z);
    segment(p,pitch,w,h,sx,sy-5*z,sx+8*z,sy);
    segment(p,pitch,w,h,sx+8*z,sy,sx,sy+5*z);
    segment(p,pitch,w,h,sx,sy+5*z,sx-8*z,sy);
  }
}

#endif
