# SimGolf Remastered for Windows

[![Support me on Ko-fi](https://img.shields.io/badge/Ko--fi-support%20this%20mod-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/finalpantasy)

A fan-made upgrade of **Sid Meier's SimGolf** (Firaxis / EA, 2002) for Windows PCs.
It runs your own copy of the game as a modern program: sharp at 1080p, 1440p
and 4K, widescreen, smooth scrolling and zoom, a cleaner look for the menus and
course HUD, an Options page, and a way to change the game with mods.

This download contains **no game files**. You bring your own copy of the game
(an installed game folder or disc image, and the version 1.02 no-CD `golf.exe`); the installer prepares
it on your PC and nothing from it ever leaves your computer.

![The showcase course in HD with the new HUD](screenshots/course-hd.jpg)

| | |
|---|---|
| ![Start menu](screenshots/start-menu.jpg) | ![Golfer card](screenshots/golfer-card.jpg) |
| ![Course report](screenshots/reports.jpg) | ![Options page](screenshots/options.jpg) |
| ![Create your pro](screenshots/create-pro.jpg) | ![Skill points](screenshots/skill-points.jpg) |
| ![Golfer list in the People tab](screenshots/golfer-list.jpg) | ![Accomplishment page](screenshots/trophy.jpg) |

## What's improved over vanilla SimGolf

### Bigger courses and more staff

- **Up to 10x the land:** choose Original (50 x 50 tiles), Bigger (70 x 70), Large (100 x 100) or Huge (158 x 158) when starting a course. Terrain, scenery and wildlife scale with the map, and saves keep their course size.
- **Larger staff teams:** keep the original limit of 20 paid staff or raise it to 100 in Options. Expanded teams are stored in game and course saves.

### Modern visuals

- **1080p, 1440p, 4K and ultrawide (21:9)** support, with windowed or borderless full-screen play.
- **Optional HD art:** the game's original artwork upscaled 4x, including golfers, buildings, trees, terrain, animals and effects. Setup can download the separate pack, about 2.6 GB. Choose **HD**, **Pixel HD** or **Original** textures.
- **Frosted Mint interface:** redesigned menus, loading screens, course controls, dialogs, golfer cards and reports, with readable modern text.
- **Frame rate choices:** 60, 120, 144, 240 or uncapped, plus v-sync.
- **A shaded HD golf ball** with golfer-specific colours and a fading flight trail. Ball size, colours and trail are adjustable.
- **Rendering improvements:** water on slopes, waterfall spray, scenery visibility at distant zoom levels and HD shadows.

### Smoother controls

- **Smooth camera movement** with mouse-wheel zoom toward the pointer, WASD panning and middle-mouse dragging.
- **Adjustable game speed**, mouse settings and key bindings, with Original and Modern control presets.
- **A clear course view:** hide the HUD with U, show golfer names with Shift+N, or hide trees and scenic palms with Shift+T.

### Easier course building

- **Picture-based design choices** for benches, flower beds, landmarks, scenic trees and bridges.
- **Rotate controls** for greens and the clubhouse, plus camera-aligned placement outlines and lighter path shading.
- **Repeated placement** for landmarks and scenic bridges, with an option to return to single placement.
- **Home Site details** show lot value, clearing and preparation costs, and profit in the modern placement card.
- **Automatic or manual clubhouse placement** when starting a career.

### Golfers, your pro and reports

- **Golfer names on hover**, lighter thought bubbles and mood faces.
- **Golfer cards** with mood bars, scores, recent comments and profile information.
- **People lists:** see each golfer's mood and current hole, jump to them, and hire, fire, move or rename staff.
- **Modern pro screens** for profiles, skill points, practice, match pairing, accomplishments and Top Ten Designers.
- **Live pro scorecards** with the current stroke count, plus shot choices and flight previews.
- **Clickable notifications** with full messages and shortcuts to the relevant golfer or hole, plus message history.
- **Tabbed reports** for course information, comments, histograph, SGA, finances, roster, best scores and hole stats.
- **A redesigned year-end summary** with a clear Continue button.

### More content and mod support

- **Restored cut content:** enable items already present in the original game files, including the oriental house, dinosaur tar pit and lovers' bench.
- **Community themes:** import compatible story, pro and celebrity ZIPs while keeping stock themes available. See the [installation guide](INSTALL.md#add-a-community-theme).
- **Editable game data:** adjust building prices and income, terrain costs, staff wages, difficulty, starting cash, golfer moods and personalities through text files.
- **Mod folders and profiles** with load order, dependencies and conflict handling. Saves remember their mod setup. See [docs/modding](docs/modding).

### Setup and updates

- **Guided setup** from your existing SimGolf installation or a disc image, using your own matching version 1.02 no-CD program.
- **In-game update checks:** choose when to download and install an update, with checks for incomplete program files and recovery support.
- **Preserved settings:** updates and setup retain player settings, mod profiles and custom mod files.

Original interface and texture options remain available. The default energy, mood and weed balance follows vanilla SimGolf.

We're working on many other fixes, changes, updates, improvements, and mods to make SimGolf better.

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
