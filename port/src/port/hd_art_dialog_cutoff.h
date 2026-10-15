#pragma once

typedef struct {
  int count;
  int which;
} HdArtDialogCutoff;

typedef struct {
  HdArtDialogCutoff mark;
  unsigned generation;
  int pending;
} HdArtDialogPreCutoff;

static inline int hd_art_dialog_presnap_fresh(unsigned now, unsigned marked_at, int kept) {
  // The caller stores ticks | 1, so an even same-tick mark can be 1 ms ahead.
  unsigned age = now - marked_at;
  return marked_at && kept && (age < 20000u || age == ~0u);
}

static inline int hd_art_dialog_use_presnap(int kind, int fresh) {
  return kind == 1 && fresh;
}

static inline void hd_art_dialog_capture_pre(HdArtDialogPreCutoff *pre, int enabled, int count, int which,
                                              unsigned generation) {
  if (!pre) return;
  pre->mark.count = enabled ? count : -1;
  pre->mark.which = which;
  pre->generation = generation;
  pre->pending = enabled && count >= 0;
}

static inline HdArtDialogCutoff hd_art_dialog_begin_cutoff(HdArtDialogPreCutoff *pre, int use_pre,
                                                           HdArtDialogCutoff current, unsigned generation) {
  HdArtDialogCutoff selected = current;
  if (pre && pre->pending && use_pre && pre->mark.count >= 0 &&
      pre->generation == generation && pre->mark.which == current.which && pre->mark.count <= current.count)
    selected = pre->mark;
  if (pre) pre->pending = 0;
  return selected;
}
