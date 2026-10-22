// The profile's source pieces drawn directly into a native-resolution image.
// Source indices retain color-key and clothing semantics; optional existing HD
// sheets provide detail. This never changes a golfer record or save.
#include "port/portrait_game.h"
#include "port/host.h"
#include "port/runtime.h"
#include "port/hd_art.h"
#include "port/reports_frosted.h"
#include "port/golfers_frosted.h"
#include "mods/mods.h"
#include "port/roster.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int portrait_memory_readable(uintptr_t p, unsigned n);
unsigned FUN_004675d0(int slot);
#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SHEETS 64
#define HI_BUDGET (96u * 1024u * 1024u)
typedef struct {
  char name[128];
  int w, h, hw, hh, tried, painted;
  uint8_t *idx, pal[768];
  uint32_t *hi, used;
} Sheet;
static Sheet sheets[SHEETS];
static unsigned nsheet, tick;
static size_t hi_bytes;
static char art_dir[512];
static char portrait_dir[512];
typedef struct { char file[1024]; uint32_t *px; int w,h,x0,y0,x1,y1; unsigned used; } Override;
static Override overrides[4];
static int use_hd = -1;
static uint32_t profile_key, card_key, preview_key;
static int profile_art = -1, card_art = -1;

static uint32_t hash(const void *v, size_t n, uint32_t h) {
  const uint8_t *p = v;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h ? h : 1;
}
static int enabled(void) {
  const char *v = mods_get("ui.portraits");
  return !v || (strcmp(v, "original") && strcmp(v, "off"));
}
void portrait_profile_reset(void) { profile_key = 0; }
static void settings(void) {
  char dir[512] = {0};
  int hd = hd_art_portrait_source(dir, sizeof dir);
  const char *extra = getenv("SIMGOLF_PORTRAITS_DIR");
  if (!extra || !*extra) extra = mods_get("art.portraits_dir");
  char bundled[512];
  if (!extra || !*extra) {
    snprintf(bundled, sizeof bundled, "%s/ui/portraits", host_exe_dir());
    extra = bundled;
  }
  if (hd == use_hd && !strcmp(dir, art_dir) && !strcmp(extra, portrait_dir)) return;
  for (unsigned i = 0; i < nsheet; i++) {
    free(sheets[i].hi); sheets[i].hi = NULL; sheets[i].tried = sheets[i].painted = 0;
  }
  hi_bytes = 0; use_hd = hd;
  snprintf(art_dir, sizeof art_dir, "%s", dir);
  snprintf(portrait_dir, sizeof portrait_dir, "%s", extra);
  for (unsigned i=0;i<4;i++) { free(overrides[i].px); memset(&overrides[i],0,sizeof overrides[i]); }
  profile_key = card_key = preview_key = 0;
}
uint32_t *portrait_face_image(int character,int *w,int *h) {
  *w=*h=0;if(character<0 || character>=ROSTER_ADDITIONS || !roster_enabled())return NULL;
  settings();if(!portrait_dir[0])return NULL;
  char file[1024];snprintf(file,sizeof file,"%s/new-%02d-expression-0.png",portrait_dir,character+1);
  uint32_t *px=hd_art_load_png(file,w,h);
  if(px && (*w<=0 || *h<=0 || *w>2048 || *h>2048)){free(px);px=NULL;*w=*h=0;}
  return px;
}
uint32_t *portrait_original_face_image(int male,int head,int *w,int *h) {
  *w=*h=0;if(!enabled() || head<0 || head>=19)return NULL;
  settings();if(!portrait_dir[0])return NULL;
  char file[1024];snprintf(file,sizeof file,"%s/%s-%02d-expression-0.png",portrait_dir,male?"male":"female",head);
  uint32_t *px=hd_art_load_png(file,w,h);
  if(px && (*w<=0 || *h<=0 || *w>2048 || *h>2048)){free(px);px=NULL;*w=*h=0;}
  return px;
}
static int read_pcx(Sheet *s) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", !strncmp(s->name,"Preview/",8) ? portrait_dir : host_game_dir(), s->name);
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *b = n > 896 && n < (32 << 20) ? malloc((size_t)n) : NULL;
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
  fclose(f);
  if (!b) return 0;
  int w = (b[8] | b[9] << 8) - (b[4] | b[5] << 8) + 1;
  int h = (b[10] | b[11] << 8) - (b[6] | b[7] << 8) + 1;
  int stride = b[66] | b[67] << 8;
  int ok = b[0] == 10 && b[2] == 1 && b[3] == 8 && b[65] == 1 &&
      w > 0 && h > 0 && w <= 8192 && h <= 8192 && stride >= w &&
      (size_t)w * h <= (32u << 20) && b[n - 769] == 12;
  uint8_t *idx = ok ? malloc((size_t)w * h) : NULL;
  if (!idx) ok = 0;
  size_t p = 128;
  for (int y = 0; y < h && ok; y++) {
    int x = 0;
    while (x < stride) {
      if (p >= (size_t)n - 769) { ok = 0; break; }
      int c = b[p++], run = 1;
      if (c >= 0xc0) {
        run = c & 63;
        if (!run || p >= (size_t)n - 769) { ok = 0; break; }
        c = b[p++];
      }
      if (run > stride - x) { ok = 0; break; }
      while (run--) { if (x < w) idx[(size_t)y * w + x] = (uint8_t)c; x++; }
    }
  }
  if (ok) {
    s->w = w; s->h = h; s->idx = idx;
    memcpy(s->pal, b + n - 768, 768);
  } else free(idx);
  free(b);
  return ok;
}
static Sheet *sheet(const char *name) {
  Sheet *s = NULL;
  for (unsigned i = 0; i < nsheet; i++) if (!strcmp(sheets[i].name, name)) s = &sheets[i];
  if (!s) {
    if (nsheet == SHEETS) return NULL;
    s = &sheets[nsheet++];
    snprintf(s->name, sizeof s->name, "%s", name);
    if (!read_pcx(s)) return NULL;
  }
  if (!s->idx) return NULL;
  s->used = ++tick;
  if ((use_hd || portrait_dir[0]) && !s->tried) {
    s->tried = 1;
    char rel[128], path[1024];
    snprintf(rel, sizeof rel, "%s", !strncmp(name,"Preview/",8) ? name+8 : name);
    char *dot = strrchr(rel, '.'); if (dot) *dot = 0;
    for (char *p = rel; *p; p++) if (*p == ' ') *p = '_';
    snprintf(path, sizeof path, "%s/%s.png", portrait_dir, rel);
    int w = 0, h = 0;
    uint32_t *px = portrait_dir[0] && (!strncmp(name,"Bodies/",7) || !strncmp(name,"Preview/",8)) ? hd_art_load_png(path, &w, &h) : NULL;
    s->painted = px != NULL;
    if (!px && use_hd) {
      snprintf(path, sizeof path, "%s/%s.png", art_dir, rel);
      px = hd_art_load_png(path, &w, &h);
    }
    size_t bytes = (size_t)w * h * 4;
    if (px && w >= s->w && h >= s->h && w % s->w == 0 &&
        h % s->h == 0 && w / s->w == h / s->h && w / s->w <= 8 && bytes <= HI_BUDGET) {
      while (hi_bytes + bytes > HI_BUDGET) {
        Sheet *old = NULL;
        for (unsigned i = 0; i < nsheet; i++)
          if (sheets[i].hi && (&sheets[i] != s) && (!old || sheets[i].used < old->used)) old = &sheets[i];
        if (!old) break;
        hi_bytes -= (size_t)old->hw * old->hh * 4;
        free(old->hi); old->hi = NULL; old->tried = 0;
      }
      s->hi = px; s->hw = w; s->hh = h; hi_bytes += bytes;
    } else free(px);
  }
  return s;
}
static int working_palette(uint16_t pal[256]) {
  uint32_t b = R32(0x81ca14);
  if (!portrait_memory_readable(b, 8)) return 0;
  uint32_t vt = R32(b);
  if (!portrait_memory_readable(vt + 24, 4)) return 0;
  uint32_t flags = 0;
  IcallFn fn = icall_find(R32(vt + 24), &flags);
  if (!fn) return 0;
  uint32_t p = fn((flags & 1) ? b : 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  if (!portrait_memory_readable(p, 512)) return 0;
  memcpy(pal, (const void *)(uintptr_t)p, 512);
  return 1;
}
static int clamp(int c) { return c < 0 ? 0 : c > 255 ? 255 : c; }
static uint32_t over(uint32_t prev,uint32_t src) {
  int a=src>>24;if(!a)return prev;if(a==255||!(prev>>24))return src;
  int weight=((prev>>24)*(255-a)+127)/255,oa=a+weight;
  int r=((((src>>16)&255)*a+((prev>>16)&255)*weight)+oa/2)/oa;
  int g=((((src>>8)&255)*a+((prev>>8)&255)*weight)+oa/2)/oa;
  int b=(((src&255)*a+(prev&255)*weight)+oa/2)/oa;
  return (uint32_t)oa<<24|(uint32_t)r<<16|(uint32_t)g<<8|(uint32_t)b;
}

// An optional, explicitly selected face variant. Clothes remain separate.
// Missing skin/hair/expression variants retain the real source face.
static Override *face_override(int male,int head,int expr,int child,int profile) {
  if (!portrait_dir[0]) return NULL;
  const uint8_t *r=(const uint8_t *)(uintptr_t)(roster_address(0x4d6088)+profile*0x230);
  char file[1024];
  snprintf(file,sizeof file,"%s/%s%s-%02d-skin-%02d-hair-%02d-expression-%d.png",portrait_dir,
      male?"male":"female",child?"-child":"",head,r[0x27],r[0x28],expr);
  int character=roster_character(profile);
  if (character>=0)snprintf(file,sizeof file,"%s/new-%02d-expression-%d.png",portrait_dir,character+1,expr);
  Override *o=NULL;
  for(unsigned i=0;i<4;i++) if(!strcmp(overrides[i].file,file)) o=&overrides[i];
  if(!o) {
    o=&overrides[0];for(unsigned i=1;i<4;i++)if(overrides[i].used<o->used)o=&overrides[i];
    free(o->px);memset(o,0,sizeof *o);snprintf(o->file,sizeof o->file,"%s",file);
    o->px=hd_art_load_png(file,&o->w,&o->h);
    if (!o->px) {
      char generic[1024];
      snprintf(generic,sizeof generic,"%s/%s-%02d-expression-%d.png",portrait_dir,male?"male":"female",head,expr);
      o->px=hd_art_load_png(generic,&o->w,&o->h);
    }
    if(o->px && o->w>0 && o->h>0 && (size_t)o->w*o->h<=(16u<<20)) {
      o->x0=o->w;o->y0=o->h;
      for(int y=0;y<o->h;y++)for(int x=0;x<o->w;x++)if((o->px[(size_t)y*o->w+x]>>24)>8) {
        if(x<o->x0)o->x0=x;if(y<o->y0)o->y0=y;
        if(x+1>o->x1)o->x1=x+1;if(y+1>o->y1)o->y1=y+1;
      }
      if(o->x1<=o->x0 || o->y1<=o->y0){free(o->px);o->px=NULL;}
    } else {free(o->px);o->px=NULL;}
  }
  o->used=++tick;
  return o->px?o:NULL;
}
static int replacement(uint32_t *dst,int dw,int dh,Sheet *s,int sx,int sy,int w,int h,
                       int dx,int dy,int male,int head,int expr,int child,int profile) {
  Override *o=face_override(male,head,expr,child,profile);
  if(!o || !s || sx<0 || sy<0 || sx+w>s->w || sy+h>s->h) return 0;
  int x0=w,y0=h,x1=0,y1=0;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(s->idx[(size_t)(sy+y)*s->w+sx+x]<254) {
    if(x<x0)x0=x;if(y<y0)y0=y;if(x+1>x1)x1=x+1;if(y+1>y1)y1=y+1;
  }
  if(x1<=x0 || y1<=y0) return 0;
  float kx=(x1-x0)*4.0f/(o->x1-o->x0),ky=(y1-y0)*4.0f/(o->y1-o->y0);
  float k=kx<ky?kx:ky;
  int W=(int)((o->x1-o->x0)*k+0.5f),H=(int)((o->y1-o->y0)*k+0.5f);
  int X=(dx+x0)*4+((x1-x0)*4-W)/2,Y=(dy+y1)*4-H;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++) {
    int ox=X+x,oy=Y+y;if(ox<0||oy<0||ox>=dw||oy>=dh)continue;
    int ix=o->x0+(int)((x+0.5f)*(o->x1-o->x0)/W);
    int iy=o->y0+(int)((y+0.5f)*(o->y1-o->y0)/H);
    if(ix>=o->x1)ix=o->x1-1;if(iy>=o->y1)iy=o->y1-1;
    uint32_t p=o->px[(size_t)iy*o->w+ix];
    if((p>>24)>8)dst[(size_t)oy*dw+ox]=over(dst[(size_t)oy*dw+ox],p);
  }
  return 1;
}
static void piece(uint32_t *dst, int dw, int dh, Sheet *s, int sx, int sy,
                  int w, int h, int dx, int dy, const uint16_t *pal) {
  if (!s || sx < 0 || sy < 0 || sx + w > s->w || sy + h > s->h) return;
  int k = s->hi ? s->hw / s->w : 1;
  for (int y = 0; y < h * 4; y++) for (int x = 0; x < w * 4; x++) {
    int ox = dx * 4 + x, oy = dy * 4 + y;
    if (ox < 0 || oy < 0 || ox >= dw || oy >= dh) continue;
    int ix = sx + x / 4, iy = sy + y / 4, index = s->idx[(size_t)iy * s->w + ix];
    if (index >= 254) continue;
    int r = s->pal[index * 3], g = s->pal[index * 3 + 1], b = s->pal[index * 3 + 2], a = 255;
    if (s->hi) {
      uint32_t p = s->hi[(size_t)(sy * k + y * k / 4) * s->hw + sx * k + x * k / 4];
      a = p >> 24; r = (p >> 16) & 255; g = (p >> 8) & 255; b = p & 255;
    }
    if (pal) {
      uint16_t c = pal[index];
      int nr = (c >> 10) & 31, ng = (c >> 5) & 31, nb = c & 31;
      if (s->painted) {
        // Painted cloth supplies light/detail, original indices own material hue.
        // This prevents generated mask colors bleeding across a source boundary.
        int light=r>g?r:g; if(b>light)light=b;
        int base=s->pal[index*3];if(s->pal[index*3+1]>base)base=s->pal[index*3+1];
        if(s->pal[index*3+2]>base)base=s->pal[index*3+2];
        float gain=base>16?(float)light/base:1.0f;
        if(gain<0.5f)gain=0.5f;if(gain>1.5f)gain=1.5f;
        r=clamp((int)(((nr<<3)|(nr>>2))*gain));
        g=clamp((int)(((ng<<3)|(ng>>2))*gain));
        b=clamp((int)(((nb<<3)|(nb>>2))*gain));
      } else {
        r = clamp(r + ((nr << 3) | (nr >> 2)) - s->pal[index * 3]);
        g = clamp(g + ((ng << 3) | (ng >> 2)) - s->pal[index * 3 + 1]);
        b = clamp(b + ((nb << 3) | (nb >> 2)) - s->pal[index * 3 + 2]);
      }
    }
    if (!a) continue;
    uint32_t src=(uint32_t)a<<24|(uint32_t)r<<16|(uint32_t)g<<8|(uint32_t)b;
    dst[(size_t)oy*dw+ox]=over(dst[(size_t)oy*dw+ox],src);
  }
}
static int identity(int profile, int slot, int *male, int *head, int *expr, int *body, int *child) {
  if (!roster_profile_valid(profile) || slot < 0 || slot >= 0x9c) return 0;
  const uint8_t *r = (const uint8_t *)(uintptr_t)(roster_address(0x4d6088) + profile * 0x230);
  *male = !(r[0x21] & 128); *head = (int8_t)r[0x22];
  // Authored companions may use allocated custom-head indices. The painted
  // identity has its own stable namespace; stock head8 supplies only framing.
  if (roster_character(profile)>=0)*head=8;
  if (*head < 0 || *head >= 19) return 0; // Imported/custom heads retain their actual original picture.
  *expr = (int)FUN_004675d0(slot);
  if (*expr < 0 || *expr > 2) return 0;
  *body = (r[0x23] >> 4) & 3; *child = !!(r[0x2c] & 8);
  return 1;
}

// The paired match skill cards use the original page, not the profile panel.
// Reuse the same selected face and fitting as the profile before the original
// head is drawn; body, skill bars, clicks and missing-art fallback stay original.
uint16_t *gdi_dib_at(uintptr_t, int *, int *, int *, int *);
int portrait_skill_head(int slot, int x, int y) {
  if (!enabled() || slot < 0 || slot >= 156) return 0;
  settings();
  int profile = *(int16_t *)(uintptr_t)(0x57956e + slot * 256);
  int male, head, expr, body, child;
  if (!identity(profile, slot, &male, &head, &expr, &body, &child)) return 0;
  uint32_t wrap = R32(0x4c1570);
  if (!portrait_memory_readable(wrap, 8)) return 0;
  uint32_t surface = R32(wrap + 4);
  if (!portrait_memory_readable(surface, 0x4c4)) return 0;
  int width, height, pitch, is565;
  uint16_t *pixels = gdi_dib_at(R32(surface + 0x4c0), &width, &height, &pitch, &is565);
  if (!pixels) return 0;
  Sheet *hs = sheet(male ? "Heads/sim_MALE_all_expressionsflat.pcx" : "Heads/sim_FEMALE_all_expressionsflat.pcx");
  if (!hs) return 0;
  uint32_t *face = calloc(360 * 480, sizeof *face);
  if (!face) return 0;
  int replaced = replacement(face,360,480,hs,(head/2)*100,(head&1)*372+4+expr*124,
                             90,120,0,0,male,head,expr,child,profile);
  if (replaced) for (int dy = 0; dy < 120; dy++) for (int dx = 0; dx < 90; dx++) {
    int ox = x + dx, oy = y - 20 + dy;
    if (ox < 0 || oy < 0 || ox >= width || oy >= height) continue;
    unsigned a=0,r=0,g=0,b=0;
    for (int sy=0;sy<4;sy++) for (int sx=0;sx<4;sx++) {
      uint32_t v=face[(dy*4+sy)*360+dx*4+sx]; unsigned alpha=v>>24;
      a+=alpha; r+=((v>>16)&255)*alpha; g+=((v>>8)&255)*alpha; b+=(v&255)*alpha;
    }
    if (!a) continue;
    r/=a; g/=a; b/=a; a=(a+8)/16;
    uint16_t *dst=pixels+(ptrdiff_t)oy*pitch+ox, old=*dst;
    unsigned dr=((old>>(is565?11:10))&31)*255/31;
    unsigned dg=((old>>5)&(is565?63:31))*255/(is565?63:31);
    unsigned db=(old&31)*255/31;
    r=(r*a+dr*(255-a)+127)/255; g=(g*a+dg*(255-a)+127)/255; b=(b*a+db*(255-a)+127)/255;
    *dst=(uint16_t)((r>>3)<<(is565?11:10) | (g>>(is565?2:3))<<5 | b>>3);
  }
  free(face);
  if (replaced && getenv("SIMGOLF_PORTRAITS_LOG")) fprintf(stderr,
    "portrait: skill card slot %d profile %d head %d expression %d character %d\n",
    slot,profile,head,expr,roster_character(profile));
  return replaced;
}

int portrait_profile(int profile, int art) {
  if (!enabled()) { profile_key = 0; return 0; }
  settings();
  int male, head, expr, body, child;
  if (!identity(profile, 0x99, &male, &head, &expr, &body, &child)) { profile_key = 0; return 0; }
  uint16_t pal[256]; if (!working_palette(pal)) { profile_key = 0; return 0; }
  const uint8_t *r=(const uint8_t *)(uintptr_t)(roster_address(0x4d6088)+profile*0x230);
  int values[] = {profile, male, head, expr, body, child, use_hd, r[0x27], r[0x28], roster_character(profile)};
  uint32_t key = hash(pal, sizeof pal, hash(values, sizeof values, 2166136261u));
  if (key == profile_key && art == profile_art) return 1;
  static const char *names[2][4] = {{"FemalePLS","FemaleSSS","FemalePSS","FemaleSkTT"},
                                    {"MalePLS","MaleKLS","MalePSS","MaleSSS"}};
  char name[128];
  snprintf(name, sizeof name, "Bodies/%s%s.pcx", names[male][body], child ? "_sm" : "");
  Sheet *bs = sheet(name);
  Sheet *hs = sheet(male ? "Heads/sim_MALE_all_expressionsflat.pcx" : "Heads/sim_FEMALE_all_expressionsflat.pcx");
  if (!bs || !hs) { profile_key = 0; return 0; }
  // sheet() may evict an earlier high-resolution buffer, but never its source indices.
  // Match the original profile's framing, expressed relative to its head anchor.
  // Child bodies keep the same frame, so cropping does not enlarge them to adults.
  int w = PORTRAIT_PROFILE_W * 4, h = PORTRAIT_PROFILE_H * 4;
  uint32_t *px = calloc((size_t)w * h, 4);
  if (!px) { profile_key = 0; return 0; }
  if (child) piece(px,w,h,sheet("Bodies/Barrel.pcx"),0,0,60,120,-6,85,NULL);
  piece(px,w,h,bs,0,0,60,120,-3,85,pal);
  int replaced=replacement(px,w,h,hs,(head/2)*100,(head&1)*372+4+expr*124,
      90,120,-22,-11,male,head,expr,child,profile);
  if(!replaced)piece(px,w,h,hs,(head/2)*100,(head&1)*372+4+expr*124,90,120,-22,-11,NULL);
  int published = rpf_publish_art(art, px, w, h); free(px);
  if (!published) { profile_key = 0; return 0; }
  profile_key = key; profile_art = art;
  if (getenv("SIMGOLF_PORTRAITS_LOG")) fprintf(stderr,
      "portrait: profile %d head %d male %d expression %d body %d child %d palette %08x direct %dx%d hd %d head_hd %d body_hd %d replacement %d character %d\n",
      profile,head,male,expr,body,child,key,w,h,use_hd,hs->hi!=NULL,bs->hi!=NULL,replaced,roster_character(profile));
  return 1;
}
int portrait_preview(int profile, int art) {
  if (!enabled()) { preview_key = 0; return 0; }
  settings();
  const char *mode = mods_get("ui.portrait_preview");
  if (!portrait_dir[0] || (mode && !strcmp(mode,"original")) || !roster_profile_valid(profile)) {
    preview_key = 0; return 0;
  }
  uint32_t wrap = R32(0x5aa6c4);
  char source[128]; int ax,ay;
  if (!portrait_memory_readable(wrap,8) || !hd_art_portrait_frame(wrap,source,sizeof source,&ax,&ay)) {
    preview_key = 0; return 0;
  }
  uint16_t pal[256]; if (!working_palette(pal)) { preview_key = 0; return 0; }
  int child = !!(*(const uint8_t *)(uintptr_t)(roster_address(0x4d6088)+profile*0x230+0x2c)&8);
  uint32_t key = hash(source,strlen(source),hash(pal,sizeof pal,hash(&child,sizeof child,2166136261u)));
  if (key == preview_key) return 1;
  uint32_t *px=calloc(192u*192u,4); if (!px) return 0;
  int painted=0;
  for (int side=0;side<2;side++) {
    char name[128]; snprintf(name,sizeof name,"Preview/%s/v%d_f05.pcx",source,side*4);
    Sheet *s=sheet(name);
    if (!s || s->w>256 || s->h>256) { free(px); preview_key=0; return 0; }
    painted += s->painted;
    int w=s->w*4,h=s->h*4;
    uint32_t *one=calloc((size_t)w*h,4);
    if (!one) { free(px); return 0; }
    piece(one,w,h,s,0,0,s->w,s->h,0,0,pal);
    int scale=child?3:4,W=w*scale/4,H=h*scale/4;
    int dx=(side?33:11)*4-ax*scale,dy=44*4-ay*scale;
    for (int y=0;y<H;y++) for(int x=0;x<W;x++) {
      int ox=x+dx,oy=y+dy,ix=x*4/scale,iy=y*4/scale;
      if (ox<0 || oy<0 || ox>=192 || oy>=192 || s->idx[(iy/4)*s->w+ix/4]>=248) continue;
      uint32_t p=one[(size_t)iy*w+ix];
      px[oy*192+ox]=over(px[oy*192+ox],p);
    }
    free(one);
  }
  unsigned clear=0,solid=0;
  for(unsigned i=0;i<192u*192u;i++){if(!(px[i]>>24))clear++;else solid++;}
  if (!solid) {free(px);preview_key=0;return 0;}
  int published=rpf_publish_art(art,px,192,192);free(px);
  if(!published){preview_key=0;return 0;}
  preview_key=key;
  if(getenv("SIMGOLF_PORTRAITS_LOG"))fprintf(stderr,"portrait: preview profile %d source %s child %d direct 192x192 transparent 1 palette %08x painted %d clear %u solid %u\n",profile,source,child,key,painted,clear,solid);
  return 1;
}
int portrait_card(int slot, int art) {
  if (!enabled()) { card_key = 0; return 0; }
  settings();
  if (slot < 0 || slot >= 0x9c) return 0;
  int profile = *(const int16_t *)(uintptr_t)(0x57956e + slot * 0x100);
  int male,head,expr,body,child;
  if (!identity(profile,slot,&male,&head,&expr,&body,&child)) { card_key = 0; return 0; }
  const uint8_t *r=(const uint8_t *)(uintptr_t)(roster_address(0x4d6088)+profile*0x230);
  int values[] = {profile,male,head,expr,use_hd,child,r[0x27],r[0x28],roster_character(profile)};
  uint32_t key = hash(values,sizeof values,2166136261u);
  if (key == card_key && art == card_art) return 1;
  Sheet *s = sheet(male ? "Interface/golfballhalopage_male .pcx" : "Heads/golfballhalopage_female.pcx");
  if (!s) { card_key = 0; return 0; }
  uint32_t *px = calloc(560u*560u,4); if (!px) { card_key = 0; return 0; }
  int replaced=replacement(px,560,560,s,(head/2)*140,(head&1)*420+expr*140,
      140,140,0,0,male,head,expr,child,profile);
  if(!replaced)piece(px,560,560,s,(head/2)*140,(head&1)*420+expr*140,140,140,0,0,NULL);
  int x0=560,y0=560,x1=0,y1=0;
  for(int y=0;y<560;y++)for(int x=0;x<560;x++)if(px[y*560+x]>>24){
    if(x<x0)x0=x;if(y<y0)y0=y;if(x+1>x1)x1=x+1;if(y+1>y1)y1=y+1;
  }
  int w=x1-x0,h=y1-y0;
  if(w<=0||h<=0){free(px);card_key=0;return 0;}
  uint32_t *crop=malloc((size_t)w*h*4);
  if(!crop){free(px);card_key=0;return 0;}
  for(int y=0;y<h;y++)memcpy(crop+(size_t)y*w,px+(size_t)(y+y0)*560+x0,(size_t)w*4);
  gwf_publish_art(art,crop,w,h);free(crop);free(px);
  card_key=key;card_art=art;
  if (getenv("SIMGOLF_PORTRAITS_LOG")) fprintf(stderr,"portrait: card profile %d head %d expression %d direct %dx%d hd %d replacement %d character %d\n",profile,head,expr,w,h,use_hd,replaced,roster_character(profile));
  return 1;
}

