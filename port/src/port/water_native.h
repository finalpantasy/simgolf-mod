// Realistic water for the native HD presentation (simgolf mod, task-78).
//
// Golf draws water as static tile textures (the shore painted into them) plus
// ripple, wave and sparkle sprites at the game rate. With display.water=hd the
// render thread draws a Direct3D 11 water surface over golf's water triangles
// every presented frame, between the HD terrain and golf's sprites:
//  - the shoreline comes from the painted tile texture (water is turquoise,
//    sand and grass are not), so the beach stays golf's and the water laps it;
//  - depth from the distance to the shore and golf's depth classes, flow along
//    streams from the water levels, turbulence below drops (waterfall pools);
//  - waves: wind-driven detail and swell, flow-mapped along streams, plus a
//    height-field simulation on a world grid (ripples that spread and bounce off
//    the shore: ball splashes, waterfall pools, drops; task-79 rain and wind
//    through water_impulse and water_set_wind);
//  - sky reflection with drifting clouds, sun glints, caustics, foam.
// golf's ripple and wave sprites and its sparkles are not drawn while it is on
// (drawing only; golf's state is untouched). Tuning: ui/water.txt (data/ui).
// display.water=original (or the vanilla profile, --no-mods) is the 2002 water.
#ifndef SIMGOLF_PORT_WATER_NATIVE_H
#define SIMGOLF_PORT_WATER_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Renderer;

// The terrain layer's current scene (hd_terrain_native.c, render thread).
typedef struct {
  const float *verts;      // 8 floats per vertex: x, y (window, y up), r, g, b, a, u, v
  const float *world;      // 3 floats per vertex: Terrain units (100 per tile), y up
  const int32_t *batches;  // 5 ints: first, count, texture index, texture serial, flags (8 = water)
  int nbatches, tw, th;
  long serial;
  float view[6];           // world: toward the viewer, toward the light
  const uint8_t *wcode;    // per map tile: 0 ground, 1 + depth class for water
  const float *wheight;    // per map tile: centre height
  int map_w, map_h;
  long map_serial;
  const float *normals;
  const uint8_t *materials;
  float height_pixels;
} HdtScene;
int hdt_scene(HdtScene *out);

// Render thread.
void water_take_texture(uint32_t serial, int w, int h, const uint8_t *rgba);   // a water tile texture (RGBA)
// Draws the water over the terrain layer just copied to (x, y, w, h) of the
// renderer's current target (SDL logical coordinates). now_ms: the time the
// picture is for.
void water_draw(struct SDL_Renderer *r, int x, int y, int w, int h, double now_ms);
// After the frame is composed (clip recording, water.clip).
void water_after_frame(struct SDL_Renderer *r, int cw, int ch);

// Any thread.
int water_on(void);                                         // display.water=hd and the pass works
void water_impulse(float tx, float ty, float strength, float radius_tiles);   // a disturbance at map tile coords
void water_set_wind(float dir_rad, float speed);            // task-79: direction (map axes), 0..2 (1 = default breeze)

// Game thread: reads the settings (call once per frame).
void water_poll(void);
// golf's drawing hooks (translator patch golf.water.json): 1 = skip golf's own ripple/wave sprite or sparkle.
int water_hide_golf_ripples(void);
void water_ball_splash(float tx, float ty);   // a ball lands in the water at map tile tx, ty: droplets and two small rings, under a second
int water_underwater_tint(float rgb[3], float *alpha);   // hd_art: shoal, rock and coral sprites seen through the water

#ifdef __cplusplus
}
#endif

#endif  // SIMGOLF_PORT_WATER_NATIVE_H
