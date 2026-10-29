// Inject transient resource failures into the production glyph renderer.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/ui/ui_text.h"
static int fail_malloc, fail_alpha, fail_create, fail_update, fail_draw, updates;
static size_t allocation_limit;
static void *test_malloc(size_t n) {
  if(fail_malloc || (fail_alpha && n==2048u*2048u) || (allocation_limit && n>allocation_limit))return NULL;
  return malloc(n);
}
static SDL_Texture *test_create(SDL_Renderer *r,Uint32 f,int access,int w,int h) {
  return fail_create?NULL:SDL_CreateTexture(r,f,access,w,h);
}
static int test_update(SDL_Texture *t,const SDL_Rect *r,const void *p,int pitch) {
  updates++;
  return fail_update && (fail_update<0 || updates==fail_update)?-1:SDL_UpdateTexture(t,r,p,pitch);
}
static int test_draw(SDL_Renderer *r,SDL_Texture *t,const SDL_Vertex *v,int nv,const int *ix,int ni) {
  return fail_draw?-1:SDL_RenderGeometry(r,t,v,nv,ix,ni);
}
#define malloc test_malloc
#define SDL_CreateTexture test_create
#define SDL_UpdateTexture test_update
#define SDL_RenderGeometry test_draw
#include "../src/port/ui/ui_text.c"
#undef SDL_RenderGeometry
#undef SDL_UpdateTexture
#undef SDL_CreateTexture
#undef malloc
static int checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
int main(int argc,char **argv) {
  if(argc!=2)return 3;
  CHECK(SDL_Init(0)==0);
  SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,640,80,32,SDL_PIXELFORMAT_ARGB8888);
  SDL_Renderer *r=SDL_CreateSoftwareRenderer(s);CHECK(s&&r);if(!r)return 3;
  unsigned char reference[640*80*4],actual[640*80*4];
  for(int mode=0;mode<9;mode++) {
    UiText *t=calloc(1,sizeof *t);
    fail_malloc=mode==1;fail_alpha=mode==2;fail_create=mode==4;
    uit_init(t,r,argv[1],1);
    fail_malloc=fail_alpha=fail_create=0;
    if(mode==1 || mode==2 || mode==4)SDL_Delay(1100);
    allocation_limit=mode==3?512u*1024u:0;
    SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
    updates=0;fail_update=mode==5?1:mode==6?2:mode==8?-1:0;fail_draw=mode==7;
    uit_draw(t,5,50,600,24,"I.M. Picky: Thirst Hunger Energy Attitude",0,(UiColor){255,255,255,255});
    if(mode==8){uit_draw(t,5,50,600,24,"I.M. Picky: Thirst Hunger Energy Attitude",0,(UiColor){255,255,255,255});CHECK(!t->atlas);}
    fail_update=fail_draw=0;
    if(mode>=5){CHECK(t->dirty);SDL_Delay(1100);SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
      uit_draw(t,5,50,600,24,"I.M. Picky: Thirst Hunger Energy Attitude",0,(UiColor){255,255,255,255});}
    SDL_RenderPresent(r);SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_ARGB8888,actual,640*4);
    int lit=0;for(int i=0;i<640*80;i++)if(((Uint32*)actual)[i]&0xffffff)lit++;
    CHECK(lit>100);CHECK(!t->dirty);
    if(!mode)memcpy(reference,actual,sizeof reference);
    else CHECK(!memcmp(reference,actual,sizeof reference));
    allocation_limit=0;uit_free(t);free(t);
  }
  SDL_DestroyRenderer(r);SDL_FreeSurface(s);SDL_Quit();
  printf("Text resource recovery: %d checks, %d failures\n",checks,failures);
  return failures?1:0;
}
