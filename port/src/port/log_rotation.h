#ifndef SIMGOLF_LOG_ROTATION_H
#define SIMGOLF_LOG_ROTATION_H
#include <windows.h>
#include <stdio.h>

// Keep five prior sessions. On any failure leave the current file intact and
// let the caller append; losing a boundary is preferable to losing evidence.
static int host_rotate_log(const char *path) {
  char from[MAX_PATH + 32], to[MAX_PATH + 32];
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES)
    return GetLastError() == ERROR_FILE_NOT_FOUND;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) return 0;
  for (int i = 4; i >= 0; --i) {
    int a = i ? snprintf(from, sizeof from, "%s.%d", path, i)
              : snprintf(from, sizeof from, "%s", path);
    int b = snprintf(to, sizeof to, "%s.%d", path, i + 1);
    if (a < 0 || a >= (int)sizeof from || b < 0 || b >= (int)sizeof to) return 0;
    if (!MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
      if (GetLastError() != ERROR_FILE_NOT_FOUND) return 0;
    }
  }
  return 1;
}
#endif
