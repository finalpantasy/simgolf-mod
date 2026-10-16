// Native-resolution composition of the original head and standing body.
// Game thread only; publication uses the existing synchronized UI channels.
#pragma once
#include <stdint.h>
#define PORTRAIT_PROFILE_W 76
#define PORTRAIT_PROFILE_H 184
int portrait_profile(int profile, int art_slot);
int portrait_card(int slot, int art_slot);
int portrait_preview(int profile, int art_slot);
void portrait_profile_reset(void);


// Owned straight-ARGB pixels for the face browser; caller frees them.
uint32_t *portrait_face_image(int character,int *w,int *h);
uint32_t *portrait_original_face_image(int male,int head,int *w,int *h);
