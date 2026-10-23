#ifndef SIMGOLF_HD_PRESENT_POLICY_H
#define SIMGOLF_HD_PRESENT_POLICY_H

#define HD_PRESENT_SHOULD_HOLD_COURSE(snapshot_fresh, frame_layered, frame_course, course_transition, options_open, \
                                      dialog_active, report_active, full_screen_cover, enabled)                  \
  (!(snapshot_fresh) && (frame_layered) && (frame_course) && (course_transition || options_open) &&             \
   !(dialog_active) && !(report_active) && !(full_screen_cover) && (enabled))

// Native modal panels draw over the retained course, whose raw pixels have
// not been graded yet. Publishing their unlayered back buffer drops lighting.
// Original dialogs, reports and full-screen pages still publish normally.
static inline int hd_present_hold_course(int fresh, int layered, int course,
    int transition, int options, int paused, int dialog, int native_dialog,
    int report, int cover, int enabled) {
  if (HD_PRESENT_SHOULD_HOLD_COURSE(fresh,layered,course,transition,options,
                                   dialog,report,cover,enabled)) return 1;
  return !fresh && layered && course && enabled && !report && !cover &&
         ((paused && !dialog) || (dialog && native_dialog));
}

// Pace completed pictures, so variable draw costs do not move presentation
// times. An over-budget frame starts a fresh period without a catch-up burst.
static inline double hd_present_deadline(double previous, double ready, double period) {
  double next = previous + period;
  return next < ready ? ready : next;
}

#endif
