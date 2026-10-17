// Frosted Mint UI kit (simgolf mod, task-26): the context and the components.
//
// Immediate mode: every frame a screen calls ui_begin, then one call per
// component with an id and a rectangle (target pixels), then ui_end. A
// component draws itself, adds its hit region, and returns 1 when it was
// clicked during the frame. Input reaches the kit through ui_route_event (any
// thread: it decides at once whether the game may see the event, from the
// regions of the last finished frame); the render thread consumes it at the
// next ui_begin. docs/ui/KIT.md has the full API and how screens use it.
#pragma once

#include <SDL.h>
#include <stdint.h>

#include "port/ui/ui_draw.h"
#include "port/ui/ui_faces.h"
#include "port/ui/ui_hit.h"
#include "port/ui/ui_icons.h"
#include "port/ui/ui_layout.h"
#include "port/ui/ui_text.h"
#include "port/ui/ui_tokens.h"

typedef struct UiCtx UiCtx;

// ------------------------------------------------------------------ setup

// font_dir: the Plus Jakarta Sans cuts (fonts/plus-jakarta-sans next to the exe).
UiCtx *ui_create(SDL_Renderer *r, const UiTokens *tokens, const char *font_dir);
// Debug overlay (task-113): name a context after creating it; ask each one whether its last frame covers a point.
void ui_debug_name(UiCtx *c, const char *name);
int ui_debug_count(void);
int ui_debug_probe(int i, float x, float y, const UiCtx *skip, char *name, int cap, uint32_t *id, UiRect *rect);
void ui_destroy(UiCtx *c);
const UiTokens *ui_tokens(const UiCtx *c);
void ui_set_tokens(UiCtx *c, const UiTokens *t);
// Loads data/ui/frosted_mint.toml-style files: base, then each mod's overlay.
// Defaults first; missing files are skipped. Problems go to stderr ("ui: ...").
void ui_tokens_load_all(UiTokens *t, const char *base_path, const char *const *overlays, int n);

// ------------------------------------------------------------------ input (any thread)

// Decides where an event goes (UI_ROUTE_GAME / UI / BOTH) and queues it for
// the UI when the UI gets it. Pointer coordinates in target pixels.
int ui_route_event(UiCtx *c, const UiEvent *e);
// Read the last finished frame's blocking regions without queuing input.
int ui_blocks_point(UiCtx *c, float x, float y);
// The pointer left the window or the layer is hidden: drop hover.
void ui_input_reset(UiCtx *c);

// ------------------------------------------------------------------ frame (render thread)

// target: where to draw (NULL = the current render target is the window);
// backdrop: the picture under the UI to frost (NULL = solid glass).
void ui_begin(UiCtx *c, SDL_Texture *target, int w, int h, float scale, SDL_Texture *backdrop, double now_ms);
void ui_end(UiCtx *c);
// Ends a frame that drew nothing: no regions, the game gets every event.
void ui_skip_frame(UiCtx *c);

float ui_scale(const UiCtx *c);
// Draw the next components k times as big (or small); pairs, nest up to 8.
void ui_push_scale(UiCtx *c, float k);
void ui_pop_scale(UiCtx *c);
// Pointer position in target pixels; returns 0 when it is outside the window.
int ui_pointer(const UiCtx *c, float *x, float *y);
// Share (0..1) of the target the last finished frame's UI covers (panels and
// controls), sampled every step pixels.
float ui_coverage(const UiCtx *c, int step);
float ui_s(const UiCtx *c, float v);          // token size -> pixels (rounded)
UiRect ui_screen(const UiCtx *c);             // the whole target
double ui_now(const UiCtx *c);
int ui_is_hot(const UiCtx *c, uint32_t id);
int ui_is_active(const UiCtx *c, uint32_t id);
int ui_clicked(const UiCtx *c, uint32_t id);
int ui_right_clicked(const UiCtx *c, uint32_t id);
float ui_wheel(const UiCtx *c, uint32_t id);  // wheel notches over a UI_HIT_WHEEL region this frame
UiDraw *ui_drawer(UiCtx *c);
UiText *ui_texter(UiCtx *c);
// 0..1 eased towards target over ms (hover fades, open/close slides).
float ui_anim(UiCtx *c, uint32_t id, float target, float ms);
float *ui_state(UiCtx *c, uint32_t id);       // a float that lives across frames (scroll offsets)
void ui_hit(UiCtx *c, UiRect r, float radius, uint32_t id, uint32_t flags);

// ------------------------------------------------------------------ text and icons

typedef struct { float px; int weight; float tracking, line; } UiFont;
UiFont ui_font(const UiCtx *c, int style);    // a type token at the current scale (floor applied)
float ui_text_width(UiCtx *c, int style, const char *s);
// Baseline at y; returns the pen x after the text.
float ui_text(UiCtx *c, float x, float y, int style, UiColor col, const char *s);
// Text in a box: align 0 left, 0.5 centre, 1 right; vertically centred on the caps.
void ui_text_in(UiCtx *c, UiRect r, int style, UiColor col, const char *s, float align);
// Same, cut with an ellipsis to fit the width.
void ui_text_fit(UiCtx *c, UiRect r, int style, UiColor col, const char *s, float align);
// An icon of size px centred in r.
void ui_icon(UiCtx *c, UiIcon icon, UiRect r, float size_px, UiColor col);
// A Frosted Mint mood face (0..10, golf's mood sprites redrawn, ui_faces.h) of size_px centred in r.
void ui_face(UiCtx *c, int mood, UiRect r, float size_px, float alpha);

// ------------------------------------------------------------------ components

// READABLE: still frosted, but over the near-opaque glass.solid fill, so the text on it keeps 7:1 contrast
// over any picture (a dark backdrop under the normal fill gives about 5:1)
enum { UI_PANEL_STRONG = 1, UI_PANEL_NOBLOCK = 2, UI_PANEL_NOSHADOW = 4, UI_PANEL_SOLID = 8, UI_PANEL_READABLE = 16 };
// Frosted glass: shadow, blurred backdrop, tint, border, top light. Blocks the pointer.
void ui_panel(UiCtx *c, UiRect r, float radius, int flags);

enum { UI_BTN_PRIMARY, UI_BTN_PILL, UI_BTN_GHOST };
enum { UI_DISABLED = 1, UI_SELECTED = 2, UI_COMPACT = 4, UI_ROUND = 8, UI_TILE_COVER = 16 };
int ui_button(UiCtx *c, uint32_t id, UiRect r, const char *label, UiIcon icon, int kind, int flags);
int ui_icon_button(UiCtx *c, uint32_t id, UiRect r, UiIcon icon, int flags, const char *tooltip);
// Segmented control (tabs): n segments, *sel changes on a click; returns 1 then.
// widths NULL: each segment as wide as its label; else the given widths.
int ui_segmented(UiCtx *c, uint32_t id, UiRect r, const char *const *labels, const UiIcon *icons, int n, int *sel);
float ui_segmented_width(UiCtx *c, const char *const *labels, const UiIcon *icons, int n);
// Dock button in a slot dock_item_w wide (square, label, active dot). label
// NULL: icon only, a square filling the slot (the label goes in the tooltip).
int ui_dock_item(UiCtx *c, uint32_t id, UiRect slot, UiIcon icon, const char *label, int active, const char *tooltip);
// A round category button (hub layouts): a small glass disc with the item in it.
int ui_round_item(UiCtx *c, uint32_t id, float cx, float cy, float d, UiIcon icon, int active, const char *tooltip);
// Build tool tile: art (any texture, kept to its aspect; else the icon), name, price.
int ui_tile(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *art, UiIcon icon, const char *name, const char *price, int flags,
            const char *tooltip);
float ui_tile_height(const UiCtx *c);
// The same tool as art only (the compact strip): name and price go in the tooltip.
// UI_TILE_COVER fills the mini tile with cropped art inside its rounded border.
int ui_tile_mini(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *art, UiIcon icon, int flags, const char *tooltip);
int ui_tile_mini_label(UiCtx *c, uint32_t id, UiRect r, UiIcon icon, const char *label, int flags, const char *tooltip);
// The same tool as one row of a list: art at the left, name, price at the right.
int ui_tile_row(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *art, UiIcon icon, const char *name, const char *price,
                int flags, const char *tooltip);
void ui_divider_v(UiCtx *c, float x, float y0, float y1);
// Icon, bold number and a quiet unit, left to right from x; returns the end x.
float ui_stat(UiCtx *c, float x, float cy, UiIcon icon, const char *value, const char *unit);
float ui_stat_width(UiCtx *c, UiIcon icon, const char *value, const char *unit);
// Shows text in a glass pill above anchor once id has been hot for the delay.
void ui_tooltip(UiCtx *c, uint32_t id, UiRect anchor, const char *text);
// Toast: glass capsule, optional portrait, title and one line (wrapped to 2).
float ui_toast_height(UiCtx *c, float w, const char *title, const char *line);
void ui_toast(UiCtx *c, UiRect r, SDL_Texture *portrait, const char *title, const char *line);
// Horizontal scroll row: content wider than view scrolls with the wheel.
// Returns the x offset to add to children; call ui_scroll_end after them.
float ui_scroll_begin(UiCtx *c, uint32_t id, UiRect view, float content_w);
void ui_scroll_end(UiCtx *c);

// Vertical scroll area (long lists): the wheel scrolls by step px; returns the y offset to add to children.
float ui_vscroll_begin(UiCtx *c, uint32_t id, UiRect view, float content_h, float step);
void ui_vscroll_end(UiCtx *c);

// ------------------------------------------------------------------ more components (gallery, UI-5/UI-6)

void ui_card(UiCtx *c, UiRect r, int selected);
void ui_meter(UiCtx *c, UiRect r, float value);      // 0..1, warn colours under 0.4
int ui_slider(UiCtx *c, uint32_t id, UiRect r, float *value, const char *value_text);
int ui_toggle(UiCtx *c, uint32_t id, UiRect r, int *on);
int ui_list_row(UiCtx *c, uint32_t id, UiRect r, SDL_Texture *avatar, const char *name, const char *line,
                const char *right, int flags);
// Dialog: scrim over the target, centred panel; returns the panel's content rect.
UiRect ui_dialog_begin(UiCtx *c, uint32_t id, float w, float h, const char *title);
void ui_thought(UiCtx *c, float x, float y, SDL_Texture *icon, const char *text);   // (x, y): the golfer's head
// Popover panel of size w x h beside an anchor point with an arrow; returns its rect.
UiRect ui_popover(UiCtx *c, float ax, float ay, float w, float h, int side);
void ui_tag(UiCtx *c, float cx, float y, const char *text, int selected);   // name tag under a golfer
