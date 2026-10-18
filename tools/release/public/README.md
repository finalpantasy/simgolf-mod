# SimGolf Remastered for Windows

[![Support me on Ko-fi](https://img.shields.io/badge/Ko--fi-support%20this%20mod-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/finalpantasy)

A fan-made upgrade of **Sid Meier's SimGolf** (Firaxis / EA, 2002) for Windows PCs.
It runs your own copy of the game as a modern program: sharp at 1080p, 1440p
and 4K, widescreen, smooth scrolling and zoom, a cleaner look for the menus and
course HUD, an Options page, and a way to change the game with mods.

Includes **45 additional golfer identities and portraits**, Living Club personalities,
an optional **day/night lighting and regional weather system**, and the new Parkland clubhouse.

This download contains **no game files**. You bring your own copy of the game
(an installed game folder or disc image, and the version 1.02 no-CD `golf.exe`); the installer prepares
it on your PC and nothing from it ever leaves your computer.

![Remastered construction menu in the Windows game](screenshots/remastered-construction.jpg)

[Download for Windows](https://github.com/finalpantasy/simgolf-mod/releases/latest) | [Installation help](INSTALL.md) | [Use the original UI](#use-the-original-ui) | [Report a bug](#report-a-bug-no-coding-experience-needed)

## Use the original UI

Prefer the classic menus and controls? The replacement interface has several
independent switches. **Original construction menu** in Options only changes the
building wheel; it does not turn off the other new screens. Use the settings
below to switch all the replacement UI layers back to their original paths.

1. **Save your game and close SimGolf Remastered completely.**
2. Right-click your **SimGolf Remastered** desktop shortcut, choose **Properties**,
   then **Open File Location**. Use the installed folder containing `golf.exe`,
   not the download ZIP or your separate original SimGolf installation.
3. Find `options.ini` and make a backup copy named `options-before-original-ui.ini`.
   Open `options.ini` with Notepad. If it does not exist, create it: in Notepad's
   **Save As**, select **All files**, name it `options.ini`, and save beside
   `golf.exe`. Check that Windows has not named it `options.ini.txt` (turn on
   **File name extensions** in File Explorer's View menu).
4. Merge the settings below into that file. If a `[ui]` or `[art]` section already
   exists, update matching lines there and add the missing lines under that
   heading. Otherwise, add the section. **Keep only one entry for each key** and
   leave your unrelated settings intact. Copy the spelling exactly, with no
   spaces around `=` and no leading semicolons; a semicolon comments a line out.

```ini
[ui]
font=original
hd_legacy_text=0
hud=original
hud_show=1
original_bottom=1
original_rotate_button=0
speed_button=0
golfers=original
faces=original
portraits=original
portrait_preview=original
frontend=original
logo=original
map_badges=original
map_globe=original
map_icons=original
dialogs=original
reports=original
options=original
loading=original
smooth_loading=0
paths=original
aim=original
shot_readout=original
ball=original
ball_trail=original

[art]
ui=original
```

5. Save the file, then launch the game again. You should see the classic start
   menu, course HUD/building wheel, golfer cards, dialogs and reports. Preferences
   uses the original **Edit Preferences** list instead of the modern Options
   page. The extra Rotate button, shot card, path overlay, modern ball/trail and
   high-resolution legacy text replay are also disabled.

**Why `options.ini`?** It is read after `mods.ini` and overrides matching values,
so changing only `mods.ini` can leave a previously saved Options choice active.
The UI switches do not require disabling all mods or changing the active profile.
If you deliberately use `SIMGOLF_OPTIONS_FILE` to select another settings file,
edit that file instead. A launch using `--no-mods` ignores these settings files.

**To get the new UI back:** close the game, then restore your backed-up
`options.ini`. If you created the file for these instructions, remove only the
entries you added (or remove the file if it still contains nothing else), then
restart. Restoring a backup also restores the other options saved in that backup;
if you changed those later, undo just the UI entries instead.

**This restores the supported original interface, not an unmodified 2002 game.**
Bug fixes, resolution/scaling, gameplay mods, the added golfer roster, course art,
water and optional lighting/weather have separate settings. For original course
textures, choose **Textures: Original** in the modern Options page before doing
this, or set `textures=original` under `[art]` in `options.ini` (any explicit
`look` or per-category art overrides must also be removed or set to `original`).
For original water, set `water=original` under `[display]`; to turn off the
optional atmosphere, set `enabled=0` under `[environment]`. Merge these into
existing sections too. You can keep HD course art and bug fixes with the classic
UI; none of these steps requires deleting saves, game data or mod folders.

## New features and improvements over vanilla

### Mod settings in the game

Select a mod and use the green **Configure** button to change named settings, with guidance on when each choice takes effect. Settings save automatically; disabled mods and the Vanilla profile keep their original behavior.

### Bigger courses and more staff

- **Up to 10x the land:** choose Original (50 x 50 tiles), Bigger (70 x 70), Large (100 x 100) or Huge (158 x 158) when starting a course. Terrain, scenery and wildlife scale with the map, and saves keep their course size.
- **Larger staff teams:** keep the original limit of 20 paid staff or raise it to 100 in Options. Expanded teams are stored in game and course saves.

### Modern visuals

- **1080p, 1440p, 4K and ultrawide (21:9)** support, with windowed or borderless full-screen play.
- **Optional HD art:** the game's original artwork upscaled 4x, including golfers, buildings, trees, terrain, animals and effects. Setup can download the separate pack, about 2.6 GB. Choose **HD**, **Pixel HD** or **Original** textures.
- **Sharp legacy text:** original and modern font choices are rendered at display resolution, including classic construction labels and the original HUD.
- **Frosted Mint interface:** redesigned menus, loading screens, course controls, dialogs, golfer cards and reports, with readable modern text.
- **Frame rate choices:** 60, 120, 144, 240 or uncapped, plus v-sync.
- **A shaded HD golf ball** with golfer-specific colours and a fading flight trail. Ball size, colours and trail are adjustable.
- **Rendering improvements:** water on slopes, waterfall spray, scenery visibility at distant zoom levels and HD shadows.

### Faster loading

- **Faster startup and new courses:** fewer loading pauses and repeated terrain calculations, with a smooth progress bar.
- **Original results preserved:** the same course generation and full-quality artwork, with original loading paths available through the performance settings.

### Smoother controls

- **Smooth camera movement** with mouse-wheel zoom toward the pointer, WASD panning and middle-mouse dragging.
- **Adjustable game speed**, mouse settings and key bindings, with Original and Modern control presets.
- **A clear course view:** adjustable golfer names, optional HUD hiding and the ability to hide terrain trees and scenic palms. Original controls remain available.

### Easier course building

- **Remastered construction controls by default:** the combined Course palette includes path-material and lamp pickers. The original wheel and Course/Buildings trays are an optional choice in Options, with modern top stats and optional HD panel artwork.
- **Optional joined pot bunkers** with connected sand, rounded edges and a steep outer rim. Off by default.
- **18 cosmetic path materials** in the modern picture picker, preserved with saved courses.
- **Six optional course-light designs** with saved placement and day/night artwork. Enable the lamps separately and use the modern palette to select them.
- **Optional click-and-drag construction:** terrain rectangles and L-shaped paths keep the original per-tile prices and placement rules. Single placement remains the default.
- **Picture-based design choices** for benches, flower beds, landmarks, scenic trees and bridges.
- **Rotate controls** for greens and the clubhouse, plus camera-aligned placement outlines and lighter path shading.
- **Repeated placement** for landmarks and scenic bridges, with an option to return to single placement.
- **Home Site details** show lot value, clearing and preparation costs, and profit in the modern placement card.
- **Automatic or manual clubhouse placement** when starting a career.

### Golfers, your pro and reports

- **Golfer names on hover**, lighter thought bubbles and mood faces.
- **Golfer cards** with mood bars, scores, recent comments and profile information.
- **People lists:** see each golfer's mood and current hole, jump to them, and hire, fire, move or rename staff.
- **Remaining professional goals**, matching pro skill bars and a direct-opponent match scorecard.
- **Modern pro screens** for profiles, skill points, practice, match pairing, accomplishments and Top Ten Designers.
- **Live pro scorecards** with the current stroke count, plus shot choices and flight previews.
- **Clickable notifications** with full messages and shortcuts to the relevant golfer or hole, plus message history.
- **Tabbed reports** for course information, comments, histograph, SGA, finances, roster, best scores and hole stats.
- **A redesigned year-end summary** with a clear Continue button.

### Optional lighting and regional weather

Enable the optional atmosphere mod for warm dawns and golden afternoons, moonlit
nights, moving cloud shade, rain, wet ground and storms with thunder. Regional
weather follows the course's location and season. Lighting adds terrain shading, water reflections
and soft directional shadows while keeping the original course and artwork recognizable.

| Daylight | Golden hour |
|---|---|
| ![Daylight on the showcase course with optional lighting enabled](screenshots/lighting-day.jpg) | ![Warm golden-hour lighting on the same course](screenshots/lighting-golden.jpg) |
| **Moonlight** | **Rain and cloud cover** |
| ![Moonlit course with the optional lighting mod](screenshots/lighting-night.jpg) | ![Rain and cloud shade over the same course](screenshots/weather-rain.jpg) |

These are actual Windows screenshots with the optional atmosphere enabled. The hour and weather were selected for the pictures; original lighting remains the default. The separate experimental Terrain Mod is not installed.

To try the moving day/night cycle and regional weather, add or update these
values in the existing `[environment]` section of `mods.ini`:

```ini
[environment]
enabled=1
day_night=1
regional_weather=1
gameplay=0
```

Set `enabled=0` to return to the original presentation. Weather effects on ball
rolls and arriving golfers' mood are a separate, **off-by-default** gameplay mod.
Thunder and rain have their own volume setting; lightning can be disabled.
See the [independent mod switches](docs/modding/CORE.md#optional-additions-in-0117).

### More golfers and Living Club

Meet **25 new ordinary visitors, 10 touring pros and 10 fictional celebrities**,
with authored names, portraits and character details. The original cast remains
available. Living Club adds 25 personalities, contextual dialogue, memories and
persistent stories, with extra mood reactions available separately.

| Ada Fairway | Calder Stone |
|---|---|
| ![Ada Fairway's portrait and skill preview in the real Pick A Pro screen](screenshots/golfers-ada.jpg) | ![Calder Stone's portrait and skill preview in the real Pick A Pro screen](screenshots/golfers-calder.jpg) |

New ordinary visitors follow **original skill progression by default**. Authored
arrival skill mixes and extra mood reactions are opt-in. Original energy, mood
and weed balance is preserved. The roster, stories and reactions each have
[independent switches](docs/modding/CORE.md#optional-additions-in-0117).

### A new Parkland clubhouse

The new clubhouse includes all four views, animated flags and birdbath water.
Original textures retain the original building.

![The new Parkland clubhouse and garden in the Windows game](screenshots/clubhouse-parkland.jpg)

### Fixes and compatibility

Construction animations, pro-shot controls, Save editing and cancellation,
long welcome messages, theme memory and the Routing Map are repaired. Saves,
custom mods and settings are preserved during updates. Original energy, mood,
weed balance and ordinary visitor skill progression remain the defaults;
optional gameplay changes have independent switches.

### More content and mod support

- **Restored cut content:** enable items already present in the original game files, including the oriental house, dinosaur tar pit and lovers' bench.
- **Community themes:** import compatible story, pro and celebrity ZIPs while keeping stock themes available. See the [installation guide](INSTALL.md#add-a-community-theme).
- **Editable game data:** adjust building prices and income, terrain costs, staff wages, difficulty, starting cash, golfer moods and personalities through text files.
- **Mod folders and profiles** with load order, dependencies and conflict handling. Saves remember their mod setup. Start with [the modding guide](docs/modding/README.md) or [make your first mod](docs/modding/FIRST_MOD.md).

### Setup and updates

- **Guided setup** from your existing SimGolf installation or a disc image, using your own matching version 1.02 no-CD program.
- **In-game update checks:** choose when to download and install an update, with actual download progress, checksum/unpacking status and recovery support.
- **Preserved settings:** updates and setup retain player settings, mod profiles and custom mod files.

Original interface and texture options remain available. The default energy, mood and weed balance follows vanilla SimGolf.

## More screenshots

| | |
|---|---|
| ![Start menu](screenshots/start-menu.jpg) | ![Golfer card](screenshots/golfer-card.jpg) |
| ![Course report](screenshots/reports.jpg) | ![Options page](screenshots/options.jpg) |
| ![Create your pro](screenshots/create-pro.jpg) | ![Skill points](screenshots/skill-points.jpg) |
| ![Golfer list in the People tab](screenshots/golfer-list.jpg) | ![Accomplishment page](screenshots/trophy.jpg) |

## Install in four steps

**Windows 10:** if you downloaded the game archive from the Internet Archive,
extract it with [7-Zip](https://www.7-zip.org/) first. During Setup, choose the
`.iso` or `.bin` inside the extracted folder. Choose the version 1.02 no-CD ZIP
separately; that ZIP can stay zipped. See [INSTALL.md](INSTALL.md) for archive help.

1. Get your game files (see [INSTALL.md](INSTALL.md) for exactly where):
   your existing SimGolf installation folder or disc image, plus your matching v1.02 no-CD program.
2. Download `SimGolf-Remastered-v*-windows.zip` from the
   [Releases page](../../releases) and unzip it (right-click, Extract All).
3. Double-click **Setup.cmd** and answer the questions. Say yes to the HD art
   if you have 3 GB free.
4. Play with the **SimGolf Remastered** shortcut on your desktop.

Full step-by-step help and fixes for common problems are in [INSTALL.md](INSTALL.md).

Ideas and help are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

## Report a bug (no coding experience needed)

**A save from just before the problem, the log, and clear steps are much more useful than “it crashed.”** Please send what you have even if some files are missing.

### 1. Open a report

1. [Check existing reports](https://github.com/finalpantasy/simgolf-mod/issues) for the same problem. If one matches, add your details and files in a comment there.
2. Otherwise, [open a new bug report](https://github.com/finalpantasy/simgolf-mod/issues/new?template=bug_report.yml). GitHub will ask you to sign in or create a free account.
3. Give it a specific title, such as **“0.1.21 crashes when scrolling a Huge Parkland course.”** You do not need to fork, clone, or download the repository.

### 2. Find your save, log and crash report

**After a crash, copy the log before opening the game again. In 0.1.21 and older builds, the next launch overwrites `golf.log`. Builds with log rotation retain five prior sessions, but collecting promptly is still best.** Copy files; do not move or delete your originals.

For a default installation, press **Windows + R**, paste this, then press Enter:

```text
%LOCALAPPDATA%\Programs\SimGolf Remastered
```

If you installed elsewhere, open that installation folder instead. You can right-click the game's shortcut, choose **Properties**, and look at its **Target** to find the folder containing `Sim Golf Remastered.exe`.

Look for:

| What to attach | Where to find it |
| --- | --- |
| **The affected save** (`.sve`), preferably from immediately before the problem | `game\Saved Games` inside the installation folder |
| **Companion files for that save** | In the same folder, copy files whose names start with the full save filename, for example `My Course.sve.*`. These can hold paths, lamps, camera restoration or visitor histories. |
| **`golf.log`** | Installation folder, beside the game program |
| **`golf.log.crash.txt`**, if present | Same folder; a crash may not always produce this file |
| **Settings**, when relevant | `mods.ini`, `options.ini`, and `golf.ini` in the installation folder; also say which mod profile and extra mods you use |
| **Screenshot or short video** | Especially helpful for missing words, visual glitches, misplaced controls or steps that are hard to describe |

If you use a custom save directory through `SIMGOLF_USER_DIR`, look for `Saved Games` there instead. The log records “host: saves and snapshots in …” when this override is active. A custom log command-line option may also change the log location.

**New-game crash with no save yet?** Attach the log and crash file, and tell us the course theme, map size, game mode and settings you chose. **Freeze without a crash file?** Say it froze, how long you waited, and whether you had to close it yourself. Do not claim there should be a crash file if none was created.

### Optional diagnostic ZIP exporter

If your installation includes `Collect-Diagnostics.ps1`, see the bundled `DIAGNOSTICS.txt` guide for a local exporter that previews files and asks for confirmation before making a ZIP. After export, it shows step-by-step GitHub reporting instructions and offers to open the Issues page. You review, attach and submit the report yourself; it never uploads anything. Saves require explicit selection; their paths, lamp data and visitor histories can be included with the separate `-IncludeSaveCompanions` option. Review the ZIP before sharing. If the exporter is missing or Windows blocks it, use the manual ZIP steps below; do not change security settings just to file a bug.

### 3. Attach the files to GitHub

1. Make a new folder somewhere convenient, such as your Desktop, and **copy only the relevant files** into it.
2. Right-click that folder and choose **Compress to ZIP file** (Windows 11), or **Send to > Compressed (zipped) folder** (Windows 10).
3. Drag the ZIP into the bug report's description box, or use the attachment link beneath the box. Wait for the upload to finish before submitting.
4. Add screenshots the same way. If GitHub rejects a large ZIP, remove unrelated files and try a smaller one. Do not ZIP the entire installation.

**Reports and attachments are public.** Logs can include your Windows username and local paths; saves can contain names you entered. Review the files first and remove personal details you do not want public. Do not include passwords, account information, original game executables, disc images, or the HD art pack. If you cannot share a save publicly, say so and ask the maintainer about alternatives; do not assume a private upload channel exists.

### 4. Tell us how to reproduce it

Please include:

- **Version:** the Remastered release number, and whether you updated or installed fresh.
- **What you were doing:** numbered steps, starting from loading the attached save or creating a new game.
- **Expected result / actual result:** what should have happened, and what happened instead.
- **How often:** every time, sometimes, or once; roughly how long you played before it happened.
- **Your setup:** Windows version, graphics card if known, screen resolution, and windowed/full-screen mode.
- **Relevant choices:** course theme and size, HD/Pixel HD/Original textures, optional water/lighting, mod profile and additional mods.
- **Attached files:** save and its companions, log, crash file if available, and screenshot/video if useful.

Keep unrelated bugs in separate reports. If you test another setting or version, add the result to your original report. Please keep a backup of the affected save and avoid overwriting the only copy while testing.

## Good to know

- Your saved games go in the `game\Saved Games` folder inside the install folder.
- To go back to the original look, open **Options** and pick Original textures,
  or edit `golf.ini` (the comments explain every line).
- Windows 10 or 11, 64-bit, a graphics card with OpenGL 3.3 or newer.

## Legal

This is an unofficial fan project. It is not affiliated with or endorsed by
Firaxis Games, Electronic Arts or Take-Two Interactive. SimGolf and its
assets are their property. This project distributes **none** of the game's
files: you supply your own copy. The optional HD art pack is a separate,
unofficial fan-made upscale of the game's own art; it is removed on request
by the rights holders.

The program code in this repository is MIT licensed ([LICENSE](LICENSE)).
It is built on [RichardAtCT/simgolf-web](https://github.com/RichardAtCT/simgolf-web)
(MIT), which translated the decompiled game into C. Third-party pieces keep
their own licences (see [CREDITS.md](CREDITS.md)).

## Support

This is a free hobby project. If you enjoy it and want to help keep it going, you can [buy me a coffee on Ko-fi](https://ko-fi.com/finalpantasy). Thank you!
