// Runtime artwork from the player's theme. No game pixels stored in source.
#ifndef SIMGOLF_JOINED_POT_H
#define SIMGOLF_JOINED_POT_H
#include <math.h>
#include <stdint.h>
namespace joined_pot {
// Clockwise in texture space, north first. Diagonals preserve inside corners.
static const int dx[8]={0,1,1,1,0,-1,-1,-1};
static const int dy[8]={1,1,0,-1,-1,-1,0,1};
inline unsigned rotate(unsigned mask,int turns) {
  unsigned n=(unsigned)(turns&3)*2;
  return ((mask<<n)|(mask>>((8-n)&7)))&255;
}
inline bool connected(unsigned mask) { return (mask&0x55)!=0; }
// A four-tile period in screen texture space. Neighboring tile origins
// advance the same continuous contour, including after camera rotation.
inline unsigned phase(int x,int y,int rotation) {
  switch(rotation&3) {
    case 0:return (unsigned)(x-y-1)&3;
    case 1:return (unsigned)(x+y)&3;
    case 2:return (unsigned)(y-x-1)&3;
    default:return (unsigned)(-x-y-2)&3;
  }
}
// Distance from a point in this tile to the exterior of the tile union.
// Absent diagonal cells produce concave corners and preserve grassy holes.
inline float distance(unsigned mask,float x,float y,float *nx=nullptr,float *ny=nullptr) {
  float best=4,bx=0,by=1;
  for(int i=0;i<8;i++) if(!(mask&(1u<<i))) {
    float loX=dx[i],hiX=loX+1,loY=dy[i],hiY=loY+1;
    float vx=x<loX?x-loX:x>hiX?x-hiX:0;
    float vy=y<loY?y-loY:y>hiY?y-hiY:0;
    float d=sqrtf(vx*vx+vy*vy);
    if(d<best) { best=d;bx=d>0?-vx/d:0;by=d>0?-vy/d:1; }
  }
  if(nx)*nx=bx;
  if(ny)*ny=by;
  return best;
}
inline float contour(unsigned mask,float u,float v,unsigned phase,float *nx,float *ny) {
  float d=distance(mask,u,v,nx,ny);
  // Broad rounded caps and shoulders, rather than the tile's square corner.
  // Broad two-tile ends can approach an oval. Keep narrow arms wide enough
  // to retain a continuous floor rather than pinching them into points.
  bool narrow=(!(mask&1u)&&!(mask&16u)) || (!(mask&4u)&&!(mask&64u));
  const float radius=narrow?.49f:.90f;
  for(int corner=0;corner<4;corner++) {
    int a=corner*2,b=(a+2)&7;
    bool right=corner==0||corner==1,top=corner==0||corner==3;
    if(!(mask&(1u<<a)) && !(mask&(1u<<b))) {
      float cx=right?1-radius:radius;
      float cy=top?1-radius:radius;
      if((right?u>cx:u<cx)&&(top?v>cy:v<cy))
        d=fminf(d,radius-sqrtf((u-cx)*(u-cx)+(v-cy)*(v-cy)));
    }
  }
  // Coherent low-frequency undulations break up long ruled edges. The
  // whole contour, including the recessed wall, follows this same field.
  const float tau=6.28318530718f;
  float z=(u+v+phase)*.25f;
  return d+.085f*sinf(tau*z)+.035f*sinf(tau*(2*z+.13f))
          +.020f*sinf(tau*(u-v)*2+.7f);
}
inline void pixels(unsigned mask,const uint8_t *source,int sw,int sh,uint8_t *out,int size,unsigned phase=0,
                   const uint8_t *ground=nullptr,int gw=0,int gh=0,
                   const uint8_t *floor=nullptr,int fw=0,int fh=0) {
  for(int y=0;y<size;y++) for(int x=0;x<size;x++) {
    float nx,ny,d=contour(mask,(x+.5f)/size,(y+.5f)/size,phase,&nx,&ny);
    const uint8_t *p;
    if(d<.07f && ground && gw>0 && gh>0) {
      p=ground+((y*gh/size)*gw+x*gw/size)*4;
    } else if(d<.30f) {
      // Retain the original painted, pixel-sized rim rather than generating
      // smooth lighting or randomized grain. Sample its outer ring by normal.
      float radius=.49f-fmaxf(0,d-.07f)*1.1f;
      int sx=(int)((.5f+nx*radius)*sw),sy=(int)((.5f+ny*radius)*sh);
      sx=sx<0?0:sx>=sw?sw-1:sx;sy=sy<0?0:sy>=sh?sh-1:sy;
      p=source+(sy*sw+sx)*4;
    } else if(floor && fw>0 && fh>0) {
      // Original pot-bunker material at its original texel density.
      p=floor+((y*fh/size)*fw+x*fw/size)*4;
    } else {
      p=source+((sh/2+y%(sh/10))*sw+sw/2+x%(sw/10))*4;
    }
    uint8_t *q=out+(y*size+x)*4;
    for(int c=0;c<3;c++)q[c]=p[c];
    q[3]=255;
  }
}
}
#endif
