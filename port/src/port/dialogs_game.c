// Frosted Mint dialogs, game-thread side, native build (simgolf mod, task-27).
// The overview is in port/dialogs_frosted.h. This file does not include
// golf_protos.h (its types clash with the C library headers).
//
// The original's modal dialogs (golf's popup menu FUN_0046d6e0 and the text
// entry FUN_0045b2c0) loop on the pointer and the keyboard and draw into the
// back buffer. They keep running as they are. With ui.dialogs=frosted_mint and
// the presenter up, translator patches (port/tools/translate/golf.dialogs.json)
// call these hooks:
//   dlg_menu / dlg_entry   once per frame, just before the loop presents:
//       read what the dialog shows into a DlgSnap and publish it (the native
//       layer draws the Frosted Mint dialog; golf's own picture of the dialog
//       is hidden), run the click injector, and answer 1 when the player asked
//       to close it.
//   dlg_done               the dialog returns.
// Clicks: a native choice only says what was asked (DlgAction); here it becomes
// a scripted mouse move, press and release (SDL events without a window, which
// the game takes as its pointer) at the ORIGINAL control's place, one step per
// frame, so the original handlers run.
#include <SDL.h>
#include "port/text_selection.h"
#include "mods/mods.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/dialogs_frosted.h"
#include "port/hd_art_dialog_cutoff.h"
#include "port/hd_native.h"
#include "port/roster.h"
#include "port/portrait_game.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))

#define TEXT_BUF 0x51a068   // golf's shared text buffer: the dialog's lines, the entry's text

static int g_on;            // this dialog: the native version replaces the original
static int g_kind;
static int g_modal_depth, g_hint_open;
static int g_log = -1;
static uint32_t g_serial;
static DlgSnap g_snap;

// the click injector
static int g_inj, g_inj_x, g_inj_y, g_inj_stage, g_inj_wait;

static void sdl_motion(int x, int y) {
  SDL_Event e = {0};
  e.type = SDL_MOUSEMOTION;
  e.motion.x = x;
  e.motion.y = y;
  SDL_PushEvent(&e);
}

static void sdl_button(int x, int y, int down) {
  SDL_Event e = {0};
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.clicks = 1;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}

static void inj_start(int x, int y) {
  g_inj = 1;
  g_inj_x = x;
  g_inj_y = y;
  g_inj_stage = 0;
  g_inj_wait = 0;
  if (g_log) fprintf(stderr, "dialogs: click at the original's %d,%d\n", x, y);
}

// One step per frame: move, press, release.
static void inj_step(void) {
  if (!g_inj) return;
  if (g_inj_wait > 0) {
    g_inj_wait--;
    return;
  }
  switch (g_inj_stage) {
    case 0: sdl_motion(g_inj_x, g_inj_y); g_inj_wait = 1; break;
    case 1: sdl_button(g_inj_x, g_inj_y, 1); g_inj_wait = 2; break;
    case 2: sdl_button(g_inj_x, g_inj_y, 0); g_inj_wait = 1; break;
    default: g_inj = 0; return;
  }
  g_inj_stage++;
}

// What the pictures held when the dialog opened, put back before every present: the original draws its own
// dialog (and the caller's strip) into golf's page buffers, the native one replaces them. golf draws into one
// of several page buffers (the course and its UI layer, hud_game.c), so every page is kept.
uint16_t *gdi_screen_dib(int i, int *w, int *h, int *pitch, int *is565);
void hd_art_dlg_begin(void);      // hd_art.c (task-88)
void hd_art_dlg_begin_pre(void);  // Save Name: also hide the caller's strip, recorded after dlg_pre
void hd_art_dlg_pre(void);        // mark art before the Save Name caller draws its strip
void hd_art_hide_dialog(void);
enum { PAGES = 8 };
static uint16_t *g_keep[PAGES];
static int g_keep_w[PAGES], g_keep_h[PAGES];

static int g_kept;

static void keep_free(void) {
  g_kept = 0;
  for (int i = 0; i < PAGES; i++) {
    free(g_keep[i]);
    g_keep[i] = NULL;
  }
}

static void keep_save(void) {
  keep_free();
  g_kept = 1;
  for (int i = 0; i < PAGES; i++) {
    int w, h, pitch, is565;
    uint16_t *bb = gdi_screen_dib(i, &w, &h, &pitch, &is565);
    if (!bb) continue;
    g_keep[i] = malloc((size_t)w * h * 2);
    if (!g_keep[i]) continue;
    for (int y = 0; y < h; y++) memcpy(g_keep[i] + (size_t)y * w, bb + (ptrdiff_t)y * pitch, (size_t)w * 2);
    g_keep_w[i] = w, g_keep_h[i] = h;
  }
}

static void keep_restore(void) {
  for (int i = 0; i < PAGES; i++) {
    if (!g_keep[i]) continue;
    int w, h, pitch, is565;
    uint16_t *bb = gdi_screen_dib(i, &w, &h, &pitch, &is565);
    if (!bb || w != g_keep_w[i] || h != g_keep_h[i]) continue;
    for (int y = 0; y < h; y++) memcpy(bb + (ptrdiff_t)y * pitch, g_keep[i] + (size_t)y * w, (size_t)w * 2);
  }
}

static unsigned g_pre_t;   // dlg_pre kept the picture at this time

// The save-name dialog's caller draws its own strip before the entry opens: it keeps the picture first.
// task-107: the entry can open seconds later on a busy machine (a 1 s limit made begin() keep a picture with golf's old
// strip in it, and the strip showed behind the native box); 20 s still tells a stale mark from this call.
void dlg_pre(void) {
  if (!dlgf_enabled()) return;
  keep_save();
  hd_art_dlg_pre();
  g_pre_t = SDL_GetTicks() | 1;
}

void hud_clean_pages(void);   // hud_game.c
int rp_is_on(void);           // reports_game.c: a report page (task-73: the golfer profile) is up under this dialog

static int g_text_anchor = -1;
static char *g_entry_text;
static unsigned *g_entry_cursor, *g_entry_length;
static int g_entry_capacity;
unsigned GetKeyState(unsigned vk);
int dlg_entry_nav(int vk, int shift) {
  if (!g_on || g_kind != 1 || !g_entry_text || !mods_get_int("fixes.text_selection", 1)) return 0;
  return text_selection_key(g_entry_text, g_entry_cursor, g_entry_length,
                            &g_text_anchor, 0, vk, shift, (unsigned)g_entry_capacity);
}
int dlg_edit_key(char *text, unsigned *cursor, unsigned *length, int ch, int vk, int capacity) {
  if (!g_on || !mods_get_int("fixes.text_selection", 1) || capacity <= 0) return 0;
  if (g_entry_cursor) *cursor = *g_entry_cursor;
  return text_selection_key(text, cursor, length, &g_text_anchor, ch, vk,
                            (GetKeyState(0x10) & 0x8000) != 0, (unsigned)capacity);
}

static void begin(int kind) {
  if (g_log < 0) g_log = getenv("SIMGOLF_DIALOGS_LOG") != NULL;
  g_on = dlgf_enabled();
  g_kind = kind;
  g_text_anchor = -1;
  g_entry_text = NULL;
  g_inj = 0;
  if (g_log && g_on) fprintf(stderr, "dialogs: open %s\n", kind == 1 ? "entry" : "menu");
  if (g_on) {
    hd_ui_show_golf_ui(0);   // golf's own UI layer (and the old HUD under it) stays hidden
    int pre_saved = hd_art_dialog_presnap_fresh(SDL_GetTicks(), g_pre_t, g_kept);
    if (hd_art_dialog_use_presnap(kind, pre_saved)) hd_art_dlg_begin_pre();
    else hd_art_dlg_begin();
    hud_clean_pages();
    if (!pre_saved) keep_save();
    g_pre_t = 0;
  }
}

void dlg_begin(int kind) { ++g_modal_depth; begin(kind); }
int dlg_game_active(void) { return g_modal_depth > 0 || g_hint_open; }

static void copy_str(char *dst, int cap, const char *src) {
  int n = 0;
  while (n < cap - 1 && src && src[n]) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

// ------------------------------------------------------------------ popup menu (FUN_0046d6e0)

// The geometry the original uses for hit tests (see the loop): left, first row y, 0x18 per row, the OK disc.
int dlg_menu(int px, int py, int sel, int checks, unsigned mask, unsigned disabled, int left, int first_y, int n_items, int width, int height) {
  if (!g_on) return 0;
  hd_ui_show_golf_ui(0);
  DlgSnap *s = &g_snap;
  memset(s, 0, sizeof *s);
  s->magic = DLG_MAGIC;
  s->serial = ++g_serial;
  s->kind = checks ? DLG_CHECKS : DLG_MENU;
  const char *t = (const char *)(uintptr_t)TEXT_BUF;
  int item = 0;
  for (const char *p = t; *p && s->n_lines < DLG_LINES;) {
    const char *e = strchr(p, '\n');
    int len = e ? (int)(e - p) : (int)strlen(p);
    char tmp[DLG_TEXT];
    if (len > DLG_TEXT - 1) len = DLG_TEXT - 1;
    memcpy(tmp, p, (size_t)len);
    tmp[len] = 0;
    int is_item = tmp[0] == ' ';
    s->line[s->n_lines].item = is_item ? item++ : -1;
    copy_str(s->line[s->n_lines].text, DLG_TEXT, tmp);
    s->n_lines++;
    if (!e) break;
    p = e + 1;
  }
  s->n_items = item;
  s->sel = (checks || sel < 0 || sel >= item) ? -1 : sel;
  s->mask = checks ? mask : 0;
  s->disabled = disabled;
  dlgf_publish(s);
  if (g_log) {
    static int last_n = -1;
    if (last_n != s->serial / 600) {
      last_n = (int)(s->serial / 600);
      fprintf(stderr, "dialogs: menu checks %d items %d mask %x at left %d first_y %d width %d height %d center %d,%d\n", checks, item, mask, left,
              first_y, width, height, px, py);
    }
  }
  int cancel = 0;
  DlgAction a;
  while (!g_inj && dlgf_take_action(&a)) {
    if (a.kind == DLG_A_ITEM) {
      inj_start(left + 0x40, first_y + a.arg * 0x18 + 0xc);
    } else if (a.kind == DLG_A_OK && checks) {
      inj_start(px + width / 2 - 0x14, py + height - 0x14);
    } else if (a.kind == DLG_A_CANCEL || a.kind == DLG_A_OK) {
      cancel = 1;
    }
  }
  inj_step();
  keep_restore();
  hd_art_hide_dialog();   // task-88: golf's own popup frame, drawn again this frame
  return cancel;
}

// ------------------------------------------------------------------ text entry (FUN_0045b2c0)

// Returns 1 for Enter (OK), 2 for Esc (Cancel), 0 otherwise.
int dlg_entry(const char *prompt, char *text, unsigned *caret, unsigned *length, int max_chars) {
  if (!g_on) return 0;
  g_entry_text = text; g_entry_cursor = caret; g_entry_length = length; g_entry_capacity = max_chars;
  hd_ui_show_golf_ui(0);
  DlgSnap *s = &g_snap;
  memset(s, 0, sizeof *s);
  s->magic = DLG_MAGIC;
  s->serial = ++g_serial;
  s->kind = DLG_ENTRY;
  copy_str(s->prompt, sizeof s->prompt, prompt);
  copy_str(s->field, sizeof s->field, text);
  s->caret = (int)*caret;
  s->selection_anchor = g_text_anchor;
  s->max_chars = max_chars;
  dlgf_publish(s);
  int r = 0;
  DlgAction a;
  while (dlgf_take_action(&a)) {
    if (a.kind == DLG_A_OK) r = 1;
    else if (a.kind == DLG_A_CANCEL) r = 2;
  }
  keep_restore();
  hd_art_hide_dialog();   // task-107: golf's own strip (name box), drawn again this frame, like the menus above
  return r;
}

// ------------------------------------------------------------------ hints with a speaker (FUN_0040d320 + FUN_0045c0c0)

// gdi.c: the screen-sized DIB whose pixels contain address p
uint16_t *gdi_dib_at(uintptr_t p, int *w, int *h, int *pitch, int *is565);
extern int win_screen_w, win_screen_h;
int FUN_00473f60(void *sprite, int mask, int target, int x, int y, int clip);
int FUN_00473cb0(void *sprite, int target, int x, int y, int sx, int sy, int div, int clip);
int FUN_0045c200(int who, int x, int y, int z);   // a golfer's face
void FUN_00480c80(void *page);                    // flush the page's changes to the screen

// golf's back buffer as pixels (the page it draws into), NULL when it is not what we expect
static uint16_t *back_pixels(int *pitch, int *is565, int *w, int *h) {
  uint32_t wrap = (uint32_t)G32(0x4c1570);
  if (wrap < 0x10000) return NULL;
  uint32_t p = (uint32_t)G32(wrap + 4);
  if (p < 0x10000) return NULL;
  uint8_t *s = (uint8_t *)(uintptr_t)p;
  if (*(int32_t *)(s + 0x24) != 16 || *(int32_t *)(s + 0x40) < win_screen_w || !*(uint32_t *)(s + 0x4c0)) return NULL;
  uint32_t bits = *(uint32_t *)(s + 0x4c0);
  int dw, dh, dp;
  if (!gdi_dib_at(bits, &dw, &dh, &dp, is565)) return NULL;
  *pitch = *(int32_t *)(s + 0x40);
  *w = *(int32_t *)(s + 0x38);
  *h = *(int32_t *)(s + 0x3c);
  return (uint16_t *)(uintptr_t)bits;
}

static void rgb_of(uint16_t v, int is565, int *r, int *g, int *b) {
  if (is565) {
    *r = ((v >> 11) & 31) * 255 / 31;
    *g = ((v >> 5) & 63) * 255 / 63;
    *b = (v & 31) * 255 / 31;
  } else {
    *r = ((v >> 10) & 31) * 255 / 31;
    *g = ((v >> 5) & 31) * 255 / 31;
    *b = (v & 31) * 255 / 31;
  }
}

// Draws something into a scratch box of the back buffer over black and over white, turns the two into ARGB with
// alpha (a = 1 - (white - black)) and puts the pixels back (as frontend_game.c).
typedef void (*DrawFn)(int bx, int by, void *user);
static uint32_t *capture(int BW, int BH, DrawFn draw, void *user, int *ow, int *oh) {
  int pitch, is565, w, h;
  uint16_t *bb = back_pixels(&pitch, &is565, &w, &h);
  if (!bb || BW > w || BH > h) return NULL;
  size_t n = (size_t)BW * BH;
  uint16_t *saved = malloc(n * 2 * 3);
  if (!saved) return NULL;
  uint16_t *shot[2] = {saved + n, saved + 2 * n};
  for (int y = 0; y < BH; y++) memcpy(saved + (size_t)y * BW, bb + (ptrdiff_t)y * pitch, (size_t)BW * 2);
  uint16_t white = is565 ? 0xffff : 0x7fff;
  for (int pass = 0; pass < 2; pass++) {
    uint16_t fillc = pass ? white : 0;
    for (int y = 0; y < BH; y++)
      for (int x = 0; x < BW; x++) bb[(ptrdiff_t)y * pitch + x] = fillc;
    draw(0, 0, user);
    for (int y = 0; y < BH; y++) memcpy(shot[pass] + (size_t)y * BW, bb + (ptrdiff_t)y * pitch, (size_t)BW * 2);
  }
  for (int y = 0; y < BH; y++) memcpy(bb + (ptrdiff_t)y * pitch, saved + (size_t)y * BW, (size_t)BW * 2);
  int x0 = BW, y0 = BH, x1 = -1, y1 = -1;
  for (int y = 0; y < BH; y++)
    for (int x = 0; x < BW; x++)
      if (shot[0][y * BW + x] != 0 || shot[1][y * BW + x] != white) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
  if (x1 < x0) {
    free(saved);
    return NULL;
  }
  int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
  uint32_t *out = malloc((size_t)cw * ch * 4);
  if (out) {
    for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++) {
        int br, bg, bl, wr, wg, wb;
        rgb_of(shot[0][(y0 + y) * BW + x0 + x], is565, &br, &bg, &bl);
        rgb_of(shot[1][(y0 + y) * BW + x0 + x], is565, &wr, &wg, &wb);
        int d = ((wr - br) + (wg - bg) + (wb - bl)) / 3;
        int a = 255 - d;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        uint32_t px = 0;
        if (a > 0) {
          int r = br * 255 / a, g = bg * 255 / a, bb2 = bl * 255 / a;
          r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, bb2 = bb2 > 255 ? 255 : bb2;
          px = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bb2;
        }
        out[y * cw + x] = px;
      }
    *ow = cw;
    *oh = ch;
  }
  free(saved);
  return out;
}

// ------------------------------------------------------------------ normal face browser
// pickCharEditor00438390 is kept as the original-mode fallback. Native mode
// previews faces in its own catalog, then changes only appearance on acceptance.
unsigned dlg_done(unsigned v);
int FUN_0045ae70(void);
int FUN_0045ae30(void);
void FUN_0045c030(void);
void FUN_0045bf80(int ticks,int unused);

static void draw_picker_head(int bx,int by,void *user) {
  int code=(int)(intptr_t)user;
  FUN_00473cb0((void *)(uintptr_t)(0x562914+code*0x84),G32(0x4c1570),bx+8,by+8,1,1,1,0);
}
static DlgPixels picker_image(int male,int code) {
  DlgPixels p={0};
  if(code>=1000)p.px=portrait_face_image(code-1000,&p.w,&p.h);
  else {
    p.px=portrait_original_face_image(male,code,&p.w,&p.h);
    if(p.px)fprintf(stderr,"faces: original painted head %d male %d\n",code,male);
    else p.px=capture(160,160,draw_picker_head,(void *)(intptr_t)(code+male*72),&p.w,&p.h);
  }
  return p;
}
static void picker_label(char *text,int capacity,int code) {
  if(code>=1000)snprintf(text,capacity,"%s",roster_face_name(code-1000));
  else snprintf(text,capacity,code<20?"Original %d":"Imported %d",code<20?code+1:code-19);
}
static int picker_catalog(int male,int stock,int filter,int *entries) {
  int count=0;
  // Index19 is the stock authoring stencil ("place Normal face here"),
  // not a golfer face. Imported allocations start at20 and remain visible.
  if(filter!=1)for(int head=0;head<stock;head++)if(head!=19)entries[count++]=head;
  if(filter!=2)for(int i=0;i<roster_face_count(male);i++)entries[count++]=1000+roster_face_character(male,i);
  return count;
}
int dlg_face_picker(int profile,int male) {
  if(!dlgf_enabled() || !roster_profile_valid(profile) || (male!=0 && male!=1))return -2;
  const uint8_t *record=(const uint8_t *)(uintptr_t)(roster_address(0x4d6088)+profile*560);
  int stock=G32(0x59b76c+male*4);if(stock<0)stock=0;if(stock>72)stock=72;
  int entries[72+ROSTER_ADDITIONS],filter=0,page=0;
  int selected=roster_character(profile),count=picker_catalog(male,stock,filter,entries);
  selected=selected>=0?1000+selected:record[34];if(selected==19)selected=0;
  for(int i=0;i<count;i++)if(entries[i]==selected)page=i/DLG_FACE_TILES;
  int loaded_page=-1,loaded_filter=-1,loaded_preview=-2,preview_ready=0;
  int answer=-1,accept=0;
  DlgSnap snap={0};snap.magic=DLG_MAGIC;snap.kind=DLG_FACE;snap.face_male=male;
  dlg_begin(DLG_FACE);
  fprintf(stderr,"faces: open profile %d male %d originals %d new %d selected %d\n",profile,male,stock,roster_face_count(male),selected);
  for(;;) {
    count=picker_catalog(male,stock,filter,entries);
    int pages=(count+DLG_FACE_TILES-1)/DLG_FACE_TILES;if(pages<1)pages=1;
    if(page<0)page=0;if(page>=pages)page=pages-1;
    DlgPixels tiles[DLG_FACE_TILES]={{0}},preview={0};
    int refresh_page=page!=loaded_page || filter!=loaded_filter;
    int refresh_preview=selected!=loaded_preview;
    int page_ready=1;
    if(refresh_page) {
      snap.face_rows=count-page*DLG_FACE_TILES;
      if(snap.face_rows>DLG_FACE_TILES)snap.face_rows=DLG_FACE_TILES;
      for(int i=0;i<DLG_FACE_TILES;i++) {
        snap.face_code[i]=-1;snap.face_ready[i]=0;snap.face_name[i][0]=0;
        if(i>=snap.face_rows)continue;
        int code=entries[page*DLG_FACE_TILES+i];snap.face_code[i]=code;
        picker_label(snap.face_name[i],sizeof snap.face_name[i],code);
        tiles[i]=picker_image(male,code);snap.face_ready[i]=tiles[i].px!=NULL;
        if(!tiles[i].px)page_ready=0;
      }
      fprintf(stderr,"faces: page %d filter %d count %d rows %d\n",page,filter,count,snap.face_rows);
    }
    if(refresh_preview) {
      preview=picker_image(male,selected);preview_ready=preview.px!=NULL;
      picker_label(snap.face_preview,sizeof snap.face_preview,selected);
      fprintf(stderr,"faces: preview code %d ready %d [%s]\n",selected,preview_ready,snap.face_preview);
    }
    snap.serial=++g_serial;snap.face_filter=filter;snap.face_page=page;
    snap.face_total=count;snap.face_selected=selected;snap.has_art=preview_ready;
    if(dlgf_publish_faces(&snap,refresh_page?tiles:NULL,refresh_preview?&preview:NULL)) {
      if(refresh_page && page_ready){loaded_page=page;loaded_filter=filter;}
      if(refresh_preview && preview_ready)loaded_preview=selected;
    }
    for(int i=0;i<DLG_FACE_TILES;i++)free(tiles[i].px);free(preview.px);
    keep_restore();hd_art_hide_dialog();FUN_00480c80((void *)0x519a60);
    FUN_0045bf80(1,0);FUN_0045c030();
    DlgAction action;
    int cancel=0;
    while(dlgf_take_action(&action)) {
      if(action.kind==DLG_A_CANCEL)cancel=1;
      else if(action.kind==DLG_A_OK && preview_ready && selected==loaded_preview)accept=1;
      else if(action.kind==DLG_A_FACE_PAGE)page+=action.arg<0?-1:1;
      else if(action.kind==DLG_A_FACE_FILTER && action.arg>=0 && action.arg<=2){filter=action.arg;page=0;}
      else if(action.kind==DLG_A_ITEM)for(int i=0;i<count;i++)if(entries[i]==action.arg){selected=action.arg;break;}
    }
    if(FUN_0045ae70()) {
      int key=FUN_0045ae30(),index=-1;
      for(int i=0;i<count;i++)if(entries[i]==selected){index=i;break;}
      if(key==0x1b)cancel=1;
      else if(key==0x0d && preview_ready && selected==loaded_preview)accept=1;
      else if(key==0x21)page--;
      else if(key==0x22)page++;
      else if(key>=0x25 && key<=0x28 && count>0) {
        int step=key==0x25?-1:key==0x27?1:key==0x26?-4:4;
        index=index<0?0:index+step;if(index<0)index=0;if(index>=count)index=count-1;
        selected=entries[index];page=index/DLG_FACE_TILES;
      }
    }
    if(cancel){fprintf(stderr,"faces: cancelled profile %d\n",profile);break;}
    if(accept) {
      int character=selected>=1000?selected-1000:-1;
      if(roster_face_apply(profile,male,selected,character)) {
        answer=character>=0?-1:selected;
        fprintf(stderr,"faces: applied profile %d character %d stock %d\n",profile,character,character<0?selected:-1);
      }
      break;
    }
  }
  dlg_done((unsigned)-1);
  return answer;
}

// The speaker's picture as FUN_0040d320 draws it (golf's speaker code in 0x4c2e08), at the box's origin.
static void draw_speaker(int bx, int by, void *user) {
  int sp = (int)(intptr_t)user;
  int tgt = G32(0x4c1570);
  int x = bx + 60, y = by + 50;
  if (sp >= 0) {
    FUN_00473f60((void *)0x5a58c4, 0x58bb7c, tgt, x - 0x32, y - 0x28, 0);
    FUN_0045c200(sp, x - 0x32, y - 0x28, -1);
  } else if (sp < -5 && sp > -0xf) {
    FUN_00473cb0((void *)(uintptr_t)(0x53df84 + (sp * 0xb + 0x42) * -0x10), tgt, x + 8, y - 0x14, 1, 1, 1, 0);
  } else if (sp == -5) {
    FUN_00473f60((void *)0x5a58c4, 0x58bb7c, tgt, x - 0x28, y - 0x28, 0);
  } else if (sp == -3) {
    FUN_00473f60((void *)0x567a4c, 0x53dea4, tgt, x, y + 0x10, 0);
  } else if (sp == -2) {
    FUN_00473f60((void *)0x567a78, 0x53ded0, tgt, x, y + 0x10, 0);
  } else if (sp == -0x14) {
    FUN_00473f60((void *)0x567a20, 0x53de78, tgt, x, y + 0x10, 0);
  } else if (sp == -0x15) {
    FUN_00473f60((void *)0x567ad0, 0x53df28, tgt, x, y + 0x10, 0);
  } else if (sp == -0x16) {
    FUN_00473f60((void *)0x567aa4, 0x53defc, tgt, x, y + 0x10, 0);
  }
}

#define SPEAKER 0x4c2e08
static unsigned g_hint_t;   // ticks when FUN_0040d320 last drew a message panel
static int g_hint_on;

// Before FUN_0040d320 draws a message panel (its param_4 is -2): keep the picture to bring back under the native hint.
void dlg_hint_pre(void) {
  if (!dlgf_enabled()) return;
  if (g_on) return;   // a native dialog is already up
  unsigned now = SDL_GetTicks();
  if (g_hint_t && now - g_hint_t < 300 && g_kept) {
    g_hint_t = now | 1;
    return;
  }
  hud_clean_pages();
  keep_save();
  g_hint_t = now | 1;
}

void dlg_wait_begin(void) { g_hint_open = 1; }

// Once per iteration of FUN_0045c0c0, golf's wait for a click or a key. 1 when the native hint was closed.
void rp_analysis_tick(void);
int dlg_wait(void) {
  rp_analysis_tick();
  g_hint_open = 1;   // the original wait loop also owns input until dlg_hint_end
  if (!g_hint_on) {
    if (g_on || !g_hint_t || SDL_GetTicks() - g_hint_t > 400 || !g_kept || !dlgf_enabled()) return 0;
    if (g_log < 0) g_log = getenv("SIMGOLF_DIALOGS_LOG") != NULL;
    g_hint_on = 1;
    g_on = 1;
    g_inj = 0;
    hd_ui_show_golf_ui(0);
    g_snap.has_art = 0;
  }
  if (!g_snap.has_art) {
    int sp = G32(SPEAKER), w = 0, h = 0;
    uint32_t *px = capture(160, 140, draw_speaker, (void *)(intptr_t)sp, &w, &h);
    if (g_log && px) {
      int nz = 0;
      for (int i = 0; i < w * h; i++) nz += (px[i] >> 24) > 8;
      fprintf(stderr, "dialogs: portrait opaque pixels %d of %d, first %08x mid %08x", nz, w * h, px[0], px[(h / 2) * w + w / 2]);
      fputc(10, stderr);
    }
    g_snap.has_art = px && dlgf_publish_art(DLG_ART_PORTRAIT, px, w, h);
    free(px);
    if (g_log) fprintf(stderr, "dialogs: open hint speaker %d portrait %dx%d\n", sp, w, h);
  }
  DlgSnap *s = &g_snap;
  int has_art = s->has_art;
  memset(s, 0, sizeof *s);
  s->magic = DLG_MAGIC;
  s->serial = ++g_serial;
  s->kind = DLG_HINT;
  s->has_art = has_art;
  s->speaker = G32(SPEAKER);
  copy_str(s->text, sizeof s->text, (const char *)(uintptr_t)TEXT_BUF);
  dlgf_publish(s);
  DlgAction a;
  while (!g_inj && dlgf_take_action(&a))
    if (a.kind == DLG_A_OK || a.kind == DLG_A_CANCEL) inj_start(400, 300);
  inj_step();
  keep_restore();
  FUN_00480c80((void *)0x519a60);   // golf's wait loop flushes nothing: the picture without the original panel goes out now
  return 0;
}

// ------------------------------------------------------------------ golfer profile page (FUN_004385d0)

int FUN_0045ae70(void);   // a key is waiting
int FUN_0045ae30(void);   // takes the special key (VK code)

// The page closes only with its OK disc: Esc presses it (the disc's place is in golf's hit table, entry 0x13).
// Whatever the dialogs setting: the original look keeps this too.
void dlg_profile_tick(void) {
  if (!g_inj && FUN_0045ae70() && FUN_0045ae30() == 0x1b) {
    int x = *(volatile int16_t *)(uintptr_t)(0x4c7b38 + 4 * 0x13), y = *(volatile int16_t *)(uintptr_t)(0x4c7b38 + 4 * 0x13 + 2);
    inj_start(x, y);
  }
  inj_step();
}

// ------------------------------------------------------------------ hire an employee (FUN_00459400)

#define JOB_NAMES 0x4c1464   // eight pointers to the job names
#define JOB_WAGES 0x4c2e2c   // eight bytes: the weekly wage in hundreds of dollars
static const unsigned g_group_str[4] = {0x4d3720, 0x4d370c, 0x4d36fc, 0x4d36e8};   // "Greeters...", "Speed up play...", ...
// the rows' middles in the original's coordinates (the original reads the pointer's y only)
static const int g_job_y[8] = {180, 200, 252, 271, 323, 342, 394, 413};

int dlg_hire(int hover) {
  if (!g_on) return 0;
  hd_ui_show_golf_ui(0);
  DlgSnap *s = &g_snap;
  memset(s, 0, sizeof *s);
  s->magic = DLG_MAGIC;
  s->serial = ++g_serial;
  s->kind = DLG_HIRE;
  for (int g = 0; g < 4; g++) copy_str(s->group[g], sizeof s->group[g], (const char *)(uintptr_t)g_group_str[g]);
  for (int i = 0; i < 8; i++) {
    const char *nm = (const char *)(uintptr_t)(uint32_t)G32(JOB_NAMES + 4 * i);
    copy_str(s->job[i], sizeof s->job[i], nm);
    s->wage[i] = *(volatile int8_t *)(uintptr_t)(JOB_WAGES + i) * 100;
  }
  s->hover = hover;
  dlgf_publish(s);
  int cancel = 0;
  DlgAction a;
  while (!g_inj && dlgf_take_action(&a)) {
    if (a.kind == DLG_A_ITEM && a.arg >= 0 && a.arg < 8) inj_start(400, g_job_y[a.arg]);
    else if (a.kind == DLG_A_CANCEL) cancel = 1;
  }
  inj_step();
  keep_restore();
  hd_art_hide_dialog();   // the HD copy of the original hiring panel is replaced too
  return cancel;
}

static unsigned finish(unsigned v);
unsigned dlg_done(unsigned v);
void FUN_0045bf80(int n, int unused);
void FUN_0045c030(void);

// Original catalogue and return value of chooseFace (0x00438390). Only the
// presentation and input collection change; the profile applies the result.
static void draw_face_thumbnail(int x, int y, void *user) {
  int index = (int)(intptr_t)user;
  FUN_00473cb0((void *)(uintptr_t)(0x562914 + index * 0x84), G32(0x4c1570), x + 8, y + 8, 1, 1, 1, 0);
}

int dlg_face_native(int group) {
  if (!dlgf_enabled() || group < 0 || group > 1) return -2;
  int count = G32(0x59b76c + group * 4);
  if (count < 1 || count > 72) return -2;
  int visible_count = count - (count > 19); // slot 19 is the original authoring stencil
  int page = G32(0x5aa55c) / 10, loaded_page = -1, answer = -1, done = 0;
  int pages = (visible_count + 9) / 10;
  if (page < 0 || page >= pages) page = 0;
  dlg_begin(DLG_FACE);
  if (g_log) fprintf(stderr, "faces: native open group %d count %d\n", group, count);
  while (!done) {
    DlgSnap snap = {0};
    snap.magic = DLG_MAGIC; snap.serial = ++g_serial; snap.kind = DLG_FACE;
    snap.face_page = page; snap.face_count = visible_count;
    int published = 0;
    if (page != loaded_page) {
      DlgPixels tiles[DLG_FACE_TILES] = {{0}};
      int ready = 1;
      for (int i = 0; i < 10; i++) {
        int entry = page * 10 + i, head = entry + (entry >= 19), w = 0, h = 0;
        uint32_t *px = entry < visible_count ? capture(160, 160, draw_face_thumbnail, (void *)(intptr_t)(group * 72 + head), &w, &h) : NULL;
        tiles[i] = (DlgPixels){px,w,h};
        if (entry < visible_count && !px) ready = 0;
      }
      published = dlgf_publish_faces(&snap, tiles, NULL);
      for (int i = 0; i < DLG_FACE_TILES; i++) free(tiles[i].px);
      if (published && ready) {
        loaded_page = page;
        G32(0x5aa55c) = page * 10;
      }
      if (g_log) fprintf(stderr, "faces: native page %d\n", page);
    }
    if (page == loaded_page && !published) dlgf_publish(&snap);
    keep_restore();
    hd_art_hide_dialog();
    FUN_00480c80((void *)0x519a60);
    FUN_0045bf80(1, 0);
    FUN_0045c030();
    DlgAction action;
    while (dlgf_take_action(&action)) {
      if (action.kind == DLG_A_CANCEL) done = 1;
      else if (action.kind == DLG_A_ITEM && action.arg >= 0 && action.arg != 19 && action.arg < count) { answer = action.arg; done = 1; }
      else if (action.kind == DLG_A_FACE_PAGE) {
        int next = page + (action.arg < 0 ? -1 : 1);
        if (next >= 0 && next < pages) page = next;
      }
    }
    if (FUN_0045ae70()) {
      int key = FUN_0045ae30();
      if (key == 0x1b) done = 1;
      else if (key == 0x25 && page > 0) --page;
      else if (key == 0x27 && page + 1 < pages) ++page;
    }
  }
  dlg_done(0);
  if (g_log) fprintf(stderr, "faces: native result %d\n", answer);
  return answer;
}

// runScrollText (0x0046dea0) is shared by profile notices, file errors and
// roster messages. Its original parser, keyboard handling and return stay live.
static char g_document[2048];
static uint32_t g_document_epoch;
void dlg_document_begin(const char *text) {
  dlg_begin(DLG_DOCUMENT);
  ++g_document_epoch;
  copy_str(g_document, sizeof g_document, text);
  if (g_log && g_on) fprintf(stderr, "dialogs: open document %u bytes %u\n", g_document_epoch, (unsigned)strlen(g_document));
}

int FUN_00404970(int value);
uint32_t FUN_004657e0(int x, int y);
uint32_t FUN_00465840(int x, int y);
void dlg_document_frame(int selected, int input) {
  if (!g_on || g_kind != DLG_DOCUMENT) return;
  memset(&g_snap, 0, sizeof g_snap);
  g_snap.magic = DLG_MAGIC; g_snap.serial = ++g_serial; g_snap.kind = DLG_DOCUMENT;
  copy_str(g_snap.document, sizeof g_snap.document, g_document);
  g_snap.document_first = G32(0x83927c);
  g_snap.n_items = G32(0x838f7c);
  g_snap.disabled = (uint32_t)G32(0x839280);
  g_snap.sel = selected;
  g_snap.document_epoch = g_document_epoch;
  dlgf_publish(&g_snap);
  if (input) {
    DlgAction a;
    while (dlgf_take_action(&a)) {
      if (a.kind == DLG_A_ITEM) {
        int item = a.arg & 0xffff;
        if (item < 0 || item >= g_snap.n_items || item >= 32 || (g_snap.disabled & (1u << item))) continue;
        int x = FUN_00404970(G32(0x839260) + 12);
        int y = FUN_00404970(G32(0x839264) + 5 + (g_snap.document_first + item) * G32(0x4e3dac));
        sdl_motion(x, y); // the original also hit-tests the cursor when the button is released
        if (a.arg & 0x10000) FUN_00465840(x, y); else FUN_004657e0(x, y);
        if (g_log) fprintf(stderr, "dialogs: document original choice %d at %d,%d\n", item, x, y);
      } else if (a.kind == DLG_A_CANCEL || a.kind == DLG_A_OK) {
        SDL_Event e = {0}; e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_ESCAPE; SDL_PushEvent(&e);
        e.type = SDL_KEYUP; SDL_PushEvent(&e);
      }
    }
  }
  keep_restore();
  hd_art_hide_dialog();
}

void dlg_document_flush(void *page) {
  dlg_document_frame(-1, 0);
  FUN_00480c80(page);
}

int dlg_document_end(int result) {
  if (g_log && g_on) fprintf(stderr, "dialogs: document result %d\n", result);
  dlg_done((unsigned)result);
  return result;
}

void dlg_hint_end(void) {
  g_hint_open = 0;
  if (g_hint_on) finish(0);
}

unsigned dlg_done(unsigned v) {
  if (g_modal_depth > 0) --g_modal_depth;
  return finish(v);
}

static unsigned finish(unsigned v) {
  if (g_on) {
    if (g_log) fprintf(stderr, "dialogs: closed with %d\n", (int)v);
    dlgf_end();
    if (!rp_is_on()) hd_ui_show_golf_ui(1);   // a page under this dialog keeps golf's own UI hidden
    keep_free();
    g_on = 0;
    g_hint_on = 0;
    g_hint_t = 0;
    g_inj = 0;
  }
  g_kind = 0;
  return v;
}
