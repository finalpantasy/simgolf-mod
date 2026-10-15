#include <stdio.h>

#include "port/reports_handoff_policy.h"

static int failed, checks;

static void check(const char *name, int got, int want) {
  checks++;
  if (got == want) return;
  fprintf(stderr, "FAIL %s: got %d, wanted %d\n", name, got, want);
  failed++;
}

static void check_enabled_tab_targets(void) {
#define CHECK_TAB_TARGET(kind) \
  check("enabled tab target", rp_handoff_preserve_panel(1, RP_HOLE, kind, 1), 1);
  RP_REPORT_TAB_TARGETS(CHECK_TAB_TARGET)
#undef CHECK_TAB_TARGET
}

static void check_queued_actions(void) {
  int pending = RP_NONE;
  pending = rp_handoff_next_pending(pending, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_COURSE);
  check("first tab queues its destination", pending, RP_COURSE);
  pending = rp_handoff_next_pending(pending, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_COMMENTS);
  check("later tab replaces earlier destination", pending, RP_COMMENTS);
  check("latest tab is the preserved destination",
        rp_handoff_preserve_panel(1, RP_HOLE, pending, 1), 1);

  pending = RP_NONE;
  pending = rp_handoff_next_pending(pending, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_COURSE);
  pending = rp_handoff_next_pending(pending, RP_HOLE, RP_HANDOFF_ACTION_CLOSE, RP_NONE);
  check("close cancels a queued tab", pending, RP_NONE);
  check("tab then close does not preserve the panel",
        rp_handoff_preserve_panel(1, RP_HOLE, pending, 1), 0);

  pending = rp_handoff_next_pending(RP_NONE, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_COURSE);
  pending = rp_handoff_next_pending(pending, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_TOURNEY);
  check("later invalid tab replaces earlier valid destination", pending, RP_TOURNEY);
  check("invalid latest tab cleans up instead of preserving",
        rp_handoff_preserve_panel(1, RP_HOLE, pending, 1), 0);

  pending = rp_handoff_next_pending(RP_NONE, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_COURSE);
  pending = rp_handoff_next_pending(pending, RP_HOLE, RP_HANDOFF_ACTION_TAB, RP_HOLE);
  check("same-page tab cancels an older destination", pending, RP_NONE);
}

int main(void) {
  check_enabled_tab_targets();
  check_queued_actions();
  check("course handoff keeps panel", rp_handoff_preserve_panel(1, RP_HOLE, RP_COURSE, 1), 1);
  check("comments handoff keeps panel", rp_handoff_preserve_panel(1, RP_HOLE, RP_COMMENTS, 1), 1);
  check("same page does not hand off", rp_handoff_preserve_panel(1, RP_COURSE, RP_COURSE, 1), 0);
  check("cancelled navigation cleans up", rp_handoff_preserve_panel(1, RP_HOLE, RP_NONE, 1), 0);
  check("invalid destination cleans up", rp_handoff_preserve_panel(1, RP_HOLE, RP_TOURNEY, 1), 0);
  check("non-tab report cleans up", rp_handoff_preserve_panel(1, RP_HOLE, RP_KEYS, 1), 0);
  check("disabled destination cleans up", rp_handoff_preserve_panel(1, RP_HOLE, RP_COURSE, 0), 0);
  check("inactive report cleans up", rp_handoff_preserve_panel(0, RP_HOLE, RP_COURSE, 1), 0);
  if (failed) return 1;
  printf("reports handoff policy: %d checks passed\n", checks);
  return 0;
}
