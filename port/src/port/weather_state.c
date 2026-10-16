#define _CRT_SECURE_NO_WARNINGS
#include "port/weather_state.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static double clamp(double x, double lo, double hi) {
  if (!isfinite(x)) return lo;
  return x < lo ? lo : x > hi ? hi : x;
}
static uint32_t mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; return x ^ (x >> 16);
}
static double strike_at(uint32_t index, uint32_t seed) {
  return index * 18.0 + 2 + 4.0 * (mix(index ^ seed) & 65535) / 65535.0;
}
WeatherState weather_state_sample(int enabled, int weather, double intensity,
                                 double seconds, double hour, uint32_t seed) {
  WeatherState s;
  memset(&s, 0, sizeof s);
  s.strike_age = s.thunder_age = -1;
  s.enabled = enabled != 0;
  s.seconds = clamp(seconds, 0, 1e9);
  s.hour = fmod(clamp(hour, 0, 24), 24);
  if (!enabled) return s;
  intensity = clamp(intensity, 0, 1);
  if (weather < 0 || weather > 3) weather = 0;
  s.rain = weather >= 2 ? intensity : 0;
  s.storm = weather == 3 ? intensity : 0;
  s.cloud = (weather == 0 ? 0.15 : weather == 1 ? 0.8 : 1) * intensity;
  s.wind = (weather == 0 ? 0.08 : weather == 1 ? 0.3 : weather == 2 ? 0.5 : 0.9) * intensity;
  double angle = 0.3 + 0.18 * sin(s.seconds * 0.025);
  s.wind_x = cos(angle) * s.wind;
  s.wind_z = sin(angle) * s.wind;
  double day = clamp((sin((s.hour - 6) * 6.283185307179586 / 24) + 0.1) / 0.6, 0, 1);
  s.night = 1 - day * day * (3 - 2 * day);
  if (s.storm <= 0) return s;
  uint32_t index = (uint32_t)(s.seconds / 18);
  double strike = strike_at(index, seed);
  if (s.seconds < strike) {
    if (!index) return s;
    strike = strike_at(--index, seed);
  }
  s.event_id = mix(seed ^ index ^ 0xa3155b21U) | 1U;
  s.strike_age = s.seconds - strike;
  double delay = 1.2 + (mix(index ^ seed ^ 0x1234U) & 65535) / 65535.0 * 2.4;
  s.thunder_age = s.strike_age - delay;
  if (s.strike_age < 0.8) {
    uint32_t pattern=mix(s.event_id^0x7a4d31U);
    double first_width=.16+.05*(pattern&255)/255.0;
    double second_start=.19+.12*((pattern>>8)&255)/255.0;
    double second_width=.12+.06*((pattern>>16)&255)/255.0;
    double first_gain=.85+.15*((pattern>>24)&255)/255.0;
    double second_gain=.45+.50*(mix(pattern^0x41U)&255)/255.0;
    double first=s.strike_age/first_width;
    double second=(s.strike_age-second_start)/second_width;
    double pulse=0;
    // A fast electrical onset followed by an afterglow, then a separate echo.
    if(first>=0&&first<1)pulse+=first_gain*(1-exp(-s.strike_age/.004))*exp(-first*3.8)*(1-first);
    if(second>=0&&second<1)pulse+=second_gain*(1-exp(-(s.strike_age-second_start)/.004))*exp(-second*3.8)*(1-second);
    s.lightning=s.storm*.333*pulse;
  }
  return s;
}

WeatherSurfaceTuning weather_surface_defaults(void) {
  WeatherSurfaceTuning t={18,100,32,.72,.85,.16,1.9,.28,.22,180,1.4,{.025,.042,.18,.001},{4,10,28,4},{.13,.075,.13,.20}}; // Safe fallback; shipped tuning is data/weather-surfaces.csv.
  return t;
}
int weather_condition(const WeatherState *s) {
  if(!s||!s->enabled)return 0;
  if(s->storm>.15&&s->rain>.08)return 5;
  if(s->rain>.55)return 4;
  if(s->rain>.08)return 3;
  if(s->cloud>.62)return 2;
  return s->cloud>.28?1:0;
}
int weather_surface_load(WeatherSurfaceTuning *t, const char *path) {
  if (!t || !path) return 0;
  FILE *f=fopen(path,"r");if(!f)return 0;
  WeatherSurfaceTuning n=*t;char line[160],key[64];double value;
  int seen=0;
  while(fgets(line,sizeof line,f))if(sscanf(line,"%63[^,],%lf",key,&value)==2 && isfinite(value)) {
    if(!strcmp(key,"wet_seconds")){n.wet_seconds=clamp(value,1,600);seen|=1;}
    else if(!strcmp(key,"dry_seconds")){n.dry_seconds=clamp(value,2,1800);seen|=2;}
    else if(!strcmp(key,"puddle_seconds")){n.puddle_seconds=clamp(value,1,600);seen|=4;}
    else if(!strcmp(key,"flash_gain")){n.flash_gain=clamp(value,.1,.9);seen|=8;}
    else if(!strcmp(key,"splash_strength")){n.splash_strength=clamp(value,0,1);seen|=16;}
    else if(!strcmp(key,"evaporation_strength")){n.evaporation_strength=clamp(value,0,.3);seen|=32;}
    else if(!strcmp(key,"cloud_size")){n.cloud_size=clamp(value,.5,4);seen|=64;}
    else if(!strcmp(key,"cloud_shade")){n.cloud_shade=clamp(value,.05,.65);seen|=128;}
    else if(!strcmp(key,"cloud_feather")){n.cloud_feather=clamp(value,.02,.4);seen|=256;}
    else if(!strcmp(key,"cloud_morph_seconds")){n.cloud_morph_seconds=clamp(value,60,900);seen|=512;}
    else if(!strcmp(key,"wet_grass_gloss"))n.material_gloss[1]=clamp(value,0,.4);
    else if(!strcmp(key,"wet_path_gloss"))n.material_gloss[2]=clamp(value,0,.4);
    else if(!strcmp(key,"wet_sand_gloss"))n.material_gloss[3]=clamp(value,0,.04);
    else if(!strcmp(key,"wet_grass_power"))n.material_power[1]=clamp(value,1,96);
    else if(!strcmp(key,"wet_path_power"))n.material_power[2]=clamp(value,1,96);
    else if(!strcmp(key,"wet_sand_power"))n.material_power[3]=clamp(value,1,96);
    else if(!strcmp(key,"wet_grass_absorb"))n.material_absorb[1]=clamp(value,0,.4);
    else if(!strcmp(key,"wet_path_absorb"))n.material_absorb[2]=clamp(value,0,.4);
    else if(!strcmp(key,"wet_sand_absorb"))n.material_absorb[3]=clamp(value,0,.4);
  }
  fclose(f);if(seen!=1023)return 0;*t=n;return 1;
}
void weather_surface_update(WeatherSurfaceState *s,const WeatherSurfaceTuning *t,
                            double rain,double cloud,double night,double wind,double dt) {
  if(!s||!t)return;
  rain=clamp(rain,0,1);cloud=clamp(cloud,0,1);night=clamp(night,0,1);wind=clamp(wind,0,2);dt=clamp(dt,0,1);
  s->wetness=clamp(s->wetness,0,1);s->puddles=clamp(s->puddles,0,1);
  if(rain>.001) {
    s->wetness+=(1-s->wetness)*(1-exp(-dt*rain*3/clamp(t->wet_seconds,1,600)));
    double target=clamp((s->wetness-.25)/.75,0,1)*rain;
    s->puddles+=(target-s->puddles)*(1-exp(-dt/clamp(t->puddle_seconds,1,600)));
    s->evaporation=0;
  } else {
    double drying=(.45+.55*(1-night))*(1-.35*cloud)*(1+.35*wind);
    double loss=3*drying/clamp(t->dry_seconds,2,1800);
    s->wetness*=exp(-dt*loss);s->puddles*=exp(-dt*loss*(t->puddle_drain>0?clamp(t->puddle_drain,.15,2):1.4));
    s->evaporation=s->wetness*drying*clamp(t->evaporation_strength,0,.3);
    if(s->wetness<.002){s->wetness=s->evaporation=0;}
    if(s->puddles<.002)s->puddles=0;
  }
}
