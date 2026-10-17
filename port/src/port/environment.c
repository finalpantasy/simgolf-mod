#include "port/environment.h"
#include <math.h>
#include <stdint.h>

static double bound(double v, double lo, double hi, double fallback) {
  if (!isfinite(v)) return fallback;
  return v < lo ? lo : v > hi ? hi : v;
}
static double wrap(double x, double period) {
  x = fmod(x, period);
  return x < 0 ? x + period : x;
}
static double smooth(double x) { return x * x * (3 - 2 * x); }
static double mix(double a, double b, double t) { return a + (b - a) * t; }

EnvironmentConfig environment_defaults(void) {
  EnvironmentConfig c = {.hour=14,.cycle_minutes=30,.cloud_strength=.65,.rain_strength=.65,
    .lighting=1,.cast_shadows=1,.terrain_shadows=1,.occlusion=1,.quality=1,.shader=1,
    .glowbugs=1,.night_sky=1,.lightning=1,.reduced_flashes=1,.sound=1,.gameplay=0,.weather_volume=.5};
  c.black_background = 1;
  c.mist=c.haze=c.gusts=c.golden_hour=c.distant_thunder=1;
  c.celsius=18;c.humidity=65;c.atmosphere=atmosphere_defaults();
  c.wet_surfaces = c.splashes = 1;
  c.wet_seconds = 18; c.dry_seconds = 100;
  c.weather_wildlife=1;c.linear_light=1;c.edge_aa=.65;
  c.relief_shadows=c.weather_shadows=1;c.enclosure_shade=.55;c.material_wetness=1;c.valley_mist=1;
  c.shadow_cache=1;
  c.lighting_hz=15;
  c.ambient_fill=.7;c.surface_light=.65;c.contact_shadow=.6;c.shadow_softness=1.8;c.night_lights=0;c.light_radius=22;
  c.day_minutes=20; c.night_minutes=10; c.moon_strength=.18;
  c.readability=.8;c.reflections=.44;c.reflection_ripple=.8;c.rainbows=0;c.rainbow_strength=.075;
  return c;
}

double environment_cycle_hour(const EnvironmentConfig *c, double seconds) {
  double day=60*bound(c?c->day_minutes:20,1,120,20);
  double night=60*bound(c?c->night_minutes:10,1,120,10);
  double phase=wrap(bound(seconds,0,1e12,0),day+night);
  return wrap(phase<day?6+12*phase/day:18+12*(phase-day)/night,24);
}

double environment_light_seconds(const EnvironmentConfig *c,double seconds){
  if(!c||!c->shadow_cache||!c->day_night)return seconds;
  double hz=bound(c->lighting_hz,1,60,15);
  return floor(bound(seconds,0,1e9,0)*hz)/hz;
}

EnvironmentSample environment_sample(const EnvironmentConfig *c, double seconds) {
  EnvironmentSample s = {.hour=14,.red=255,.green=255,.blue=255};
  if (!c || !c->enabled) return s;
  seconds = bound(seconds, 0, 1e12, 0);
  s.hour = c->day_night ? environment_cycle_hour(c,seconds) :
    wrap(bound(c->hour, 0, 24, 14), 24);
  // Readable moonlit minimum, warm low sun, neutral midday. Smooth at midnight.
  double sun = sin((s.hour - 6) * 6.283185307179586 / 24);
  double day = smooth(bound((sun + 0.15) / 0.8, 0, 1, 1));
  // Preserve late-afternoon exposure while the key light becomes warmer.
  // This reaches zero at sunset and leaves the moonlit night unchanged.
  double sunlight=smooth(bound(sun/.30,0,1,0))*bound(c->readability,0,1,0);
  day=mix(day,fmax(day,.90),sunlight);
  s.daylight=day;
  double twilight = (1 - fabs(2 * day - 1)) * (s.hour < 12 ? 0.7 : 1);
  double cloud = c->weather == 1 ? 0.8 : c->weather >= 2 ? 1 : 0.2;
  s.cloud = c->regional_weather ? c->regional_cloud : cloud * bound(c->cloud_strength, 0, 1, 0.65);
  s.rain = c->regional_weather ? c->regional_rain : c->weather >= 2 ? bound(c->rain_strength, 0, 1, 0.65) : 0;
  double overcast = c->weather == 1 ? 0.94 : c->weather == 2 ? 0.88 : c->weather == 3 ? 0.82 : 1;
  if (c->regional_weather) overcast = 1 - .06 * s.cloud - .06 * s.rain - .06 * c->regional_storm;
  AtmosphereSample mood=atmosphere_sample(&c->atmosphere,seconds,s.hour,c->celsius,c->humidity,s.cloud,s.rain,0,c->cloud_seed);
  double warmth=c->golden_hour?mood.golden:0;
  // Low sunlight stays readable while its color and direction change.
  day=fmin(1,day+warmth*(1-day)*1.6);
  s.red = (Uint8)lround(255 * mix(0.40, 1, day) * overcast);
  s.green = (Uint8)lround(255 * (mix(0.49, 1, day) - 0.14 * twilight) * overcast);
  s.blue = (Uint8)lround(255 * (mix(0.69, 1, day) - 0.24 * twilight));

  double seasonal=c->season==1?.012:c->season==2?-.008:0;
  double readable=bound(c->readability,0,1,0)*day;
  s.green=(Uint8)lround(bound(s.green*(1-warmth*mix(.28,.07,readable)+seasonal*.3),0,255,255));
  s.blue=(Uint8)lround(bound(s.blue*(1-warmth*mix(.55,.20,readable)-seasonal),0,255,255));
  double angle = (s.hour - 6) * 6.283185307179586 / 24;
  // The full moon travels opposite the sun. Its directional light reveals
  // terrain form and faint tree contours rather than a flat blue night tint.
  double key=sin(angle)>=0?1:-1;
  s.sun_x = key*cos(angle);
  s.sun_y = key*1.4*sin(angle);
  s.sun_z = key*-0.55;
  double length = sqrt(s.sun_x*s.sun_x + s.sun_y*s.sun_y + s.sun_z*s.sun_z);
  s.sun_x /= length; s.sun_y /= length; s.sun_z /= length;
  s.sun_strength = bound(fabs(sin(angle)) * 2, 0, 1, 0) * overcast *
    (key>0?1:bound(c->moon_strength,0,.35,.18));
  return s;
}

int environment_shadow_slope(const EnvironmentSample *s, double ax, double ay,
                             double bx, double by, double *dx, double *dy) {
  if (!dx || !dy) return 0;
  *dx = *dy = 0;
  if (!s || s->sun_y < 0.03 || s->sun_strength < 0.02) return 0;
  double height_squared = ax*ax + bx*bx - ay*ay - by*by;
  if (!isfinite(height_squared) || height_squared < 0.001) return 0;
  double denominator = s->sun_y * sqrt(height_squared);
  *dx = (s->sun_x * ax + s->sun_z * bx) / denominator;
  *dy = (s->sun_x * ay + s->sun_z * by) / denominator;
  double length = hypot(*dx, *dy);
  if (!isfinite(length)) { *dx = *dy = 0; return 0; }
  if (length > 1.8) { *dx *= 1.8 / length; *dy *= 1.8 / length; }
  return 1;
}

static SDL_Texture *make_cloud(SDL_Renderer *r) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 128, 128, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
    double u = (x - 63.5) / 63.5, v = (y - 63.5) / 63.5;
    double radius = sqrt(u*u + v*v);
    double edge = bound(1 - radius, 0, 1, 0);
    double lobes = 0.82 + 0.18 * sin(u * 8 + sin(v * 5));
    Uint8 alpha = (Uint8)lround(48 * smooth(edge) * lobes);
    ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] = (Uint32)alpha << 24;
  }
  SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
  SDL_FreeSurface(s);
  if (t) {
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
  }
  return t;
}

static uint32_t hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; return x ^ (x >> 16);
}

void environment_draw(EnvironmentRenderer *state, SDL_Renderer *r,
                      SDL_Rect course, const EnvironmentConfig *c, double seconds,
                      double camera_x, double camera_y, double scale) {
  if (!state || !r || !c || !c->enabled || course.w <= 0 || course.h <= 0) return;
  EnvironmentSample s = environment_sample(c, seconds);
  seconds = bound(seconds, 0, 1e12, 0);
  scale = bound(scale, 0.25, 16, 1);
  SDL_BlendMode old_blend;
  Uint8 red, green, blue, alpha;
  SDL_Rect old_clip;
  int clipped = SDL_RenderIsClipEnabled(r);
  SDL_GetRenderDrawBlendMode(r, &old_blend);
  SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
  SDL_RenderGetClipRect(r, &old_clip);
  SDL_Rect clip = course;
  if (clipped && !SDL_IntersectRect(&course, &old_clip, &clip)) return;
  SDL_RenderSetClipRect(r, &clip);
  // Multiply the course only. UI is composed later and retains its colours.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_MOD);
  SDL_SetRenderDrawColor(r, s.red, s.green, s.blue, 255);
  SDL_RenderFillRect(r, &course);
  if (s.cloud > 0 && !c->overlay_suppressed) {
    if (!state->cloud) state->cloud = make_cloud(r);
    if (state->cloud) {
      SDL_SetTextureAlphaMod(state->cloud, (Uint8)lround(255 * s.cloud));
      double gx = 320 * scale, gy = 220 * scale;
      double drift_x = wrap(camera_x * scale + seconds * 6 * scale, gx);
      double drift_y = wrap(camera_y * scale + seconds * 2 * scale, gy);
      for (int y = -1; y * gy < course.h + gy; y++) for (int x = -1; x * gx < course.w + gx; x++) {
        SDL_Rect cloud = {(int)(course.x + x * gx - drift_x), (int)(course.y + y * gy - drift_y),
                          (int)(gx * 1.7), (int)(gy * 1.5)};
        SDL_RenderCopy(r, state->cloud, NULL, &cloud);
      }
    }
  }
  if (s.rain > 0 && !c->overlay_suppressed) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 195, 220, 238, (Uint8)(100 * s.rain));
    int drops = (int)bound(course.w * (double)course.h / (8500 * scale * scale), 32, 500, 100);
    for (int i = 0; i < drops; i++) {
      double a = hash((uint32_t)i + 13) / 4294967296.0;
      double b = hash((uint32_t)i + 797) / 4294967296.0;
      double speed = (320 + a * 180) * scale;
      int x = course.x + (int)wrap(a * course.w - seconds * speed * 0.18, course.w);
      int y = course.y + (int)wrap(b * course.h + seconds * speed, course.h);
      SDL_RenderDrawLine(r, x, y, x - (int)(3 * scale), y + (int)((9 + a * 9) * scale));
    }
  }
  SDL_SetRenderDrawBlendMode(r, old_blend);
  SDL_SetRenderDrawColor(r, red, green, blue, alpha);
  SDL_RenderSetClipRect(r, clipped ? &old_clip : NULL);
}

void environment_destroy(EnvironmentRenderer *s) {
  if (s && s->cloud) { SDL_DestroyTexture(s->cloud); s->cloud = NULL; }
}

double environment_shadow_transmission(const EnvironmentConfig *c,const EnvironmentSample *s){
  if(!c||!s)return 0;double cover=bound(s->cloud,0,1,0);
  if(!c->weather_shadows)return 1-.65*cover;
  double clear=1-cover*cover;return clear*clear;
}
double environment_shadow_softness(const EnvironmentConfig *c,const EnvironmentSample *s){
  if(!c||!s)return 0;double base=bound(c->shadow_softness,0,4,0);
  return c->weather_shadows?fmin(8,base*(1+2.4*bound(s->cloud,0,1,0))):base;
}
