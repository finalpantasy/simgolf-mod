// Fixed relocated storage and versioned extra records. All generated accesses
// are redirected by golf.staff.json; legacy save fields keep their old size.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "port/staff.h"
#include "port/grid.h"
#include "port/runtime.h"
#include "port/camera_tiles.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif

static _Alignas(16) unsigned char pool[STAFF_BYTES];
static unsigned char staged[64 * STAFF_STRIDE];
static int initialized, staged_fd = -1, staged_tag;

uintptr_t staff_base(void) {
  if (!initialized) { memcpy(pool, (void *)0x585850, 64 * STAFF_STRIDE); initialized = 1; }
  return (uintptr_t)pool;
}
uintptr_t staff_translate(uintptr_t addr) {
  return addr >= 0x585850 && addr < 0x586b50 ? staff_base() + addr - 0x585850 : addr;
}
int staff_max(void) {
  int n = STAFF_DEFAULT_MAX;
#ifndef __EMSCRIPTEN__
  const char *v = mods_get("gameplay.staff_max");
  if (v && *v) {
    long configured = strtol(v, NULL, 10);
    n = configured < 1 ? 1 : configured > STAFF_HIRE_MAX ? STAFF_HIRE_MAX : (int)configured;
  }
#endif
  return n < 1 ? 1 : n > STAFF_HIRE_MAX ? STAFF_HIRE_MAX : n;
}
// Staff zone coordinates are bytes, not signed positions. Large maps use
// 128..159; wrapped negative legacy targets stay negative. Save bytes are unchanged.
int staff_tile_coord(int raw) {
#ifndef __EMSCRIPTEN__
  static unsigned seen = ~0u;
  static int enabled = 1;
  unsigned serial = mods_serial();
  if (serial != seen) {
    seen = serial;
    const char *v = mods_get("fixes.staff_tiles");
    enabled = !v || atoi(v) != 0;
  }
  if (!enabled) return (int8_t)raw;
#endif
  unsigned char tile = (unsigned char)raw;
  // Preserve wrapped negative legacy targets beyond every supported map.
  return tile < GRID_MAX_SIDE ? (int)tile : (int8_t)raw;
}
int staff_paid_count(void) {
  const unsigned char *base = (void *)staff_base(); int n = 0;
  for (int i = 0; i < STAFF_CAPACITY; i++) {
    const unsigned char *r = base + i * STAFF_STRIDE; int job = (signed char)r[0x13];
    if (r[0x12] && job < 0 && job != -6) n++;
  }
  return n;
}
int staff_free_slot(int job) {
  unsigned char *base = (void *)staff_base(); job = (signed char)job;
  if (job == -6) return !base[0x12] || (signed char)base[0x13] == -6 ? 0 : -1;
  if (job < 0 && staff_paid_count() >= staff_max()) return -1;
  for (int i = 1; i < STAFF_CAPACITY; i++) if (!base[i * STAFF_STRIDE + 0x12]) return i;
  return -1;
}
int staff_can_hire(void) { return staff_free_slot(-2) >= 0; }

typedef struct { char magic[4]; uint32_t version, count, stride, bytes, crc; } StaffTag;
static uint32_t checksum(const void *data, size_t n) {
  const unsigned char *p = data; uint32_t crc = ~0u;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1)); }
  return ~crc;
}
static int valid_records(const unsigned char *data) {
  for (int i = 0; i < 64; i++) {
    const unsigned char *r = data + i * STAFF_STRIDE;
    int job = (signed char)r[0x13];
    // Inactive bytes include legitimate fired-record history. Nonnegative
    // signed-byte jobs are follower references 0..127, not employee roles.
    if (r[0x12] && job < 0 && (job < -5 || job > -2)) return 0;
  }
  return 1;
}

typedef struct { uint32_t addr, bytes; } SaveField;
#include "port/staff_save_fields.inc"
static uint32_t field_bytes(SaveField f, int w, int h) {
  for (int i = 0; i < grid_count; i++) if (grid_arrays[i].addr == f.addr)
    return (uint32_t)(w + grid_arrays[i].corner) * (h + grid_arrays[i].corner) * grid_arrays[i].elem;
  return f.bytes;
}

int staff_preflight(int fd, int course, int offset) {
  staged_fd = -1; staged_tag = 0;
  const char *path = crt_real_path((unsigned)fd);
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) return 0;
  int ok = fseek(f, 0, SEEK_END) == 0; long end = ftell(f);
  if (offset < 0) offset = crt_tell(fd);
  if (offset < 0 || fseek(f, offset, SEEK_SET)) ok = 0;
  uint32_t head[4]; int w = 50, h = 50, tag = 0;
  if (ok) ok = fread(head, 4, 1, f) == 1;
  if (ok && !memcmp(head, "SGMP", 4)) {
    ok = fread(head + 1, 12, 1, f) == 1;
    if (ok) { w = (int)head[2]; h = (int)head[3]; ok = head[1] == 1 && w >= 50 && h >= 50 && w <= GRID_MAX_SIDE && h <= GRID_MAX_SIDE; }
  } else if (ok) ok = fseek(f, -4, SEEK_CUR) == 0;
  StaffTag t; unsigned char extra[sizeof staged]; memset(extra, 0, sizeof extra);
  if (ok) ok = fread(t.magic, 4, 1, f) == 1;
  if (ok && !memcmp(t.magic, "SGST", 4)) {
    tag = 1;
    ok = fread(&t.version, sizeof t - 4, 1, f) == 1 && t.version == 1 && t.count == 64 &&
      t.stride == STAFF_STRIDE && t.bytes == sizeof extra;
    if (ok) ok = fread(extra, sizeof extra, 1, f) == 1 && t.crc == checksum(extra, sizeof extra) && valid_records(extra);
  } else if (ok) ok = fseek(f, -4, SEEK_CUR) == 0;
  long body = ftell(f); uint32_t bytes = 0, flag_offset = 0, flag_word = 0;
  const SaveField *fields = course ? course_fields : game_fields;
  size_t count = course ? sizeof course_fields / sizeof *fields : sizeof game_fields / sizeof *fields;
  for (size_t i = 0; i < count; i++) { if (fields[i].addr == 0x59e7b8) flag_offset = bytes; bytes += field_bytes(fields[i], w, h); }
  if (ok) ok = body >= 0 && end >= body + bytes && fseek(f, body + flag_offset, SEEK_SET) == 0 && fread(&flag_word, 4, 1, f) == 1;
  if (!course) {
    if (flag_word & 0x40000000) bytes += 0x8c0;
    if (flag_word & 0x200000) bytes += 0xb7c0 + 0x19a28;
  } else bytes += 8; // course trailer written outside the field serializer
  if (ok) ok = end >= body + bytes;
  if (fclose(f)) ok = 0;
  if (!ok) { fprintf(stderr, "staff: rejected invalid or truncated %s save before mutation\n", course ? "course" : "game"); return 0; }
  memcpy(staged, extra, sizeof staged); staged_fd = fd; staged_tag = tag;
  fprintf(stderr, "staff: preflight %s %dx%d %s\n", course ? "course" : "game", w, h, tag ? "SGST" : "legacy");
  return 1;
}

void staff_save_tag(int fd, int reading) {
  if (!reading) {
    unsigned char *extra = (void *)(staff_base() + 64 * STAFF_STRIDE);
    int nonzero = 0; for (size_t i = 0; i < sizeof staged; i++) nonzero |= extra[i];
    if (!nonzero) return;
    if (!valid_records(extra)) {
      fprintf(stderr, "staff: refusing to save invalid extra records\n");
      crt_io_failed(fd);
      return;
    }
    StaffTag t = {{'S','G','S','T'},1,64,STAFF_STRIDE,sizeof staged,checksum(extra,sizeof staged)};
    crt_write(fd, (uint32_t)(uintptr_t)&t, sizeof t);
    crt_write(fd, (uint32_t)(uintptr_t)extra, sizeof staged);
    fprintf(stderr, "staff: saved SGST extra records\n");
    return;
  }
  // Every full-load caller preflights before any course or pool reset. The
  // staged data survives those resets and is committed only by this serializer.
  if (staged_fd != fd) { fprintf(stderr, "staff: serializer lacks a validated load\n"); return; }
  if (staged_tag) crt_lseek(fd, sizeof(StaffTag) + sizeof staged, 1);
  memcpy((void *)(staff_base() + 64 * STAFF_STRIDE), staged, sizeof staged);
  staged_fd = -1;
}
