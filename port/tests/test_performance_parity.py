"""Production queue and cache optimizations retain exact ordering and fallbacks."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations
from test_publication_allocations import function

ROOT = Path(__file__).resolve().parents[2]


class PerformanceParity(unittest.TestCase):
    run_c = allocations.PublicationAllocations.run_c

    def test_sprite_order_and_traversal(self):
        header = (ROOT / 'port/src/port/golf/sprq_fingers.h').read_text()
        impl = (ROOT / 'port/src/port/golf/sprq_fingers.c').read_text()
        test = (ROOT / 'port/tests/sprq_fingers.c').read_text()
        include = '#include "port/golf/sprq_fingers.h"'
        self.run_c('sprite_order', header + impl.replace(include, '') + test.replace(include, ''), 0)

    def test_bounds_setting_cache_tracks_live_changes(self):
        source = (ROOT / 'port/src/port/golf/re_bounds.c').read_text()
        self.run_c('bounds_setting', r'''
#include <assert.h>
#include <string.h>
static unsigned serial;
static int enabled=1, reads;
unsigned mods_serial(void) { return serial; }
int mods_get_int(const char *key,int fallback) {
  assert(!strcmp(key,"fixes.re_bounds") && fallback==1); reads++; return enabled;
}
''' + function(source, 'int fix_re_bounds_on(') + r'''
int main(int argc,char **argv) {
  (void)argc; (void)argv;
  for(int i=0;i<100000;i++) assert(fix_re_bounds_on()==1);
  assert(reads==1);
  enabled=0; serial++; assert(fix_re_bounds_on()==0); assert(reads==2);
  enabled=1; serial++; assert(fix_re_bounds_on()==1); assert(reads==3);
  return 0;
}
''', 0)

    def test_texture_victims_and_low_memory_fallback(self):
        source = (ROOT / 'port/src/port/hd_art.c').read_text()
        bodies = ''.join(function(source, s) for s in [
            'static void tex_drop(', 'static int tex_oldest_first(', 'static void tex_evict('])
        self.run_c('texture_victims', r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define TEX_N 2048
typedef struct { unsigned a,b; } Key;
typedef struct { Key key; void *tex,*emissive,*request; int w,h,used_slot; uint32_t used; } Tex;
typedef struct { int state; } KeyState;
static Tex storage[TEX_N], *g_tex=storage, **g_evict_order;
static KeyState states[TEX_N];
static size_t g_tex_bytes;
static int g_ntex,g_lock,fail_alloc,dropped[TEX_N*2],ndrop;
static uint32_t g_tick=100;
enum { S_NONE=0 };
static int texture_budget_mb(void) { return 1; }
static KeyState *ks_find(Key k,int make) { (void)make; return &states[k.a]; }
static void EnterCriticalSection(int *lock) { assert(lock==&g_lock); }
static void LeaveCriticalSection(int *lock) { assert(lock==&g_lock); }
static void SDL_DestroyTexture(void *tex) { dropped[ndrop++]=(int)(uintptr_t)tex; }
static void *scratch(size_t size) { return fail_alloc ? NULL : malloc(size); }
#define malloc scratch
''' + bodies + r'''
#undef malloc
int main(int argc,char **argv) {
  (void)argc; (void)argv;
  for(int mode=0;mode<5;mode++) for(int fail=0;fail<2;fail++) {
    size_t reference_bytes=0; int expected[TEX_N*2],ne=0,live[TEX_N];
    memset(storage,0,sizeof storage);ndrop=0;g_ntex=TEX_N;fail_alloc=fail;
    free(g_evict_order);g_evict_order=NULL;
    for(int i=0;i<TEX_N;i++) {
      Tex *t=&storage[i];t->key.a=i;t->tex=(void *)(uintptr_t)(i+1);
      t->emissive=mode==1 && i%3==0 ? (void *)(uintptr_t)(TEX_N+i+1):NULL;
      t->w=mode==2?8+i%64:32;t->h=32;t->used=mode==3?99:mode==4?0:(i*31)%100;
      t->request=malloc(1);assert(t->request);states[i].state=1;live[i]=1;
      reference_bytes+=(size_t)t->w*t->h*(t->emissive?8:4);
    }
    g_tex_bytes=reference_bytes;
    while(reference_bytes>(1u<<20)) {
      int oldest=-1;
      for(int i=0;i<TEX_N;i++) if(live[i] && (oldest<0 || storage[i].used<storage[oldest].used)) oldest=i;
      if(oldest<0 || storage[oldest].used+2>=g_tick) break;
      Tex *t=&storage[oldest];live[oldest]=0;expected[ne++]=oldest+1;
      if(t->emissive)expected[ne++]=TEX_N+oldest+1;
      reference_bytes-=(size_t)t->w*t->h*(t->emissive?8:4);
    }
    tex_evict();assert(g_tex_bytes==reference_bytes && ndrop==ne);
    for(int i=0;i<ne;i++)assert(dropped[i]==expected[i]);
    int count=0;
    for(int i=0;i<TEX_N;i++) {
      assert((storage[i].tex!=NULL)==live[i]);assert(states[i].state==live[i]);
      if(live[i]) { count++; assert(storage[i].request); free(storage[i].request); }
      else assert(!storage[i].request && !storage[i].emissive);
    }
    assert(count==g_ntex);
  }
  free(g_evict_order);return 0;
}
''', 0)
