// Native Windows host (simgolf mod, task-2). The browser build has none of
// this: Emscripten's runtime and the page play its part there.
//
// host_win32.c owns the process: it commits the address ranges the module
// images need before anything else can map there, picks the game folder, sends
// stdout and stderr to golf.log and runs the game on a thread with a big stack.
// harness.c runs scripted input and screenshots (--script) for the tests.

#ifndef SIMGOLF_PORT_HOST_H
#define SIMGOLF_PORT_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Commits the image ranges, opens the log and runs game_main on the game
// thread; returns the process exit code.
int host_main(int argc, char **argv, int (*game_main)(void));

// Milliseconds since start, sub-millisecond resolution.
double host_now_ms(void);
void host_sleep_ms(int ms);

// The game's install folder (absolute, '/' separated, no trailing slash).
const char *host_game_dir(void);
const char *host_exe_dir(void);   // the folder holding golf.exe (forward slashes)
const char *host_log_path(void);  // golf.log (task-113: the debug overlay's bug report copies it)
// Maps the browser build's absolute FS paths ("/image/golf.bin",
// "/fonts/x.ttf") to files next to the exe; other paths come back unchanged.
// The result lives in one of a few rotating static buffers.
const char *host_data_path(const char *virt);

// Set by --headless or SIMGOLF_HEADLESS=1: never a message box (scripted runs).
int host_headless(void);
// Mute: --mute, SIMGOLF_MUTE=1, mute=1 in golf.ini, and always for --headless
// or --script runs. The mixer still runs (on SDL's dummy audio driver, which
// never touches a sound card) so tests can prove sound without playing it.
int host_muted(void);
// Land area as a multiple of the original 50 x 50 map: map_scale= in golf.ini
// (default 1). port/src/port/golf/grid.c turns it into the grid size.
double host_map_scale(void);
// Shows a fatal error (unless headless) and exits with code 3.
void host_fatal(const char *what) __attribute__((noreturn));
// The game asked to exit (exit(), ExitProcess, WinMain returning).
void host_exit(int code) __attribute__((noreturn));

// Test harness (harness.c): --script <file>.
void harness_load(const char *path);
void harness_tick(void);              // from every yield of the game loop
void harness_note_file(const char *path);   // every game file the FS resolves
int harness_write_bmp(const char *path);    // the current frame, 24-bit BMP

// Game bridge (bridge.c, task-13): --bridge PORT or SIMGOLF_BRIDGE=1.
void bridge_init(int argc, char **argv);
int bridge_enabled(void);
void bridge_tick(void);               // from every yield; holds the game while paused
void bridge_on_present(void);         // every presented frame
// The game's clock (port_now_ms natively): real time unless the bridge
// paused, scaled or turbo-charged it. bridge_wait replaces the game's sleeps.
double bridge_clock_ms(void);
void bridge_set_game_speed(double s);   // the Options page's game speed (1 = the original pace)
double bridge_game_speed(void);        // game thread: current player-selected multiplier
void bridge_wait(int ms);
// Live mod values ("section.key" from mods.ini, changed by tools at run time).
const char *bridge_mod(const char *key);
int bridge_mod_int(const char *key, int def);
unsigned bridge_mods_serial(void);    // changes whenever a value does

// Per-instance user folder (SIMGOLF_USER_DIR): "Saved Games" and "Snapshots"
// live there instead of in the game folder, so parallel games never share saves.
const char *host_user_dir(void);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_HOST_H
