# Multiple owned courses (Windows)

Optional, off by default: **Options > Gameplay > Own several courses**, or
`[gameplay] multi_course=1` in mods.ini. Off keeps the original game: one live
course, the in-game world map reseeds its properties and offers the refresh
disc, and no course snapshots are written. Saves record the setting in their
`.mods.toml` `[rules]` table.

In a running game, open **Menu → World map**. Buying another property saves a
snapshot of the course you are leaving before the original game creates the
new one. On a later visit to the map, select a purchased property and choose
**Switch course** to return to its saved layout.

Each course keeps its own land, buildings, staff, clock and progress. Cash is
one bank account: it travels with you to the course you open. Golfer identities, membership statistics and profile history travel to the
course you open. The career's world map remains shared so all bought sites
stay marked as owned. A course's purchase price is paid before its previous
course snapshot is written.

The game stores each layout in an ordinary `Saved Games/&Course_<career>_<site>.sve`
file, with the same mod and roster companions as other saves. These internal
files are hidden from **Load Game**. Back up the whole `Saved Games` folder to
keep every course; a lone manual save still contains only its active layout.

Existing single-course saves load as before. The game cannot reconstruct an
older property's layout if no snapshot was saved when the player left it; the
map then shows **No saved course**. Switching is offered in the Frosted Mint
world map on the Windows build. The original-skin map has no switch control,
and in-game property rerolls are disabled to keep site indices stable. The
browser build keeps its existing controls.
