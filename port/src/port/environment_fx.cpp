// Optional procedural course atmosphere. The Direct3D pass runs inside a
// private D3D11.1 context state so SDL's cached renderer state is untouched.
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#define D3D11_NO_HELPERS

#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <intrin.h>

#include <SDL.h>
#include <SDL_system.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "port/environment_fx.h"

#ifndef D3DCOMPILE_OPTIMIZATION_LEVEL3
#define D3DCOMPILE_OPTIMIZATION_LEVEL3 (1 << 15)
#endif

namespace {

constexpr size_t kMaxReceiverBytes = 16u * 1024u * 1024u;
constexpr int kCloudTextureSize = 96;
constexpr int kMaxRainDrops = 1400;
constexpr int kMaxFallbackBugCells = 4096;

typedef HRESULT(WINAPI *CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
                                   ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
                                   ID3DBlob **, ID3DBlob **);

struct FxConstants {
  float target[4];       // target width, height, inverse width, inverse height
  float clip[4];         // target-space x, y, width, height
  float camera[4];       // camera x/y, output scale, seconds
  float weather[4];      // cloud, rain, storm, wind x
  float sky[4];          // wind y, night, lightning, quality
  float receiver[4];     // origin x/y, source-pixel scale, enabled
  float receiver_size[4];// source width/height, insect activity, night sky
  float surfaces[4];     // wetness, puddles, drying haze, scene copy available
  float impacts[4];      // splash strength, flash exposure, cloud morph rate
  float clouds[4];       // bank size, shade opacity, edge feather, cloud seed24
  float air[4];          // mist, haze, analytic wind displacement x/z
  float air_style[4];    // golden light, integrated advection enabled
  float material_gloss[4],material_power[4],material_absorb[4];
  float light[4];        // world direction and energy of the sun or moon
  float view_material[4];// world eye direction, material wetness strength
  float terrain_style[4];// valley fog strength, reserved
  float flags[4];        // reduced flashes, event low16, strike age, event high16
};

const char kShaderSource[] = R"HLSL(
cbuffer Fx : register(b0) {
  float4 target_size;
  float4 clip_rect;
  float4 camera;
  float4 weather;
  float4 sky;
  float4 receiver;
  float4 receiver_size;
  float4 surfaces;
  float4 impacts;
  float4 clouds;
  float4 air;
  float4 air_style;
  float4 material_gloss,material_power,material_absorb;
  float4 light;
  float4 view_material;
  float4 terrain_style;
  float4 flags;
};
Texture2D<float4> receiver_map : register(t0);
Texture2D<float4> scene_map : register(t1);

float hash21(float2 p) {
  return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

// Large sine arguments lose event variation on D3D hardware. Keep the same
// integer avalanche as the SDL path and convert only the bounded result.
uint event_hash(uint x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

float event_random(uint x) {
  return float(event_hash(x) >> 8) * (1.0 / 16777216.0);
}

float value_noise(float2 p) {
  float2 cell = floor(p);
  float2 f = frac(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash21(cell);
  float b = hash21(cell + float2(1.0, 0.0));
  float c = hash21(cell + float2(0.0, 1.0));
  float d = hash21(cell + float2(1.0, 1.0));
  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float cloud_detail(float2 p) {
  int2 i=(int2)floor(p);float2 f=frac(p);f=f*f*(3-2*f);
  uint a=uint(i.x)*0x85ebca6bu ^ uint(i.y)*0xc2b2ae35u;
  uint b=uint(i.x+1)*0x85ebca6bu ^ uint(i.y)*0xc2b2ae35u;
  uint c=uint(i.x)*0x85ebca6bu ^ uint(i.y+1)*0xc2b2ae35u;
  uint d=uint(i.x+1)*0x85ebca6bu ^ uint(i.y+1)*0xc2b2ae35u;
  return lerp(lerp(event_random(a),event_random(b),f.x),lerp(event_random(c),event_random(d),f.x),f.y);
}
float cloud_shape(float2 p,float phase,uint seed) {
  float2 offset=float2(seed&255u,(seed>>8)&255u)*.173;
  p+=offset;
  float2 warp=float2(cloud_detail(p*.7+float2(phase*.04,0)),
                     cloud_detail(p*.7+float2(41.7,-phase*.03)))-.5;
  float2 q=p+warp*.65;
  // Coherent banks with nested billows and wisps, without geometric stamps.
  float n=cloud_detail(q)*.53;
  n+=cloud_detail(q*2.03+float2(17.3,9.2)+phase*.025)*.27;
  n+=cloud_detail(q*4.11+float2(31.7,19.4)-phase*.02)*.14;
  n+=cloud_detail(q*8.3+float2(57.1,43.7))*.06;
  return saturate(n);
}

float receiver_value(float2 pixel) {
  if (receiver.w < 0.5 || receiver.z <= 0.0) return 0.0;
  int2 q = (int2)floor((pixel - receiver.xy) / receiver.z);
  int2 size = (int2)receiver_size.xy;
  if (any(q < 0) || any(q >= size)) return 0.0;
  return receiver_map.Load(int3(q, 0)).r;
}

// Short, edge-directed spatial resolve. Receiver and clip guards keep UI,
// unknown ownership and the empty background out of every filter footprint.
float3 edge_texel(int2 q, float3 center) {
  float2 p=float2(q)+0.5;
  float v=receiver_value(p);
  if(any(q<0)||any(q>=int2(target_size.xy))||any(p<clip_rect.xy)||
     any(p>=clip_rect.xy+clip_rect.zw)||v<=.15||v>=.68)return center;
  return scene_map.Load(int3(q,0)).rgb;
}
float3 edge_bilinear(float2 p,float3 center) {
  float2 q=p-.5;int2 i=int2(floor(q));float2 f=frac(q);
  return lerp(lerp(edge_texel(i,center),edge_texel(i+int2(1,0),center),f.x),
              lerp(edge_texel(i+int2(0,1),center),edge_texel(i+1,center),f.x),f.y);
}
float edge_luma(float3 c){return dot(c,float3(.299,.587,.114));}
float3 resolve_edge(float2 p,float3 center) {
  if(air_style.z<=.001)return center;
  int2 i=int2(p);
  float nw=edge_luma(edge_texel(i+int2(-1,-1),center));
  float ne=edge_luma(edge_texel(i+int2(1,-1),center));
  float sw=edge_luma(edge_texel(i+int2(-1,1),center));
  float se=edge_luma(edge_texel(i+int2(1,1),center));
  float m=edge_luma(center),lo=min(m,min(min(nw,ne),min(sw,se)));
  float hi=max(m,max(max(nw,ne),max(sw,se)));
  if(hi-lo<max(.04,hi*.16))return center;
  float n=edge_luma(edge_texel(i+int2(0,-1),center));
  float s=edge_luma(edge_texel(i+int2(0,1),center));
  float e=edge_luma(edge_texel(i+int2(1,0),center));
  float w=edge_luma(edge_texel(i+int2(-1,0),center));
  // Keep single-pixel straight marks and isolated highlights from dissolving.
  if(((m-n)*(m-s)>0&&min(abs(m-n),abs(m-s))>.08&&abs(m-e)<.025&&abs(m-w)<.025)||
     ((m-e)*(m-w)>0&&min(abs(m-e),abs(m-w))>.08&&abs(m-n)<.025&&abs(m-s)<.025))return center;
  float2 dir=float2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
  float reduce=max((nw+ne+sw+se)*.03125,.0078125);
  dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),-3.0,3.0);
  float3 a=(edge_bilinear(p-dir/6,center)+edge_bilinear(p+dir/6,center))*.5;
  float3 b=a*.5+(edge_bilinear(p-dir*.5,center)+edge_bilinear(p+dir*.5,center))*.25;
  float l=edge_luma(b);
  return lerp(center,(l<lo||l>hi)?a:b,air_style.z);
}

void composite_layer(inout float3 color, inout float alpha, float3 layer_color, float layer_alpha) {
  layer_alpha = saturate(layer_alpha);
  color = lerp(color, layer_color, layer_alpha / max(alpha + layer_alpha - alpha * layer_alpha, 0.0001));
  alpha = layer_alpha + alpha * (1.0 - layer_alpha);
}

float segment_distance(float2 sample_pos, float2 a, float2 b) {
  float2 ab = b - a;
  float t = saturate(dot(sample_pos - a, ab) / max(dot(ab, ab), 0.0001));
  return length(sample_pos - (a + t * ab));
}

struct VertexOut { float4 position : SV_POSITION; };
VertexOut vs_main(uint id : SV_VertexID) {
  float2 p = id == 0 ? float2(-1.0, -1.0) : id == 1 ? float2(-1.0, 3.0) : float2(3.0, -1.0);
  VertexOut o;
  o.position = float4(p, 0.0, 1.0);
  return o;
}

float3 feature_value(float2 pixel) {
  int2 q=int2(floor((pixel-receiver.xy)/max(receiver.z,.001)));
  if(any(q<0)||any(q>=int2(receiver_size.xy))||receiver.w<.5)return 0;
  return receiver_map.Load(int3(q,0)).gba;
}
float3 feature_normal(float packed) {
  uint v=uint(floor(packed*255+.5));float nx=(float(v&15u)-8)/7,nz=(float(v>>4)-8)/7;
  return normalize(float3(nx,sqrt(max(.05,1-nx*nx-nz*nz)),nz));
}
float4 ps_main(VertexOut input) : SV_TARGET {
  float2 pixel = input.position.xy;
  if (pixel.x < clip_rect.x || pixel.y < clip_rect.y ||
      pixel.x >= clip_rect.x + clip_rect.z || pixel.y >= clip_rect.y + clip_rect.w) discard;

  float quality = clamp(floor(sky.w + 0.5), 1.0, 4.0);
  float time = camera.w;
  float receiver_sample = receiver_value(pixel);
  float is_ground = receiver_sample > 0.35 && receiver_sample < 0.68 ? 1.0 : 0.0;
  float is_scene = receiver_sample > 0.15 && receiver_sample < 0.68 ? 1.0 : 0.0;
  float is_sky = receiver_sample > 0.80 ? 1.0 : 0.0;
  uint mode = uint(floor(flags.x + 0.5));
  bool black_background = (mode & 2u) != 0u;
  float2 local = pixel - clip_rect.xy;
  float2 source_local = local / max(camera.z, 0.25) + camera.xy;
  float night = saturate(sky.y);
  float cloud = saturate(weather.x);
  float rain = saturate(weather.y);
  float storm = saturate(weather.z);

  float3 color = 0.0;
  float alpha = 0.0;
  float water_surface = receiver_sample > 0.57 && receiver_sample < 0.68 ? 1.0 : 0.0;
  float wet = saturate(surfaces.x);
  float3 feature=feature_value(pixel);
  uint packed_material=uint(floor(feature.z*255+.5)),material=packed_material&3u;
  float fog_height=float(packed_material>>2)/63;
  float pool = 0.0;
  if (surfaces.w > 0.5 && is_scene > 0.5) {
    float3 original = resolve_edge(pixel,scene_map.Load(int3(int2(pixel),0)).rgb);
    // Bounded daylight toe lift reveals painted canopy detail without lifting
    // exact black, changing hue, or washing out already bright highlights.
    original=lerp(original,sqrt(max(original,0.0)),terrain_style.y*.42);
    color = original; alpha = 1.0;
    if (wet > 0.001 || surfaces.y > 0.001) {
      float green = saturate((original.g - max(original.r,original.b))*7.0);
      float grain = value_noise(source_local / float2(47.0,31.0));
      float basin=saturate((receiver_sample*255.0-127.0)/16.0);
      pool = smoothstep(0.67,0.88,grain) * saturate(surfaces.y) * is_ground * (1.0-water_surface) * lerp(.3,1.7,basin);
      float absorb = wet * lerp(0.18,0.11,green) * (1.0-water_surface*0.65);
      color *= 1.0-absorb;
      float sky_sheen = smoothstep(0.52,0.84,grain) * wet * (0.035+0.10*(1.0-green));
      float3 reflection = lerp(float3(0.36,0.44,0.51),float3(0.10,0.15,0.23),night);
      color = lerp(color,reflection,pool*0.23);
      color += reflection * sky_sheen * (1.0+water_surface);
      if(view_material.w>.001 && is_ground>.5 && water_surface<.5 && packed_material>0u){
        float3 normal=feature_normal(feature.x);
        float3 eye=normalize(view_material.xyz+float3(0,.00001,0));
        float3 sun=normalize(light.xyz+float3(0,.00001,0)),halfway=normalize(eye+sun+float3(0,.00001,0));
        float exponent=material_power[material];
        float amplitude=material_gloss[material];
        float lobe=pow(saturate(dot(normal,halfway)),exponent)*saturate(light.w)*(1-cloud*.85);
        float grazing=.025+.975*pow(1-saturate(dot(normal,eye)),5);
        float absorb_material=material_absorb[material];
        float3 detailed=original*(1-wet*absorb_material);
        float sand_drain=material==3u?.12:1;
        detailed=lerp(detailed,reflection,pool*.23*sand_drain);
        float3 key_color=lerp(float3(1,.91,.75),float3(.46,.60,.86),night);
        detailed+=wet*amplitude*(key_color*lobe+reflection*grazing*(.45+.55*grain));
        color=lerp(color,detailed,saturate(view_material.w));
      }
    }
  }
  if (surfaces.z > 0.001 && is_ground > 0.5 && water_surface < 0.5) {
    float haze = value_noise(source_local/float2(80.0,17.0)+float2(time*.018,time*.045));
    haze = smoothstep(0.60,0.88,haze) * surfaces.z * 0.55;
    composite_layer(color,alpha,float3(0.64,0.70,0.72),haze);
  }

  if (!black_background && receiver_size.w > 0.5 && is_sky > 0.5 && night > 0.01) {
    float2 moon_center = clip_rect.zw * float2(0.80, 0.18);
    float moon_radius = length(local - moon_center) /
                        max(min(clip_rect.z, clip_rect.w), 1.0);
    float glow = exp(-moon_radius * moon_radius * 170.0) * night * 0.17;
    float disc = (1.0 - smoothstep(0.020, 0.026, moon_radius)) * night * 0.82;
    composite_layer(color, alpha, float3(0.60, 0.72, 0.88), glow);
    composite_layer(color, alpha, float3(0.87, 0.91, 1.00), disc);

    float star_cell = lerp(46.0, 28.0, (quality - 1.0) / 3.0);
    float2 grid = local / star_cell;
    float2 cell = floor(grid);
    float2 within = frac(grid);
    float seed = hash21(cell + float2(31.0, 7.0));
    float2 center = float2(0.14 + 0.72 * hash21(cell), 0.14 + 0.72 * seed);
    float radius = lerp(0.8, 1.8, hash21(cell + 13.0));
    float distance_to_star = length((within - center) * star_cell);
    float twinkle = 0.58 + 0.42 * sin(time * (1.1 + seed * 2.2) + seed * 19.0);
    float star = (1.0 - smoothstep(radius, radius + 1.2, distance_to_star)) * night * twinkle;
    composite_layer(color, alpha, float3(0.76, 0.84, 1.0), star * 0.86);
  }

  if (receiver_size.z > 0.001 && receiver_sample > (126.5/255.0) && receiver_sample < (143.5/255.0) && night > 0.01) {
    float bug_cell = lerp(56.0, 34.0, (quality - 1.0) / 3.0);
    float2 grid = source_local / bug_cell;
    float2 cell = floor(grid);
    float population = hash21(cell + float2(211.0, 79.0));
    if (population < 0.14) {
      float2 center = float2(hash21(cell + 67.0), hash21(cell + 129.0));
      float seed = hash21(cell + float2(67.0, 129.0));
      float phase = seed * 6.2831853 + time * (0.28 + center.x * 0.20);
      float2 drift = float2(sin(phase), cos(phase * 0.83 + center.y)) * float2(0.030, 0.024);
      float distance_to_bug = length(frac(grid) - center - drift);
      float pulse = 0.52 + 0.48 * sin(time * (1.25 + center.x * 1.5) + center.y * 16.0);
      float glow = (1.0 - smoothstep(0.025, 0.095, distance_to_bug)) * receiver_size.z * pulse;
      float core = (1.0 - smoothstep(0.010, 0.030, distance_to_bug)) * receiver_size.z * pulse;
      composite_layer(color, alpha, float3(0.44, 0.68, 0.24), glow * 0.20);
      composite_layer(color, alpha, float3(0.82, 1.0, 0.58), core * 0.82);
    }
  }


  if (cloud > 0.001 && (!black_background || cloud > 0.16)) {
    float2 drift = float2(time*.035+(air_style.y>.5?air.z:time*weather.w)*.025,
                         time*.012+(air_style.y>.5?air.w:time*sky.x)*.018);
    // Enlarge the banks without increasing their world-space travel speed.
    float2 cloud_pos = (source_local / float2(165.0, 115.0) - drift) / clouds.x;
    float formation = cloud_shape(cloud_pos,time*impacts.z,uint(clouds.w));
    float edge = .53-cloud*.08;
    float cover = smoothstep(edge-clouds.z*.5,edge+clouds.z*.5,formation);
    // Firm, broad interiors with a narrow penumbra cross terrain and objects.
    float cloud_receiver = is_ground + (is_scene-is_ground)*0.8;
    float shadow = cover * cloud * smoothstep(.16,.32,cloud) * clouds.y * cloud_receiver;
    composite_layer(color, alpha, float3(0.10,0.14,0.19), shadow);
    float body = black_background ? 0.0 : cover * cloud * (is_sky > 0.5 ? 0.62 : 0.045 + rain*0.025);
    float rim = saturate(formation);
    composite_layer(color, alpha, lerp(float3(0.27,0.32,0.39),float3(0.66,0.71,0.75),rim)*(1.0-storm*0.26), body);
  }

  if ((air.x>.001 || air.y>.001) && is_scene>.5) {
    float2 fp=source_local/float2(210.0,43.0)-float2(time*.018+air.z*.004,time*.007);
    float density=cloud_shape(fp,time*.003,uint(clouds.w)^0xa1e7u);
    float basin=water_surface>.5?1.0:saturate((receiver_sample*255.0-127.0)/16.0);
    float low=lerp(.25,1.0,basin);
    float shape=smoothstep(.28,.70,density);
    float legacy=(air.y*.65+air.x*low*shape)*(is_ground>.5?1.0:.30);
    float valley=feature.y*fog_height;
    float local_fog=air.y*.65*(is_ground>.5?1:.30)+air.x*valley*shape;
    float veil=lerp(legacy,local_fog,saturate(terrain_style.x));
    float3 fog=lerp(float3(.72,.79,.82),float3(.83,.76,.62),air_style.x*1.6);
    fog=lerp(fog,float3(.30,.38,.49),night*.7);
    float rim=4*shape*(1-shape);
    fog*=1+rim*.18*saturate(light.w)*(1-cloud*.85)*saturate(terrain_style.x);
    composite_layer(color,alpha,fog,min(veil,.30));
  }

  if (rain > 0.001 && (!black_background || is_scene > 0.5)) {
    // Slow curtains of rain cross the view independently of falling drops.
    // Keep a light bed in the lulls; intensity still controls population.
    float patch = value_noise(source_local / float2(170.0, 115.0) -
                              float2(time * (0.065 + weather.w*0.025), time*0.018) + 41.7);
    float curtain = lerp(0.22, 1.32, smoothstep(0.20, 0.80, patch));
    float streak = 0.0;
    [unroll] for (int layer=0;layer<3;layer++) {
      if (layer > (int)quality) break;
      float depth = 0.7 + layer*0.43;
      float2 rp = source_local * depth;
      float slant = 0.10 + weather.w*0.20 + sin(time*0.4 + layer)*0.035;
      rp.x -= rp.y*slant;
      rp.x -= (air_style.y>.5?air.z:weather.w*time)*17.0 + sin(time*0.57+layer*2.1)*7.0;
      float2 cell_size=float2(19.0+layer*7.3, 47.0+layer*13.7);
      int column=(int)floor(rp.x/cell_size.x);
      uint column_seed=event_hash(uint(column)*0x85ebca6bu ^ uint(layer)*7919u);
      // Independent column velocities keep tracks coherent across row boundaries.
      // Random per-drop velocity in this inverse grid would make drops teleport.
      rp.y -= time*(115.0 + layer*61.0)*(0.72+event_random(column_seed)*0.65);
      int2 cell=(int2)floor(rp/cell_size);
      uint seed=event_hash(uint(cell.x)*0x85ebca6bu ^ uint(cell.y)*0xc2b2ae35u ^ uint(layer)*7919u);
      float2 q=frac(rp/cell_size)*cell_size;
      float xdist=abs(q.x-(2.0+(cell_size.x-4.0)*event_random(seed^17u)));
      float width=(0.17+layer*0.14)*(0.65+event_random(seed^19u)*0.8);
      float length=2.0+event_random(seed^21u)*(5.0+rain*7.0);
      float dy=q.y-(1.0+event_random(seed^29u)*(cell_size.y-length-2.0));
      float tail=smoothstep(0.0,1.0,dy)*(1.0-smoothstep(length-1.0,length,dy));
      float active=event_random(seed^33u)<rain*(0.45+rain*0.5)?1.0:0.0;
      streak += (1.0-smoothstep(width,width+0.45,xdist))*tail*active*
                (0.10+layer*0.065)*(0.6+event_random(seed^39u)*0.65)*curtain;
    }
    composite_layer(color,alpha,float3(0.70,0.79,0.84),min(0.52,streak));
    if (is_ground > 0.5 && impacts.x > 0.001) {
      float2 cell_size=float2(23.0,17.0);
      int2 cell=(int2)floor(source_local/cell_size);
      uint cell_seed=event_hash(uint(cell.x)*0x85ebca6bu ^ uint(cell.y)*0xc2b2ae35u);
      float life=time*(1.7+event_random(cell_seed)*1.1)+event_random(cell_seed^67u);
      float phase=frac(life);
      uint seed=event_hash(cell_seed ^ uint(floor(life))*0x9e3779b9u);
      float2 center=float2(4.0+15.0*event_random(seed^43u),4.0+9.0*event_random(seed^91u));
      float2 q=frac(source_local/cell_size)*cell_size-center;
      float visible=event_random(seed^73u)<rain*(0.22+curtain*0.40)?1.0:0.0;
      float radius=0.5+phase*(3.0+water_surface*2.0);
      float ring=(1.0-smoothstep(0.16,0.55,abs(length(q*float2(1.0,1.9))-radius)))*pow(1-phase,2);
      float second=(1.0-smoothstep(0.16,0.5,abs(length(q*float2(1.0,1.9))-(radius-1.4))))*smoothstep(.20,.32,phase)*pow(1-phase,2);
      float age=saturate(phase/.42);
      float lift=4.5*age*(1-age);
      float spread=.35+age*2.3;
      float crown=(1.0-smoothstep(.22,.75,length(q-float2(-spread,-lift))))+
                  (1.0-smoothstep(.22,.75,length(q-float2(spread,-lift))))+
                  (1.0-smoothstep(.18,.65,length(q-float2(0,-lift*1.4))));
      crown *= (1.0-smoothstep(.28,.43,phase));
      float splash=lerp(crown*.57+ring*.14,ring*.55+second*.25,water_surface+pool*.5);
      composite_layer(color,alpha,float3(.77,.85,.89),saturate(splash*visible*impacts.x*rain));
    }
  }

  uint event_id=uint(floor(flags.y+0.5))|(uint(floor(flags.w+0.5))<<16);
  if (event_id!=0u && flags.z>=0.0 && flags.z<0.8 && sky.z>0.001 && storm>0.02 &&
      (is_scene>0.5 || (!black_background && is_sky>0.5))) {
    float pulse=saturate(sky.z/max(storm*0.333,0.02));
    float energy=pulse*saturate(storm/0.45)*((mode&1u)!=0u?0.35:1.0);
    // Distant sheet lightning illuminates a broad bank. No bolt geometry.
    float side=0.2+0.6*event_random(event_id^11u);
    float spread=0.76+0.24*exp(-pow((local.x/clip_rect.z-side)*1.8,2.0));
    float variation=0.86+0.14*event_random(event_id^0x57ab39u);
    float gain=impacts.y>0.0?impacts.y:0.72;
    if (surfaces.w>0.5 && is_scene>0.5) {
      // Raise actual scene exposure, retaining material detail instead of
      // painting a uniform pale veil. Wet patches catch the strike.
      float3 lit=saturate(color*(2.5+wet*.35)+float3(.22,.25,.30));
      color=lerp(color,lit,saturate(energy*spread*variation*gain));
      color+=float3(.14,.17,.22)*wet*(pool+water_surface*.4)*energy;
    } else composite_layer(color,alpha,float3(0.90,0.94,1.0),energy*spread*variation*(is_sky>0.5?0.34:gain));
  }
  if (alpha <= 0.001) discard;
  return float4(color, alpha);
}
)HLSL";

double finite_or(double x, double fallback) {
  return std::isfinite(x) ? x : fallback;
}

double clampd(double x, double low, double high, double fallback) {
  if (!std::isfinite(x)) return fallback;
  return x < low ? low : x > high ? high : x;
}

double wrapd(double x, double period) {
  if (!(period > 0.0) || !std::isfinite(x)) return 0.0;
  x = std::fmod(x, period);
  return x < 0.0 ? x + period : x;
}

uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  return x ^ (x >> 16);
}

double hash01(uint32_t x) {
  return (double)hash32(x) / 4294967296.0;
}

double lightning_energy(const WeatherState *weather, int reduced_flashes) {
  if (!weather || !weather->event_id) return 0.0;
  double strike_age = clampd(weather->strike_age, 0.0, 0.8, 1.0);
  double storm = clampd(weather->storm, 0.0, 1.0, 0.0);
  double lightning = clampd(weather->lightning, 0.0, 1.0, 0.0);
  if (strike_age >= 0.8 || storm <= 0.02 || lightning <= 0.001) return 0.0;
  double pulse = clampd(lightning / std::fmax(storm * 0.333, 0.02), 0.0, 1.0, 0.0);
  double storm_gain = clampd(storm / 0.45, 0.0, 1.0, 0.0);
  return pulse * storm_gain * (reduced_flashes ? 0.35 : 1.0);
}

Uint8 byte_clamp(double x) {
  return (Uint8)(x < 0.0 ? 0 : x > 255.0 ? 255 : x);
}

void set_detail(EnvironmentFxState *state, const char *message);

}  // namespace

struct EnvironmentFxState {
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *cloud_texture = nullptr;
  EnvironmentFxBackend backend = ENVIRONMENT_FX_BACKEND_NONE;
  int shader_attempted = 0;
  int shader_compiled = 0;
  int receiver_uploaded = 0;
  char detail[160] = "not initialized";

#ifdef _WIN32
  int d3d_tried = 0;
  int d3d_ready = 0;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  ID3D11DeviceContext1 *context1 = nullptr;
  ID3DDeviceContextState *context_state = nullptr;
  SDL_Texture *confirmed_target = nullptr;
  ID3D11RenderTargetView *confirmed_view = nullptr;
  HMODULE compiler_module = nullptr;
  CompileFn compile = nullptr;
  ID3D11VertexShader *vertex_shader = nullptr;
  ID3D11PixelShader *pixel_shader = nullptr;
  ID3D11Buffer *constant_buffer = nullptr;
  ID3D11BlendState *blend_state = nullptr;
  ID3D11RasterizerState *raster_state = nullptr;
  ID3D11Texture2D *scene_texture = nullptr;
  ID3D11ShaderResourceView *scene_view = nullptr;
  UINT scene_width = 0, scene_height = 0;
  DXGI_FORMAT scene_format = DXGI_FORMAT_UNKNOWN;
  ID3D11Texture2D *receiver_texture = nullptr;
  ID3D11ShaderResourceView *receiver_view = nullptr;
  const uint8_t *receiver_pixels = nullptr,*receiver_features = nullptr;
  uint8_t *receiver_upload=nullptr;size_t receiver_upload_bytes=0;
  uint64_t receiver_serial = 0;
  int receiver_width = 0, receiver_height = 0, receiver_pitch = 0;
#endif
};

namespace {

void set_detail(EnvironmentFxState *state, const char *message) {
  if (!state) return;
  std::snprintf(state->detail, sizeof state->detail, "%s", message ? message : "unknown status");
}

bool valid_receiver(const EnvironmentFxReceiver *receiver) {
  if (!receiver || !receiver->pixels || receiver->width <= 0 || receiver->height <= 0 ||
      receiver->width > 16384 || receiver->height > 16384 ||
      receiver->pitch < receiver->width || !std::isfinite(receiver->pixel_scale) ||
      receiver->pixel_scale <= 0.0 || !std::isfinite(receiver->origin_x) ||
      !std::isfinite(receiver->origin_y)) return false;
  size_t rows = (size_t)receiver->height;
  size_t pitch = (size_t)receiver->pitch;
  return rows <= kMaxReceiverBytes / pitch && rows * pitch <= kMaxReceiverBytes;
}

uint8_t receiver_at(const EnvironmentFxReceiver *receiver, double output_x, double output_y) {
  if (!valid_receiver(receiver)) return 0;
  double sx = std::floor((output_x - receiver->origin_x) / receiver->pixel_scale);
  double sy = std::floor((output_y - receiver->origin_y) / receiver->pixel_scale);
  if (sx < 0 || sy < 0 || sx >= receiver->width || sy >= receiver->height) return 0;
  uint8_t raw=receiver->pixels[(size_t)(int)sy * receiver->pitch + (int)sx];
  return raw>=128&&raw<=143?127:raw;
}

const uint8_t *feature_at(const EnvironmentFxReceiver *r,double x,double y){
  if(!valid_receiver(r)||!r->features)return nullptr;
  int xx=(int)std::floor((x-r->origin_x)/r->pixel_scale),yy=(int)std::floor((y-r->origin_y)/r->pixel_scale);
  return xx<0||yy<0||xx>=r->width||yy>=r->height?nullptr:r->features+((size_t)yy*r->width+xx)*3;
}
const WeatherSurfaceTuning &material_defaults(){static const WeatherSurfaceTuning tuning=weather_surface_defaults();return tuning;}
double wet_material_gain(const uint8_t *f,const WeatherState *w){
  if(!f||!w)return 0;
  int material=f[2]&3;double nx=((f[0]&15)-8)/7.,nz=((f[0]>>4)-8)/7.;
  double normal[3]={nx,std::sqrt(std::fmax(.05,1-nx*nx-nz*nz)),nz},half[3],length=0,dot=0,view=0;
  double nl=std::sqrt(nx*nx+normal[1]*normal[1]+nz*nz);
  for(int i=0;i<3;i++){half[i]=w->light[i]+w->eye[i];length+=half[i]*half[i];view+=normal[i]/nl*w->eye[i];}
  if(length<.001)return 0;length=std::sqrt(length);
  for(int i=0;i<3;i++)dot+=normal[i]/nl*half[i]/length;
  double amplitude=w->material_tuning_valid?w->material_gloss[material]:material_defaults().material_gloss[material];
  double exponent=w->material_tuning_valid?w->material_power[material]:material_defaults().material_power[material];
  double lobe=std::pow(clampd(dot,0,1,0),exponent)*clampd(w->light_strength,0,1,0)*(1-clampd(w->cloud,0,1,0)*.85);
  double grazing=.025+.975*std::pow(1-clampd(view,0,1,0),5);
  return amplitude*(lobe+grazing*.45);
}

double basin_at(const EnvironmentFxReceiver *r,double x,double y){
  if(!valid_receiver(r))return 0;int xx=(int)std::floor((x-r->origin_x)/r->pixel_scale),yy=(int)std::floor((y-r->origin_y)/r->pixel_scale);
  if(xx<0||yy<0||xx>=r->width||yy>=r->height)return 0;
  int v=r->pixels[(size_t)yy*r->pitch+xx];return v==160?1:v>=127&&v<=143?(v-127)/16.0:0;
}
bool clip_valid(const SDL_Rect &clip) {
  return clip.w > 0 && clip.h > 0;
}

void set_pixel(SDL_Surface *surface, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  Uint32 pixel = SDL_MapRGBA(surface->format, r, g, b, a);
  Uint8 *address = (Uint8 *)surface->pixels + y * surface->pitch + x * surface->format->BytesPerPixel;
  std::memcpy(address, &pixel, surface->format->BytesPerPixel);
}

double field_noise(double x,double y) {
  int ix=(int)std::floor(x),iy=(int)std::floor(y);
  double u=x-ix,v=y-iy;u=u*u*(3-2*u);v=v*v*(3-2*v);
  auto at=[](int a,int b){return hash01((uint32_t)a*0x85ebca6bU^(uint32_t)b*0xc2b2ae35U);};
  double a=at(ix,iy)*(1-u)+at(ix+1,iy)*u;
  double b=at(ix,iy+1)*(1-u)+at(ix+1,iy+1)*u;
  return a*(1-v)+b*v;
}
double cloud_shape(double px,double py,double phase,uint32_t seed) {
  px+=(seed&255)*.173;py+=((seed>>8)&255)*.173;
  double wx=field_noise(px*.7+phase*.04,py*.7)-.5;
  double wy=field_noise(px*.7+41.7,py*.7-phase*.03)-.5;
  double x=px+wx*.65,y=py+wy*.65;
  double n=field_noise(x,y)*.53;
  n+=field_noise(x*2.03+17.3+phase*.025,y*2.03+9.2+phase*.025)*.27;
  n+=field_noise(x*4.11+31.7-phase*.02,y*4.11+19.4-phase*.02)*.14;
  n+=field_noise(x*8.3+57.1,y*8.3+43.7)*.06;
  return clampd(n,0,1,0);
}
uint8_t shade_class(uint8_t raw){return raw>=89&&raw<=173?127:raw;}
void draw_cloud_field(EnvironmentFxState *state,SDL_Renderer *renderer,SDL_Rect clip,
                      const EnvironmentFxReceiver *receiver,double camera_x,double camera_y,
                      double scale,double seconds,double cloud,int black_background,
                      double cloud_size,double cloud_shade,double cloud_feather,double wind_x,double wind_z,
                      double morph_rate,uint32_t seed,const WeatherState *mist_weather=nullptr) {
  if(!valid_receiver(receiver))return;
  if(!state->cloud_texture) {
    state->cloud_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,kCloudTextureSize,kCloudTextureSize);
    if(!state->cloud_texture)return;
    SDL_SetTextureBlendMode(state->cloud_texture,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(state->cloud_texture,SDL_ScaleModeLinear);
  }
  // A stable world lattice keeps the same bilinear samples under camera pans.
  double step=1;
  while(step*(kCloudTextureSize-4)<std::fmax(clip.w/scale,clip.h/scale))step*=2;
  double grid_x=std::floor(camera_x/step)*step-step;
  double grid_y=std::floor(camera_y/step)*step-step;
  Uint8 pixels[kCloudTextureSize*kCloudTextureSize*4];
  for(int y=0;y<kCloudTextureSize;y++)for(int x=0;x<kCloudTextureSize;x++) {
    double px=((grid_x+(x+.5)*step)/165.0-seconds*(.035+wind_x*.025))/cloud_size;
    double py=((grid_y+(y+.5)*step)/115.0-seconds*(.012+wind_z*.018))/cloud_size;
    double edge=.53-cloud*.08;
    double cover=clampd((cloud_shape(px,py,seconds*morph_rate,seed)-(edge-cloud_feather*.5))/cloud_feather,0,1,0);
    cover=cover*cover*(3-2*cover);
    if(mist_weather){
      px=(grid_x+(x+.5)*step)/210-seconds*.018-mist_weather->wind_drift_x*.004;
      py=(grid_y+(y+.5)*step)/43-seconds*.007;
      double density=clampd((cloud_shape(px,py,seconds*.003,seed^0xa1e7u)-.28)/.42,0,1,0);
      density=density*density*(3-2*density);
      cover=mist_weather->haze*.65+mist_weather->mist*density;
    }
    Uint8 *pixel=pixels+(y*kCloudTextureSize+x)*4;
    pixel[0]=pixel[1]=pixel[2]=255;pixel[3]=byte_clamp(cover*255);
  }
  if(SDL_UpdateTexture(state->cloud_texture,nullptr,pixels,kCloudTextureSize*4)<0)return;
  SDL_Vertex vertices[4092];int n=0;
  const int order[6]={0,1,2,2,1,3};
  for(int y=0;y<receiver->height;y++)for(int x=0;x<receiver->width;) {
    uint8_t value=receiver->pixels[(size_t)y*receiver->pitch+x];
    if(!mist_weather)value=shade_class(value);
    bool ground=value>=89&&value<=173,object=value==64,sky=!black_background&&value>=224;
    int start=x++;if(!ground&&!object&&!sky)continue;
    while(x<receiver->width&&(mist_weather?receiver->pixels[(size_t)y*receiver->pitch+x]:shade_class(receiver->pixels[(size_t)y*receiver->pitch+x]))==value&&(!mist_weather||!receiver->features||mist_weather->valley_mist<=0||x-start<12))x++;
    float xx[2]={(float)(receiver->origin_x+start*receiver->pixel_scale),(float)(receiver->origin_x+x*receiver->pixel_scale)};
    float yy[2]={(float)(receiver->origin_y+y*receiver->pixel_scale),(float)(receiver->origin_y+(y+1)*receiver->pixel_scale)};
    double sunbreak=clampd((cloud-.16)/.16,0,1,0);sunbreak=sunbreak*sunbreak*(3-2*sunbreak);
    SDL_Color tint=(ground||object)?SDL_Color{25,35,49,byte_clamp(255*cloud*sunbreak*cloud_shade*(object?.8:1))}:
                          SDL_Color{145,157,173,byte_clamp(255*cloud*.62)};
    if(mist_weather){
      double low=value==160?1:value>=127&&value<=143?(value-127)/16.0:0;
      double factor=object?.30:ground?.25+.75*low:0;
      if(receiver->features&&mist_weather->valley_mist>0){
        const uint8_t *f=receiver->features+((size_t)y*receiver->width+(start+x-1)/2)*3;
        double local=f[1]/255.*(f[2]>>2)/63.;
        double amount=clampd(mist_weather->valley_mist,0,1,0);
        factor=factor*(1-amount)+local*amount;
      }
      double night=clampd(mist_weather->night,0,1,0),gold=clampd(mist_weather->golden,0,1,0);
      tint=SDL_Color{byte_clamp((184+28*gold)*(1-night*.5)),byte_clamp((201-8*gold)*(1-night*.42)),
        byte_clamp((209-52*gold)*(1-night*.28)),byte_clamp(255*factor)};
    }
    for(int j=0;j<6;j++){int q=order[j];float X=xx[q&1],Y=yy[q/2];
      vertices[n++]={{X,Y},tint,{(float)((camera_x+(X-clip.x)/scale-grid_x)/(step*kCloudTextureSize)),
                                (float)((camera_y+(Y-clip.y)/scale-grid_y)/(step*kCloudTextureSize))}};
    }
    if(n==4092){SDL_RenderGeometry(renderer,state->cloud_texture,vertices,n,nullptr,0);n=0;}
  }
  if(n)SDL_RenderGeometry(renderer,state->cloud_texture,vertices,n,nullptr,0);
}

int quality_level(int quality) {
  return quality < 0 ? 0 : quality > 3 ? 3 : quality;
}

int fallback_cloud_count(int quality) {
  static const int counts[] = {4, 8, 14, 20};
  return counts[quality_level(quality)];
}

int fallback_sky_count(int quality) {
  static const int counts[] = {20, 48, 88, 144};
  return counts[quality_level(quality)];
}

void draw_sky_points(SDL_Renderer *renderer, SDL_Rect clip, const EnvironmentFxReceiver *receiver,
                     double seconds, int quality, double night) {
  int count = fallback_sky_count(quality);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < count; ++i) {
    uint32_t seed = (uint32_t)i * 0x9e3779b9U + 0x1743a1U;
    int x = clip.x + (int)(hash01(seed) * clip.w);
    int y = clip.y + (int)(hash01(seed ^ 0x76a1d32fU) * clip.h);
    if (receiver_at(receiver, x + 0.5, y + 0.5) < 224) continue;
    double twinkle = 0.62 + 0.38 * std::sin(seconds * (1.0 + hash01(seed ^ 0x11U) * 2.0) + hash01(seed ^ 0x22U) * 19.0);
    Uint8 a = byte_clamp(220.0 * night * twinkle);
    SDL_SetRenderDrawColor(renderer, 195, 218, 255, a);
    SDL_RenderDrawPoint(renderer, x, y);
    if (i % 17 == 0) {
      SDL_SetRenderDrawColor(renderer, 195, 218, 255, byte_clamp(a * 0.45));
      SDL_RenderDrawLine(renderer, x - 1, y, x + 1, y);
      SDL_RenderDrawLine(renderer, x, y - 1, x, y + 1);
    }
  }
}

void draw_moon_glow(SDL_Renderer *renderer, SDL_Rect clip, const EnvironmentFxReceiver *receiver,
                    double night) {
  if (clip.w < 32 || clip.h < 32) return;
  const double cx = clip.x + clip.w * 0.80;
  const double cy = clip.y + clip.h * 0.18;
  const double radius = std::fmax(8.0, std::fmin(clip.w, clip.h) * 0.04);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int ring = 8; ring >= 1; --ring) {
    double r = radius * ring / 8.0;
    Uint8 a = byte_clamp((1.0 - ring / 9.0) * night * 36.0);
    SDL_SetRenderDrawColor(renderer, 135, 167, 213, a);
    const int points = 36;
    for (int j = 0; j < points; ++j) {
      double angle = 6.283185307179586 * j / points;
      int x = (int)std::lround(cx + std::cos(angle) * r);
      int y = (int)std::lround(cy + std::sin(angle) * r);
      if (receiver_at(receiver, x + 0.5, y + 0.5) >= 224) SDL_RenderDrawPoint(renderer, x, y);
    }
  }
  for (int y = (int)(cy - radius * 0.65); y <= (int)(cy + radius * 0.65); ++y) {
    for (int x = (int)(cx - radius * 0.65); x <= (int)(cx + radius * 0.65); ++x) {
      double dx = (x + 0.5 - cx) / (radius * 0.65);
      double dy = (y + 0.5 - cy) / (radius * 0.65);
      if (dx * dx + dy * dy <= 1.0 && receiver_at(receiver, x + 0.5, y + 0.5) >= 224) {
        SDL_SetRenderDrawColor(renderer, 218, 229, 249, byte_clamp(night * 200.0));
        SDL_RenderDrawPoint(renderer, x, y);
      }
    }
  }
}

void draw_glowbugs(SDL_Renderer *renderer, SDL_Rect clip, const EnvironmentFxReceiver *receiver,
                   double camera_x, double camera_y, double scale,
                   double seconds, int quality, double night) {
  quality = quality_level(quality);
  const double cell_size = 56.0 - 22.0 * quality / 3.0;
  const double source_width = clip.w / scale;
  const double source_height = clip.h / scale;
  const int columns = (int)std::ceil(source_width / cell_size) + 2;
  const int rows = (int)std::ceil(source_height / cell_size) + 2;
  const int first_x = (int)std::floor(camera_x / cell_size) - 1;
  const int first_y = (int)std::floor(camera_y / cell_size) - 1;
  const uint64_t total_cells = (uint64_t)columns * (uint64_t)rows;
  const int stride = total_cells > kMaxFallbackBugCells
                         ? (int)std::ceil(std::sqrt((double)total_cells / kMaxFallbackBugCells)) : 1;
  uint32_t offset_seed = hash32((uint32_t)first_x * 0x85ebca6bU ^
                                (uint32_t)first_y * 0xc2b2ae35U ^ 0x2d39U);
  const int offset_x = (int)(offset_seed % (uint32_t)stride);
  const int offset_y = (int)(hash32(offset_seed ^ 0x1c47U) % (uint32_t)stride);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int cell_y = first_y + offset_y; cell_y < first_y + rows; cell_y += stride) {
    for (int cell_x = first_x + offset_x; cell_x < first_x + columns; cell_x += stride) {
      uint32_t seed = hash32((uint32_t)cell_x * 0x85ebca6bU ^
                             (uint32_t)cell_y * 0xc2b2ae35U ^ 0x2d39U);
      if (hash01(seed ^ 0x38fdU) >= 0.14) continue;
      double center_x = cell_x + 0.18 + hash01(seed) * 0.64;
      double center_y = cell_y + 0.18 + hash01(seed ^ 0x18273645U) * 0.64;
      double phase = hash01(seed ^ 0xabcU) * 6.283185307179586 +
                     seconds * (0.28 + hash01(seed ^ 0x2841U) * 0.20);
      double world_x = (center_x + std::sin(phase) * 0.030) * cell_size;
      double world_y = (center_y + std::cos(phase * 0.83 + center_y) * 0.024) * cell_size;
      int x = clip.x + (int)std::lround((world_x - camera_x) * scale);
      int y = clip.y + (int)std::lround((world_y - camera_y) * scale);
      if (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h) continue;
      uint8_t receiver_value = receiver_at(receiver, x + 0.5, y + 0.5);
      if (receiver_value < 127 || receiver_value > 143) continue;

      double pulse = 0.52 + 0.48 * std::sin(seconds * (1.25 + center_x * 0.20) + center_y * 16.0);
      int glow_radius = (int)clampd(std::lround(scale * 2.0), 1.0, 4.0, 1.0);
      SDL_SetRenderDrawColor(renderer, 112, 166, 67, byte_clamp(34.0 * night * pulse));
      for (int oy = -glow_radius; oy <= glow_radius; ++oy) {
        for (int ox = -glow_radius; ox <= glow_radius; ++ox) {
          if (ox * ox + oy * oy > glow_radius * glow_radius) continue;
          int px = x + ox, py = y + oy;
          uint8_t value = receiver_at(receiver, px + 0.5, py + 0.5);
          if (value >= 127 && value <= 143) SDL_RenderDrawPoint(renderer, px, py);
        }
      }
      SDL_SetRenderDrawColor(renderer, 205, 246, 145, byte_clamp(220.0 * night * pulse));
      SDL_RenderDrawPoint(renderer, x, y);
    }
  }
}

void draw_rain_line(SDL_Renderer *renderer,const EnvironmentFxReceiver *receiver,int black_background,
                    int x,int y,int end_x,int end_y,int ground_only=0) {
  if(!black_background){SDL_RenderDrawLine(renderer,x,y,end_x,end_y);return;}
  int dx=std::abs(end_x-x),sx=x<end_x?1:-1,dy=-std::abs(end_y-y),sy=y<end_y?1:-1,error=dx+dy;
  int run_x=0,run_y=0,last_x=x,last_y=y;bool running=false;
  for(;;) {
    uint8_t value=receiver_at(receiver,x+.5,y+.5);
    bool scene=ground_only ? (value==127||value==160) : (value>=48&&value<=173);
    if(scene&&!running){run_x=x;run_y=y;running=true;}
    if(!scene&&running){SDL_RenderDrawLine(renderer,run_x,run_y,last_x,last_y);running=false;}
    last_x=x;last_y=y;
    if(x==end_x&&y==end_y)break;
    int twice=2*error;
    if(twice>=dy){error+=dy;x+=sx;}
    if(twice<=dx){error+=dx;y+=sy;}
  }
  if(running){if(run_x==last_x&&run_y==last_y)SDL_RenderDrawPoint(renderer,run_x,run_y);else SDL_RenderDrawLine(renderer,run_x,run_y,last_x,last_y);}
}

void draw_fallback_lightning(SDL_Renderer *renderer, SDL_Rect clip,
                             const EnvironmentFxReceiver *receiver, const WeatherState *weather,
                             int reduced_flashes) {
  double energy=lightning_energy(weather,reduced_flashes);
  if (energy<=0.001 || !valid_receiver(receiver)) return;
  double side=0.2+0.6*hash01(weather->event_id^11U);
  double variation=0.86+0.14*hash01(weather->event_id^0x57ab39U);
  SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
  SDL_Vertex vertices[4092];int n=0;const int order[6]={0,1,2,2,1,3};
  // Iterate source-mask spans once, instead of revalidating every4K pixel.
  for(int y=0;y<receiver->height;y++){
    int top=(int)std::ceil(clampd(receiver->origin_y+y*receiver->pixel_scale-.5,clip.y,clip.y+clip.h,clip.y));
    int bottom=(int)std::ceil(clampd(receiver->origin_y+(y+1)*receiver->pixel_scale-.5,clip.y,clip.y+clip.h,clip.y));
    if(top>=bottom)continue;
    for(int x=0;x<receiver->width;){
      uint8_t value=receiver->pixels[(size_t)y*receiver->pitch+x];if(value>=128&&value<=143)value=127;
      int start=x++;
      while(x<receiver->width){
        uint8_t next=receiver->pixels[(size_t)y*receiver->pitch+x];if(next>=128&&next<=143)next=127;
        if(next!=value)break;++x;
      }
      bool sky=!weather->black_background&&value>=224,ground=value>=48&&value<=173;
      if(!sky&&!ground)continue;
      int left=(int)std::ceil(clampd(receiver->origin_x+start*receiver->pixel_scale-.5,clip.x,clip.x+clip.w,clip.x));
      int right=(int)std::ceil(clampd(receiver->origin_x+x*receiver->pixel_scale-.5,clip.x,clip.x+clip.w,clip.x));
      if(left>=right)continue;
      double dx=((left+right)*.5-clip.x)/clip.w-side;
      double spread=.76+.24*std::exp(-dx*dx*3.24);
      SDL_Color color={230,240,255,byte_clamp(255*energy*spread*variation*(sky?.34:clampd(weather->flash_gain>0?weather->flash_gain:.72,.1,.9,.72)))};
      float xx[2]={(float)left,(float)right},yy[2]={(float)top,(float)bottom};
      for(int j=0;j<6;j++){int q=order[j];vertices[n++]={{xx[q&1],yy[q/2]},color,{0,0}};}
      if(n==4092){SDL_RenderGeometry(renderer,nullptr,vertices,n,nullptr,0);n=0;}
    }
  }
  if(n)SDL_RenderGeometry(renderer,nullptr,vertices,n,nullptr,0);
}

void draw_wet_fallback(SDL_Renderer *r,SDL_Rect clip,const EnvironmentFxReceiver *receiver,
                       const WeatherState *w,double cx,double cy,double scale,double seconds) {
  double wet=clampd(w->wetness,0,1,0);if((wet<.001&&w->puddles<.001)||!valid_receiver(receiver))return;
  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
  SDL_Vertex v[4092];int n=0;const int order[6]={0,1,2,2,1,3};
  for(int y=0;y<receiver->height;++y)for(int x=0;x<receiver->width;){
    int start=x;int material=receiver->features?receiver->features[((size_t)y*receiver->width+x)*3+2]&3:0;
    uint8_t value=receiver->pixels[(size_t)y*receiver->pitch+x++];if(value>=127&&value<=143)value=127;if(value<48||value>173)continue;
    while(x<receiver->width){uint8_t next=receiver->pixels[(size_t)y*receiver->pitch+x];if(next>=127&&next<=143)next=127;if(next!=value||(receiver->features&&(receiver->features[((size_t)y*receiver->width+x)*3+2]&3)!=material))break;++x;}
    float xx[2]={(float)(receiver->origin_x+start*receiver->pixel_scale),(float)(receiver->origin_x+x*receiver->pixel_scale)};
    float yy[2]={(float)(receiver->origin_y+y*receiver->pixel_scale),(float)(receiver->origin_y+(y+1)*receiver->pixel_scale)};
    double absorb=value==160?.07:value==64?.12:.18;
    if(w->material_wetness>0&&value==127&&receiver->features){
      double detailed=w->material_tuning_valid?w->material_absorb[material]:material_defaults().material_absorb[material],amount=clampd(w->material_wetness,0,1,0);
      absorb=absorb*(1-amount)+detailed*amount;
    }
    SDL_Color color={9,16,21,byte_clamp(255*wet*absorb)};
    for(int j=0;j<6;++j){int q=order[j];v[n++]={{xx[q&1],yy[q/2]},color,{0,0}};}
    if(n==4092){SDL_RenderGeometry(r,nullptr,v,n,nullptr,0);n=0;}
  }
  if(n)SDL_RenderGeometry(r,nullptr,v,n,nullptr,0);
  double cell=std::fmax(38.0,std::sqrt(clip.w*(double)clip.h/(scale*scale*2500.0)));
  int left=(int)std::floor(cx/cell)-1,top=(int)std::floor(cy/(cell*.55))-1;
  int cols=(int)std::ceil(clip.w/(scale*cell))+3,rows=(int)std::ceil(clip.h/(scale*cell*.55))+3;
  for(int y=top;y<top+rows;++y)for(int x=left;x<left+cols;++x){
    uint32_t seed=hash32((uint32_t)x*0x85ebca6bu^(uint32_t)y*0xc2b2ae35u);if(hash01(seed)>.28)continue;
    int px=clip.x+(int)std::lround(((x+.2+.6*hash01(seed^11))*cell-cx)*scale);
    int py=clip.y+(int)std::lround(((y+.2+.6*hash01(seed^17))*cell*.55-cy)*scale);
    uint8_t kind=receiver_at(receiver,px+.5,py+.5);if(kind<48||kind>173)continue;
    int half=(int)std::lround((kind==64?1.3:3)*scale);if(half<1)half=1;
    double highlight=wet*(kind==64?.045:.06)+w->puddles*.12*(.3+1.4*basin_at(receiver,px,py));
    const uint8_t *f=feature_at(receiver,px,py);
    if(f&&w->material_wetness>0&&kind==127){
      double amount=clampd(w->material_wetness,0,1,0);
      double detailed=wet*wet_material_gain(f,w)+w->puddles*.12*((f[2]&3)==3?.12:1);
      highlight=highlight*(1-amount)+detailed*amount;
    }
    SDL_SetRenderDrawColor(r,137,162,179,byte_clamp(255*highlight));
    draw_rain_line(r,receiver,1,px-half,py,px+half,py);
    if(kind==127&&w->evaporation>.001){
      double phase=wrapd(seconds*.12+hash01(seed^31),1);
      int lift=(int)std::lround(phase*3*scale);
      SDL_SetRenderDrawColor(r,164,178,184,byte_clamp(255*w->evaporation*std::sin(phase*3.141592653589793)*.6));
      draw_rain_line(r,receiver,1,px-half*2,py-lift,px+half*2,py-lift);
    }
  }
}
void draw_impacts_fallback(SDL_Renderer *r,SDL_Rect clip,const EnvironmentFxReceiver *receiver,
                          const WeatherState *w,double cx,double cy,double scale,double seconds) {
  double rain=clampd(w->rain,0,1,0),strength=clampd(w->impact_strength,0,1,0);
  if(rain<.001||strength<.001||!valid_receiver(receiver))return;
  double step=std::fmax(23.0,std::sqrt(clip.w*(double)clip.h/(scale*scale*3500.0))),vertical=step*17/23;
  int left=(int)std::floor(cx/step)-1,top=(int)std::floor(cy/vertical)-1;
  int cols=(int)std::ceil(clip.w/(scale*step))+3,rows=(int)std::ceil(clip.h/(scale*vertical))+3;
  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
  for(int y=top;y<top+rows;++y)for(int x=left;x<left+cols;++x){
    uint32_t cell=hash32((uint32_t)x*0x85ebca6bu^(uint32_t)y*0xc2b2ae35u);
    double life=seconds*(1.7+hash01(cell)*1.1)+hash01(cell^67u),phase=wrapd(life,1);
    uint32_t seed=hash32(cell^(uint32_t)std::floor(life)*0x9e3779b9u);if(hash01(seed^73u)>rain*.65)continue;
    double wx=x*step+(5+13*hash01(seed^43u))*step/23,wy=y*vertical+(4+9*hash01(seed^91u))*vertical/17;
    int px=clip.x+(int)std::lround((wx-cx)*scale),py=clip.y+(int)std::lround((wy-cy)*scale);
    uint8_t kind=receiver_at(receiver,px+.5,py+.5);if(kind!=127&&kind!=160)continue;
    SDL_SetRenderDrawColor(r,197,217,227,byte_clamp(255*strength*rain*std::pow(1-phase,2)*(kind==160?.5:.6)));
    if(kind==160){
      for(int ring=0;ring<2;++ring){double rad=(.5+phase*5-ring*1.4)*scale;if(rad<=0)continue;
        for(int segment=0;segment<8;++segment){double a=segment*.7853981633974483,b=(segment+1)*.7853981633974483;
          draw_rain_line(r,receiver,1,px+(int)std::lround(std::cos(a)*rad),py+(int)std::lround(std::sin(a)*rad/1.9),px+(int)std::lround(std::cos(b)*rad),py+(int)std::lround(std::sin(b)*rad/1.9),1);}
      }
    }else if(phase<.42){
      double age=phase/.42,spread=(.35+age*2.3)*scale,lift=4.5*age*(1-age)*scale;
      for(int side=-1;side<=1;++side){int xx=px+(int)std::lround(side*spread),yy=py-(int)std::lround(lift*(side?1:1.4));draw_rain_line(r,receiver,1,xx,yy,xx,yy,1);}
    }
  }
}

void draw_mist_fallback(EnvironmentFxState *state,SDL_Renderer *r,SDL_Rect clip,const EnvironmentFxReceiver *receiver,
 const WeatherState *w,double cx,double cy,double scale){
  if(w->mist<.001&&w->haze<.001)return;
  draw_cloud_field(state,r,clip,receiver,cx,cy,scale,w->seconds,1,1,1,.28,.22,0,0,0,w->cloud_seed,w);
}

void draw_fallback(EnvironmentFxState *state, SDL_Renderer *renderer, SDL_Rect clip,
                   const EnvironmentFxReceiver *receiver, const WeatherState *weather,
                   double camera_x, double camera_y, double scale, int quality,
                   int glowbugs, int night_sky, int reduced_flashes) {
  SDL_BlendMode old_blend;
  Uint8 old_r, old_g, old_b, old_a;
  float old_scale_x = 1.0f, old_scale_y = 1.0f;
  SDL_Rect old_viewport = {0, 0, 0, 0}, old_clip = {0, 0, 0, 0};
  int had_clip = SDL_RenderIsClipEnabled(renderer);
  SDL_GetRenderDrawBlendMode(renderer, &old_blend);
  SDL_GetRenderDrawColor(renderer, &old_r, &old_g, &old_b, &old_a);
  SDL_RenderGetScale(renderer, &old_scale_x, &old_scale_y);
  SDL_RenderGetViewport(renderer, &old_viewport);
  SDL_RenderGetClipRect(renderer, &old_clip);

  SDL_RenderSetViewport(renderer, nullptr);
  SDL_RenderSetScale(renderer, 1.0f, 1.0f);
  SDL_RenderSetClipRect(renderer, &clip);
  const double seconds = wrapd(clampd(weather->seconds, 0.0, 1.0e9, 0.0), 86400.0);
  const double night = clampd(weather->night, 0.0, 1.0, 0.0);
  const double cloud = clampd(weather->cloud, 0.0, 1.0, 0.0);
  const double rain = clampd(weather->rain, 0.0, 1.0, 0.0);
  camera_x = clampd(camera_x, -1.0e8, 1.0e8, 0.0);
  camera_y = clampd(camera_y, -1.0e8, 1.0e8, 0.0);
  scale = clampd(scale, 0.25, 16.0, 1.0);
  quality = quality_level(quality);

  draw_wet_fallback(renderer,clip,receiver,weather,camera_x,camera_y,scale,seconds);
  if (cloud > 0.001 && (!weather->black_background || cloud > .16)) draw_cloud_field(state,renderer,clip,receiver,camera_x,camera_y,scale,seconds,cloud,weather->black_background,
    clampd(weather->cloud_size>0?weather->cloud_size:1.9,.5,4,1.9),weather->cloud_shade>0?clampd(weather->cloud_shade,.05,.65,.28):.28,
    weather->cloud_feather>0?clampd(weather->cloud_feather,.02,.4,.22):.22,
    weather->wind_advection&&seconds>0?weather->wind_drift_x/seconds:clampd(weather->wind_x,-1,1,0),
    weather->wind_advection&&seconds>0?weather->wind_drift_z/seconds:clampd(weather->wind_z,-1,1,0),
    6.28318530718/clampd(weather->cloud_morph_seconds>0?weather->cloud_morph_seconds:180,60,900,180),weather->cloud_seed&0xffffffU);

  if (rain > 0.001) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const double wind_x = finite_or(weather->wind_x, 0.0);
    const double rain_shear = 0.10 + clampd(wind_x, -1.0, 1.0, 0.0) * 0.20;
    int count = (int)clampd((clip.w * (double)clip.h) / (2400.0 * scale * scale) * rain * (0.35+0.65*rain),
                            1, kMaxRainDrops, 240);
    for (int i = 0; i < count; ++i) {
      uint32_t seed = (uint32_t)i * 0x9e3779b9U + 0x7f4aU;
      double depth=0.65+hash01(seed^0x719U)*0.85;
      double gust=std::sin(seconds*.57+depth*2.1)*7.0;
      double x0 = hash01(seed) * clip.w + (seconds*12.0+(weather->wind_advection?weather->wind_drift_x:seconds*wind_x)*18.0+gust) * scale;
      double y0 = hash01(seed ^ 0xc27U) * clip.h + seconds * (180.0 + hash01(seed ^ 0x1d3U) * 120.0) * scale * depth;
      int x = clip.x + (int)wrapd(x0, clip.w);
      int y = clip.y + (int)wrapd(y0, clip.h);
      double patch=field_noise((camera_x+(x-clip.x)/scale)/170.0-seconds*(.065+wind_x*.025)+41.7,
                               (camera_y+(y-clip.y)/scale)/115.0-seconds*.018+41.7);
      double curtain=clampd((patch-.20)/.60,0,1,0);curtain=.22+1.10*curtain*curtain*(3-2*curtain);
      int length = (int)((2.0+hash01(seed^21U)*(5.0+rain*7.0))*scale/depth);
      SDL_SetRenderDrawColor(renderer,179,199,213,byte_clamp((24+depth*30)*curtain*(.6+hash01(seed^39U)*.65)));
      draw_rain_line(renderer,receiver,weather->black_background,x,y,
        x+(int)std::lround(length*(rain_shear+std::sin(seconds*.4+depth)*.035)),y+length);

    }
  }

  draw_mist_fallback(state,renderer,clip,receiver,weather,camera_x,camera_y,scale);
  draw_impacts_fallback(renderer,clip,receiver,weather,camera_x,camera_y,scale,seconds);
  if (!weather->black_background && night_sky && night > 0.01 && valid_receiver(receiver)) {
    draw_moon_glow(renderer, clip, receiver, night);
    draw_sky_points(renderer, clip, receiver, seconds, quality, night);
  }
  double insect_activity=glowbugs?weather_night_activity(weather):0;
  if (insect_activity > 0.001 && night > 0.01 && valid_receiver(receiver))
    draw_glowbugs(renderer, clip, receiver, camera_x, camera_y, scale, seconds, quality, insect_activity);

  draw_fallback_lightning(renderer, clip, receiver, weather, reduced_flashes);

  SDL_SetRenderDrawBlendMode(renderer, old_blend);
  SDL_SetRenderDrawColor(renderer, old_r, old_g, old_b, old_a);
  SDL_RenderSetScale(renderer, old_scale_x, old_scale_y);
  SDL_RenderSetViewport(renderer, &old_viewport);
  SDL_RenderSetClipRect(renderer, had_clip ? &old_clip : nullptr);
}

#ifdef _WIN32

#define FX_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = nullptr; } } while (0)

void release_d3d(EnvironmentFxState *state) {
  if (!state) return;
  FX_RELEASE(state->scene_view);
  FX_RELEASE(state->scene_texture);
  state->scene_width = state->scene_height = 0;
  FX_RELEASE(state->receiver_view);
  FX_RELEASE(state->receiver_texture);
  std::free(state->receiver_upload);state->receiver_upload=nullptr;state->receiver_upload_bytes=0;
  FX_RELEASE(state->raster_state);
  FX_RELEASE(state->blend_state);
  FX_RELEASE(state->constant_buffer);
  FX_RELEASE(state->pixel_shader);
  FX_RELEASE(state->vertex_shader);
  FX_RELEASE(state->confirmed_view);
  state->confirmed_target = nullptr;
  FX_RELEASE(state->context_state);
  FX_RELEASE(state->context1);
  FX_RELEASE(state->context);
  FX_RELEASE(state->device);
  if (state->compiler_module) FreeLibrary(state->compiler_module);
  state->compiler_module = nullptr;
  state->compile = nullptr;
  state->receiver_pixels = nullptr;
  state->receiver_serial = 0;
  state->receiver_width = state->receiver_height = state->receiver_pitch = 0;
  state->receiver_uploaded = 0;
  state->d3d_tried = 0;
  state->d3d_ready = 0;
}

ID3DBlob *compile_blob(CompileFn compile, const char *entry, const char *profile, char *detail, size_t detail_size) {
  if (!compile) return nullptr;
  ID3DBlob *code = nullptr;
  ID3DBlob *errors = nullptr;
  HRESULT hr = compile(kShaderSource, sizeof(kShaderSource) - 1, "environment_fx.hlsl", nullptr,
                       nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
  if (FAILED(hr)) {
    if (detail && detail_size) {
      const char *why = errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "D3DCompile failed";
      std::snprintf(detail, detail_size, "%s: %.120s", entry, why);
    }
    FX_RELEASE(errors);
    FX_RELEASE(code);
    return nullptr;
  }
  FX_RELEASE(errors);
  return code;
}

bool create_d3d_objects(EnvironmentFxState *state, char *detail, size_t detail_size) {
  ID3DBlob *vs_blob = compile_blob(state->compile, "vs_main", "vs_4_0", detail, detail_size);
  ID3DBlob *ps_blob = compile_blob(state->compile, "ps_main", "ps_4_0", detail, detail_size);
  state->shader_compiled = vs_blob && ps_blob;
  bool ok = state->shader_compiled != 0;
  if (ok) {
    ok = SUCCEEDED(ID3D11Device_CreateVertexShader(state->device, ID3D10Blob_GetBufferPointer(vs_blob),
                                                    ID3D10Blob_GetBufferSize(vs_blob), nullptr,
                                                    &state->vertex_shader)) &&
         SUCCEEDED(ID3D11Device_CreatePixelShader(state->device, ID3D10Blob_GetBufferPointer(ps_blob),
                                                   ID3D10Blob_GetBufferSize(ps_blob), nullptr,
                                                   &state->pixel_shader));
  }
  FX_RELEASE(vs_blob);
  FX_RELEASE(ps_blob);
  if (!ok) {
    if (!detail[0]) std::snprintf(detail, detail_size, "D3D11 shader or pipeline object creation failed");
    return false;
  }

  D3D11_BUFFER_DESC cb = {};
  cb.ByteWidth = sizeof(FxConstants);
  cb.Usage = D3D11_USAGE_DYNAMIC;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(ID3D11Device_CreateBuffer(state->device, &cb, nullptr, &state->constant_buffer))) {
    std::snprintf(detail, detail_size, "D3D11 constant buffer creation failed");
    return false;
  }

  D3D11_BLEND_DESC blend = {};
  blend.RenderTarget[0].BlendEnable = TRUE;
  blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
  blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
  blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(ID3D11Device_CreateBlendState(state->device, &blend, &state->blend_state))) {
    std::snprintf(detail, detail_size, "D3D11 blend state creation failed");
    return false;
  }

  D3D11_RASTERIZER_DESC raster = {};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_NONE;
  raster.ScissorEnable = TRUE;
  raster.DepthClipEnable = TRUE;
  if (FAILED(ID3D11Device_CreateRasterizerState(state->device, &raster, &state->raster_state))) {
    std::snprintf(detail, detail_size, "D3D11 raster state creation failed");
    return false;
  }
  return true;
}

bool initialize_d3d(EnvironmentFxState *state, SDL_Renderer *renderer) {
  if (!state || !renderer || state->d3d_tried) return state && state->d3d_ready;
  state->d3d_tried = 1;
  state->shader_compiled = 0;
  ID3D11Device *device = SDL_RenderGetD3D11Device(renderer);
  if (!device) {
    set_detail(state, "renderer has no Direct3D 11 device; using bounded SDL effects");
    return false;
  }
  state->device = device;
  // SDL2 returns an owned reference. release_d3d releases it on both failed
  // initialization and normal teardown; taking another one leaks each reset.
  if(std::getenv("SIMGOLF_RENDER_PROFILE")){
    IDXGIDevice *dxgi=nullptr;IDXGIAdapter *adapter=nullptr;
    if(SUCCEEDED(ID3D11Device_QueryInterface(device,IID_IDXGIDevice,(void **)&dxgi))&&
       SUCCEEDED(IDXGIDevice_GetAdapter(dxgi,&adapter))){
      DXGI_ADAPTER_DESC desc={};
      if(SUCCEEDED(IDXGIAdapter_GetDesc(adapter,&desc))){
        char name[256]={};WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof name,nullptr,nullptr);
        std::fprintf(stderr,"render profile: D3D adapter %s vendor %04x device %04x\n",name,desc.VendorId,desc.DeviceId);
      }
    }
    FX_RELEASE(adapter);FX_RELEASE(dxgi);
  }
  ID3D11Device_GetImmediateContext(state->device, &state->context);
  ID3D11Device1 *device1 = nullptr;
  if (FAILED(ID3D11Device_QueryInterface(state->device, IID_ID3D11Device1, (void **)&device1)) ||
      FAILED(ID3D11DeviceContext_QueryInterface(state->context, IID_ID3D11DeviceContext1,
                                                 (void **)&state->context1))) {
    FX_RELEASE(device1);
    set_detail(state, "Direct3D 11.1 context states unavailable; using bounded SDL effects");
    release_d3d(state);
    state->d3d_tried = 1;
    return false;
  }

  D3D_FEATURE_LEVEL feature = ID3D11Device_GetFeatureLevel(state->device);
  D3D_FEATURE_LEVEL actual = feature;
  UINT create_flags = ID3D11Device_GetCreationFlags(state->device);
  UINT state_flags = (create_flags & D3D11_CREATE_DEVICE_SINGLETHREADED)
                         ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
  HRESULT hr = ID3D11Device1_CreateDeviceContextState(device1, state_flags, &feature, 1,
                                                        D3D11_SDK_VERSION, IID_ID3D11Device,
                                                        &actual, &state->context_state);
  FX_RELEASE(device1);
  if (FAILED(hr)) {
    set_detail(state, "could not create an isolated Direct3D state; using bounded SDL effects");
    release_d3d(state);
    state->d3d_tried = 1;
    return false;
  }

  state->compiler_module = LoadLibraryA("d3dcompiler_47.dll");
  state->compile = state->compiler_module
                       ? (CompileFn)(void *)GetProcAddress(state->compiler_module, "D3DCompile") : nullptr;
  if (!state->compile) {
    set_detail(state, "d3dcompiler_47.dll unavailable; using bounded SDL effects");
    release_d3d(state);
    state->d3d_tried = 1;
    return false;
  }

  char detail[160] = {};
  state->shader_attempted = 1;
  if (!create_d3d_objects(state, detail, sizeof detail)) {
    set_detail(state, detail[0] ? detail : "D3D11 shader setup failed; using bounded SDL effects");
    release_d3d(state);
    state->d3d_tried = 1;
    return false;
  }
  state->d3d_ready = 1;
  state->backend = ENVIRONMENT_FX_BACKEND_D3D11;
  set_detail(state, "Direct3D 11 procedural shader ready");
  return true;
}

bool update_receiver(EnvironmentFxState *state, const EnvironmentFxReceiver *receiver) {
  state->receiver_uploaded = 0;
  if (!valid_receiver(receiver)) return true;
  bool changed = !state->receiver_texture || receiver->pixels != state->receiver_pixels ||
                 receiver->serial != state->receiver_serial || receiver->width != state->receiver_width ||
                 receiver->height != state->receiver_height || receiver->pitch != state->receiver_pitch || receiver->features!=state->receiver_features;
  if (changed) {
    size_t bytes=(size_t)receiver->width*receiver->height*4;
    if(bytes>kMaxReceiverBytes*4)return false;
    if(state->receiver_upload_bytes<bytes){
      uint8_t *upload=(uint8_t *)std::realloc(state->receiver_upload,bytes);if(!upload)return false;
      state->receiver_upload=upload;state->receiver_upload_bytes=bytes;
    }
    for(int y=0;y<receiver->height;y++)for(int x=0;x<receiver->width;x++){
      size_t p=(size_t)y*receiver->width+x;uint8_t *dst=state->receiver_upload+p*4;
      dst[0]=receiver->pixels[(size_t)y*receiver->pitch+x];
      if(receiver->features)std::memcpy(dst+1,receiver->features+p*3,3);
      else{dst[1]=136;dst[2]=dst[3]=0;}
    }
    if (!state->receiver_texture || receiver->width != state->receiver_width ||
        receiver->height != state->receiver_height) {
      FX_RELEASE(state->receiver_view);
      FX_RELEASE(state->receiver_texture);
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = (UINT)receiver->width;
      desc.Height = (UINT)receiver->height;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      D3D11_SUBRESOURCE_DATA data = {};
      data.pSysMem = state->receiver_upload;
      data.SysMemPitch = (UINT)receiver->width*4;
      if (FAILED(ID3D11Device_CreateTexture2D(state->device, &desc, &data, &state->receiver_texture)) ||
          FAILED(ID3D11Device_CreateShaderResourceView(state->device,
                                                        (ID3D11Resource *)state->receiver_texture,
                                                        nullptr, &state->receiver_view))) {
        FX_RELEASE(state->receiver_view);
        FX_RELEASE(state->receiver_texture);
        set_detail(state, "receiver mask upload failed; sky and ground effects are gated off");
        return false;
      }
    } else {
      ID3D11DeviceContext_UpdateSubresource(state->context, (ID3D11Resource *)state->receiver_texture,
                                             0, nullptr, state->receiver_upload,
                                             (UINT)receiver->width*4, 0);
    }
    state->receiver_pixels = receiver->pixels;state->receiver_features=receiver->features;
    state->receiver_serial = receiver->serial;
    state->receiver_width = receiver->width;
    state->receiver_height = receiver->height;
    state->receiver_pitch = receiver->pitch;
  }
  state->receiver_uploaded = state->receiver_view != nullptr;
  return true;
}

// A new SDL target is not necessarily bound by SDL_RenderClear/Flush. Use
// the ordinary SDL fallback to establish it for one frame, then use the GPU
// on subsequent frames. Never change native bindings behind SDL's state cache.
bool confirm_sdl_target(EnvironmentFxState *state, SDL_Renderer *renderer, SDL_Rect clip) {
  SDL_BlendMode blend;
  Uint8 r, g, b, a;
  SDL_GetRenderDrawBlendMode(renderer, &blend);
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  bool ok = SDL_RenderDrawPoint(renderer, clip.x, clip.y) == 0;
  if (SDL_RenderFlush(renderer) != 0) ok = false;
  SDL_SetRenderDrawBlendMode(renderer, blend);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  if (!ok) return false;
  ID3D11RenderTargetView *view = nullptr;
  ID3D11DeviceContext_OMGetRenderTargets(state->context, 1, &view, nullptr);
  if (!view) return false;
  FX_RELEASE(state->confirmed_view);
  state->confirmed_view = view;
  state->confirmed_target = SDL_GetRenderTarget(renderer);
  return true;
}

bool copy_scene(EnvironmentFxState *state, ID3D11RenderTargetView *rtv, const D3D11_TEXTURE2D_DESC &source_desc) {
  if(source_desc.SampleDesc.Count!=1 || !source_desc.Width || !source_desc.Height)return false;
  if(!state->scene_texture || state->scene_width!=source_desc.Width || state->scene_height!=source_desc.Height || state->scene_format!=source_desc.Format) {
    FX_RELEASE(state->scene_view);FX_RELEASE(state->scene_texture);
    D3D11_TEXTURE2D_DESC desc=source_desc;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags=0;desc.MiscFlags=0;
    if(FAILED(ID3D11Device_CreateTexture2D(state->device,&desc,nullptr,&state->scene_texture)) ||
       FAILED(ID3D11Device_CreateShaderResourceView(state->device,(ID3D11Resource*)state->scene_texture,nullptr,&state->scene_view))) {
      FX_RELEASE(state->scene_texture);FX_RELEASE(state->scene_view);return false;
    }
    state->scene_width=desc.Width;state->scene_height=desc.Height;state->scene_format=desc.Format;
  }
  ID3D11Resource *source=nullptr;ID3D11RenderTargetView_GetResource(rtv,&source);
  if(!source)return false;
  ID3D11DeviceContext_CopyResource(state->context,(ID3D11Resource*)state->scene_texture,source);
  FX_RELEASE(source);return true;
}

bool render_d3d(EnvironmentFxState *state, SDL_Renderer *renderer, SDL_Rect clip,
                const EnvironmentFxReceiver *receiver, const WeatherState *weather,
                double camera_x, double camera_y, double scale, int quality,
                int glowbugs, int night_sky, int reduced_flashes) {
  if (!state || !renderer || !state->context1 || !state->context_state) return false;
  if (SDL_RenderFlush(renderer) != 0) return false;
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11DeviceContext_OMGetRenderTargets(state->context, 1, &rtv, nullptr);
  if (!rtv || SDL_GetRenderTarget(renderer) != state->confirmed_target ||
      rtv != state->confirmed_view) {
    FX_RELEASE(rtv);
    set_detail(state, "SDL target transition; bounded SDL effects establish the new target");
    return false;
  }
  ID3D11Resource *resource = nullptr;
  ID3D11RenderTargetView_GetResource(rtv, &resource);
  ID3D11Texture2D *target = nullptr;
  D3D11_TEXTURE2D_DESC target_desc = {};
  bool target_ok = resource &&
                   SUCCEEDED(ID3D11Resource_QueryInterface(resource, IID_ID3D11Texture2D,
                                                           (void **)&target));
  if (target_ok) ID3D11Texture2D_GetDesc(target, &target_desc);
  FX_RELEASE(target);
  FX_RELEASE(resource);
  int logical_width = 0, logical_height = 0;
  SDL_Texture *logical_target = SDL_GetRenderTarget(renderer);
  int logical_status = logical_target
      ? SDL_QueryTexture(logical_target, nullptr, nullptr, &logical_width, &logical_height)
      : SDL_GetRendererOutputSize(renderer, &logical_width, &logical_height);
  if (logical_status != 0 || logical_width != (int)target_desc.Width ||
      logical_height != (int)target_desc.Height) {
    FX_RELEASE(rtv);
    set_detail(state, "SDL target size changed; bounded SDL effects establish the new target");
    return false;
  }
  if (!target_ok || target_desc.Width == 0 || target_desc.Height == 0) {
    FX_RELEASE(rtv);
    set_detail(state, "active target is not a 2D texture; using bounded SDL effects");
    return false;
  }

  clip.x = clip.x < 0 ? 0 : clip.x;
  clip.y = clip.y < 0 ? 0 : clip.y;
  if (clip.x >= (int)target_desc.Width || clip.y >= (int)target_desc.Height) {
    FX_RELEASE(rtv);
    set_detail(state, "clip is outside the active target");
    return false;
  }
  if (clip.w > (int)target_desc.Width - clip.x) clip.w = (int)target_desc.Width - clip.x;
  if (clip.h > (int)target_desc.Height - clip.y) clip.h = (int)target_desc.Height - clip.y;
  if (!clip_valid(clip)) {
    FX_RELEASE(rtv);
    return false;
  }

  // AA alone may reuse at most64MiB. Wet/flash copies retain their existing
  // limits. No CPU readback, frame history or extra retained target per frame.
  float edge_aa=(uint64_t)target_desc.Width*target_desc.Height*4<=64ULL*1024*1024
      ?(float)clampd(weather->edge_aa,0,1,0):0;
  float readable=(uint64_t)target_desc.Width*target_desc.Height*4<=64ULL*1024*1024
      ?(float)clampd(weather->readability,0,1,0):0;
  bool scene_needed=weather->wetness>0.001 || weather->puddles>0.001 || weather->lightning>0.001 || edge_aa>.001 || readable>.001;
  if(scene_needed && !copy_scene(state,rtv,target_desc)) {
    FX_RELEASE(rtv);set_detail(state,"scene copy unavailable; bounded SDL wet surface effects");return false;
  }
  update_receiver(state, receiver);
  FxConstants constants = {};
  constants.air[0]=(float)clampd(weather->mist,0,.45,0);
  constants.air[1]=(float)clampd(weather->haze,0,.15,0);
  constants.air[2]=(float)finite_or(weather->wind_drift_x,0);
  constants.air[3]=(float)finite_or(weather->wind_drift_z,0);
  constants.air_style[0]=(float)clampd(weather->golden,0,1,0);
  constants.air_style[1]=weather->wind_advection?1.0f:0.0f;
  constants.air_style[2]=edge_aa;
  constants.terrain_style[0]=receiver&&receiver->features?(float)clampd(weather->valley_mist,0,1,0):0;
  constants.terrain_style[1]=readable;
  constants.view_material[3]=receiver&&receiver->features?(float)clampd(weather->material_wetness,0,1,0):0;
  for(int i=0;i<3;i++){constants.light[i]=(float)clampd(weather->light[i],-1,1,0);constants.view_material[i]=(float)clampd(weather->eye[i],-1,1,i==1?1:0);}
  constants.light[3]=(float)clampd(weather->light_strength,0,1,0);
  for(int i=0;i<4;i++){
    constants.material_gloss[i]=(float)clampd(weather->material_tuning_valid?weather->material_gloss[i]:material_defaults().material_gloss[i],0,.4,0);
    constants.material_power[i]=(float)clampd(weather->material_tuning_valid?weather->material_power[i]:material_defaults().material_power[i],1,96,4);
    constants.material_absorb[i]=(float)clampd(weather->material_tuning_valid?weather->material_absorb[i]:material_defaults().material_absorb[i],0,.4,0);
  }
  constants.surfaces[0]=(float)clampd(weather->wetness,0,1,0);
  constants.surfaces[1]=(float)clampd(weather->puddles,0,1,0);
  constants.surfaces[2]=(float)clampd(weather->evaporation,0,.3,0);
  constants.surfaces[3]=scene_needed?1.0f:0.0f;
  constants.impacts[0]=(float)clampd(weather->impact_strength,0,1,0);
  constants.impacts[1]=(float)clampd(weather->flash_gain>0?weather->flash_gain:.72,.1,.9,.72);
  constants.impacts[2]=(float)(6.28318530718/clampd(weather->cloud_morph_seconds>0?weather->cloud_morph_seconds:180,60,900,180));
  constants.clouds[0]=(float)clampd(weather->cloud_size>0?weather->cloud_size:1.9,.5,4,1.9);
  constants.clouds[1]=(float)clampd(weather->cloud_shade>0?weather->cloud_shade:.28,.05,.65,.28);
  constants.clouds[2]=(float)clampd(weather->cloud_feather>0?weather->cloud_feather:.22,.02,.4,.22);
  constants.clouds[3]=(float)(weather->cloud_seed&0xffffffU);
  constants.target[0] = (float)target_desc.Width;
  constants.target[1] = (float)target_desc.Height;
  constants.target[2] = 1.0f / constants.target[0];
  constants.target[3] = 1.0f / constants.target[1];
  constants.clip[0] = (float)clip.x;
  constants.clip[1] = (float)clip.y;
  constants.clip[2] = (float)clip.w;
  constants.clip[3] = (float)clip.h;
  constants.camera[0] = (float)clampd(camera_x, -1.0e8, 1.0e8, 0.0);
  constants.camera[1] = (float)clampd(camera_y, -1.0e8, 1.0e8, 0.0);
  constants.camera[2] = (float)clampd(scale, 0.25, 16.0, 1.0);
  constants.camera[3] = (float)wrapd(clampd(weather->seconds, 0.0, 1.0e9, 0.0), 86400.0);
  constants.weather[0] = (float)clampd(weather->cloud, 0.0, 1.0, 0.0);
  constants.weather[1] = (float)clampd(weather->rain, 0.0, 1.0, 0.0);
  constants.weather[2] = (float)clampd(weather->storm, 0.0, 1.0, 0.0);
  constants.weather[3] = (float)clampd(weather->wind_x, -64.0, 64.0, 0.0);
  constants.sky[0] = (float)clampd(weather->wind_z, -64.0, 64.0, 0.0);
  constants.sky[1] = (float)clampd(weather->night, 0.0, 1.0, 0.0);
  constants.sky[2] = (float)clampd(weather->lightning, 0.0, 1.0, 0.0);
  constants.sky[3] = (float)(quality_level(quality) + 1);
  if (valid_receiver(receiver) && state->receiver_view) {
    constants.receiver[0] = (float)receiver->origin_x;
    constants.receiver[1] = (float)receiver->origin_y;
    constants.receiver[2] = (float)receiver->pixel_scale;
    constants.receiver[3] = 1.0f;
    constants.receiver_size[0] = (float)receiver->width;
    constants.receiver_size[1] = (float)receiver->height;
  }
  constants.receiver_size[2] = glowbugs ? (float)weather_night_activity(weather) : 0.0f;
  constants.receiver_size[3] = night_sky ? 1.0f : 0.0f;
  constants.flags[0] = (reduced_flashes ? 1.0f : 0.0f) + (weather->black_background ? 2.0f : 0.0f);
  // Each half is exact in float32. Passing one float drops high event bits.
  constants.flags[1] = (float)(weather->event_id & 0xffffU);
  constants.flags[2] = (float)clampd(weather->strike_age, -1.0, 0.8, -1.0);
  constants.flags[3] = (float)(weather->event_id >> 16);

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(ID3D11DeviceContext_Map(state->context, (ID3D11Resource *)state->constant_buffer,
                                     0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    FX_RELEASE(rtv);
    set_detail(state, "constant buffer map failed; using bounded SDL effects");
    return false;
  }
  std::memcpy(mapped.pData, &constants, sizeof constants);
  ID3D11DeviceContext_Unmap(state->context, (ID3D11Resource *)state->constant_buffer, 0);

  ID3DDeviceContextState *previous = nullptr;
  ID3D11DeviceContext1_SwapDeviceContextState(state->context1, state->context_state, &previous);
  if (!previous) {
    FX_RELEASE(rtv);
    set_detail(state, "Direct3D state swap failed; using bounded SDL effects");
    return false;
  }
  ID3D11DeviceContext_OMSetRenderTargets(state->context, 1, &rtv, nullptr);
  D3D11_VIEWPORT viewport = {0.0f, 0.0f, (float)target_desc.Width, (float)target_desc.Height, 0.0f, 1.0f};
  ID3D11DeviceContext_RSSetViewports(state->context, 1, &viewport);
  D3D11_RECT scissor = {clip.x, clip.y, clip.x + clip.w, clip.y + clip.h};
  ID3D11DeviceContext_RSSetScissorRects(state->context, 1, &scissor);
  ID3D11DeviceContext_RSSetState(state->context, state->raster_state);
  ID3D11DeviceContext_OMSetBlendState(state->context, state->blend_state, nullptr, 0xffffffffU);
  ID3D11DeviceContext_OMSetDepthStencilState(state->context, nullptr, 0);
  ID3D11DeviceContext_IASetInputLayout(state->context, nullptr);
  ID3D11DeviceContext_IASetPrimitiveTopology(state->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(state->context, state->vertex_shader, nullptr, 0);
  ID3D11DeviceContext_PSSetShader(state->context, state->pixel_shader, nullptr, 0);
  ID3D11DeviceContext_PSSetConstantBuffers(state->context, 0, 1, &state->constant_buffer);
  ID3D11DeviceContext_PSSetShaderResources(state->context, 0, 1, &state->receiver_view);
  ID3D11ShaderResourceView *scene_resource=scene_needed?state->scene_view:nullptr;
  ID3D11DeviceContext_PSSetShaderResources(state->context, 1, 1, &scene_resource);
  ID3D11DeviceContext_Draw(state->context, 3, 0);
  ID3D11ShaderResourceView *none = nullptr;
  ID3D11DeviceContext_PSSetShaderResources(state->context, 0, 1, &none);
  ID3D11DeviceContext_PSSetShaderResources(state->context, 1, 1, &none);
  ID3D11RenderTargetView *no_target = nullptr;
  ID3D11DeviceContext_OMSetRenderTargets(state->context, 1, &no_target, nullptr);
  ID3D11DeviceContext1_SwapDeviceContextState(state->context1, previous, nullptr);
  FX_RELEASE(previous);
  FX_RELEASE(rtv);
  state->backend = ENVIRONMENT_FX_BACKEND_D3D11;
  if (valid_receiver(receiver) && !state->receiver_uploaded)
    set_detail(state, "Direct3D 11 shader active; receiver upload unavailable, sky and ground effects gated off");
  else
    set_detail(state, "Direct3D 11 procedural shader active");
  return true;
}

#endif  // _WIN32

}  // namespace

extern "C" void environment_fx_draw(EnvironmentFxState **state_pointer, SDL_Renderer *renderer,
                                      SDL_Rect clip, const EnvironmentFxReceiver *receiver,
                                      const WeatherState *weather, double camera_x,
                                      double camera_y, double scale, int quality,
                                      int glowbugs, int night_sky, int reduced_flashes,
                                      int shader_enabled) {
  if (!state_pointer || !renderer || !weather || !weather->enabled || !clip_valid(clip)) return;
  if (!*state_pointer) *state_pointer = new EnvironmentFxState();
  EnvironmentFxState *state = *state_pointer;
  if (!state) return;
  if (state->renderer != renderer) {
    if (state->cloud_texture) SDL_DestroyTexture(state->cloud_texture);
    state->cloud_texture = nullptr;
#ifdef _WIN32
    release_d3d(state);
#endif
    state->renderer = renderer;
    state->backend = ENVIRONMENT_FX_BACKEND_NONE;
    state->shader_attempted = 0;
    state->shader_compiled = 0;
    set_detail(state, "renderer changed; backend not initialized");
  }
  quality = quality_level(quality);
#ifdef _WIN32
  if (shader_enabled && !state->d3d_tried) initialize_d3d(state, renderer);
  if (shader_enabled && state->d3d_ready &&
      render_d3d(state, renderer, clip, receiver, weather, camera_x, camera_y, scale,
                 quality, glowbugs, night_sky, reduced_flashes)) return;
#else
  (void)shader_enabled;
#endif
  state->backend = ENVIRONMENT_FX_BACKEND_SDL;
  if (!shader_enabled) set_detail(state, "shader disabled; bounded SDL effects active");
  else if (!state->detail[0] || std::strcmp(state->detail, "not initialized") == 0)
    set_detail(state, "bounded SDL effects active");
  state->receiver_uploaded = 0;
  draw_fallback(state, renderer, clip, receiver, weather, camera_x, camera_y, scale,
                quality, glowbugs, night_sky, reduced_flashes);
#ifdef _WIN32
  if (state->d3d_ready) confirm_sdl_target(state, renderer, clip);
#endif
}

extern "C" void environment_fx_destroy(EnvironmentFxState **state_pointer) {
  if (!state_pointer || !*state_pointer) return;
  EnvironmentFxState *state = *state_pointer;
  if (state->cloud_texture) SDL_DestroyTexture(state->cloud_texture);
#ifdef _WIN32
  release_d3d(state);
#endif
  delete state;
  *state_pointer = nullptr;
}

extern "C" void environment_fx_get_status(const EnvironmentFxState *state, EnvironmentFxStatus *status) {
  if (!status) return;
  std::memset(status, 0, sizeof *status);
  if (!state) {
    std::snprintf(status->detail, sizeof status->detail, "not initialized");
    return;
  }
  status->backend = state->backend;
  status->shader_compile_attempted = state->shader_attempted;
  status->shader_compiled = state->shader_compiled;
  status->receiver_uploaded = state->receiver_uploaded;
  std::snprintf(status->detail, sizeof status->detail, "%s", state->detail);
}

#if defined(SIMGOLF_ENVIRONMENT_FX_TESTING)
extern "C" int environment_fx_test_compile_shader(char *detail, size_t detail_size) {
#ifdef _WIN32
  HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
  CompileFn compile = module ? (CompileFn)(void *)GetProcAddress(module, "D3DCompile") : nullptr;
  if (!compile) {
    if (detail && detail_size) std::snprintf(detail, detail_size, "d3dcompiler_47.dll unavailable");
    if (module) FreeLibrary(module);
    return 0;
  }
  ID3DBlob *vs = compile_blob(compile, "vs_main", "vs_4_0", detail, detail_size);
  ID3DBlob *ps = compile_blob(compile, "ps_main", "ps_4_0", detail, detail_size);
  bool ok = vs && ps;
  FX_RELEASE(vs);
  FX_RELEASE(ps);
  FreeLibrary(module);
  if (ok && detail && detail_size) std::snprintf(detail, detail_size, "D3DCompile passed for vs_4_0 and ps_4_0");
  return ok ? 1 : 0;
#else
  if (detail && detail_size) std::snprintf(detail, detail_size, "native D3D11 shader compiler requires Windows");
  return 0;
#endif
}
#endif
