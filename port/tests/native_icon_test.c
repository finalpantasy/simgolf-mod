/* Real SDL/Win32 icon acceptance. Creates only hidden, silent test windows. */
#define SDL_MAIN_HANDLED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/app_icon.h"

#define REQUIRE(condition, message) do { \
  if (!(condition)) { fprintf(stderr, "%s (SDL: %s; Win32: %lu)\n", \
      message, SDL_GetError(), GetLastError()); exit(1); } \
} while (0)

static uint32_t *icon_pixels(HICON icon, int width, int height) {
  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof bmi);
  bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  HDC dc = CreateCompatibleDC(NULL);
  REQUIRE(dc != NULL, "CreateCompatibleDC failed");
  void *pixels = NULL;
  HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &pixels, NULL, 0);
  REQUIRE(bitmap != NULL && pixels != NULL, "CreateDIBSection failed");
  HGDIOBJ old = SelectObject(dc, bitmap);
  memset(pixels, 0x5a, (size_t)width * height * 4);
  REQUIRE(DrawIconEx(dc, 0, 0, icon, width, height, 0, NULL, DI_NORMAL), "DrawIconEx failed");
  GdiFlush();
  uint32_t *copy = malloc((size_t)width * height * 4);
  REQUIRE(copy != NULL, "pixel copy allocation failed");
  memcpy(copy, pixels, (size_t)width * height * 4);
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return copy;
}

static int matching_icon(HWND hwnd, int small) {
  int width = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON);
  int height = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
  HICON actual = (HICON)SendMessage(hwnd, WM_GETICON, small ? ICON_SMALL : ICON_BIG, 0);
  if (!actual)
    actual = (HICON)GetClassLongPtr(hwnd, small ? GCLP_HICONSM : GCLP_HICON);
  REQUIRE(actual != NULL, "SDL window has no effective icon");
  HICON expected = (HICON)LoadImage(GetModuleHandle(NULL),
      MAKEINTRESOURCE(SIMGOLF_APP_ICON_ID), IMAGE_ICON, width, height, 0);
  REQUIRE(expected != NULL, "cannot load expected application icon resource");
  uint32_t *a = icon_pixels(actual, width, height);
  uint32_t *b = icon_pixels(expected, width, height);
  int equal = memcmp(a, b, (size_t)width * height * 4) == 0;
  free(a);
  free(b);
  DestroyIcon(expected);
  return equal;
}

static void check_resource_sizes(void) {
  static const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
  for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    int size = sizes[i];
    HICON icon = (HICON)LoadImage(GetModuleHandle(NULL),
        MAKEINTRESOURCE(SIMGOLF_APP_ICON_ID), IMAGE_ICON, size, size, 0);
    REQUIRE(icon != NULL, "Windows cannot load an advertised icon size");
    ICONINFO info;
    BITMAP bitmap;
    REQUIRE(GetIconInfo(icon, &info), "GetIconInfo failed");
    REQUIRE(info.hbmColor && GetObject(info.hbmColor, sizeof bitmap, &bitmap),
            "icon has no colour bitmap");
    REQUIRE(bitmap.bmWidth == size && bitmap.bmHeight == size,
            "Windows loaded the wrong icon dimensions");
    uint32_t *pixels = icon_pixels(icon, size, size);
    int varied = 0;
    for (int j = 1; j < size * size; j++) varied |= pixels[j] != pixels[0];
    REQUIRE(varied, "icon draws a blank or uniform image");
    free(pixels);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    DestroyIcon(icon);
  }
}

int main(void) {
  REQUIRE(SIMGOLF_APP_ICON_ID == 101, "unexpected application resource ID");
  REQUIRE(FindResource(NULL, MAKEINTRESOURCE(SIMGOLF_APP_ICON_ID), RT_GROUP_ICON) != NULL,
          "test binary lacks the production group icon resource");
  check_resource_sizes();
  /* The Python runner removes inherited icon-hint environment variables. */
  REQUIRE(!SDL_GetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON), "unexpected inherited large icon hint");
  REQUIRE(!SDL_GetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL), "unexpected inherited small icon hint");
  SDL_SetMainReady();
  REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) == 0, "SDL video initialization failed");
  SDL_version version;
  SDL_GetVersion(&version);
  printf("SDL runtime: %u.%u.%u\n", version.major, version.minor, version.patch);
  for (int iteration = 0; iteration < 3; iteration++) {
    SDL_Window *window = SDL_CreateWindow("SimGolf icon regression", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
    REQUIRE(window != NULL, "hidden SDL window creation failed");
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    REQUIRE(SDL_GetWindowWMInfo(window, &info), "SDL window handle lookup failed");
    REQUIRE(info.subsystem == SDL_SYSWM_WINDOWS, "SDL did not create a Windows window");
    REQUIRE(!IsWindowVisible(info.info.win.window), "test window unexpectedly visible");
    int big = matching_icon(info.info.win.window, 0);
    int small = matching_icon(info.info.win.window, 1);
    REQUIRE(big && small, "hidden SDL window icon differs from production resource 101");
    SDL_DestroyWindow(window);
  }
  SDL_Quit();
  puts("PASS: Windows extracts all 8 sizes; SDL selects resource 101 without hints for big/small icons (3 hidden windows)");
  return 0;
}
