// Save Name's caller strip needs a pre-strip art cutoff only when its page snapshot is fresh.
#include <stdio.h>
#include <string.h>

#include "port/hd_art_dialog_cutoff.h"

static int failed;

static void check(const char *name, int got, int expected) {
  if (got != expected) {
    fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", name, got, expected);
    failed++;
  }
}

static void check_mark(const char *name, HdArtDialogCutoff got, int count, int which) {
  if (got.count != count || got.which != which) {
    fprintf(stderr, "FAIL: %s (got {%d,%d}, expected {%d,%d})\n", name, got.count, got.which, count, which);
    failed++;
  }
}

int main(void) {
  check("fresh snapshot is reusable", hd_art_dialog_presnap_fresh(1200, 1000, 1), 1);
  check("snapshot at the freshness boundary is stale", hd_art_dialog_presnap_fresh(21000, 1000, 1), 0);
  check("missing page copy is not reusable", hd_art_dialog_presnap_fresh(1200, 1000, 0), 0);
  check("no marked snapshot is not reusable", hd_art_dialog_presnap_fresh(1200, 0, 1), 0);
  check("only text entry can use the Save Name cutoff", hd_art_dialog_use_presnap(1, 1), 1);
  check("menus keep their ordinary art cutoff", hd_art_dialog_use_presnap(0, 1), 0);
  check("stale Save Name uses the ordinary art cutoff", hd_art_dialog_use_presnap(1, 0), 0);

  check("even same-tick marker is fresh", hd_art_dialog_presnap_fresh(2000u, 2000u | 1u, 1), 1);
  check("zero-tick encoded marker is fresh", hd_art_dialog_presnap_fresh(0u, 0u | 1u, 1), 1);
  check("odd same-tick marker is fresh", hd_art_dialog_presnap_fresh(2001u, 2001u | 1u, 1), 1);
  check("tick wrap preserves freshness", hd_art_dialog_presnap_fresh(0u, ~0u, 1), 1);
  check("future marker beyond encoding is rejected", hd_art_dialog_presnap_fresh(2000u, 2002u, 1), 0);

  HdArtDialogPreCutoff pre;
  memset(&pre, 0, sizeof pre);
  HdArtDialogCutoff current = {12, 1};
  hd_art_dialog_capture_pre(&pre, 1, 7, 1, 10);
  check_mark("fresh Save Name hides records from before its caller strip",
             hd_art_dialog_begin_cutoff(&pre, 1, current, 10), 7, 1);
  check("fresh cutoff is one-shot", pre.pending, 0);
  check_mark("a second begin uses the current list", hd_art_dialog_begin_cutoff(&pre, 1, current, 10), 12, 1);

  hd_art_dialog_capture_pre(&pre, 1, 7, 1, 10);
  check_mark("ordinary modal keeps its post-caller cutoff", hd_art_dialog_begin_cutoff(&pre, 0, current, 10), 12, 1);
  check("ordinary modal clears the pending Save Name mark", pre.pending, 0);

  hd_art_dialog_capture_pre(&pre, 1, 7, 1, 10);
  HdArtDialogCutoff other_list = {12, 0};
  check_mark("changed art list rejects the stale pre-strip index",
             hd_art_dialog_begin_cutoff(&pre, 1, other_list, 10), 12, 0);

  hd_art_dialog_capture_pre(&pre, 1, 7, 1, 10);
  HdArtDialogCutoff reused_list = {12, 1};
  check_mark("a reset and same-which list reuse reject the stale index",
             hd_art_dialog_begin_cutoff(&pre, 1, reused_list, 11), 12, 1);
  check("generation mismatch clears the pending Save Name mark", pre.pending, 0);

  hd_art_dialog_capture_pre(&pre, 0, 7, 1, 10);
  check_mark("disabled art retains the original fallback", hd_art_dialog_begin_cutoff(&pre, 1, current, 10), 12, 1);

  if (failed) {
    fprintf(stderr, "%d Save Name art-cutoff cases failed\n", failed);
    return 1;
  }
  puts("PASS: all 20 Save Name art-cutoff cases");
  return 0;
}
