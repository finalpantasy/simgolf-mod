// Locally authored, provisional original-scale course furniture. No game art is copied.
#include "port/course_lamps_native.h"
#include "port/hd_art.h"
#include "port/host.h"
#include "port/release_data.h"
#include "port/runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uint32_t sprites[LAMP_STYLES][32*64];
static SDL_Texture *tex[LAMP_STYLES],*spill;
static SDL_Renderer *owner;
static uint32_t *pixels;
static uint8_t *last_land;
static LampFrame cached;
static int cw,ch,valid;
static SDL_Texture *model[LAMP_STYLES][4][2];
static int model_tried;
static void model_setup(SDL_Renderer *r){
  if(model_tried)return;model_tried=1;
  for(int s=0;s<LAMP_STYLES;s++)for(int v=0;v<4;v++)for(int n=0;n<2;n++){
    char path[1024];int w=0,h=0;
    release_data_path(path,sizeof path,port_host_path("/ui/../data"),"course-lamps");
    size_t used=strlen(path);snprintf(path+used,sizeof path-used,"/%s-v%d-%s.png",lamps_art_id(s),v,n?"night":"day");
    uint32_t *p=hd_art_load_png(path,&w,&h);
    if(!p)continue;
    if(w!=512||h!=768){free(p);continue;}
    model[s][v][n]=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,w,h);
    if(!model[s][v][n]){free(p);continue;}
    SDL_UpdateTexture(model[s][v][n],NULL,p,w*4);
    free(p);
    SDL_SetTextureBlendMode(model[s][v][n],SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(model[s][v][n],SDL_ScaleModeLinear);
    fprintf(stderr,"course lights: modeled %s v%d %s loaded 512x768\n",lamps_art_id(s),v,n?"night":"day");
  }
}
static int setup(SDL_Renderer *r,int w,int h){
  if(owner!=r){for(int s=0;s<LAMP_STYLES;s++)for(int v=0;v<4;v++)for(int n=0;n<2;n++){if(model[s][v][n])SDL_DestroyTexture(model[s][v][n]);model[s][v][n]=NULL;}model_tried=0;for(int i=0;i<LAMP_STYLES;i++){if(tex[i])SDL_DestroyTexture(tex[i]);tex[i]=NULL;}if(spill)SDL_DestroyTexture(spill);spill=NULL;owner=r;valid=0;for(int i=0;i<LAMP_STYLES;i++)lamps_sprite_rgba(i,sprites[i]);}
  model_setup(r);
  for(int s=0;s<LAMP_STYLES;s++)if(!tex[s]){tex[s]=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,32,64);if(!tex[s])return 0;SDL_UpdateTexture(tex[s],NULL,sprites[s],32*4);SDL_SetTextureBlendMode(tex[s],SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(tex[s],SDL_ScaleModeNearest);}
  if(cw!=w||ch!=h){if(spill)SDL_DestroyTexture(spill);spill=NULL;free(pixels);free(last_land);pixels=calloc((size_t)w*h,4);last_land=calloc((size_t)w*h,1);cw=w;ch=h;valid=0;}
  if(!pixels||!last_land)return 0;
  if(!spill){spill=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h);if(!spill)return 0;SDL_SetTextureBlendMode(spill,SDL_BLENDMODE_ADD);SDL_SetTextureScaleMode(spill,SDL_ScaleModeLinear);}
  return 1;
}
static void spill_build(const LampFrame *f,const uint8_t *land,int w,int h){
  memset(pixels,0,(size_t)w*h*4);
  for(int i=0;i<f->count;i++){
    const LampPoint *p=&f->points[i];const LampStyle *s=&f->styles[p->style];
    double rx=s->radius*8*f->zoom,ry=rx*.625;
    int x0=(int)fmax(0,floor(p->x-rx)),x1=(int)fmin(w,ceil(p->x+rx));
    int y0=(int)fmax(0,floor(p->y-ry)),y1=(int)fmin(h,ceil(p->y+ry));
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
      int q=y*w+x;if(land[q]<127||land[q]>143)continue;
      double dx=(x+.5-p->x)/rx,dy=(y+.5-p->y)/ry;float a=lamps_falloff((float)sqrt(dx*dx+dy*dy),1)*.30f;
      uint32_t c=0xff000000|((uint32_t)(s->r*a)<<16)|((uint32_t)(s->g*a)<<8)|(uint32_t)(s->b*a);
      // Strongest single light wins. Overlap never multiplies brightness.
      unsigned old=(pixels[q]>>16)&255;if(((c>>16)&255)>old)pixels[q]=c;
    }
  }
  SDL_UpdateTexture(spill,NULL,pixels,w*4);cached=*f;memcpy(last_land,land,(size_t)w*h);valid=1;
}
static int model_pole(SDL_Renderer *r,const LampPoint *p,int rotation,int zoom,float height,double ox,double oy,double k,double night,int ghost){
  if(p->style<0||p->style>=LAMP_STYLES||!model[p->style][rotation&3][0])return 0;
  double z=height*(zoom/4.)/640.,bx=p->x-256*z,by=p->y-700*z;
  // Clip in course coordinates while sampling the full-resolution model bake.
  // Each visible course pixel keeps its continuous UV range at any screen scale.
  const int order[6]={0,1,2,2,1,3};SDL_Vertex verts[4092];int count;
  for(int mode=0;mode<2;mode++){
    SDL_Texture *t=model[p->style][rotation&3][mode];if(!t)continue;
    double alpha=mode?night:1;if(alpha<.001)continue;
    Uint8 shade=mode?255:(Uint8)lround(255*(1-night*.55));SDL_SetTextureColorMod(t,shade,shade,shade);
    SDL_SetTextureAlphaMod(t,(Uint8)lround((ghost?135:255)*alpha));count=0;
    for(int y=(int)floor(by);y<(int)ceil(by+768*z);y++)for(int x=(int)floor(bx);x<(int)ceil(bx+512*z);){
      if(hd_art_lamp_occluded(x,y,p->y)){x++;continue;}
      int start=x++;while(x<(int)ceil(bx+512*z)&&!hd_art_lamp_occluded(x,y,p->y))x++;
      double left=fmax(bx,start),right=fmin(bx+512*z,x),top=fmax(by,y),bottom=fmin(by+768*z,y+1);
      for(int j=0;j<6;j++){int q=order[j];double xx=q&1?right:left,yy=q/2?bottom:top;
        verts[count++]=(SDL_Vertex){{(float)(ox+xx*k),(float)(oy+yy*k)},{255,255,255,255},{(float)((xx-bx)/(512*z)),(float)((yy-by)/(768*z))}};
      }
      if(count==4092){SDL_RenderGeometry(r,t,verts,count,NULL,0);count=0;}
    }
    if(count)SDL_RenderGeometry(r,t,verts,count,NULL,0);
  }
  return 1;
}
static void pole(SDL_Renderer *r,const LampPoint *p,int rotation,int zoom,float height,double ox,double oy,double k,double night,int ghost){
  if(model_pole(r,p,rotation,zoom,height,ox,oy,k,night,ghost))return;
  int style=p->style;float z=zoom/4.f;
  // Normalize to each design's occupied artwork, not its padded sprite canvas.
  int top=style==4?46:style==5?3:style==1?16:style==2||style==3?12:9;
  float zy=height/(62.f-top)*z;
  double bx=p->x-16*z,by=p->y-62*zy;
  Uint8 shade=(Uint8)lround(255*(1-night*.57));SDL_SetTextureColorMod(tex[style],shade,shade,shade);SDL_SetTextureAlphaMod(tex[style],ghost?135:255);
  SDL_Vertex v[4092];int n=0;const int order[6]={0,1,2,2,1,3};
  for(int y=0;y<64;y++)for(int x=0;x<32;){
    double px=bx+x*z,py=by+y*zy;
    if(!(sprites[style][y*32+x]>>24)||hd_art_lamp_occluded((int)px,(int)py,p->y)){x++;continue;}
    int start=x++;
    while(x<32&&(sprites[style][y*32+x]>>24)&&!hd_art_lamp_occluded((int)(bx+x*z),(int)py,p->y))x++;
    for(int j=0;j<6;j++){int q=order[j];float xx=q&1?(float)x:(float)start,yy=(float)(y+q/2);
      v[n++]=(SDL_Vertex){{(float)(ox+(bx+xx*z)*k),(float)(oy+(by+yy*zy)*k)},{255,255,255,255},{xx/32,yy/64}};
    }
    if(n==4092){SDL_RenderGeometry(r,tex[style],v,n,NULL,0);n=0;}
  }
  if(n)SDL_RenderGeometry(r,tex[style],v,n,NULL,0);
  if(night>.01&&!ghost){
    // A small visible emitter; the ground spill does the useful illumination.
    double ex=p->x+(style==1?7:0)*z,ey=by+(style==4?50:style==5?8:style==2||style==3?24:style==1?20:21)*zy;
    if(!hd_art_lamp_occluded((int)ex,(int)ey,p->y)){
      SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);SDL_SetRenderDrawColor(r,255,215,145,(Uint8)(night*210));
      SDL_FRect glow={(float)(ox+(ex-2*z)*k),(float)(oy+(ey-1*z)*k),(float)(4*z*k),(float)(3*z*k)};SDL_RenderFillRectF(r,&glow);
    }
  }
}
static void contact_shadow(SDL_Renderer *r,const LampPoint *p,int zoom,const uint8_t *land,int w,int h,double ox,double oy,double k){
  double rx=2.6*zoom/4.,ry=fmax(.6,rx*.4);
  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
  for(int y=(int)floor(p->y-ry);y<=(int)ceil(p->y+ry);y++)for(int x=(int)floor(p->x-rx);x<=(int)ceil(p->x+rx);x++){
    if(x<0||y<0||x>=w||y>=h||land[y*w+x]<127||land[y*w+x]>143)continue;
    double dx=(x+.5-p->x)/rx,dy=(y+.5-p->y)/ry,d=dx*dx+dy*dy;if(d>=1)continue;
    SDL_SetRenderDrawColor(r,20,30,18,(Uint8)(60*(1-d)));
    SDL_FRect dst={(float)(ox+x*k),(float)(oy+y*k),(float)k,(float)k};SDL_RenderFillRectF(r,&dst);
  }
}
void lamps_render(SDL_Renderer *r,const LampFrame *f,SDL_Rect clip,const uint8_t *land,int w,int h,double ox,double oy,double k,double night){
  if(!r||!f||!f->enabled||!land||w<1||h<1||w>4096||h>4096||k<=0||!setup(r,w,h))return;
  night=fmax(0,fmin(1,night));SDL_Rect old,combined;SDL_bool clipped=SDL_RenderIsClipEnabled(r);SDL_RenderGetClipRect(r,&old);if(clipped){if(!SDL_IntersectRect(&clip,&old,&combined))return;clip=combined;}SDL_RenderSetClipRect(r,&clip);
  SDL_BlendMode oldblend;Uint8 cr,cg,cb,ca;SDL_GetRenderDrawBlendMode(r,&oldblend);SDL_GetRenderDrawColor(r,&cr,&cg,&cb,&ca);
  if(night>.001&&f->count){
    if(!valid||cached.count!=f->count||cached.zoom!=f->zoom||memcmp(cached.styles,f->styles,sizeof f->styles)||memcmp(cached.points,f->points,sizeof f->points)||memcmp(last_land,land,(size_t)w*h))spill_build(f,land,w,h);
    SDL_SetTextureAlphaMod(spill,(Uint8)lround(255*night));SDL_FRect d={(float)ox,(float)oy,(float)(w*k),(float)(h*k)};SDL_RenderCopyF(r,spill,NULL,&d);
  }
  // Far anchors first, so nearby poles occlude distant poles.
  for(int i=0;i<f->count;i++)contact_shadow(r,&f->points[i],f->zoom,land,w,h,ox,oy,k);
  int order[LAMP_LIMIT];for(int i=0;i<f->count;i++){int j=i;while(j>0&&f->points[order[j-1]].y>f->points[i].y){order[j]=order[j-1];j--;}order[j]=i;}
  for(int i=0;i<f->count;i++)pole(r,&f->points[order[i]],f->rotation,f->zoom,f->styles[f->points[order[i]].style].height,ox,oy,k,night,0);
  if(f->selected>=0&&f->hover_x>0){
    int s=f->hover_style;double radius=(s<LAMP_STYLES?f->styles[s].radius:1)*8*f->zoom;
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(r,f->hover_valid?135:255,f->hover_valid?239:115,155,180);
    for(int i=0;i<80;i++){double a=i*6.28318530718/80,b=(i+1)*6.28318530718/80;SDL_RenderDrawLineF(r,(float)(ox+(f->hover_x+cos(a)*radius)*k),(float)(oy+(f->hover_y+sin(a)*radius*.625)*k),(float)(ox+(f->hover_x+cos(b)*radius)*k),(float)(oy+(f->hover_y+sin(b)*radius*.625)*k));}
    if(s<LAMP_STYLES)pole(r,&(LampPoint){f->hover_x,f->hover_y,s},f->rotation,f->zoom,f->styles[s].height,ox,oy,k,night,1);
  }
  SDL_SetRenderDrawBlendMode(r,oldblend);SDL_SetRenderDrawColor(r,cr,cg,cb,ca);SDL_RenderSetClipRect(r,clipped?&old:NULL);
}

void lamps_device_reset(void) {
  for(int s=0;s<LAMP_STYLES;s++) {
    if(tex[s])SDL_DestroyTexture(tex[s]);tex[s]=NULL;
    for(int v=0;v<4;v++)for(int n=0;n<2;n++) {
      if(model[s][v][n])SDL_DestroyTexture(model[s][v][n]);model[s][v][n]=NULL;
    }
  }
  if(spill)SDL_DestroyTexture(spill);spill=NULL;
  model_tried=valid=0;
}
