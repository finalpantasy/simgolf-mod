// Art pack (simgolf mod, task-17): the 4x upscaled game art (task-16,
// art_local/upscaled) drawn at screen resolution in place of golf's sprites.
// Native build only; see hd_art.c.

#ifndef SIMGOLF_PORT_HD_ART_H
#define SIMGOLF_PORT_HD_ART_H

#include <SDL.h>
#include <stdint.h>
#include "port/environment.h"

// Settings (hd_native.c's reader hands over every art* key; then the mods
// store's art.<category> and SIMGOLF_DISPLAY beat golf.ini).
void hd_art_set_key(const char *key, const char *value);
// Game thread, from hd_native_prepare (images loaded, before any drawing).
void hd_art_init(int presenter_on);
int hd_art_active(void);   // any category not original
// Game thread: current people-art source for the separate native portrait.
int hd_art_portrait_source(char *dir, int cap);
// Read-only identity of an existing original sprite wrapper, game thread only.
int hd_art_portrait_frame(uint32_t wrap, char *source, int cap, int *ax, int *ay);
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
// Render-thread only: shared wind sample for anchored flag cloth.
#include "port/course_relief.h"
void hd_art_relief(const CourseRelief *relief,int enabled);
void hd_art_fog_features(uint8_t *features,int width,int height,double fog_pixels);
void hd_art_enclosure_ground(SDL_Renderer *r,const uint8_t *ground,int width,int height,double ox,double oy,double scale);
void hd_art_lighting(const EnvironmentConfig *config,const EnvironmentSample *sun,double ax,double ay,double bx,double by);
void hd_art_lamps(SDL_Renderer *r,SDL_Rect clip,const uint8_t *receiver,int w,int h,double ox,double oy,double scale);
void hd_art_wind(int enabled,double seconds,double wind_x);
// Draws a list: canvas px -> target via (ox, oy) + px * k.
void hd_art_draw(int which, SDL_Renderer *r, double ox, double oy, double k);
// Course-only ground pass, immediately before hd_art_draw. The terrain mask
// uses canvas coordinates. Slopes are screen-ground displacement per pixel
// of displayed sprite height, after applying the camera's sun projection.
// enabled=0 releases all shadow resources and restores baked-shadow drawing.
int hd_art_draw_shadows(SDL_Renderer *r, SDL_Rect clip, double ox, double oy, double k,
                        const uint8_t *ground, int width, int height, int enabled,
                        double slope_x, double slope_y, double opacity, double ground_aspect);

// Game thread (hd_terrain_native.c): the terrain texture loaded from `src`
// (rgba w x h, rows as the terrain renderer keeps them) in the terrain look:
// a malloc'd 4x replacement (*ow x *oh), or NULL to keep the original.
uint8_t *hd_art_terrain(const char *src, const uint8_t *rgba, int w, int h, int *ow, int *oh);
typedef struct HdTerrainJob {
  const char *src;
  const uint8_t *rgba;
  int w, h;
  uint8_t *up;
  int uw, uh;
} HdTerrainJob;
// Native new-course preparation. Inputs stay immutable until all jobs join.
void hd_art_terrain_jobs(HdTerrainJob *jobs, int count);
void hdt_resend(void);   // hd_terrain_native.c: the terrain look changed
// Any thread: a PNG file as malloc'd straight-alpha ARGB8888 pixels (task-80 start menu logo), or NULL.
uint32_t *hd_art_load_png(const char *path, int *w, int *h);

// Native HUD design pictures. Capture the actual tagged object draw on the
// game thread, then request its cached HD image on the render thread. A NULL
// texture keeps the existing complete original picture while loading or when
// any part cannot be reproduced. Returned textures are owned by hd_art.
#define HD_ART_PREVIEW_MAX 128
unsigned hd_art_preview_revision(void);
void hd_art_preview_begin(int slot);
void hd_art_preview_pause(void);
void hd_art_preview_commit(int slot, int x, int y, int w, int h);
SDL_Texture *hd_art_preview_texture(int slot, SDL_Renderer *r);

#endif  // SIMGOLF_PORT_HD_ART_H
