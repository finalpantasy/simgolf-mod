#!/usr/bin/env python3
"""Compile real terrain handover code with allocation/SDL failure injection."""
import pathlib, subprocess, tempfile, os
root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'port/src/port/hd_terrain_native.c').read_text()
def span(start, end):
    return src[src.index(start):src.index(end)]
preamble = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
typedef int LONG;
typedef int CRITICAL_SECTION;
typedef int TerrainLightingModel;
typedef int SDL_Texture;
typedef int SDL_Renderer;
typedef uint8_t Uint8;
typedef struct {struct {float x,y;} position;struct {Uint8 r,g,b,a;} color;struct {float x,y;} tex_coord;} SDL_Vertex;
typedef int SDL_BlendMode;
#define SDL_PIXELFORMAT_ABGR8888 0
#define SDL_TEXTUREACCESS_STATIC 0
#define SDL_ScaleModeLinear 0
static int fail_malloc, fail_alloc, fail_at, alloc_calls, fail_create, fail_upload, fail_water, destroyed, locks, create_calls;
static void *test_realloc(void *p,size_t n) { if(fail_alloc || (++alloc_calls == fail_at)) return NULL; return realloc(p,n); }
static void *test_malloc(size_t n) {if(fail_malloc)return NULL;return malloc(n);}
#define malloc test_malloc
#define realloc test_realloc
static void EnterCriticalSection(CRITICAL_SECTION *p) {(void)p;locks++;}
static void LeaveCriticalSection(CRITICAL_SECTION *p) {(void)p;assert(locks>0);locks--;}
static SDL_Texture *SDL_CreateTexture(SDL_Renderer *r,int a,int b,int w,int h) {
 (void)r;(void)a;(void)b;(void)w;(void)h;create_calls++; return fail_create?NULL:malloc(sizeof(int));
}
static int SDL_UpdateTexture(SDL_Texture *t,void *a,const void *p,int pitch) {
 (void)t;(void)a;(void)p;(void)pitch;return fail_upload?-1:0;
}
static void SDL_DestroyTexture(SDL_Texture *t) {if(t){destroyed++;free(t);}}
static void SDL_SetTextureScaleMode(SDL_Texture *t,int m) {(void)t;(void)m;}
static int mods_get_int(const char *n,int v) {(void)n;return v;}
static int water_take_texture(uint32_t s,int w,int h,const uint8_t *p) {
 (void)s;(void)w;(void)h;(void)p;return !fail_water;
}
'''
preamble += r'''
typedef struct {const uint16_t *bits;int pitch,w,h;} Surf;
#define HD_FRAME_W 1
#define HD_FRAME_H 1
enum {kBlend=1,kTextured=2,kWater=8};
typedef struct {const char *src;const uint8_t *rgba;int w,h;uint8_t *up;int uw,uh,path_style;} HdTerrainJob;
static uint16_t screen_pixel=1;
static int32_t batches[5]={0,1,0,22,2};
static float vertices[16]={17},world[6]={18},normals[6]={19};
static uint8_t pixels[4]={20};
static int invalidated,fast;
static int terrain_cache(Surf *s){*s=(Surf){&screen_pixel,1,1,1};return 1;}
static uint8_t shade(uint16_t s,uint16_t t){return s==t?255:0;}
static void terrain_hd_scroll_padding(int on){(void)on;}
static int terrain_hd_build(void){return 1;}
static int terrain_hd_batch_count(void){return 1;}
static const int32_t *terrain_hd_batches(void){return batches;}
static void terrain_hd_target(int32_t *t){t[0]=t[1]=1;}
static int port_course_textures_fast(void){return fast;}
static void port_course_textures_done(void){}
static const float *terrain_hd_vertices(void){return vertices;}
static const float *terrain_hd_world(void){return world;}
static const float *terrain_hd_normals(void){return normals;}
static void terrain_hd_light_model(TerrainLightingModel *p){*p=0;}
static void terrain_hd_view(float *p){memset(p,0,6*sizeof(float));}
static float terrain_hd_height_pixels(void){return 1;}
static int course_relief_material(const char *s){(void)s;return 0;}
static const char *terrain_hd_texture_src(int i){(void)i;return "fixture";}
static int terrain_grid_size(int *w,int *h){(void)w;(void)h;return 0;}
static int terrain_water_tiles(uint8_t *c,float *h,int n){(void)c;(void)h;return n;}
static const uint8_t *terrain_hd_texture(int i,int32_t *s){(void)i;s[0]=s[1]=1;return pixels;}
static int terrain_hd_texture_path_style(int i){(void)i;return 0;}
static uint8_t *hd_art_terrain_style(const char *src,const uint8_t *rgba,int w,int h,int *uw,int *uh,int style){
(void)src;(void)rgba;(void)w;(void)h;(void)uw;(void)uh;(void)style;return NULL;
}
static void hd_art_terrain_jobs(HdTerrainJob *jobs,int n){(void)jobs;(void)n;}
static void terrain_hd_invalidate(void){invalidated++;}
'''
preamble += r'''
#define SDL_PIXELFORMAT_ARGB8888 1
#define SDL_TEXTUREACCESS_TARGET 1
#define SDL_BLENDMODE_BLEND 1
#define SDL_BLENDMODE_NONE 0
#define SDL_BLENDFACTOR_SRC_ALPHA 1
#define SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA 1
#define SDL_BLENDOPERATION_ADD 1
#define SDL_BLENDFACTOR_ZERO 0
#define SDL_BLENDFACTOR_ONE 1
static void SDL_SetTextureBlendMode(SDL_Texture *t,int m){(void)t;(void)m;}
static int SDL_ComposeCustomBlendMode(int a,int b,int c,int d,int e,int f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 1;}
static unsigned terrain_lighting_revision(void){return 0;}
static int terrain_lighting_rebuild(const float *v,const float *w,const float *n,int nv,const int32_t *b,int nb,const TerrainLightingModel *l,LONG serial){(void)v;(void)w;(void)n;(void)nv;(void)b;(void)nb;(void)l;(void)serial;return 0;}
static const float *terrain_lighting_vertices(void){return vertices;}
static const int32_t *terrain_lighting_batches(void){return batches;}
static void SDL_SetRenderTarget(SDL_Renderer *r,SDL_Texture *t){(void)r;(void)t;}
static void SDL_SetRenderDrawColor(SDL_Renderer *r,int a,int b,int c,int d){(void)r;(void)a;(void)b;(void)c;(void)d;}
static void SDL_SetRenderDrawBlendMode(SDL_Renderer *r,int m){(void)r;(void)m;}
static void SDL_RenderClear(SDL_Renderer *r){(void)r;}
static void SDL_RenderGeometry(SDL_Renderer *r,SDL_Texture *t,SDL_Vertex *v,int n,const int *indices,int count){(void)r;(void)t;(void)v;(void)n;(void)indices;(void)count;}
'''
helpers = span('enum { kMaxPendingTextures', '// Decode the first')
types = span('typedef struct { uint32_t serial; int w, h;', 'void hdt_init(')
capture = span('// Decode the first', '// ------------------------------------------------------------------ render thread')
render = span('typedef struct { uint32_t serial; SDL_Texture *tex;', 'SDL_Texture *hdt_render(')
main = r'''
static void cleanup(void) {
 for(int i=0;i<g_pending.ntex;i++)free(g_pending.tex[i].rgba);
 free(g_pending.tex);free(g_pending.verts);free(g_pending.world);free(g_pending.normals);
 free(g_pending.materials);free(g_pending.batches);free(g_pending.wcode);free(g_pending.wheight);
 memset(&g_pending,0,sizeof g_pending);
 for(int i=0;i<R.ntex;i++)SDL_DestroyTexture(R.tex[i].tex);
 free(R.tex);free(R.verts);free(R.world);free(R.normals);free(R.materials);free(R.batches);free(R.wcode);free(R.wheight);
 SDL_DestroyTexture(R.targets[0].texture);SDL_DestroyTexture(R.targets[1].texture);free(R.sv);
 memset(&R,0,sizeof R);free(g_sent);g_sent=NULL;g_nsent=g_cap_sent=0;
 fail_malloc=fail_alloc=fail_at=alloc_calls=fail_create=fail_upload=fail_water=create_calls=0;
 assert(locks==0);
}
static void enqueue(uint32_t serial,int water) {
 NewTex n={serial,1,1,malloc(4),water,0,0};
 assert(queue_texture(&g_pending,&n));assert(!n.rgba);
}
int main(void) {
 SDL_Renderer r=0;g_on=1;
 size_t bytes=0;int cap=77;
 assert(!texture_bytes(0,1,&bytes));assert(!texture_bytes(-1,1,&bytes));
 assert(!texture_bytes(INT_MAX,INT_MAX,&bytes));assert(!texture_bytes(16384,16384,&bytes));
 assert(texture_bytes(1,1,&bytes) && bytes==4);
 assert(!grow_array(NULL,0,SIZE_MAX,4,&cap) && cap==77);
 assert(!grow_array(NULL,0,1,SIZE_MAX,&cap) && cap==77);
 g_pending.serial=1;g_pending.frame=1;enqueue(7,0);
 fail_create=1;take(&r,1);
 assert(g_pending.ntex==1 && "failed GPU creation must retain pending texture");assert(g_nsent==0);assert(locks==0);
 fail_create=0;fail_upload=1;take(&r,1);
 assert(g_pending.ntex==1 && "failed GPU upload must retain pending texture");assert(g_nsent==0);assert(locks==0);
 fail_upload=0;take(&r,1);
 assert(g_pending.ntex==0 && find_tex(7));assert(g_nsent==1 && g_sent[0]==7);assert(locks==0);
 // Re-send: old texture is retained through SDL and downstream allocation failure.
 SDL_Texture *old=find_tex(7);g_nsent=0;enqueue(7,1);
 fail_upload=1;take(&r,1);assert(find_tex(7)==old && g_pending.ntex==1);
 fail_upload=0;fail_water=1;take(&r,1);assert(find_tex(7)!=old && g_pending.ntex==1 && !g_nsent);
 int before_retry=create_calls;
 fail_water=0;take(&r,1);assert(find_tex(7)!=old && !g_pending.ntex && g_nsent==1 && create_calls==before_retry);
 assert(R.targets[0].serial==-1 && R.targets[1].serial==-1);cleanup();
 // Both cache and acknowledgement reserve failures retain pending ownership.
 for(int fail=1;fail<=2;fail++) {
  g_pending.serial=1;g_pending.frame=1;enqueue(9,0);alloc_calls=0;fail_at=fail;
  take(&r,1);assert(g_pending.ntex==1 && !g_nsent && !R.ntex && locks==0);
  fail_at=0;take(&r,1);assert(g_pending.ntex==0 && find_tex(9));cleanup();
 }
 // Queue grow failure neither loses its pointer nor publishes an entry.
 NewTex n={42,1,1,malloc(4),0,0,0};fail_alloc=1;
 assert(!queue_texture(&g_pending,&n));assert(n.rgba && !g_pending.tex && !g_pending.ntex && !g_pending.cap_tex);
 fail_alloc=0;assert(queue_texture(&g_pending,&n));enqueue(42,0);assert(g_pending.ntex==1);cleanup();
 // Bounds, frame gating and fair bounded retries.
 g_pending.serial=1;g_pending.frame=2;
 for(unsigned i=0;i<10;i++)enqueue(100+i,0);
 take(&r,1);assert(create_calls==0 && g_pending.ntex==10 && locks==0);
 fail_create=1;take(&r,2);assert(create_calls==10 && g_pending.ntex==10);
 create_calls=0;take(&r,2);assert(create_calls==kUploadsPerTake && g_pending.ntex==10);
 assert(g_pending.tex[0].serial==108);
 fail_create=0;take(&r,2);take(&r,2);assert(g_pending.ntex==0 && R.ntex==10);cleanup();
 g_pending.ntex=kMaxPendingTextures;n=(NewTex){42,1,1,malloc(4),0,0,0};
 // Avoid a synthetic invalid array: test count bound with a real full queue.
 g_pending.ntex=0;
 for(unsigned i=0;i<kMaxPendingTextures;i++)enqueue(1000+i,0);
 assert(!queue_texture(&g_pending,&n));free(n.rgba);cleanup();
 // Execute the real game-thread capture: every core reserve failure leaves
 // the previous scene unpublished and schedules a rebuild without holding lock.
 for(int f=1;f<=5;f++) {
  uint8_t mask=0;invalidated=0;alloc_calls=0;fail_at=f;
  assert(hdt_capture(&screen_pixel,&mask,3)==1);
  assert(invalidated==1 && g_pending.serial==0 && g_pending.nverts==0 && locks==0);
  fail_at=0;assert(hdt_capture(&screen_pixel,&mask,3)==1);
  assert(g_pending.serial==1 && g_pending.nverts==1 && g_pending.verts[0]==17 && g_pending.ntex==1);
  take(&r,3);assert(find_tex(22));cleanup();
 }
 for(int f=0;f<2;f++) {
  uint8_t mask;fast=f;hdt_capture(&screen_pixel,&mask,4);
  assert(g_pending.ntex==1 && !g_nsent);take(&r,4);assert(find_tex(22));cleanup();
 }
 // Pixel malloc failure is retried by the game on both decode paths.
 for(int f=0;f<2;f++) {
  uint8_t mask;fast=f;invalidated=0;fail_malloc=1;
  hdt_capture(&screen_pixel,&mask,5);
  assert(!g_pending.ntex && !g_nsent && invalidated==1 && locks==0);
  fail_malloc=0;hdt_capture(&screen_pixel,&mask,6);assert(g_pending.ntex==1);cleanup();
 }
 // A later reserve failure must not mix new vertex contents with old counts.
 for(int f=1;f<=3;f++) {
  uint8_t mask;fast=0;batches[1]=1;vertices[0]=17;
  hdt_capture(&screen_pixel,&mask,7);take(&r,7);int oldserial=R.serial;
  batches[1]=2;vertices[0]=99;hdt_capture(&screen_pixel,&mask,8);
  alloc_calls=0;fail_at=f;take(&r,8);
  assert(R.serial==oldserial && R.nverts==1 && R.verts[0]==17 && locks==0);
  fail_at=0;take(&r,8);assert(R.nverts==2 && R.verts[0]==99);cleanup();
 }
 // Full downstream backpressure across course changes never monopolizes
 // the current scene's queue or hides successfully uploaded base terrain.
 fail_water=1;fast=0;batches[1]=1;batches[4]=kTextured|kWater;
 for(int i=0;i<300;i++) {
  uint8_t mask;batches[3]=5000+i;hdt_capture(&screen_pixel,&mask,10+i);take(&r,10+i);
  assert(find_tex(5000+i) && g_pending.ntex==1 && !g_nsent && locks==0);
 }
 hdt_resend();assert(!g_pending.ntex && !g_nsent && locks==0);cleanup();
 // A completely full stale terrain queue is pruned before current admission.
 for(unsigned i=0;i<kMaxPendingTextures;i++)enqueue(10000+i,0);
 {uint8_t mask;batches[3]=22;batches[4]=kTextured;
  hdt_capture(&screen_pixel,&mask,400);assert(g_pending.ntex==1 && g_pending.tex[0].serial==22);}
 cleanup();
 {uint8_t mask;batches[1]=1;batches[3]=22;batches[4]=kTextured;
  hdt_capture(&screen_pixel,&mask,500);
  SDL_Texture *old_target=render_at(&r,2,2,500,0,0,0);assert(old_target);
  fail_create=1;assert(!render_at(&r,3,3,500,0,0,0));
  assert(R.targets[0].texture==old_target && R.targets[0].w==2);
  fail_create=0;assert(render_at(&r,3,3,500,0,0,0));
 }
 cleanup();
 puts("terrain recovery: PASS (GPU failures, ownership, allocation, bounds, fair retry, lock release)");return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c=pathlib.Path(tmp)/'test.c';c.write_text(preamble+types+span('void hdt_resend(', '// Bounded CPU')+helpers+capture+render+main)
    exe=pathlib.Path(tmp)/'test'
    subprocess.run([os.environ.get('CC','gcc'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-g','-fsanitize='+os.environ.get('SANITIZERS','undefined'),'-o',str(exe),str(c)],check=True)
    subprocess.run([str(exe)],check=True)
