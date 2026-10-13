// Tournament structures temporarily replace terrain. Keep their original cells
// in a CRC-bound sidecar, leaving the original game save layout intact.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "port/grid.h"
#include "port/camera_tiles.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

enum { MAX_CELLS = GRID_MAX_SIDE * GRID_MAX_SIDE };
typedef struct {
  uint16_t x, y, ax, ay, flags;
  uint8_t terrain, decoration, building, height;
  uint16_t reserved;
} CameraCell;
typedef struct {
  char magic[4];
  uint32_t version, w, h, count, stride, save_size, save_crc, payload_crc;
} CameraHeader;
static CameraCell cells[MAX_CELLS], staging[MAX_CELLS];
static uint8_t active[MAX_CELLS], seen[MAX_CELLS], writing[256];

// This module also builds in the browser, which does not link the native mod
// record helper. Keep the checksum independent of that platform layer.
static uint32_t checksum(uint32_t crc, const void *data, size_t n) {
  const uint8_t *p = data; crc = ~crc;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1)); }
  return ~crc;
}
static int file_checksum(const char *path, uint32_t *size, uint32_t *crc) {
  FILE *f = fopen(path, "rb"); if (!f) return 0;
  uint8_t buf[8192]; size_t n; *size = *crc = 0;
  while ((n = fread(buf, 1, sizeof buf, f))) { *size += (uint32_t)n; *crc = checksum(*crc, buf, n); }
  int ok = !ferror(f); if (fclose(f)) ok = 0; return ok;
}

static uint8_t *bytes(uint32_t addr) { return (uint8_t *)grid_translate(addr); }
static uint16_t *flags(void) { return (uint16_t *)grid_translate(0x53caf0); }

void camera_tiles_reset(void) { memset(active, 0, sizeof active); }
void camera_tiles_closed(int fd) { if (fd >= 0 && fd < 256) writing[fd] = 0; }

void camera_tiles_capture(int x, int y, int side) {
  const char *setting = NULL;
#ifndef __EMSCRIPTEN__
  setting = mods_get("gameplay.restore_camera_tiles");
#endif
  if (setting && !atoi(setting)) return;
  if (side < 1 || x < 0 || y < 0 || x + side > MAP_H || y + side > MAP_W) return;
  for (int dx = 0; dx < side; dx++) for (int dy = 0; dy < side; dy++) {
    int i = (x + dx) * MAP_W + y + dy;
    // Placement rejects occupied tiles. Never overwrite the first snapshot.
    if (active[i]) continue;
    cells[i] = (CameraCell){(uint16_t)(x + dx), (uint16_t)(y + dy), (uint16_t)x, (uint16_t)y,
      flags()[i], bytes(0x5722e8)[i], bytes(0x53bbac)[i], bytes(0x56988c)[i],
      bytes(0x5a4998)[(x + dx) * (MAP_W + 1) + y + dy], 0};
    active[i] = 1;
  }
}

int camera_tiles_restore(int x, int y) {
  int restored = 0;
  for (int i = 0; i < MAP_W * MAP_H; i++) {
    CameraCell *c = &cells[i];
    if (!active[i] || c->ax != x || c->ay != y) continue;
    bytes(0x5722e8)[i] = c->terrain;
    bytes(0x53bbac)[i] = c->decoration;
    bytes(0x56988c)[i] = c->building;
    flags()[i] = c->flags;
    bytes(0x5a4998)[c->x * (MAP_W + 1) + c->y] = c->height;
    active[i] = 0;
    restored++;
  }
  if (restored) fprintf(stderr, "camera-tiles: restored %d cells at %d,%d\n", restored, x, y);
  return restored != 0;
}

int camera_tiles_written(int fd, const char *path) {
  if (fd < 0 || fd >= 256 || !writing[fd]) return 1;
  writing[fd] = 0;
  CameraHeader h = {{'S','G','C','T'}, 1, MAP_W, MAP_H, 0, sizeof(CameraCell), 0, 0, 0};
  for (int i = 0; i < MAP_W * MAP_H; i++) if (active[i]) staging[h.count++] = cells[i];
  char side[640], temp[650];
  snprintf(side, sizeof side, "%s.camera-tiles", path);
  snprintf(temp, sizeof temp, "%s.tmp", side);
  if (!file_checksum(path, &h.save_size, &h.save_crc)) return 0;
  h.payload_crc = checksum(0, staging, h.count * sizeof(CameraCell));
  FILE *f = fopen(temp, "wb");
  if (!f) { fprintf(stderr, "camera-tiles: cannot write sidecar\n"); return 0; }
  int ok = fwrite(&h, sizeof h, 1, f) == 1 &&
    fwrite(staging, sizeof(CameraCell), h.count, f) == h.count;
  if (fclose(f)) ok = 0;
  // rename on Windows does not replace an existing destination. Use the same
  // replace operation as the mod sidecar, keeping the previous file on failure.
#ifdef _WIN32
  extern int __stdcall MoveFileExA(const char *, const char *, unsigned long);
  if (ok) ok = MoveFileExA(temp, side, 1) != 0;
#else
  if (ok) ok = rename(temp, side) == 0;
#endif
  if (!ok) { remove(temp); fprintf(stderr, "camera-tiles: sidecar write failed\n"); }
  else fprintf(stderr, "camera-tiles: saved %u cells\n", h.count);
  return ok;
}

void camera_tiles_save_field(int fd, int reading) {
  if (!reading) { if (fd >= 0 && fd < 256) writing[fd] = 1; return; }
  camera_tiles_reset();
  const char *path = crt_real_path((unsigned)fd);
  if (!path) return;
  char side[640]; snprintf(side, sizeof side, "%s.camera-tiles", path);
  FILE *f = fopen(side, "rb");
  if (!f) return; // Legacy saves have no snapshots.
  CameraHeader h;
  uint32_t size = 0, crc = 0;
  int ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "SGCT", 4) &&
    h.version == 1 && h.w == (unsigned)MAP_W && h.h == (unsigned)MAP_H &&
    h.count <= (unsigned)(MAP_W * MAP_H) && h.stride == sizeof(CameraCell);
  if (ok) ok = fread(staging, sizeof(CameraCell), h.count, f) == h.count && fgetc(f) == EOF && !ferror(f);
  fclose(f);
  if (ok) ok = file_checksum(path, &size, &crc) && h.save_size == size && h.save_crc == crc &&
    h.payload_crc == checksum(0, staging, h.count * sizeof(CameraCell));
  memset(seen, 0, sizeof seen);
  for (unsigned j = 0; ok && j < h.count; j++) {
    CameraCell *c = &staging[j];
    ok = c->x < MAP_H && c->y < MAP_W && c->ax < MAP_H && c->ay < MAP_W && !c->reserved;
    if (ok) { int i = c->x * MAP_W + c->y; ok = !seen[i]; seen[i] = 1; }
  }
  if (!ok) { fprintf(stderr, "camera-tiles: rejected stale or invalid sidecar\n"); return; }
  for (unsigned j = 0; j < h.count; j++) {
    CameraCell *c = &staging[j]; int i = c->x * MAP_W + c->y;
    cells[i] = *c; active[i] = 1;
  }
  fprintf(stderr, "camera-tiles: loaded %u cells\n", h.count);
}
