// Run with port/tests/hud_skill_format.sh, or build without game assets or SDL:
//   cl /nologo /W4 /WX /I port/src port/tests/hud_skill_format_test.c /Fe:hud_skill_format_test.exe
//   hud_skill_format_test.exe
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "port/ui/ui_signed_rating.h"

static int failed;

static void check(int value, const char *expected) {
  char text[16];
  ui_signed_rating(text, sizeof text, value);
  if (strcmp(text, expected) != 0) {
    fprintf(stderr, "FAIL: skill %d: got '%s', expected '%s'\n", value, text, expected);
    failed++;
  }
}

int main(void) {
  check(-1, "-0.01");
  check(-25, "-0.25");
  check(-99, "-0.99");
  check(-100, "-1.00");
  check(-125, "-1.25");
  check(0, "0.00");
  check(1, "0.01");
  check(25, "0.25");
  check(100, "1.00");
  check(125, "1.25");
  check(INT_MIN, "-21474836.48");
  check(INT_MAX, "21474836.47");

  if (failed) {
    fprintf(stderr, "%d HUD skill-format cases failed\n", failed);
    return 1;
  }
  puts("PASS: all 12 HUD skill-format cases");
  return 0;
}
