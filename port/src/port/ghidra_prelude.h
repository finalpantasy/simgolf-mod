// Definitions that make Ghidra's decompiler output compile as C under
// Emscripten (wasm32: 32-bit pointers, little endian, like the original x86).
// Included first by every translated file, before the module's types.h.

#ifndef SIMGOLF_PORT_GHIDRA_PRELUDE_H
#define SIMGOLF_PORT_GHIDRA_PRELUDE_H

#include <stdint.h>
#include "port/golf/re_bounds.h"
#include "port/camera_tiles.h"
#include "port/clubhouse_start.h"
#include "port/staff.h"
#include "port/shot_readout.h"
#include "port/grid.h"   // MAP_W, MAP_H, GRID(i): tools/translate/grid.py
void port_loading_begin(void);
void port_loading_end(void);
int port_loading_fast(void);
void port_course_begin(int creating, uint32_t fn);
void port_course_end(uint32_t fn);
int port_course_fast(void);
uint32_t port_course_height(int x, int y);

#ifdef _MSC_VER
// Native Windows build (simgolf mod): the MSVC headers already typedef
// wchar_t as unsigned short; the Ghidra types redeclare it as short.
#define wchar_t ghidra_wchar_t
#endif

#define __cdecl
#define __stdcall
#define __thiscall
#define __fastcall

typedef uint32_t pointer32;
typedef double float10;   // x87 80-bit: double is close enough
typedef uint32_t undefined3;
typedef uint64_t undefined5;
typedef uint64_t undefined6;
typedef uint64_t undefined7;
typedef long long longlong;
typedef unsigned long long ulonglong;
typedef void code;
typedef int16_t sword;
typedef int8_t sbyte;
typedef int32_t sdword;
typedef uint64_t qword;
typedef int64_t sqword;
typedef uint32_t uint3;
typedef int32_t int3;
typedef uint64_t uint5, uint6, uint7;
typedef int64_t int5, int6, int7;
typedef uint16_t wchar16_t;
typedef uint64_t uint8;
typedef uint32_t nu;
static inline uint32_t F2U(float f) { union { float f; uint32_t u; } v = {f}; return v.u; }
static inline int32_t F2I(float f) { union { float f; int32_t i; } v = {f}; return v.i; }
static inline float I2F(int32_t i) { union { int32_t i; float f; } v = {i}; return v.f; }

// Ghidra piece access X._off_size_
#define PIECE(x, off, size) (*(PIECE_T##size *)((char *)&(x) + (off)))
typedef uint8_t PIECE_T1;
typedef uint16_t PIECE_T2;
typedef uint32_t PIECE_T3;
typedef uint32_t PIECE_T4;
typedef uint64_t PIECE_T8;

// CONCATxy(a, b): a in the high x bytes, b in the low y bytes.
#define CC_MASK(y) ((y) >= 8 ? ~0ull : ((1ull << (8 * (y))) - 1))
#define CC(x, y, a, b) ((((uint64_t)(a) & CC_MASK(x)) << (8 * (y))) | ((uint64_t)(b) & CC_MASK(y)))
#define CONCAT11(a, b) ((uint16_t)CC(1, 1, a, b))
#define CONCAT12(a, b) ((uint32_t)CC(1, 2, a, b))
#define CONCAT13(a, b) ((uint32_t)CC(1, 3, a, b))
#define CONCAT14(a, b) ((uint64_t)CC(1, 4, a, b))
#define CONCAT21(a, b) ((uint32_t)CC(2, 1, a, b))
#define CONCAT22(a, b) ((uint32_t)CC(2, 2, a, b))
#define CONCAT24(a, b) ((uint64_t)CC(2, 4, a, b))
#define CONCAT26(a, b) ((uint64_t)CC(2, 6, a, b))
#define CONCAT31(a, b) ((uint32_t)CC(3, 1, a, b))
#define CONCAT34(a, b) ((uint64_t)CC(3, 4, a, b))
#define CONCAT35(a, b) ((uint64_t)CC(3, 5, a, b))
#define CONCAT42(a, b) ((uint64_t)CC(4, 2, a, b))
#define CONCAT43(a, b) ((uint64_t)CC(4, 3, a, b))
#define CONCAT44(a, b) ((uint64_t)CC(4, 4, a, b))
#define CONCAT51(a, b) ((uint64_t)CC(5, 1, a, b))
#define CONCAT52(a, b) ((uint64_t)CC(5, 2, a, b))
#define CONCAT53(a, b) ((uint64_t)CC(5, 3, a, b))
#define CONCAT62(a, b) ((uint64_t)CC(6, 2, a, b))
#define CONCAT71(a, b) ((uint64_t)CC(7, 1, a, b))

// SUBxy(v, off): y bytes of an x-byte value starting at byte off.
#define SUB21(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB41(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB42(v, o) ((uint16_t)((uint64_t)(v) >> (8 * (o))))
#define SUB81(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB82(v, o) ((uint16_t)((uint64_t)(v) >> (8 * (o))))
#define SUB84(v, o) ((uint32_t)((uint64_t)(v) >> (8 * (o))))
#define SUB31(v, o) ((uint8_t)((uint64_t)(v) >> (8 * (o))))
#define SUB32(v, o) ((uint16_t)((uint64_t)(v) >> (8 * (o))))
#define SUB43(v, o) ((uint32_t)(((uint64_t)(v) >> (8 * (o))) & 0xffffff))

#define ZEXT12(v) ((uint16_t)(uint8_t)(v))
#define ZEXT14(v) ((uint32_t)(uint8_t)(v))
#define ZEXT18(v) ((uint64_t)(uint8_t)(v))
#define ZEXT24(v) ((uint32_t)(uint16_t)(v))
#define ZEXT28(v) ((uint64_t)(uint16_t)(v))
#define ZEXT48(v) ((uint64_t)(uint32_t)(v))
#define ZEXT34(v) ((uint32_t)(v) & 0xffffff)
#define SEXT12(v) ((int16_t)(int8_t)(v))
#define SEXT14(v) ((int32_t)(int8_t)(v))
#define SEXT18(v) ((int64_t)(int8_t)(v))
#define SEXT24(v) ((int32_t)(int16_t)(v))
#define SEXT28(v) ((int64_t)(int16_t)(v))
#define SEXT48(v) ((int64_t)(int32_t)(v))

#define CARRY1(a, b) ((uint8_t)((uint8_t)(a) + (uint8_t)(b)) < (uint8_t)(a))
#define CARRY2(a, b) ((uint16_t)((uint16_t)(a) + (uint16_t)(b)) < (uint16_t)(a))
#define CARRY4(a, b) ((uint32_t)((uint32_t)(a) + (uint32_t)(b)) < (uint32_t)(a))
#define SCARRY1(a, b) (((int8_t)(a) + (int)(int8_t)(b)) != (int8_t)((int8_t)(a) + (int8_t)(b)))
#define SCARRY2(a, b) (((int16_t)(a) + (int)(int16_t)(b)) != (int16_t)((int16_t)(a) + (int16_t)(b)))
#define SCARRY4(a, b) (((int64_t)(int32_t)(a) + (int32_t)(b)) != (int32_t)((uint32_t)(a) + (uint32_t)(b)))
#define SBORROW1(a, b) (((int8_t)(a) - (int)(int8_t)(b)) != (int8_t)((int8_t)(a) - (int8_t)(b)))
#define SBORROW2(a, b) (((int16_t)(a) - (int)(int16_t)(b)) != (int16_t)((int16_t)(a) - (int16_t)(b)))
#define SBORROW4(a, b) (((int64_t)(int32_t)(a) - (int32_t)(b)) != (int32_t)((uint32_t)(a) - (uint32_t)(b)))
#define POPCOUNT(x) __builtin_popcount((uint32_t)(x))
#define LZCOUNT(x) __builtin_clz((uint32_t)(x))
#define ROUND(x) __builtin_rint(x)
#define TRUNC(x) __builtin_trunc(x)
#define SQRT(x) __builtin_sqrt(x)
#define ABS(x) __builtin_fabs(x)
#define NAN(x) __builtin_isnan(x)
// x87 transcendental instructions Ghidra prints as calls
#define fsin(x) __builtin_sin(x)
#define fcos(x) __builtin_cos(x)
#define fsqrt(x) __builtin_sqrt(x)
#define fpatan(y, x) __builtin_atan2(y, x)
#define fptan(x) __builtin_tan(x)
#define frndint(x) __builtin_rint(x)
#define LOCK()
#define UNLOCK()
#define halt_baddata() port_halt(__func__, "baddata")
#define halt_unimplemented() port_halt(__func__, "unimplemented")
#define software_interrupt(n) (port_halt(__func__, "int"), 0)
#define swi(n) (port_halt(__func__, "swi"), 0)
#ifndef true
#define true 1
#define false 0
#endif
typedef uint8_t BADSPACEBASE;

// SEH frame chain: the translated code links frames here; nothing unwinds.
extern void *ExceptionList;

// Indirect calls. Code addresses in the image (vtables, callbacks) stay as
// original addresses; icall_lookup maps them to wrappers that take eight
// 32-bit argument slots, which is how the x86 stack passed them.
#define ICALL_PARAMS uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14, uint32_t a15
typedef uint32_t (*IcallFn)(ICALL_PARAMS);
typedef struct { uint32_t addr; IcallFn fn; uint32_t flags; } IcallEntry;  // flags 1: __thiscall
uint32_t icall_this(uint32_t fn, uint32_t obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
  uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12,
  uint32_t a13, uint32_t a14);
IcallFn icall_lookup(uint32_t addr);
extern double g_fret;     // ST0 return of a float function called indirectly
extern uint32_t g_edx;    // high half of a 64-bit return

static inline uint32_t argu_u(uint32_t x) { return x; }
static inline uint32_t argu_f(float x) { union { float f; uint32_t u; } v = {x}; return v.u; }
#define ARGU(x) _Generic((x), float: argu_f, double: argu_f, long double: argu_f, \
  default: argu_u)(_Generic((x), float: (x), double: (x), long double: (x), default: (uint32_t)(uintptr_t)(x)))
#define ICALL_FN(f) icall_lookup((uint32_t)(uintptr_t)(f))
#define ICALL0(fn_) ICALL_FN(fn_)(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL1(fn_, a) ICALL_FN(fn_)(ARGU(a), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL2(fn_, a, b) ICALL_FN(fn_)(ARGU(a), ARGU(b), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL3(fn_, a, b, c) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL4(fn_, a, b, c, d) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL5(fn_, a, b, c, d, e) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL6(fn_, a, b, c, d, e, f) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL7(fn_, a, b, c, d, e, f, g) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL8(fn_, a, b, c, d, e, f, g, h) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALL9(fn_, a, b, c, d, e, f, g, h, i) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), 0, 0, 0, 0, 0, 0, 0)
#define ICALL10(fn_, a, b, c, d, e, f, g, h, i, j) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), 0, 0, 0, 0, 0, 0)
#define ICALL11(fn_, a, b, c, d, e, f, g, h, i, j, k) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), 0, 0, 0, 0, 0)
#define ICALL12(fn_, a, b, c, d, e, f, g, h, i, j, k, l) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), 0, 0, 0, 0)
#define ICALL13(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), 0, 0, 0)
#define ICALL14(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m, n) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), 0, 0)
#define ICALL15(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), ARGU(o), 0)
#define ICALL16(fn_, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) ICALL_FN(fn_)(ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), ARGU(o), ARGU(p))
#define ICALLT0(fn_, obj_) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT1(fn_, obj_, a) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT2(fn_, obj_, a, b) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT3(fn_, obj_, a, b, c) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT4(fn_, obj_, a, b, c, d) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT5(fn_, obj_, a, b, c, d, e) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT6(fn_, obj_, a, b, c, d, e, f) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT7(fn_, obj_, a, b, c, d, e, f, g) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), 0, 0, 0, 0, 0, 0, 0, 0)
#define ICALLT8(fn_, obj_, a, b, c, d, e, f, g, h) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), 0, 0, 0, 0, 0, 0, 0)
#define ICALLT9(fn_, obj_, a, b, c, d, e, f, g, h, i) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), 0, 0, 0, 0, 0, 0)
#define ICALLT10(fn_, obj_, a, b, c, d, e, f, g, h, i, j) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), 0, 0, 0, 0, 0)
#define ICALLT11(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), 0, 0, 0, 0)
#define ICALLT12(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), 0, 0, 0)
#define ICALLT13(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l, m) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), 0, 0)
#define ICALLT14(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l, m, n) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), 0)
#define ICALLT15(fn_, obj_, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) icall_this((uint32_t)(uintptr_t)(fn_), ARGU(obj_), ARGU(a), ARGU(b), ARGU(c), ARGU(d), ARGU(e), ARGU(f), ARGU(g), ARGU(h), ARGU(i), ARGU(j), ARGU(k), ARGU(l), ARGU(m), ARGU(n), ARGU(o))

static inline float U2F(uint32_t u) { union { uint32_t u; float f; } v = {u}; return v.f; }
static inline double U2D(uint32_t lo, uint32_t hi) {
  union { uint64_t u; double d; } v = {((uint64_t)hi << 32) | lo}; return v.d;
}
#define U2Q(lo, hi) (((uint64_t)(hi) << 32) | (uint32_t)(lo))

void port_halt(const char *fn, const char *why);

void port_ret_log(uint32_t fn, int where, uint32_t value);
void port_trace_dump(int n);
void port_debug4(const char *what, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
#define PORT_RET(fn, where, x) ({ __typeof__(x) r_ = (x); \
  port_ret_log((fn), (where), (uint32_t)(uintptr_t)(r_)); r_; })

// Ring buffer of the last translated functions entered (port_trace_dump()).
extern uint32_t port_trace_buf[1024];
extern uint32_t port_trace_pos;
// Up to three functions port code wants to count entries of (camera_input.c:
// the course frame, modal waits, text entry), set at run time. Counters can't
// be overwritten like the ring, which a busy frame fills many times over.
extern uint32_t port_watch_addr[3];
extern uint32_t port_watch_hits[3];
// task-113, the debug overlay's call tracer: port_trace_sel holds one bit per code byte of
// 0x401000..0x4a4f10 (port_trace_select sets them); port_trace_any is 0 unless something is
// selected, so an untraced game pays one load per call.
extern volatile uint32_t port_trace_any;
extern uint8_t port_trace_sel[0xa4000 >> 3];
void port_trace_hit(uint32_t addr);
// Short loops (A B A B ...) are folded so the buffer keeps history.
#define PORT_TRACE(addr) do { uint32_t a_ = (addr); \
  if (port_trace_any && a_ - 0x401000u < 0xa4000u && ((port_trace_sel[(a_ - 0x401000u) >> 3] >> (a_ & 7)) & 1)) port_trace_hit(a_); \
  if (a_ == port_watch_addr[0]) port_watch_hits[0]++; \
  else if (a_ == port_watch_addr[1]) port_watch_hits[1]++; \
  else if (a_ == port_watch_addr[2]) port_watch_hits[2]++; \
  if (port_trace_buf[(port_trace_pos - 1) & 1023] != a_ && port_trace_buf[(port_trace_pos - 2) & 1023] != a_) \
    port_trace_buf[port_trace_pos++ & 1023] = a_; } while (0)

// task-3 (win32.c): the game's screen ("canvas") and the original resolution
// whose art scale it uses (800x600; 1024x768 and 1280x1024 only when the
// canvas is exactly that size). Translator patches (tools/translate/golf.json)
// use the canvas for extents and the tier for golf's per-resolution tables.
extern int win_screen_w, win_screen_h, win_tier_w, win_tier_h;
// task-3 B4/B5 (port/src/port/golf/hd_ui.c): golf's bottom-left course UI
// drawn at the bottom of a taller canvas (HD_UI_DY down, 0 on 600), the
// course/UI layer split point, the frame's camera and the wide-view clamp.
#define HD_UI_DX (win_screen_w - 800)
#define HD_UI_DY (win_screen_h - 600)
int hd_ui_view_begin(int dx, int dy);
void hd_ui_view_end(void);
void hd_ui_frame_camera(void);
void hd_ui_course_done(void);
void hd_ui_camera_clamp(void);
// task-17 (port/src/port/golf/art_hooks.c): PCX file loaded into a surface.
void art_pcx_load(void *surface, const char *path);
// task-37: the sub-tile camera's course offset (canvas px) on top of golf's
// whole-tile camera, used by worldToScreen and screenToWorld.
extern int hd_cam_ox, hd_cam_oy;
// task-68: the cull overscan while golf draws the course part of a frame (golf/hd_ui.h).
extern int hd_os_l, hd_os_r, hd_os_t, hd_os_b;

// task-19 (src/mods/mdata.h): moddable constants and golfer thoughts.
// Translator patches (tools/translate/golf.json) wrap an original literal in
// MDATA(...): the value of a data table field when a mod changed it, else
// the literal. The browser build has no mod layer: always the literal.
#ifndef __EMSCRIPTEN__
int mdata_cell(int *cache, const char *table, const char *row, const char *field, int lit);
int mdata_at(int *cache, const char *table, int row, const char *field, int lit);
void mdata_thought_begin(char *buf);
void mdata_thought_end(int event, int arg, int arg2, int slot, char *buf);
#define MDATA(t, r, f, lit) ({ static int mdc_ = -1; mdata_cell(&mdc_, (t), (r), (f), (int)(lit)); })
#define MDATA_AT(t, i, f, lit) ({ static int mdc_ = -1; mdata_at(&mdc_, (t), (int)(i), (f), (int)(lit)); })
#else
#define MDATA(t, r, f, lit) ((int)(lit))
#define MDATA_AT(t, i, f, lit) ((int)(lit))
#define mdata_thought_begin(buf) ((void)0)
#define mdata_thought_end(e, a, b, s, buf) ((void)0)
#endif

// task-40 (src/port/golf/overrides.c): 1 = FUN_00449540 raises water tiles at load too.
int fix_water_push(void);
int fix_weed_guard(int tile, int original);   // task-114t
int fix_hole_time(void);   // task-114: repairs an inflated hole 1 time total at load (mods.ini [fixes] hole_time)

// task-6 (src/port/golf/overrides.c): FUN_00462020's default skin for a head, with v1.03's bound.
int head_default_skin(int male, int rec);
int fix_clothing_palette_on(void);
uint32_t compose_golfer_palette(int slot);

// task-74 (src/port/golf/skills.c): the pro's skill caps and the bad-shot loss switch.
int skill_soft_cap(int tier);
int skill_hard_cap(void);
int skill_loss_on(void);

// task-44 (src/port/golf/overrides.c): Shift+T "trees off" also hides the
// decoration trees. FUN_0040f5c0 sets g_decor_tree_pass while its decoration
// switch draws a tree (the game's own code and state run unchanged); the two
// sprite queue functions (FUN_004628d0, FUN_00462a30) drop the sprite then
// when hide_decor_trees() says the view is "trees off" and the setting is on
// (mods.ini [view] hide_decor_trees, default 1).
extern int g_decor_tree_pass;
int hide_decor_trees(void);
// task-90 (src/port/golf/overrides.c): 1 when the view is at zoom 1 and
// mods.ini [ui] far_trees (default 1) says draw the terrain trees there too
// (the original skips them at zoom 1).
int far_trees(void);
// task-90: golf's sprite draw queue (FUN_004627d0 reset, FUN_00462a30 sorted
// insert, FUN_004628d0 direct draw, FUN_00462be0 draw one, FUN_00463100 walk,
// FUN_00462800 snapshot) held 1024 sprites in arrays at 0x82415c..0x8361f4; a
// big course at zoom 1 with its trees needs far more. Patches in
// tools/translate/golf.trees.json point those functions at port storage of
// SPRQ_N entries per array; g_sprq_cap is SPRQ_N, or 0x400 (the original) when
// far_trees is off. sprq_begin() runs at the queue reset (once per frame).
#define SPRQ_N 32768
extern uint32_t g_sprq[10][SPRQ_N];
extern int g_sprq_cap, g_sprq_hint;
void sprq_begin(void);
#define SPRQ_KEY   (*g_sprq[0])   // was 0x82415c: sort key (depth)
#define SPRQ_FLAGS (*g_sprq[1])   // 0x82815c: draw flags (param 8)
#define SPRQ_LIFT  (*g_sprq[2])   // 0x82915c: param 7
#define SPRQ_SPR   (*g_sprq[3])   // 0x82a15c: sprite
#define SPRQ_X     (*g_sprq[4])   // 0x82b160: x
#define SPRQ_Y     (*g_sprq[5])   // 0x82c164: y
#define SPRQ_SX    (*g_sprq[6])   // 0x830164: screen x
#define SPRQ_SY    (*g_sprq[7])   // 0x831164: screen y
#define SPRQ_MARK  (*g_sprq[8])   // 0x834174: cleared per entry
#define SPRQ_NEXT  (*g_sprq[9])   // 0x8361f4: next in sorted order

// task-48 (src/port/golf/overrides.c): the player's building placement and its hover ghost
// (main loop, FUN_0040f5c0) go through fix_place_check, which is FUN_0040db90 plus: a lot
// that is not being upgraded may not overlap a building of its own kind
// (fix_overlap_blocks, read inside FUN_0040db90 while the check runs).
int fix_place_check(int x, int y, int size, int tool);
int fix_overlap_blocks(int tool);

// task-26/30 (src/port/golf/hud_hooks.c): the Frosted Mint HUD. Translator
// patches in FUN_0040f5c0 call these; with ui.hud=original (and in the
// browser build) every one is a no-op that keeps the original behaviour.
void hud_save(void);          // before the old HUD draws: keep the course under it
void hud_restore(void);       // after: put the course back, hand state to the new HUD
void hud_news_posted(void);   // accepted news post: capture before the single slot is overwritten
void opt_edit_prefs(void);     // the wrench menu's "Options..." (was Edit Preferences): the native Options page, else the original list
int hud_on(void);             // the new HUD replaces the old one this frame
int hud_mx(int x);            // mouse x for the old HUD's hover (parked off screen when on)
int hud_my(int y);
int hud_old_click(void);      // a click may go to the old wheel/trays (injected or scripted ones only when on)
int hud_strip_click(void);    // the same for the old golfer chip strip
int hud_ylim(int y);          // the old HUD's y thresholds for the world cursor (off screen when on)

#include "port/course_popovers.h"
// task-31 (src/port/golf/golfers_hooks.c, golfers_game.c): the Frosted Mint
// name tags, thought bubbles and golfer card. Translator patches
// (tools/translate/golf.golfers.json) in FUN_00462be0 (golfer sprite and name),
// FUN_0040f5c0 (thought bubbles, card call) and FUN_0045c560 (the card) call
// these; with ui.golfers=original (and in the browser build) each one keeps the
// original behaviour.
extern int gw_hide;                                    // 1 while the original bubble's draws are skipped
void gw_seen(int slot, int x, int y);                  // a golfer sprite was drawn, feet at (x, y)
int gw_name(int slot, const char *text, int x, int y, int mood);   // 1: the native tag replaces the name text
int gw_active(void);                                   // native golfer tags are recorded this frame
void gw_thought_begin(int type, int state);            // a thought bubble is about to draw (sets gw_hide)
void gw_face(int idx);                                 // the speaker's face sprite index
void gw_thought_end(int slot, int x, int y, const char *text, unsigned colour);
int gw_staff_name(int idx, const char *text, int x, int y);   // 1: the native tag replaces a staff member's name text
void gw_staff_begin(void);                             // a staff member's speech is about to draw (sets gw_hide)
void gw_staff_end(int idx, int x, int y, const char *text);
void gc_begin(int slot);                               // the golfer card is about to draw
void gc_end(int slot);
int gc_text(int fn, const char *text, int x, int y, unsigned colour);   // the card's text draws (fn 0..3 = 404b70, 404bc0, 4049d0, 404ad0)
void gc_rect(int x, int y, int w, int h, unsigned colour);              // the card's meter bars
void gc_mouse(int *x, int *y);                         // the card's pointer: parked unless the native card clicked
// task-41 (src/port/golf/ball_hooks.c): the round HD golf ball. Translator patches
// (golf.ball.json) route golf's own ball draws through these; with ui.ball=original
// (and in the browser build) they make the original call and nothing else.
// task-87: the 0x58-byte object table at 0x81ca6c, read with an index. Garbage indexes (lost vcall
// arguments in FUN_00462020) give null, as the browser build's zero pages did.
static inline int *gs_slot(int i) { return (i >= 0 && i < 0x200) ? *(int **)(uintptr_t)(0x81ca6cu + (uint32_t)i * 0x58u) : (int *)0; }
void hdball_rest(int slot, int sx, int sy);   // a ball at rest on a hole (golf's 2 px line)
void hdball_air(int slot, int sx, int sy);    // a ball in flight or rolling (golf queues its sprites)
void hdball_item(int i);                      // one queued sprite of FUN_00463100 (FUN_00462be0 for others)
void hdball_resolve(void);                    // after golf drew its sprites
void hdball_trail(int slot, int x0, int y0, int x1, int y1, int col, int w, int a);   // task-61: golf's trail segment behind a ball in flight
void aim_start(int x, int y);   // task-115: the shot aim arrow hooks (golf/aim_hooks.c)
void aim_dot(int x, int y, int col, int w, int a);
void aim_seg(int x0, int y0, int x1, int y1, int col, int w, int a);
void place_center(int *tx, int *ty);   // task-115: lot placement centred on the pointer (golf/place_hooks.c)
int place_tool_after_placement(int tool);   // keep repeatable amenities armed after a successful placement
int hole_line_shift(int tee);   // task-119: the sideways offset of a tee's route line (golf/hole_line_hooks.c)

// task-78 (port/water_native.c): realistic water. 1 while the presenter draws the water,
// so golf skips its own ripples, waves, waterfalls, spray and sparkles (translator patch golf.water.json).
int water_hide_golf_ripples(void);
void *water_sprite(int id, int frame, int view);   // FUN_0043d6f0, or 0 for water golf no longer draws
uint32_t water_queue(int x, int y, uint32_t depth, int w, int h, uint32_t sprite, int a, uint32_t b);   // FUN_004628d0 unless sprite is 0

// task-28 (src/port/golf/reports_hooks.c, reports_game.c): the Frosted Mint reports. Translator
// patches (port/tools/translate/golf.reports.json) in the main loop and the report screens call these;
// with ui.reports=original (and in the browser build) each one keeps the original behaviour.
void rp_tick(void);             // once per main-loop frame
void rp_begin(int kind);        // the main loop opens report kind (reports_frosted.h RP_*)
void rp_frame(void);            // per iteration of the report's loop, before it presents
void rp_frame_preflush(void);   // publish the native panel, leaving the following original flush to present once
void rp_done(void);             // the report returned
void rp_quad(void *w, unsigned a, unsigned b, unsigned c, unsigned d);       // FUN_00476310
void rp_t650(void *w, int font, char *s, int x, int y, int len);             // FUN_00476650
void rp_t6a0(void *w, int font, char *s, int x, int y, int wd, char *len);   // FUN_004766a0
void rp_t700(void *w, int font, char *s, int x, int y, char *len);           // FUN_00476700
void rp_t750(void *w, int font, char *s, int x, int y, char *len);           // FUN_00476750
int rp_tc(int fn, char *s, int x, int y, unsigned col);   // fn 0..3 = 404b70, 404bc0, 4049d0, 404ad0
void rp_line(void *w, int x0, int y0, int x1, int y1, unsigned col);   // FUN_00478b80
void rp_rect(void *page, int x, int y, int w, int h, unsigned col);    // FUN_00480b00
int rp_chip(void *sprite, int target, int x, int y, int clip);        // FUN_00473e60, golf's pictogram or highlight box
int rp_ds(void *w, char *s, int x, int y, char *len);                // FUN_00478140
void rp_ov(int kind, int phase);   // a report golf draws every main-loop frame: 0 before, 1 after
// task-73 (golf.pages.json): the golfer profile page and its nested screens
int rp_tcp(int fn, char *s, int x, int y, unsigned col);   // rp_tc, recorded only on the task-73 pages (FUN_0045b0d0)
void rp_sub(int open);          // the face picker opens (1) and closes (0): the original look shows meanwhile
void rp_edit(void);             // once per iteration of the bio editor's loop
// the land purchase map (FUN_004587a0) and the snapshot popup (FUN_0046e810)
int rp_t7c30(void *w, char *s, int x, int y, int len);            // FUN_00477c30, a string at a place
int rp_t8430(void *w, char *s, int x, int y, int width);          // FUN_00478430, wrapped text
int rp_t8080(void *w, char *s, int x, int y, int a, char *len);   // FUN_00478080, right aligned text
void rp_wfix(int on);           // FUN_00486110 creates its text widget: the create call below gets the parent, not the flags
int rp_warg(int flags, int parent);   // FUN_0047a4c0's FUN_0047abe0 argument
int rp_esc(void);               // Esc was pressed on the native land map
int rp_end(int v);              // the screen is leaving with v: closes the native page, returns v
// task-102 (golf.pages.json): the pro's skill points screen (FUN_0045f0f0) as a native page
void rp_skills(char *name, int points, int who, int x0);   // the screen opens (not for its draw-only call, points -1)
int rp_skills_frosted(void);    // the native page will show it (FUN_004065c0 then leaves out golf's hint box)
void rp_photo(int id, int x, int y);   // task-105: golf pinned accomplishment id's photo at x,y (200x160) on its board
void rp_photo_blit(void *source, int target, int x, int y, int id); // task-105: safe native scratch blit, original browser blit
int rp_photo_limit(int vertical); // task-105: the actual source surface extent less the photo size; original when disabled
void rp_board_wait(int n);      // task-105: the board's waits (FUN_0045bf80(n,0)); the turns' waits go while the native page is up
void rp_year_continue(int y);   // the End of Year page original Continue hit centre
int rp_top_designers(void);     // native Top Ten Designers after the original score insertion
void rp_pair_row(int index);    // task-020: text recorded next belongs to this golfer card
void rp_pair_state(unsigned selected, int cursor, int count, int picked, int style);
int rp_pair_cursor_override(int cursor);
int rp_pair_y_limit(int original_limit, int count);
void rp_pace(void);             // a task-73 page's wait between turns (golf's 50 ms, none while the native page is up)

// task-32 (src/port/golf/frontend_hooks.c, frontend_game.c): the Frosted Mint
// front end. Translator patches (tools/translate/golf.frontend.json) in the
// title menu (FUN_0040f5c0) and the other front-end screens call these; with
// ui.frontend=original (and in the browser build) each one does nothing.
void fe_frame(int screen);      // once per frame of a front-end screen's loop (ui_frontend.h FE_*)
void fe_base(int screen);       // the screen's base picture was just drawn
void fe_context(int n);         // what the caller was doing (the title menu's pick)
// the property screen (FUN_0046f550): the text draws, with the tag index (or garbage outside the tag loop), and what each tag draws
int fe_text(int fn, int tag, const char *text, int x, int y, unsigned colour);   // fn 0..3 = 404b70, 404bc0, 4049d0, 404ad0
void fe_world_loop(int on);     // the loop over the 16 tags starts (1) and ends (0)
void fe_world_tag(int tag, int state, int theme, int owned, int hover, int sprite);
void fe_world_pin(int tag, int x, int y, int state);   // the pin's place on the globe (original 800x600)
void fe_world_hover(int tag);   // the tag the original has highlighted this frame, -1 none
// the list screens (FUN_0043b610 load game / championship courses, FUN_0043a8c0 pro pick, FUN_004725b0 themes)
void fe_rect(int x, int y, int w, int h, unsigned colour);              // FUN_00480b00 on the back buffer
int fe_sprite3(void *self, int p1, int target, unsigned x, unsigned y, int p5);   // FUN_00473f60
int fe_sprite_b(void *self, int target, int x, int y, int a, int b, int c, int d);   // FUN_00473cb0 (the pro's portrait)
void fe_list_vars(int count, int *scroll, int sel, int p1, int p2);
void fe_theme_vars(int n, const unsigned char *mask, int sel);   // the theme packs: rows, content bits per row, golf's current row
void fe_loading_pic(void *sprite);   // task-69: golf's loading picture (FUN_00406250) was just drawn
void fe_loading(int pct, const char *quote);   // task-69: the loading screen's quote is about to be drawn; pct -1 = no bar
void fe_loading_progress(int assets);   // completed FLCs; no artwork reload, random numbers or waits
void fe_suspend(void);          // a dialog of the original is about to run its own loop: show it, not the native screen

// task-27 (src/port/golf/dialogs_hooks.c, dialogs_game.c): the Frosted Mint dialogs. Translator
// patches (port/tools/translate/golf.dialogs.json) in the popup menu (FUN_0046d6e0) and the text
// entry (FUN_0045b2c0) call these; with ui.dialogs=original (and in the browser build) each one
// keeps the original behaviour.
void dlg_pre(void);             // before the caller draws its own strip: the picture to bring back
void dlg_begin(int kind);       // a dialog's loop starts (0 menu, 1 text entry)
int dlg_menu(int px, int py, int sel, int checks, unsigned mask, unsigned disabled, int left, int first_y, int n_items, int width, int height);   // per frame; 1 = close it
int dlg_entry(const char *prompt, const char *text, int caret, int max_chars);   // per frame; 1 = Enter, 2 = Esc
void dlg_hint_pre(void);        // FUN_0040d320 is about to draw a message panel (param_4 -2): keep the picture
void dlg_wait_begin(void);      // the wait owns input before its first event pump
int dlg_wait(void);             // per iteration of golf's wait for a click or key (FUN_0045c0c0)
void dlg_hint_end(void);        // that wait ends
void dlg_profile_tick(void);    // per frame of the golfer profile page (FUN_004385d0): Esc presses its OK disc
int dlg_hire(int hover);        // per frame in the hire panel (FUN_00459400); 1 = close it
unsigned dlg_done(unsigned v);  // the dialog returns v

#endif  // SIMGOLF_PORT_GHIDRA_PRELUDE_H
