// Completed CPU images need their own byte budget, separate from GPU residency.
#pragma once
#include <stddef.h>
static int hd_art_queue_accept(size_t held,size_t incoming,size_t budget) {
  // One oversized image must still make progress. No following image is
  // admitted until it has been consumed; subtraction avoids size_t overflow.
  return held==0 || (incoming<=budget && held<=budget-incoming);
}
