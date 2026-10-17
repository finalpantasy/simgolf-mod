// Cosmetic per-path materials. Original game records and pathfinding stay intact.
#include "port/path_styles.h"
#include "port/grid.h"
#include "mods/mods.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CAP = GRID_MAX_SIDE * GRID_MAX_SIDE };
static uint8_t styles[CAP], previous[CAP], staged[CAP], writing[256];
static int baseline, old_w, old_h, loading;
extern void terrain_hd_invalidate(void);
extern const char *crt_real_path(unsigned fd);
static uint16_t *tile_flags(void) { return (uint16_t *)grid_translate(0x53caf0); }
static int valid(int x,int y) { return g_map_w>0&&g_map_h>0&&x>=0&&y>=0&&x<g_map_h&&y<g_map_w&&g_map_w*g_map_h<=CAP; }
int path_styles_selected(void) {
  const char *s=mods_get("build.path_style");
  for(int i=1;s&&i<PATH_STYLE_COUNT;i++)if(!strcmp(s,path_style_id(i)))return i;
  return 0;
}
void path_styles_select(int style) {
  if(style>=0&&style<PATH_STYLE_COUNT)mods_option_set("build.path_style",path_style_id(style));
}
void path_styles_reset(void) { memset(styles,0,sizeof styles);memset(previous,0,sizeof previous);baseline=loading=0;old_w=g_map_w;old_h=g_map_h; }
void path_styles_loaded(void) { loading=0;baseline=0; }
int path_styles_get(int x,int y) { return valid(x,y)?styles[x*g_map_w+y]:0; }
int path_styles_at(int x,int y) { return mods_get_int("art.path_styles",1)?path_styles_get(x,y):0; }
int path_styles_paint(int x,int y,int style) {
  if(!valid(x,y)||style<0||style>=PATH_STYLE_COUNT||!(tile_flags()[x*g_map_w+y]&0x20))return 0;
  styles[x*g_map_w+y]=(uint8_t)style;terrain_hd_invalidate();return 1;
}
void path_styles_frame(void) {
  if(!g_map_w||!g_map_h||!g_grid[9])return;
  if(old_w!=g_map_w||old_h!=g_map_h)path_styles_reset();
  if(loading)return;
  uint16_t *flags=tile_flags();int changed=0;
  int placing=*(int *)(uintptr_t)0x4c2854==0;
  for(int i=0;i<g_map_w*g_map_h;i++) {
    int path=!!(flags[i]&0x20);
    if(!path&&styles[i]) {styles[i]=0;changed=1;}
    if(baseline&&path&&!previous[i]&&placing) {styles[i]=(uint8_t)(mods_get_int("art.path_styles",1)?path_styles_selected():0);changed=1;}
    previous[i]=(uint8_t)path;
  }
  baseline=1;
  if(changed)terrain_hd_invalidate();
}

typedef struct { char magic[4];uint32_t version,w,h,count,save_size,save_crc,payload_crc; } Header;
static uint32_t checksum(uint32_t crc,const void *data,size_t n) {
  const uint8_t *p=data;crc=~crc;while(n--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;
}
static int file_crc(const char *p,uint32_t *size,uint32_t *crc) {
  FILE *f=fopen(p,"rb");if(!f)return 0;uint8_t buf[8192];size_t n;*size=*crc=0;
  while((n=fread(buf,1,sizeof buf,f))){*size+=(uint32_t)n;*crc=checksum(*crc,buf,n);}
  int ok=!ferror(f);if(fclose(f))ok=0;return ok;
}
void path_styles_closed(int fd) { if(fd>=0&&fd<256)writing[fd]=0; }
void path_styles_save_field(int fd,int reading) {
  if(!reading){path_styles_frame();if(fd>=0&&fd<256)writing[fd]=1;return;}
  path_styles_reset();loading=1;const char *p=crt_real_path((unsigned)fd);if(!p)return;
  char side[640];snprintf(side,sizeof side,"%s.path-styles",p);FILE *f=fopen(side,"rb");if(!f)return;
  Header h;uint32_t size=0,crc=0;
  int ok=fread(&h,sizeof h,1,f)==1&&!memcmp(h.magic,"SGPS",4)&&h.version==1&&
    h.w==(unsigned)g_map_w&&h.h==(unsigned)g_map_h&&h.count==h.w*h.h&&h.count<=CAP;
  if(ok)ok=fread(staged,1,h.count,f)==h.count&&fgetc(f)==EOF&&!ferror(f);
  fclose(f);
  if(ok)ok=file_crc(p,&size,&crc)&&h.save_size==size&&h.save_crc==crc&&h.payload_crc==checksum(0,staged,h.count);
  for(unsigned i=0;ok&&i<h.count;i++)if(staged[i]>=PATH_STYLE_COUNT)ok=0;
  if(ok)memcpy(styles,staged,h.count);
  fprintf(stderr,"path-styles: %s save metadata\n",ok?"loaded":"rejected stale or invalid");
}
int path_styles_written(int fd,const char *p) {
  if(fd<0||fd>=256||!writing[fd])return 1;writing[fd]=0;
  Header h={{'S','G','P','S'},1,g_map_w,g_map_h,g_map_w*g_map_h,0,0,0};
  if(h.count>CAP||!file_crc(p,&h.save_size,&h.save_crc))return 0;
  h.payload_crc=checksum(0,styles,h.count);char side[640],temp[650];
  snprintf(side,sizeof side,"%s.path-styles",p);snprintf(temp,sizeof temp,"%s.tmp",side);
  FILE *f=fopen(temp,"wb");if(!f)return 0;
  int ok=fwrite(&h,sizeof h,1,f)==1&&fwrite(styles,1,h.count,f)==h.count;if(fclose(f))ok=0;
#ifdef _WIN32
  extern int __stdcall MoveFileExA(const char *,const char *,unsigned long);
  if(ok)ok=MoveFileExA(temp,side,1)!=0;
#else
  if(ok)ok=rename(temp,side)==0;
#endif
  if(!ok)remove(temp);
  fprintf(stderr,"path-styles: %s metadata\n",ok?"saved":"could not save");return ok;
}
