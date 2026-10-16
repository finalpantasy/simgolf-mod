// Optional smooth mesh lighting. Independent implementation, see docs/TERRAIN_LIGHTING.md.
#ifndef SIMGOLF_TERRAIN_LIGHTING_H
#define SIMGOLF_TERRAIN_LIGHTING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float ambient[3], diffuse[3], specular[3];
  float shininess, eye[3];
} TerrainLightingModel;

// Render-thread only. World coordinates, Y up, direction towards the sun.
// Disabled by default. Invalid directions disable the optional path safely.
void terrain_lighting_set(int enabled, int shadows, int occlusion, int quality,
                          float sun_x, float sun_y, float sun_z, float sun_strength);
unsigned terrain_lighting_revision(void);
// Optional sky fill for the next set call. Negative restores legacy automatic fill.
void terrain_lighting_sky(float daylight);
void terrain_lighting_ambient(float strength);
void terrain_lighting_linear(int enabled);
int terrain_lighting_enabled(void);

// Returns 0 on disabled/invalid data, so the caller retains its legacy path.
// Inputs stay owned by the caller; output lives until the next rebuild.
int terrain_lighting_rebuild(const float *vertices, const float *world, const float *normals,
                             int nvertices, const int32_t *batches, int nbatches,
                             const TerrainLightingModel *model, long scene_serial);
const float *terrain_lighting_vertices(void);
const int32_t *terrain_lighting_batches(void);
int terrain_lighting_vertex_count(void);

#ifdef __cplusplus
}

#include <vector>
namespace terrain_light {
struct Config {
  bool enabled = false, shadows = false, occlusion = false, linear = false;
  int quality = 1;
  float sun[3] = {0, 1, 0}, strength = 1;
  float sky_day = -1, ambient_fill = 0;
};

// Unknown cells are open boundaries, never invented occluders. Heights and
// distances use Terrain.dll units (100 per tile).
class HeightField {
public:
  bool assign(int width, int height, float origin_x, float origin_z, float spacing,
              const float *heights);
  bool build(const float *world, int nvertices, const int32_t *batches, int nbatches);
  bool sample(float x, float z, float &height) const;
  float visibility(float x, float y, float z, const float sun[3], int quality) const;
  float ambient(float x, float y, float z) const;
  bool smoothNormal(float x, float z, float normal[3]) const;
  void bake(const Config &config);
  bool lightingSample(float x, float z, float normal[3], float &visibility, float &ambient) const;
private:
  int width_ = 0, height_ = 0;
  float x_ = 0, z_ = 0, spacing_ = 50;
  std::vector<float> heights_;
  struct Baked { float n[3], visibility, ambient; };
  std::vector<Baked> baked_;
  Config baked_config_;
};
void shade(const TerrainLightingModel &model, const Config &config, const float normal[3],
           float visibility, float ambient, float out[3]);
}  // namespace terrain_light
#endif
#endif
