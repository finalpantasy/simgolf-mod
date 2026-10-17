// composeGolferPalette (0x00462020), recovered from original x86 calls.
// Ghidra lost callee-popped palette arguments and confused outgoing stack
// slots with selectors. Keep the old body behind fixes.clothing_palette.
#include "golf_protos.h"
#include "port/runtime.h"
#include "re_bounds.h"
#ifndef __EMSCRIPTEN__
#include "mods/mods.h"
#endif
#define CP32(a) (*(uint32_t *)(uintptr_t)(a))
#define CP8(a) (*(int8_t *)(uintptr_t)(a))
#define CP_Z 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
static uint32_t palette_call(uint32_t object, int method, uint8_t *rgb, int first, int count) {
  return icall_lookup(CP32(CP32(object) + method))(object, (uint32_t)(uintptr_t)rgb, first, count, CP_Z);
}
static uint32_t working_palette(void) {
  if (!CP32(0x81ca14))
    icall_lookup(CP32(CP32(0x81ca10)))(0x81ca10, 0, 0, 0, CP_Z);
  return CP32(0x81ca14);
}
static void palette_read(uint8_t *rgb, int selector, int count) {
  // Retain the slot guard for malformed saves.
  if ((unsigned)selector >= 0x200) return;
  uint32_t source = CP32(0x81ca6c + selector * 0x58);
  if (source) palette_call(source, 0x10, rgb, 0, count);
}
static uint32_t palette_write(uint8_t *rgb, int first, int count) {
  uint32_t destination = working_palette();
  return destination ? palette_call(destination, 0x14, rgb + first * 3, first, count) : 0;
}
int fix_clothing_palette_on(void) {
#ifndef __EMSCRIPTEN__
  return mods_get_int("fixes.clothing_palette", 1);
#else
  return 1;
#endif
}
uint32_t compose_golfer_palette(int slot) {
  // The original leaves unused tail bytes on its stack. Zero those bytes;
  // golfer sprites index only 0..106 plus the transparent key.
  uint8_t rgb[768] = {0};
  int male = 1, special = slot >= 0x9c, profile = 0;
  int shirt, pants, skin, hair = 4, hat = 1, head_hair = 0;
  if (slot < 0 || slot >= 0x200) return 0;
  if (!special) {
    profile = *(int16_t *)(uintptr_t)(0x57956e + slot * 0x100);
    if (!roster_profile_valid(profile)) return 0;
    uint32_t person = roster_address(0x4d6088) + profile * 0x230;
    uint32_t golfer = 0x5794b8 + slot * 0x100;
    male = FUN_0046c940(slot) != 0;
    shirt = profile * 3 % 10; pants = profile % 10;
    skin = head_default_skin(male, profile);
    hair = (CP8(golfer + 0x19) & 2) ? 4 : skin;
    hat = (CP8(golfer + 0x19) & 4) ? shirt : (male ? 0 : 9);
    if (!male) shirt = profile * 2 % 3 + 1;
    int head = CP8(person + 0x22);
    if (head >= 0 && head < 0x13)
      head_hair = CP8(0x4d55eb + (head + (male ? 0 : 20)) * 0x44);
    if (CP32(person + 0x2c)) {
      shirt = CP8(person + 0x24); pants = CP8(person + 0x25);
      skin = CP8(person + 0x27); hair = CP8(person + 0x26);
      hat = CP8(person + 0x23) & 15; head_hair = CP8(person + 0x28);
    }
    if (CP8(golfer + 0x18) == 0x20) {
      uint32_t roster = roster_address(0x58dd70) + (uint8_t)CP8(golfer + 0xbb) * 0x38;
      hair = 4; male = 1;
      hat = CP8(roster + 2); skin = CP8(roster + 1);
      shirt = CP8(roster + 3); pants = CP8(roster + 4);
    }
    *(uint8_t *)(uintptr_t)(person + 0x23) = (CP8(person + 0x23) & 0xf0) | hat;
    *(uint8_t *)(uintptr_t)(person + 0x24) = shirt;
    *(uint8_t *)(uintptr_t)(person + 0x25) = pants;
    *(uint8_t *)(uintptr_t)(person + 0x27) = skin;
    *(uint8_t *)(uintptr_t)(person + 0x26) = hair;
    *(uint8_t *)(uintptr_t)(person + 0x28) = head_hair;
  } else {
    uint32_t record = 0x55d759 + (slot - 0x9c) * 0x25;
    skin = CP8(record); hat = CP8(record + 1) + 5;
    shirt = CP8(record + 2); pants = CP8(record + 3);
  }
  int offset = male ? 0 : 0x8b;
  palette_read(rgb, offset, 120); palette_write(rgb, 0, 120);
  palette_read(rgb, shirt + offset, 20); palette_write(rgb, 0, 20);
  palette_read(rgb, pants + offset, 40); palette_write(rgb, 20, 20);
  palette_read(rgb, skin + offset, 60); palette_write(rgb, 40, 20);
  palette_read(rgb, hat + offset, 80); palette_write(rgb, 60, 20);
  if (male) {
    palette_read(rgb, shirt, 121); palette_write(rgb, 107, 15);
    palette_read(rgb, pants, 131); palette_write(rgb, 122, 10);
    palette_read(rgb, skin, 136); palette_write(rgb, 132, 50);
    if (!special) { palette_read(rgb, hair, 90); palette_write(rgb, 80, 7); }
  } else {
    palette_read(rgb, head_hair + offset, 90); palette_write(rgb, 80, 10);
    palette_read(rgb, hair + offset, 100); palette_write(rgb, 90, 7);
    palette_read(rgb, 0x8b + (slot & 1), 172);
  }
  int tail = slot < 0x98 && (*(uint8_t *)(uintptr_t)(roster_address(0x5849e2) + profile * 0x2c) & 7) > 3;
  palette_read(rgb, 0x8b + tail, 172);
  return palette_write(rgb, 144, 28);
}
