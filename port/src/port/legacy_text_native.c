// Rasterize the recorded original font/glyph at the final display size.
#include "port/legacy_text_native.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define CACHE 2048
typedef struct {
  const unsigned char *font;
  int index, size, w, h, ox, oy;
  SDL_Texture *texture;
} Glyph;
struct LegacyRenderer { SDL_Renderer *r; Glyph glyph[CACHE]; int next; };
LegacyRenderer *legacy_text_create(SDL_Renderer *r) {
  LegacyRenderer *t=calloc(1,sizeof *t);
  if(t)t->r=r;
  return t;
}
static Glyph *glyph(LegacyRenderer *t,const unsigned char *font,int index,float em) {
  int size=(int)lroundf(em*64);
  if(size<1 || size>1024*64)return NULL;
  for(int i=0;i<CACHE;i++) {
    Glyph *g=t->glyph+i;
    if(g->font==font && g->index==index && g->size==size)return g;
  }
  stbtt_fontinfo info;
  if(!stbtt_InitFont(&info,font,stbtt_GetFontOffsetForIndex(font,0)))return NULL;
  Glyph *g=t->glyph+(t->next++%CACHE);
  if(g->texture)SDL_DestroyTexture(g->texture);
  memset(g,0,sizeof *g);
  float scale=stbtt_ScaleForMappingEmToPixels(&info,size/64.f);
  unsigned char *alpha=stbtt_GetGlyphBitmap(&info,scale,scale,index,&g->w,&g->h,&g->ox,&g->oy);
  if(alpha && g->w>0 && g->h>0) {
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,g->w,g->h,32,SDL_PIXELFORMAT_ARGB8888);
    if(s) {
      for(int y=0;y<g->h;y++)for(int x=0;x<g->w;x++)
        ((uint32_t*)((char*)s->pixels+y*s->pitch))[x]=0x00ffffffu|((uint32_t)alpha[y*g->w+x]<<24);
      g->texture=SDL_CreateTextureFromSurface(t->r,s);
      SDL_FreeSurface(s);
      if(g->texture)SDL_SetTextureBlendMode(g->texture,SDL_BLENDMODE_BLEND);
    }
  }
  stbtt_FreeBitmap(alpha,NULL);
  // A failed upload is retried. Spaces legitimately have no raster.
  if(!g->w || !g->h || g->texture) {g->font=font;g->index=index;g->size=size;}
  return g;
}
void legacy_text_draw(LegacyRenderer *t,const LegacyText *text,float x,float y,float scale,SDL_Rect bounds) {
  if(!t || !text || scale<=0)return;
  SDL_Rect saved;int clipped=SDL_RenderIsClipEnabled(t->r);
  SDL_RenderGetClipRect(t->r,&saved);
  for(int i=text->count-1;i>=0;i--) { // captured newest first, replay oldest first
    const LegacyRun *r=text->run+i;
    SDL_Rect rc={(int)floorf(x+r->clip[0]*scale),(int)floorf(y+r->clip[1]*scale),
      (int)ceilf((r->clip[2]-r->clip[0])*scale),(int)ceilf((r->clip[3]-r->clip[1])*scale)},clip;
    if(!SDL_IntersectRect(&bounds,&rc,&clip))continue;
    if(clipped && !SDL_IntersectRect(&clip,&saved,&clip))continue;
    SDL_RenderSetClipRect(t->r,&clip);
    Uint8 red=r->rgb&255,green=(r->rgb>>8)&255,blue=(r->rgb>>16)&255;
    for(int k=0;k<r->count;k++) {
      const LegacyGlyph *p=text->glyph+r->first+k;
      Glyph *g=glyph(t,r->font,p->index,r->em*scale);
      if(!g || !g->texture)continue;
      SDL_SetTextureColorMod(g->texture,red,green,blue);
      SDL_FRect d={x+p->x*scale+g->ox,y+p->y*scale+g->oy,(float)g->w,(float)g->h};
      SDL_RenderCopyF(t->r,g->texture,NULL,&d);
      if(r->bold) { d.x+=scale;SDL_RenderCopyF(t->r,g->texture,NULL,&d); }
    }
    if(r->underline) {
      SDL_SetRenderDrawBlendMode(t->r,SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(t->r,red,green,blue,255);
      SDL_FRect line={x+r->uw0*scale,y+r->ubase*scale,(r->uw1-r->uw0)*scale,fmaxf(1,scale)};
      SDL_RenderFillRectF(t->r,&line);
    }
  }
  SDL_RenderSetClipRect(t->r,clipped?&saved:NULL);
}
