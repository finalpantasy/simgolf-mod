// Render-thread-only recovery. Keep CPU state and input intact when SDL loses GPU resources.
#pragma once
#include <SDL.h>
void ui_device_reset(SDL_Renderer *r);
// Call after discarding GPU resources, before any drawing on the new renderer.
void ui_renderer_rebind(SDL_Renderer *old,SDL_Renderer *next);
void hdt_device_reset(void);
void hd_art_device_reset(void);
void water_device_reset(void);
void hdball_device_reset(void);
void lamps_device_reset(void);
void fef_device_reset(void);
void dlgf_device_reset(void);
void hudf_device_reset(void);
void gwf_device_reset(void);
void rpf_device_reset(void);
void modsf_device_reset(void);
