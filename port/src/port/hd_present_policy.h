#ifndef SIMGOLF_HD_PRESENT_POLICY_H
#define SIMGOLF_HD_PRESENT_POLICY_H

#define HD_PRESENT_SHOULD_HOLD_COURSE(snapshot_fresh, frame_layered, frame_course, course_transition, options_open, \
                                      dialog_active, report_active, full_screen_cover, enabled)                  \
  (!(snapshot_fresh) && (frame_layered) && (frame_course) && (course_transition || options_open) &&             \
   !(dialog_active) && !(report_active) && (!(full_screen_cover) || (options_open)) && (enabled))

// Pace completed pictures, so variable draw costs do not move presentation
// times. An over-budget frame starts a fresh period without a catch-up burst.
static inline double hd_present_deadline(double previous, double ready, double period) {
  double next = previous + period;
  return next < ready ? ready : next;
}

#endif
