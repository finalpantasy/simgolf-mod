// Straight-alpha textures need meaningful RGB in their transparent edge texels:
// bilinear filtering interpolates color before blending by interpolated alpha.
#ifndef SIMGOLF_SPRITE_ALPHA_H
#define SIMGOLF_SPRITE_ALPHA_H
#include <stdint.h>
#include <stddef.h>
static inline int sprite_alpha_extrude(uint8_t *rgba,int w,int h,int enabled){
  if(!enabled||!rgba||w<=0||h<=0||w>8192||h>8192||(size_t)w*h>16u*1024u*1024u)return 0;
  int changed=0;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    uint8_t *p=rgba+((size_t)y*w+x)*4;
    if(p[3])continue;
    unsigned sum[3]={0,0,0},weight=0;
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
      if(x+dx<0||x+dx>=w||y+dy<0||y+dy>=h)continue;
      const uint8_t *q=rgba+((size_t)(y+dy)*w+x+dx)*4;
      // Only original nonzero-alpha texels contribute. This cannot propagate
      // through pixels already filled by this scan, or alter the silhouette.
      weight+=q[3];for(int c=0;c<3;c++)sum[c]+=q[c]*q[3];
    }
    if(weight){for(int c=0;c<3;c++)p[c]=(uint8_t)((sum[c]+weight/2)/weight);changed++;}
  }
  return changed;
}
#endif
