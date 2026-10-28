"""Failed table compaction must retain live artwork and pending requests."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations
from test_publication_allocations import function

ROOT = Path(__file__).resolve().parents[2]


class ArtRehashFailure(unittest.TestCase):
    run_c = allocations.PublicationAllocations.run_c
    def test_failed_allocation_preserves_table_and_retries(self):
        source = (ROOT / 'port/src/port/hd_art.c').read_text(encoding='utf-8')
        self.run_c('art_rehash', r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
typedef struct { uint32_t a, b; } Key;
typedef struct { Key key; void *tex, *emissive, *request; int w,h,used_slot; uint32_t used; } Tex;
#define TEX_N (1 << 16)
static Tex *g_tex;
static int g_tex_slots, g_recover_cursor = TEX_N, fail_alloc;
static uint32_t key_hash(Key k) { return k.a; }
static int key_eq(Key a, Key b) { return a.a == b.a && a.b == b.b; }
static void *allocate(size_t n,size_t size) { return fail_alloc ? NULL : calloc(n,size); }
#define calloc allocate
''' + function(source, 'static Tex *tex_find(') + function(source, 'static void tex_rehash(') + r'''
#undef calloc
int main(int argc,char **argv) {
  (void)argc; (void)argv;
  g_tex = calloc(TEX_N,sizeof *g_tex); assert(g_tex);
  Tex *old = g_tex;
  g_tex[7].used_slot = 1; g_tex[7].key.a = 7; g_tex[7].tex = (void *)1;
  g_tex[8].used_slot = 1; g_tex[8].key.a = 8; g_tex[8].request = (void *)2;
  g_tex_slots = TEX_N * 3 / 4; fail_alloc = 1;
  tex_rehash();
  assert(g_tex == old && g_tex_slots == TEX_N * 3 / 4);
  assert(g_tex[7].tex == (void *)1 && g_tex[8].request == (void *)2);
  fail_alloc = 0; tex_rehash();
  assert(g_tex != old && g_tex_slots == 2);
  assert(tex_find((Key){7,0},0)->tex == (void *)1);
  assert(tex_find((Key){8,0},0)->request == (void *)2);
  free(g_tex); return 0;
}
''', 0)
