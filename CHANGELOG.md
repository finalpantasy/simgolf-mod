# Changelog

## 0.1.4 - 2026-10-13

Staff expansion and safer play. More setup choices, cleaner controls, and fixes for saves, reports and course details.

- Changed: staff starts at the original limit of 20. Options > Gameplay can raise it to 100; existing staff survive lowering the limit and saving/loading
- Fixed: a busy save-name box no longer shows the old moving text strip
- Fixed: temporary text strings are terminated before font measurements, preventing a verified invalid read
- Fixed: failed mod-record writes no longer emit a successful completion receipt
- Fixed: Esc closes the active layer in order, build trays stay open after placement, cart shadows align, and uncovered weeds offer an Open staff notice
- Fixed: tee-to-cup lines align at both ends, pro profiles distinguish adults and children, and the Best Hole award plaque is readable
- New: setup can use an existing SimGolf installation folder as well as a disc image, with checked files and a separate required version 1.02 program
- Fixed: tournament camera cleanup restores the original course tiles, including after save/load; Career can place the clubhouse automatically or leave it for manual placement
- Fixed: the pro shot readout and pro export use the current game values and chosen save name
- Fixed: No thanks on the land-purchase page keeps your money, with the pointer held on the intended exit control
- Fixed: additional verified table, animation and shot-cache bounds guards (switch fixes.re_bounds)

- Fixed: the browser development build compiles with the default pro skill settings again

- Fixed: the first dialogs and reports now use the new interface from their first visible frame

- New: a Mod Manager (Mods on the start menu, in Options,): search, sort, filters, load order, profiles, conflict and missing-mod warnings, changes apply on restart
- The property map legend says "Can't afford" instead of "Too dear"
- New: an Updates panel on the start menu shows what is new and can check for a new version (Update now checks the download before installing; turn the check off in Options)
- Fixed: a building being placed hung off to one side of the mouse; the middle of the building now sits under the pointer ([ui] place_center=0 keeps golf's corner placement). Buildings that turn get a Rotate button while placing
- New: 16 painted badges for the properties on the map list ([ui] map_badges=original keeps golf's emblems)
- Changed: the pro works like the original game again: no pro popup at a career start, Edit pro from People, Practice opens the skill page; Look buttons (skin, hair, shirt, pants) on Create your pro now work, and the old profile and skill screens no longer show behind the new pages
- Fixed: Time to Play in the course report climbed every game year (past 5 hours on small courses, so the tournament check failed). Hole 1's start time read only one byte of the game clock. Found and diagnosed by cvanderlinden (issue #2, PR #3). Saves with an inflated hole 1 are repaired when they load, and golfers already on hole 1 have truncated start ticks re-stamped (switch fixes.hole_time)
- Fixed: a few sounds that never played because their file names differ a little from the installed files (lake, alligator, marshal and others)
- Fixed: three counters in the course map summary were read as one byte, wrong above 127 on big maps
- Fixed: updating replaced your own mods.ini; an update now keeps it and ships the new defaults as mods.ini.default
- Fixed: a crash or power cut while saving could wreck the old save; saves are written to a temporary file and swapped in only when complete ([fixes] safe_saves)
- Fixed: a crash when quitting (the same translation fault sat at 29 places in the game)
- Fixed: sound went silent for good after unplugging headphones or changing speakers ([fixes] audio_recover)
- Fixed: memory overruns when every staff slot is full, with a very long save-file list, or with a broken sprite or head number ([fixes] bounds)
- Fixed: the No Dandelion landmark checked the wrong spot when weeds grew, so it did not protect the tiles around it ([fixes] no_dandelion)
- Fixed: robustness: clicks are never dropped under heavy input, text is never cut mid-character, art that fails to load retries, stricter mod setting numbers, and the installer refuses damaged discs instead of crashing
- Fixed: the old 2002 menu and loading screen flashed at startup and after choosing a property (Build here). A clean loading card now covers every frame until the new screens are ready

## 0.1.3 - 2026-10-13

Playability fixes: everything from the original game reachable in the new interface.

- Fixed: no way to create your pro or spend skill points in the new HUD. A new career now opens pro creation, People has a "Your pro" tab (Create pro, Profile, Skills, Practice, Match, Tournament), and accomplishments offer the three skill points again
- Fixed: the pro profile at a career start was slow (about a second per click) and led to the tiny old skill screen. It is now "Create your pro" with clear steps, clicks register at once, and a new skill points page has big - and + buttons and a points counter
- New: each accomplishment gets a clean page (trophy name, a photo of your course with its date, Continue to your 3 new skill points) instead of the half-screen wooden board; a crash when the trophy history was full is fixed
- New: an HD golf-ball globe on the property map, in the new look
- Fixed: backing out of the theme packs reset the theme; menus lay out text much faster
- Fixed: the female head crash from the official 1.03 patch, ported
- Fixed: dark see-through blobs at cliff corners with the HD art (rock chips drawn as shadows)
- Fixed: a dark wedge behind waterfalls; golf's own waterfall spray is back
- Fixed: an old popup frame showing beside the new Options menu
- Fixed: a dark pixelated dot following the ball; the ball shadow is softer
- Fixed: trees, palms and forests vanished at the farthest zoom
- Fixed: the People tab had no golfer or staff list. Golfers now lists everyone with their mood and hole (click to jump to them), and Staff lists every employee with Hire, Fire, Move and Rename
- Fixed: landmarks could not be placed, and benches, flower beds, scenic trees and bridges gave a random design. A chooser now shows a picture of every design, with Rotate (Tab); greens and the clubhouse get Rotate too
- Fixed: building upgrades greyed out one level early; the N key (cancel match) was taken by the name tags toggle (name tags are now Shift+N)
- New: cart path help (path view on Y, no-path markers), a tiny ball splash in water
- Pro skills can grow past 100 percent on 18-hole courses

## 0.1.2 - 2026-10-12

Crash fix.

- Fixed: the game could crash a minute or two after loading a course, while drawing golfer portraits (also in 0.1.0 and 0.1.1)

## 0.1.1 - 2026-10-12

Fixes and small upgrades.

- Fixed: with the HD art on, parts of the old 2002 HUD showed underneath the new course HUD
- New HD start menu logo with Remastered built in ([ui] logo=original brings back the old one)
- New golfer profile page (Profile button on the golfer card): edit the bio, see everything the golfer says; the year-end land purchase map in the new look
- Smaller golf ball (about 25 percent), each golfer plays their own ball colour, smooth fading trail; Options, Gameplay has Ball size, Ball colours and Ball trail

## 0.1.0 - 2026-10-12

First public build.

- Native Windows program for the translated game: runs at any resolution, borderless full screen, smooth scrolling and zoom
- Frosted Mint menus, dialogs, report panel and course HUD; Options page
- Round HD golf ball, golfer names and speech bubbles
- Optional HD art pack (separate download)
- Mods: data tables for prices, personalities and difficulty
- One-click installer that unpacks your own game files
