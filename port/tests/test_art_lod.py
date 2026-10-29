"""Distant-sprite image sizing and alpha-correct box filtering."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations
from test_publication_allocations import function

ROOT=Path(__file__).resolve().parents[2]
class ArtLod(unittest.TestCase):
    run_c=allocations.PublicationAllocations.run_c
    def test_allocation_failure_preserves_the_full_image(self):
        header=(ROOT/'port/src/port/hd_art_lod.h').read_text().replace('#pragma once','')
        self.run_c('art_lod_alloc',r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static void *fail_malloc(size_t n){(void)n;return NULL;}
#define malloc fail_malloc
'''+header+r'''
int main(int argc,char **argv){
 (void)argc;(void)argv;unsigned char source[4*4*4],before[sizeof source];
 memset(source,173,sizeof source);memcpy(before,source,sizeof source);
 int w=4,h=4;
 assert(!hd_art_reduce_rgba(source,4,4,&w,&h));
 assert(w==4&&h==4&&!memcmp(source,before,sizeof source));
 assert(!hd_art_reduce_rgba(NULL,4,4,&w,&h));
 assert(!hd_art_reduce_rgba(source,32769,4,&w,&h));
 return 0;
}
''',0)

    def test_production_keys_separate_resolution_without_changing_palette_identity(self):
        source=(ROOT/'port/src/port/hd_art.c').read_text()
        header=(ROOT/'port/src/port/hd_art_lod.h').read_text().replace('#pragma once','')
        variants=source[source.index('enum { V_RGBA,'):source.index('};',source.index('enum { V_RGBA,'))+2]
        entry=source[source.index('typedef struct {\n  int16_t file, view, frame;'):source.index('} Entry;')+len('} Entry;')]
        self.run_c('lod_keys','#include <assert.h>\n#include <string.h>\n'+header+variants+entry+r'''
typedef struct { uint32_t a,b; } Key;
typedef struct { int cat,w4,h4; } ArtFile;
enum { C_PEOPLE,C_BUILDINGS,C_TREES,C_TERRAIN,C_ANIMALS,C_UI,C_EFFECTS };static ArtFile g_files[2]={{C_TREES,256,512},{C_TREES,256,512}};
static long g_sprite_lod=1,g_alpha_edges=1;
static long InterlockedCompareExchange(long *p,long x,long c){(void)x;(void)c;return *p;}
static uint32_t hash_bytes(const void *p,size_t n,uint32_t h){(void)p;(void)n;return h;}
''' + function(source,'static Key make_key(')+function(source,'static Key entry_key(')+r'''
int main(int argc,char **argv){
 (void)argc;(void)argv;Entry e={0};e.view=2;e.frame=3;e.w=16;e.h=32;e.shadow_file=1;e.pal_hash=123456;e.shadow_hash=98765;
 for(int edge=0;edge<2;edge++)for(int variant=0;variant<4;variant++){
   e.variant=variant;g_alpha_edges=edge;g_sprite_lod=0;Key normal=entry_key(&e,0),normalShadow=entry_key(&e,1);
   g_sprite_lod=1;Key distant=entry_key(&e,0),distantShadow=entry_key(&e,1);
   int eligible=variant==V_RGBA||variant==V_RECOLOR;
   assert((normal.b!=distant.b)==eligible);assert((normalShadow.b!=distantShadow.b)==eligible);
   assert(normal.a==distant.a&&(normal.b&0xfffffff)==(distant.b&0xfffffff));
   e.w=64;e.h=128;assert(entry_key(&e,0).b==normal.b);e.w=16;e.h=32;
 }
 e.variant=V_RGBA;g_alpha_edges=1;
 for(int cat=0;cat<=C_EFFECTS;cat++){
   g_files[0].cat=cat;g_sprite_lod=0;Key normal=entry_key(&e,0);
   g_sprite_lod=1;Key distant=entry_key(&e,0);
   assert((normal.b!=distant.b)==(cat==C_PEOPLE||cat==C_BUILDINGS||cat==C_TREES||cat==C_ANIMALS));
 }
 return 0;
}
''',0)
    def test_dimensions_alpha_and_closeup_identity(self):
        header=(ROOT/'port/src/port/hd_art_lod.h').read_text().replace('#pragma once','')
        self.run_c('art_lod','#include <assert.h>\n'+header+r'''
int main(int argc,char **argv){
 (void)argc;(void)argv;
 assert(hd_art_sprite_lod(1,16,32,256,512)==4);
 assert(hd_art_sprite_lod(0,16,32,256,512)==1);
 assert(hd_art_sprite_lod(1,64,128,256,512)==1);
 assert(hd_art_sprite_lod(1,32,64,256,512)==1);
 assert(hd_art_sprite_lod(1,0,0,256,512)==1);
 unsigned char px[16]={255,0,0,255,0,0,255,0,0,255,0,0,0,0,0,0};
 int w=0,h=0;unsigned char *p=hd_art_reduce_rgba(px,2,2,&w,&h);
 assert(p&&w==1&&h==1);assert(p[0]==255&&p[1]==0&&p[2]==0&&p[3]==64);free(p);
 unsigned char odd[3*5*4];for(int i=0;i<15;i++){odd[4*i]=51;odd[4*i+1]=72;odd[4*i+2]=93;odd[4*i+3]=255;}
 p=hd_art_reduce_rgba(odd,3,5,&w,&h);assert(p&&w==1&&h==2);
 for(int i=0;i<w*h;i++)assert(p[4*i]==51&&p[4*i+1]==72&&p[4*i+2]==93&&p[4*i+3]==255);free(p);
 assert(!hd_art_reduce_rgba(px,0,2,&w,&h));return 0;
}
''',0)
