// Rebuild the Windows icon from the existing Frosted Mint golf-ball glyph.
// See art/manifests/app-icon.md for provenance and the reproducible command.
#include "port/ui/ui_icons.h"
#include "port/ui/ui_png.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
static void u16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void u32(FILE *f, uint32_t v) { u16(f, v & 65535); u16(f, v >> 16); }
static unsigned dib_bytes(int n) { return 40 + n * n * 4 + ((n + 31) / 32) * 4 * n; }
static float clamp01(float v) { return fmaxf(0, fminf(1, v)); }

static uint8_t *pixels(int n) {
  int glyph = (int)(n * .82f), offset = (n - glyph) / 2;
  uint8_t *alpha = calloc((size_t)glyph * glyph, 1);
  uint8_t *rgba = calloc((size_t)n * n, 4);
  if (!alpha || !rgba) { free(alpha); free(rgba); return NULL; }
  if (!ui_icon_raster(UI_ICON_GOLF_BALL, glyph, 1.8f, alpha)) {
    free(alpha); free(rgba); return NULL;
  }
  // Existing theme tokens: accent.color #0E9F6E and text.on_accent #FFFFFF.
  const int bg[3] = {14, 159, 110};
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
    float radius = n * .22f, half = n * .47f;
    float qx = fabsf(x + .5f - n / 2.0f) - (half - radius);
    float qy = fabsf(y + .5f - n / 2.0f) - (half - radius);
    float dx = fmaxf(qx, 0), dy = fmaxf(qy, 0);
    float distance = sqrtf(dx * dx + dy * dy) + fminf(fmaxf(qx, qy), 0) - radius;
    float ba = clamp01(.5f - distance), ga = 0;
    int gx = x - offset, gy = y - offset;
    if (gx >= 0 && gy >= 0 && gx < glyph && gy < glyph) ga = alpha[gy * glyph + gx] / 255.0f;
    float a = ga + ba * (1 - ga);
    uint8_t *p = rgba + ((size_t)y * n + x) * 4;
    for (int c = 0; c < 3; c++)
      p[c] = a > 0 ? (uint8_t)((255 * ga + bg[c] * ba * (1 - ga)) / a + .5f) : 0;
    p[3] = (uint8_t)(a * 255 + .5f);
  }
  free(alpha);
  return rgba;
}

static void write_dib(FILE *f, const uint8_t *rgba, int n) {
  unsigned mask_stride = ((n + 31) / 32) * 4;
  u32(f, 40); u32(f, n); u32(f, n * 2); u16(f, 1); u16(f, 32);
  u32(f, 0); u32(f, n * n * 4); u32(f, 0); u32(f, 0); u32(f, 0); u32(f, 0);
  for (int y = n - 1; y >= 0; y--) for (int x = 0; x < n; x++) {
    const uint8_t *p = rgba + ((size_t)y * n + x) * 4;
    fputc(p[2], f); fputc(p[1], f); fputc(p[0], f); fputc(p[3], f);
  }
  for (int y = n - 1; y >= 0; y--) for (unsigned b = 0; b < mask_stride; b++) {
    unsigned bits = 0;
    for (int k = 0; k < 8; k++) {
      unsigned x = b * 8 + k;
      if (x < (unsigned)n && !rgba[((size_t)y * n + x) * 4 + 3]) bits |= 0x80u >> k;
    }
    fputc(bits, f);
  }
}

static int preview(const char *path, const uint8_t *rgba, int n) {
  uint32_t *rgb = malloc((size_t)n * n * sizeof *rgb);
  if (!rgb) return 0;
  for (int i = 0; i < n * n; i++) {
    const uint8_t *p = rgba + i * 4;
    unsigned a = p[3], c[3];
    for (int k = 0; k < 3; k++) c[k] = (p[k] * a + 255 * (255 - a) + 127) / 255;
    rgb[i] = 0xff000000u | c[0] << 16 | c[1] << 8 | c[2];
  }
  int ok = ui_png_write(path, rgb, n, n, n);
  free(rgb);
  return ok;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) { fprintf(stderr, "usage: make_app_icon output.ico [preview.png]\n"); return 2; }
  FILE *f = fopen(argv[1], "wb");
  if (!f) { perror(argv[1]); return 1; }
  unsigned count = sizeof sizes / sizeof sizes[0], offset = 6 + 16 * count;
  u16(f, 0); u16(f, 1); u16(f, count);
  for (unsigned i = 0; i < count; i++) {
    int n = sizes[i];
    fputc(n == 256 ? 0 : n, f); fputc(n == 256 ? 0 : n, f); u16(f, 0);
    u16(f, 1); u16(f, 32); u32(f, dib_bytes(n)); u32(f, offset);
    offset += dib_bytes(n);
  }
  for (unsigned i = 0; i < count; i++) {
    int n = sizes[i];
    uint8_t *rgba = pixels(n);
    if (!rgba) { fclose(f); fprintf(stderr, "icon allocation/rasterization failed\n"); return 1; }
    write_dib(f, rgba, n);
    if (argc == 3 && i + 1 == count && !preview(argv[2], rgba, n)) {
      free(rgba); fclose(f); fprintf(stderr, "preview write failed\n"); return 1;
    }
    free(rgba);
  }
  int failed = ferror(f);
  if (fclose(f)) failed = 1;
  if (failed) { fprintf(stderr, "icon write failed\n"); return 1; }
  printf("Wrote %s (%u sizes, %u bytes)\n", argv[1], count, offset);
  return 0;
}
