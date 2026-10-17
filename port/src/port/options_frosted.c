// The Options page, render side and settings (simgolf mod, card task-24), see options_frosted.h.
// Native build only. One top UI layer of the presenter: draws the page with the kit (ui/ui_kit.h), takes
// every pointer and key event while it is open (the game thread's event pump asks it first) and writes
// the player's choices through the mods store (mods_option_set -> options.ini).
// an undeclared function returning a double leaves the x87 stack unbalanced: never allow it (the build passes -w)
#pragma clang diagnostic error "-Wimplicit-function-declaration"

#include "port/options_frosted.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mods/mods.h"
#include "port/staff.h"
#include "port/golf/keymap.h"
#include "port/hd_art.h"
#include "port/hd_native.h"
#include "port/host.h"
#include "port/mods_frosted.h"
#include "port/runtime.h"
#include "port/ui/ui_kit.h"

#define G32(a) (*(volatile int32_t *)(uintptr_t)(a))

// ------------------------------------------------------------------ the rows

enum { OT_HEAD, OT_SEG, OT_TOGGLE, OT_SLIDER, OT_BIT };
enum { SP_NONE, SP_TEXTURES, SP_PROFILE };
typedef struct {
  int type;
  const char *label, *desc;
  const char *key;      // mods store key ("section.key")
  const char *ini;      // golf.ini key that sets the default when the store has no value (or NULL)
  int n;                // OT_SEG: choices
  const char *vals[6], *names[6], *vdesc[6];   // vdesc: the line under the label for that choice (else desc)
  const char *def;      // value when nothing sets it
  float lo, hi, step;   // OT_SLIDER
  const char *unit;
  int restart;          // applies at the next start
  int bit;              // OT_BIT: the mask in the game's preferences word (0x5a5a00, saved with each game)
  int course;           // only while a game is running
  int special;
} Row;

#define SEG3(a, an, b, bn, c, cn) .n = 3, .vals = {a, b, c}, .names = {an, bn, cn}
static const Row k_graphics[] = {
    {.type = OT_HEAD, .label = "Look"},
    {.type = OT_SEG, .label = "Textures", .key = "art.textures", .def = "hd", .special = SP_TEXTURES,
     .desc = "How the course and everything on it is drawn.",
     SEG3("hd", "HD", "pixel_hd", "Pixel HD", "original", "Original"),
     .vdesc = {"Smooth, detailed art from the upscaled art pack.", "Crisp, chunky pixel art in the game's own colours.",
               "The original 2002 pixels."}},
#ifdef SIMGOLF_PATH_STYLE_ASSETS
    {.type = OT_TOGGLE, .label = "Custom path materials", .key = "art.path_styles", .def = "1",
     .desc = "Show placed path materials. Pick each material in the path build palette."},
#endif
    {.type = OT_SEG, .label = "Water", .key = "display.water", .def = "hd",
     .desc = "How ponds, streams, waterfalls and the sea are drawn.",
     .n = 2, .vals = {"hd", "original"}, .names = {"Realistic", "Original"},
     .vdesc = {"Real-time water: waves, ripples, reflections, glitter, foam and falling water.",
               "The original 2002 water and its ripples."}},
    {.type = OT_HEAD, .label = "Motion"},
    {.type = OT_TOGGLE, .label = "Smooth scrolling", .key = "display.smooth", .ini = "smooth", .def = "1",
     .desc = "The course glides and stops anywhere instead of jumping tile by tile."},
    {.type = OT_TOGGLE, .label = "Smooth zoom", .key = "display.smooth_zoom", .ini = "smooth_zoom", .def = "1",
     .desc = "The mouse wheel zoom glides between zoom levels."},
    {.type = OT_HEAD, .label = "Video"},
    {.type = OT_SEG, .label = "Window", .key = "display.display", .ini = "display", .def = "borderless", .restart = 1,
     .desc = "Borderless fills the screen without changing the video mode.",
     .n = 2, .vals = {"borderless", "windowed"}, .names = {"Borderless", "Windowed"}},
    {.type = OT_SEG, .label = "Frame rate", .key = "display.fps", .ini = "fps", .def = "0", .restart = 1,
     .desc = "Highest frames per second drawn. Lower saves power.",
     .n = 5, .vals = {"0", "60", "120", "144", "240"}, .names = {"No cap", "60", "120", "144", "240"}},
    {.type = OT_TOGGLE, .label = "V-sync", .key = "display.vsync", .ini = "vsync", .def = "1", .restart = 1,
     .desc = "Waits for the monitor so the picture never tears."},
    {.type = OT_SEG, .label = "Interface size", .key = "display.ui_scale", .ini = "ui_scale", .def = "auto", .restart = 1,
     .desc = "Auto shows more course on larger screens. Closer restores the original framing for a larger view. Restart to apply.",
     .n = 6, .vals = {"auto", "1", "1.5", "2", "3", "fit"}, .names = {"Auto", "1x", "1.5x", "2x", "3x", "Closer"}},
    {.type = OT_SEG, .label = "Sharpness", .key = "display.filter", .ini = "filter", .def = "sharp", .restart = 1,
     .desc = "How the picture is scaled to your screen.",
     .n = 4, .vals = {"sharp", "smooth", "nearest", "integer"}, .names = {"Sharp", "Smooth", "Blocky", "Whole steps"}},
};

#define PREF(m, lab, d) {.type = OT_BIT, .label = lab, .desc = d, .bit = m, .course = 1}
static const Row k_gameplay[] = {
    {.type = OT_HEAD, .label = "Pace"},
    {.type = OT_SEG, .label = "Game speed", .key = "gameplay.speed", .def = "1",
     .desc = "Runs the whole game clock faster: seasons, golfers and money. Pause still stops it.",
     .n = 6, .vals = {"1", "2", "3", "4", "5", "10"}, .names = {"1x", "2x", "3x", "4x", "5x", "10x"}},
    {.type = OT_SLIDER, .label = "Staff you can hire", .key = "gameplay.staff_max", .def = "20",
     .desc = "Hire up to this many staff. Lowering it keeps everyone already hired.",
     .lo = 1, .hi = STAFF_HIRE_MAX, .step = 1},
    {.type = OT_SEG, .label = "New Career clubhouse", .key = "gameplay.manual_clubhouse", .def = "0",
     .desc = "Automatic placement, or choose its position yourself when starting a Career.",
     .n = 2, .vals = {"0", "1"}, .names = {"Automatic", "Place manually"}},
    {.type = OT_HEAD, .label = "This game (saved with it)"},
    PREF(1, "Golfer names", "Show each golfer's name under them on the course."),
    PREF(2, "Great shot snapshots", "Show a snapshot when a golfer makes a great shot."),
    PREF(4, "Full golfer comments", "Explain what a golfer's comment is about."),
    PREF(8, "New member messages", "Show a message when a golfer joins the club."),
    PREF(16, "Golfer thoughts", "Show golfers' thoughts on screen."),
    PREF(32, "Animals and habitats", "Include animals and habitats on the course."),
    {.type = OT_HEAD, .label = "Look of the game"},
    {.type = OT_TOGGLE, .label = "Original construction menu", .key = "ui.original_bottom", .def = "0",
     .desc = "Use the classic building wheel and trays with modern top stats. Turn off for the combined Course menu and its path and lamp pickers."},
    // Original construction and the modern course palette are independently switchable.
    {.type = OT_SEG, .label = "Golfer tags and cards", .key = "ui.golfers", .def = "frosted_mint",
     .desc = "Name tags, thought bubbles and the golfer card on the course.",
     .n = 2, .vals = {"frosted_mint", "original"}, .names = {"Frosted Mint", "Original"}},
    {.type = OT_SEG, .label = "Name tags", .key = "ui.tags", .def = "hover",
     .desc = "Which golfers show a name on the course. The N key shows or hides all names.",
     SEG3("hover", "On hover", "always", "Always", "off", "Off"),
     .vdesc = {"Only the golfer under the pointer and the selected one.", "Every golfer, all the time.", "No names."}},
    {.type = OT_SEG, .label = "Name style", .key = "ui.tag_style", .def = "plain",
     .desc = "Plain outlined text, or a small frosted pill around each name.",
     .n = 2, .vals = {"plain", "pill"}, .names = {"Plain", "Pill"}},
    {.type = OT_SEG, .label = "Thought bubbles", .key = "ui.bubbles", .def = "brief",
     .desc = "How golfers' thoughts show on the course.",
     .n = 4, .vals = {"brief", "full", "icons", "off"}, .names = {"Brief", "Full", "Faces only", "Off"},
     .vdesc = {"Big thoughts as a bubble for a few seconds; small ones as a face.", "Every thought as a bubble.",
               "Only a small mood face over the head.", "No thoughts."}},
    {.type = OT_SEG, .label = "Tag and bubble size", .key = "ui.tag_scale", .def = "0.7",
     .desc = "How big names and bubbles are.",
     .n = 4, .vals = {"0.5", "0.7", "1", "1.3"}, .names = {"Small", "Medium", "Large", "Largest"}},
    {.type = OT_SEG, .label = "Tag and bubble glass", .key = "ui.tag_opacity", .def = "0.85",
     .desc = "How see-through the frosted glass is. The text stays solid.",
     .n = 3, .vals = {"0.6", "0.85", "1"}, .names = {"Clear", "Frosted", "Solid"}},
    {.type = OT_SEG, .label = "Start menu and new game", .key = "ui.frontend", .def = "frosted_mint",
     .desc = "The start menu, property and load screens. Applies from the next screen.",
     .n = 2, .vals = {"frosted_mint", "original"}, .names = {"Frosted Mint", "Original"}},
    {.type = OT_SEG, .label = "Text", .key = "ui.font", .def = "plus_jakarta",
     .desc = "The game's text in a smooth modern font, or the original 1-bit letters.",
     .n = 2, .vals = {"plus_jakarta", "original"}, .names = {"Modern", "Original"}},
    {.type = OT_SEG, .label = "Golf ball", .key = "ui.ball", .def = "round",
     .desc = "A round, shaded ball with smooth flight, or the original 2 pixel ball.",
     .n = 2, .vals = {"round", "original"}, .names = {"Round", "Original"}},
    {.type = OT_SEG, .label = "Ball size", .key = "ui.ball_size", .def = "75",
     .desc = "How big the round ball is drawn. Larger is easier to follow, smaller is more like a real ball.",
     .n = 4, .vals = {"100", "85", "75", "60"}, .names = {"Large", "Medium", "Small", "Tiny"}},
    {.type = OT_SEG, .label = "Ball colours", .key = "ui.ball_colours", .def = "personal",
     .desc = "Every golfer plays their own colour of ball, or all balls are white.",
     .n = 2, .vals = {"personal", "white"}, .names = {"Personal", "All white"}},
    {.type = OT_SEG, .label = "Ball trail", .key = "ui.ball_trail", .def = "smooth",
     .desc = "The streak behind a ball in flight.",
     .n = 4, .vals = {"smooth", "all", "original", "off"}, .names = {"Smooth", "Every shot", "Original", "Off"},
     .vdesc = {"A smooth fading ribbon on the shots the game marks with a streak.", "A smooth fading ribbon behind every ball in flight.",
               "The game's own short, jumpy streak.", "No streak."}},
    {.type = OT_HEAD, .label = "Course and rules"},
    {.type = OT_SEG, .label = "Size of new courses", .key = "map.scale", .ini = "map_scale", .def = "1",
     .desc = "The land area of a new game. A saved game keeps its own size.",
     .n = 4, .vals = {"1", "2", "4", "10"}, .names = {"Original", "Bigger", "Large", "Huge"}},
    {.type = OT_TOGGLE, .label = "Shift+T hides scenic trees too", .key = "view.hide_decor_trees", .def = "1",
     .desc = "Palms, scenic trees and swamp trees hide with the other trees. Off: only terrain trees."},
    {.type = OT_TOGGLE, .label = "One building per lot", .key = "fixes.building_overlap", .def = "1",
     .desc = "A click on a standing building no longer places a second one on top and takes its price."},
    {.type = OT_TOGGLE, .label = "No black waterfall triangles", .key = "fixes.water_push", .def = "1",
     .desc = "Water on a slope is drawn right after a load. Off: the original, with black triangles."},
    {.type = OT_TOGGLE, .label = "Cut content", .key = "mods.cut_content", .def = "0", .restart = 1,
     .desc = "Adds what the game's files had but never offered: oriental house, dinosaur tarpit, lovers' bench."},
    {.type = OT_HEAD, .label = "Your pro golfer"},
    {.type = OT_SEG, .label = "Skill limit", .key = "skills.cap", .def = "15", .course = 1,
     .desc = "How high your pro's skills can grow. Past 100% needs an 18 hole course (or the switch below).",
     .n = 4, .vals = {"10", "12", "15", "20"}, .names = {"100%", "120%", "150%", "200%"}},
    {.type = OT_TOGGLE, .label = "Past 100% on any course", .key = "skills.any_course", .def = "0", .course = 1,
     .desc = "Off: skills pass 100% only once the course has 18 holes, as the original intended."},
    {.type = OT_TOGGLE, .label = "Bad shots cost skill", .key = "skills.loss", .def = "1", .course = 1,
     .desc = "A ball that ends up in a hazard can lower one of your pro's skills (the original rule)."},
    {.type = OT_SEG, .label = "Cart path help", .key = "ui.paths", .def = "frosted_mint",
     .desc = "A \"No path\" marker over buildings no cart path joins to the clubhouse, and the Y key shows which paths are joined.",
     .n = 2, .vals = {"frosted_mint", "original"}, .names = {"On", "Original"}},
    {.type = OT_HEAD, .label = "Updates"},
    {.type = OT_TOGGLE, .label = "Check for updates", .key = "updates.check", .def = "1",
     .desc = "Asks GitHub once at start whether a newer release exists. Nothing is sent but the request; off means no network at all."},
};

static const Row k_audio[] = {
    {.type = OT_HEAD, .label = "Volume"},
    {.type = OT_SLIDER, .label = "Master volume", .key = "audio.master", .def = "100", .lo = 0, .hi = 100, .step = 5,
     .desc = "Everything you hear."},
    {.type = OT_SLIDER, .label = "Music and ambience", .key = "audio.music", .def = "100", .lo = 0, .hi = 100, .step = 5,
     .desc = "The background music and the sound of the course."},
    {.type = OT_SLIDER, .label = "Sound effects", .key = "audio.effects", .def = "100", .lo = 0, .hi = 100, .step = 5,
     .desc = "Club hits, golfers, crowds and the buttons."},
    {.type = OT_TOGGLE, .label = "Mute everything", .key = "audio.mute", .def = "0",
     .desc = "Silence the game without losing your volume settings."},
};

static const Row k_controls[] = {
    {.type = OT_HEAD, .label = "Keys"},
    {.type = OT_SEG, .label = "Key set", .key = "camera.wasd", .special = SP_PROFILE, .def = "modern",
     .desc = "Original keys are the 2002 game's. Modern keys pan the course with W A S D and move water, sand and draw.",
     .n = 2, .vals = {"original", "modern"}, .names = {"Original keys", "Modern keys"}},
    {.type = OT_HEAD, .label = "Mouse"},
    {.type = OT_SEG, .label = "Drag to pan with", .key = "camera.drag_button", .def = "middle",
     .desc = "Hold this mouse button and drag to move the course. A right click that does not move stays a right click.",
     .n = 4, .vals = {"middle", "right", "middle,right", "none"}, .names = {"Middle", "Right", "Both", "Off"}},
    {.type = OT_TOGGLE, .label = "Camera glide", .key = "camera.glide", .def = "0",
     .desc = "Adds a softer trailing motion when dragging. Off keeps dragging more responsive."},
    {.type = OT_TOGGLE, .label = "Mouse wheel zooms", .key = "camera.wheel_zoom", .def = "1",
     .desc = "Turn the wheel to zoom the course in and out."},
    {.type = OT_SEG, .label = "Wheel zooms toward", .key = "camera.zoom_anchor", .def = "pointer",
     .desc = "The mouse pointer keeps the spot under it, or the middle of the view stays put (as the original).",
     .n = 2, .vals = {"pointer", "centre"}, .names = {"Pointer", "View centre"}},
    {.type = OT_TOGGLE, .label = "Scroll at the screen edge", .key = "camera.edge_scroll", .def = "0",
     .desc = "The course moves when the pointer touches the edge of the screen, as in the original."},
    {.type = OT_SLIDER, .label = "Key pan speed", .key = "camera.pan_speed", .def = "900", .lo = 300, .hi = 2400, .step = 100,
     .unit = " px/s", .desc = "How fast the pan keys move the course."},
    {.type = OT_HEAD, .label = "Keys for each action"},
};

typedef struct {
  const char *name;
  UiIcon icon;
  const Row *rows;
  int n;
  int keys;   // the key bindings follow the rows (every action of keymap.c, two keys each)
} Page;
static const Row k_weather[] = {
    {.type = OT_TOGGLE, .label = "Current weather display", .key = "environment.report_icon", .def = "1",
     .desc = "Show current weather, temperature and humidity beside the status bar."},
    {.type = OT_SEG, .label = "Temperature units", .key = "environment.temperature_units", .def = "fahrenheit",
     .n = 2, .vals = {"fahrenheit", "celsius"}, .names = {"Fahrenheit", "Celsius"},
     .desc = "Units for the course's simulated current temperature."},
    {.type = OT_HEAD, .label = "Course atmosphere"},
    {.type = OT_TOGGLE, .label = "Lighting and weather", .key = "environment.enabled", .def = "0",
     .desc = "Turn on enhanced sunlight, shadows and course weather."},
    {.type = OT_SEG, .label = "Fixed weather", .key = "environment.weather", .def = "clear",
     .n = 4, .vals = {"clear", "cloudy", "rain", "storm"}, .names = {"Clear", "Cloudy", "Rain", "Storm"},
     .desc = "Choose the weather when Regional weather is off."},
    {.type = OT_TOGGLE, .label = "Regional weather", .key = "environment.regional_weather", .def = "0",
     .desc = "Changing weather follows this course's location and season, including sunny breaks."},
    {.type = OT_SEG, .label = "Weather spell length", .key = "environment.weather_minutes", .def = "3",
     .n = 3, .vals = {"2", "3", "5"}, .names = {"Brief", "Normal", "Long"},
     .desc = "Normal spells last roughly two to four active minutes. Season and location set the odds."},
    {.type = OT_SEG, .label = "Rain amount", .key = "environment.rain_strength", .def = "0.65",
     .n = 4, .vals = {"0", "0.25", "0.65", "1"}, .names = {"Off", "Light", "Steady", "Downpour"},
     .desc = "Changes droplet density, splashes and recorded rain ambience."},
    {.type = OT_SEG, .label = "Cloud cover", .key = "environment.cloud_strength", .def = "0.65",
     .n = 4, .vals = {"0", "0.35", "0.65", "1"}, .names = {"Off", "Scattered", "Cloudy", "Overcast"},
     .desc = "Moving clouds and soft cloud shadows across the course."},
    {.type = OT_SEG, .label = "Time of day", .key = "environment.hour", .def = "14",
     .n = 4, .vals = {"8", "14", "17", "0"}, .names = {"Morning", "Day", "Evening", "Night"},
     .desc = "Fixed time when the day and night cycle is off."},
    {.type = OT_TOGGLE, .label = "Day and night cycle", .key = "environment.day_night", .def = "0",
     .desc = "30 minutes of playing time: 20 minutes day, 10 minutes night. Pause holds the light; game speed does not change it."},
    {.type = OT_TOGGLE, .label = "Morning mist", .key = "environment.mist", .def = "1",
     .desc = "Patchy mist on cool humid mornings, clearing as the day warms."},
    {.type = OT_TOGGLE, .label = "Atmospheric haze", .key = "environment.haze", .def = "1",
     .desc = "A light seasonal veil over the course, keeping the background black."},
    {.type = OT_TOGGLE, .label = "Wind gusts", .key = "environment.gusts", .def = "1",
     .desc = "Coordinated gusts move rain, flag cloth and enhanced water."},
    {.type = OT_TOGGLE, .label = "Golden-hour light", .key = "environment.golden_hour", .def = "1",
     .desc = "Warmer low sunlight at dawn and late afternoon."},
    {.type = OT_TOGGLE, .label = "Distant storms", .key = "environment.distant_thunder", .def = "1",
     .desc = "Quiet distant thunder as storm clouds gather before rain."},
    {.type = OT_HEAD, .label = "Lighting and night"},
    {.type = OT_SLIDER, .label = "Sunlight clarity", .key = "environment.readability", .def = "0.8", .lo=0, .hi=1,
     .desc = "Reveal detail in shaded trees and retain color in warm sunlight."},
    {.type = OT_SLIDER, .label = "Scenery reflections", .key = "environment.reflections", .def = "0.44", .lo=0, .hi=0.7,
     .desc = "Nearby buildings, trees and golfers mirrored in water."},
    {.type = OT_TOGGLE, .label = "Rainbows after showers", .key = "environment.rainbows", .def = "0",
     .desc = "Experimental distant arc beyond the course as rain clears. Off by default."},
    {.type = OT_TOGGLE, .label = "Terrain lighting", .key = "environment.lighting", .def = "1",
     .desc = "Light the slopes and shadows of the course."},
    {.type = OT_TOGGLE, .label = "Tree and golfer shadows", .key = "environment.cast_shadows", .def = "1",
     .desc = "Soft shadows from trees, golfers and building shapes follow the sun."},
    {.type = OT_TOGGLE, .label = "Black background", .key = "environment.black_background", .def = "1",
     .desc = "Keep the space beyond the course black, including during lightning."},
    {.type = OT_TOGGLE, .label = "Glowbugs", .key = "environment.glowbugs", .def = "1",
     .desc = "Small drifting lights over the ground at night."},
    {.type = OT_TOGGLE, .label = "Lightning", .key = "environment.lightning", .def = "1",
     .desc = "Storm flashes. Thunder can still be heard when flashes are off."},
    {.type = OT_TOGGLE, .label = "Reduced flashes", .key = "environment.reduced_flashes", .def = "1",
     .desc = "Keep lightning subtle. Turn Lightning off to remove flashes completely."},
    {.type = OT_HEAD, .label = "Golfers and sound"},
    {.type = OT_TOGGLE, .label = "Weather affects golfers", .key = "environment.gameplay", .def = "0",
     .desc = "Rain softens ball roll and affects the mood of arriving golfers."},
    {.type = OT_TOGGLE, .label = "Weather sound", .key = "environment.sound", .def = "1",
     .desc = "Rain, wind, night insects and delayed thunder. Uses your sound-effects volume."},
    {.type = OT_SLIDER, .label = "Weather volume", .key = "environment.weather_volume", .def = "50",
     .lo = 0, .hi = 100, .step = 5, .unit = "%", .desc = "Balance weather against the rest of the game."},
    {.type = OT_TOGGLE, .label = "Enhanced atmosphere", .key = "environment.shader", .def = "1",
     .desc = "Detailed moving clouds and rain. Turn off for simpler effects."},
};
static const Page k_pages[] = {
    {"Graphics", UI_ICON_PALETTE, k_graphics, (int)(sizeof k_graphics / sizeof k_graphics[0])},
    {"Gameplay", UI_ICON_FLAG, k_gameplay, (int)(sizeof k_gameplay / sizeof k_gameplay[0])},
    {"Controls", UI_ICON_GEAR, k_controls, (int)(sizeof k_controls / sizeof k_controls[0]), 1},
    {"Audio", UI_ICON_BELL, k_audio, (int)(sizeof k_audio / sizeof k_audio[0])},
    {"Weather", UI_ICON_PALETTE, k_weather, (int)(sizeof k_weather / sizeof k_weather[0])},
};
#define NPAGES ((int)(sizeof k_pages / sizeof k_pages[0]))

// ------------------------------------------------------------------ state

static volatile int g_open, g_close_req, g_registered, g_native = -1;
static volatile int g_listen_a = -1, g_listen_slot;   // a key box is waiting for its key
static volatile int g_conf_a = -1, g_conf_slot, g_conf_other, g_conf_oslot;   // a key that another action has
static char g_conf_combo[48];
static volatile int g_scroll_req;                      // arrow and page keys, in rows
static struct { int a, slot; UiRect r; } g_klog[40];
static int g_nklog;
static UiRect g_confr[3];   // Swap, Replace, Cancel while a conflict waits (the layout log)
static volatile int g_in_course;   // the course view is showing (render thread)
static volatile double g_note_until;
static char g_note_title[64], g_note_line[160];
static int g_tab;
static UiRect g_mods_btn;   // the Mods button in the header (the layout log)
static double g_open_t;
static int g_paused_by_us;

// render thread
static UiCtx *g_ui;
static int g_failed;
static volatile float g_view_x, g_view_y, g_view_px = 1;
static volatile int g_seen_draw;
static char g_initial[NPAGES][40][48];   // each row's value when the page was first drawn (restart notes)
static int g_initial_done;
enum { OPTIONS_LOG_ROWS = 32 };
static UiRect g_ctl_log[OPTIONS_LOG_ROWS][8];   // each logged row's control pieces (segments, the switch), for the layout log
static int g_ctl_n[OPTIONS_LOG_ROWS];

void opt_open(void) {
  g_open = 1;
  g_close_req = 0;
  g_open_t = host_now_ms();
}
void opt_close(void) { g_close_req = 1; }
int opt_is_open(void) { return g_open; }

int opt_native_on(void) {
  static unsigned seen = ~0u;
  unsigned ser = mods_serial();
  if (ser != seen) {
    seen = ser;
    const char *v = mods_get("ui.options");
    g_native = !(v && !strcasecmp(v, "original"));
  }
  return g_native && g_registered;
}

// ------------------------------------------------------------------ values

#define PREF_WORD 0x5a5a00

static void row_value(const Row *r, char *out, size_t n) {
  const char *v = NULL;
  if (r->type == OT_BIT) {
    snprintf(out, n, "%d", (G32(PREF_WORD) & r->bit) != 0);
    return;
  }
  if (r->special == SP_TEXTURES) {
    v = hd_art_textures_name();
  } else if (r->special == SP_PROFILE) {
    v = km_modern() ? "modern" : "original";
  } else {
    v = mods_get(r->key);
    if ((!v || !*v) && r->ini) {
      char k[96];
      snprintf(k, sizeof k, "golf.%s", r->ini);
      v = mods_get(k);
    }
    if (!v || !*v) v = r->def;
  }
  snprintf(out, n, "%s", v ? v : "");
}

// 1 when both are the same word, or the same number ("1" and "1.0")
static int val_eq(const char *a, const char *b) {
  if (!strcasecmp(a, b)) return 1;
  char *ea, *eb;
  double x = strtod(a, &ea), y = strtod(b, &eb);
  return ea != a && eb != b && !*ea && !*eb && x == y;
}

static int is_on(const char *v) { return v && (atoi(v) != 0 || !strcasecmp(v, "on") || !strcasecmp(v, "true")); }

static int g_slider_dirty;
static int g_staff_slider_dirty;
static uint32_t g_slider_id;

static void set_value(const Row *r, const char *v) {
  if (r->key && !strcmp(r->key, "gameplay.manual_clubhouse")) {
    mods_set(r->key, v);
    fprintf(stderr, "options: manual_clubhouse %s mods.ini %s\n", v, mods_ini_save() ? "saved" : "write failed");
    return;
  }
  if (r->type == OT_SLIDER) {   // dragged: the store follows at once, the file when the button is let go
    if (!strcmp(r->key, "gameplay.staff_max")) {
      mods_set(r->key, v);
      g_staff_slider_dirty = g_slider_dirty = 1;
      return;
    }
    mods_option_put(r->key, v);
    g_slider_dirty = 1;
    return;
  }
  if (r->type == OT_BIT) {   // the game's own preferences word: atomic, the game thread may flip a bit too
    if (is_on(v)) InterlockedOr((volatile LONG *)(uintptr_t)PREF_WORD, r->bit);
    else InterlockedAnd((volatile LONG *)(uintptr_t)PREF_WORD, ~r->bit);
    fprintf(stderr, "options: game preference %d = %s\n", r->bit, v);
    return;
  }
  if (r->special == SP_PROFILE) {   // every key goes back to that profile's own
    km_reset(!strcmp(v, "modern"));
    fprintf(stderr, "options: key set = %s\n", v);
    return;
  }
  if (mods_option_set(r->key, v)) fprintf(stderr, "options: %s = %s\n", r->key, v);
  else fprintf(stderr, "options: %s = %s (not saved)\n", r->key, v);
}

// 0 hd, 1 pixel_hd, 2 original: the next look after the current one
void opt_cycle_textures(void) {
  static const char *const order[] = {"hd", "pixel_hd", "original"};
  static const char *const names[] = {"HD", "Pixel HD", "Original"};
  const char *cur = hd_art_textures_name();
  int i = 0;
  for (int k = 0; k < 3; k++)
    if (!strcmp(cur, order[k])) i = k;
  int next = (i + 1) % 3;
  mods_option_set("art.textures", order[next]);
  snprintf(g_note_title, sizeof g_note_title, "Textures: %s", names[next]);
  snprintf(g_note_line, sizeof g_note_line, "F11 switches the look");
  g_note_until = host_now_ms() + 2200;
  fprintf(stderr, "options: textures now %s\n", order[next]);
}

// ------------------------------------------------------------------ tokens and scale (as the front end)

void snd_native_set_volumes(float master, float music, float effects);   // platform/sound_native.cpp

// The volumes in the store reach the mixer (render thread, every frame the layer is called; cheap).
static void sync_audio(void) {
  static unsigned seen = ~0u;
  if (mods_serial() == seen) return;
  seen = mods_serial();
  double m = mods_get_num("audio.master", 100), mu = mods_get_num("audio.music", 100), e = mods_get_num("audio.effects", 100);
  if (mods_get_int("audio.mute", 0)) m = 0;
  m = m < 0 ? 0 : m > 100 ? 100 : m;
  mu = mu < 0 ? 0 : mu > 100 ? 100 : mu;
  e = e < 0 ? 0 : e > 100 ? 100 : e;
  // a squared curve: the middle of the slider sounds like the middle
  snd_native_set_volumes((float)(m * m / 10000), (float)(mu * mu / 10000), (float)(e * e / 10000));
}

static void load_tokens(UiTokens *t) {
  char base[1024];
  snprintf(base, sizeof base, "%s", port_host_path("/ui/frosted_mint.toml"));
  static char paths[16][1024];
  const char *ov[16];
  int n = 0;
  for (int i = 0; i < mods_count() && n < 16; i++) {
    const char *dir = mods_dir(mods_id_at(i));
    if (!dir) continue;
    snprintf(paths[n], sizeof paths[n], "%s/ui/frosted_mint.toml", dir);
    FILE *f = fopen(paths[n], "rb");
    if (!f) continue;
    fclose(f);
    ov[n] = paths[n];
    n++;
  }
  ui_tokens_load_all(t, base, ov, n);
}

static float page_scale(int w, int h) {
  double s = mods_get_num("ui.scale", 0);
  float base = (s >= 0.5 && s <= 4) ? (float)s : ui_scale_for_height(h);
  float fit = fminf(w / 1400.0f, h / 900.0f);   // the page's design size always fits
  if (base > fit) base = fit;
  return base < 0.3f ? 0.3f : base;
}

// ------------------------------------------------------------------ drawing

static void log_layout(int w, int h, const UiRect *close, const UiRect *tabs, int ntabs, const UiRect *rows, int nrows,
                       const UiRect *done, const UiRect *reset, double now) {
  static int on = -1;
  static double last;
  if (on < 0) on = getenv("SIMGOLF_FRONTEND_LOG") != NULL || getenv("SIMGOLF_OPTIONS_LOG") != NULL;
  if (!on || now - last < 1000) return;
  last = now;
  char line[4096];
  int o = snprintf(line, sizeof line, "options: layout tab %d target %dx%d", g_tab, w, h);
#define PUT(nm, r) \
  if ((r)->w > 0) o += snprintf(line + o, sizeof line - (size_t)o, " %s %.0f,%.0f", nm, (r)->x + (r)->w / 2, (r)->y + (r)->h / 2)
  PUT("close", close);
  PUT("done", done);
  PUT("reset", reset);
  PUT("mods", &g_mods_btn);
  for (int i = 0; i < ntabs && o < 1500; i++) {
    char nm[16];
    snprintf(nm, sizeof nm, "tab%d", i);
    PUT(nm, &tabs[i]);
  }
  for (int i = 0; i < nrows && o < 3200; i++) {
    char nm[24];
    snprintf(nm, sizeof nm, "row%d", i);
    PUT(nm, &rows[i]);
    for (int k = 0; i < OPTIONS_LOG_ROWS && k < g_ctl_n[i]; k++) {
      snprintf(nm, sizeof nm, "row%ds%d", i, k);
      PUT(nm, &g_ctl_log[i][k]);
    }
  }
#undef PUT
  snprintf(line + o, sizeof line - (size_t)o, " end");   // the tests wait for the whole line
  fprintf(stderr, "%s\n", line);
}

// One row's control. Returns the new value (a static string) when the player changed it, else NULL.
static void note_segments(UiCtx *c, const Row *r, UiRect sr, int slot) {
  if (slot < 0 || slot >= OPTIONS_LOG_ROWS) return;
  float pad = ui_s(c, 3), gap = ui_s(c, 2);
  float nat = ui_segmented_width(c, r->names, NULL, r->n);
  float extra = sr.w > nat ? (sr.w - nat) / r->n : 0;
  float x = sr.x + pad;
  for (int i = 0; i < r->n && i < 8; i++) {
    float w = ui_segmented_width(c, &r->names[i], NULL, 1) - ui_s(c, 6) + extra;
    g_ctl_log[slot][i] = ui_rect(x, sr.y + pad, w, sr.h - 2 * pad);
    x += w + gap;
  }
  g_ctl_n[slot] = r->n < 8 ? r->n : 8;
  if (r->key && !strcmp(r->key, "gameplay.manual_clubhouse") && getenv("SIMGOLF_OPTIONS_LOG")) {
    static uint32_t last; uint32_t now = host_now_ms();
    if (now - last > 1000) {
      UiRect scr = ui_screen(c), a = g_ctl_log[slot][0], b = g_ctl_log[slot][1];
      fprintf(stderr, "options: clubhouse target %dx%d automatic %d,%d manual %d,%d\n", (int)scr.w, (int)scr.h,
        (int)(a.x+a.w/2), (int)(a.y+a.h/2), (int)(b.x+b.w/2), (int)(b.y+b.h/2));
      last = now;
    }
  }
}

static const char *draw_control(UiCtx *c, uint32_t id, const Row *r, UiRect cr, const char *cur, int slot) {
  static char out[48];
  const UiTokens *t = ui_tokens(c);
  (void)t;
  if (r->type == OT_SEG) {
    int sel = -1;
    for (int i = 0; i < r->n; i++)
      if (val_eq(cur, r->vals[i])) sel = i;
    float w = ui_segmented_width(c, r->names, NULL, r->n);
    float cw = fminf(cr.w, fmaxf(w, ui_s(c, 90) * r->n));
    UiRect sr = ui_rect(cr.x + cr.w - cw, cr.y + (cr.h - ui_s(c, 38)) / 2, cw, ui_s(c, 38));
    int before = sel;
    note_segments(c, r, sr, slot);
    if (ui_segmented(c, id, sr, r->names, NULL, r->n, &sel) && sel != before && sel >= 0) {
      snprintf(out, sizeof out, "%s", r->vals[sel]);
      return out;
    }
  } else if (r->type == OT_TOGGLE || r->type == OT_BIT) {
    int on = is_on(cur);
    if (slot >= 0 && slot < OPTIONS_LOG_ROWS) {
      g_ctl_log[slot][0] = ui_rect(cr.x + cr.w - ui_s(c, 44), cr.y + (cr.h - ui_s(c, 26)) / 2, ui_s(c, 44), ui_s(c, 26));
      g_ctl_n[slot] = 1;
    }
    if (ui_toggle(c, id, cr, &on)) {
      snprintf(out, sizeof out, "%d", on);
      return out;
    }
  } else if (r->type == OT_SLIDER) {
    double v = atof(cur);
    float norm = r->hi > r->lo ? (float)((v - r->lo) / (r->hi - r->lo)) : 0;
    norm = norm < 0 ? 0 : norm > 1 ? 1 : norm;
    char txt[24];
    snprintf(txt, sizeof txt, r->hi<=1?"%.2f%s":"%.0f%s", v, r->unit ? r->unit : "");
    UiRect sr = ui_rect(cr.x + cr.w - ui_s(c, 300), cr.y, ui_s(c, 300), cr.h);
    if (slot >= 0 && slot < OPTIONS_LOG_ROWS) {   // the two ends of the track, for the tests
      float tw = sr.w - ui_s(c, 56);
      g_ctl_log[slot][0] = ui_rect(sr.x, sr.y, 1, sr.h);
      g_ctl_log[slot][1] = ui_rect(sr.x + tw, sr.y, 1, sr.h);
      g_ctl_n[slot] = 2;
    }
    if (ui_slider(c, id, sr, &norm, txt)) {
      double nv = r->lo + norm * (r->hi - r->lo);
      if (r->step > 0) nv = floor(nv / r->step + 0.5) * r->step;
      snprintf(out, sizeof out, "%g", nv);
      if (strcmp(out, cur)) return out;
    }
  }
  return NULL;
}

static void draw_page(UiCtx *c, int w, int h, double now) {
  const UiTokens *t = ui_tokens(c);
  UiRect scr = ui_screen(c);
  // the scrim and the panel
  uid_rrect(ui_drawer(c), scr, 0, t->scrim);
  ui_hit(c, scr, 0, 0, UI_HIT_BLOCK | UI_HIT_MODAL);
  float pw = fminf(ui_s(c, 1120), scr.w - 2 * ui_s(c, 20)), ph = fminf(ui_s(c, 900), scr.h - 2 * ui_s(c, 20));
  UiRect p = ui_rect_round(ui_center(scr, pw, ph));
  ui_panel(c, p, ui_s(c, t->radius_panel), UI_PANEL_STRONG);
  float pad = ui_s(c, 30);
  UiRect in = ui_inset_all(p, pad);

  // header: title, close
  UiFont tf = ui_font(c, UI_TYPE_TITLE);
  float hh = ui_s(c, 44);
  ui_text_in(c, ui_rect(in.x, in.y, in.w - hh - ui_s(c, 12), hh), UI_TYPE_TITLE, t->text_primary, "Options", 0);
  (void)tf;
  UiRect closer = ui_rect(in.x + in.w - hh, in.y, hh, hh);
  int do_close = ui_icon_button(c, ui_id("opt.close"), closer, UI_ICON_X, UI_ROUND, "Close (Esc)");
  {   // task-22: the Mods screen, one click from here (the wrench menu's Options... item is how a running game gets here)
    UiRect mb = ui_rect(closer.x - ui_s(c, 14) - ui_s(c, 150), in.y, ui_s(c, 150), hh);
    g_mods_btn = mb;
    if (ui_button(c, ui_id("opt.mods"), mb, "Mods", UI_ICON_SPARKLE, UI_BTN_PILL, 0)) {
      modsp_open();
      do_close = 1;
    }
  }

  // tabs
  float ty = in.y + hh + ui_s(c, 14);
  const char *tab_names[8];
  UiIcon tab_icons[8];
  for (int i = 0; i < NPAGES; i++) tab_names[i] = k_pages[i].name, tab_icons[i] = k_pages[i].icon;
  float tw = fminf(in.w, ui_segmented_width(c, tab_names, tab_icons, NPAGES) + ui_s(c, 40) * NPAGES);
  UiRect tabr = ui_rect(in.x, ty, tw, ui_s(c, 44));
  ui_segmented(c, ui_id("opt.tabs"), tabr, tab_names, tab_icons, NPAGES, &g_tab);
  UiRect tabs[8] = {0};
  {   // where each tab is, for the layout log (the kit's own widths: the label's, plus an equal share of the rest)
    float nat = ui_segmented_width(c, tab_names, tab_icons, NPAGES);
    float extra = tabr.w > nat ? (tabr.w - nat) / NPAGES : 0, x = tabr.x + ui_s(c, 3);
    for (int i = 0; i < NPAGES; i++) {
      float w = ui_segmented_width(c, &tab_names[i], &tab_icons[i], 1) - ui_s(c, 6) + extra;
      tabs[i] = ui_rect(x, tabr.y, w, tabr.h);
      x += w + ui_s(c, 2);
    }
  }

  // footer
  float fh = ui_s(c, 48);
  UiRect foot = ui_rect(in.x, in.y + in.h - fh, in.w, fh);
  UiRect done = ui_rect(foot.x + foot.w - ui_s(c, 130), foot.y, ui_s(c, 130), fh);
  UiRect reset = ui_rect(done.x - ui_s(c, 16) - ui_s(c, 220), foot.y, ui_s(c, 220), fh);
  if (ui_button(c, ui_id("opt.done"), done, "Done", UI_ICON_CHECK, UI_BTN_PRIMARY, 0)) do_close = 1;
  const Page *pg = &k_pages[g_tab];
  int do_reset = ui_button(c, ui_id("opt.reset"), reset, "Reset this tab", UI_ICON_UNDO, UI_BTN_PILL, 0);

  // body
  UiRect body = ui_rect(in.x, ty + ui_s(c, 44) + ui_s(c, 18), in.w, foot.y - ui_s(c, 14) - (ty + ui_s(c, 44) + ui_s(c, 18)));
  float row_h = ui_s(c, 76), head_h = ui_s(c, 46), gap = ui_s(c, 8);
  float content = 0;
  for (int i = 0; i < pg->n; i++) content += (pg->rows[i].type == OT_HEAD ? head_h : row_h) + gap;
  float key_h = ui_s(c, 64);
  if (pg->keys) {
    const char *grp = "";
    for (int a = 0; a < km_count(); a++) {
      if (strcmp(grp, km_group(a))) content += head_h + gap, grp = km_group(a);
      content += key_h + gap;
    }
  }
  uint32_t scroll_id = ui_idi(ui_id("opt.scroll"), g_tab);
  if (g_scroll_req) {   // the arrow and page keys
    *ui_state(c, scroll_id) += (float)g_scroll_req * ui_s(c, 70);
    g_scroll_req = 0;
  }
  float off = ui_vscroll_begin(c, scroll_id, body, content, ui_s(c, 70));
  float y = body.y + off;
  UiRect rows_log[OPTIONS_LOG_ROWS];
  int nrows_log = 0;
  int restart_pending = 0;
  const char *changed_val = NULL;
  const Row *changed_row = NULL;
  float rw = body.w - ui_s(c, 6);
  for (int i = 0; i < pg->n; i++) {
    const Row *r = &pg->rows[i];
    if (r->type == OT_HEAD) {
      ui_text_in(c, ui_rect(body.x + ui_s(c, 6), y + ui_s(c, 14), rw, ui_s(c, 24)), UI_TYPE_HEADING, t->text_primary, r->label, 0);
      y += head_h + gap;
      continue;
    }
    UiRect rr = ui_rect(body.x, y, rw, row_h);
    uid_rrect(ui_drawer(c), rr, ui_s(c, t->radius_tile), t->surface_inset);
    uid_rrect_stroke(ui_drawer(c), rr, ui_s(c, t->radius_tile), fmaxf(1.0f, ui_s(c, 1)), t->surface_inset_border);
    if (nrows_log < OPTIONS_LOG_ROWS) rows_log[nrows_log++] = rr;
    char cur[48];
    row_value(r, cur, sizeof cur);
    float cw = ui_s(c, r->type == OT_TOGGLE || r->type == OT_BIT ? 60 : 470);
    UiRect cr = ui_rect(rr.x + rr.w - cw - ui_s(c, 22), rr.y, cw, rr.h);
    float lw = cr.x - rr.x - ui_s(c, 40);
    // the label, the line under it, and a tag when the change waits for a restart
    const char *desc = r->desc;
    if (r->type == OT_SEG)
      for (int k = 0; k < r->n; k++)
        if (r->vdesc[k] && val_eq(cur, r->vals[k])) desc = r->vdesc[k];
    int waits = 0;
    if (r->restart) {
      const char *init = g_initial[g_tab][i];
      waits = strcmp(init, cur) != 0;
      restart_pending |= waits;
    }
    float lx = rr.x + ui_s(c, 22);
    float lblw = ui_text_width(c, UI_TYPE_BODY, r->label);
    ui_text_in(c, ui_rect(lx, rr.y + ui_s(c, 12), fminf(lw, lblw + 2), ui_s(c, 24)), UI_TYPE_BODY, t->text_primary, r->label, 0);
    if (waits) {
      const char *tag = "Applies after restart";
      float tgw = ui_text_width(c, UI_TYPE_MICRO, tag) + ui_s(c, 20);
      UiRect tg = ui_rect(lx + lblw + ui_s(c, 12), rr.y + ui_s(c, 14), tgw, ui_s(c, 20));
      uid_rrect(ui_drawer(c), tg, tg.h / 2, t->accent_soft);
      ui_text_in(c, tg, UI_TYPE_MICRO, t->accent_soft_text, tag, 0.5f);
    } else if (r->restart) {
      const char *tag = "Needs restart";
      float tgw = ui_text_width(c, UI_TYPE_MICRO, tag) + ui_s(c, 20);
      UiRect tg = ui_rect(lx + lblw + ui_s(c, 12), rr.y + ui_s(c, 14), tgw, ui_s(c, 20));
      uid_rrect(ui_drawer(c), tg, tg.h / 2, uid_alpha(t->text_primary, 0.07f));
      ui_text_in(c, tg, UI_TYPE_MICRO, t->text_secondary, tag, 0.5f);
    }
    if (desc) ui_text_fit(c, ui_rect(lx, rr.y + ui_s(c, 40), lw, ui_s(c, 22)), UI_TYPE_CAPTION, t->text_secondary, desc, 0);
    const char *nv = NULL;
    if (r->course && !g_in_course) {   // a setting of the running game: nothing to change on the start menu
      UiRect nr = ui_rect(cr.x - ui_s(c, 140), cr.y, cr.w + ui_s(c, 140), cr.h);
      ui_text_in(c, nr, UI_TYPE_CAPTION, t->text_secondary, "Open a game to change this", 1);
      if (nrows_log > 0) g_ctl_n[nrows_log - 1] = 0;
    } else {
      nv = draw_control(c, ui_idi(ui_id("opt.ctl"), g_tab * 64 + i), r, cr, cur, nrows_log - 1);
    }
    if (nv && !changed_val) {
      changed_val = nv, changed_row = r;
      if (r->type == OT_SLIDER) g_slider_id = ui_idi(ui_id("opt.ctl"), g_tab * 64 + i);
    }
    y += row_h + gap;
  }
  g_nklog = 0;
  if (pg->keys) {
    const char *grp = "";
    for (int a = 0; a < km_count(); a++) {
      if (strcmp(grp, km_group(a))) {
        grp = km_group(a);
        ui_text_in(c, ui_rect(body.x + ui_s(c, 6), y + ui_s(c, 14), rw, ui_s(c, 24)), UI_TYPE_HEADING, t->text_primary, grp, 0);
        y += head_h + gap;
      }
      UiRect rr = ui_rect(body.x, y, rw, key_h);
      y += key_h + gap;
      if (rr.y + rr.h < body.y || rr.y > body.y + body.h) continue;   // out of view
      uid_rrect(ui_drawer(c), rr, ui_s(c, t->radius_tile), t->surface_inset);
      uid_rrect_stroke(ui_drawer(c), rr, ui_s(c, t->radius_tile), fmaxf(1.0f, ui_s(c, 1)), t->surface_inset_border);
      float bw = ui_s(c, 190), bh = ui_s(c, 38);
      UiRect b[2] = {ui_rect(rr.x + rr.w - ui_s(c, 22) - 2 * bw - ui_s(c, 12), rr.y + (rr.h - bh) / 2, bw, bh),
                     ui_rect(rr.x + rr.w - ui_s(c, 22) - bw, rr.y + (rr.h - bh) / 2, bw, bh)};
      char txt[2][48];
      const char *share = NULL;
      for (int sl = 0; sl < KM_SLOTS; sl++) {
        km_binding(a, sl, txt[sl], sizeof txt[sl]);
        int osl, o = txt[sl][0] ? km_find(txt[sl], a, &osl) : -1;
        if (o >= 0 && !share) share = km_label(o);
      }
      float lx = rr.x + ui_s(c, 22), lw = b[0].x - lx - ui_s(c, 16);
      ui_text_fit(c, ui_rect(lx, rr.y + (share ? ui_s(c, 10) : ui_s(c, 18)), lw, ui_s(c, 24)), UI_TYPE_BODY, t->text_primary,
                  km_label(a), 0);
      if (share) {
        char note[96];
        snprintf(note, sizeof note, "Also used by: %s", share);
        ui_text_fit(c, ui_rect(lx, rr.y + ui_s(c, 36), lw, ui_s(c, 20)), UI_TYPE_CAPTION, t->danger, note, 0);
      }
      for (int sl = 0; sl < KM_SLOTS; sl++) {
        int listening = g_listen_a == a && g_listen_slot == sl;
        const char *label = listening ? "Press a key..." : txt[sl][0] ? txt[sl] : "Not set";
        if (ui_button(c, ui_idi(ui_id("opt.key"), a * 2 + sl), b[sl], label, UI_ICON_NONE, UI_BTN_PILL,
                      listening ? UI_SELECTED : 0)) {
          g_listen_a = a;
          g_listen_slot = sl;
          g_conf_a = -1;
          fprintf(stderr, "options: key box %s slot %d is listening\n", km_id(a), sl);
        }
        if (g_nklog < 40) g_klog[g_nklog].a = a, g_klog[g_nklog].slot = sl, g_klog[g_nklog++].r = b[sl];
      }
    }
  }
  ui_vscroll_end(c);

  // a key another action has: the choice
  int do_swap = 0, do_replace = 0, do_cancel = 0;
  if (g_conf_a >= 0) {
    float bw = ui_s(c, 104), bx = reset.x - ui_s(c, 16) - 3 * bw - 2 * ui_s(c, 8);
    char msg[160];
    snprintf(msg, sizeof msg, "%s is already used by %s.", g_conf_combo, km_label(g_conf_other));
    ui_text_fit(c, ui_rect(foot.x, foot.y, bx - foot.x - ui_s(c, 12), fh), UI_TYPE_CAPTION, t->danger, msg, 0);
    g_confr[0] = ui_rect(bx, foot.y, bw, fh);
    g_confr[1] = ui_rect(bx + bw + ui_s(c, 8), foot.y, bw, fh);
    g_confr[2] = ui_rect(bx + 2 * (bw + ui_s(c, 8)), foot.y, bw, fh);
    do_swap = ui_button(c, ui_id("opt.swap"), ui_rect(bx, foot.y, bw, fh), "Swap", UI_ICON_NONE, UI_BTN_PILL, 0);
    do_replace = ui_button(c, ui_id("opt.replace"), ui_rect(bx + bw + ui_s(c, 8), foot.y, bw, fh), "Replace", UI_ICON_NONE, UI_BTN_PILL, 0);
    do_cancel = ui_button(c, ui_id("opt.cancel"), ui_rect(bx + 2 * (bw + ui_s(c, 8)), foot.y, bw, fh), "Cancel", UI_ICON_NONE, UI_BTN_PILL, 0);
  } else if (pg->keys) {
    const char *help = g_listen_a >= 0 ? "Press the new key (with Ctrl, Shift or Alt if you like). Backspace clears it, Esc cancels."
                                       : "Click a key box, then press the key. Two keys per action.";
    ui_text_fit(c, ui_rect(foot.x, foot.y, reset.x - foot.x - ui_s(c, 16), fh), UI_TYPE_CAPTION, t->text_secondary, help, 0);
  } else if (restart_pending)
    ui_text_in(c, ui_rect(foot.x, foot.y, reset.x - foot.x - ui_s(c, 16), fh), UI_TYPE_CAPTION, t->text_secondary,
               "Some changes apply the next time you start the game.", 0);

  if (changed_row) set_value(changed_row, changed_val);
  if (g_slider_dirty && !ui_is_active(c, g_slider_id)) {
    g_slider_dirty = 0;
    mods_option_flush();
    if (g_staff_slider_dirty) {
      g_staff_slider_dirty = 0;
      fprintf(stderr, "options: staff_max mods.ini %s\n", mods_ini_save() ? "saved" : "write failed");
    }
    fprintf(stderr, "options: volume or slider settled\n");
  }
  if (do_swap || do_replace) {
    char old[48];
    km_binding(g_conf_a, g_conf_slot, old, sizeof old);
    km_set(g_conf_a, g_conf_slot, g_conf_combo);
    km_set(g_conf_other, g_conf_oslot, do_swap ? old : "");
    fprintf(stderr, "options: key %s moved from %s to %s (%s)\n", g_conf_combo, km_id(g_conf_other), km_id(g_conf_a),
            do_swap ? "swap" : "replace");
  }
  if (do_swap || do_replace || do_cancel) g_conf_a = -1;
  if (do_reset) {
    if (pg->keys) {
      km_reset(km_modern());
      g_conf_a = -1;
    }
    for (int i = 0; i < pg->n; i++) {
      const Row *r = &pg->rows[i];
      if (r->type == OT_HEAD || (r->course && !g_in_course)) continue;
      if (r->type == OT_BIT) { set_value(r, "1"); continue; }
      set_value(r, r->def);
    }
  }
  if (do_close) g_close_req = 1;
  log_layout(w, h, &closer, tabs, NPAGES, rows_log, nrows_log, &done, &reset, now);
  if (g_tab == 2) {   // where the key boxes in view are, for the tests (the Controls tab)
    static int on = -1;
    static double last;
    if (on < 0) on = getenv("SIMGOLF_FRONTEND_LOG") != NULL || getenv("SIMGOLF_OPTIONS_LOG") != NULL;
    if (on && now - last > 1000) {
      last = now;
      char line[3000];
      int o = snprintf(line, sizeof line, "options: keys target %dx%d", w, h);
      for (int i = 0; i < g_nklog && o < 2700; i++) {
        char cur[48];
        km_binding(g_klog[i].a, g_klog[i].slot, cur, sizeof cur);
        for (char *q = cur; *q; q++)
          if (*q == ' ') *q = '_';
        o += snprintf(line + o, sizeof line - (size_t)o, " %s/%d/%s %.0f,%.0f", km_id(g_klog[i].a), g_klog[i].slot,
                      cur[0] ? cur : "none", g_klog[i].r.x + g_klog[i].r.w / 2, g_klog[i].r.y + g_klog[i].r.h / 2);
      }
      if (g_conf_a >= 0) {
        static const char *const nm[3] = {"swap", "replace", "cancel"};
        for (int i = 0; i < 3; i++)
          o += snprintf(line + o, sizeof line - (size_t)o, " conf_%s/0/none %.0f,%.0f", nm[i], g_confr[i].x + g_confr[i].w / 2,
                        g_confr[i].y + g_confr[i].h / 2);
      }
      snprintf(line + o, sizeof line - (size_t)o, " end");
      fprintf(stderr, "%s\n", line);
    }
  }
}

static void draw_note(UiCtx *c, double now) {
  UiRect scr = ui_screen(c);
  float nw = ui_s(c, 380);
  float nh = ui_toast_height(c, nw, g_note_title, g_note_line);
  UiRect r = ui_rect_round(ui_rect((scr.w - nw) / 2, ui_s(c, 36), nw, nh));
  ui_toast(c, r, NULL, g_note_title, g_note_line);
  (void)now;
}

static void draw(const HdUiFrame *f, void *user) {
  (void)user;
  g_seen_draw = 1;
  sync_audio();
  if (g_failed) return;
  double now = host_now_ms();
  if (g_close_req) {
    g_open = 0;
    g_close_req = 0;
  }
  int note = now < g_note_until;
  int mods_open = modsp_is_open();
  if (!g_open && !note && !mods_open) {
    if (g_ui) ui_skip_frame(g_ui);
    return;
  }
  SDL_Renderer *r = f->renderer;
  if (!g_ui) {
    UiTokens t;
    load_tokens(&t);
    char fonts[1024];
    snprintf(fonts, sizeof fonts, "%s", port_host_path("/fonts/plus-jakarta-sans"));
    g_ui = ui_create(r, &t, fonts); ui_debug_name(g_ui, "options");
    if (!g_ui) {
      g_failed = 1;
      return;
    }
    fprintf(stderr, "ui: Options page layer on\n");
  }
  int w = 0, h = 0;
  if (f->target) SDL_QueryTexture(f->target, NULL, NULL, &w, &h);
  else SDL_GetRendererOutputSize(r, &w, &h);
  g_view_x = (float)f->view.x;
  g_view_y = (float)f->view.y;
  g_view_px = f->px > 0 ? f->px : 1;
  g_in_course = f->course;
  if (!g_initial_done) {   // what each row said when the game started: a later difference waits for a restart
    g_initial_done = 1;
    for (int p = 0; p < NPAGES; p++)
      for (int i = 0; i < k_pages[p].n && i < 40; i++)
        if (k_pages[p].rows[i].type != OT_HEAD) row_value(&k_pages[p].rows[i], g_initial[p][i], sizeof g_initial[p][i]);
  }
  ui_begin(g_ui, f->target, w, h, page_scale(w, h), f->target, now);
  if (mods_open) modsp_draw(g_ui, r, w, h, now, g_in_course);
  else if (g_open) draw_page(g_ui, w, h, now);
  if (note) draw_note(g_ui, now);
  ui_end(g_ui);
}

// ------------------------------------------------------------------ input (game thread, before the game sees it)

// A key box is waiting: the next key press is its binding.
static void listen_key(const SDL_Event *e) {
  SDL_Keycode k = e->key.keysym.sym;
  int a = g_listen_a, slot = g_listen_slot;
  if (k == SDLK_ESCAPE) {
    g_listen_a = -1;
    return;
  }
  if (k == SDLK_BACKSPACE || k == SDLK_DELETE) {
    km_set(a, slot, "");
    g_listen_a = -1;
    fprintf(stderr, "options: key %s slot %d cleared\n", km_id(a), slot);
    return;
  }
  char combo[48];
  if (!km_event_combo(e, combo, sizeof combo)) return;   // a modifier alone: wait for the key
  int osl = 0, o = km_find(combo, a, &osl);
  g_listen_a = -1;
  if (o >= 0) {   // another action has it: ask
    snprintf(g_conf_combo, sizeof g_conf_combo, "%s", combo);
    g_conf_slot = slot;
    g_conf_other = o;
    g_conf_oslot = osl;
    g_conf_a = a;
    fprintf(stderr, "options: key %s for %s is used by %s\n", combo, km_id(a), km_id(o));
    return;
  }
  char same[48];   // the action's other box has it: it moves
  km_binding(a, 1 - slot, same, sizeof same);
  if (!strcmp(same, combo)) km_set(a, 1 - slot, "");
  km_set(a, slot, combo);
  fprintf(stderr, "options: key %s = %s slot %d\n", km_id(a), combo, slot);
}

static int input(SDL_Event *e, int course, void *user) {
  (void)course, (void)user;
  if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) {
    SDL_Keycode k = e->key.keysym.sym;
    int down = e->type == SDL_KEYDOWN;
    if (!down) return 0;   // releases go on: a key held when the page opened must not stay down
    if (modsp_is_open()) {   // task-22: the Mods screen takes every key press while it is up
      modsp_key(e);
      return 1;
    }
    if (km_event_is(e, "mods_page")) {
      if (!e->key.repeat) {
        g_close_req = 1;
        modsp_open();
      }
      return 1;
    }
    if (g_open && g_listen_a >= 0) {
      listen_key(e);
      return 1;
    }
    if (km_event_is(e, "textures_cycle")) {   // the Textures look, anywhere (also while the page is open)
      if (!e->key.repeat) opt_cycle_textures();
      return 1;
    }
    if (km_event_is(e, "opt_page")) {   // the page itself (Ctrl+F12 stays the trace dump)
      if (!e->key.repeat) {
        if (g_open) opt_close();
        else opt_open();
      }
      return 1;
    }
    if (!g_open) return 0;
    if (k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LALT || k == SDLK_RALT) return 0;
    if (k == SDLK_ESCAPE) {
      if (g_conf_a >= 0) g_conf_a = -1;
      else opt_close();
    } else if ((k == SDLK_RIGHT || k == SDLK_LEFT) && NPAGES > 1) {
      g_tab = (g_tab + (k == SDLK_RIGHT ? 1 : NPAGES - 1)) % NPAGES;
      g_listen_a = g_conf_a = -1;
    } else if (k == SDLK_DOWN) g_scroll_req += 1;
    else if (k == SDLK_UP) g_scroll_req -= 1;
    else if (k == SDLK_PAGEDOWN) g_scroll_req += 6;
    else if (k == SDLK_PAGEUP) g_scroll_req -= 6;
    else if (k == SDLK_HOME) g_scroll_req = -2000;
    else if (k == SDLK_END) g_scroll_req = 2000;
    return 1;   // the page takes every key press while it is open
  }
  if ((!g_open && !modsp_is_open()) || !g_ui) return 0;
  UiEvent u = {0};
  int cx = 0, cy = 0;
  switch (e->type) {
    case SDL_MOUSEMOTION:
      u.kind = UI_EV_MOVE;
      cx = e->motion.x, cy = e->motion.y;
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      u.kind = e->type == SDL_MOUSEBUTTONDOWN ? UI_EV_DOWN : UI_EV_UP;
      u.button = e->button.button;
      cx = e->button.x, cy = e->button.y;
      break;
    case SDL_MOUSEWHEEL:
      u.kind = UI_EV_WHEEL;
      u.wheel = (float)e->wheel.y;
      break;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_LEAVE || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) ui_input_reset(g_ui);
      return 0;
    default:
      return 0;
  }
  if (u.kind != UI_EV_WHEEL && ((e->type == SDL_MOUSEMOTION && e->motion.windowID) ||
                                (e->type != SDL_MOUSEMOTION && e->button.windowID))) {
    cx = hd_raw_mouse_x;   // a real pointer: the unclamped window position (the page is bigger than the 800x600 picture)
    cy = hd_raw_mouse_y;
  }
  u.x = g_view_x + (cx + 0.5f) * g_view_px;
  u.y = g_view_y + (cy + 0.5f) * g_view_px;
  ui_route_event(g_ui, &u);
  return 1;
}

void opt_register(void) {
  if (g_registered) return;
  g_registered = hd_ui_set_top(draw, input, NULL);
}

// ------------------------------------------------------------------ game thread

// Once per course frame: the game stands still while the page is up (the clock flag, bit 2 of the game flags).
void opt_game_frame(void) {
  const int FLAGS = 0x59e7b8;
  static int labelled;
  static unsigned seen_serial = ~0u;
  if (mods_serial() != seen_serial) {   // Options > Gameplay > Game speed
    seen_serial = mods_serial();
    const char *v = mods_get("gameplay.speed");
    bridge_set_game_speed(v && *v ? atof(v) : 1.0);
  }
  if (!labelled && opt_native_on()) {   // the wrench menu's "Edit Preferences" opens this page now (hd_ui.c's hook)
    labelled = 1;
    memcpy((void *)(uintptr_t)0x4c7c6f, "Options...      ", 16);
  }
  int open = g_open || modsp_is_open();
  if (open && !g_paused_by_us) {
    if (!(G32(FLAGS) & 4)) {
      G32(FLAGS) |= 4;
      g_paused_by_us = 1;
    }
  } else if (!open && g_paused_by_us) {
    G32(FLAGS) &= ~4;
    g_paused_by_us = 0;
  }
}
