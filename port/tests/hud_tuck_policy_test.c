// Build without proprietary game assets or SDL:
//   cl /nologo /W4 /WX /I port/src port/tests/hud_tuck_policy_test.c /Fe:hud_tuck_policy_test.exe
//   hud_tuck_policy_test.exe
#include <stdio.h>
#include <string.h>
#include "port/hud_tuck_policy.h"

static int failed;
static void check(const char *test, const HudSnap *s, int expected) {
  int got = hudf_keep_tray_on_world_click(s);
  if (got != expected) {
    fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", test, got, expected);
    failed++;
  }
}

int main(void) {
  HudSnap s;
  memset(&s, 0, sizeof s);
  s.valid = s.visible = 1;
  s.terrain_tool = s.build_tool = s.elevation_tool = -1;
  check("no construction tool: regular click tucks", &s, 0);
  check("null snapshot: regular click tucks", NULL, 0);

  s.terrain_tool = 0;
  check("course first terrain tool: retains", &s, 1);
  s.terrain_tool = 15;
  check("course last terrain tool: retains", &s, 1);
  s.terrain_tool = -1;
  check("course deselected: tucks", &s, 0);

  s.amenities_on = 1;
  s.build_tool = 1;
  check("extras active: retains", &s, 1);
  s.build_tool = -1;
  s.terrain_tool = 7;  // stale course selection cannot arm Extras
  check("extras unarmed: tucks despite stale terrain tool", &s, 0);
  s.terrain_tool = -1;
  s.amenities_on = 0;

  s.dock_mode = 1;
  s.build_tool = 13;
  check("buildings tool active: retains", &s, 1);
  s.build_tool = -1;
  check("buildings without tool: tucks", &s, 0);
  s.elevation_on = 1;
  s.elevation_tool = 0;
  check("landscape elevation tool: retains", &s, 1);
  s.elevation_tool = -1;
  s.elev_edit = 1;
  check("landscape elevation-edit flag: retains", &s, 1);
  s.elev_edit = 0;
  s.build_tool = 13; // stale Buildings tool cannot arm Landscape
  check("landscape unarmed: tucks despite stale build tool", &s, 0);
  s.elevation_on = 0;

  s.dock_mode = 3;
  s.terrain_tool = 5;
  check("People page unaffected by stale tool", &s, 0);
  s.pro_shots = 1;
  check("waiting pro with all shot choices keeps controls visible", &s, 1);
  s.pro_shots = 2;
  check("waiting pro with straight and Low Punch only keeps controls visible", &s, 1);
  s.dock_mode = 2;
  check("other People page ignores stale shot-choice state", &s, 0);
  s.dock_mode = 4;
  check("paused reopen mode-4 pro keeps shot choices visible", &s, 1);
  s.pro_shots = 0;
  check("mode-4 pro without a pending shot tucks", &s, 0);

  s.dock_mode = 0;
  s.terrain_tool = 5;
  s.visible = 0;
  check("hidden HUD does not retain tray", &s, 0);
  s.visible = 1;
  s.valid = 0;
  check("invalid snapshot does not retain tray", &s, 0);

  if (failed) {
    fprintf(stderr, "%d HUD placement-policy tests failed\n", failed);
    return 1;
  }
  puts("PASS: all 20 HUD tuck-policy cases");
  return 0;
}
