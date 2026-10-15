#ifndef PORT_REPORTS_HANDOFF_POLICY_H
#define PORT_REPORTS_HANDOFF_POLICY_H

#include "port/reports_kinds.h"

enum { RP_HANDOFF_ACTION_OTHER = 0, RP_HANDOFF_ACTION_CLOSE, RP_HANDOFF_ACTION_TAB };

static inline int rp_handoff_is_tab_target(int kind) {
  switch (kind) {
#define RP_TAB_TARGET_CASE(report_kind) case report_kind: return 1;
    RP_REPORT_TAB_TARGETS(RP_TAB_TARGET_CASE)
#undef RP_TAB_TARGET_CASE
    default: return 0;
  }
}

static inline int rp_handoff_next_pending(int pending_kind, int current_kind, int action, int target_kind) {
  if (action == RP_HANDOFF_ACTION_CLOSE) return RP_NONE;
  if (action == RP_HANDOFF_ACTION_TAB)
    return target_kind != current_kind ? target_kind : RP_NONE;
  return pending_kind;
}

static inline int rp_handoff_preserve_panel(int report_on, int current_kind, int pending_kind, int target_on) {
  return report_on && target_on && pending_kind != current_kind && rp_handoff_is_tab_target(pending_kind);
}

#endif
