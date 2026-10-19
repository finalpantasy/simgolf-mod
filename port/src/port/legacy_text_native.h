#pragma once
#include <SDL.h>
#include "port/legacy_text.h"
typedef struct LegacyRenderer LegacyRenderer;
// Renderer thread only. Returns 0 before any pixels are removed if setup fails.
LegacyRenderer *legacy_text_create(SDL_Renderer *renderer);
void legacy_text_draw(LegacyRenderer *renderer, const LegacyText *text,
                      float x, float y, float scale, SDL_Rect bounds);

void legacy_text_device_reset(LegacyRenderer *t);
