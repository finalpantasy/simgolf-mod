// Art pack (simgolf mod, task-17): the 4x upscaled game art (task-16,
// art_local/upscaled) drawn at screen resolution in place of golf's sprites.
// Native build only; see hd_art.c.

#ifndef SIMGOLF_PORT_HD_ART_H
#define SIMGOLF_PORT_HD_ART_H

#include <SDL.h>
#include <stdint.h>

// Settings (hd_native.c's reader hands over every art* key; then the mods
// store's art.<category> and SIMGOLF_DISPLAY beat golf.ini).
void hd_art_set_key(const char *key, const char *value);
// Game thread, from hd_native_prepare (images loaded, before any drawing).
void hd_art_init(int presenter_on);
int hd_art_active(void);   // any category not original
// Pixel HD (card task-71): the size in screen pixels of the pixel grid the course picture is drawn on
// (0: off). k: screen pixels per canvas pixel. Render thread.
int hd_art_pixel_grid(double k);
// The Textures choice in effect: "hd", "pixel_hd", "original" or "custom".
const char *hd_art_textures_name(void);

// Two lists per frame: HD_ART_COURSE (golf's course, under its UI) and
// HD_ART_TOP (golf's UI over the course; the whole picture on other pages).
enum { HD_ART_COURSE, HD_ART_TOP };
// Game thread. The course part of the frame is done (golf/hd_ui.c's hook):
// back buffer bits and pitch. Closes the course list.
void hd_art_course_done(const uint16_t *bits, int pitch);
// Game thread, hd_present: closes the top list against the finished frame
// and hands both to the render thread (layered=0: no course layer, the
// course list is not shown).
void hd_art_publish(int layered, const uint16_t *screen);
void hd_art_discard_pending(void);          // discard an unpublished transient frame; keep published art intact
int hd_art_mark(void);                      // start of a replaced original draw
void hd_art_hide_from(int mark);            // hide only art recorded since that mark

// Debug overlay (task-113). The sprite that owns canvas pixel (x, y) of the last published frame: the sheet
// (index.tsv name), frame, look, which list drew it (HD_ART_TOP: golf's own UI over the course, or the whole
// page; HD_ART_COURSE: the course world), plus how many entries' boxes cover the point. Game thread.
// Returns 1 found, 0 no entry owns the pixel (terrain, an untagged sprite, a native layer), -1 art pack off.
typedef struct {
  int layer, entry, entries;
  char file[200], shadow[200];
  const char *category, *look;
  int view, frame, variant, flip, hidden, ncover;
  float x, y, w, h;
} HdArtPick;
int hd_art_pick(int x, int y, HdArtPick *out);

void hd_art_read_store(void);   // the mods store's art.* keys (hd_native.c's reader)

// Render thread. hd_art_take (under the presenter's frame lock, with the
// frame) takes the newest list; hd_art_prepare uploads what the loader
// finished and works out which entries can be drawn: for a new frame, or
// when textures arrived; returns 1 when that changed the cut masks.
void hd_art_take(void);
int hd_art_prepare(SDL_Renderer *r, int new_frame);
// 1: canvas pixel i belongs to a drawn hi-res sprite of that list
// (hd_native.c cuts it out of golf's layer). NULL: none.
const uint8_t *hd_art_cut_mask(int which);
// Draws a list: canvas px -> target via (ox, oy) + px * k.
void hd_art_draw(int which, SDL_Renderer *r, double ox, double oy, double k);

// Game thread (hd_terrain_native.c): the terrain texture loaded from `src`
// (rgba w x h, rows as the terrain renderer keeps them) in the terrain look:
// a malloc'd 4x replacement (*ow x *oh), or NULL to keep the original.
uint8_t *hd_art_terrain(const char *src, const uint8_t *rgba, int w, int h, int *ow, int *oh);
void hdt_resend(void);   // hd_terrain_native.c: the terrain look changed
// Any thread: a PNG file as malloc'd straight-alpha ARGB8888 pixels (task-80 start menu logo), or NULL.
uint32_t *hd_art_load_png(const char *path, int *w, int *h);

#endif  // SIMGOLF_PORT_HD_ART_H
