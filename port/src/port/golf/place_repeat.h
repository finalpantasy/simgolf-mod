// The original keeps small decorations armed, but clears landmarks and bridges.
// Repeat only those two amenities; building and clubhouse placement stays single.
#pragma once

static inline int place_tool_after(int tool, int repeat_amenities) {
  if (repeat_amenities && (tool == 4 || tool == 19)) return tool;
  return (tool < 6 && tool != 4) || tool == 16 ? tool : -1;
}
