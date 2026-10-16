// Independent MIT implementation of standard normal lighting and heightfield
// horizon tests. OpenSGolf's documented effect selection informed the design;
// no OpenSGolf source bodies were incorporated. See docs/TERRAIN_LIGHTING.md.
#include "platform/terrain_lighting.h"
#include "port/light_transfer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace {
float clamp(float value, float lo, float hi) { return std::max(lo, std::min(value, hi)); }
bool finite3(const float *v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
bool normalize(float *v) {
  if (!finite3(v)) return false;
  float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!(length > 1e-8f) || !std::isfinite(length)) return false;
  for (int i = 0; i < 3; i++) v[i] /= length;
  return true;
}
terrain_light::Config g_config;
unsigned g_revision = 1;
std::vector<float> g_vertices;
std::vector<int32_t> g_batches;
terrain_light::HeightField g_field;
long g_field_serial = 0;
bool g_has_field = false;
unsigned g_bake_revision = 0;
}  // namespace

namespace terrain_light {

bool HeightField::assign(int width, int height, float ox, float oz, float spacing, const float *heights) {
  width_ = height_ = 0;
  heights_.clear();
  baked_.clear();
  if (!heights || width < 2 || height < 2 || width > 513 || height > 513 ||
      !std::isfinite(ox) || !std::isfinite(oz) || !std::isfinite(spacing) || spacing <= 0) return false;
  width_ = width;
  height_ = height;
  x_ = ox;
  z_ = oz;
  spacing_ = spacing;
  heights_.assign(heights, heights + (size_t)width * height);
  return true;
}

bool HeightField::build(const float *world, int nvertices, const int32_t *batches, int nbatches) {
  if (!world || !batches || nvertices < 3 || nbatches < 1) return false;
  float minx = std::numeric_limits<float>::infinity(), minz = minx, maxx = -minx, maxz = -minx;
  for (int k = 0; k < nbatches; k++) {
    const int32_t *b = batches + k * 5;
    if (b[0] < 0 || b[1] < 0 || b[0] > nvertices || b[1] > nvertices - b[0]) return false;
    if (b[4] & 1) continue;  // overlays must not manufacture terrain elevations
    for (int i = b[0]; i < b[0] + b[1]; i++) {
      const float *p = world + (size_t)i * 3;
      if (!finite3(p)) return false;
      minx = std::min(minx, p[0]); maxx = std::max(maxx, p[0]);
      minz = std::min(minz, p[2]); maxz = std::max(maxz, p[2]);
    }
  }
  if (!std::isfinite(minx) || !std::isfinite(minz)) return false;
  const float step = 50;
  minx = std::floor(minx / step) * step;
  minz = std::floor(minz / step) * step;
  float fw = std::ceil((maxx - minx) / step) + 1, fh = std::ceil((maxz - minz) / step) + 1;
  if (fw < 2 || fh < 2 || fw > 513 || fh > 513) return false;
  int w = (int)fw, h = (int)fh;
  std::vector<float> values((size_t)w * h, std::numeric_limits<float>::quiet_NaN());
  for (int k = 0; k < nbatches; k++) {
    const int32_t *b = batches + k * 5;
    if (b[4] & 1) continue;
    for (int i = b[0]; i < b[0] + b[1]; i++) {
      const float *p = world + (size_t)i * 3;
      float gx = (p[0] - minx) / step, gz = (p[2] - minz) / step;
      int x = (int)std::round(gx), z = (int)std::round(gz);
      if (std::fabs(gx - x) > .001f || std::fabs(gz - z) > .001f) continue;
      if (x < 0 || z < 0 || x >= w || z >= h) continue;
      float &height = values[(size_t)z * w + x];
      // At a cliff, the upper lip is a caster. The cliff face keeps its source normal.
      if (!std::isfinite(height) || p[1] > height) height = p[1];
    }
  }
  return assign(w, h, minx, minz, step, values.data());
}

bool HeightField::sample(float x, float z, float &height) const {
  if (!std::isfinite(x) || !std::isfinite(z) || width_ < 2 || height_ < 2) return false;
  float gx = (x - x_) / spacing_, gz = (z - z_) / spacing_;
  if (gx < 0 || gz < 0 || gx > width_ - 1 || gz > height_ - 1) return false;
  int ix = std::min((int)gx, width_ - 2), iz = std::min((int)gz, height_ - 2);
  float u = gx - ix, v = gz - iz;
  float result = 0;
  const float weights[4] = {(1-u)*(1-v), u*(1-v), (1-u)*v, u*v};
  const int indices[4] = {iz*width_+ix, iz*width_+ix+1, (iz+1)*width_+ix, (iz+1)*width_+ix+1};
  for (int i = 0; i < 4; i++) {
    if (weights[i] <= 1e-6f) continue;
    float value = heights_[indices[i]];
    if (!std::isfinite(value)) return false;
    result += weights[i] * value;
  }
  height = result;
  return true;
}

float HeightField::visibility(float x, float y, float z, const float sun_in[3], int quality) const {
  float sun[3] = {sun_in[0], sun_in[1], sun_in[2]};
  if (!finite3(sun) || !normalize(sun) || !std::isfinite(y)) return 1;
  if (sun[1] <= 0) return 0;
  float horizontal = std::sqrt(sun[0]*sun[0] + sun[2]*sun[2]);
  if (horizontal < .0001f) return 1;
  float dx = sun[0] / horizontal, dz = sun[2] / horizontal, slope = sun[1] / horizontal;
  // Quarter-lattice stepping resolves narrow ridges; the range is bounded to
  // eight tiles. Clearance over distance makes the penumbra widen naturally.
  int steps = quality <= 0 ? 24 : quality == 1 ? 48 : 64;
  float stride = 800.0f / steps, result = 1;
  for (int i = 1; i <= steps; i++) {
    float distance = i * stride, terrain = 0;
    if (!sample(x + dx*distance, z + dz*distance, terrain)) break;
    float clearance = y + 1.5f + slope*distance - terrain;
    if (clearance <= 0) return 0;
    result = std::min(result, clearance / (2.0f + distance * .07f));
  }
  return clamp(result, 0, 1);
}

float HeightField::ambient(float x, float y, float z) const {
  if (!std::isfinite(y)) return 1;
  static const float dirs[8][2] = {{1,0},{.7071068f,.7071068f},{0,1},{-.7071068f,.7071068f},
                                  {-1,0},{-.7071068f,-.7071068f},{0,-1},{.7071068f,-.7071068f}};
  float blocked = 0;
  for (const auto &dir : dirs) {
    float horizon = 0;
    for (int i = 1; i <= 4; i++) {
      float distance = spacing_*i, height;
      if (!sample(x+dir[0]*distance, z+dir[1]*distance, height)) break;
      horizon = std::max(horizon, (height-y-1) / distance);
    }
    blocked += horizon / std::sqrt(1+horizon*horizon);
  }
  return clamp(1 - .55f*blocked/8, .45f, 1);
}

bool HeightField::smoothNormal(float x, float z, float out[3]) const {
  float centre, left, right, back, front;
  if (!sample(x,z,centre)) return false;
  bool l = sample(x-spacing_,z,left), r = sample(x+spacing_,z,right);
  bool b = sample(x,z-spacing_,back), f = sample(x,z+spacing_,front);
  if (!(l || r) || !(b || f)) return false;
  if (!l) left = centre;
  if (!r) right = centre;
  if (!b) back = centre;
  if (!f) front = centre;
  out[0] = -(right-left) / (spacing_*(l && r ? 2 : 1));
  out[1] = 1;
  out[2] = -(front-back) / (spacing_*(b && f ? 2 : 1));
  return normalize(out);
}

void HeightField::bake(const Config &config) {
  bool first=baked_.empty();
  bool shadows=config.shadows && (first || !baked_config_.shadows || config.quality!=baked_config_.quality ||
      std::memcmp(config.sun,baked_config_.sun,sizeof config.sun)!=0);
  bool ao=config.occlusion && (first || !baked_config_.occlusion);
  if(!first && !shadows && !ao) { baked_config_=config;return; }
  baked_.resize(heights_.size());
  for (int z=0;z<height_;z++) for (int x=0;x<width_;x++) {
    size_t index=(size_t)z*width_+x;
    Baked &b=baked_[index];
    if(first) { b.n[0]=b.n[1]=b.n[2]=0;b.visibility=b.ambient=1; }
    if (!std::isfinite(heights_[index])) continue;
    float wx=x_+x*spacing_,wz=z_+z*spacing_,y=heights_[index];
    if(first)smoothNormal(wx,wz,b.n);
    if(shadows)b.visibility=visibility(wx,y,wz,config.sun,config.quality);
    if(ao)b.ambient=ambient(wx,y,wz);
  }
  baked_config_=config;
}

bool HeightField::lightingSample(float x,float z,float normal[3],float &visibility_out,float &ambient_out) const {
  if (baked_.empty() || !std::isfinite(x) || !std::isfinite(z)) return false;
  float gx=(x-x_)/spacing_,gz=(z-z_)/spacing_;
  if(gx<0 || gz<0 || gx>width_-1 || gz>height_-1)return false;
  int ix=std::min((int)gx,width_-2),iz=std::min((int)gz,height_-2);
  float u=gx-ix,v=gz-iz;
  const float weights[4]={(1-u)*(1-v),u*(1-v),(1-u)*v,u*v};
  const int indices[4]={iz*width_+ix,iz*width_+ix+1,(iz+1)*width_+ix,(iz+1)*width_+ix+1};
  float n[3]={},shadow=0,ao=0;
  for(int i=0;i<4;i++) {
    if(weights[i]<=1e-6f)continue;
    if(!std::isfinite(heights_[indices[i]]))return false;
    const Baked &b=baked_[indices[i]];
    if(b.n[0]==0 && b.n[1]==0 && b.n[2]==0)return false;
    for(int c=0;c<3;c++)n[c]+=weights[i]*b.n[c];
    shadow+=weights[i]*b.visibility;ao+=weights[i]*b.ambient;
  }
  std::memcpy(normal,n,sizeof n);visibility_out=shadow;ambient_out=ao;
  return true;
}

void shade(const TerrainLightingModel &model, const Config &config, const float source[3],
           float visibility, float ao, float out[3]) {
  float normal[3] = {source[0],source[1],source[2]};
  if (!normalize(normal)) { normal[0]=normal[2]=0; normal[1]=1; }
  float sun[3] = {config.sun[0],config.sun[1],config.sun[2]};
  if (!normalize(sun)) { sun[0]=sun[2]=0; sun[1]=1; }
  float diffuse = std::max(0.0f,normal[0]*sun[0]+normal[1]*sun[1]+normal[2]*sun[2]);
  float half[3] = {sun[0]+model.eye[0],sun[1]+model.eye[1],sun[2]+model.eye[2]};
  float specular = 0;
  if (diffuse > 0 && normalize(half)) {
    float dot = std::max(0.0f,normal[0]*half[0]+normal[1]*half[1]+normal[2]*half[2]);
    specular = std::pow(dot,clamp(model.shininess,0,128));
  }
  float ambient = config.occlusion ? clamp(ao,0,1) : 1;
  // A low sun changes the direction of illumination without making a course
  // unreadable. Bound directional compensation to 2x and redistribute the
  // missing flat-ground irradiance into hemispherical sky light. Twilight
  // transitions from 75% daytime diffuse to a 30% moon/sky diffuse floor.
  float strength = sun[1] > 0 ? clamp(config.strength,0,1) : 0;
  float irradianceGain = 1 / std::max(.5f,sun[1]);
  float twilight = clamp((sun[1]+.12f)/.14f,0,1);
  twilight = twilight*twilight*(3-2*twilight);
  float skyFloor = .30f + .45f*(config.sky_day>=0?clamp(config.sky_day,0,1):twilight);
  float flatDirect = std::max(0.0f,sun[1])*irradianceGain*strength;
  float skyFill = std::max(0.0f,skyFloor-flatDirect);
  float hemisphere = .5f + .5f*std::max(0.0f,normal[1]);
  float direct = strength * (config.shadows ? .38f + .62f*clamp(visibility,0,1) : 1);
  for (int i = 0; i < 3; i++){
    float day=config.sky_day>=0?clamp(config.sky_day,0,1):twilight;
    float skyTint[3]={.62f,.79f,1.0f},bounceTint[3]={.72f,1.0f,.52f};
    float fill=config.ambient_fill*(.018f+.032f*day)*ambient;
    float colorFill=fill*(skyTint[i]*hemisphere+bounceTint[i]*(1-hemisphere)*.6f);
    out[i] = clamp((model.ambient[i]+model.diffuse[i]*skyFill*hemisphere)*ambient +
                   (model.diffuse[i]*diffuse*irradianceGain + model.specular[i]*specular)*direct + colorFill,0,1);
    if(config.linear)out[i]=light_display_encode(out[i]);
  }
}
}  // namespace terrain_light

extern "C" {
static float g_sky_day=-1,g_ambient_fill=0;
static bool g_linear=false;
void terrain_lighting_linear(int enabled){g_linear=enabled!=0;}
void terrain_lighting_ambient(float value){g_ambient_fill=std::isfinite(value)?clamp(value,0,1):0;}
void terrain_lighting_sky(float daylight) {
  g_sky_day=std::isfinite(daylight)&&daylight>=0?std::round(clamp(daylight,0,1)*100)/100:-1;
}
void terrain_lighting_set(int enabled, int shadows, int occlusion, int quality,
                          float x, float y, float z, float strength) {
  terrain_light::Config next;
  next.sky_day=g_sky_day;next.ambient_fill=g_ambient_fill;next.linear=g_linear;
  next.enabled = enabled != 0;
  next.shadows = shadows != 0;
  next.occlusion = occlusion != 0;
  next.quality = std::max(0,std::min(quality,2));
  next.sun[0] = x; next.sun[1] = y; next.sun[2] = z;
  if (!normalize(next.sun) || !std::isfinite(strength)) next.enabled = false;
  for (int i = 0; i < 3; i++) next.sun[i] = std::round(next.sun[i]*64)/64;
  next.strength = std::round(clamp(strength,0,1)*100)/100;
  bool different = next.enabled != g_config.enabled;
  if (next.enabled) different = different || next.shadows != g_config.shadows || next.occlusion != g_config.occlusion ||
      next.linear != g_config.linear || next.quality != g_config.quality || next.strength != g_config.strength || next.sky_day != g_config.sky_day || next.ambient_fill != g_config.ambient_fill ||
      std::memcmp(next.sun,g_config.sun,sizeof next.sun) != 0;
  if (different) { g_config = next; ++g_revision; }
}
unsigned terrain_lighting_revision(void) { return g_revision; }
int terrain_lighting_enabled(void) { return g_config.enabled; }

static int rebuild_impl(const float *verts, const float *world, const float *normals,
                             int nvertices, const int32_t *batches, int nbatches,
                             const TerrainLightingModel *model, long serial) {
  if (!g_config.enabled || !verts || !world || !normals || !model || !batches ||
      nvertices < 3 || nvertices > 750000 || nbatches < 1 || nbatches > 65536 || nbatches > nvertices) return 0;
  for (int k = 0; k < nbatches; k++) {
    const int32_t *b = batches+k*5;
    if (b[0]<0 || b[1]<0 || b[0]>nvertices || b[1]>nvertices-b[0] || b[1]%3) return 0;
  }
  for (int i = 0; i < nvertices; i++) {
    if (!finite3(world+(size_t)i*3) || !finite3(normals+(size_t)i*3)) return 0;
    for (int j=0;j<8;j++) if (!std::isfinite(verts[(size_t)i*8+j])) return 0;
  }
  if (!finite3(model->ambient) || !finite3(model->diffuse) || !finite3(model->specular) ||
      !finite3(model->eye) || !std::isfinite(model->shininess)) return 0;
  if (!g_has_field || g_field_serial != serial) {
    g_field = terrain_light::HeightField();
    g_has_field = g_field.build(world,nvertices,batches,nbatches);
    g_field_serial = serial;
    g_bake_revision = 0;
  }
  if(g_has_field && g_bake_revision!=g_revision) { g_field.bake(g_config);g_bake_revision=g_revision; }
  g_vertices.clear();
  // Reserve once at the strict ceiling, avoiding vector growth above the cap.
  g_vertices.reserve((size_t)750000*8);
  g_batches.assign(batches,batches+(size_t)nbatches*5);
  const int maxDivisions[3] = {2,4,8};
  const float edgeLength[3] = {64,32,16};
  const int budgetDivisions=std::max(1,(int)std::sqrt(750000.0/nvertices));
  for (int k = 0; k < nbatches; k++) {
    const int32_t *b = batches+k*5;
    int start = (int)g_vertices.size()/8;
    g_batches[k*5] = start;
    for (int t=0;t<b[1];t+=3) {
      int first = b[0]+t;
      const float *v = verts+(size_t)first*8, *p=world+(size_t)first*3, *n=normals+(size_t)first*3;
      // Water retains the original mesh/colours for the independent water pass.
      if (b[4]&8) { g_vertices.insert(g_vertices.end(),v,v+24); continue; }
      float length = 0;
      for (int e=0;e<3;e++) {
        int a=e*8, c=((e+1)%3)*8;
        length = std::max(length,std::hypot(v[a]-v[c],v[a+1]-v[c+1]));
      }
      int divisions = (int)std::ceil(std::min(length/edgeLength[g_config.quality],(float)maxDivisions[g_config.quality]));
      divisions = std::max(1,std::min(divisions,budgetDivisions));
      // Geometry decides walls, never the sometimes tilted legacy wall normal.
      float ax=p[3]-p[0], ay=p[4]-p[1], az=p[5]-p[2];
      float bx=p[6]-p[0], by=p[7]-p[1], bz=p[8]-p[2];
      float nx=ay*bz-az*by, ny=az*bx-ax*bz, nz=ax*by-ay*bx;
      bool ground = std::fabs(ny) > .55f*std::sqrt(nx*nx+ny*ny+nz*nz);
      auto emit = [&](int i,int j) {
        float weights[3] = {1-(float)(i+j)/divisions,(float)i/divisions,(float)j/divisions};
        float result[8]={}, position[3]={}, normal[3]={};
        for (int q=0;q<3;q++) {
          for (int c=0;c<8;c++) result[c]+=weights[q]*v[q*8+c];
          for (int c=0;c<3;c++) { position[c]+=weights[q]*p[q*3+c]; normal[c]+=weights[q]*n[q*3+c]; }
        }
        float visibility=1, ao=1;
        if (g_has_field) {
          float smooth[3];
          if(ground) {
            if(g_field.lightingSample(position[0],position[2],smooth,visibility,ao))
              std::memcpy(normal,smooth,sizeof normal);
          } else {
            if (g_config.shadows) visibility=g_field.visibility(position[0],position[1],position[2],g_config.sun,g_config.quality);
            if (g_config.occlusion) ao=g_field.ambient(position[0],position[1],position[2]);
          }
        }
        terrain_light::shade(*model,g_config,normal,visibility,ao,result+2);
        g_vertices.insert(g_vertices.end(),result,result+8);
      };
      for (int j=0;j<divisions;j++) for (int i=0;i<divisions-j;i++) {
        emit(i,j); emit(i+1,j); emit(i,j+1);
        if (i+j+1<divisions) { emit(i+1,j); emit(i+1,j+1); emit(i,j+1); }
      }
    }
    g_batches[k*5+1] = (int)g_vertices.size()/8-start;
  }
  return 1;
}
int terrain_lighting_rebuild(const float *verts, const float *world, const float *normals,
                             int nvertices, const int32_t *batches, int nbatches,
                             const TerrainLightingModel *model, long serial) {
  try { return rebuild_impl(verts,world,normals,nvertices,batches,nbatches,model,serial); }
  catch (...) {
    // Allocation failure must not escape through the C presenter interface.
    g_vertices.clear();g_batches.clear();g_has_field=false;g_bake_revision=0;
    return 0;
  }
}
const float *terrain_lighting_vertices(void) { return g_vertices.data(); }
const int32_t *terrain_lighting_batches(void) { return g_batches.data(); }
int terrain_lighting_vertex_count(void) { return (int)g_vertices.size()/8; }
}  // extern C
