#include "port/rainbow.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static double clamp(double x,double lo,double hi){return isfinite(x)?fmax(lo,fmin(hi,x)):lo;}
static double smooth(double x){x=clamp(x,0,1);return x*x*(3-2*x);}
static uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;return x^(x>>16);}
void rainbow_update(RainbowState *s,double rain,double cloud,double day,double dt,unsigned seed){
 if(!s)return;dt=clamp(dt,0,.25);rain=clamp(rain,0,1);cloud=clamp(cloud,0,1);day=clamp(day,0,1);
 if(rain>.25){s->rain_charge=fmin(12,s->rain_charge+dt);if(s->rain_charge>=8)s->armed=1;s->clearing=0;s->age=0;}
 if(s->armed&&rain<.12&&cloud<.62){s->armed=0;s->rain_charge=0;s->episode++;s->clearing=hash(seed+s->episode*7919u)%3==0;s->age=0;}
 if(s->clearing){s->age+=dt;if(s->age>65)s->clearing=0;}
 double envelope=smooth(s->age/7)*smooth((65-s->age)/15);
 s->intensity=s->clearing?envelope*(1-smooth(rain/.2))*(1-smooth((cloud-.4)/.35))*smooth((day-.55)/.4):0;
}
void rainbow_destroy(RainbowDraw *d){if(!d)return;if(d->texture)SDL_DestroyTexture(d->texture);memset(d,0,sizeof *d);}
static int make(RainbowDraw *d,SDL_Renderer *r,SDL_Rect view,double sun){
 enum{W=512,H=256};uint8_t *pixels=calloc(W*H,4);if(!pixels)return 0;
 double radius=view.w*.25,cx=view.w*(.94+sun*.03),cy=view.h*.58,band=radius*.032;
 for(int y=0;y<H;y++)for(int x=0;x<W;x++){
  double px=(x+.5)*view.w/W,py=(y+.5)*view.h/H,dx=px-cx,dy=py-cy;
  double q=(hypot(dx,dy)-radius)/band+.5;
  if(q<-.25||q>1.25||dy>-.26*radius)continue;
  double edge=smooth((q+.25)/.3)*smooth((1.25-q)/.3),ends=smooth((-dy/radius-.26)/.25);
  // Overlapping broad spectral lobes avoid seven hard painted stripes.
  double red=exp(-pow((q-.83)/.30,2))+.30*exp(-pow((q-.05)/.18,2));
  double green=exp(-pow((q-.53)/.25,2));double blue=exp(-pow((q-.19)/.24,2));
  size_t p=((size_t)y*W+x)*4;pixels[p]=(uint8_t)lround(clamp(.60+.40*red,0,1)*255);
  pixels[p+1]=(uint8_t)lround(clamp(.60+.40*green,0,1)*255);pixels[p+2]=(uint8_t)lround(clamp(.60+.40*blue,0,1)*255);
  pixels[p+3]=(uint8_t)lround(255*edge*ends);
 }
 if(!d->texture)d->texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,W,H);
 int ok=d->texture&&SDL_UpdateTexture(d->texture,NULL,pixels,W*4)==0;free(pixels);if(!ok)return 0;
 SDL_SetTextureBlendMode(d->texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(d->texture,SDL_ScaleModeLinear);
 d->renderer=r;d->width=view.w;d->height=view.h;d->sun=sun;return 1;
}
int rainbow_draw(RainbowDraw *d,SDL_Renderer *r,SDL_Rect view,const unsigned char *mask,int w,int h,
 double ox,double oy,double k,double strength,double sun){
 if(!d||!r||!mask||view.w<1||view.h<1||w<1||h<1||w>4096||h>4096||!isfinite(k)||k<=0||!isfinite(ox)||!isfinite(oy))return 0;
 strength=clamp(strength,0,.24);if(strength<.001)return 0;sun=round(clamp(sun,-1,1)*8)/8;
 if(d->renderer&&d->renderer!=r)rainbow_destroy(d);
 if((!d->texture||d->width!=view.w||d->height!=view.h||d->sun!=sun)&&!make(d,r,view,sun))return 0;
 SDL_Rect old,clip;SDL_bool clipped=SDL_RenderIsClipEnabled(r);SDL_RenderGetClipRect(r,&old);clip=view;
 if(clipped&&!SDL_IntersectRect(&old,&view,&clip))return 0;SDL_RenderSetClipRect(r,&clip);
 SDL_Vertex v[3072];int n=0,spans=0;const int order[6]={0,1,2,2,1,3};
 // The revised arc exists only in distant space beyond the course. Terrain,
 // trees, buildings and water occlude it completely. It adds no sky gradient.
 // This optional arc is the only exception to the otherwise empty background.
 for(int y=0;y<h;y++){
 for(int x=0;x<w;){
  int m=mask[(size_t)y*w+x];if(m!=255){x++;continue;}
  int left=x++;while(x<w&&mask[(size_t)y*w+x]==255)x++;
  for(int j=0;j<6;j++){int q=order[j];double px=ox+(q&1?x:left)*k,py=oy+(y+q/2)*k;
   v[n++]=(SDL_Vertex){{(float)px,(float)py},{255,255,255,(Uint8)lround(strength*255)},
    {(float)((px-view.x)/view.w),(float)((py-view.y)/view.h)}};
  }spans++;if(n==3072){SDL_RenderGeometry(r,d->texture,v,n,NULL,0);n=0;}
 }
 }
 if(n)SDL_RenderGeometry(r,d->texture,v,n,NULL,0);
 SDL_RenderSetClipRect(r,clipped?&old:NULL);return spans;
}
