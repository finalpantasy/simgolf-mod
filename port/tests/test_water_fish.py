"""The optional fish view changes only underwater decoration draws."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations
from test_publication_allocations import function

ROOT=Path(__file__).resolve().parents[2]

class WaterFish(unittest.TestCase):
    run_c=allocations.PublicationAllocations.run_c
    def test_fallback_and_unrelated_sprite_parity(self):
        source=(ROOT/'port/src/port/golf/water_hooks.c').read_text()
        self.run_c('water_fish',r'''
#include <assert.h>
#include <stdint.h>
static int on,fish,last_id,last_frame,last_view;
static int water_hide_golf_ripples(void){return on;}
static int water_fish_enabled(void){return on&&fish;}
static void *FUN_0043d6f0(int id,int frame,int view){last_id=id;last_frame=frame;last_view=view;return (void *)(uintptr_t)(id+1);}
''' + function(source,'void *water_sprite(') + r'''
int main(int argc,char **argv){
 (void)argc;(void)argv;
 for(on=0;on<2;on++)for(fish=0;fish<2;fish++)for(int id=0x220;id<0x240;id++){
   void *p=water_sprite(id,7,3);
   int water=(id>=0x233&&id<=0x235)||id==0x237||id==0x238;
   int hidden=on&&(water||(fish&&id>=0x22e&&id<=0x232));
   assert((p==0)==hidden);
   if(!hidden)assert(last_id==id&&last_frame==7&&last_view==3);
 }
 return 0;
}
''',0)
