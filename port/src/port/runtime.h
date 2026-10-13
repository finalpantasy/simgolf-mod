// Runtime support shared by translated code and the platform layer.
//
// Memory layout (wasm32 linear memory):
//   0x00400000-0x00845000  golf.exe image at its original addresses
//   0x10000000-0x1013a000  jgld.dll (graphsy) at its image base
//   0x11000000             Emscripten static data (GLOBAL_BASE = 272 MB),
//                          then the stack and the malloc heap. Untouched
//                          pages below it cost address space, not memory.
//   0xF0000000+            fake code addresses for native functions that
//                          translated code reaches through icall

#ifndef SIMGOLF_PORT_RUNTIME_H
#define SIMGOLF_PORT_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_GOLF_BASE 0x00400000u
#define PORT_GOLF_LIMIT 0x00900000u
#define PORT_FAKE_CODE_BASE 0xF0000000u

#ifndef ICALL_PARAMS
#define ICALL_PARAMS uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11, uint32_t a12, uint32_t a13, uint32_t a14, uint32_t a15
typedef uint32_t (*IcallFn)(ICALL_PARAMS);
typedef struct { uint32_t addr; IcallFn fn; uint32_t flags; } IcallEntry;
IcallFn icall_lookup(uint32_t addr);
IcallFn icall_find(uint32_t addr, uint32_t *flags);   // NULL if unknown
extern double g_fret;
extern uint32_t g_edx;
#endif

void icall_register(uint32_t addr, IcallFn fn);
void icall_register_flags(uint32_t addr, IcallFn fn, uint32_t flags);
void icall_register_table(const IcallEntry *t, unsigned n);
// Swaps the function at addr for fn; returns the old one (NULL if unknown).
IcallFn icall_replace(uint32_t addr, IcallFn fn, uint32_t *flags);
// Gives a native function a fake code address that translated code can store
// and call through icall.
uint32_t icall_register_native(IcallFn fn);

int port_load_image(const char *path, uint32_t base, uint32_t limit);
// A serializer's validation or short write must survive until its final close.
void crt_io_failed(uint32_t fd);
// Milliseconds, sub-millisecond resolution (emscripten_get_now in the browser).
double port_now_ms(void);
// SIMGOLF_SEED for golf's random numbers, else clock (runtime.c, task-38)
uint32_t port_seed(uint32_t clock);
// The browser build's absolute FS paths for files packaged with the port
// ("/image/golf.bin", "/fonts/x.ttf"); the native build keeps them next to the
// exe (host_win32.c).
#ifdef __EMSCRIPTEN__
#define port_host_path(p) (p)
#else
const char *host_data_path(const char *virt);
#define port_host_path(p) host_data_path(p)
#endif
void port_backtrace(void);
void port_abort(const char *why) __attribute__((noreturn));

// Development chatter (API calls, fonts, text, perf). Off when the page sets
// Module.quiet, as the GitHub Pages build does.
extern int port_quiet;
#define PORT_LOG(...) do { if (!port_quiet) fprintf(stderr, __VA_ARGS__); } while (0)
void port_halt(const char *fn, const char *why);
void port_unimplemented(const char *name);
void port_trace_dump(int n);

// Call tracer for chosen functions (task-113, the debug overlay): see PORT_TRACE in ghidra_prelude.h.
#define PORT_TRACE_MAX 48       // functions traced at once
#define PORT_CALLLOG_N 512      // calls kept
typedef struct { uint32_t addr, frame, after; } PortCallLog;   // after: the function entered just before it
extern PortCallLog port_calllog[PORT_CALLLOG_N];
extern volatile uint32_t port_calllog_pos;
extern uint32_t port_trace_frame;
extern uint32_t port_trace_buf[1024];
extern uint32_t port_trace_pos;
void port_trace_select(uint32_t addr, int on);
void port_trace_clear(void);
int port_trace_selected(int i, uint32_t *addr, uint32_t *calls);   // the i-th traced function and its calls

// Frontend modal hooks called by translator patches (native and browser).
void fe_world_end(void);
int fe_theme_key_ready(void);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_RUNTIME_H
