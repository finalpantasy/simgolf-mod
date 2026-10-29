"""New-sandbox transforms preserve default terrain and independent options."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations

ROOT = Path(__file__).resolve().parents[2]

class SandboxLandscape(unittest.TestCase):
    run_c = allocations.PublicationAllocations.run_c

    def test_independent_options_and_corner_extents(self):
        header = (ROOT / 'port/src/port/sandbox_landscape.h').read_text().replace('#pragma once', '')
        self.run_c('sandbox_landscape', '#include <assert.h>\n#include <string.h>\n' + header + r'''
int main(int argc,char **argv) {
 (void)argc;(void)argv;
 unsigned char terrain[25602],base[25602],decor[25602],building[25602],height[25923];
 unsigned short flags[25602];
 for(int w=50;w<=160;w+=55)for(int mode=0;mode<4;mode++) {
   int n=w*w,c=(w+1)*(w+1);
   memset(terrain,17,sizeof terrain);memset(base,20,sizeof base);memset(decor,13,sizeof decor);
   memset(building,9,sizeof building);memset(height,7,sizeof height);
   for(int i=0;i<25602;i++)flags[i]=0x8765;
   assert(sandbox_landscape_apply(w,w,mode&1,mode&2,terrain,base,decor,building,flags,height,6));
   for(int i=0;i<n;i++) {
     assert(terrain[i]==((mode&1)?4:17));assert(base[i]==((mode&1)?4:20));
     assert(decor[i]==((mode&1)?6:13));assert(building[i]==((mode&1)?0:9));
     assert(flags[i]==((mode&1)?0:0x8765));
   }
   for(int i=0;i<c;i++)assert(height[i]==((mode&2)?3:7));
   assert(terrain[n]==17&&base[n]==20&&decor[n]==13&&building[n]==9&&flags[n]==0x8765&&height[c]==7);
 }
 assert(!sandbox_landscape_apply(161,50,1,1,terrain,base,decor,building,flags,height,6));
 assert(!sandbox_landscape_apply(0,50,1,1,terrain,base,decor,building,flags,height,6));
 return 0;
}
''', 0)
