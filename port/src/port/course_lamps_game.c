// Game-thread adapter. Original names/addresses: worldToScreen 0042fb90,
// screenToWorld 00430020, g_money_div100 00571fd4, calendar year 005a6d3c.
#include "port/course_lamps_native.h"
#include "port/grid.h"
#include "port/host.h"
#include "port/release_data.h"
#include "port/runtime.h"
#include "port/camera_tiles.h"
#include "port/golf/hd_ui.h"
#include "mods/mods.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile int16_t *)(uintptr_t)(a))
uint32_t FUN_0042fb90(int,int,int *,int *,int);
int *FUN_00430020(int,int,int *,int *,int);
int FUN_0040c890(int,int,int,int);
static LampState state;
static LampStyle styles[LAMP_STYLES];
static LampFrame frame;
static int ready,selected=-1,mouse_x=-1,mouse_y=-1;
static uint8_t writing[256];
static int preserve_error,extension_present;
const LampStyle *lamps_game_styles(void){if(!ready){char path[1024];release_data_path(path,sizeof path,port_host_path("/ui/../data"),"course-lights.csv");ready=lamps_load_styles(styles,path)?1:-1;}return styles;}
int lamps_game_enabled(void){lamps_game_styles();return ready==1&&mods_get_int("course_lights.enabled",0);}
int lamps_game_selected(void){return lamps_game_enabled()?selected:-1;}
void lamps_game_select(int style){if(!lamps_game_enabled())return;selected=style>=0&&style<=LAMP_STYLES?style:-1;fprintf(stderr,"course lights: selected %d\n",selected);}
void lamps_game_reset(void){memset(&state,0,sizeof state);memset(&frame,0,sizeof frame);selected=-1;preserve_error=0;extension_present=0;}
void lamps_game_save_field(int fd,int reading){
  if(fd<0||fd>=256)return;
  if(!reading){writing[fd]=1;return;}
  lamps_game_reset();const char *path=crt_real_path((unsigned)fd);if(!path)return;
  int r=lamps_load(&state,path,MAP_W,MAP_H);preserve_error=r<0;extension_present=r!=0;
  fprintf(stderr,"course lights: load %d count %d, optional rules v2 (free positions, no collision, installed price, no income, no salvage)\n",r,state.count);
}
int lamps_game_written(int fd,const char *path){
  if(fd<0||fd>=256||!writing[fd])return 1;writing[fd]=0;
  if(!extension_present&&!state.count&&!lamps_game_enabled())return 1;
  // Do not silently overwrite an unreadable extension. Player's existing sidecar stays recoverable.
  if(preserve_error){fprintf(stderr,"course lights: unreadable extension kept, save extension NOT written\n");return 0;}
  int ok=lamps_save(&state,path,MAP_W,MAP_H);if(ok)extension_present=1;fprintf(stderr,"course lights: save %s count %d\n",ok?"ok":"FAILED",state.count);return ok;
}
void lamps_game_closed(int fd){if(fd>=0&&fd<256)writing[fd]=0;}
static int allowed(int row,int col){
  // Decorative no-clip placement never writes the terrain, occupancy, navigation
  // or ball-collision arrays. Existing objects and surface type impose no limit.
  return row>=1&&col>=1&&row<MAP_H-1&&col<MAP_W-1;
}
static int spacing(void){int n=mods_get_int("course_lights.spacing_units",128);return n<0?0:n>1024?1024:n;}
static int pick(int x,int y,int *row,int *col,int *wr,int *wc){
  if(hd_ui_zoom_map_hook)hd_ui_zoom_map_hook(&x,&y);
  FUN_00430020(x,y,row,col,0);*wr=*wc=-1;if(!allowed(*row,*col))return 0;
  double r=*row*1024+512,c=*col*1024+512;
  // Refine the original terrain picker using worldToScreen (0042fb90).
  // Its terrain heights, camera rotation, tier scaling and sub-tile offsets
  // remain authoritative. No flat-ground inverse or tile-centre snapping.
  for(int i=0;i<8;i++){
    int px,py,rx,ry,cx,cy;int ir=(int)lround(r),ic=(int)lround(c);
    if(!allowed(ir/1024,ic/1024))return 0;
    int dr=ir+256<(MAP_H-1)*1024?256:-256,dc=ic+256<(MAP_W-1)*1024?256:-256;
    FUN_0042fb90(ir,ic,&px,&py,4000);if(abs(x-px)<=1&&abs(y-py)<=1)break;
    FUN_0042fb90(ir+dr,ic,&rx,&ry,4000);FUN_0042fb90(ir,ic+dc,&cx,&cy,4000);
    double ax=(rx-px)/(double)dr,ay=(ry-py)/(double)dr,bx=(cx-px)/(double)dc,by=(cy-py)/(double)dc;
    double det=ax*by-ay*bx;if(fabs(det)<1e-8)return 0;
    double rr=((x-px)*by-(y-py)*bx)/det,cc=(ax*(y-py)-ay*(x-px))/det;
    r+=fmax(-512,fmin(512,rr));c+=fmax(-512,fmin(512,cc));
  }
  *wr=(int)lround(r);*wc=(int)lround(c);*row=*wr/1024;*col=*wc/1024;
  int px,py;if(!allowed(*row,*col))return 0;FUN_0042fb90(*wr,*wc,&px,&py,4000);
  return abs(x-px)<=2&&abs(y-py)<=2;
}
int lamps_game_probe(int x,int y,int *row,int *col,int *occupied,int *count,int *wr,int *wc){
  *row=*col=*wr=*wc=-1;*occupied=-1;*count=state.count;
  if(!lamps_game_enabled())return 0;
  int ok=pick(x,y,row,col,wr,wc);if(ok)*occupied=lamps_nearest(&state,*wr,*wc,256);return ok;
}
int lamps_game_event(SDL_Event *e){
  if(!lamps_game_enabled()||selected<0)return 0;
  if(e->type==SDL_KEYDOWN&&e->key.keysym.sym==SDLK_ESCAPE){selected=-1;return 1;}
  if(e->type==SDL_MOUSEMOTION){mouse_x=e->motion.x;mouse_y=e->motion.y;return 0;}
  if(e->type!=SDL_MOUSEBUTTONDOWN&&e->type!=SDL_MOUSEBUTTONUP)return 0;
  if(e->button.button==SDL_BUTTON_RIGHT){if(e->type==SDL_MOUSEBUTTONDOWN)selected=-1;return 1;}
  if(e->button.button!=SDL_BUTTON_LEFT)return 0;
  if(e->type==SDL_MOUSEBUTTONUP)return 1;
  mouse_x=e->button.x;mouse_y=e->button.y;int row=-1,col=-1,wr=-1,wc=-1;
  int valid_pick=pick(mouse_x,mouse_y,&row,&col,&wr,&wc);
  if(preserve_error){fprintf(stderr,"course lights: placement disabled until unreadable save extension is recovered\n");return 1;}
  if(!valid_pick)return 1;
  if(selected==LAMP_STYLES){int ok=lamps_remove_at(&state,wr,wc,256);fprintf(stderr,"course lights: remove %d tile %d,%d count %d refund 0\n",ok,row,col,state.count);return 1;}
  int money=G32(0x571fd4),old=money,sandbox=(G32(0x59e7b8)&0x1000000)!=0;
  int ok=lamps_place_at(&state,styles,selected,wr,wc,MAP_W,MAP_H,spacing(),&money,sandbox);
  if(ok){G32(0x571fd4)=money;int year=G16(0x5a6d3c);if(year<0)year=0;int delta=old-money;
    // Same signed facilities category and year ring as original facility purchases.
    volatile int16_t *ledger=(volatile int16_t *)(uintptr_t)(0x584218+(year%100)*20);
    *ledger=(int16_t)((uint16_t)*ledger-(uint16_t)delta);
    if(delta)FUN_0040c890(-delta,wr,wc,-1);
  }
  fprintf(stderr,"course lights: place %d style %d tile %d,%d world %d,%d count %d cash %d -> %d sandbox %d\n",ok,selected,row,col,wr,wc,state.count,old,money,sandbox);return 1;
}
void lamps_game_course_done(void){
  memset(&frame,0,sizeof frame);frame.enabled=lamps_game_enabled();frame.selected=selected;
  if(!frame.enabled)return;frame.zoom=G32(0x4c2844);frame.rotation=(4-G32(0x5685f4)/2)&3;memcpy(frame.styles,styles,sizeof styles);
  for(int i=0;i<state.count;i++){
    const CourseLamp *p=&state.lamps[i];int x=0,y=0;
    // Free decorative positions remain present when the underlying course changes.
    if(!allowed(p->row,p->col))continue;
    FUN_0042fb90(lamps_world_row(p),lamps_world_col(p),&x,&y,4000);
    frame.points[frame.count++]=(LampPoint){(float)x,(float)y,p->style};
  }
  if(selected>=0&&mouse_x>=0){int row,col,wr,wc,x=0,y=0;
    if(pick(mouse_x,mouse_y,&row,&col,&wr,&wc)){FUN_0042fb90(wr,wc,&x,&y,4000);
      frame.hover_x=(float)x;frame.hover_y=(float)y;frame.hover_style=selected;
      frame.hover_valid=selected==LAMP_STYLES?lamps_nearest(&state,wr,wc,256)>=0:lamps_nearest(&state,wr,wc,spacing())<0&&state.count<LAMP_LIMIT&&((G32(0x59e7b8)&0x1000000)||G32(0x571fd4)>=styles[selected].cost);
    }
  }
}
void lamps_game_frame(LampFrame *out){*out=frame;}
