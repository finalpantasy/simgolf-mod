#include "port/ui/ui_course_map.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { SDL_Vertex *v; int n, capacity; } Mesh;
static void point(const UiCourseMap *m, UiRect r, float x, float y, float *px, float *py) {
  float span = (float)(m->width + m->height);
  *px = r.x + (x + y + 1) * r.w / span;
  *py = r.y + (y - x + m->width) * r.h / span;
}
static void triangle(Mesh *mesh, const UiCourseMap *m, UiRect r,
                     float ax, float ay, float bx, float by, float cx, float cy,
                     UiColor a, UiColor b, UiColor c) {
  if (mesh->n + 3 > mesh->capacity) {
    int capacity = mesh->capacity ? mesh->capacity * 2 : 4096;
    SDL_Vertex *next = realloc(mesh->v, (size_t)capacity * sizeof *next);
    if (!next) return;
    mesh->v = next; mesh->capacity = capacity;
  }
  const float x[3] = {ax,bx,cx}, y[3] = {ay,by,cy};
  const UiColor color[3] = {a,b,c};
  for (int i = 0; i < 3; i++) {
    SDL_Vertex *v = &mesh->v[mesh->n++];
    point(m,r,x[i],y[i],&v->position.x,&v->position.y);
    v->color = (SDL_Color){color[i].r,color[i].g,color[i].b,color[i].a};
    v->tex_coord = (SDL_FPoint){0,0};
  }
}
static int category(int tile) {
  if (tile == 0 || tile == 1) return 3;
  if (tile == 2 || tile == 3) return 2;
  if (tile >= 7 && tile <= 9) return 4;
  if (tile == 10 || tile == 12) return 5;
  if (tile == 11 || (tile >= 13 && tile <= 16)) return 6;
  if (tile >= 17 && tile <= 19) return 7;
  if (tile == 21 || tile == 22) return 8;
  if (tile == 20 || tile > 22) return 0;
  return 1;
}
static int inside(const UiCourseMap *m, int x, int y, int layer) {
  if (x < 0 || y < 0 || x >= m->width || y >= m->height) return 0;
  int i = x*m->height+y, cat = category(m->terrain[i]);
  if (layer == 1) return cat != 0;
  if (layer == 9) return m->path[i] != 0 && cat != 0;
  return cat == layer;
}
static void polygon(Mesh *mesh, const UiCourseMap *m, UiRect r, float x, float y, float step,
                    const float value[4], float threshold, const int *ids, int n, UiColor color) {
  float edge[4];
  for(int i=0;i<4;i++) {
    float delta=value[(i+1)%4]-value[i];
    edge[i]=fabsf(delta)>.00001f?(threshold-value[i])/delta:.5f;
    edge[i]=fmaxf(0,fminf(1,edge[i]));
  }
  const float px[8] = {0,1,1,0,edge[0],1,1-edge[2],0};
  const float py[8] = {0,0,1,1,0,edge[1],1,1-edge[3]};
  for (int i=1;i+1<n;i++) triangle(mesh,m,r,x+px[ids[0]]*step,y+py[ids[0]]*step,
    x+px[ids[i]]*step,y+py[ids[i]]*step,x+px[ids[i+1]]*step,y+py[ids[i+1]]*step,color,color,color);
}
// Marching squares joins adjacent areas at half-tile boundaries. This avoids
// enlarging the old stair-step raster, and keeps isolated sand/water separate.
static void contours(Mesh *mesh, const UiCourseMap *m, UiRect r, int layer, UiColor color) {
  static const int polygons[16][7] = {
    {0}, {3,0,4,7}, {3,1,5,4}, {4,0,1,5,7},
    {3,2,6,5}, {0}, {4,1,2,6,4}, {5,0,1,2,6,7},
    {3,3,7,6}, {4,0,4,6,3}, {0}, {5,0,1,5,6,3},
    {4,2,3,7,5}, {5,0,4,5,2,3}, {5,4,1,2,3,7}, {4,0,1,2,3}
  };
  int stride=m->height+2;
  float *field=malloc((size_t)(m->width+2)*stride*sizeof *field);
  if(!field)return;
  for(int x=-1;x<=m->width;x++)for(int y=-1;y<=m->height;y++)
    field[(x+1)*stride+y+1]=.65f*inside(m,x,y,layer)+.0875f*(inside(m,x-1,y,layer)+
      inside(m,x+1,y,layer)+inside(m,x,y-1,layer)+inside(m,x,y+1,layer));
  float threshold=layer==9?.6f:.5f;
  int quality=m->width<=70?4:2;
  for (int x=-1;x<m->width;x++) for (int y=-1;y<m->height;y++) {
    float f[4]={field[(x+1)*stride+y+1],field[(x+2)*stride+y+1],
                field[(x+2)*stride+y+2],field[(x+1)*stride+y+2]};
    int outer=(f[0]>=threshold)|((f[1]>=threshold)<<1)|((f[2]>=threshold)<<2)|((f[3]>=threshold)<<3);
    if(!outer)continue;
    if(outer==15) { polygon(mesh,m,r,(float)x,(float)y,1,f,threshold,polygons[15]+1,4,color);continue; }
    for(int sx=0;sx<quality;sx++)for(int sy=0;sy<quality;sy++) {
      float step=1.0f/quality,value[4];
      for(int i=0;i<4;i++) {
        float u=(sx+(i==1||i==2))*step,v=(sy+(i>=2))*step;
        value[i]=(1-v)*(f[0]*(1-u)+f[1]*u)+v*(f[3]*(1-u)+f[2]*u);
      }
      int mask=(value[0]>=threshold)|((value[1]>=threshold)<<1)|((value[2]>=threshold)<<2)|((value[3]>=threshold)<<3);
      float ax=x+sx*step,ay=y+sy*step;
      if(mask==5||mask==10) {
        const int a[3]={mask==5?0:1,mask==5?4:5,mask==5?7:4};
        const int b[3]={mask==5?2:3,mask==5?6:7,mask==5?5:6};
        polygon(mesh,m,r,ax,ay,step,value,threshold,a,3,color);
        polygon(mesh,m,r,ax,ay,step,value,threshold,b,3,color);
      } else if(mask)polygon(mesh,m,r,ax,ay,step,value,threshold,polygons[mask]+1,polygons[mask][0],color);
    }
  }
  free(field);
}
static UiColor heat_color(const UiCourseMap *m, int x, int y) {
  if (x<0) x=0; if (y<0)y=0;
  if (x>=m->width)x=m->width-1; if (y>=m->height)y=m->height-1;
  int i=x*m->height+y, cat=category(m->terrain[i]);
  if (!cat) return (UiColor){219,226,210,255};
  if (cat==7) return (UiColor){93,164,179,255};
  uint16_t v=m->heat[i];
  float rr=((v>>10)&31)/31.0f, gg=((v>>5)&31)/31.0f, bb=(v&31)/31.0f;
  if (m->mode==2) {
    if (rr>.2f && gg<.05f) return (UiColor){205,187,168,255};
    return uid_mix((UiColor){222,225,195,255},(UiColor){58,151,108,255},gg);
  }
  // Preserve the game's red/green/blue meaning with a quieter map palette.
  UiColor raw={(uint8_t)(rr*255),(uint8_t)(gg*255),(uint8_t)(bb*255),255};
  return uid_mix((UiColor){169,195,165,255},raw,.55f);
}
static UiColor corner_color(const UiCourseMap *m,int x,int y) {
  UiColor a=heat_color(m,x,y),b=heat_color(m,x-1,y),c=heat_color(m,x,y-1),d=heat_color(m,x-1,y-1);
  return (UiColor){(uint8_t)((a.r+b.r+c.r+d.r)/4),(uint8_t)((a.g+b.g+c.g+d.g)/4),(uint8_t)((a.b+b.b+c.b+d.b)/4),255};
}
static void heat_mesh(Mesh *mesh,const UiCourseMap *m,UiRect r) {
  for(int x=0;x<m->width;x++)for(int y=0;y<m->height;y++) {
    UiColor a=corner_color(m,x,y),b=corner_color(m,x+1,y),c=corner_color(m,x+1,y+1),d=corner_color(m,x,y+1);
    triangle(mesh,m,r,x-.5f,y-.5f,x+.5f,y-.5f,x+.5f,y+.5f,a,b,c);
    triangle(mesh,m,r,x-.5f,y-.5f,x+.5f,y+.5f,x-.5f,y+.5f,a,c,d);
  }
}
static uint32_t map_hash(const UiCourseMap *m) {
  uint32_t h=2166136261u;
  const unsigned char *p=(const unsigned char *)m;
  for(size_t i=0;i<sizeof *m;i++)h=(h^p[i])*16777619u;
  return h;
}
static SDL_Texture *cache_texture;
static SDL_Renderer *cache_renderer;
static uint32_t cache_hash;
static int cache_width,cache_height;
void ui_course_map_clear(SDL_Renderer *renderer) {
  if(renderer!=cache_renderer)return;
  if(cache_texture)SDL_DestroyTexture(cache_texture);
  cache_texture=NULL;cache_renderer=NULL;cache_hash=0;
}
static SDL_Texture *map_texture(UiCtx *c,const UiCourseMap *m,int width,int height) {
  SDL_Renderer *r=ui_drawer(c)->r;
  uint32_t next=map_hash(m);
  if(cache_texture && cache_renderer==r && cache_hash==next && cache_width==width && cache_height==height)return cache_texture;
  if(cache_renderer && cache_renderer!=r)ui_course_map_clear(cache_renderer);
  cache_renderer=r;
  SDL_Texture *target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width,height);
  if(!target)return NULL;
  SDL_Texture *previous=SDL_GetRenderTarget(r);
  SDL_Rect viewport,clip; SDL_RenderGetViewport(r,&viewport); SDL_RenderGetClipRect(r,&clip);
  SDL_bool clipped=SDL_RenderIsClipEnabled(r);
  Uint8 red,green,blue,alpha; SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
  SDL_BlendMode blend;SDL_GetRenderDrawBlendMode(r,&blend);
  SDL_SetRenderTarget(r,target);SDL_RenderSetViewport(r,NULL);SDL_RenderSetClipRect(r,NULL);
  SDL_SetRenderDrawColor(r,0,0,0,0);SDL_RenderClear(r);SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
  UiRect area={4,4,(float)width-8,(float)height-8};
  Mesh mesh={0};
  UiColor base={217,224,204,255};
  triangle(&mesh,m,area,-.5f,-.5f,m->width-.5f,-.5f,m->width-.5f,m->height-.5f,base,base,base);
  triangle(&mesh,m,area,-.5f,-.5f,m->width-.5f,m->height-.5f,-.5f,m->height-.5f,base,base,base);
  if(m->mode>=1)heat_mesh(&mesh,m,area);
  else {
    static const UiColor palette[10]={{0},{126,168,115,255},{185,210,136,255},{104,184,133,255},
      {234,215,169,255},{149,157,140,255},{57,112,85,255},{94,165,179,255},{126,146,126,255},{219,220,194,255}};
    for(int layer=1;layer<=9;layer++)contours(&mesh,m,area,layer,palette[layer]);
  }
  if(mesh.n)SDL_RenderGeometry(r,NULL,mesh.v,mesh.n,NULL,0);
  if(getenv("SIMGOLF_REPORTS_LOG"))fprintf(stderr,"map: native mesh mode %d grid %dx%d target %dx%d vertices %d\n",m->mode,m->width,m->height,width,height,mesh.n);
  free(mesh.v);
  SDL_SetRenderTarget(r,previous);SDL_RenderSetViewport(r,&viewport);
  SDL_RenderSetClipRect(r,clipped?&clip:NULL);SDL_SetRenderDrawColor(r,red,green,blue,alpha);SDL_SetRenderDrawBlendMode(r,blend);
  SDL_SetTextureBlendMode(target,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(target,SDL_ScaleModeLinear);
  if(cache_texture)SDL_DestroyTexture(cache_texture);
  cache_texture=target;cache_hash=next;cache_width=width;cache_height=height;
  return cache_texture;
}
void ui_course_map_draw(UiCtx *c,const UiCourseMap *m,UiRect bounds) {
  const UiTokens *t=ui_tokens(c);
  if(!m->valid || m->width<1 || m->height<1 || m->width>UI_MAP_SIDE || m->height>UI_MAP_SIDE) {
    ui_text_in(c,bounds,UI_TYPE_CAPTION,t->text_secondary,"Map unavailable",.5f);return;
  }
  UiRect fit=ui_inset_all(bounds,ui_s(c,18));
  float w=fminf(fit.w,fit.h*2),h=w/2;
  UiRect area=ui_center(fit,w,h);
  // Render the actual shapes at twice the output resolution for clean edges.
  SDL_Texture *texture=map_texture(c,m,(int)ceilf(w*2),(int)ceilf(h*2));
  if(texture)uid_image(ui_drawer(c),texture,NULL,area,0,(UiColor){255,255,255,255});
  UiDraw *d=ui_drawer(c);
  // Draw every route before every badge, so another hole's line cannot cross
  // out a number that has already been painted.
  for(int i=0;i<18;i++) {
    if(!m->hole[i].active || m->hole[i].n<2)continue;
    int n=m->hole[i].n;
    for(int j=1;j<n;j++) {
      float ax,ay,bx,by;
      point(m,area,m->hole[i].x[j-1],m->hole[i].y[j-1],&ax,&ay);
      point(m,area,m->hole[i].x[j],m->hole[i].y[j],&bx,&by);
      uid_line(d,ax,ay,bx,by,ui_s(c,3),uid_alpha((UiColor){23,58,45,255},.65f));
      uid_line(d,ax,ay,bx,by,ui_s(c,1.3f),(UiColor){247,252,239,255});
    }
  }
  UiRect label[18];int labels=0;
  float radius=ui_s(c,11);
  for(int i=0;i<18;i++) {
    if(!m->hole[i].active || m->hole[i].n<2)continue;
    int n=m->hole[i].n;
    float x,y;point(m,area,m->hole[i].x[n-1],m->hole[i].y[n-1],&x,&y);
    uid_rrect(d,ui_rect(x-ui_s(c,3),y-ui_s(c,3),ui_s(c,6),ui_s(c,6)),ui_s(c,3),(UiColor){250,253,245,255});
    float lx=x+radius,ly=y-radius;
    for(int attempt=0;attempt<24;attempt++) {
      int overlap=0;
      for(int j=0;j<labels;j++)if(fabsf(lx-label[j].x)<2*radius && fabsf(ly-label[j].y)<2*radius){overlap=1;break;}
      if(!overlap)break;
      float angle=attempt*.85f,reach=radius*(1.3f+attempt*.15f);
      lx=x+cosf(angle)*reach;ly=y+sinf(angle)*reach;
    }
    label[labels++]=(UiRect){lx,ly,0,0};
    uid_line(d,x,y,lx,ly,ui_s(c,1),(UiColor){245,250,236,255});
    UiRect badge=ui_rect(lx-radius,ly-radius,2*radius,2*radius);
    uid_rrect(d,badge,radius,(UiColor){244,250,237,245});
    char text[4];snprintf(text,sizeof text,"%d",i+1);
    ui_text_in(c,badge,UI_TYPE_CAPTION,(UiColor){24,69,47,255},text,.5f);
  }
  UiRect north=ui_rect(bounds.x+ui_s(c,6),bounds.y+bounds.h-ui_s(c,38),ui_s(c,22),ui_s(c,22));
  ui_icon(c,UI_ICON_UP,north,ui_s(c,18),t->text_secondary);
  ui_text_in(c,ui_rect(north.x-ui_s(c,2),north.y-ui_s(c,18),north.w+ui_s(c,4),ui_s(c,18)),UI_TYPE_MICRO,t->text_secondary,"N",.5f);
}
