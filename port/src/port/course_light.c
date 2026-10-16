#include "port/course_light.h"
#include "port/light_transfer.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static double clamp(double x,double lo,double hi){return isfinite(x)?fmax(lo,fmin(hi,x)):lo;}
int course_windows_load(CourseWindows *out,const char *path){
  if(!out||!path)return 0;
  FILE *f=fopen(path,"r");if(!f)return 0;
  CourseWindows next={0};char row[512];int ok=1;
  while(fgets(row,sizeof row,f)){
    if(row[0]=='#'||row[0]=='\n'||row[0]=='\r')continue;
    CourseWindow p={0};char tail;
    if(next.count==COURSE_WINDOW_LIMIT||sscanf(row,"%99[^,],%d,%f,%f,%f,%f,%f,%f %c",p.asset,&p.view,&p.x,&p.y,&p.w,&p.h,&p.ground_x,&p.ground_y,&tail)!=8||
       p.view<0||p.view>3||!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.w)||!isfinite(p.h)||!isfinite(p.ground_x)||!isfinite(p.ground_y)||
       p.x<0||p.y<0||p.w<=0||p.h<=0||p.x+p.w>1.001||p.y+p.h>1.001||p.ground_x<0||p.ground_x>1||p.ground_y<0||p.ground_y>1){ok=0;break;}
    next.windows[next.count++]=p;
  }
  if(ferror(f))ok=0;fclose(f);if(!ok||!next.count)return 0;*out=next;return 1;
}
double course_light_night(const CourseLight *l){double n=clamp((.65-l->daylight)/.65,0,1);return n*n*(3-2*n);}
void course_light_color(const CourseLight *l,int building,double u,double v,Uint8 rgb[3]){
  double nx=(u-.5)*1.4,ny=(.60-v)*1.25;
  // Broad roof planes versus a curved leafy canopy. These are inferred normals,
  // not a claim of recovered geometry or physically based source materials.
  if(building){nx=u<.5?-.55:.55;ny=v<.52?.7:.08;}
  double dot=nx*l->light_x+ny*l->elevation+(building?.18:.3)*l->light_y;
  double direct=clamp(l->surfaces,0,1)*clamp(l->strength,0,1);
  double shade=1-direct*(.14-.13*clamp(dot,-1,1));
  double ambient=clamp(l->ambient,0,1),up=clamp(1-v,0,1);
  double bounce=ambient*(1-up)*.035;
  double sky=ambient*up*.028;
  double warm=clamp((.6-l->elevation)/.6,0,1)*l->daylight*direct;
  double enclosure=(1-clamp(u,0,1))*l->enclosure_left+clamp(u,0,1)*l->enclosure_right;
  shade*=1-.24*clamp(enclosure,0,1)*(.2+.8*clamp(v,0,1));
  double channels[3]={shade+sky*.35+bounce-warm*.005,shade+sky*.65+bounce*.7-warm*.025,shade+sky-bounce*.4-warm*.055};
  for(int c=0;c<3;c++){double v=clamp(channels[c],0,1);if(l->linear)v=light_display_encode((float)v);rgb[c]=(Uint8)lround(v*255); }
}
int course_light_sprite(SDL_Renderer *r,SDL_Texture *tx,SDL_FRect d,int flip,int building,const CourseLight *l){
  if(!r||!tx||!l||!isfinite(d.x)||!isfinite(d.y)||!isfinite(d.w)||!isfinite(d.h)||d.w<=0||d.h<=0)return 0;
  SDL_Vertex v[9*7];int indices[8*6*6],n=0;
  Uint8 cr,cg,cb,ca;SDL_GetTextureColorMod(tx,&cr,&cg,&cb);SDL_GetTextureAlphaMod(tx,&ca);
  for(int y=0;y<=6;y++)for(int x=0;x<=8;x++){
    double u=x/8.,t=y/6.;Uint8 rgb[3];course_light_color(l,building,u,t,rgb);
    v[y*9+x]=(SDL_Vertex){{d.x+(float)(u*d.w),d.y+(float)(t*d.h)},
      {(Uint8)(rgb[0]*cr/255),(Uint8)(rgb[1]*cg/255),(Uint8)(rgb[2]*cb/255),ca},
      {(float)(flip&1?1-u:u),(float)(flip&2?1-t:t)}};
  }
  for(int y=0;y<6;y++)for(int x=0;x<8;x++){int a=y*9+x;int q[6]={a,a+1,a+9,a+9,a+1,a+10};for(int j=0;j<6;j++)indices[n++]=q[j];}
  // Geometry carries the color modulation, so neutralize the texture temporarily.
  SDL_SetTextureColorMod(tx,255,255,255);SDL_SetTextureAlphaMod(tx,255);
  int ok=SDL_RenderGeometry(r,tx,v,63,indices,n)==0;
  SDL_SetTextureColorMod(tx,cr,cg,cb);SDL_SetTextureAlphaMod(tx,ca);return ok;
}
uint8_t *course_light_windows(const CourseWindows *ps,const char *asset,int view,const uint8_t *src,int w,int h,int *lit){
  if(lit)*lit=0;if(!ps||!asset||!src||w<=0||h<=0||w>4096||h>4096)return NULL;
  int matches=0;for(int i=0;i<ps->count;i++)matches+=ps->windows[i].view==view&&!strcmp(ps->windows[i].asset,asset);
  if(!matches)return NULL;uint8_t *out=calloc((size_t)w*h,4);if(!out)return NULL;int count=0;
  for(int i=0;i<ps->count;i++){
    const CourseWindow *p=&ps->windows[i];if(p->view!=view||strcmp(p->asset,asset))continue;
    int x0=(int)(p->x*w),y0=(int)(p->y*h),x1=(int)ceil((p->x+p->w)*w),y1=(int)ceil((p->y+p->h)*h);
    for(int y=y0;y<y1&&y<h;y++)for(int x=x0;x<x1&&x<w;x++){
      const uint8_t *s=src+((size_t)y*w+x)*4;uint8_t *d=out+((size_t)y*w+x)*4;
      // Blue/neutral dark glass only. Excludes pale frames, red trim, greenery,
      // transparent background and bright roofs inside an imperfect rectangle.
      if(s[3]<200||s[0]>125||s[1]>135||s[2]>145||s[1]>s[0]*1.25+5||s[0]>s[2]*1.65+8)continue;
      if(!d[3])count++;d[0]=255;d[1]=176;d[2]=78;d[3]=210;
    }
  }
  if(lit)*lit=count;if(!count){free(out);return NULL;}return out;
}
int course_light_spill(SDL_Renderer *r,const uint8_t *ground,int w,int h,double ox,double oy,double k,double x,double y,double radius,double gain){
  if(!r||!ground||w<=0||h<=0||w>4096||h>4096||!isfinite(k)||k<=0||!isfinite(x)||!isfinite(y)||!isfinite(radius)||radius<=0||!isfinite(ox)||!isfinite(oy))return 0;
  radius=clamp(radius,1,80);gain=clamp(gain,0,1);if(!gain)return 0;
  SDL_BlendMode old;SDL_GetRenderDrawBlendMode(r,&old);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);
  SDL_Vertex vertices[4092];int n=0,ok=1,cells=0;const int order[6]={0,1,2,2,1,3};
  int x0=(int)clamp(floor(x-radius),0,w),x1=(int)clamp(ceil(x+radius),0,w),y0=(int)clamp(floor(y-radius*.55),0,h),y1=(int)clamp(ceil(y+radius*.55),0,h);
  for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
    if(ground[(size_t)yy*w+xx]<127||ground[(size_t)yy*w+xx]>143)continue;
    cells++;
    for(int j=0;j<6;j++){
      int q=order[j];double px=xx+(q&1),py=yy+q/2;
      double fall=fmax(0,1-hypot((px-x)/radius,(py-y)/(radius*.55)));fall*=fall;
      vertices[n++]=(SDL_Vertex){{(float)(ox+px*k),(float)(oy+py*k)},{255,171,74,(Uint8)lround(105*gain*fall)},{0,0}};
    }
    if(n==4092){ok&=SDL_RenderGeometry(r,NULL,vertices,n,NULL,0)==0;n=0;}
  }
  if(n)ok&=SDL_RenderGeometry(r,NULL,vertices,n,NULL,0)==0;SDL_SetRenderDrawBlendMode(r,old);return ok?cells:0;
}

static double enclosure_kernel(double x,double y,double cx,double cy,double rx,double ry){
  if(rx<=0||ry<=0)return 0;double dx=(x-cx)/rx,dy=(y-cy)/ry,d=dx*dx+dy*dy;
  return d<6.25?exp(-2*d):0;
}
void course_enclosure_draw_destroy(CourseEnclosureDraw *c){
  if(!c)return;if(c->texture)SDL_DestroyTexture(c->texture);memset(c,0,sizeof *c);
}
int course_enclosure_draw(CourseEnclosureDraw *c,SDL_Renderer *r,const CourseEnclosure *f,
 const uint8_t *ground,int w,int h,double ox,double oy,double k,double strength,int linear){
  if(!c||!r||!f||!ground||f->cols<2||f->rows<2||f->cols>128||f->rows>128||
    !isfinite(f->cell)||f->cell<=0||w<1||h<1||w>4096||h>4096||
    !isfinite(ox)||!isfinite(oy)||!isfinite(k)||k<=0||!isfinite(strength)||strength<=0)return 0;
  strength=clamp(strength,0,1);linear=linear!=0;
  if(c->renderer!=r||c->cols!=f->cols||c->rows!=f->rows)course_enclosure_draw_destroy(c);
  if(!c->texture){
    c->texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,f->cols,f->rows);
    if(!c->texture)return 0;c->renderer=r;
    SDL_SetTextureBlendMode(c->texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(c->texture,SDL_ScaleModeLinear);
  }
  if(!c->valid||c->strength!=strength||c->linear!=linear||c->cell!=f->cell||memcmp(c->ground,f->ground,sizeof c->ground)){
    for(int y=0;y<f->rows;y++)for(int x=0;x<f->cols;x++){
      double shade=clamp(f->ground[y*128+x],0,1)*strength*.20;
      double alpha=linear?1-light_display_encode((float)(1-shade)):shade;
      size_t p=((size_t)y*128+x)*4;c->pixels[p]=c->pixels[p+1]=c->pixels[p+2]=0;c->pixels[p+3]=(uint8_t)lround(alpha*255);
    }
    c->valid=0;if(SDL_UpdateTexture(c->texture,NULL,c->pixels,128*4)<0)return 0;
    memcpy(c->ground,f->ground,sizeof c->ground);c->strength=strength;c->linear=linear;c->cell=f->cell;c->cols=f->cols;c->rows=f->rows;c->valid=1;c->uploads++;
  }
  SDL_Vertex v[4092];int used=0,ok=1;const int order[6]={0,1,2,2,1,3};
  for(int y=0;y<h;y++)for(int x=0;x<w;){
    if(!ground[(size_t)y*w+x]){x++;continue;}int left=x++;while(x<w&&ground[(size_t)y*w+x])x++;
    for(int j=0;j<6;j++){int q=order[j];double px=q&1?x:left,py=y+q/2;
      v[used++]=(SDL_Vertex){{(float)(ox+px*k),(float)(oy+py*k)},{255,255,255,255},
        {(float)((px/f->cell+.5)/f->cols),(float)((py/f->cell+.5)/f->rows)}};
    }
    if(used==4092){ok&=SDL_RenderGeometry(r,c->texture,v,used,NULL,0)==0;used=0;}
  }
  if(used)ok&=SDL_RenderGeometry(r,c->texture,v,used,NULL,0)==0;
  return ok;
}
void course_enclosure_begin(CourseEnclosure *f,int w,int h){
  if(!f)return;memset(f,0,sizeof *f);if(w<1||h<1||w>4096||h>4096)return;
  f->cell=fmax(12,ceil(fmax(w,h)/126.));f->cols=(int)ceil(w/f->cell)+1;f->rows=(int)ceil(h/f->cell)+1;
}
void course_enclosure_add(CourseEnclosure *f,double x,double y,double rx,double ry){
  if(!f||!f->cols||!isfinite(x)||!isfinite(y)||!isfinite(rx)||!isfinite(ry)||rx<=0||ry<=0)return;
  rx=clamp(rx,2,300);ry=clamp(ry,2,180);
  int x0=(int)clamp(floor((x-rx*2.5)/f->cell),0,f->cols-1),x1=(int)clamp(ceil((x+rx*2.5)/f->cell),0,f->cols-1);
  int y0=(int)clamp(floor((y-ry*2.5)/f->cell),0,f->rows-1),y1=(int)clamp(ceil((y+ry*2.5)/f->cell),0,f->rows-1);
  for(int yy=y0;yy<=y1;yy++)for(int xx=x0;xx<=x1;xx++){
    int p=yy*128+xx;f->density[p]+=(float)enclosure_kernel(xx*f->cell,yy*f->cell,x,y,rx,ry);
    f->ground[p]=(float)(1-exp(-fmin(8,fmax(0,f->density[p]-.95))*.55));
  }
}
double course_enclosure_sample(const CourseEnclosure *f,double x,double y,double sx,double sy,double rx,double ry){
  if(!f||f->cols<2||f->rows<2||!isfinite(x)||!isfinite(y)||x<0||y<0)return 0;
  double px=x/f->cell,py=y/f->cell;int ix=(int)px,iy=(int)py;if(ix>=f->cols-1||iy>=f->rows-1)return 0;
  double u=px-ix,v=py-iy,weights[4]={(1-u)*(1-v),u*(1-v),(1-u)*v,u*v},density=0;
  for(int q=0;q<4;q++){int xx=ix+(q&1),yy=iy+q/2;double d=rx<=0?f->ground[yy*128+xx]:f->density[yy*128+xx];
    if(rx>0&&ry>0)d-=enclosure_kernel(xx*f->cell,yy*f->cell,sx,sy,clamp(rx,2,300),clamp(ry,2,180));
    density+=fmax(0,d)*weights[q];}
  // Ground only gains enclosure where multiple volumes overlap. Standalone
  // trees keep their existing contact shadows without another round blob.
  if(rx<=0)return density;
  return 1-exp(-fmin(8,density)*.55);
}
