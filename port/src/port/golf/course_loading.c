// Keep the callback in handwritten C. The translator converts bare original
// function names used as arguments into original executable addresses.
#include "golf_protos.h"
#include "port/runtime.h"
uint32_t port_course_height(int x, int y) {
  return port_course_height_cached(x, y, FUN_0040c170);
}
