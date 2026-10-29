"""Decoded CPU images must not accumulate without a byte limit."""
from pathlib import Path
import unittest
import test_publication_allocations as allocations

ROOT = Path(__file__).resolve().parents[2]


class ArtDecodeQueue(unittest.TestCase):
    run_c = allocations.PublicationAllocations.run_c

    def test_byte_bound_and_oversized_image_progress(self):
        source = (ROOT/'port/src/port/hd_art_queue.h').read_text().replace('#pragma once','')
        self.run_c('art_queue', '#include <assert.h>\n#include <stdint.h>\n'+source+r'''
int main(int argc,char **argv) {
  (void)argc;(void)argv;size_t mb=1024u*1024u,budget=32*mb;
  assert(hd_art_queue_accept(0,40*mb,budget));
  assert(!hd_art_queue_accept(40*mb,1,budget));
  assert(!hd_art_queue_accept(1,40*mb,budget));
  assert(hd_art_queue_accept(31*mb,mb,budget));
  assert(!hd_art_queue_accept(31*mb,mb+1,budget));
  assert(!hd_art_queue_accept(SIZE_MAX-5,10,budget));
  assert(!hd_art_queue_accept(10,SIZE_MAX-5,budget));
  assert(hd_art_queue_accept(budget,0,budget));
  // Deterministically interleave four producers and a slow consumer.
  size_t queue[4096],held=0,peak=0;int head=0,tail=0,produced=0,consumed=0;
  for(int tick=0;consumed<4000;tick++) {
    for(int worker=0;worker<4 && produced<4000;worker++) {
      size_t bytes=(produced%97==0?40:1+produced%7)*mb;
      if(hd_art_queue_accept(held,bytes,budget)) {
        if(held)assert(held+bytes<=budget);
        queue[tail++]=bytes;held+=bytes;if(held>peak)peak=held;produced++;
      }
    }
    if(tick%5==0 && head<tail){held-=queue[head++];consumed++;}
    assert(held<=40*mb);assert(tick<25000);
  }
  assert(produced==4000 && held==0 && peak==40*mb);
  return 0;
}
''', 0)
