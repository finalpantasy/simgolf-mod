// Generation-only options. Existing saves never pass through this transform.
#pragma once
#include <stdint.h>
#include <string.h>

static int sandbox_landscape_apply(int w,int h,int blank,int flat,
    uint8_t *terrain,uint8_t *base,uint8_t *decor,uint8_t *building,
    uint16_t *flags,uint8_t *height,uint8_t rough_decor) {
  if(w<1||h<1||w>160||h>160)return 0;
  size_t n=(size_t)w*h;
  if(blank){
    memset(terrain,4,n);memset(base,4,n);memset(decor,rough_decor,n);
    memset(building,0,n);memset(flags,0,n*sizeof *flags);
  }
  // The game's zero world elevation is corner byte 3, not byte 0.
  if(flat)memset(height,3,(size_t)(w+1)*(h+1));
  return 1;
}
