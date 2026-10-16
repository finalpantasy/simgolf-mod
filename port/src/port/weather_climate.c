#include "port/weather_climate.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t hash(uint32_t v) {
  v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; return v ^ (v >> 16);
}
static uint32_t place_hash(const char *s) {
  uint32_t h = 2166136261U;
  for (int i = 0; s && s[i] && i < 31; ++i) h = (h ^ (unsigned char)s[i]) * 16777619U;
  return h;
}
static double unit(uint32_t v) { return (hash(v) & 65535) / 65535.0; }
static const char *fallback(int theme) {
  static const char *names[] = {"Parkland", "Desert", "Tropical", "Links"};
  return names[theme >= 0 && theme < 4 ? theme : 0];
}
static const WeatherClimateRow *find(const WeatherClimateTable *t, const char *place, int season) {
  if (!t || !place) return NULL;
  for (int i = 0; i < t->count; ++i)
    if (t->rows[i].season == season && !strcmp(t->rows[i].place, place)) return &t->rows[i];
  return NULL;
}

static const WeatherAirRow *air_find(const WeatherAirTable *t,const char *place) {
  if(t&&place)for(int i=0;i<t->count;++i)if(!strcmp(t->rows[i].place,place))return &t->rows[i];
  return NULL;
}
int weather_air_load(WeatherAirTable *table,const char *path) {
  if(!table||!path)return 0;
  FILE *f=fopen(path,"r");if(!f)return 0;
  WeatherAirTable candidate={0};char line[320];int valid=1;
  while(fgets(line,sizeof line,f)) {
    if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;
    WeatherAirRow row={0};char extra;
    int n=sscanf(line,"%31[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf %c",row.place,
      &row.c[0],&row.c[1],&row.c[2],&row.rh[0],&row.rh[1],&row.rh[2],&row.day_swing,&row.spell_swing,&extra);
    if(n!=9||candidate.count>=WEATHER_AIR_ROWS||air_find(&candidate,row.place)){valid=0;break;}
    for(int i=0;i<3;++i)if(!isfinite(row.c[i])||row.c[i]<-40||row.c[i]>50||
      !isfinite(row.rh[i])||row.rh[i]<5||row.rh[i]>95)valid=0;
    if(!isfinite(row.day_swing)||row.day_swing<0||row.day_swing>25||
       !isfinite(row.spell_swing)||row.spell_swing<0||row.spell_swing>20)valid=0;
    if(!valid)break;candidate.rows[candidate.count++]=row;
  }
  if(ferror(f))valid=0;fclose(f);
  for(int i=0;i<4;++i)if(!air_find(&candidate,fallback(i)))valid=0;
  if(!valid)return 0;*table=candidate;return candidate.count;
}
static double air_bound(double v,double lo,double hi,double value) {
  return !isfinite(v)?value:v<lo?lo:v>hi?hi:v;
}
void weather_air_update(WeatherAir *s,const WeatherAirTable *t,const char *place,
                        int theme,uint32_t tick,uint32_t seed,uint32_t front,
                        double seconds,double hour,double rain,double cloud) {
  if(!s||!t||!t->count||!isfinite(seconds)||seconds<0)return;
  const WeatherAirRow *row=air_find(t,place);if(!row)row=air_find(t,fallback(theme));if(!row)return;
  uint32_t identity=place_hash(place)^hash(seed)^hash((uint32_t)theme);
  if(s->initialized&&(s->identity!=identity||seconds<s->last))memset(s,0,sizeof *s);
  unsigned month=(tick>>10)&7,season=month<3?0:month<6?1:2;
  hour=air_bound(hour,0,24,14);rain=air_bound(rain,0,1,0);cloud=air_bound(cloud,0,1,0);
  double daylight=cos((hour-15)*6.28318530718/24);
  double spell=(unit(identity^hash(front)^0x94a1U)*2-1)*row->spell_swing;
  double c=row->c[season]+daylight*row->day_swing*.5+spell-rain*3-cloud*1.5;
  double rh=row->rh[season]-daylight*7+(unit(identity^hash(front)^0x71e2U)*2-1)*8+cloud*3;
  rh+=(96-rh)*rain;
  c=air_bound(c,-45,55,20);rh=air_bound(rh,5,100,65);
  double dt=s->initialized?air_bound(seconds-s->last,0,.25,0):0;
  double blend=s->initialized?1-exp(-dt/45):1;
  s->c+=(c-s->c)*blend;s->rh+=(rh-s->rh)*blend;
  s->initialized=1;s->identity=identity;s->last=seconds;
}
int weather_climate_load(WeatherClimateTable *table, const char *path) {
  if (!table || !path) return 0;
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  WeatherClimateTable candidate = {0};
  char line[256];
  int valid = 1;
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    WeatherClimateRow row = {0};
    char extra;
    int n = sscanf(line, "%31[^,],%d,%d,%d,%d,%d,%lf,%lf,%lf %c", row.place, &row.season,
      &row.weight[0], &row.weight[1], &row.weight[2], &row.weight[3], &row.rain_min, &row.rain_max, &row.year_swing, &extra);
    int total = 0;
    if (n != 9 || candidate.count >= WEATHER_CLIMATE_ROWS || row.season < 0 || row.season > 2 ||
        !isfinite(row.rain_min) || !isfinite(row.rain_max) || row.rain_min < 0 ||
        row.rain_max > 1 || row.rain_min > row.rain_max || !isfinite(row.year_swing) || row.year_swing < 0 ||
        row.year_swing > .5 || find(&candidate, row.place, row.season)) { valid = 0; break; }
    for (int i = 0; i < 4; ++i) {
      if (row.weight[i] < 1 || row.weight[i] > 100) valid = 0;
      total += row.weight[i];
    }
    if (!valid || total != 100) { valid = 0; break; }
    candidate.rows[candidate.count++] = row;
  }
  if (ferror(f)) valid = 0;
  fclose(f);
  // Each location must have all seasons, including four unknown-location fallbacks.
  for (int i = 0; i < candidate.count; ++i)
    for (int s = 0; s < 3; ++s) if (!find(&candidate, candidate.rows[i].place, s)) valid = 0;
  for (int theme = 0; theme < 4; ++theme)
    for (int s = 0; s < 3; ++s) if (!find(&candidate, fallback(theme), s)) valid = 0;
  if (!valid) return 0;
  *table = candidate;
  return candidate.count;
}
void weather_climate_update(WeatherClimate *s, const WeatherClimateTable *t,
                           const char *place, int theme, uint32_t tick,
                           uint32_t seed, double seconds, double minutes) {
  if (!s || !t || !t->count || !isfinite(seconds) || seconds < 0) return;
  if (!isfinite(minutes)) minutes = 4;
  minutes = fmax(1, fmin(30, minutes));
  uint32_t identity = place_hash(place) ^ hash((uint32_t)theme) ^ hash(seed);
  if (s->initialized && (s->identity != identity || seconds < s->last)) {
    double lead=s->lead_seconds;memset(s,0,sizeof *s);s->lead_seconds=lead;
  }
  double dt = s->initialized ? fmin(.25, fmax(0, seconds - s->last)) : 0;
  int first = !s->initialized;
  if (first || seconds >= s->next) {
    unsigned month = (tick >> 10) & 7;
    s->season = month < 3 ? 0 : month < 6 ? 1 : 2;
    const WeatherClimateRow *row = find(t, place, s->season);
    if (!row) row = find(t, fallback(theme), s->season);
    if (!row) return;
    uint32_t h = hash(seed ^ identity ^ hash(tick) ^ hash(++s->front));
    s->year_rain_scale = 1 + row->year_swing * (2 * unit(seed ^ identity ^ hash(tick >> 13) ^ 0x7ea2U) - 1);
    double weights[4], total = 0;
    for (int i = 0; i < 4; ++i) {
      weights[i] = row->weight[i] * (i == 0 ? 2 - s->year_rain_scale : i >= 2 ? s->year_rain_scale : 1);
      total += weights[i];
    }
    double choice = unit(h) * total;
    s->kind = 0;
    while (s->kind < 3 && choice >= weights[s->kind]) choice -= weights[s->kind++];
    s->target_rain = s->kind >= 2 ? row->rain_min + (row->rain_max - row->rain_min) * unit(h ^ 0x52U) : 0;
    s->target_storm = s->kind == 3 ? s->target_rain : 0;
    s->target_cloud = s->kind == 0 ? .12 + .22 * unit(h ^ 0x13U) :
      s->kind == 1 ? .5 + .35 * unit(h ^ 0x14U) : .85 + .15 * unit(h ^ 0x15U);
    s->next = seconds + minutes * 60 * (.75 + .5 * unit(h ^ 0x71U));
    s->front_started=seconds;
    s->initialized = 1; s->identity = identity;
    if (first) { s->rain = s->target_rain; s->cloud = s->target_cloud; s->storm = s->target_storm; }
  }
  // Clouds gather faster than rainfall; rain and thunder fade smoothly on clearing.
  double cloud_blend = 1 - exp(-dt / 8), rain_blend = 1 - exp(-dt / 16);
  s->cloud += (s->target_cloud - s->cloud) * cloud_blend;
  int gathering=s->target_rain>s->rain&&seconds-s->front_started<fmax(0,fmin(60,s->lead_seconds));
  if(!gathering)s->rain += (s->target_rain - s->rain) * rain_blend;
  if(!gathering)s->storm += (s->target_storm - s->storm) * rain_blend;
  if (s->target_rain == 0 && s->rain < .001) s->rain = 0;
  if (s->target_storm == 0 && s->storm < .001) s->storm = 0;
  s->last = seconds;
}
