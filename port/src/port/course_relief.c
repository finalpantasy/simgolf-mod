#include "port/course_relief.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static double clamp(double x,double a,double b){return !isfinite(x)?a:fmax(a,fmin(b,x));}
int course_relief_material(const char *name){
  char path[256];int i=0;
  if(!name)return 0;
  for(;i<255&&name[i];i++)path[i]=(char)tolower((unsigned char)name[i]);path[i]=0;
  if(strstr(path,"sand")||strstr(path,"bunker"))return 3;
  if(strstr(path,"path")||strstr(path,"pav")||strstr(path,"road"))return 2;
  if(strstr(path,"grass")||strstr(path,"green")||strstr(path,"fairway"))return 1;
  if(strstr(path,"desert"))return 3;
  if(strstr(path,"rough"))return 1;
  return 0;
}
void course_relief_destroy(CourseRelief *f){if(f){free(f->lift);free(f->features);memset(f,0,sizeof *f);}}
double course_relief_valley(const HdtScene *s,double x,double z){
  if(!s||!s->wheight||s->map_w<3||s->map_h<3||s->map_w>256||s->map_h>256||!isfinite(x)||!isfinite(z))return 0;
  int ix=(int)floor(x/100+s->map_w*.5),iy=(int)floor(z/100+s->map_h*.5);
  if(ix<0||iy<0||ix>=s->map_w||iy>=s->map_h)return 0;
  double mid=s->wheight[iy*s->map_w+ix];if(!isfinite(mid))return 0;
  double sum=0,lo=mid,hi=mid,water=0,weights=0;int n=0;
  for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++){
    int xx=ix+dx,yy=iy+dy;if(xx<0||yy<0||xx>=s->map_w||yy>=s->map_h)continue;
    double h=s->wheight[yy*s->map_w+xx];if(!isfinite(h))continue;
    double weight=1./(1+dx*dx+dy*dy);weights+=weight;
    if(s->wcode&&s->wcode[yy*s->map_w+xx])water+=weight;
    sum+=h;n++;lo=fmin(lo,h);hi=fmax(hi,h);
  }
  if(!n)return 0;
  double hollow=clamp((sum/n-mid)/24,0,1),ridge=clamp((mid-sum/n)/30,0,1);
  double shelter=1-.75*clamp((hi-lo)/130,0,1);
  return clamp((.08+.82*hollow+.75*water/fmax(.01,weights))*shelter*(1-ridge),0,1);
}
int course_relief_build(CourseRelief *out,const HdtScene *s,int w,int h){
  if(!out||!s||!s->verts||!s->world||!s->batches||w<1||h<1||w>4096||h>4096||
     (size_t)w*h>4194304||s->tw<=0||s->th<=0||s->nbatches<1||s->nbatches>65536)return 0;
  if(out->lift&&out->width==w&&out->height==h&&out->serial==s->serial&&out->map_serial==s->map_serial)return 1;
  double sum=0;int points=0;
  for(int b=0;b<s->nbatches;b++){
    int first=s->batches[b*5],count=s->batches[b*5+1];
    if(first<0||count<0||first>750000-count||count%3)return 0;
    if(s->batches[b*5+4]&1)continue;
    for(int i=first;i<first+count;i++){double v=s->world[i*3+1];if(isfinite(v)&&fabs(v)<1e6){sum+=v;points++;}}
  }
  if(!points||!isfinite(s->height_pixels)||s->height_pixels<0||s->height_pixels>100)return 0;
  CourseRelief n={0};n.width=w;n.height=h;n.serial=s->serial;n.map_serial=s->map_serial;
  n.datum=(float)(sum/points);n.height_pixels=s->height_pixels*h/s->th;
  n.lift=malloc((size_t)w*h*sizeof(float));n.features=calloc((size_t)w*h,3);
  if(!n.lift||!n.features){course_relief_destroy(&n);return 0;}
  for(size_t i=0;i<(size_t)w*h;i++)n.lift[i]=NAN;
  for(int b=0;b<s->nbatches;b++){
    int first=s->batches[b*5],count=s->batches[b*5+1],flags=s->batches[b*5+4];if(flags&1)continue;
    int material=s->materials?s->materials[b]&3:0;
    for(int i=first;i+2<first+count;i+=3){
      double x[3],y[3],e[3],valley[3];int valid=1;
      for(int j=0;j<3;j++){
        x[j]=s->verts[(i+j)*8]*(double)w/s->tw;y[j]=(s->th-s->verts[(i+j)*8+1])*(double)h/s->th;
        e[j]=(s->world[(i+j)*3+1]-n.datum)*n.height_pixels;
        if(!isfinite(x[j])||!isfinite(y[j])||!isfinite(e[j])||fabs(x[j])>1e7||fabs(y[j])>1e7||fabs(e[j])>4096)valid=0;
        valley[j]=flags&8?1:course_relief_valley(s,s->world[(i+j)*3],s->world[(i+j)*3+2]);
      }
      double den=(y[1]-y[2])*(x[0]-x[2])+(x[2]-x[1])*(y[0]-y[2]);if(!valid||fabs(den)<1e-6)continue;
      int left=(int)clamp(floor(fmin(x[0],fmin(x[1],x[2]))),0,w-1),right=(int)clamp(ceil(fmax(x[0],fmax(x[1],x[2]))),0,w-1);
      int top=(int)clamp(floor(fmin(y[0],fmin(y[1],y[2]))),0,h-1),bottom=(int)clamp(ceil(fmax(y[0],fmax(y[1],y[2]))),0,h-1);
      for(int yy=top;yy<=bottom;yy++)for(int xx=left;xx<=right;xx++){
        double a=((y[1]-y[2])*(xx+.5-x[2])+(x[2]-x[1])*(yy+.5-y[2]))/den;
        double c=((y[2]-y[0])*(xx+.5-x[2])+(x[0]-x[2])*(yy+.5-y[2]))/den,d=1-a-c;if(a<-.00001||c<-.00001||d<-.00001)continue;
        size_t p=(size_t)yy*w+xx;double lift=a*e[0]+c*e[1]+d*e[2];n.lift[p]=(float)lift;n.max_lift=fmaxf(n.max_lift,(float)fabs(lift));
        double nx=0,nz=0;
        if(s->normals){nx=a*s->normals[i*3]+c*s->normals[(i+1)*3]+d*s->normals[(i+2)*3];nz=a*s->normals[i*3+2]+c*s->normals[(i+1)*3+2]+d*s->normals[(i+2)*3+2];}
        if(!isfinite(nx)||!isfinite(nz)){nx=nz=0;}
        int qx=(int)lround(8+7*clamp(nx,-1,1)),qz=(int)lround(8+7*clamp(nz,-1,1));
        n.features[p*3]=(uint8_t)(qx|(qz<<4));n.features[p*3+1]=(uint8_t)lround(clamp(a*valley[0]+c*valley[1]+d*valley[2],0,1)*255);
        n.features[p*3+2]=(uint8_t)(252+material);
      }
    }
  }
  course_relief_destroy(out);*out=n;return 1;
}
double course_relief_sample(const CourseRelief *f,double x,double y){
  if(!f||!f->lift||f->width<1||f->height<1||!isfinite(x)||!isfinite(y)||x<0||y<0||x>=f->width||y>=f->height)return 0;
  x=fmax(0,x-.5);y=fmax(0,y-.5);int ix=(int)x,iy=(int)y,jx=ix+1<f->width?ix+1:ix,jy=iy+1<f->height?iy+1:iy;
  double u=x-ix,v=y-iy,total=0,weight=0;int xx[4]={ix,jx,ix,jx},yy[4]={iy,iy,jy,jy};double ww[4]={(1-u)*(1-v),u*(1-v),(1-u)*v,u*v};
  for(int i=0;i<4;i++){float z=f->lift[(size_t)yy[i]*f->width+xx[i]];if(isfinite(z)){total+=z*ww[i];weight+=ww[i];}}
  return weight>.001?total/weight:0;
}
void course_relief_shadow(double x,double y,double lift,double sx,double sy,double *u,double *v){
  if(!isfinite(lift))lift=0;*u=x+sx*lift;*v=y+(1+sy)*lift;
}
