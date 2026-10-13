// Game bridge (native build, simgolf mod task-13): lets tools drive the live
// game. Off unless --bridge PORT or SIMGOLF_BRIDGE=1 (port from
// SIMGOLF_BRIDGE_PORT, default 0 = any free port; --bridge-portfile FILE gets
// the port actually used). docs/BRIDGE.md has the protocol and examples.
//
// Transport: TCP on 127.0.0.1 only, one JSON object per line each way. The
// sockets are non-blocking and polled from the game thread at every yield
// (win_yield), so every command runs between frames on the thread that owns
// the game's memory, window and input queue: no locks, no races.
//
// Clock: the game reads time only through port_now_ms() (GetTickCount,
// timeGetTime, QueryPerformanceCounter, SetTimer, timeSetEvent, Sleep). On
// the native build that is bridge_clock_ms(): real time scaled by `speed`,
// frozen while paused, and in turbo every wait the game asks for becomes a
// jump of the clock instead of a real sleep, so the game runs uncapped.
// Without the bridge the clock is plain real time.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>

#include <SDL.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


#include "port/grid.h"
#include "port/staff.h"
#include "port/host.h"
#include "port/runtime.h"
#include "mods/mods.h"

#define BRIDGE_VERSION 1
extern int win_screen_w, win_screen_h;   // win32.c: the game's canvas (800x600 unless canvas= is wider, task-3)
#define SCREEN_W win_screen_w
#define SCREEN_H win_screen_h

extern unsigned g_presents;   // gdi.c
int dlg_game_active(void);
int rp_game_active(void);
int opt_is_open(void);
int modsp_is_open(void);
int hud_on(void);             // ghidra_prelude.h: the new HUD replaces the old one this frame
int hd_native_shot(const char *name);   // hd_native.c (task-3): the presented picture, 0 when off
int hd_native_dump(const char *path);   // hd_native.c (task-68): the course of every presented frame to a file, NULL ends
uint16_t *win_screen(void);   // win32.h (not included: its enum clashes with wingdi.h)

// ------------------------------------------------------------------ clock

static double g_vbase, g_rbase;   // virtual time g_vbase at real time g_rbase
static double g_speed = 1.0;
static volatile double g_user_speed = 1.0;   // the Options page's game speed (card task-24), on top of the bridge's
static double speed_now(void) { return g_speed * g_user_speed; }
static int g_paused, g_turbo;
static double g_skipped;           // ms the turbo jumped instead of sleeping

static void rebase(void) {
  double r = host_now_ms();
  if (!g_paused) g_vbase += (r - g_rbase) * speed_now();
  g_rbase = r;
}

double bridge_clock_ms(void) {
  if (g_paused) return g_vbase;
  return g_vbase + (host_now_ms() - g_rbase) * speed_now();
}

// Game thread: the game's clock runs this many times faster than real time (Options > Gameplay > Game speed).
void bridge_set_game_speed(double s) {
  if (s < 0.25) s = 0.25;
  if (s > 20) s = 20;
  rebase();
  g_user_speed = s;
}

void bridge_wait(int ms) {
  if (ms <= 0) return;
  if (g_turbo) {
    g_vbase += ms;   // the game asked to wait: let the time pass at once
    g_skipped += ms;
    return;
  }
  int real = speed_now() > 0 ? (int)(ms / speed_now() + 0.5) : ms;
  host_sleep_ms(real);
}

// ------------------------------------------------------------------ mods key store

// The live key/value store is the mod platform's (port/src/mods, task-11):
// golf.ini < loaded mods < mods.ini (--mods FILE or SIMGOLF_MODS) < live
// changes from "mods_set". These wrappers keep the task-13 names working.
const char *bridge_mod(const char *key) { return mods_get(key); }
int bridge_mod_int(const char *key, int def) { return mods_get_int(key, def); }
unsigned bridge_mods_serial(void) { return mods_serial(); }

// ------------------------------------------------------------------ JSON in

typedef struct { const char *k, *v; int klen, vlen; char t; } JPair;   // t: s n b z a o
typedef struct { JPair p[24]; int n; } JObj;

static const char *skip_ws(const char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  return s;
}

// The end of the value starting at s (strings, nested arrays and objects).
static const char *value_end(const char *s) {
  if (*s == '"') {
    for (s++; *s && *s != '"'; s++)
      if (*s == '\\' && s[1]) s++;
    return *s ? s + 1 : NULL;
  }
  if (*s == '[' || *s == '{') {
    int depth = 0;
    for (; *s; s++) {
      if (*s == '"') {
        const char *e = value_end(s);
        if (!e) return NULL;
        s = e - 1;
      } else if (*s == '[' || *s == '{') depth++;
      else if (*s == ']' || *s == '}') {
        if (--depth == 0) return s + 1;
      }
    }
    return NULL;
  }
  while (*s && *s != ',' && *s != '}' && *s != ']' && !isspace((unsigned char)*s)) s++;
  return s;
}

static int jparse(const char *s, JObj *o) {
  o->n = 0;
  s = skip_ws(s);
  if (*s++ != '{') return 0;
  for (;;) {
    s = skip_ws(s);
    if (*s == '}') return 1;
    if (*s != '"') return 0;
    const char *ke = value_end(s);
    if (!ke) return 0;
    JPair *p = o->n < 24 ? &o->p[o->n++] : NULL;
    if (p) { p->k = s + 1; p->klen = (int)(ke - s - 2); }
    s = skip_ws(ke);
    if (*s++ != ':') return 0;
    s = skip_ws(s);
    const char *ve = value_end(s);
    if (!ve || ve == s) return 0;
    if (p) {
      p->t = *s == '"' ? 's' : *s == '[' ? 'a' : *s == '{' ? 'o' : *s == 't' || *s == 'f' ? 'b' : *s == 'n' ? 'z' : 'n';
      p->v = p->t == 's' ? s + 1 : s;
      p->vlen = (int)(ve - s) - (p->t == 's' ? 2 : 0);
    }
    s = skip_ws(ve);
    if (*s == ',') { s++; continue; }
    if (*s == '}') return 1;
    return 0;
  }
}

static const JPair *jget(const JObj *o, const char *key) {
  int n = (int)strlen(key);
  for (int i = 0; i < o->n; i++)
    if (o->p[i].klen == n && !memcmp(o->p[i].k, key, n)) return &o->p[i];
  return NULL;
}

static void unescape(const char *s, int n, char *out, int cap) {
  int j = 0;
  for (int i = 0; i < n && j < cap - 1; i++) {
    char c = s[i];
    if (c == '\\' && i + 1 < n) {
      c = s[++i];
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
      else if (c == 'r') c = '\r';
      else if (c == 'u' && i + 4 < n) {
        char hex[5] = {s[i + 1], s[i + 2], s[i + 3], s[i + 4], 0};
        unsigned u = (unsigned)strtoul(hex, NULL, 16);
        c = u < 0x80 ? (char)u : '?';
        i += 4;
      }
    }
    out[j++] = c;
  }
  out[j] = 0;
}

static int jstr(const JObj *o, const char *key, char *out, int cap) {
  const JPair *p = jget(o, key);
  if (!p) return 0;   // out keeps the caller's default
  if (p->t == 's') unescape(p->v, p->vlen, out, cap);
  else snprintf(out, cap, "%.*s", p->vlen, p->v);
  return 1;
}

// Numbers; strings like "0x4a2b10" or "123" count too (addresses).
static int num_at(const char *s, int n, double *out) {
  char buf[64];
  if (n <= 0 || n >= (int)sizeof buf) return 0;
  memcpy(buf, s, n);
  buf[n] = 0;
  char *e;
  if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) *out = (double)strtoull(buf, &e, 16);
  else if (buf[0] == '-' && buf[1] == '0' && (buf[2] == 'x' || buf[2] == 'X')) *out = -(double)strtoull(buf + 1, &e, 16);
  else *out = strtod(buf, &e);
  return e != buf;
}

static int jnum(const JObj *o, const char *key, double *out) {
  const JPair *p = jget(o, key);
  if (!p) return 0;
  if (p->t == 'b') { *out = p->v[0] == 't'; return 1; }
  return (p->t == 'n' || p->t == 's') && num_at(p->v, p->vlen, out);
}

static double jnum_or(const JObj *o, const char *key, double def) {
  double v;
  return jnum(o, key, &v) ? v : def;
}

// Numbers in an array value, up to max.
static int jnums(const JObj *o, const char *key, double *out, int max) {
  const JPair *p = jget(o, key);
  if (!p || p->t != 'a') return 0;
  const char *s = p->v + 1, *end = p->v + p->vlen - 1;
  int n = 0;
  while (s < end && n < max) {
    s = skip_ws(s);
    if (*s == ',') { s++; continue; }
    if (s >= end) break;
    const char *ve = value_end(s);
    if (!ve) break;
    const char *vs = s;
    int vl = (int)(ve - s);
    if (*s == '"') { vs++; vl -= 2; }
    if (num_at(vs, vl, &out[n])) n++;
    s = ve;
  }
  return n;
}

// ------------------------------------------------------------------ JSON out

typedef struct { char *p; int n, cap; } Buf;

static void bput(Buf *b, const char *s, int n) {
  if (b->n + n + 1 > b->cap) {
    int cap = b->cap ? b->cap : 4096;
    while (b->n + n + 1 > cap) cap *= 2;
    b->p = realloc(b->p, cap);
    b->cap = cap;
  }
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = 0;
}

static void bprintf(Buf *b, const char *fmt, ...) {
  char tmp[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n < (int)sizeof tmp) { bput(b, tmp, n); return; }
  char *big = malloc(n + 1);
  va_start(ap, fmt);
  vsnprintf(big, n + 1, fmt, ap);
  va_end(ap);
  bput(b, big, n);
  free(big);
}

static void bstr(Buf *b, const char *s, int n) {
  bput(b, "\"", 1);
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') { char e[2] = {'\\', (char)c}; bput(b, e, 2); }
    else if (c < 0x20 || c >= 0x7f) bprintf(b, "\\u%04x", c);
    else bput(b, (const char *)&c, 1);
  }
  bput(b, "\"", 1);
}

// ------------------------------------------------------------------ connections

#define MAXCONN 8
typedef struct {
  SOCKET s;
  char in[65536];
  int nin;
  Buf out;
} Conn;

static int g_enabled;
static SOCKET g_listen = INVALID_SOCKET;
static Conn g_conns[MAXCONN];
static int g_port;

// A reply that waits for something (input played, frames stepped).
typedef enum { WAIT_NONE, WAIT_INPUT, WAIT_STEP, WAIT_SHOT } WaitKind;
typedef struct { int conn; char id[64]; WaitKind kind; unsigned target, start; double deadline; char path[MAX_PATH]; } Pending;
static Pending g_pending[32];
static int g_npending;

static void reply(int ci, const char *id, Buf *body) {
  if (ci < 0 || g_conns[ci].s == INVALID_SOCKET) return;
  Buf *o = &g_conns[ci].out;
  bput(o, "{\"id\":", 6);
  if (id && *id) bput(o, id, (int)strlen(id));
  else bput(o, "null", 4);
  if (body && body->n) bput(o, body->p, body->n);
  bput(o, "}\n", 2);
}

static void reply_err(int ci, const char *id, const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  Buf b = {0};
  bput(&b, ",\"ok\":false,\"error\":", 20);
  bstr(&b, msg, (int)strlen(msg));
  reply(ci, id, &b);
  free(b.p);
}

static void flush_conn(Conn *c) {
  while (c->out.n > 0) {
    int k = send(c->s, c->out.p, c->out.n, 0);
    if (k <= 0) {
      if (k < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return;
      closesocket(c->s);
      c->s = INVALID_SOCKET;
      c->out.n = 0;
      return;
    }
    memmove(c->out.p, c->out.p + k, c->out.n - k);
    c->out.n -= k;
  }
}

// ------------------------------------------------------------------ input queue

// Mouse and keyboard go in as SDL events (the path real input takes, see
// win32.c pump_sdl), spaced in real time like the harness: the game sees a
// hover frame before a press, as with a real mouse. The real cursor never moves.
//
// Timing follows the game, not only the wall clock (task-33). The game reads
// the mouse and keyboard once per frame, so under load (frames far apart)
// two events pushed 50 ms apart can land in the same frame: a click's press
// and release collapse, and the keyboard's single-slot mailbox (0x822b9c:
// FUN_00465750 writes it, FUN_0045ae50 reads and clears it, FUN_0045ae70
// polls it and clears it once more after its next message pump) is
// overwritten or cleared, which dropped typed letters and Backspaces.
// So an act sets a gate for the
// next one: `frames` presented frames, and for keys and text an
// acknowledgement (every SDL event delivered, the game's WM_KEY/WM_CHAR
// messages taken, the mailbox empty). Both gates time out, and are skipped
// on a screen that presents no frames or while the game is paused.
typedef struct { char op; int x, y, button; char text[64]; int after_ms; int frames; int ack; } Act;
#define ACT_CAP 4096
#define MAX_PATH_PTS 600          // waypoints in a drag path
#define KEY_MAILBOX 0x822b9c
#define KEY_LATCH 0x4d3834
#define GATE_FRAME_STALL_MS 500   // stop waiting for a present
#define GATE_ACK_STALL_MS 1500    // stop waiting for the key mailbox
#define NO_PRESENT_MS 700         // no present for this long: a screen that presents no frames
static Act g_acts[ACT_CAP];
static int g_nacts, g_acthead;
static double g_act_next;
static unsigned g_acts_done, g_acts_queued;
static unsigned g_gate_presents;   // presents needed before the next act
static int g_gate_ack;             // a key or text act ran: wait for its delivery
static double g_gate_since;
static double g_last_present_ms;
static double g_fps;               // presents per second over the last second (set in bridge_on_present)
int win_key_msgs_pending(void);   // win32.c: WM_KEYDOWN/KEYUP/CHAR the game has not taken yet

static int act_room(int n) { return ACT_CAP - g_nacts >= n; }

static void push_act_g(char op, int x, int y, int button, const char *text, int after_ms, int frames, int ack) {
  if (g_nacts == ACT_CAP) return;   // callers check act_room first
  Act *a = &g_acts[(g_acthead + g_nacts++) % ACT_CAP];
  memset(a, 0, sizeof *a);
  a->op = op; a->x = x; a->y = y; a->button = button; a->after_ms = after_ms; a->frames = frames; a->ack = ack;
  if (text) snprintf(a->text, sizeof a->text, "%s", text);
  g_acts_queued++;
}

// Default gates: mouse acts wait one presented frame (a hover frame before
// the press, the press held across a frame, a frame after the release); key
// and text acts wait for delivery.
static void push_act(char op, int x, int y, int button, const char *text, int after_ms) {
  int frames = (op == 'm' || op == 'd' || op == 'u' || op == 'M' || op == 'W' || op == 'D' || op == 'U' || op == 'K' || op == 'L') ? 1 : 0;
  int ack = (op == 'k' || op == 't' || op == 'K' || op == 'L');
  push_act_g(op, x, y, button, text, after_ms, frames, ack);
}

static void sdl_motion(int x, int y) {
  SDL_Event e = {0};
  e.type = SDL_MOUSEMOTION;
  e.motion.x = x;
  e.motion.y = y;
  SDL_PushEvent(&e);
}

// Window-pixel events (wmove, wclick): they carry the window's id, as SDL's own
// mouse events do, so the presenter maps them from window pixels to the frame
// and native UI layers (the Frosted Mint HUD, the golfer card) see them as a
// real mouse's. The plain move and click above are scripted input: the game's.
static Uint32 window_id(void) {
  for (Uint32 i = 1; i < 16; i++)
    if (SDL_GetWindowFromID(i)) return i;
  return 0;
}

static void sdl_wmotion(int x, int y) {
  SDL_Event e = {0};
  e.type = SDL_MOUSEMOTION;
  e.motion.windowID = window_id();
  e.motion.x = x;
  e.motion.y = y;
  SDL_PushEvent(&e);
}

static void sdl_wbutton(int x, int y, int right, int down) {
  SDL_Event e = {0};
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  e.button.windowID = window_id();
  e.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.clicks = 1;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}

static void sdl_wwheel(int n) {   // task-94: a mouse wheel notch count over the window pointer (button field carries n)
  SDL_Event e = {0};
  e.type = SDL_MOUSEWHEEL;
  e.wheel.windowID = window_id();
  e.wheel.y = n;
  SDL_PushEvent(&e);
}

static void sdl_button(int x, int y, int right, int down) {
  SDL_Event e = {0};
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  e.button.button = right == 2 ? SDL_BUTTON_MIDDLE : right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;   // 2: middle (camera pan)
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.clicks = 1;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}

static int g_key_mods;   // Shift, Ctrl and Alt the script holds (the key events it pushes carry them, as real ones do)

static void sdl_key(const char *name, int down) {
  SDL_Event e = {0};
  SDL_Keycode k = SDL_GetKeyFromName(name);
  int bit = k == SDLK_LSHIFT ? KMOD_LSHIFT : k == SDLK_RSHIFT ? KMOD_RSHIFT : k == SDLK_LCTRL ? KMOD_LCTRL :
            k == SDLK_RCTRL ? KMOD_RCTRL : k == SDLK_LALT ? KMOD_LALT : k == SDLK_RALT ? KMOD_RALT : 0;
  if (bit) g_key_mods = down ? (g_key_mods | bit) : (g_key_mods & ~bit);
  e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.key.keysym.sym = k;
  e.key.keysym.mod = (Uint16)g_key_mods;
  e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
  SDL_PushEvent(&e);
}

static void sdl_text(const char *s) {
  SDL_Event e = {0};
  e.type = SDL_TEXTINPUT;
  snprintf(e.text.text, sizeof e.text.text, "%s", s);
  SDL_PushEvent(&e);
}

static int mem_ok(uintptr_t a, size_t n, int write);

// The delivery state of the last key or text act: 0 = delivered and taken,
// 1 = still on its way (SDL queue, the game's message queue, or the mailbox
// not read yet), 2 = taken, but the key poll's latch is still set.
static int key_state(void) {
  if (SDL_PeepEvents(NULL, 0, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) return 1;
  if (win_key_msgs_pending() > 0) return 1;
  if (mem_ok(KEY_MAILBOX, 4, 0) && *(volatile int *)KEY_MAILBOX != 0) return 1;
  // The key poll (FUN_0045ae70) latches a key it found (0x4d3834 = 1); its
  // next call pumps messages and then ZEROES the mailbox again, so a key that
  // arrives in that pump is lost. Only when the latch is back to 0 is the next
  // key safe. (It stays 1 outside text entry; that wait is kept short.)
  if (mem_ok(KEY_LATCH, 4, 0) && *(volatile int *)KEY_LATCH != 0) return 2;
  return 0;
}

// True when the next act may run (its time is up and the gate is open).
static int gate_open(double now) {
  if (g_paused) return 1;   // the game is stopped; events just queue
  double waited = now - g_gate_since;
  int presenting = now - g_last_present_ms < NO_PRESENT_MS;
  // SIMGOLF_GATE_STALL_MS: test hook (task-47), shortens the frame wait to mimic a game
  // that is late with its frames; the click_tiles read-back must still paint every tile.
  static double stall_ms = -1;
  if (stall_ms < 0) { const char *e = getenv("SIMGOLF_GATE_STALL_MS"); stall_ms = e ? atof(e) : GATE_FRAME_STALL_MS; }
  if (g_presents < g_gate_presents && waited < stall_ms && presenting) return 0;
  if (g_gate_ack) {
    // Nobody may read the mailbox on this screen: stop waiting after a few
    // frames (the latch alone, which only text entry clears, a little sooner).
    double frame = presenting && g_fps > 0 ? 1000.0 / g_fps : 100;
    double stall = !presenting ? 300 : frame * 5;
    double stall_latch = !presenting ? 200 : frame * 4;
    if (stall < 400) stall = 400;
    if (stall_latch < 300) stall_latch = 300;
    if (stall > GATE_ACK_STALL_MS) stall = GATE_ACK_STALL_MS;
    if (stall_latch > GATE_ACK_STALL_MS) stall_latch = GATE_ACK_STALL_MS;
    int ks = key_state();
    if (ks == 1 && waited < stall) return 0;
    if (ks == 2 && waited < stall_latch) return 0;
  }
  return 1;
}

static void run_acts(void) {
  double now = host_now_ms();
  while (g_nacts && now >= g_act_next && gate_open(now)) {
    Act *a = &g_acts[g_acthead];
    switch (a->op) {
      case 'm': sdl_motion(a->x, a->y); break;
      case 'M': sdl_wmotion(a->x, a->y); break;
      case 'W': sdl_wwheel(a->button); break;
      case 'D': sdl_wbutton(a->x, a->y, a->button, 1); break;
      case 'U': sdl_wbutton(a->x, a->y, a->button, 0); break;
      case 'd': sdl_button(a->x, a->y, a->button, 1); break;
      case 'u': sdl_button(a->x, a->y, a->button, 0); break;
      case 'k': sdl_key(a->text, 1); sdl_key(a->text, 0); break;
      case 'K': sdl_key(a->text, 1); break;
      case 'L': sdl_key(a->text, 0); break;
      case 't': sdl_text(a->text); break;
    }
    g_act_next = now + a->after_ms;
    g_gate_presents = g_presents + (unsigned)a->frames;
    g_gate_ack = a->ack;
    g_gate_since = now;
    g_acthead = (g_acthead + 1) % ACT_CAP;
    g_nacts--;
    g_acts_done++;
    now = host_now_ms();
  }
}

// ------------------------------------------------------------------ memory

static int mem_ok(uintptr_t a, size_t n, int write) {
  if (a < 0x10000) return 0;
  while (n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)a, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return 0;
    DWORD p = mi.Protect & 0xff;
    if (write && !(p & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
      return 0;
    uintptr_t end = (uintptr_t)mi.BaseAddress + mi.RegionSize;
    if (end - a >= n) return 1;
    n -= end - a;
    a = end;
  }
  return 1;
}

typedef enum { T_I8, T_U8, T_I16, T_U16, T_I32, T_U32, T_F32, T_F64, T_STR, T_BYTES, T_BAD } Type;

static Type parse_type(const char *s) {
  static const char *names[] = {"i8", "u8", "i16", "u16", "i32", "u32", "f32", "f64", "str", "bytes"};
  for (int i = 0; i < 10; i++)
    if (!_stricmp(s, names[i])) return (Type)i;
  if (!_stricmp(s, "int32") || !_stricmp(s, "int")) return T_I32;
  if (!_stricmp(s, "uint32")) return T_U32;
  if (!_stricmp(s, "int16")) return T_I16;
  if (!_stricmp(s, "uint16")) return T_U16;
  if (!_stricmp(s, "uint8") || !_stricmp(s, "byte")) return T_U8;
  if (!_stricmp(s, "int8")) return T_I8;
  if (!_stricmp(s, "float")) return T_F32;
  if (!_stricmp(s, "double")) return T_F64;
  if (!_stricmp(s, "string") || !_stricmp(s, "char")) return T_STR;
  return T_BAD;
}

static int type_size(Type t) {
  static const int sz[] = {1, 1, 2, 2, 4, 4, 4, 8, 1, 1};
  return t < T_BAD ? sz[t] : 0;
}

static double load_num(uintptr_t a, Type t) {
  switch (t) {
    case T_I8: return *(int8_t *)a;
    case T_U8: return *(uint8_t *)a;
    case T_I16: return *(int16_t *)a;
    case T_U16: return *(uint16_t *)a;
    case T_I32: return *(int32_t *)a;
    case T_U32: return *(uint32_t *)a;
    case T_F32: return *(float *)a;
    case T_F64: return *(double *)a;
    default: return 0;
  }
}

static void store_num(uintptr_t a, Type t, double v) {
  switch (t) {
    case T_I8: case T_U8: *(uint8_t *)a = (uint8_t)(int64_t)v; break;
    case T_I16: case T_U16: *(uint16_t *)a = (uint16_t)(int64_t)v; break;
    case T_I32: case T_U32: *(uint32_t *)a = (uint32_t)(int64_t)v; break;
    case T_F32: *(float *)a = (float)v; break;
    case T_F64: *(double *)a = v; break;
    default: break;
  }
}

static void put_num(Buf *b, double v, Type t) {
  if (t == T_F32 || t == T_F64) bprintf(b, "%.9g", v);
  else bprintf(b, "%.0f", v);
}

// ------------------------------------------------------------------ commands

static unsigned g_ticks;
static unsigned g_step_left;       // presents (or loops) to run before pausing again
static int g_stepping;
static uint32_t g_pause_addr;      // pause_at (task-19): pause at the first present where
static int32_t g_pause_value;      // the i32 at this address equals the value (0: off)
static int g_step_loops;           // 1: counting game-loop iterations (the screen presents no frames)
static int g_step_skip;            // do not count the loop that started the step
static double g_step_since;        // when the step started, or its last present
static const char *g_step_unit = "frames";

static void set_paused(int p) {
  rebase();
  if (g_paused && !p) g_last_present_ms = host_now_ms();   // no frames while paused is not "a screen without frames"
  g_paused = p;
}

static void state_fields(Buf *b) {
  bprintf(b, ",\"paused\":%s,\"turbo\":%s,\"speed\":%g,\"clock_ms\":%.1f,\"real_ms\":%.1f,\"frames\":%u,\"loops\":%u",
          g_paused ? "true" : "false", g_turbo ? "true" : "false", g_speed, bridge_clock_ms(), host_now_ms(),
          g_presents, g_ticks);
  // The canvas the bridge's x,y are in, and where the original bottom-left
  // controls sit on it: HD_UI_DY (canvas height - 600) below their 800x600
  // place (task-3 B4). hud: the new HUD replaces the old one (scripted clicks
  // still reach the old controls, hud_game.c).
  bprintf(b, ",\"canvas_w\":%d,\"canvas_h\":%d,\"ui_dy\":%d,\"hud\":%s,\"map_w\":%d,\"map_h\":%d", SCREEN_W, SCREEN_H, SCREEN_H - 600,
          hud_on() ? "true" : "false", g_map_w, g_map_h);
  bprintf(b, ",\"modal\":%s", (dlg_game_active() || rp_game_active() || opt_is_open() || modsp_is_open()) ? "true" : "false");
}

int harness_write_bmp(const char *path);   // harness.c
int hd_native_shot(const char *name);      // hd_native.c (0: presentation off)

static void cmd_peek(int ci, const char *id, const JObj *o, Buf *b) {
  double addr;
  char tname[16] = "i32";
  if (!jnum(o, "addr", &addr)) { reply_err(ci, id, "peek: addr missing"); return; }
  jstr(o, "type", tname, sizeof tname);
  Type t = parse_type(tname);
  if (t == T_BAD) { reply_err(ci, id, "unknown type %s", tname); return; }
  int count = (int)jnum_or(o, "count", 1);
  int len = (int)jnum_or(o, "len", t == T_STR ? 64 : 16);
  if (count < 1) count = 1;
  if (count > 4096) count = 4096;
  if (len < 1) len = 1;
  if (len > 65536) len = 65536;
  uintptr_t a = staff_translate(grid_translate((uintptr_t)addr));   // map and legacy staff addresses
  if (t == T_STR || t == T_BYTES) {
    if (!mem_ok(a, len, 0)) {
      // shorter readable prefix for strings near the end of a region
      while (len > 1 && !mem_ok(a, len, 0)) len /= 2;
      if (!mem_ok(a, len, 0)) { reply_err(ci, id, "address %08x is not readable", (unsigned)a); return; }
    }
    bprintf(b, ",\"ok\":true,\"addr\":%u,\"type\":\"%s\",\"value\":", (unsigned)a, tname);
    if (t == T_STR) {
      int n = 0;
      while (n < len && ((char *)a)[n]) n++;
      bstr(b, (const char *)a, n);
    } else {
      bput(b, "\"", 1);
      for (int i = 0; i < len; i++) bprintf(b, "%02x", ((uint8_t *)a)[i]);
      bput(b, "\"", 1);
    }
    return;
  }
  int sz = type_size(t);
  int stride = (int)jnum_or(o, "stride", sz);
  if (!mem_ok(a, (size_t)stride * (count - 1) + sz, 0)) { reply_err(ci, id, "address %08x is not readable", (unsigned)a); return; }
  bprintf(b, ",\"ok\":true,\"addr\":%u,\"type\":\"%s\",\"value\":", (unsigned)a, tname);
  if (count == 1 && !jget(o, "count")) { put_num(b, load_num(a, t), t); return; }
  bput(b, "[", 1);
  for (int i = 0; i < count; i++) {
    if (i) bput(b, ",", 1);
    put_num(b, load_num(a + (uintptr_t)i * stride, t), t);
  }
  bput(b, "]", 1);
}

static void cmd_poke(int ci, const char *id, const JObj *o, Buf *b) {
  double addr, v;
  char tname[16] = "i32";
  if (!jnum(o, "addr", &addr)) { reply_err(ci, id, "poke: addr missing"); return; }
  jstr(o, "type", tname, sizeof tname);
  Type t = parse_type(tname);
  uintptr_t a = staff_translate(grid_translate((uintptr_t)addr));   // map and legacy staff addresses
  if (t == T_BAD) { reply_err(ci, id, "unknown type %s", tname); return; }
  if (t == T_STR || t == T_BYTES) {
    static char s[4096];
    if (!jstr(o, "value", s, sizeof s)) { reply_err(ci, id, "poke: value missing"); return; }
    int n;
    if (t == T_STR) {
      n = (int)strlen(s) + 1;
      int max = (int)jnum_or(o, "len", n);
      if (n > max) { reply_err(ci, id, "string longer than len %d", max); return; }
    } else {
      n = (int)strlen(s) / 2;
      for (int i = 0; i < n; i++) {
        unsigned x;
        sscanf(s + 2 * i, "%2x", &x);
        s[i] = (char)x;
      }
    }
    if (!mem_ok(a, n, 1)) { reply_err(ci, id, "address %08x is not writable", (unsigned)a); return; }
    memcpy((void *)a, s, n);
    bprintf(b, ",\"ok\":true,\"addr\":%u,\"bytes\":%d", (unsigned)a, n);
    return;
  }
  if (!jnum(o, "value", &v)) { reply_err(ci, id, "poke: value missing"); return; }
  if (!mem_ok(a, type_size(t), 1)) { reply_err(ci, id, "address %08x is not writable", (unsigned)a); return; }
  double before = load_num(a, t);
  store_num(a, t, v);
  bprintf(b, ",\"ok\":true,\"addr\":%u,\"before\":", (unsigned)a);
  put_num(b, before, t);
  bput(b, ",\"value\":", 9);
  put_num(b, load_num(a, t), t);
}

// Every address holding value (as type); "in" narrows an earlier result.
static void cmd_find(int ci, const char *id, const JObj *o, Buf *b) {
  double v;
  char tname[16] = "i32";
  if (!jnum(o, "value", &v)) { reply_err(ci, id, "find: value missing"); return; }
  jstr(o, "type", tname, sizeof tname);
  Type t = parse_type(tname);
  if (t == T_BAD || t == T_STR || t == T_BYTES) { reply_err(ci, id, "find: numeric type needed"); return; }
  int sz = type_size(t);
  int align = (int)jnum_or(o, "align", sz > 4 ? 4 : sz);
  int max = (int)jnum_or(o, "max", 200);
  double tol = jnum_or(o, "tol", (t == T_F32 || t == T_F64) ? 0.001 : 0);
  uintptr_t lo = (uintptr_t)jnum_or(o, "lo", 0x10000), hi = (uintptr_t)jnum_or(o, "hi", 0x7fff0000u);
  static double cand[8192];
  int ncand = jnums(o, "in", cand, 8192);
  uint32_t *hits = malloc(sizeof(uint32_t) * (max > 0 ? max : 1));
  int nhits = 0, total = 0;
  if (jget(o, "in")) {
    for (int i = 0; i < ncand; i++) {
      uintptr_t a = (uintptr_t)cand[i];
      if (!mem_ok(a, sz, 0)) continue;
      double x = load_num(a, t);
      if (x >= v - tol && x <= v + tol) {
        if (nhits < max) hits[nhits++] = (uint32_t)a;
        total++;
      }
    }
  } else {
    // Our own exe, SDL and the system DLLs are images outside the game's
    // ranges: skip images other than golf.exe/jgld.dll to keep results useful.
    for (uintptr_t p = lo; p < hi;) {
      MEMORY_BASIC_INFORMATION mi;
      if (!VirtualQuery((void *)p, &mi, sizeof mi)) break;
      uintptr_t base = (uintptr_t)mi.BaseAddress, end = base + mi.RegionSize;
      int game_image = base >= 0x00400000u && base < 0x00900000u ? 1 : base >= 0x10000000u && base < 0x10200000u;
      int readable = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                     (mi.Type != MEM_IMAGE || game_image);
      if (readable) {
        uintptr_t s = base < lo ? lo : base, e = end > hi ? hi : end;
        s = (s + align - 1) & ~(uintptr_t)(align - 1);
        for (uintptr_t a = s; a + sz <= e; a += align) {
          double x = load_num(a, t);
          if (x >= v - tol && x <= v + tol) {
            if (nhits < max) hits[nhits++] = (uint32_t)a;
            total++;
          }
        }
      }
      if (end <= p) break;
      p = end;
    }
  }
  bprintf(b, ",\"ok\":true,\"total\":%d,\"addrs\":[", total);
  for (int i = 0; i < nhits; i++) bprintf(b, "%s%u", i ? "," : "", hits[i]);
  bput(b, "]", 1);
  free(hits);
}

// call with "async":1: the reply goes out at once and the function runs after the bridge returns to the game's loop, so a
// modal screen (its own loop) can be opened without holding the bridge (tests of the task-73 pages)
static IcallFn g_async_fn;
static uint32_t g_async_a[16];

static void cmd_call(int ci, const char *id, const JObj *o, Buf *b) {
  double addr, args[15] = {0}, async = 0;
  jnum(o, "async", &async);
  if (!jnum(o, "addr", &addr)) { reply_err(ci, id, "call: addr missing"); return; }
  int n = jnums(o, "args", args, 15);
  uint32_t flags = 0;
  IcallFn fn = icall_find((uint32_t)addr, &flags);
  if (!fn) { reply_err(ci, id, "no game function at %08x", (unsigned)addr); return; }
  uint32_t a[16] = {0};
  for (int i = 0; i < n; i++) a[i] = (uint32_t)(int64_t)args[i];
  fprintf(stderr, "bridge: call %08x with %d args%s\n", (unsigned)addr, n, async ? " (async)" : "");
  if (async && !g_async_fn) {
    g_async_fn = fn;
    memcpy(g_async_a, a, sizeof a);
    bprintf(b, ",\"ok\":true,\"async\":true");
    return;
  }
  uint32_t r = fn(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
  bprintf(b, ",\"ok\":true,\"result\":%u,\"fret\":%.9g,\"flags\":%u", r, g_fret, flags);
}

static void cmd_perf(Buf *b) {
  PROCESS_MEMORY_COUNTERS pmc = {sizeof pmc};
  K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
  bput(b, ",\"ok\":true", 10);
  state_fields(b);
  bprintf(b, ",\"fps\":%.1f,\"ticks\":%u,\"turbo_skipped_ms\":%.0f,\"working_set_mb\":%.1f,\"peak_working_set_mb\":%.1f,"
             "\"pagefile_mb\":%.1f,\"input_queued\":%d,\"pid\":%lu",
          g_fps, g_ticks, g_skipped, pmc.WorkingSetSize / 1048576.0, pmc.PeakWorkingSetSize / 1048576.0,
          pmc.PagefileUsage / 1048576.0, g_nacts, GetCurrentProcessId());
}

static Pending *add_pending(int ci, const char *id, WaitKind k, unsigned target) {
  if (g_npending == 32) { reply_err(ci, id, "too many waiting requests"); return NULL; }
  Pending *p = &g_pending[g_npending++];
  p->conn = ci;
  snprintf(p->id, sizeof p->id, "%s", id);
  p->kind = k;
  p->target = target;
  p->start = g_presents;
  p->deadline = 0;
  p->path[0] = 0;
  return p;
}

// Writes the screenshot reply. fresh: the wait for new frames ended normally.
static void reply_shot(int ci, const char *id, const char *path, int fresh, unsigned waited) {
  Buf b = {0};
  if (!win_screen()) { reply_err(ci, id, "no frame yet"); return; }
  if (!harness_write_bmp(path)) { reply_err(ci, id, "cannot write %s", path); return; }
  bput(&b, ",\"ok\":true,\"path\":", 18);
  bstr(&b, path, (int)strlen(path));
  bprintf(&b, ",\"width\":%d,\"height\":%d,\"fresh\":%s,\"presents_waited\":%u", SCREEN_W, SCREEN_H, fresh ? "true" : "false", waited);
  state_fields(&b);
  reply(ci, id, &b);
  free(b.p);
}

static void run_cmd(int ci, const char *line) {
  JObj o;
  char id[64] = "", cmd[32] = "";
  if (!jparse(line, &o)) { reply_err(ci, NULL, "bad JSON"); return; }
  const JPair *idp = jget(&o, "id");
  if (idp) snprintf(id, sizeof id, "%.*s", idp->vlen + (idp->t == 's' ? 2 : 0), idp->t == 's' ? idp->v - 1 : idp->v);
  jstr(&o, "cmd", cmd, sizeof cmd);
  Buf b = {0};
  if (!strcmp(cmd, "ping")) {
    bprintf(&b, ",\"ok\":true,\"version\":%d,\"pid\":%lu,\"port\":%d,\"game_dir\":", BRIDGE_VERSION, GetCurrentProcessId(), g_port);
    bstr(&b, host_game_dir(), (int)strlen(host_game_dir()));
    state_fields(&b);
  } else if (!strcmp(cmd, "staff")) {
    bprintf(&b, ",\"ok\":true,\"base\":%u,\"stride\":%d,\"capacity\":%d,\"hire_max\":%d,\"paid\":%d",
      (unsigned)staff_base(), STAFF_STRIDE, STAFF_CAPACITY, staff_max(), staff_paid_count());
  } else if (!strcmp(cmd, "pause")) {
    set_paused(1);
    g_stepping = 0;
    bput(&b, ",\"ok\":true", 10);
    state_fields(&b);
  } else if (!strcmp(cmd, "resume")) {
    set_paused(0);
    g_stepping = 0;
    bput(&b, ",\"ok\":true", 10);
    state_fields(&b);
  } else if (!strcmp(cmd, "step")) {
    int frames = (int)jnum_or(&o, "frames", 1);
    if (frames < 1) frames = 1;
    // Frames are presented frames. A screen that presents none (a report,
    // the routing map, an award) never finishes that count, so there the
    // step counts game-loop iterations (win_yield calls) instead: at once if
    // nothing was presented for a while, or after NO_PRESENT_MS of waiting.
    double nowms = host_now_ms();
    g_step_left = frames;
    g_stepping = 1;
    g_step_skip = 1;
    g_step_since = nowms;
    set_paused(0);   // (from a pause this counts as a present just now)
    g_step_loops = nowms - g_last_present_ms > NO_PRESENT_MS;
    g_step_unit = g_step_loops ? "loops" : "frames";
    add_pending(ci, id, WAIT_STEP, 0);
    return;
  } else if (!strcmp(cmd, "pause_at")) {
    // Frame-exact pause for A/B runs: the game pauses itself at the first
    // present where the i32 at addr equals value (e.g. a tick after a load;
    // equality, because screens such as the load list's preview write other
    // values there first).
    double a, v;
    if (!jnum(&o, "addr", &a) || !jnum(&o, "value", &v)) { reply_err(ci, id, "pause_at: addr and value"); return; }
    if (a && !mem_ok((uint32_t)a, 4, 0)) { reply_err(ci, id, "pause_at: address not mapped"); return; }
    g_pause_addr = (uint32_t)a;
    g_pause_value = (int32_t)v;
    bput(&b, ",\"ok\":true", 10);
    state_fields(&b);
  } else if (!strcmp(cmd, "speed")) {
    double s;
    rebase();
    if (jnum(&o, "speed", &s) && s > 0 && s <= 1000) g_speed = s;
    double tb;
    if (jnum(&o, "turbo", &tb)) g_turbo = tb != 0;
    bput(&b, ",\"ok\":true", 10);
    state_fields(&b);
  } else if (!strcmp(cmd, "screenshot")) {
    // fresh=N: wait for N more presented frames first, so a shot right after
    // an input shows the game's reaction (default 2; 0 = the frame as it is).
    // Paused, or a screen that presents no frames: no wait (fresh:false).
    char path[MAX_PATH];
    if (!jstr(&o, "path", path, sizeof path)) { reply_err(ci, id, "screenshot: path missing"); return; }
    int fresh = (int)jnum_or(&o, "fresh", 2);
    if (fresh < 0) fresh = 0;
    if (fresh > 30) fresh = 30;
    if (fresh && !g_paused && host_now_ms() - g_last_present_ms < NO_PRESENT_MS) {
      Pending *p = add_pending(ci, id, WAIT_SHOT, g_presents + (unsigned)fresh);
      if (p) {
        snprintf(p->path, sizeof p->path, "%s", path);
        p->deadline = host_now_ms() + 2500;
      }
      return;
    }
    reply_shot(ci, id, path, fresh == 0, 0);
    free(b.p);
    return;
  } else if (!strcmp(cmd, "hdshot")) {
    // task-40: the picture the sharp presenter puts on screen (the course at
    // native resolution), as the harness's hdshot; the 800x600 frame when the
    // presentation is off. No wait for fresh frames: pause first.
    char path[MAX_PATH];
    if (!jstr(&o, "path", path, sizeof path)) { reply_err(ci, id, "hdshot: path missing"); return; }
    int hd = hd_native_shot(path);
    if (hd < 0) { reply_err(ci, id, "hdshot: the presenter did not answer"); return; }
    if (hd == 0) { reply_shot(ci, id, path, 1, 0); free(b.p); return; }
    bput(&b, ",\"ok\":true,\"hd\":true,\"path\":", 28);
    bstr(&b, path, (int)strlen(path));
    state_fields(&b);
  } else if (!strcmp(cmd, "hddump")) {
    // task-68: the harness's hddump, the course part of every presented frame
    // to a raw file (path) until called with no path. Tests only.
    char path[MAX_PATH] = "";
    jstr(&o, "path", path, sizeof path);
    if (!hd_native_dump(path[0] ? path : NULL)) { reply_err(ci, id, "hddump: the HD presentation is off"); return; }
    bprintf(&b, ",\"ok\":true");
  } else if (!strcmp(cmd, "wmove") || !strcmp(cmd, "wclick")) {
    // A real mouse at a window pixel (the harness's wmove and wclick, task-31).
    int x = (int)jnum_or(&o, "x", 0), y = (int)jnum_or(&o, "y", 0);
    char btn[16] = "left";
    jstr(&o, "button", btn, sizeof btn);
    int right = !_stricmp(btn, "right");
    if (!act_room(3)) { reply_err(ci, id, "input queue full (wait for the earlier input to finish)"); return; }
    push_act('M', x, y, 0, NULL, !strcmp(cmd, "wmove") ? 50 : 200);
    if (!strcmp(cmd, "wclick")) {
      push_act('D', x, y, right, NULL, 100);
      push_act('U', x, y, right, NULL, 50);
    }
    bprintf(&b, ",\"ok\":true");
  } else if (!strcmp(cmd, "wwheel")) {
    // task-94: the mouse over window pixel x,y, then n wheel notches (negative = towards you), as a real mouse
    int x = (int)jnum_or(&o, "x", 0), y = (int)jnum_or(&o, "y", 0), n = (int)jnum_or(&o, "n", 0);
    if (!act_room(2)) { reply_err(ci, id, "input queue full (wait for the earlier input to finish)"); return; }
    push_act('M', x, y, 0, NULL, 100);
    push_act('W', x, y, n, NULL, 100);
    bprintf(&b, ",\"ok\":true");
  } else if (!strcmp(cmd, "audio_device")) {
    // task-114c tests: what SDL sends when the sound device goes away ("removed", the open device) or a new one
    // arrives ("added"); the game thread handles it in its event pump like a real one
    int snd_native_device_id(void);
    char what[16] = "removed";
    jstr(&o, "event", what, sizeof what);
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = strcmp(what, "added") ? SDL_AUDIODEVICEREMOVED : SDL_AUDIODEVICEADDED;
    e.adevice.which = strcmp(what, "added") ? (Uint32)snd_native_device_id() : 0;
    e.adevice.iscapture = 0;
    SDL_PushEvent(&e);
    bprintf(&b, ",\"ok\":true,\"device\":%d", snd_native_device_id());
  } else if (!strcmp(cmd, "pixel")) {
    int x = (int)jnum_or(&o, "x", 0), y = (int)jnum_or(&o, "y", 0);
    const uint16_t *px = win_screen();
    if (!px || x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) { reply_err(ci, id, "pixel: no frame or off screen"); return; }
    uint16_t v = px[y * SCREEN_W + x];
    int r = (v >> 10) & 31, g = (v >> 5) & 31, bl = v & 31;
    bprintf(&b, ",\"ok\":true,\"rgb\":\"%02x%02x%02x\"", r << 3 | r >> 2, g << 3 | g >> 2, bl << 3 | bl >> 2);
  } else if (!strcmp(cmd, "hdshot")) {
    // The picture on screen at its full size (the presenter's canvas: the
    // native-resolution terrain, the round ball, the native HUD), as a 24-bit
    // BMP. Needs the HD presenter; errors when the original presentation runs.
    char path[MAX_PATH];
    if (!jstr(&o, "path", path, sizeof path)) { reply_err(ci, id, "hdshot: path missing"); return; }
    int r = hd_native_shot(path);
    if (r == 0) { reply_err(ci, id, "hdshot: the HD presentation is off"); return; }
    if (r < 0) { reply_err(ci, id, "hdshot: the presenter did not answer"); return; }
    bput(&b, ",\"ok\":true,\"path\":", 18);
    bstr(&b, path, (int)strlen(path));
  } else if (!strcmp(cmd, "mousedown") || !strcmp(cmd, "mouseup")) {
    // The two halves of a click or drag, for scripts that act while the button is held.
    int x = (int)jnum_or(&o, "x", 0), y = (int)jnum_or(&o, "y", 0);
    char btn[16] = "left";
    jstr(&o, "button", btn, sizeof btn);
    int right = !_stricmp(btn, "middle") ? 2 : !_stricmp(btn, "right");
    if (!act_room(3)) { reply_err(ci, id, "input queue full (wait for the earlier input to finish)"); return; }
    if (!strcmp(cmd, "mousedown")) {
      push_act('m', x, y, 0, NULL, 200);
      push_act('d', x, y, right, NULL, 50);
    } else push_act('u', x, y, right, NULL, 50);
    add_pending(ci, id, WAIT_INPUT, g_acts_queued);
    return;
  } else if (!strcmp(cmd, "move") || !strcmp(cmd, "click") || !strcmp(cmd, "drag") || !strcmp(cmd, "key") ||
             !strcmp(cmd, "keydown") || !strcmp(cmd, "keyup") || !strcmp(cmd, "text")) {
    int x = (int)jnum_or(&o, "x", 0), y = (int)jnum_or(&o, "y", 0);
    char btn[16] = "left", text[512] = "";
    jstr(&o, "button", btn, sizeof btn);
    int right = !_stricmp(btn, "middle") ? 2 : !_stricmp(btn, "right");
    unsigned before = g_acts_queued;
    if (!strcmp(cmd, "move") || !strcmp(cmd, "click")) {
      if (!act_room(8)) { reply_err(ci, id, "input queue full (wait for the earlier input to finish)"); return; }
    }
    double cpts[2 * MAX_PATH_PTS];
    int ncp = !strcmp(cmd, "click") ? jnums(&o, "path", cpts, 2 * MAX_PATH_PTS) / 2 : 0;
    if (!strcmp(cmd, "move")) push_act('m', x, y, 0, NULL, 50);
    else if (!strcmp(cmd, "click") && ncp > 0) {
      // Many clicks in a row, each frame-gated: a tool the game applies on
      // button release (terrain, paths) is applied once per click.
      if (!act_room(3 * ncp)) { reply_err(ci, id, "click: %d points is too many for the input queue", ncp); return; }
      for (int i = 0; i < ncp; i++) {
        int px = (int)cpts[2 * i], py = (int)cpts[2 * i + 1];
        push_act('m', px, py, 0, NULL, 20);
        push_act('d', px, py, right, NULL, 30);
        push_act('u', px, py, right, NULL, 30);
      }
    } else if (!strcmp(cmd, "click")) {
      int times = (int)jnum_or(&o, "count", 1);
      if (times < 1) times = 1;
      if (times > 3) times = 3;
      push_act('m', x, y, 0, NULL, 200);
      for (int i = 0; i < times; i++) {
        push_act('d', x, y, right, NULL, 100);
        push_act('u', x, y, right, NULL, i + 1 < times ? 80 : 50);
      }
    } else if (!strcmp(cmd, "drag")) {
      // Press at the start, move with the button held, release at the end.
      // The game samples the pointer once per frame, so every move waits for
      // a presented frame; a tile is painted only if a sample lands on it.
      //   path: [x0,y0,x1,y1,...] waypoints (default: x,y to x2,y2)
      //   step_px: > 0 adds a sample every step_px pixels along each leg
      //            (use <= 16 to touch every tile); 0 = one sample per leg
      //            split into `steps` equal moves (the old behaviour)
      double pts[2 * MAX_PATH_PTS];
      int np = jnums(&o, "path", pts, 2 * MAX_PATH_PTS) / 2;
      if (np < 1) {
        pts[0] = x; pts[1] = y;
        pts[2] = jnum_or(&o, "x2", x); pts[3] = jnum_or(&o, "y2", y);
        np = 2;
      }
      int steps = (int)jnum_or(&o, "steps", 10);
      if (steps < 1) steps = 1;
      double step_px = jnum_or(&o, "step_px", 0);
      int per_leg_fixed = np == 2 && step_px <= 0;
      long need = 3;
      for (int i = 1; i < np; i++) {
        double dx = pts[2 * i] - pts[2 * i - 2], dy = pts[2 * i + 1] - pts[2 * i - 1];
        double len = sqrt(dx * dx + dy * dy);
        long n = per_leg_fixed ? steps : step_px > 0 ? (long)ceil(len / step_px) : 1;
        need += n < 1 ? 1 : n;
      }
      if (!act_room((int)need) || need > 3000) { reply_err(ci, id, "drag: %ld moves is too many for the input queue (raise step_px)", need); return; }
      int sx = (int)pts[0], sy = (int)pts[1];
      push_act('m', sx, sy, 0, NULL, 200);
      push_act('d', sx, sy, right, NULL, 100);
      int ex = sx, ey = sy;
      for (int i = 1; i < np; i++) {
        double ax = pts[2 * i - 2], ay = pts[2 * i - 1], bx = pts[2 * i], by = pts[2 * i + 1];
        double len = sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        long n = per_leg_fixed ? steps : step_px > 0 ? (long)ceil(len / step_px) : 1;
        if (n < 1) n = 1;
        for (long k = 1; k <= n; k++) push_act('m', (int)floor(ax + (bx - ax) * k / n + 0.5), (int)floor(ay + (by - ay) * k / n + 0.5), 0, NULL, 20);
        ex = (int)bx; ey = (int)by;
      }
      push_act('u', ex, ey, right, NULL, 50);
    } else if (!strcmp(cmd, "key") || !strcmp(cmd, "keydown") || !strcmp(cmd, "keyup")) {
      char name[64] = "";
      jstr(&o, "name", name, sizeof name);
      if (SDL_GetKeyFromName(name) == SDLK_UNKNOWN) { reply_err(ci, id, "unknown key name \"%s\" (SDL key names: Return, Escape, F5, a)", name); return; }
      int times = (int)jnum_or(&o, "count", 1);
      char op = !strcmp(cmd, "keydown") ? 'K' : !strcmp(cmd, "keyup") ? 'L' : 'k';
      if (times > 200) times = 200;
      if (!act_room(times)) { reply_err(ci, id, "input queue full (wait for the earlier input to finish)"); return; }
      for (int i = 0; i < times; i++) push_act(op, 0, 0, 0, name, 30);
    } else {
      jstr(&o, "text", text, sizeof text);
      if (!act_room((int)strlen(text))) { reply_err(ci, id, "input queue full (wait for the earlier input to finish)"); return; }
      for (const char *p = text; *p; p++) {
        char ch[2] = {*p, 0};
        // The game's edit fields take one character per frame (a one-slot
        // mailbox): each waits until the last one was taken (see run_acts).
        push_act('t', 0, 0, 0, ch, 20);
      }
    }
    if (g_acts_queued == before) { bput(&b, ",\"ok\":true,\"queued\":0", 21); }
    else {
      add_pending(ci, id, WAIT_INPUT, g_acts_queued);
      return;
    }
  } else if (!strcmp(cmd, "peek")) {
    cmd_peek(ci, id, &o, &b);
    if (!b.n) return;
  } else if (!strcmp(cmd, "poke")) {
    cmd_poke(ci, id, &o, &b);
    if (!b.n) return;
  } else if (!strcmp(cmd, "find")) {
    cmd_find(ci, id, &o, &b);
    if (!b.n) return;
  } else if (!strcmp(cmd, "call")) {
    cmd_call(ci, id, &o, &b);
    if (!b.n) return;
  } else if (!strcmp(cmd, "mods_get")) {
    char key[128] = "";
    bprintf(&b, ",\"ok\":true");
    if (jstr(&o, "key", key, sizeof key) && key[0]) {
      const char *v = mods_get(key), *from = mods_origin(key);
      bprintf(&b, ",\"value\":");
      if (v) bstr(&b, v, (int)strlen(v));
      else bprintf(&b, "null");
      bprintf(&b, ",\"origin\":");
      if (from) bstr(&b, from, (int)strlen(from));
      else bprintf(&b, "null");
    } else {
      bprintf(&b, ",\"values\":{");
      for (int i = 0, n = mods_value_count(); i < n; i++) {
        const char *k = mods_value_key(i), *v = mods_get(k);
        if (i) bprintf(&b, ",");
        bstr(&b, k, (int)strlen(k));
        bprintf(&b, ":");
        bstr(&b, v, (int)strlen(v));
      }
      bprintf(&b, "}");
    }
    bprintf(&b, ",\"serial\":%u,\"file\":", mods_serial());
    bstr(&b, mods_ini_path(), (int)strlen(mods_ini_path()));
  } else if (!strcmp(cmd, "mods_set")) {
    char key[128] = "", val[192] = "";
    double persist = 0;
    if (!jstr(&o, "key", key, sizeof key) || !key[0]) { reply_err(ci, id, "mods_set: key missing"); return; }
    jstr(&o, "value", val, sizeof val);
    jnum(&o, "persist", &persist);
    if (!mods_set(key, val)) { reply_err(ci, id, "mods_set: bad key"); return; }
    int saved = persist ? mods_ini_save() : 0;
    if (persist && !saved) { reply_err(ci, id, "set, but cannot write %s", mods_ini_path()[0] ? mods_ini_path() : "(no mods file)"); return; }
    fprintf(stderr, "bridge: mod %s=%s%s\n", key, val, saved ? " (saved)" : "");
    bprintf(&b, ",\"ok\":true,\"serial\":%u,\"saved\":%s", mods_serial(), saved ? "true" : "false");
  } else if (!strcmp(cmd, "mods_status")) {
    // The mod platform: profile, load order, startup report, last save check.
    bprintf(&b, ",\"ok\":true,\"profile\":");
    bstr(&b, mods_profile(), (int)strlen(mods_profile()));
    bprintf(&b, ",\"mods_dir\":");
    bstr(&b, mods_root(), (int)strlen(mods_root()));
    bprintf(&b, ",\"order\":[");
    for (int i = 0; i < mods_count(); i++) {
      const char *mid = mods_id_at(i), *ver = mods_version(mid);
      bprintf(&b, "%s{\"id\":", i ? "," : "");
      bstr(&b, mid, (int)strlen(mid));
      bprintf(&b, ",\"version\":");
      bstr(&b, ver, (int)strlen(ver));
      bprintf(&b, "}");
    }
    bprintf(&b, "],\"report\":[");
    for (int i = 0; i < mods_report_count(); i++) {
      if (i) bprintf(&b, ",");
      bstr(&b, mods_report_line(i), (int)strlen(mods_report_line(i)));
    }
    bprintf(&b, "],\"save_check\":");
    bstr(&b, mods_last_save_check(), (int)strlen(mods_last_save_check()));
  } else if (!strcmp(cmd, "trace")) {
    // The function trace ring (the last distinct golf.exe functions called,
    // oldest first): what the game was doing a moment ago. For finding which
    // code reacts to an input (use it while paused).
    extern uint32_t port_trace_buf[1024];
    extern uint32_t port_trace_pos;
    int n = (int)jnum_or(&o, "count", 200);
    if (n < 1) n = 1;
    if (n > 1024) n = 1024;
    bprintf(&b, ",\"ok\":true,\"trace\":[");
    for (int i = n; i >= 1; i--) bprintf(&b, "%s\"%06x\"", i == n ? "" : ",", port_trace_buf[(port_trace_pos - i) & 1023]);
    bprintf(&b, "]");
  } else if (!strcmp(cmd, "grid")) {
    // Where the per-tile arrays live now (grid.h): golf.exe's original
    // address of each, its real base, bytes per tile and whether it is the
    // (w+1) x (h+1) corner lattice. peek/poke translate only addresses inside
    // the original 50 x 50 extent; for any other tile read base + elem * (x * stride + y).
    bprintf(&b, ",\"ok\":true,\"w\":%d,\"h\":%d,\"arrays\":[", g_map_w, g_map_h);
    for (int i = 0; i < grid_count; i++)
      bprintf(&b, "%s{\"addr\":%u,\"base\":%.0f,\"elem\":%d,\"corner\":%d}", i ? "," : "", (unsigned)grid_arrays[i].addr,
              (double)g_grid[i], grid_arrays[i].elem, grid_arrays[i].corner);
    bprintf(&b, "]");
  } else if (!strcmp(cmd, "perf")) {
    cmd_perf(&b);
  } else if (!strcmp(cmd, "log")) {
    char text[512] = "";
    jstr(&o, "text", text, sizeof text);
    fprintf(stderr, "bridge: %s\n", text);
    bput(&b, ",\"ok\":true", 10);
  } else if (!strcmp(cmd, "quit")) {
    int code = (int)jnum_or(&o, "code", 0);
    bput(&b, ",\"ok\":true", 10);
    reply(ci, id, &b);
    free(b.p);
    flush_conn(&g_conns[ci]);
    fprintf(stderr, "bridge: quit %d\n", code);
    host_exit(code);
  } else {
    reply_err(ci, id, "unknown command \"%s\"", cmd);
    return;
  }
  reply(ci, id, &b);
  free(b.p);
}

static void settle_pending(void) {
  double now = host_now_ms();
  for (int i = 0; i < g_npending;) {
    Pending *p = &g_pending[i];
    if (p->kind == WAIT_SHOT) {
      int got = (int)(g_presents - p->target) >= 0;
      if (!got && !g_paused && now < p->deadline && now - g_last_present_ms < NO_PRESENT_MS) { i++; continue; }
      reply_shot(p->conn, p->id, p->path, got, g_presents - p->start);
      g_pending[i] = g_pending[--g_npending];
      continue;
    }
    // An input request is answered when its last act ran and the gate that
    // act set is open (frame passed, key taken), unless more input waits.
    int done = p->kind == WAIT_INPUT ? ((int)(g_acts_done - p->target) >= 0 && (g_nacts > 0 || gate_open(now))) : !g_stepping;
    if (!done) { i++; continue; }
    Buf b = {0};
    bput(&b, ",\"ok\":true", 10);
    if (p->kind == WAIT_STEP) bprintf(&b, ",\"unit\":\"%s\"", g_step_unit);
    state_fields(&b);
    reply(p->conn, p->id, &b);
    free(b.p);
    g_pending[i] = g_pending[--g_npending];
  }
}

// ------------------------------------------------------------------ polling

static void poll_sockets(void) {
  if (g_listen != INVALID_SOCKET) {
    for (;;) {
      SOCKET s = accept(g_listen, NULL, NULL);
      if (s == INVALID_SOCKET) break;
      u_long nb = 1;
      ioctlsocket(s, FIONBIO, &nb);
      int slot = -1;
      for (int i = 0; i < MAXCONN; i++)
        if (g_conns[i].s == INVALID_SOCKET) { slot = i; break; }
      if (slot < 0) { closesocket(s); continue; }
      g_conns[slot].s = s;
      g_conns[slot].nin = 0;
      g_conns[slot].out.n = 0;
      fprintf(stderr, "bridge: client %d connected\n", slot);
    }
  }
  for (int i = 0; i < MAXCONN; i++) {
    Conn *c = &g_conns[i];
    if (c->s == INVALID_SOCKET) continue;
    for (;;) {
      int room = (int)sizeof c->in - 1 - c->nin;
      if (room <= 0) { c->nin = 0; reply_err(i, NULL, "request too long"); break; }
      int k = recv(c->s, c->in + c->nin, room, 0);
      if (k == 0 || (k < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) {
        closesocket(c->s);
        c->s = INVALID_SOCKET;
        for (int p = 0; p < g_npending;) {
          if (g_pending[p].conn == i) g_pending[p] = g_pending[--g_npending];
          else p++;
        }
        fprintf(stderr, "bridge: client %d left\n", i);
        break;
      }
      if (k < 0) break;
      c->nin += k;
      c->in[c->nin] = 0;
      char *nl;
      while ((nl = strchr(c->in, '\n')) != NULL) {
        *nl = 0;
        if (nl > c->in && nl[-1] == '\r') nl[-1] = 0;
        if (c->in[0]) run_cmd(i, c->in);
        if (c->s == INVALID_SOCKET) break;
        int rest = c->nin - (int)(nl + 1 - c->in);
        memmove(c->in, nl + 1, rest);
        c->nin = rest;
        c->in[c->nin] = 0;
      }
      if (c->s == INVALID_SOCKET) break;
    }
  }
}

static void flush_all(void) {
  for (int i = 0; i < MAXCONN; i++)
    if (g_conns[i].s != INVALID_SOCKET) flush_conn(&g_conns[i]);
}

static void service(void) {
  poll_sockets();
  run_acts();
  settle_pending();
  flush_all();
}

void bridge_on_present(void) {
  static double t0;
  static unsigned n0;
  double now = host_now_ms();
  if (now - t0 >= 1000) {
    g_fps = (g_presents - n0) * 1000.0 / (now - t0);
    t0 = now;
    n0 = g_presents;
  }
  g_last_present_ms = now;
  g_step_since = now;
  if (g_stepping && !g_step_loops && g_step_left && --g_step_left == 0) {
    g_stepping = 0;
    set_paused(1);
  }
  if (g_pause_addr && *(volatile int32_t *)(uintptr_t)g_pause_addr == g_pause_value) {
    g_pause_addr = 0;
    g_stepping = 0;
    set_paused(1);
  }
}

void bridge_tick(void) {
  static int busy;
  if (!g_enabled || busy) return;
  busy = 1;
  g_ticks++;
  service();
  if (g_stepping) {
    double now = host_now_ms();
    if (g_step_skip) g_step_skip = 0;   // the loop that ran the step command does not count
    else if (!g_step_loops) {
      // Nothing presented for a while: this screen shows no frames, count loops.
      if (now - g_step_since > NO_PRESENT_MS) { g_step_loops = 1; g_step_unit = "loops"; }
    } else if (g_step_left && --g_step_left == 0) {
      g_stepping = 0;
      set_paused(1);
    }
  }
  // Paused: the game thread stays here, so the game's memory is still and
  // time stands; commands keep running.
  while (g_paused) {
    host_sleep_ms(2);
    service();
    static double last_pump;
    double now = host_now_ms();
    if (now - last_pump > 50) {   // keep the window answering Windows, events stay queued for the game
      SDL_PumpEvents();
      last_pump = now;
    }
  }
  busy = 0;
  if (g_async_fn) {
    IcallFn fn = g_async_fn;
    uint32_t a[16];
    memcpy(a, g_async_a, sizeof a);
    g_async_fn = 0;
    fn(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
    // a modal screen opened outside its own flow leaves its click and key results set, and the main loop would act on them
    *(volatile int32_t *)(uintptr_t)0x822d68 = 0;
    *(volatile int32_t *)(uintptr_t)0x822b9c = 0;
  }
}

int bridge_enabled(void) { return g_enabled; }

void bridge_init(int argc, char **argv) {
  const char *port_arg = NULL, *portfile = NULL;   // --mods is read by mods_init (task-11)
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--bridge") && i + 1 < argc) port_arg = argv[++i];
    else if (!strcmp(argv[i], "--bridge-portfile") && i + 1 < argc) portfile = argv[++i];
  }
  const char *env = getenv("SIMGOLF_BRIDGE");
  if (!port_arg && env && *env == '1') {
    port_arg = getenv("SIMGOLF_BRIDGE_PORT");
    if (!port_arg) port_arg = "0";
  }
  if (!portfile) portfile = getenv("SIMGOLF_BRIDGE_PORTFILE");
  for (int i = 0; i < MAXCONN; i++) g_conns[i].s = INVALID_SOCKET;
  if (!port_arg) return;

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa)) { fprintf(stderr, "bridge: no winsock\n"); return; }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // never reachable from another machine
  a.sin_port = htons((u_short)atoi(port_arg));
  BOOL excl = TRUE;
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof excl);
  if (s == INVALID_SOCKET || bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 4)) {
    fprintf(stderr, "bridge: cannot listen on 127.0.0.1:%s (error %d)\n", port_arg, WSAGetLastError());
    if (s != INVALID_SOCKET) closesocket(s);
    return;
  }
  int alen = sizeof a;
  getsockname(s, (struct sockaddr *)&a, &alen);
  g_port = ntohs(a.sin_port);
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  g_listen = s;
  g_enabled = 1;
  fprintf(stderr, "bridge: listening on 127.0.0.1:%d\n", g_port);
  if (portfile) {
    char tmp[MAX_PATH + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", portfile);
    FILE *f = fopen(tmp, "w");
    if (f) {
      fprintf(f, "%d\n", g_port);
      fclose(f);
      MoveFileExA(tmp, portfile, MOVEFILE_REPLACE_EXISTING);
    }
  }
}
