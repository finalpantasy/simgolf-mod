#ifndef SIMGOLF_ENVIRONMENT_VIEW_H
#define SIMGOLF_ENVIRONMENT_VIEW_H

#include "port/golf/hd_ui.h"

typedef struct EnvironmentView {
  double x, y, scale;
} EnvironmentView;

// Course axes describe content motion when the camera moves, not the
// viewing origin. Undo that translation, the source centre and the live
// slide/glide before sampling weather in projected world pixels at zoom4.
static inline EnvironmentView environment_view_camera(const HdUiCamera *cam,
    int width, int height, double output_scale, double stretch, double tx, double ty) {
  double zoom = cam->zoom > 0 ? cam->zoom / 4.0 : 1.0;
  if (stretch <= 0) stretch = 1;
  EnvironmentView view;
  view.x = -(width * .5 + cam->x * cam->ax + cam->y * cam->bx + cam->ox + tx / stretch) / zoom;
  view.y = -(height * .5 + cam->x * cam->ay + cam->y * cam->by + cam->oy + ty / stretch) / zoom;
  view.scale = output_scale * stretch * zoom;
  return view;
}

#endif
