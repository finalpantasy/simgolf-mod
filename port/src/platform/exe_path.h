// The running exe's full path in the ANSI code page, which the C runtime and the
// original game's file calls use. GetModuleFileNameA turns characters outside that
// code page into '?' (a user name in another script), which no file call can open;
// the 8.3 short form of the same path is plain ASCII and opens the same files.
#pragma once
#include <windows.h>

static DWORD exe_path_ansi(char *out, DWORD n) {
  wchar_t w[MAX_PATH], s[MAX_PATH];
  DWORD wn = GetModuleFileNameW(NULL, w, MAX_PATH);
  if (wn && wn < MAX_PATH && n) {
    BOOL lossy = FALSE;
    int got = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, out, (int)n, NULL, &lossy);
    if (got > 0 && !lossy) return (DWORD)got - 1;
    DWORD sn = GetShortPathNameW(w, s, MAX_PATH);
    if (sn && sn < MAX_PATH) {
      lossy = FALSE;
      got = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, s, -1, out, (int)n, NULL, &lossy);
      if (got > 0 && !lossy) return (DWORD)got - 1;
    }
  }
  // No short names on this volume: the old behavior is the best left.
  return GetModuleFileNameA(NULL, out, n);
}
