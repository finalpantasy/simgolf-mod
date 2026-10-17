#include "port/course_lamps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
_Static_assert(sizeof(CourseLamp)==12,"Course lamp v2 record layout must remain stable");

int lamps_load_styles(LampStyle out[LAMP_STYLES],const char *path){
  FILE *f=fopen(path,"r");if(!f)return 0;
  LampStyle next[LAMP_STYLES]={0};char line[256];int n=0,ok=1;
  while(fgets(line,sizeof line,f)){
    if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;
    int r,g,b;char tail;LampStyle p={0};
    if(n>=LAMP_STYLES||sscanf(line,"%23[^,],%31[^,],%d,%f,%f,%d,%d,%d %c",p.id,p.name,&p.cost,&p.radius,&p.height,&r,&g,&b,&tail)!=8||
      p.cost<1||p.cost>1000||!isfinite(p.radius)||p.radius<.5||p.radius>8||!isfinite(p.height)||p.height<4||p.height>60||r<0||r>255||g<0||g>255||b<0||b>255){ok=0;break;}
    for(int i=0;i<n;i++)if(!strcmp(p.id,next[i].id))ok=0;
    p.r=(uint8_t)r;p.g=(uint8_t)g;p.b=(uint8_t)b;next[n++]=p;
  }
  if(ferror(f))ok=0;fclose(f);if(!ok||n!=LAMP_STYLES)return 0;
  memcpy(out,next,sizeof next);return 1;
}
int lamps_at(const LampState *s,int row,int col){for(int i=0;i<s->count;i++)if(s->lamps[i].row==row&&s->lamps[i].col==col)return i;return -1;}
int lamps_world_row(const CourseLamp *p){return p->row*1024+512+p->offset_row;}
int lamps_world_col(const CourseLamp *p){return p->col*1024+512+p->offset_col;}
int lamps_nearest(const LampState *s,int row,int col,int radius){
  if(!s||s->count<0||s->count>LAMP_LIMIT||radius<0||radius>163840)return -1;
  int nearest=-1;int64_t best=(int64_t)radius*radius;
  for(int i=0;i<s->count;i++){
    int64_t dr=(int64_t)lamps_world_row(&s->lamps[i])-row,dc=(int64_t)lamps_world_col(&s->lamps[i])-col;
    if(llabs(dr)>radius||llabs(dc)>radius)continue;
    int64_t d=dr*dr+dc*dc;if(d<=best){best=d;nearest=i;}
  }return nearest;
}
int lamps_place_at(LampState *s,const LampStyle *p,int style,int row,int col,int w,int h,int spacing,int *cash,int sandbox){
  if(!s||!p||!cash||style<0||style>=LAMP_STYLES||w<3||h<3||w>160||h>160||
    row<1024||col<1024||row>=(h-1)*1024||col>=(w-1)*1024||spacing<0||spacing>1024||s->count<0||s->count>=LAMP_LIMIT||lamps_nearest(s,row,col,spacing)>=0)return 0;
  int cost=p[style].cost;if(cost<1||cost>1000||(!sandbox&&*cash<cost))return 0;
  s->lamps[s->count++]=(CourseLamp){(uint16_t)(row/1024),(uint16_t)(col/1024),(uint8_t)style,0,(uint16_t)(sandbox?0:cost),(int16_t)(row%1024-512),(int16_t)(col%1024-512)};
  if(!sandbox)*cash-=cost;return 1;
}
int lamps_place(LampState *s,const LampStyle *p,int style,int row,int col,int w,int h,int allowed,int *cash,int sandbox){
  if(!s||!p||!cash||style<0||style>=LAMP_STYLES||row<1||col<1||row>=h-1||col>=w-1||!allowed||s->count<0||s->count>=LAMP_LIMIT||lamps_at(s,row,col)>=0)return 0;
  int cost=p[style].cost;if(cost<1||cost>1000||(!sandbox&&*cash<cost))return 0;
  s->lamps[s->count++]=(CourseLamp){(uint16_t)row,(uint16_t)col,(uint8_t)style,0,(uint16_t)(sandbox?0:cost),0,0};
  if(!sandbox)*cash-=cost;return 1;
}
int lamps_remove(LampState *s,int row,int col){int i=lamps_at(s,row,col);if(i<0)return 0;s->lamps[i]=s->lamps[--s->count];memset(&s->lamps[s->count],0,sizeof(CourseLamp));return 1;}
int lamps_remove_at(LampState *s,int row,int col,int radius){int i=lamps_nearest(s,row,col,radius);if(i<0)return 0;s->lamps[i]=s->lamps[--s->count];memset(&s->lamps[s->count],0,sizeof(CourseLamp));return 1;}
float lamps_falloff(float d,float r){if(!isfinite(d)||!isfinite(r)||r<=0||d<0||d>=r)return 0;float t=1-d/r;return t*t*(3-2*t);}

static uint32_t crc32(uint32_t crc,const void *v,size_t n){const uint8_t *p=v;crc=~crc;while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(int32_t)(crc&1));}return ~crc;}
static int file_crc(const char *path,uint32_t *size,uint32_t *crc){FILE *f=fopen(path,"rb");if(!f)return 0;uint8_t b[8192];size_t n;*size=*crc=0;while((n=fread(b,1,sizeof b,f))){*size+=(uint32_t)n;*crc=crc32(*crc,b,n);}int ok=!ferror(f);if(fclose(f))ok=0;return ok;}
typedef struct { char magic[8];uint32_t version,w,h,count,stride,save_size,save_crc,payload_crc; } Header;
static int valid(const LampState *s,int w,int h){if(s->count<0||s->count>LAMP_LIMIT||w<3||h<3||w>160||h>160)return 0;for(int i=0;i<s->count;i++){const CourseLamp *p=&s->lamps[i];if(p->row<1||p->col<1||p->row>=h-1||p->col>=w-1||p->style>=LAMP_STYLES||p->reserved||p->paid>1000||p->offset_row< -512||p->offset_row>511||p->offset_col< -512||p->offset_col>511)return 0;for(int j=0;j<i;j++)if(lamps_world_row(p)==lamps_world_row(&s->lamps[j])&&lamps_world_col(p)==lamps_world_col(&s->lamps[j]))return 0;}return 1;}
int lamps_save(const LampState *s,const char *save,int w,int h){
  if(!s||!save||strlen(save)>900||!valid(s,w,h))return 0;
  Header hdr={{'S','G','L','A','M','P','S',0},2,(uint32_t)w,(uint32_t)h,(uint32_t)s->count,sizeof(CourseLamp),0,0,0};
  if(!file_crc(save,&hdr.save_size,&hdr.save_crc))return 0;
  hdr.payload_crc=crc32(0,s->lamps,s->count*sizeof(CourseLamp));
  char side[940],temp[950];snprintf(side,sizeof side,"%s.course-lights",save);snprintf(temp,sizeof temp,"%s.tmp",side);
  FILE *f=fopen(temp,"wb");if(!f)return 0;
  int ok=fwrite(&hdr,sizeof hdr,1,f)==1&&fwrite(s->lamps,sizeof(CourseLamp),s->count,f)==(size_t)s->count;
  if(fclose(f))ok=0;
#ifdef _WIN32
  extern int __stdcall MoveFileExA(const char *,const char *,unsigned long);
  if(ok)ok=MoveFileExA(temp,side,1)!=0;
#else
  if(ok)ok=rename(temp,side)==0;
#endif
  if(!ok)remove(temp);return ok;
}
int lamps_load(LampState *s,const char *save,int w,int h){
  if(!s||!save||strlen(save)>900)return -1;char side[940];snprintf(side,sizeof side,"%s.course-lights",save);
  FILE *f=fopen(side,"rb");if(!f)return 0;Header hdr={0};LampState next={0};uint32_t size,crc;
  int ok=fread(&hdr,sizeof hdr,1,f)==1&&!memcmp(hdr.magic,"SGLAMPS",8)&&
    ((hdr.version==1&&hdr.stride==8)||(hdr.version==2&&hdr.stride==sizeof(CourseLamp)))&&hdr.w==(uint32_t)w&&hdr.h==(uint32_t)h&&hdr.count<=LAMP_LIMIT;
  uint32_t payload=0;
  if(ok){next.count=(int)hdr.count;for(int i=0;i<next.count&&ok;i++){
    ok=fread(&next.lamps[i],1,hdr.stride,f)==hdr.stride;payload=crc32(payload,&next.lamps[i],hdr.stride);
  }ok=ok&&fgetc(f)==EOF&&!ferror(f);}
  fclose(f);if(ok)ok=valid(&next,w,h)&&payload==hdr.payload_crc&&file_crc(save,&size,&crc)&&size==hdr.save_size&&crc==hdr.save_crc;
  if(!ok)return -1;*s=next;return 1;
}
