"""Every live UI context must survive replacement of its SDL renderer."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations
from test_publication_allocations import function

ROOT = Path(__file__).resolve().parents[2]


class RendererRecovery(unittest.TestCase):
    run_c = allocations.PublicationAllocations.run_c

    def test_all_contexts_rebind_without_losing_cpu_state(self):
        source = (ROOT/'port/src/port/ui/ui_kit.c').read_text()
        self.run_c('renderer_recovery', r'''
#include <assert.h>
#include <string.h>
typedef struct { int id; } SDL_Renderer;
typedef struct { SDL_Renderer *r; int gpu, cpu; } Cache;
typedef struct UiCtx {
  struct UiCtx *next_context;
  SDL_Renderer *r;
  Cache d,t;
  struct { void *tex; } icons[1];
  int nicons, selected, input_queue;
} UiCtx;
static UiCtx *g_contexts;
static int destroyed;
static void SDL_DestroyTexture(void *p) { assert(p);destroyed++; }
static void uid_free(Cache *c) { c->gpu=0; }
static void uid_init(Cache *c,SDL_Renderer *r) { c->r=r; }
static void uit_device_reset(Cache *c) { c->gpu=0; }
''' + function(source,'void ui_device_reset(') + function(source,'void ui_renderer_rebind(') + r'''
int main(int argc,char **argv) {
  (void)argc;(void)argv;
  SDL_Renderer old={1},next={2},other={3};UiCtx c[24]={0};
  for(int i=0;i<24;i++) {
    c[i].next_context=i<23?c+i+1:0;c[i].r=i==7?&other:&old;
    c[i].d.r=c[i].t.r=c[i].r;c[i].d.gpu=c[i].t.gpu=1;
    c[i].t.cpu=9;c[i].selected=i;c[i].input_queue=17;
    c[i].icons[0].tex=(void*)1;c[i].nicons=1;
  }
  g_contexts=c;ui_device_reset(&old);ui_renderer_rebind(&old,0);
  assert(destroyed==23);
  for(int i=0;i<24;i++) {
    assert(c[i].r==(i==7?&other:0));assert(c[i].d.r==c[i].r && c[i].t.r==c[i].r);
    assert(c[i].t.cpu==9 && c[i].selected==i && c[i].input_queue==17);
    assert(c[i].nicons==(i==7));assert(c[i].t.gpu==(i==7));
  }
  ui_renderer_rebind(0,&next);
  for(int i=0;i<24;i++)assert(c[i].r==(i==7?&other:&next) && c[i].d.r==c[i].r && c[i].t.r==c[i].r);
  // Repeated cleanup after a failed allocation is safe.
  ui_device_reset(&next);assert(destroyed==23);
  return 0;
}
''', 0)
