#pragma once
#include <stdint.h>
#include <stdlib.h>

// Far zoom has sixteen source pixels per canvas pixel. A quarter-size image
// still supplies four samples per canvas pixel; the nearer zoom levels retain
// their full image. Apply only to upscaled world sprites, never pixel art/UI.
static int hd_art_sprite_lod(int enabled,float draw_w,float draw_h,int source_w,int source_h) {
  return enabled&&draw_w>0&&draw_h>0&&draw_w*12<=source_w&&draw_h*12<=source_h?4:1;
}

// Straight-alpha box filtering weights color by coverage. Transparent source
// RGB must not introduce a black/colored fringe around thin distant branches.
static uint8_t *hd_art_reduce_rgba(const uint8_t *src,int w,int h,int *ow,int *oh) {
  if(!src||w<1||h<1||w>32768||h>32768)return NULL;
  int nw=(w+3)/4,nh=(h+3)/4;
  uint8_t *out=malloc((size_t)nw*nh*4);
  if(!out)return NULL;
  for(int y=0;y<nh;y++)for(int x=0;x<nw;x++){
    unsigned a=0,r=0,g=0,b=0,n=0;
    for(int dy=0;dy<4&&y*4+dy<h;dy++)for(int dx=0;dx<4&&x*4+dx<w;dx++){
      const uint8_t *p=src+((size_t)(y*4+dy)*w+x*4+dx)*4;
      a+=p[3];r+=p[0]*p[3];g+=p[1]*p[3];b+=p[2]*p[3];n++;
    }
    uint8_t *q=out+((size_t)y*nw+x)*4;
    q[0]=a?(uint8_t)((r+a/2)/a):0;q[1]=a?(uint8_t)((g+a/2)/a):0;
    q[2]=a?(uint8_t)((b+a/2)/a):0;q[3]=(uint8_t)((a+n/2)/n);
  }
  *ow=nw;*oh=nh;return out;
}
