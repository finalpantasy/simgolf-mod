// Frosted Mint reports, hooks for the browser build (simgolf mod, task-28). The native build's hooks are
// in port/reports_game.c; the translator patches (tools/translate/golf.reports.json) call these. In the
// browser build each one keeps the original behaviour: the text calls draw as before, the rest do nothing.
#include "golf_protos.h"
#include "port/runtime.h"

#ifdef __EMSCRIPTEN__

void rp_tick(void) {}
void rp_begin(int kind) { (void)kind; }
void rp_frame(void) {}
void rp_done(void) {}
void rp_quad(void *w, unsigned a, unsigned b, unsigned c, unsigned d) { FUN_00476310(w, a, b, c, d); }
void rp_t650(void *w, int font, char *s, int x, int y, int len) { FUN_00476650(w, font, s, x, y, len); }
void rp_t6a0(void *w, int font, char *s, int x, int y, int wd, char *len) { FUN_004766a0(w, font, s, x, y, wd, len); }
void rp_t700(void *w, int font, char *s, int x, int y, char *len) { FUN_00476700(w, font, s, x, y, len); }
void rp_t750(void *w, int font, char *s, int x, int y, char *len) { FUN_00476750(w, font, s, x, y, len); }
int rp_tc(int fn, char *s, int x, int y, unsigned col) {
  switch (fn) {
    case 0: return FUN_00404b70(s, x, y, col);
    case 1: return FUN_00404bc0(s, x, y, col);
    case 2: return FUN_004049d0(s, x, y, col);
    default: return FUN_00404ad0(s, x, y, col);
  }
}
void rp_line(void *w, int x0, int y0, int x1, int y1, unsigned col) { FUN_00478b80(w, x0, y0, x1, y1, col); }
void rp_rect(void *page, int x, int y, int w, int h, unsigned col) { FUN_00480b00(page, x, y, w, h, col); }
int rp_chip(void *sprite, int target, int x, int y, int clip) { return FUN_00473e60(sprite, target, x, y, clip); }
int rp_ds(void *w, char *s, int x, int y, char *len) { return FUN_00478140(w, s, x, y, len); }
void rp_ov(int kind, int phase) { (void)kind, (void)phase; }
int rp_tcp(int fn, char *s, int x, int y, unsigned col) { return fn == 2 ? FUN_004049d0(s, x, y, col) : FUN_00404ad0(s, x, y, col); }
void rp_sub(int open) { (void)open; }
void rp_edit(void) {}
// task-73: the land purchase map
int rp_t7c30(void *w, char *s, int x, int y, int len) { return FUN_00477c30(w, s, x, y, len); }
int rp_t8430(void *w, char *s, int x, int y, int width) { return FUN_00478430(w, s, x, y, width); }
int rp_t8080(void *w, char *s, int x, int y, int a, char *len) { return FUN_00478080(w, s, x, y, a, len); }
int rp_esc(void) { return 0; }
void rp_wfix(int on) { (void)on; }
int rp_warg(int flags, int parent) { (void)parent; return flags; }
int rp_end(int v) { return v; }
// task-102: the skill points screen
void rp_skills(char *name, int points, int who, int x0) { (void)name, (void)points, (void)who, (void)x0; }
int rp_skills_frosted(void) { return 0; }
void rp_pace(void) { FUN_0045bf80(5, 0); }
// task-105: the accomplishment board
void rp_photo(int id, int x, int y) { (void)id, (void)x, (void)y; }
void rp_photo_blit(void *source, int target, int x, int y, int id) {
  (void)id;
  FUN_00475c60(source, target, 0, 0, x, y, 200, 160);
}
int rp_photo_limit(int vertical) { return vertical ? 440 : 600; }
void rp_board_wait(int n) { FUN_0045bf80(n, 0); }
void rp_year_continue(int y) { (void)y; }

#endif
