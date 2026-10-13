# SimGolf Remastered

[![Support me on Ko-fi](https://img.shields.io/badge/Ko--fi-support%20this%20mod-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/finalpantasy)

A fan-made upgrade of **Sid Meier's SimGolf** (Firaxis / EA, 2002) for Windows PCs.
It runs your own copy of the game as a modern program: sharp at 1080p, 1440p
and 4K, widescreen, smooth scrolling and zoom, a cleaner look for the menus and
course HUD, an Options page, and a way to change the game with mods.

This download contains **no game files**. You bring your own copy of the game
(the disc image and the version 1.02 no-CD `golf.exe`); the installer unpacks
it on your PC and nothing from it ever leaves your computer.

![The showcase course in HD with the new HUD](screenshots/course-hd.jpg)

| | |
|---|---|
| ![Start menu](screenshots/start-menu.jpg) | ![Golfer card](screenshots/golfer-card.jpg) |
| ![Course report](screenshots/reports.jpg) | ![Options page](screenshots/options.jpg) |
| ![Create your pro](screenshots/create-pro.jpg) | ![Skill points](screenshots/skill-points.jpg) |
| ![Golfer list in the People tab](screenshots/golfer-list.jpg) | ![Accomplishment page](screenshots/trophy.jpg) |

## What you get

### Included in the current 0.1.4 source
- **Staff:** the original default of 20 paid staff, adjustable up to 100 in Options > Gameplay. Expanded teams persist in game and course saves.
- **Controls:** orderly Esc handling, build trays that stay open, clearer weed notices, and aligned tee lines and cart shadows.
- **Setup:** use your own existing SimGolf installation folder or a disc image. Both require the checked version 1.02 no-CD program.
- **Career and pro:** automatic or manual clubhouse placement, readable adult/child labels and award plaques, current shot information, and pro export. The current Career flow follows the original game and does not open pro creation automatically.

### New in 0.1.3: the whole game, playable in the new interface
Everything from the original game is being made reachable from the new screens. This release fills the biggest gaps:
- **Your pro is back.** A new career starts with **Create your pro** (clear steps, quick clicks), then a new **skill points** page with big + and - buttons and a points counter. Each accomplishment gets its own page and opens the same skill page with your 3 new points. **People > Your pro** has Profile, Skills, Practice, Match and Tournament.
- **People tab lists:** every golfer with their mood and hole (click one to jump to them), and every staff member with **Hire, Fire, Move and Rename**.
- **Building:** a design chooser with a picture of every bench, flower bed, landmark, scenic tree and bridge. **Landmarks place again.** Greens and the clubhouse get Rotate (Tab).
- Smaller fixes: building upgrades unlock at the right level, N cancels a match again (name tags are Shift+N), backing out of theme packs keeps your theme, and menus lay out text much faster.

### Bigger courses
- **Up to 10x the land.** Pick the size of a new course in Options: Original (50 x 50 tiles), Bigger (70 x 70), Large (100 x 100) or Huge (158 x 158). Trees, water, rocks and wildlife are spread to match. Saved games keep their own size.
- Huge courses still scroll at over 100 frames per second on a modern PC.

### Looks
- **Native 1080p, 1440p, 4K, and ultrawide (21:9)**, sharp and filling the screen, borderless full screen or windowed.
- **Optional HD art pack:** all 132,000 of the game's own pictures upscaled 4x (golfers, buildings, trees, ground, animals, menus, effects). Setup downloads it for you (about 2.6 GB). Switch any time between **HD**, **Pixel HD** (crisp pixel look) and **Original**.
- **Frame rate options:** 60, 120, 144, 240 or uncapped, with v-sync.
- A round, shaded HD golf ball with smooth flight: smaller than the first version, every golfer plays their own colour, with a smooth fading trail.
- A new HD start menu logo.
- A clean new **Frosted Mint** look: start menu, loading screens, course HUD, dialogs, report panel, golfer cards and notifications, with the modern Plus Jakarta Sans font and steady numbers.

### Smoother play
- **Smooth camera** that glides and stops anywhere instead of jumping tile by tile; no stutter or scenery popping at the screen edges.
- **Mouse-wheel zoom** that glides toward the pointer; **W A S D** and **middle-mouse drag** to pan.
- **Game speed** setting so a full course gets built in fewer seasons.
- **Key remapping** for every action (two keys each, Original or Modern presets), plus mouse settings.
- **Hide the HUD** with U for a clean view of your course; Shift+T now hides scenic trees and palms too.

### Golfers and information
- Golfer **names on hover** (N shows all), smaller and lighter **thought bubbles**, new **mood faces**.
- A **golfer card** with mood bars, scores, recent comments and their profile.
- A **golfer profile page**: edit the bio and read everything the golfer says; the land purchase map in the new look.
- **Notifications** you can click for the full text and to jump to the golfer or hole, plus a **message history**.
- **All reports in one tabbed panel**: course, comments, histograph, SGA, finances, roster, best scores, hole stats.

### Fixes
- No more black triangles at waterfalls and water on slopes.
- Clicking a standing building no longer places a second one on top of it and charges you twice.
- No stuck name-tag text, no black box under the blimp in HD, no old 2002 HUD showing under the new one.
- A crash a minute or two after loading a course is fixed (0.1.2), and the female head crash from the official 1.03 patch is ported.
- Trees, palms and forests stay on screen at the farthest zoom.
- No dark see-through blobs at cliff corners with the HD art, no dark wedge behind waterfalls (their spray is back), no dark dot following the ball, no old frame beside the Options menu.

### Restored cut content
- Things the game's files contained but never offered, like the oriental house, the dinosaur tar pit and the lovers' bench. Turn on **Cut content** in Options.

### Mods
- **Change the game with small text files:** building prices and income, terrain costs, staff wages, difficulty, starting cash, golfer moods and new golfer personalities.
- A real **mod system**: mod folders, load order, dependencies, conflict handling and mod profiles; saves remember which mods they were made with. See [docs/modding](docs/modding).

### Always optional
- Every change has a switch in **Options** or `mods.ini`, so the original 2002 game is always one setting away.

## Install in four steps

1. Get your game files (see [INSTALL.md](INSTALL.md) for exactly where):
   the SimGolf disc image and the v1.02 no-CD zip.
2. Download `SimGolf-Remastered-v*-windows.zip` from the
   [Releases page](../../releases) and unzip it (right-click, Extract All).
3. Double-click **Setup.cmd** and answer the questions. Say yes to the HD art
   if you have 3 GB free.
4. Play with the **SimGolf Remastered** shortcut on your desktop.

Full step-by-step help and fixes for common problems are in [INSTALL.md](INSTALL.md).

## Coming next

Being worked on now:
- **Realistic water**: calm, clear in the shallows, deeper further out, waves that wash up the beach and slide back, ripples that catch the sky and the sun
- **Consistent icons** everywhere in the new interface, and the membership roster key with its icons
- **More staff**: a setting to hire more than the original limit
- The last original screens checked and redone: shot readout, tournament pairing, saving your pro

Planned:
- **Hardcore mode**: a tougher economy where upkeep and rival clubs keep money tight for years, a pro who ages and retires, and an optional ironman save
- **Weather and light**: time of day, drifting cloud shadows, rain, storms, fog
- **Pause and speed buttons** on the HUD, and faster speeds that hold up on big courses
- Animals you can buy and place, celebrities who buy homes and play your tournaments, more languages

Ideas and help are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).

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
