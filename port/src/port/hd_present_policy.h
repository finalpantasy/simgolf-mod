#ifndef SIMGOLF_HD_PRESENT_POLICY_H
#define SIMGOLF_HD_PRESENT_POLICY_H

#define HD_PRESENT_SHOULD_HOLD_COURSE(snapshot_fresh, frame_layered, frame_course, course_transition, options_open, \
                                      dialog_active, report_active, full_screen_cover, enabled)                  \
  (!(snapshot_fresh) && (frame_layered) && (frame_course) && (course_transition || options_open) &&             \
   !(dialog_active) && !(report_active) && !(full_screen_cover) && (enabled))

#endif
