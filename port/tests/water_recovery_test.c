// The runner includes unmodified production queue functions; only allocation
// and D3D texture creation/release are replaced with deterministic fault hooks.
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../src/port/water_native.h"

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
  } \
} while (0)

typedef struct { int id, released; } Resource;
typedef Resource ID3D11Texture2D;
typedef Resource ID3D11ShaderResourceView;
enum { DXGI_FORMAT_R8G8B8A8_UNORM = 28 };
static Resource resources[4096];
static int nresources, releases, gpu_calls, fail_gpu;
static unsigned char uploaded[4096];
static int allocations, fail_malloc, fail_realloc;
typedef int LONG;
#define MAX_PATH 260
#define _stricmp strcasecmp
static int resends;
static unsigned settings_serial = 1;
static const char *water_setting;
static unsigned mods_serial(void) { return settings_serial; }
static const char *mods_get(const char *key) {
  return !strcmp(key, "display.water") ? water_setting : NULL;
}
static int mods_get_int(const char *key, int value) { (void)key; return value; }
static char *test_getenv(const char *name) { (void)name; return NULL; }
static LONG InterlockedExchange(volatile LONG *value, LONG next) {
  LONG old = *value; *value = next; return old;
}
static void splashes(void) {}
void hdt_resend(void) { resends++; }
void water_impulse(float x, float y, float strength, float radius) {
  (void)x; (void)y; (void)strength; (void)radius;
}
void water_ball_splash(float x, float y) { (void)x; (void)y; }

static void *test_malloc(size_t bytes) {
  if (fail_malloc) { fail_malloc--; return NULL; }
  void *result = malloc(bytes);
  if (result) allocations++;
  return result;
}

static void *test_realloc(void *old, size_t bytes) {
  if (fail_realloc) { fail_realloc--; return NULL; }
  int is_new = old == NULL;
  void *result = realloc(old, bytes);
  if (result && is_new) allocations++;
  return result;
}

static void test_free(void *memory) {
  if (memory) allocations--;
  CHECK(allocations >= 0);
  free(memory);
}

static Resource *new_resource(void) {
  CHECK(nresources < (int)(sizeof resources / sizeof resources[0]));
  Resource *resource = &resources[nresources++];
  resource->id = nresources;
  return resource;
}

static void release_resource(Resource *resource) {
  CHECK(resource && !resource->released);
  resource->released = 1;
  releases++;
}

static ID3D11ShaderResourceView *make_tex(int w, int h, int format,
    const void *pixels, int pitch, int mips, ID3D11Texture2D **texture) {
  CHECK(w > 0 && h > 0 && pixels && pitch == w * 4 && mips == 1);
  CHECK(format == DXGI_FORMAT_R8G8B8A8_UNORM && texture);
  CHECK(gpu_calls < (int)(sizeof uploaded / sizeof uploaded[0]));
  uploaded[gpu_calls++] = *(const unsigned char *)pixels;
  if (fail_gpu) { fail_gpu--; return NULL; }
  *texture = new_resource();
  return new_resource();
}

#define malloc test_malloc
#define realloc test_realloc
#define free test_free
#define getenv test_getenv
#define REL(p) do { if (p) { release_resource(p); (p) = NULL; } } while (0)
#include "water_upload.inc"

static void cleanup(void) {
  size_t pending_bytes = 0;
  for (int i = 0; i < W.npend; i++) pending_bytes += W.pend[i].bytes;
  CHECK(W.pend_bytes == pending_bytes);
  CHECK(W.pend_bytes <= kWaterPendingBytes && W.npend <= kWaterMaxPending);
  for (int i = 0; i < W.npend; i++) free(W.pend[i].rgba);
  for (int i = 0; i < W.ntex; i++) { REL(W.tex[i].srv); REL(W.tex[i].tex); }
  free(W.pend);
  free(W.tex);
  CHECK(allocations == 0);
  CHECK(releases == nresources);
}

static void retains_failed_upload(void) {
  uint8_t pixels[4] = {31};
  CHECK(water_take_texture(7, 1, 1, pixels) == 1);
  CHECK(W.npend == 1);
  fail_gpu = 1;
  upload_pending();
  CHECK(W.npend == 1);
  CHECK(W.pend[0].rgba[0] == 31);
  CHECK(find_wtex(7) == NULL);
  upload_pending();
  CHECK(W.npend == 0);
  CHECK(find_wtex(7) && find_wtex(7)->srv && find_wtex(7)->tex);
  CHECK(gpu_calls == 2);
}

static void preserves_existing_texture(void) {
  uint8_t pixels[4] = {41};
  CHECK(water_take_texture(7, 1, 1, pixels) == 1);
  upload_pending();
  WTex old = *find_wtex(7);
  pixels[0] = 42;
  CHECK(water_take_texture(7, 1, 1, pixels) == 1);
  fail_gpu = 1;
  upload_pending();
  CHECK(find_wtex(7)->srv == old.srv && find_wtex(7)->tex == old.tex);
  CHECK(releases == 0 && W.npend == 1);
  upload_pending();
  CHECK(W.npend == 0 && W.ntex == 1);
  CHECK(find_wtex(7)->srv != old.srv && find_wtex(7)->tex != old.tex);
  CHECK(releases == 2 && uploaded[gpu_calls - 1] == 42);
}

static void deduplicates_pending_serial(void) {
  uint8_t first[4] = {1}, latest[8] = {2};
  CHECK(water_take_texture(9, 1, 1, first) == 1);
  CHECK(water_take_texture(9, 2, 1, latest) == 1);
  CHECK(W.npend == 1);
  CHECK(W.pend[0].w == 2 && W.pend[0].rgba[0] == 2);
  upload_pending();
  CHECK(gpu_calls == 1 && uploaded[0] == 2);
}

static void queue_realloc_failure_is_retryable(void) {
  uint8_t pixels[4] = {3};
  fail_realloc = 1;
  CHECK(water_take_texture(1, 1, 1, pixels) == 0);
  CHECK(W.npend == 0 && W.cap_pend == 0 && W.pend == NULL);
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  CHECK(W.npend == 1 && W.pend[0].rgba[0] == 3);
}

static void queue_growth_preserves_existing_data(void) {
  uint8_t pixels[4] = {4};
  water_take_texture(1, 1, 1, pixels);
  int capacity = W.cap_pend;
  for (int i = 1; i < capacity; i++) water_take_texture((uint32_t)i + 1, 1, 1, pixels);
  Pend *old = W.pend;
  fail_realloc = 1;
  CHECK(water_take_texture((uint32_t)capacity + 1, 1, 1, pixels) == 0);
  CHECK(W.npend == capacity && W.cap_pend == capacity && W.pend == old);
  for (int i = 0; i < capacity; i++) CHECK(W.pend[i].rgba[0] == 4);
  CHECK(water_take_texture((uint32_t)capacity + 1, 1, 1, pixels) == 1);
  CHECK(W.npend == capacity + 1);
}

static void pixel_allocation_failure_is_retryable(void) {
  uint8_t pixels[4] = {5};
  fail_malloc = 1;
  CHECK(water_take_texture(1, 1, 1, pixels) == 0);
  CHECK(W.npend == 0);
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  CHECK(W.npend == 1 && W.pend[0].rgba[0] == 5);
}

static void failed_replacement_preserves_pending_data(void) {
  uint8_t pixels[4] = {6}, larger[8] = {7};
  water_take_texture(1, 1, 1, pixels);
  uint8_t *old = W.pend[0].rgba;
  fail_malloc = 1;
  CHECK(water_take_texture(1, 2, 1, larger) == 0);
  CHECK(W.npend == 1 && W.pend[0].rgba == old && old[0] == 6);
  CHECK(W.pend[0].w == 1);
  CHECK(water_take_texture(1, 2, 1, larger) == 1);
  CHECK(W.npend == 1 && W.pend[0].w == 2 && W.pend[0].rgba[0] == 7);
}

static void cache_allocation_failure_preserves_pending_data(void) {
  uint8_t pixels[4] = {8};
  water_take_texture(1, 1, 1, pixels);
  fail_realloc = 1;
  upload_pending();
  CHECK(W.npend == 1 && W.ntex == 0 && W.cap_tex == 0 && W.tex == NULL);
  upload_pending();
  CHECK(W.npend == 0 && W.ntex == 1 && find_wtex(1)->srv);
}

static void upload_work_is_bounded_and_fair(void) {
  uint8_t pixels[4] = {0};
  for (int i = 0; i < 20; i++) {
    pixels[0] = (uint8_t)i;
    water_take_texture((uint32_t)i, 1, 1, pixels);
  }
  fail_gpu = 20;
  upload_pending();
  CHECK(gpu_calls == 20 && W.npend == 20 && W.ntex == 0);
  fail_gpu = 8;
  upload_pending();
  CHECK(gpu_calls == 28 && W.npend == 20 && W.ntex == 0);
  upload_pending();
  CHECK(gpu_calls == 36 && W.npend == 12 && W.ntex == 8);
  CHECK(uploaded[28] == 8 && uploaded[35] == 15);
  upload_pending();
  CHECK(gpu_calls == 44 && W.npend == 4 && W.ntex == 16);
  upload_pending();
  CHECK(gpu_calls == 48 && W.npend == 0 && W.ntex == 20);
}

static void fresh_uploads_are_not_delayed_by_retry_budget(void) {
  uint8_t pixels[4] = {0};
  for (int i = 0; i < 20; i++) {
    pixels[0] = (uint8_t)i;
    CHECK(water_take_texture((uint32_t)i, 1, 1, pixels) == 1);
  }
  fail_gpu = 20;
  upload_pending();
  CHECK(gpu_calls == 20 && W.npend == 20);
  for (int i = 20; i < 40; i++) {
    pixels[0] = (uint8_t)i;
    CHECK(water_take_texture((uint32_t)i, 1, 1, pixels) == 1);
  }
  fail_gpu = 8;
  upload_pending();
  CHECK(gpu_calls == 48 && W.npend == 20 && W.ntex == 20);
  for (int i = 20; i < 40; i++) CHECK(find_wtex((uint32_t)i) != NULL);
  CHECK(uploaded[28] == 20 && uploaded[47] == 39);
}

static void pending_entry_limit_preserves_rejected_payload(void) {
  uint8_t pixels[4] = {9};
  for (int i = 0; i < 256; i++) CHECK(water_take_texture((uint32_t)i, 1, 1, pixels) == 1);
  CHECK(water_take_texture(256, 1, 1, pixels) == 0);
  CHECK(W.npend == 256);
  CHECK(water_take_texture(255, 1, 1, pixels) == 1);
  CHECK(W.npend == 256);
  upload_pending();
  CHECK(water_take_texture(256, 1, 1, pixels) == 1);
  CHECK(W.npend == 1);
}

static void pending_byte_limit_is_bounded(void) {
  // Each upload is 16 MiB: the seventeenth must remain with the caller.
  uint8_t *pixels = calloc(2048 * 2048, 4);
  CHECK(pixels != NULL);
  for (int i = 0; i < 16; i++) CHECK(water_take_texture((uint32_t)i, 2048, 2048, pixels) == 1);
  CHECK(water_take_texture(16, 2048, 2048, pixels) == 0);
  CHECK(W.npend == 16);
  CHECK(W.pend_bytes == kWaterPendingBytes);
  upload_pending();
  CHECK(water_take_texture(16, 2048, 2048, pixels) == 1);
  CHECK(W.npend == 1);
  CHECK(W.pend_bytes == 2048u * 2048 * 4);
  // calloc above bypasses the fault-injection ownership counter.
  allocations++;
  free(pixels);
}

static void invalid_and_oversized_inputs_do_not_allocate(void) {
  uint8_t pixels[4] = {10};
  CHECK(water_take_texture(1, 0, 1, pixels) == 0);
  CHECK(water_take_texture(1, 1, -1, pixels) == 0);
  CHECK(water_take_texture(1, 1, 1, NULL) == 0);
  CHECK(water_take_texture(1, INT_MAX, 1, pixels) == 0);
  CHECK(water_take_texture(1, INT_MAX / 4, INT_MAX, pixels) == 0);
  CHECK(water_take_texture(1, 8192, 8193, pixels) == 0);
  CHECK(W.npend == 0 && W.cap_pend == 0 && W.pend_bytes == 0 && allocations == 0);
}

static void cache_growth_preserves_existing_textures(void) {
  uint8_t pixels[4] = {11};
  CHECK(water_take_texture(0, 1, 1, pixels) == 1);
  upload_pending();
  int capacity = W.cap_tex;
  for (int i = 1; i < capacity; i++) {
    CHECK(water_take_texture((uint32_t)i, 1, 1, pixels) == 1);
    upload_pending();
  }
  WTex *old = W.tex;
  Resource *old_srv = W.tex[0].srv, *old_tex = W.tex[0].tex;
  CHECK(water_take_texture((uint32_t)capacity, 1, 1, pixels) == 1);
  fail_realloc = 1;
  upload_pending();
  CHECK(W.tex == old && W.cap_tex == capacity && W.ntex == capacity);
  CHECK(W.tex[0].srv == old_srv && W.tex[0].tex == old_tex && releases == 0);
  CHECK(W.npend == 1 && gpu_calls == capacity);
  upload_pending();
  CHECK(W.npend == 0 && W.ntex == capacity + 1);
  CHECK(W.tex[0].srv == old_srv && W.tex[0].tex == old_tex && releases == 0);
}

static void repeated_failure_keeps_single_pending_copy(void) {
  uint8_t pixels[4] = {12};
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  fail_gpu = 100;
  for (int i = 0; i < 100; i++) {
    pixels[0] = (uint8_t)i;
    CHECK(water_take_texture(1, 1, 1, pixels) == 1);
    upload_pending();
    CHECK(W.npend == 1 && W.pend_bytes == 4 && W.ntex == 0);
    CHECK(W.pend[0].rgba[0] == i && gpu_calls == i + 1);
  }
  upload_pending();
  CHECK(W.npend == 0 && W.pend_bytes == 0 && W.ntex == 1 && uploaded[100] == 99);
}

static void pending_replacement_accounts_for_resized_pixels(void) {
  uint8_t pixels[16] = {13};
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  CHECK(W.pend_bytes == 4);
  CHECK(water_take_texture(1, 2, 2, pixels) == 1);
  CHECK(W.pend_bytes == 16);
  CHECK(water_take_texture(1, 1, 2, pixels) == 1);
  CHECK(W.pend_bytes == 8 && W.npend == 1);
  upload_pending();
  CHECK(W.npend == 0 && W.pend_bytes == 0);
}

static void disabled_water_does_not_backpressure_terrain(void) {
  uint8_t pixels[4] = {14};
  g_mode = 1;
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  CHECK(W.npend == 1);
  g_mode = 0;
  for (int i = 0; i < 4096; i++) CHECK(water_take_texture((uint32_t)i, 1, 1, pixels) == 1);
  CHECK(W.npend == 0 && W.pend_bytes == 0 && gpu_calls == 0);
}

static void permanent_failure_does_not_backpressure_terrain(void) {
  uint8_t pixels[4] = {15};
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  W.state = -1;
  for (int i = 0; i < 4096; i++) CHECK(water_take_texture((uint32_t)i, 1, 1, pixels) == 1);
  CHECK(W.npend == 0 && W.pend_bytes == 0 && gpu_calls == 0);
}

static void initial_unknown_mode_retains_uploads(void) {
  uint8_t pixels[4] = {16};
  CHECK(g_mode == -1);
  CHECK(water_take_texture(1, 1, 1, pixels) == 1);
  CHECK(W.npend == 1 && W.pend_bytes == 4);
}

static void reenabling_water_resends_discarded_textures(void) {
  water_setting = "original";
  water_poll();
  CHECK(g_mode == 0 && resends == 0);
  water_setting = "hd";
  settings_serial++;
  water_poll();
  CHECK(g_mode == 1 && resends == 1);
  water_poll();
  CHECK(resends == 1);
  settings_serial++;
  water_poll();
  CHECK(resends == 1);
  water_setting = "original";
  settings_serial++;
  water_poll();
  CHECK(g_mode == 0 && resends == 1);
  water_setting = "realistic";
  settings_serial++;
  water_poll();
  CHECK(g_mode == 1 && resends == 2);
}

static const struct { const char *name; void (*run)(void); } cases[] = {
  {"retains_failed_upload", retains_failed_upload},
  {"preserves_existing_texture", preserves_existing_texture},
  {"deduplicates_pending_serial", deduplicates_pending_serial},
  {"queue_realloc_failure_is_retryable", queue_realloc_failure_is_retryable},
  {"queue_growth_preserves_existing_data", queue_growth_preserves_existing_data},
  {"pixel_allocation_failure_is_retryable", pixel_allocation_failure_is_retryable},
  {"failed_replacement_preserves_pending_data", failed_replacement_preserves_pending_data},
  {"cache_allocation_failure_preserves_pending_data", cache_allocation_failure_preserves_pending_data},
  {"upload_work_is_bounded_and_fair", upload_work_is_bounded_and_fair},
  {"fresh_uploads_are_not_delayed_by_retry_budget", fresh_uploads_are_not_delayed_by_retry_budget},
  {"pending_entry_limit_preserves_rejected_payload", pending_entry_limit_preserves_rejected_payload},
  {"pending_byte_limit_is_bounded", pending_byte_limit_is_bounded},
  {"invalid_and_oversized_inputs_do_not_allocate", invalid_and_oversized_inputs_do_not_allocate},
  {"cache_growth_preserves_existing_textures", cache_growth_preserves_existing_textures},
  {"repeated_failure_keeps_single_pending_copy", repeated_failure_keeps_single_pending_copy},
  {"pending_replacement_accounts_for_resized_pixels", pending_replacement_accounts_for_resized_pixels},
  {"disabled_water_does_not_backpressure_terrain", disabled_water_does_not_backpressure_terrain},
  {"permanent_failure_does_not_backpressure_terrain", permanent_failure_does_not_backpressure_terrain},
  {"initial_unknown_mode_retains_uploads", initial_unknown_mode_retains_uploads},
  {"reenabling_water_resends_discarded_textures", reenabling_water_resends_discarded_textures},
};

int main(int argc, char **argv) {
  CHECK(argc == 2);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    if (!strcmp(argv[1], "--list")) puts(cases[i].name);
    else if (!strcmp(argv[1], cases[i].name)) {
      cases[i].run();
      cleanup();
      printf("PASS %s\n", cases[i].name);
      return 0;
    }
  }
  return strcmp(argv[1], "--list") != 0;
}
