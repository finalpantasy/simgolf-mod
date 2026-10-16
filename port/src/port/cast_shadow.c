// MIT, original C implementation. The design reference is documented separately.
#include "port/cast_shadow.h"
#include "port/light_transfer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void cast_shadow_project(double x, double y, double ay, double sx, double sy, double *ox, double *oy) {
  double height = ay > y ? ay - y : 0;
  *ox = x + height * sx;
  *oy = height > 0 ? ay + height * sy : y;
}

static void save_state(CastShadowRenderer *s, SDL_Renderer *r) {
  s->previous_target = SDL_GetRenderTarget(r);
  SDL_RenderGetViewport(r, &s->viewport);
  s->clipped = SDL_RenderIsClipEnabled(r);
  SDL_RenderGetClipRect(r, &s->clip);
  SDL_RenderGetScale(r, &s->scale_x, &s->scale_y);
  SDL_GetRenderDrawBlendMode(r, &s->blend);
  SDL_GetRenderDrawColor(r, &s->red, &s->green, &s->blue, &s->alpha);
}
static void restore_state(CastShadowRenderer *s) {
  SDL_Renderer *r = s->renderer;
  SDL_SetRenderTarget(r, s->previous_target);
  SDL_RenderSetScale(r, s->scale_x, s->scale_y);
  SDL_RenderSetViewport(r, &s->viewport);
  SDL_RenderSetClipRect(r, s->clipped ? &s->clip : NULL);
  SDL_SetRenderDrawBlendMode(r, s->blend);
  SDL_SetRenderDrawColor(r, s->red, s->green, s->blue, s->alpha);
}
void cast_shadow_destroy(CastShadowRenderer *s) {
  if (!s) return;
  if (s->active) { restore_state(s); s->active = 0; }
  if (s->masks) {
    for (int i = 0; i < CAST_SHADOW_CACHE_SLOTS; i++) {
      free(s->masks[i].slices);
      if (s->masks[i].silhouette) SDL_DestroyTexture(s->masks[i].silhouette);
    }
    free(s->masks);
  }
  if (s->target) SDL_DestroyTexture(s->target);
  free(s->software_pixels);
  if (s->disc) SDL_DestroyTexture(s->disc);
  memset(s, 0, sizeof *s);
}

int cast_shadow_begin(CastShadowRenderer *s, SDL_Renderer *r, SDL_Rect clip, double k,
                      int enabled, double sx, double sy, double opacity, double ground_aspect) {
  if (!enabled) { cast_shadow_destroy(s); return 0; }
  if (s->renderer && s->renderer != r) cast_shadow_destroy(s);
  if (s->active || !r || clip.w <= 0 || clip.h <= 0 || !isfinite(k) || k <= 0 ||
      !isfinite(sx) || !isfinite(sy) || !isfinite(opacity) || opacity <= 0) return 0;
  s->image_valid=0;s->pixel_scale=k;
  SDL_RendererInfo info;if(SDL_GetRendererInfo(r,&info)==0)s->software=(info.flags&SDL_RENDERER_SOFTWARE)!=0;
  // Limit low-sun reach and mask resolution, including pathological inputs.
  s->slope_x = fmax(-4, fmin(4, sx));
  s->slope_y = fmax(-4, fmin(4, sy));
  s->opacity = fmin(1, opacity);
  s->ground_aspect = isfinite(ground_aspect) ? fmax(0.1, fmin(1, ground_aspect)) : 0.625;
  s->factor = fmin(1 / k, fmin(2048.0 / clip.w, 2048.0 / clip.h));
  int w = (int)ceil(clip.w * s->factor), h = (int)ceil(clip.h * s->factor);
  if (w != s->width || h != s->height) {
    if (s->target) SDL_DestroyTexture(s->target);
    s->target = NULL;
    s->width = w; s->height = h;
  }
  if (!s->target) {
    s->target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!s->target) return 0;
    SDL_SetTextureBlendMode(s->target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(s->target, SDL_ScaleModeLinear);
  }
  if (!s->disc) {
    Uint8 pixels[64 * 256 * 4];
    const double feathers[4] = {0.06, 0.12, 0.24, 0.40};
    for (int band = 0; band < 4; band++) for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
      double radius = hypot((x - 31.5) / 31.5, (y - 31.5) / 31.5);
      Uint8 *p = pixels + ((band * 64 + y) * 64 + x) * 4;
      p[0] = p[1] = p[2] = 0;
      p[3] = (Uint8)lround(fmax(0, fmin(1, (1 - radius) / feathers[band])) * 255);
    }
    s->disc = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 64, 256);
    if (!s->disc) return 0;
    if (SDL_UpdateTexture(s->disc, NULL, pixels, 64 * 4) < 0) { SDL_DestroyTexture(s->disc); s->disc = NULL; return 0; }
    SDL_SetTextureBlendMode(s->disc, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(s->disc, SDL_ScaleModeLinear);
  }
  s->renderer = r;
  save_state(s, r);
  if (SDL_SetRenderTarget(r, s->target) < 0) { restore_state(s); return 0; }
  SDL_RenderSetViewport(r, NULL);
  SDL_RenderSetScale(r, 1, 1);
  SDL_RenderSetClipRect(r, NULL);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
  SDL_RenderClear(r);
  s->destination = clip;
  s->active = 1; s->tick++; s->builds = 0;
  return 1;
}

int cast_shadow_reuse(CastShadowRenderer *s,SDL_Renderer *r,SDL_Rect clip,double k,double opacity){
  if(!s||!s->image_valid||s->active||s->renderer!=r||!s->target||
     !isfinite(k)||k<=0||k!=s->pixel_scale||!isfinite(opacity)||opacity<=0||
     memcmp(&clip,&s->destination,sizeof clip))return 0;
  save_state(s,r);s->opacity=fmin(1,opacity);s->active=1;s->tick++;s->builds=0;
  return 1;
}

// Infer horizontal volume slices from alpha rows. The cache keeps at most
// 32 height bands and four disconnected silhouette spans per band. This is
// a bounded approximation of unseen depth, not a flat billboard or a stroke.
static CastShadowMask *outline(CastShadowRenderer *s, uint64_t key, SDL_Texture *body,
                                int w, int h) {
  if (!s->masks) s->masks = calloc(CAST_SHADOW_CACHE_SLOTS, sizeof *s->masks);
  if (!s->masks) return NULL;
  CastShadowMask *slot = NULL;
  for (int i = 0; i < CAST_SHADOW_CACHE_SLOTS; i++) {
    CastShadowMask *m = &s->masks[i];
    if (m->slices && m->key == key) { m->used = s->tick; return m; }
    if (!slot || !m->slices || (slot->slices && m->used < slot->used)) slot = m;
  }
  if (s->builds >= 4) return NULL;
  s->builds++;
  SDL_Renderer *r = s->renderer;
  SDL_Texture *capture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
  Uint8 *pixels = calloc((size_t)w * h, 4);
  SDL_Texture *silhouette = NULL;
  CastShadowSlice slices[128];
  int count = 0, captured = 0;
  if (!capture || !pixels) goto finish;
  if (SDL_SetRenderTarget(r, capture) < 0) goto finish;
  SDL_RenderSetViewport(r, NULL); SDL_RenderSetScale(r, 1, 1); SDL_RenderSetClipRect(r, NULL);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 0); SDL_RenderClear(r);
  Uint8 cr, cg, cb, ca;
  SDL_BlendMode blend;
  SDL_GetTextureColorMod(body, &cr, &cg, &cb); SDL_GetTextureAlphaMod(body, &ca); SDL_GetTextureBlendMode(body, &blend);
  SDL_SetTextureColorMod(body, 255, 255, 255); SDL_SetTextureAlphaMod(body, 255); SDL_SetTextureBlendMode(body, SDL_BLENDMODE_NONE);
  SDL_Rect dst = {0, 0, w, h};
  int ok = SDL_RenderCopy(r, body, NULL, &dst);
  SDL_SetTextureColorMod(body, cr, cg, cb); SDL_SetTextureAlphaMod(body, ca); SDL_SetTextureBlendMode(body, blend);
  if (ok < 0 || SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGBA32, pixels, w * 4) < 0) goto finish;
  captured = 1;
  int step = (h + 31) / 32;
  for (int y0 = 0; y0 < h; y0 += step) {
    // Select the richest row within each height band. This keeps narrow
    // conifer tiers and individual crown lobes from falling between samples.
    int row = y0, best = -1;
    for (int y = y0; y < h && y < y0 + step; y++) {
      int coverage = 0;
      for (int x = 0; x < w; x++) coverage += pixels[((size_t)y * w + x) * 4 + 3];
      if (coverage > best) { best = coverage; row = y; }
    }
    CastShadowSlice spans[4] = {{0}};
    int nspans = 0;
    for (int x = 0; x < w;) {
      if (pixels[((size_t)row * w + x) * 4 + 3] < 32) { x++; continue; }
      int left = x++, right = left;
      unsigned alpha = pixels[((size_t)row * w + left) * 4 + 3];
      while (x < w) {
        Uint8 a = pixels[((size_t)row * w + x) * 4 + 3];
        if (a < 32) break;
        alpha += a; right = x++;
      }
      int bottom = y0 + step < h ? y0 + step : h;
      CastShadowSlice span = {(left + right + 1) * 0.5f, (y0 + bottom) * 0.5f,
                              (right - left + 1) * 0.5f, alpha / (255.0f * (right - left + 1)), (float)(bottom - y0)};
      if (nspans < 4) spans[nspans++] = span;
      else {
        int smallest = 0;
        for (int j = 1; j < 4; j++) if (spans[j].radius < spans[smallest].radius) smallest = j;
        if (span.radius > spans[smallest].radius) spans[smallest] = span;
      }
    }
    for (int j = 0; j < nspans; j++) slices[count++] = spans[j];
  }
finish:
  SDL_SetRenderTarget(r, s->target);
  SDL_RenderSetViewport(r, NULL); SDL_RenderSetScale(r, 1, 1); SDL_RenderSetClipRect(r, NULL);
  if (capture) SDL_DestroyTexture(capture);
  if (captured && count) {
    // A small alpha penumbra retains the actual canopy gaps and branch shape.
    // Filter only the silhouette, after deriving the solid contact spans.
    Uint8 *alpha=malloc((size_t)w*h);
    if(alpha){
      for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        unsigned sum=4*pixels[((size_t)y*w+x)*4+3];
        if(x)sum+=pixels[((size_t)y*w+x-1)*4+3];
        if(x+1<w)sum+=pixels[((size_t)y*w+x+1)*4+3];
        if(y)sum+=pixels[((size_t)(y-1)*w+x)*4+3];
        if(y+1<h)sum+=pixels[((size_t)(y+1)*w+x)*4+3];
        alpha[(size_t)y*w+x]=(Uint8)((sum+4)/8);
      }
      for(int i=0;i<w*h;i++)pixels[i*4+3]=alpha[i];
      free(alpha);
    }
    // Retain every alpha pixel, including leaf notches and internal gaps.
    for (int i = 0; i < w * h; i++) pixels[i*4] = pixels[i*4+1] = pixels[i*4+2] = 0;
    silhouette = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (silhouette && SDL_UpdateTexture(silhouette, NULL, pixels, w * 4) < 0) {
      SDL_DestroyTexture(silhouette); silhouette = NULL;
    }
    if (silhouette) {
      SDL_SetTextureBlendMode(silhouette, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(silhouette, SDL_ScaleModeLinear);
    }
  }
  free(pixels);
  if (!silhouette) return NULL;
  size_t slice_bytes = (size_t)count * sizeof(CastShadowSlice);
  size_t bytes = slice_bytes + (size_t)w * h * 4;
  CastShadowSlice *copy = malloc(slice_bytes);
  if (!copy) { SDL_DestroyTexture(silhouette); return NULL; }
  memcpy(copy, slices, slice_bytes);
  while (s->bytes + bytes > CAST_SHADOW_CACHE_LIMIT) {
    CastShadowMask *old = NULL;
    for (int i = 0; i < CAST_SHADOW_CACHE_SLOTS; i++)
      if (s->masks[i].slices && (!old || s->masks[i].used < old->used)) old = &s->masks[i];
    if (!old) break;
    s->bytes -= old->bytes;
    SDL_DestroyTexture(old->silhouette); old->silhouette = NULL;
    free(old->slices); old->slices = NULL;
  }
  if (slot->slices) {
    s->bytes -= slot->bytes; free(slot->slices); SDL_DestroyTexture(slot->silhouette);
  }
  *slot = (CastShadowMask){copy, silhouette, bytes, key, s->tick, count};
  s->bytes += bytes;
  return slot;
}

int cast_shadow_draw(CastShadowRenderer *s, uint64_t key, SDL_Texture *body, int w, int h,
                     double ax, double ay, SDL_FRect d, int flip) {
  if (!s->active || !body || w <= 0 || h <= 0 || w > 512 || h > 512 ||
      !isfinite(ax) || !isfinite(ay) || !isfinite(d.x) || !isfinite(d.y) ||
      !isfinite(d.w) || !isfinite(d.h) || d.w <= 0 || d.h <= 0) return 0;
  CastShadowMask *m = outline(s, key, body, w, h);
  if (!m) return 0;
  double fx = d.w / w, fy = d.h / h;
  double anchor = (flip & 2 ? h - ay : ay) * fy;
  double length = hypot(s->slope_x, s->slope_y / s->ground_aspect);
  // A sun-facing vertical silhouette keeps real canopy detail and never
  // collapses when sunlight aligns with screen X. Only near overhead light
  // needs inferred depth, where a side silhouette contains no depth evidence.
  double detail = fmax(0, fmin(1, (length - 0.2) / 0.2));
  if (detail > 0) {
    double side_x = -s->slope_y / (s->ground_aspect * length);
    double side_y = s->slope_x * s->ground_aspect / length;
    double width_scale = fmax(fx, 2 / (w * s->factor * s->ground_aspect));
    double foot_x = d.x + (flip & 1 ? w - ax : ax) * fx;
    double foot_y = d.y + anchor;
    SDL_Vertex quad[12];
    for(int tap=0;tap<(s->softness>0?5:1);tap++){
    // A center fan also avoids SDL software's axis-aligned quad fast path,
    // which loses rotated texture coordinates at exactly horizontal light.
    const int corners[12] = {0,1,4,1,3,4,3,2,4,2,0,4};
    for (int i = 0; i < 12; i++) {
      int q = corners[i];
      double u = q == 4 ? 0.5 : (q & 1), v = q == 4 ? 0.5 : (q / 2);
      double x = u * w, y = v * h;
      double across = ((flip & 1 ? w - x : x) - (flip & 1 ? w - ax : ax)) * width_scale;
      double height = anchor - (flip & 2 ? h - y : y) * fy;
      double spread=s->softness*fx*fmax(0,height)/fmax(1,anchor);
      double X = foot_x + side_x * across + height * s->slope_x + (tap==1?spread:tap==2?-spread:0);
      double Y = foot_y + side_y * across + height * s->slope_y + (tap==3?spread:tap==4?-spread:0)*s->ground_aspect;
      quad[i] = (SDL_Vertex){{(float)((X-s->destination.x)*s->factor),
                             (float)((Y-s->destination.y)*s->factor)},
                            {255,255,255,(Uint8)lround(detail*(s->softness>0?(tap==0?.64:.19):1)*255)},
                            {(float)u,(float)v}};
    }
    if (SDL_RenderGeometry(s->renderer, m->silhouette, quad, 12, NULL, 0) < 0) return 0;
    }
  }
  SDL_Vertex vertices[128 * 18];
  int n = 0;
  const int order[6] = {0, 1, 2, 2, 1, 3};
  const double feathers[4] = {0.06, 0.12, 0.24, 0.40};
  for (int i = 0; i < m->count; i++) {
    CastShadowSlice slice = m->slices[i];
    int thin = slice.radius * fx * s->factor < 1.5;
    if (detail >= 1 && !thin) continue;
    double x = (flip & 1 ? w - slice.x : slice.x) * fx;
    double y = (flip & 2 ? h - slice.y : slice.y) * fy;
    double height = fmax(0, anchor - y);
    // Above-ground sections are circular volumes. The camera foreshortens
    // their receiving ellipses; this remains finite for any sun direction.
    double blur = (0.6 + 1.8 * height / fmax(1, anchor)) * fx;
    double radius = slice.radius * fx;
    double rx = radius + blur, ry = rx * s->ground_aspect;
    // A subpixel cross-section can disappear when target rasterization
    // rounds both edges onto one row. Preserve one mask pixel of coverage.
    rx = fmax(rx, 1 / s->factor);
    ry = fmax(ry, 1 / s->factor);
    double top_x, top_y, bottom_x, bottom_y;
    double half = slice.thickness * fy * 0.5;
    cast_shadow_project(x, y - half, anchor, s->slope_x, s->slope_y, &top_x, &top_y);
    cast_shadow_project(x, y + half, anchor, s->slope_x, s->slope_y, &bottom_x, &bottom_y);
    if (detail > 0 && thin) {
      double foot = (flip & 1 ? w - ax : ax) * fx;
      double across = x - foot;
      double side_x = -s->slope_y / (s->ground_aspect * length);
      double side_y = s->slope_x * s->ground_aspect / length;
      top_x += foot + side_x * across - x;
      bottom_x += foot + side_x * across - x;
      top_y += side_y * across;
      bottom_y += side_y * across;
    }
    double cx = (top_x + bottom_x) * 0.5, cy = (top_y + bottom_y) * 0.5;
    double vx = (top_x - bottom_x) * 0.5, vy = (top_y - bottom_y) * 0.5;
    // Sweep the receiving ellipse along the projected band height. Two
    // half-disc caps and a constant-width middle avoid transparent seams
    // where narrow, tall trunk bands meet under low sunlight.
    double nx = vx / rx, ny = vy / ry;
    double reach = hypot(nx, ny);
    double ux = reach > 1e-9 ? nx / reach : 1;
    double uy = reach > 1e-9 ? ny / reach : 0;
    cx = (d.x + cx - s->destination.x) * s->factor;
    cy = (d.y + cy - s->destination.y) * s->factor;
    rx *= s->factor; ry *= s->factor;
    int band = 0;
    double feather = blur / (radius + blur);
    for (int j = 1; j < 4; j++) if (fabs(feathers[j] - feather) < fabs(feathers[band] - feather)) band = j;
    Uint8 alpha = (Uint8)lround(fmin(1, slice.coverage) * (thin ? 1 : 1-detail) * 255);
    const double along[4] = {-reach - 1, -reach, reach, reach + 1};
    const double tex_u[4] = {0, 0.5, 0.5, 1};
    for (int part = 0; part < 3; part++) for (int j = 0; j < 6; j++) {
      int q = order[j];
      double u0 = along[part + (q & 1)], v0 = q / 2 ? 1 : -1;
      float X = (float)(cx + rx * (ux * u0 - uy * v0));
      float Y = (float)(cy + ry * (uy * u0 + ux * v0));
      float u = (float)tex_u[part + (q & 1)];
      float v = (float)(band * 64 + (q / 2 ? 63.5 : 0.5)) / 256;
      vertices[n++] = (SDL_Vertex){{X,Y}, {255,255,255,alpha}, {u,v}};
    }
  }
  int ok=SDL_RenderGeometry(s->renderer, s->disc, vertices, n, NULL, 0)==0;
  if(s->contact>0){
    double radius=fmax(1.5,fmin(w*.075,h*.045))*fx;
    SDL_FRect contact={(float)((d.x+(flip&1?w-ax:ax)*fx-radius-s->destination.x)*s->factor),
      (float)((d.y+anchor-radius*s->ground_aspect-s->destination.y)*s->factor),
      (float)(radius*2*s->factor),(float)(radius*2*s->ground_aspect*s->factor)};
    SDL_SetTextureAlphaMod(s->disc,(Uint8)lround(fmin(1,s->contact)*255));
    SDL_Rect src={0,192,64,64};ok&=SDL_RenderCopyF(s->renderer,s->disc,&src,&contact)==0;
    SDL_SetTextureAlphaMod(s->disc,255);
  }
  return ok;
}
// A solid building retains its ground footprint as light moves. Extruding a
// diamond foundation creates a finite convex shadow, including when the light
// is horizontal on screen, without rotating the building like a tree billboard.
int cast_shadow_building(CastShadowRenderer *s,double ax,double ay,SDL_FRect d,int flip){
  if(!s||!s->active||!isfinite(ax)||!isfinite(ay)||!isfinite(d.x)||!isfinite(d.y)||!isfinite(d.w)||!isfinite(d.h)||d.w<=0||d.h<=0)return 0;
  double hx=d.w*.31,hy=d.h*.22,height=d.h*.48;
  double cx=d.x+(flip&1?1-ax:ax)*d.w,cy=d.y+(flip&2?1-ay:ay)*d.h;
  typedef struct{double x,y;} Point;
  Point p[8],hull[16];int n=0;
  const double dx[4]={-1,0,1,0},dy[4]={0,-1,0,1};
  for(int i=0;i<8;i++){int q=i%4;p[i]=(Point){cx+dx[q]*hx+(i>=4?height*s->slope_x:0),cy+dy[q]*hy+(i>=4?height*s->slope_y:0)};}
  for(int i=1;i<8;i++){Point v=p[i];int j=i;while(j>0&&(p[j-1].x>v.x||(p[j-1].x==v.x&&p[j-1].y>v.y))){p[j]=p[j-1];j--;}p[j]=v;}
  for(int pass=0;pass<2;pass++){
    int start=n;
    for(int i=0;i<8;i++){
      Point v=p[pass?7-i:i];
      while(n>=start+2){Point a=hull[n-2],b=hull[n-1];if((b.x-a.x)*(v.y-a.y)-(b.y-a.y)*(v.x-a.x)>0)break;n--;}
      hull[n++]=v;
    }n--;
  }
  if(n<3)return 0;double mx=0,my=0;for(int i=0;i<n;i++){mx+=hull[i].x;my+=hull[i].y;}mx/=n;my/=n;
  SDL_Vertex v[16*9];int count=0;
  double feather=fmin(6,fmax(.35,s->softness)*d.w/177); // scales with the sprite, not the screen
  for(int i=0;i<n;i++){
    Point a=hull[i],b=hull[(i+1)%n];double da=hypot(a.x-mx,a.y-my),db=hypot(b.x-mx,b.y-my);
    Point ao={a.x+(a.x-mx)*feather/fmax(1,da),a.y+(a.y-my)*feather/fmax(1,da)};
    Point bo={b.x+(b.x-mx)*feather/fmax(1,db),b.y+(b.y-my)*feather/fmax(1,db)};
    Point q[9]={{mx,my},a,b,a,ao,b,b,ao,bo};
    for(int j=0;j<9;j++)v[count++]=(SDL_Vertex){{(float)((q[j].x-s->destination.x)*s->factor),(float)((q[j].y-s->destination.y)*s->factor)},
      {0,0,0,(Uint8)(j==4||j==7||j==8?0:255)},{0,0}};
  }
  SDL_BlendMode blend;SDL_GetRenderDrawBlendMode(s->renderer,&blend);SDL_SetRenderDrawBlendMode(s->renderer,SDL_BLENDMODE_BLEND);
  int ok=SDL_RenderGeometry(s->renderer,NULL,v,count,NULL,0)==0;SDL_SetRenderDrawBlendMode(s->renderer,blend);return ok;
}
// Clip in UV space instead of clamping endpoints, which would stretch an
// entire shadow span when a receiver crosses the retained texture boundary.
static int shadow_triangle(SDL_Renderer *r,SDL_Texture *texture,const SDL_Vertex *triangle,SDL_Vertex *batch,int *used){
  SDL_Vertex poly[12],temp[12];int count=3;memcpy(poly,triangle,3*sizeof *poly);
  for(int plane=0;plane<4&&count;plane++){
    int output=0;float edge=plane&1?1:0;
    for(int i=0;i<count;i++){
      SDL_Vertex a=poly[(i+count-1)%count],b=poly[i];
      float av=plane<2?a.tex_coord.x:a.tex_coord.y,bv=plane<2?b.tex_coord.x:b.tex_coord.y;
      int ai=plane&1?av<=1:av>=0,bi=plane&1?bv<=1:bv>=0;
      if(ai!=bi){
        float t=(edge-av)/(bv-av);SDL_Vertex v=a;
        v.position.x+=t*(b.position.x-a.position.x);v.position.y+=t*(b.position.y-a.position.y);
        v.tex_coord.x+=t*(b.tex_coord.x-a.tex_coord.x);v.tex_coord.y+=t*(b.tex_coord.y-a.tex_coord.y);
        if(plane<2)v.tex_coord.x=edge;else v.tex_coord.y=edge;
        temp[output++]=v;
      }
      if(bi)temp[output++]=b;
    }
    count=output;memcpy(poly,temp,(size_t)count*sizeof *poly);
  }
  int ok=1;
  for(int i=1;i+1<count;i++){
    batch[(*used)++]=poly[0];batch[(*used)++]=poly[i];batch[(*used)++]=poly[i+1];
    if(*used==4092){ok&=SDL_RenderGeometry(r,texture,batch,*used,NULL,0)==0;*used=0;}
  }
  return ok;
}
static double software_alpha(const CastShadowRenderer *s,double x,double y){
  x=(x-s->destination.x)*s->factor-.5;y=(y-s->destination.y)*s->factor-.5;
  int ix=(int)floor(x),iy=(int)floor(y);double u=x-ix,v=y-iy,total=0;
  for(int q=0;q<4;q++){int xx=ix+(q&1),yy=iy+q/2;if(xx<0||yy<0||xx>=s->width||yy>=s->height)continue;
    double weight=(q&1?u:1-u)*(q/2?v:1-v);total+=s->software_pixels[((size_t)yy*s->width+xx)*4+3]*weight;}
  return total/255.;
}
static int software_composite(CastShadowRenderer *s,const uint8_t *ground,int w,int h,double ox,double oy,double k,Uint8 opacity){
  SDL_Vertex vertices[4092];int n=0,ok=1;const int order[6]={0,1,2,2,1,3};
  SDL_SetRenderDrawBlendMode(s->renderer,SDL_BLENDMODE_MOD);
  for(int y=0;y<h;y++)for(int x=0;x<w;){
    if(!ground[(size_t)y*w+x]){x++;continue;}
    double X=ox+(x+.5)*k,Y=oy+(y+.5)*k,u=X,v=Y;
    if(s->relief)course_relief_shadow(X,Y,course_relief_sample(s->relief,x+.5,y+.5)*k,s->slope_x,s->slope_y,&u,&v);
    Uint8 alpha=(Uint8)lround(opacity*software_alpha(s,u,v));int start=x++;
    if(!alpha)continue;
    while(x<w&&ground[(size_t)y*w+x]){
      X=ox+(x+.5)*k;Y=oy+(y+.5)*k;u=X;v=Y;
      if(s->relief)course_relief_shadow(X,Y,course_relief_sample(s->relief,x+.5,y+.5)*k,s->slope_x,s->slope_y,&u,&v);
      if((Uint8)lround(opacity*software_alpha(s,u,v))!=alpha)break;x++;
    }
    Uint8 transmission=(Uint8)(256-alpha);
    for(int j=0;j<6;j++){int q=order[j];vertices[n++]=(SDL_Vertex){{(float)(ox+(q&1?x:start)*k),(float)(oy+(y+q/2)*k)},{transmission,transmission,transmission,255},{0,0}};}
    if(n==4092){ok&=SDL_RenderGeometry(s->renderer,NULL,vertices,n,NULL,0)==0;n=0;}
  }
  if(n)ok&=SDL_RenderGeometry(s->renderer,NULL,vertices,n,NULL,0)==0;
  SDL_SetRenderDrawBlendMode(s->renderer,s->blend);return ok;
}
int cast_shadow_end(CastShadowRenderer *s, const uint8_t *ground, int w, int h, double ox, double oy, double k) {
  if (!s->active) return 0;
  int software_ok=1;
  if(s->software&&!s->image_valid){
    size_t bytes=(size_t)s->width*s->height*4;
    if(s->software_bytes<bytes){uint8_t *pixels=realloc(s->software_pixels,bytes);if(pixels){s->software_pixels=pixels;s->software_bytes=bytes;}}
    software_ok=s->software_pixels&&s->software_bytes>=bytes&&SDL_RenderReadPixels(s->renderer,NULL,SDL_PIXELFORMAT_RGBA32,s->software_pixels,s->width*4)==0;
  }
  restore_state(s);
  s->active = 0;
  if (!ground || w <= 0 || h <= 0 || !isfinite(k) || k <= 0) return 0;
  SDL_Rect clip = s->destination;
  if (s->clipped && !SDL_IntersectRect(&clip, &s->clip, &clip)) return 1;
  SDL_RenderSetClipRect(s->renderer, &clip);
  SDL_Vertex vertices[4092];
  int n = 0, ok = 1;
  const int order[6] = {0, 1, 2, 2, 1, 3};
  double darkness=s->linear?1-light_display_encode((float)(1-s->opacity)):s->opacity;
  Uint8 opacity = (Uint8)lround(darkness * 255);
  if(s->software){
    int result=software_ok&&software_composite(s,ground,w,h,ox,oy,k,opacity);
    SDL_RenderSetClipRect(s->renderer,s->clipped?&s->clip:NULL);s->image_valid=result;return result;
  }
  // Only valid terrain spans are composited. Bounded batches avoid another
  // target or a full-screen readback and keep sky, dialogs and map edges clear.
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w;) {
      if (!ground[(size_t)y * w + x]) { x++; continue; }
      int x0 = x++;
      while (x < w && ground[(size_t)y * w + x]) x++;
      // Adapt only the receiver UVs, not its ownership. A common shadow plane
      // cancels caster/receiver elevation on flat land and bends over hills.
      typedef struct {int a,b;double top[2],bottom[2];} Span;
      Span stack[32];int pending=1;
      stack[0]=(Span){x0,x,{0,0},{0,0}};
      if(s->relief){
        stack[0].top[0]=course_relief_sample(s->relief,x0,y);
        stack[0].top[1]=course_relief_sample(s->relief,x,y);
        stack[0].bottom[0]=course_relief_sample(s->relief,x0,y+1);
        stack[0].bottom[1]=course_relief_sample(s->relief,x,y+1);
      }
      while(pending){
        Span p=stack[--pending];int mid=(p.a+p.b)/2;
        if(s->relief&&p.b-p.a>4&&pending<30){
          double top=course_relief_sample(s->relief,mid,y),bottom=course_relief_sample(s->relief,mid,y+1);
          double f=(mid-p.a)/(double)(p.b-p.a);
          if(fabs(top-(p.top[0]+f*(p.top[1]-p.top[0])))>.12||fabs(bottom-(p.bottom[0]+f*(p.bottom[1]-p.bottom[0])))>.12){
            stack[pending++]=(Span){mid,p.b,{top,p.top[1]},{bottom,p.bottom[1]}};
            stack[pending++]=(Span){p.a,mid,{p.top[0],top},{p.bottom[0],bottom}};continue;
          }
        }
        float xx[2]={(float)(ox+p.a*k),(float)(ox+p.b*k)};
        float yy[2]={(float)(oy+y*k),(float)(oy+(y+1)*k)};
        SDL_Vertex quad[6];
        for(int j=0;j<6;j++){
          int q=order[j];double X=xx[q&1],Y=yy[q/2],u=X,v=Y;
          if(s->relief)course_relief_shadow(X,Y,(q/2?p.bottom[q&1]:p.top[q&1])*k,s->slope_x,s->slope_y,&u,&v);
          quad[j]=(SDL_Vertex){{(float)X,(float)Y},{255,255,255,opacity},
            {(float)((u-s->destination.x)*s->factor/s->width),(float)((v-s->destination.y)*s->factor/s->height)}};
        }
        int inside=1;for(int j=0;j<6;j++)inside&=quad[j].tex_coord.x>=0&&quad[j].tex_coord.x<=1&&quad[j].tex_coord.y>=0&&quad[j].tex_coord.y<=1;
        if(inside){if(n>4086){ok&=SDL_RenderGeometry(s->renderer,s->target,vertices,n,NULL,0)==0;n=0;}memcpy(vertices+n,quad,sizeof quad);n+=6;if(n==4092){ok&=SDL_RenderGeometry(s->renderer,s->target,vertices,n,NULL,0)==0;n=0;}}
        else{ok&=shadow_triangle(s->renderer,s->target,quad,vertices,&n);ok&=shadow_triangle(s->renderer,s->target,quad+3,vertices,&n);}
      }
    }
  }
  if (n) ok &= SDL_RenderGeometry(s->renderer, s->target, vertices, n, NULL, 0) == 0;
  SDL_RenderSetClipRect(s->renderer, s->clipped ? &s->clip : NULL);
  s->image_valid=ok;
  return ok;
}
