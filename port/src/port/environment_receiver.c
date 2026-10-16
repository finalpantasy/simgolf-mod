#include "port/environment_receiver.h"
#include <stddef.h>
int environment_receiver_build(uint8_t *out, int *queue, int width, int height,
                               const uint16_t *course, const uint8_t *terrain) {
  if (!out || !queue || !course || !terrain || width <= 0 || height <= 0 ||
      (size_t)width > (4u * 1024u * 1024u) / (size_t)height) return 0;
  int count = width * height, head = 0, tail = 0;
  for (int p = 0; p < count; p++) {
    unsigned r=(course[p]>>10)&31,g=(course[p]>>5)&31,b=course[p]&31;
    // Painted water has a blue/turquoise substrate. Only terrain can be water;
    // blue object sprites never become ripple receivers.
    int water=b>r+3 && g>r+2 && b+3>=g;
    out[p] = !(course[p] & 0x7fff) ? 0 : terrain[p] ? (water ? 160 : 127) : 64;
  }
  for (int p = 0; p < count; p++) {
    int x = p % width, y = p / width;
    if (x && y && x != width - 1 && y != height - 1) continue;
    if (!out[p] && !(course[p] & 0x7fff)) {out[p] = 255; queue[tail++] = p;}
  }
  while (head < tail) {
    int p = queue[head++], x = p % width, y = p / width;
    int next[4] = {x ? p - 1 : -1, x + 1 < width ? p + 1 : -1,
                   y ? p - width : -1, y + 1 < height ? p + width : -1};
    for (int j = 0; j < 4; j++) {
      int q = next[j];
      if (q >= 0 && !out[q] && !(course[q] & 0x7fff)) {out[q] = 255; queue[tail++] = q;}
    }
  }
  return 1;
}
