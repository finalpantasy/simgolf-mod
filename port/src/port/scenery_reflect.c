#include "port/scenery_reflect.h"
#include <math.h>
static double clamp(double x,double lo,double hi){return isfinite(x)?fmax(lo,fmin(hi,x)):lo;}
int scenery_reflect_draw(SDL_Renderer *r,SDL_Texture *tx,SDL_FRect d,double anchor,
 int flip,const unsigned char *mask,int w,int h,double ox,double oy,double k,
 double seconds,double strength,double ripple){
 if(!r||!tx||!mask||w<1||h<1||w>4096||h>4096||!isfinite(k)||k<=0||
    !isfinite(ox)||!isfinite(oy)||!isfinite(d.x)||!isfinite(d.y)||!isfinite(d.w)||!isfinite(d.h)||
    d.w<=0||d.h<=0||d.w>4096||d.h>4096||!isfinite(anchor)||!isfinite(seconds))return -1;
 strength=clamp(strength,0,.7);ripple=clamp(ripple,0,3);if(!strength)return 0;
 double extent=fmin(220,fmax(0,anchor-d.y));if(extent<1)return 0;
 int y0=(int)clamp(floor(anchor),0,h),y1=(int)clamp(ceil(anchor+extent),0,h);
 Uint8 cr,cg,cb,ca;SDL_BlendMode blend;SDL_GetTextureColorMod(tx,&cr,&cg,&cb);
 SDL_GetTextureAlphaMod(tx,&ca);SDL_GetTextureBlendMode(tx,&blend);
 SDL_SetTextureColorMod(tx,255,255,255);SDL_SetTextureAlphaMod(tx,255);SDL_SetTextureBlendMode(tx,SDL_BLENDMODE_BLEND);
 SDL_Vertex v[3072];int n=0,spans=0,ok=1;const int order[6]={0,1,2,2,1,3};
 // Source-resolution rows preserve exact banks and holes. Displacement is
 // relative to the ground anchor so panning cannot change its wave phase.
 for(int y=y0;y<y1;y++){
  double dy=y-anchor,wave=(sin(dy*.27+seconds*1.7)+.45*sin(dy*.11-seconds*.8))*ripple;
  double fade=1-clamp(dy/extent,0,1);fade=.22+.78*fade;
  if(anchor-d.y>220)fade*=clamp((extent-dy)/20,0,1);
  int x0=(int)clamp(ceil(d.x+wave),0,w),x1=(int)clamp(floor(d.x+d.w+wave),0,w);
  for(int x=x0;x<x1;){
   if(mask[(size_t)y*w+x]!=160){x++;continue;}int left=x++;while(x<x1&&mask[(size_t)y*w+x]==160)x++;
   spans++;for(int j=0;j<6;j++){
    int q=order[j];double px=q&1?x:left,py=y+q/2;
    double u=clamp((px-d.x-wave)/d.w,0,1),tv=clamp((2*anchor-py-d.y)/d.h,0,1);
    v[n++]=(SDL_Vertex){{(float)(ox+px*k),(float)(oy+py*k)},
      {(Uint8)(cr*.83),(Uint8)(cg*.91),cb,(Uint8)lround(ca*strength*fade)},
      {(float)(flip&1?1-u:u),(float)(flip&2?1-tv:tv)}};
   }
   if(n==3072){ok&=SDL_RenderGeometry(r,tx,v,n,NULL,0)==0;n=0;}
  }
 }
 if(n)ok&=SDL_RenderGeometry(r,tx,v,n,NULL,0)==0;
 SDL_SetTextureColorMod(tx,cr,cg,cb);SDL_SetTextureAlphaMod(tx,ca);SDL_SetTextureBlendMode(tx,blend);
 return ok?spans:-1;
}
