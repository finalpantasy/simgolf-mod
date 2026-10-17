// High-resolution course terrain for the upscaled presentation (src/port/hd.c).
//
// Whenever golf redraws any part of the course (render, stripRender,
// localRender, pathUpdateRender, lowerEdgeCorner), the bridge notes the camera
// here. Before the next present, the whole visible terrain is traversed again
// in Terrain::render's order with the same camera, but recorded instead of
// rasterized: lit, projected triangles in the 1:1 window coordinates, batched by
// texture. hd_web.js replays them on the GPU into a framebuffer at the
// display's resolution. The 800x600 software render is untouched, so the game
// still sees exactly the pixels it drew.

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "platform/terrain.h"
#include "platform/terrain_gl.h"
#include "platform/terrain_lighting.h"
#include "platform/terrain_port.h"
#include "port/path_style_catalog.h"
#ifndef __EMSCRIPTEN__
#include "port/path_styles.h"
#endif

namespace {

enum { kBlend = 1, kTextured = 2, kTexAlpha = 4, kWater = 8 };

struct Batch {
  int32_t first, count;  // vertices
  int32_t tex, serial, flags;
};

bool g_dirty, g_seen, g_scroll_padding;
Tile *g_centre;
float g_rot;
std::vector<tgl::RecordedVertex> g_verts;
std::vector<Batch> g_batches;
std::vector<const tgl::Texture *> g_batchTex;
std::vector<int> g_batchPathStyle;
int g_path_material = -1;
std::vector<float> g_world;   // task-78: x, y, z per vertex, before the modelview
std::vector<float> g_normals;
TerrainLightingModel g_light_model;
float g_height_pixels;
float g_eye[3] = {0, 1, 0}, g_sun[3] = {0, 1, 0};   // world-space toward-viewer and toward-light directions

// Golf's water textures (WaterShallow, WaterMiddle, WaterDeep, WaterShallowDesert): task-78 draws over them.
bool isWater(const tgl::Texture *tex) {
  if (!tex) return false;
  const char *s = tex->src.c_str(), *b = s;
  for (const char *p = s; *p; p++)
    if (*p == '/' || *p == '\\') b = p + 1;
  return (b[0] == 'W' || b[0] == 'w') && (b[1] == 'a' || b[1] == 'A') && (b[2] == 't' || b[2] == 'T') &&
         (b[3] == 'e' || b[3] == 'E') && (b[4] == 'r' || b[4] == 'R');
}

void norm3(float v[3]) {
  float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 0)
    for (int i = 0; i < 3; i++) v[i] /= l;
}

// The eye's +z and light 0 (eye space) in world space, from the modelview in effect.
void noteView() {
  tgl::Context &gl = terrain_gl();
  g_height_pixels=gl.heightPixels();
  const float *m = gl.modelview().m;   // column-major: row r of the 3x3 is m[r], m[4 + r], m[8 + r]
  float e[3] = {m[2], m[6], m[10]};
  norm3(e);
  const float *lp = gl.lights[0].position;
  float l[3];
  for (int c = 0; c < 3; c++) l[c] = m[c * 4 + 0] * lp[0] + m[c * 4 + 1] * lp[1] + m[c * 4 + 2] * lp[2];
  norm3(l);
  memcpy(g_eye, e, sizeof e);
  memcpy(g_sun, l, sizeof l);
  memcpy(g_light_model.eye, e, sizeof e);
  g_light_model.shininess = gl.matShininess;
  for (int i = 0; i < 3; i++) {
    g_light_model.ambient[i] = gl.matAmbient[i] * gl.globalAmbient[i];
    g_light_model.diffuse[i] = g_light_model.specular[i] = 0;
    for (const tgl::Light &light : gl.lights) {
      if (!light.enabled) continue;
      g_light_model.ambient[i] += gl.matAmbient[i] * light.ambient[i];
      g_light_model.diffuse[i] += gl.matDiffuse[i] * light.diffuse[i];
      g_light_model.specular[i] += gl.matSpecular[i] * light.specular[i];
    }
  }
}

void record(void *, const tgl::RecordedVertex v[3], const tgl::Texture *tex, bool blend,
            const float world[9], const float normals[9]) {
  int flags = (blend ? kBlend : 0) | (tex ? kTextured | (tex->hasAlpha ? kTexAlpha : 0) : 0) | (isWater(tex) ? kWater : 0);
  if (g_verts.empty()) noteView();
  int serial = tex ? (int)tex->serial : 0;
  serial = serial * PATH_STYLE_SERIAL_STRIDE + (g_path_material > 0 ? g_path_material : 0);
  if (g_batches.empty() || g_batches.back().serial != serial || g_batches.back().flags != flags) {
    g_batches.push_back({(int32_t)g_verts.size(), 0, (int32_t)g_batchTex.size(), serial, flags});
    g_batchTex.push_back(tex);
    g_batchPathStyle.push_back(g_path_material);
  }
  g_verts.insert(g_verts.end(), v, v + 3);
  g_world.insert(g_world.end(), world, world + 9);
  g_normals.insert(g_normals.end(), normals, normals + 9);
  g_batches.back().count += 3;
}

}  // namespace

extern "C" {

void terrain_hd_path_tile(int x,int y) {
#ifndef __EMSCRIPTEN__
  g_path_material=x<0?-1:path_styles_at(x,y);
#else
  g_path_material=-1;
#endif
}
int terrain_hd_texture_path_style(int i) { return g_batchPathStyle[i]; }

// Called by terrain_bridge.cpp for every call that draws terrain.
void terrain_hd_note(void *centre, float rot) {
  g_seen = g_dirty = true;
  g_centre = (Tile *)centre;
  g_rot = rot;
}

// Any change that invalidates the recorded frame without drawing.
void terrain_hd_invalidate(void) { g_dirty = true; }

// Golf has drawn course terrain at least once (task-3: before that, the
// terrain cache holds no course).
int terrain_hd_seen(void) { return g_seen; }

void terrain_hd_scroll_padding(int on) {
  if (g_scroll_padding != (on != 0)) g_dirty = true;
  g_scroll_padding = on != 0;
}

// Re-records the visible terrain if anything was drawn since the last call.
// Returns 1 when there's a new frame to replay.
int terrain_hd_build(void) {
  if (!g_seen || !g_dirty) return 0;
  g_dirty = false;
  g_verts.clear();
  g_world.clear();
  g_normals.clear();
  g_batches.clear();
  g_batchTex.clear();
  g_batchPathStyle.clear();
  tgl::Context &gl = terrain_gl();
  gl.record_pad_x = g_scroll_padding ? gl.targetWidth() : 0;
  gl.record_pad_y = g_scroll_padding ? gl.targetHeight() : 0;
  gl.setRecorder(record, nullptr, false);
  Terrain::getInstance()->render(g_centre, g_rot);
  gl.setRecorder(nullptr, nullptr, true);
  gl.record_pad_x = gl.record_pad_y = 0;
  return 1;
}

// The recorded frame: 8 floats per vertex (x, y, r, g, b, a, u, v; window
// coordinates with y up) and 5 ints per batch (first, count, texture index,
// texture serial, flags).
const float *terrain_hd_vertices(void) { return (const float *)g_verts.data(); }
int terrain_hd_batch_count(void) { return (int)g_batches.size(); }
const int32_t *terrain_hd_batches(void) { return (const int32_t *)g_batches.data(); }

// task-78: world positions of the recorded vertices (3 floats each, Terrain's
// own units: 100 per tile, y up), and the view: out[0..2] the world direction
// toward the viewer, out[3..5] toward the light.
const float *terrain_hd_world(void) { return g_world.data(); }
const float *terrain_hd_normals(void) { return g_normals.data(); }
void terrain_hd_light_model(TerrainLightingModel *out) { if (out) *out = g_light_model; }
float terrain_hd_height_pixels(void) { return g_height_pixels; }
void terrain_hd_view(float out[6]) {
  memcpy(out, g_eye, sizeof g_eye);
  memcpy(out + 3, g_sun, sizeof g_sun);
}

// Texture of batch texture index `i`: RGBA pixels, size in out[0..1].
const uint8_t *terrain_hd_texture(int i, int32_t *out) {
  const tgl::Texture *t = g_batchTex[i];
  out[0] = t->w;
  out[1] = t->h;
  return t->rgba.data();
}

// The file batch texture index `i` was loaded from ("" if none).
const char *terrain_hd_texture_src(int i) { return g_batchTex[i]->src.c_str(); }
// The size of the 1:1 target the window coordinates refer to.
void terrain_hd_target(int32_t *out) {
  tgl::Context &gl = terrain_gl();
  out[0] = gl.targetWidth();
  out[1] = gl.targetHeight();
}

}  // extern "C"
