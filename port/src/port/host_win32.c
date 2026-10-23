// Native Windows host (see host.h). simgolf mod, task-2.
//
// Address space. The translated game keeps every global at its original
// address, so golf.exe's image must sit at 0x00400000-0x00900000 and jgld.dll's
// at 0x10000000-0x10200000. The exe itself is linked at 0x60000000 and SDL2 is
// delay-loaded. Even so, by the time main() runs the loader has put its heaps
// in the low range (seen every time on Windows 11), so the exe starts itself
// again suspended, reserves both ranges in the new process before its loader
// runs, and waits for it. If the ranges happen to be free, the first process
// commits them and plays without the second one.
//
// Threads. The game runs on one thread with a 64 MB stack (the generated code
// has big frames; the browser build gives it 8 MB of shadow stack plus the
// wasm stack). That thread creates the window, pumps its messages and calls
// WinMain exactly as the original did; the process's first thread only waits.
// One thread for window and game keeps the original's single-threaded model,
// and the bridge (task-13) hooks the same yield points the browser build uses.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/exe_path.h"
#include <timeapi.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/frontend_frosted.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/crash_record.h"
#include "mods/mods.h"

extern int win_closing;   // win32.h (not included: its enum clashes with wingdi.h)

#ifndef SIMGOLF_DEFAULT_GAME
#define SIMGOLF_DEFAULT_GAME "game"
#endif

typedef struct { uintptr_t base, size; const char *what; } Range;
static const Range k_ranges[] = {
  {0x00400000u, 0x00500000u, "golf.exe image"},
  {0x10000000u, 0x00200000u, "jgld.dll image"},
};
#define NRANGES (sizeof k_ranges / sizeof k_ranges[0])

static LARGE_INTEGER g_qpc_freq, g_qpc_start;
static char g_exe_dir[MAX_PATH];
static char g_game_dir[MAX_PATH];
static char g_log_path[MAX_PATH];
static char g_user_dir[MAX_PATH];
static const char *g_script;
static int g_headless, g_mute;
static char g_ini_game[MAX_PATH];
static double g_map_scale = 1;   // golf.ini map_scale= (task-8)
static int (*g_game_main)(void);

static void slashes(char *p) {
  for (; *p; p++)
    if (*p == '\\') *p = '/';
}

double host_now_ms(void) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return (double)(now.QuadPart - g_qpc_start.QuadPart) * 1000.0 / (double)g_qpc_freq.QuadPart;
}

void host_sleep_ms(int ms) { Sleep(ms > 0 ? (DWORD)ms : 0); }

const char *host_game_dir(void) { return g_game_dir; }
const char *host_user_dir(void) { return g_user_dir[0] ? g_user_dir : g_game_dir; }
int host_headless(void) { return g_headless; }
int host_muted(void) { return g_mute; }

const char *host_data_path(const char *virt) {
  static __declspec(thread) char bufs[4][MAX_PATH + 64];
  static __declspec(thread) unsigned next;
  if (!virt || (strncmp(virt, "/image/", 7) && strncmp(virt, "/fonts/", 7) && strncmp(virt, "/ui/", 4))) return virt;
  char *b = bufs[next++ & 3];
  snprintf(b, sizeof bufs[0], "%s%s", g_exe_dir, virt);
  return b;
}

void host_exit(int code) {
  fprintf(stderr, "exit %d after %.1f s\n", code, host_now_ms() / 1000.0);
  fflush(stdout);
  fflush(stderr);
  ExitProcess((UINT)code);
}

void host_fatal(const char *what) {
  fprintf(stderr, "fatal: %s\n", what);
  fflush(stderr);
  if (!g_headless) {
    char msg[1024];
    snprintf(msg, sizeof msg, "SimGolf stopped: %s\n\nDetails are in %s", what, g_log_path);
    MessageBoxA(NULL, msg, "SimGolf", MB_OK | MB_ICONERROR);
  }
  ExitProcess(3);
}

// ------------------------------------------------------------------ address space

static void describe_range(uintptr_t base, uintptr_t size) {
  for (uintptr_t p = base; p < base + size;) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)p, &mi, sizeof mi)) break;
    if (mi.State != MEM_FREE) {
      char name[MAX_PATH] = "";
      if (mi.Type == MEM_IMAGE) GetModuleFileNameA((HMODULE)mi.AllocationBase, name, sizeof name);
      fprintf(stderr, "  %08x-%08x %s %s\n", (unsigned)(uintptr_t)mi.BaseAddress,
              (unsigned)((uintptr_t)mi.BaseAddress + mi.RegionSize),
              mi.State == MEM_RESERVE ? "reserved" : "committed", name);
    }
    p = (uintptr_t)mi.BaseAddress + mi.RegionSize;
  }
}

// Commits [base, base+size). Already reserved (by the parent, see above) is
// fine; anything else in the way is not.
static int commit_range(const Range *r) {
  void *p = VirtualAlloc((void *)r->base, r->size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if (!p) p = VirtualAlloc((void *)r->base, r->size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  return p == (void *)r->base;
}

// Starts this exe again, suspended, with the ranges reserved before its loader
// runs; returns its exit code.
static int relaunch_reserved(void) {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  SetEnvironmentVariableA("SIMGOLF_RELAUNCHED", "1");
  STARTUPINFOW si = {sizeof si};
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(exe, GetCommandLineW(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    return 3;
  // Kill the child with us (task manager, test timeouts).
  HANDLE job = CreateJobObjectA(NULL, NULL);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0};
  // Game children normally stay in the kill-on-close job. The update worker
  // explicitly requests breakaway so it can finish after this launcher exits.
  li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
  AssignProcessToJobObject(job, pi.hProcess);
  int ok = 1;
  for (unsigned i = 0; i < NRANGES; i++)
    if (!VirtualAllocEx(pi.hProcess, (void *)k_ranges[i].base, k_ranges[i].size, MEM_RESERVE, PAGE_EXECUTE_READWRITE))
      ok = 0;
  if (!ok) {
    TerminateProcess(pi.hProcess, 3);
    return 3;
  }
  ResumeThread(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 3;
  GetExitCodeProcess(pi.hProcess, &code);
  return (int)code;
}

// ------------------------------------------------------------------ crashes

// Wild accesses. In the browser build every address below the wasm memory
// size is ordinary zero-filled memory, and the translated game depends on it:
// some decompiled functions index tables with stack slots Ghidra mis-tracked
// (FUN_00462020, the course-site screen, reads 0x81ca6c + garbage * 0x58).
// The original never made those reads. Natively such an address is usually
// unmapped, so the game thread gets the browser's behaviour: a zero page is
// committed there and the instruction runs again. Only free address space in
// the low 2 GB qualifies (never the null page, stacks, guard pages or another
// module's reservations), and every case is logged.
static DWORD g_game_thread_id;
static volatile LONG g_wild_pages;

static LONG WINAPI on_wild_access(EXCEPTION_POINTERS *ep) {
  EXCEPTION_RECORD *er = ep->ExceptionRecord;
  if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
  if (GetCurrentThreadId() != g_game_thread_id) return EXCEPTION_CONTINUE_SEARCH;
  uintptr_t addr = er->ExceptionInformation[1];
  if (er->ExceptionInformation[0] == 8 || addr < 0x10000 || addr >= 0x7fff0000u) return EXCEPTION_CONTINUE_SEARCH;
  MEMORY_BASIC_INFORMATION mi;
  if (!VirtualQuery((void *)addr, &mi, sizeof mi) || mi.State != MEM_FREE) return EXCEPTION_CONTINUE_SEARCH;
  void *page = (void *)(addr & ~(uintptr_t)0xffff);   // allocation granularity
  if (!VirtualAlloc(page, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return EXCEPTION_CONTINUE_SEARCH;
  LONG n = InterlockedIncrement(&g_wild_pages);
  if (n <= 32) {
    fprintf(stderr, "memory: the game %s unmapped %08x (code %08x); zero page committed as in the browser build\n",
            er->ExceptionInformation[0] ? "wrote" : "read", (unsigned)addr, (unsigned)(uintptr_t)er->ExceptionAddress);
    port_trace_dump(16);
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *ep) {
  host_write_crash_record(g_log_path, ep);
  EXCEPTION_RECORD *er = ep->ExceptionRecord;
  fprintf(stderr, "crash: exception %08lx at %08x", er->ExceptionCode, (unsigned)(uintptr_t)er->ExceptionAddress);
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
    fprintf(stderr, " (%s %08x)", er->ExceptionInformation[0] ? "write" : "read",
            (unsigned)er->ExceptionInformation[1]);
  fprintf(stderr, "\n");
  port_trace_dump(64);
  // The game's own teardown after the player quit reads through stale
  // objects (FUN_00402870 with this=1); the browser build reads zeros there
  // and the tab simply stays on the last frame. The player asked to quit, so
  // quit quietly.
  if (win_closing) {
    fprintf(stderr, "host: that was during shutdown; exiting normally\n");
    host_exit(0);
  }
  host_fatal("the game crashed");
}

// ------------------------------------------------------------------ setup

static int dir_exists(const char *p) {
  DWORD a = GetFileAttributesA(p);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void trim(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

// golf.ini next to the exe: "game=<folder>" and "mute=1" (no audio device).
static void read_ini(void) {
  char ini[MAX_PATH + 16], line[MAX_PATH + 16];
  snprintf(ini, sizeof ini, "%s/golf.ini", g_exe_dir);
  FILE *f = fopen(ini, "r");
  while (f && fgets(line, sizeof line, f)) {
    trim(line);
    if (!_strnicmp(line, "game=", 5)) snprintf(g_ini_game, sizeof g_ini_game, "%s", line + 5);
    else if (!_strnicmp(line, "mute=", 5)) g_mute |= atoi(line + 5) != 0;
    else if (!_strnicmp(line, "map_scale=", 10)) g_map_scale = atof(line + 10);
  }
  if (f) fclose(f);
}

const char *host_exe_dir(void) { return g_exe_dir; }
const char *host_log_path(void) { return g_log_path; }

double host_map_scale(void) { return g_map_scale; }

// The game folder: --game, SIMGOLF_GAME, "game=" in golf.ini, a game folder
// next to the exe, then the build's default.
static void pick_game_dir(const char *arg) {
  char ini[MAX_PATH + 16];
  const char *env = getenv("SIMGOLF_GAME");
  g_game_dir[0] = 0;
  if (arg) snprintf(g_game_dir, sizeof g_game_dir, "%s", arg);
  else if (env && *env) snprintf(g_game_dir, sizeof g_game_dir, "%s", env);
  else if (g_ini_game[0]) snprintf(g_game_dir, sizeof g_game_dir, "%s", g_ini_game);
  if (!g_game_dir[0]) {
    snprintf(ini, sizeof ini, "%s/game", g_exe_dir);
    snprintf(g_game_dir, sizeof g_game_dir, "%s", dir_exists(ini) ? ini : SIMGOLF_DEFAULT_GAME);
  }
  char full[MAX_PATH];
  if (GetFullPathNameA(g_game_dir, MAX_PATH, full, NULL)) snprintf(g_game_dir, sizeof g_game_dir, "%s", full);
  slashes(g_game_dir);
  size_t n = strlen(g_game_dir);
  while (n > 3 && g_game_dir[n - 1] == '/') g_game_dir[--n] = 0;
}

static void open_log(const char *arg) {
  if (arg) snprintf(g_log_path, sizeof g_log_path, "%s", arg);
  else snprintf(g_log_path, sizeof g_log_path, "%s/golf.log", g_exe_dir);
  FILE *f = fopen(g_log_path, "w");
  if (f) fclose(f);
  // Both streams append to one file, unbuffered, so a crash loses nothing.
  if (freopen(g_log_path, "a", stderr)) setvbuf(stderr, NULL, _IONBF, 0);
  if (freopen(g_log_path, "a", stdout)) setvbuf(stdout, NULL, _IONBF, 0);
}

static DWORD WINAPI game_thread(void *arg) {
  (void)arg;
  g_game_thread_id = GetCurrentThreadId();
  host_exit(g_game_main());
}

// ------------------------------------------------------------------ freeze watchdog

// Issue 69: a freeze (picture stopped, music still playing) leaves no crash
// record. The main thread only waits for the game thread, so it checks once a
// second. No yields for 20 s (the game thread is stuck) or no frame for 30 s
// while it still yields (a loop that never draws) writes <log>.freeze.txt:
// the game thread's registers, stack and modules as in a crash record, then
// where the game and render threads are, sampled 16 times. Diagnostic only:
// the game keeps running. [fixes] freeze_report=0 turns it off.
extern unsigned g_presents;          // gdi.c
extern DWORD g_hd_render_thread_id;  // hd_native.c
unsigned win_yield_count(void);      // win32.c
int bridge_paused(void);             // bridge.c

typedef struct { DWORD eip, frames[12]; int n; } FreezeSample;

// Only system calls while the thread is suspended: it may hold the heap,
// CRT or loader lock, so nothing here allocates, prints or loads.
static int freeze_sample(HANDLE th, CONTEXT *out, FreezeSample *s) {
  if (SuspendThread(th) == (DWORD)-1) return 0;
  CONTEXT c;
  memset(&c, 0, sizeof c);
  c.ContextFlags = CONTEXT_FULL;
  int ok = GetThreadContext(th, &c);
  if (ok) {
    s->eip = c.Eip;
    s->n = 0;
    DWORD ebp = c.Ebp;
    while (s->n < 12 && ebp) {   // the EBP chain; a bad frame fails the read, not the game
      DWORD pair[2];
      SIZE_T got = 0;
      if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)ebp, pair, sizeof pair, &got) ||
          got != sizeof pair)
        break;
      s->frames[s->n++] = pair[1];
      if (pair[0] <= ebp) break;
      ebp = pair[0];
    }
    if (out) *out = c;
  }
  ResumeThread(th);
  return ok;
}

static void freeze_samples(HANDLE f, const char *name, const FreezeSample *s, int n) {
  char line[256];
  DWORD written;
  for (int i = 0; i < n; i++) {
    int k = snprintf(line, sizeof line, "%s %2d: %08lx", name, i, s[i].eip);
    for (int j = 0; j < s[i].n; j++) k += snprintf(line + k, sizeof line - k, " %08lx", s[i].frames[j]);
    k += snprintf(line + k, sizeof line - k, "\r\n");
    WriteFile(f, line, k, &written, NULL);
  }
}

static void freeze_report(const char *why, HANDLE game, unsigned yields, unsigned presents) {
  static int reports;
  if (reports >= 3) return;   // a long freeze is one report, not one a minute
  reports++;
  HANDLE render = g_hd_render_thread_id
    ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_hd_render_thread_id)
    : NULL;
  FreezeSample gs[16], rs[16];
  int gn = 0, rn = 0;
  CONTEXT first;
  for (int i = 0; i < 16; i++) {
    if (freeze_sample(game, gn ? NULL : &first, &gs[gn])) gn++;
    if (render && freeze_sample(render, NULL, &rs[rn])) rn++;
    Sleep(50);
  }
  if (render) CloseHandle(render);
  if (!gn) return;
  EXCEPTION_RECORD er;
  memset(&er, 0, sizeof er);
  er.ExceptionAddress = (void *)(uintptr_t)first.Eip;
  EXCEPTION_POINTERS ep = {&er, &first};
  host_write_thread_record(g_log_path, ".freeze.txt", &ep, g_game_thread_id);
  char path[MAX_PATH + 32], line[256];
  snprintf(path, sizeof path, "%s.freeze.txt", g_log_path);
  HANDLE f = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD written;
    int k = snprintf(line, sizeof line, "freeze: %s (%u yields, %u frames)\r\nrender thread %lu\r\n",
                     why, yields, presents, g_hd_render_thread_id);
    WriteFile(f, line, k, &written, NULL);
    freeze_samples(f, "game", gs, gn);
    freeze_samples(f, "render", rs, rn);
    FlushFileBuffers(f);
    CloseHandle(f);
  }
  // Last: stderr's lock may belong to the stuck thread. The file is complete.
  fprintf(stderr, "freeze: %s; details in %s\n", why, path);
}

static void watch_game(HANDLE game) {
  int on = mods_get_int("fixes.freeze_report", 1);
  unsigned yields = win_yield_count(), presents = g_presents;
  int still = 0, dark = 0, armed = 1;
  while (WaitForSingleObject(game, 1000) == WAIT_TIMEOUT) {
    unsigned y = win_yield_count(), p = g_presents;
    if (!on || win_closing || bridge_paused()) still = dark = 0;
    else {
      still = y == yields ? still + 1 : 0;
      dark = p == presents ? dark + 1 : 0;
    }
    if (!still && !dark) armed = 1;   // progress again: a later freeze gets its own report
    if (armed && still >= 20) {
      armed = 0;
      freeze_report("the game thread made no progress for 20 s", game, y, p);
    } else if (armed && dark >= 30) {
      armed = 0;
      freeze_report("the game drew no frame for 30 s while still running", game, y, p);
    }
    yields = y;
    presents = p;
  }
}

int host_main(int argc, char **argv, int (*game_main)(void)) {
  QueryPerformanceFrequency(&g_qpc_freq);
  QueryPerformanceCounter(&g_qpc_start);
  _set_fmode(_O_BINARY);
  g_game_main = game_main;

  const char *game_arg = NULL, *log_arg = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--game") && i + 1 < argc) game_arg = argv[++i];
    else if (!strcmp(argv[i], "--script") && i + 1 < argc) g_script = argv[++i];
    else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_arg = argv[++i];
    else if (!strcmp(argv[i], "--headless")) g_headless = 1;
    else if (!strcmp(argv[i], "--mute")) g_mute = 1;
  }
  const char *hl = getenv("SIMGOLF_HEADLESS"), *mu = getenv("SIMGOLF_MUTE");
  if (hl && *hl == '1') g_headless = 1;
  if (mu && *mu == '1') g_mute = 1;
  // Scripted runs are always silent on the speakers (the maintainer, 2026-10-09).
  if (g_headless || g_script) g_mute = 1;

  // The image ranges first: nothing below may allocate before this.
  int committed = 1;
  for (unsigned i = 0; i < NRANGES; i++)
    if (!commit_range(&k_ranges[i])) committed = 0;
  if (!committed && !getenv("SIMGOLF_RELAUNCHED")) return relaunch_reserved();

  exe_path_ansi(g_exe_dir, MAX_PATH);
  slashes(g_exe_dir);
  char *slash = strrchr(g_exe_dir, '/');
  if (slash) *slash = 0;
  SetCurrentDirectoryA(g_exe_dir);   // relative paths in golf.ini (game=, mods_dir=, ...) mean "next to golf.exe"
  open_log(log_arg);
  fprintf(stderr, "SimGolf native (port build %s %s)\n", __DATE__, __TIME__);
  if (getenv("SIMGOLF_RELAUNCHED")) fprintf(stderr, "host: relaunched with the image ranges reserved\n");
  if (!committed) {
    for (unsigned i = 0; i < NRANGES; i++) {
      fprintf(stderr, "host: %s range %08x-%08x:\n", k_ranges[i].what, (unsigned)k_ranges[i].base,
              (unsigned)(k_ranges[i].base + k_ranges[i].size));
      describe_range(k_ranges[i].base, k_ranges[i].size);
    }
    host_fatal("the game's memory ranges are in use");
  }
  fprintf(stderr, "host: image ranges committed (golf 00400000, jgld 10000000)\n");

  read_ini();
  pick_game_dir(game_arg);
  fprintf(stderr, "host: game folder %s\n", g_game_dir);
  if (g_mute) fprintf(stderr, "host: muted, the mix goes to SDL's dummy audio driver (no speakers)\n");
  if (!dir_exists(g_game_dir)) host_fatal("the SimGolf game folder was not found (set game= in golf.ini)");
  mods_init(argc, argv);   // mod platform (task-11): golf.ini, mods.ini, load order
  SetCurrentDirectoryA(g_game_dir);
  if (g_script) harness_load(g_script);
  const char *ud = getenv("SIMGOLF_USER_DIR");
  if (ud && *ud) {
    char full[MAX_PATH];
    if (GetFullPathNameA(ud, MAX_PATH, full, NULL)) snprintf(g_user_dir, sizeof g_user_dir, "%s", full);
    slashes(g_user_dir);
    CreateDirectoryA(g_user_dir, NULL);
    fprintf(stderr, "host: saves and snapshots in %s\n", g_user_dir);
  }
  bridge_init(argc, argv);

  timeBeginPeriod(1);   // Sleep(1) sleeps 1 ms, not a 15.6 ms tick
  SetUnhandledExceptionFilter(on_crash);
  AddVectoredExceptionHandler(1, on_wild_access);
  HANDLE t = CreateThread(NULL, 64u << 20, game_thread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
  if (!t) host_fatal("cannot start the game thread");
  watch_game(t);
  return 0;
}

// ------------------------------------------------------------------ browser-only pieces

// hd_present (hd.c's WebGL2 upscaler in the browser) is hd_native.c here.

// The Bink intro and closing videos: the browser build plays WebM conversions
// in a <video> element; the native build skips them for now.
void port_play_game_video(const char *dospath, int background) {
  (void)background;
  if (strstr(dospath, "losing")) fef_release_cover();   // task-106: the exit: the credits follow, the front end steps aside
  fprintf(stderr, "video: %s skipped (no native video player yet)\n", dospath);
}
