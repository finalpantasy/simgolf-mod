// Where an attached shadow goes (task-112), shared by hd_art.c and its test (tests/native/shadow_rect_test.c).
// The shadow FLC has its own crop and size: the sprite frame (crop cx,cy, size w,h) is drawn at d = x,y,W,H, so
// fx = W / w, fy = H / h; the shadow sits dx = scx - cx, dy = scy - cy from the sprite's corner (mirrored with
// the sprite: flip bit 1 horizontal dx = w - dx - sw, bit 2 vertical dy = h - dy - sh) and is sw * fx by sh * fy.
#pragma once
#include <string.h>
#include <stdint.h>

// entry_key packs file/view/frame in a, texture variant/palette in b.
// Retain one silhouette pose per file/view/alpha variant. Animation and
// recoloring must not churn the bounded silhouette cache.
static inline uint64_t hd_art_shadow_shape_key(uint32_t a,uint32_t b) {
  return (uint64_t)(a&0xffffff00u)<<32 | (b&0xf0000000u);
}

static inline void hd_art_shadow_rect(int cx, int cy, int w, int h, int scx, int scy, int sw, int sh, int flip, const float *d, float *out) {
  if (w <= 0 || h <= 0) {
    memcpy(out, d, sizeof(float) * 4);
    return;
  }
  float fx = d[2] / (float)w, fy = d[3] / (float)h;
  float dx = (float)(scx - cx), dy = (float)(scy - cy);
  if (flip & 1) dx = (float)w - dx - (float)sw;
  if (flip & 2) dy = (float)h - dy - (float)sh;
  out[0] = d[0] + dx * fx;
  out[1] = d[1] + dy * fy;
  out[2] = (float)sw * fx;
  out[3] = (float)sh * fy;
}
