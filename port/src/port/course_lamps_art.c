#include "port/course_lamps.h"
#include <string.h>
const char *lamps_art_id(int style){
  static const char *ids[LAMP_STYLES]={"traditional","modern","resort","rustic","bollard","floodlight"};
  return style>=0&&style<LAMP_STYLES?ids[style]:NULL;
}
static _Thread_local uint32_t sprites[LAMP_STYLES][32*64];
static void box(int s,int x,int y,int w,int h,uint32_t c){for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(i>=0&&i<32&&j>=0&&j<64)sprites[s][j*32+i]=0xff000000|c;}
static void triangle(int s,int cx,int y,int half,int height,uint32_t c){for(int j=0;j<height;j++){int width=(half*j)/height;box(s,cx-width,y+j,width*2+1,1,c);}}
static void make_sprites(void){
  memset(sprites,0,sizeof sprites);
  for(int s=0;s<LAMP_STYLES;s++){
    int top=s==4?47:s==5?3:s==2||s==3?19:16;
    uint32_t body=s==3?0x715344:s==2?0x81664c:0x414d50;
    box(s,12,59,9,3,0x313e3c);box(s,14,top+9,4,50-top,body);box(s,14,top+10,1,48-top,s==3?0xb19362:0x8e9c91);
    if(s==4){box(s,11,47,10,5,0x333c3d);box(s,12,49,8,3,0xffdda1);box(s,10,46,12,2,0x657368);}
    else if(s==5){box(s,7,4,19,9,0x303b43);box(s,9,5,15,6,0xdbe6e8);box(s,12,11,8,3,0x485b60);box(s,10,6,5,3,0xf3f5df);box(s,19,6,4,3,0xf3f5df);}
    else if(s==1){box(s,14,top,4,9,body);box(s,14,top,12,3,0x67777b);box(s,19,top+3,7,2,0xe7f2f1);}
    else {
      box(s,11,top,10,11,body);box(s,12,top+2,8,7,0xe5bd79);box(s,14,top+2,2,6,0xffe9b9);
      box(s,15,top+1,1,9,body);box(s,10,top+10,12,2,body);
      triangle(s,16,top-6,s==2?9:7,6,s==2?0x997545:0x414a48);box(s,8,top-1,17,2,body);box(s,15,top-7,2,2,0x52615a);
      if(s==3){box(s,12,top+11,1,30,0xc0a777);box(s,18,top+11,2,9,0x43392d);}
    }
  }
}

void lamps_sprite_rgba(int style,uint32_t out[32*64]){make_sprites();if(style>=0&&style<LAMP_STYLES)memcpy(out,sprites[style],32*64*4);}
