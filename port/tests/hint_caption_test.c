// Run with port/tests/hint_caption.sh, or build without game assets or SDL:
//   clang -Iport/src port/tests/hint_caption_test.c port/src/port/ui/ui_dialog_text.c -o hint_caption_test.exe
// The golfer snapshot draws its message panel, then writes the photo's date into the
// shared text before waiting. The popup keeps the message and shows the date above it.
#include <stdio.h>
#include <string.h>

#include "port/ui/ui_dialog_text.h"

static int failed;

static void expect(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failed++;
  }
}

int main(void) {
  char date[40] = "unchanged";
  const char *message = "Olive Branch has achieved a new skill: Fountain Lover!";

  expect(ui_dialog_hint_caption(message, "8'8'2001", date, sizeof date) == 1, "snapshot date becomes the heading");
  expect(strcmp(date, "August 8, 2001") == 0, "heading reads August 8, 2001");

  snprintf(date, sizeof date, "%s", "unchanged");
  expect(ui_dialog_hint_caption(message, message, date, sizeof date) == 0, "no heading when the text is unchanged");
  expect(strcmp(date, "unchanged") == 0, "dst untouched without a heading");

  expect(ui_dialog_hint_caption(message, "snapshots\\", date, sizeof date) == 0, "no heading for other text");
  expect(ui_dialog_hint_caption("", "8'8'2001", date, sizeof date) == 0, "no heading without a panel message");
  expect(ui_dialog_hint_caption(NULL, "8'8'2001", date, sizeof date) == 0, "no heading for a missing panel");
  expect(ui_dialog_hint_caption(message, NULL, date, sizeof date) == 0, "no heading for missing text");
  expect(ui_dialog_hint_caption(message, "2'30'2001", date, sizeof date) == 0, "no heading for an impossible date");
  expect(ui_dialog_hint_caption(message, "8'8'2001", date, 8) == 0, "no heading when it does not fit");

  if (failed) return 1;
  puts("hint caption: all checks passed");
  return 0;
}
