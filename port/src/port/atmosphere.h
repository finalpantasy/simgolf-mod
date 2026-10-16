#ifndef SIMGOLF_ATMOSPHERE_H
#define SIMGOLF_ATMOSPHERE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Pure presentation model. No game RNG, live memory, simulation or save writes.
typedef struct {
  double mist, haze, gust, golden, shadow_opacity, puddle_drain, front_lead, distant_gain;
} AtmosphereTuning;
typedef struct {
  double mist, haze, gust, drift, golden;
} AtmosphereSample;
AtmosphereTuning atmosphere_defaults(void);
int atmosphere_load(AtmosphereTuning *tuning,const char *path);
AtmosphereSample atmosphere_sample(const AtmosphereTuning *tuning,double seconds,double hour,
  double celsius,double humidity,double cloud,double rain,double wind,uint32_t seed);
// Local retaining potential: concave ground fills first, exposed slopes drain.
double atmosphere_basin(const float *heights,int width,int height,double x,double z);
// Rasterize retaining potential over the existing visible ground mask only.
// verts:8floats(x,y-up,...), world:3floats, batches:5ints(first,count,...,flags).
void atmosphere_receivers(uint8_t *mask,int width,int height,const float *verts,const float *world,
  const int32_t *batches,int nbatches,int vertex_count,int target_w,int target_h,
  const float *heights,int map_w,int map_h);
#ifdef __cplusplus
}
#endif
#endif
