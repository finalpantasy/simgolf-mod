#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Converts a complete numeric game-date token to the Frosted Mint English date.
// Returns 1 only for a valid date; otherwise leaves dst unchanged.
int ui_dialog_format_date_hint(const char *src, char *dst, size_t cap);

// Returns a display-only replacement for the first choice in the game's exact
// three-choice exit confirmation, or NULL when the choice should stay as written.
const char *ui_dialog_choice_label(const char *const *items, int count, int choice);

// Computes a compact centred hint width from measured text and optional portrait.
float ui_dialog_hint_width(float text_width, float pad, float portrait_width, float portrait_gap,
                          float min_width, float max_width, float available_width);

#ifdef __cplusplus
}
#endif
