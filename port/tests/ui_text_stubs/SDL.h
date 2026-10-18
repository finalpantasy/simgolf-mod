/* Minimal SDL surface used to compile the real text renderer in fault tests. */
#pragma once
#include <stdint.h>
typedef uint32_t Uint32;
typedef struct { int unused; } SDL_Renderer;
typedef struct { int unused; } SDL_Texture;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { float x, y; } SDL_FPoint;
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct { SDL_FPoint position; SDL_Color color; SDL_FPoint tex_coord; } SDL_Vertex;
#define SDL_PIXELFORMAT_ARGB8888 1
#define SDL_TEXTUREACCESS_STATIC 0
#define SDL_BLENDMODE_BLEND 1
#define SDL_ScaleModeNearest 0
#define SDL_TICKS_PASSED(A, B) ((int32_t)((B) - (A)) <= 0)
Uint32 SDL_GetTicks(void);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *, Uint32, int, int, int);
void SDL_DestroyTexture(SDL_Texture *);
int SDL_SetTextureBlendMode(SDL_Texture *, int);
int SDL_SetTextureScaleMode(SDL_Texture *, int);
int SDL_RenderFlush(SDL_Renderer *);
int SDL_UpdateTexture(SDL_Texture *, const SDL_Rect *, const void *, int);
int SDL_RenderGeometry(SDL_Renderer *, SDL_Texture *, const SDL_Vertex *, int, const int *, int);
