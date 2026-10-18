#!/usr/bin/env bash
# tools/release/package.sh: the Windows release zip (task-76).
#   bash tools/release/package.sh [version]     -> dist/release/SimGolf-Remastered-v<version>-windows.zip
# Contains our program and the installer, NO game files: image/golf.bin and
# jgld.bin (the game's own code) are made on the player's PC from their disc
# and no-CD golf.exe by sgsetup.exe. Builds build/release (cmake preset "release": no build-machine paths in the exes).
set -euo pipefail
cd "$(dirname "$0")/../.."
VER=${1:-$(cat tools/release/VERSION)}
N=build/release
. tools/env.sh
[ -f build/gen/golf/golf_protos.h ] || { echo "build/gen missing: run tools/translate_gen.sh"; exit 3; }
(cd port && cmake --preset release >/dev/null) && cmake --build build/release | tail -1
for f in golf.exe SDL2.dll sgsetup.exe sgupdate.exe jgld-nops.json; do [ -f "$N/$f" ] || { echo "missing $N/$f: run tools/build_native.sh"; exit 3; }; done
OUT=dist/release
STAGE=$OUT/stage/SimGolf-Remastered-v$VER
rm -rf "$OUT/stage"
mkdir -p "$STAGE/app/data" "$STAGE/app/mods"
cp tools/release/setup/Setup.cmd tools/release/setup/setup.ps1 tools/release/setup/copy_app.ps1 tools/release/setup/Import-CommunityTheme.ps1 "$STAGE/"
A=$STAGE/app
cp $N/golf.exe $N/SDL2.dll $N/sgsetup.exe $N/sgupdate.exe $N/jgld-nops.json "$A/"
cp "$N/golf.exe" "$A/Sim Golf Remastered.exe"
cp tools/diagnostics/Collect-Diagnostics.ps1 "$A/"
cp docs/DIAGNOSTICS.md "$A/DIAGNOSTICS.txt"
cp -r "$N/fonts" "$A/"
cp -r "$N/data/." "$A/data/"
for family in portraits characters; do
  [ -d "art_local/release-$family" ] || { echo "bundled $family assets missing"; exit 3; }
  mkdir -p "$A/ui/$family"
  cp -r "art_local/release-$family/." "$A/ui/$family/"
done
mkdir -p "$A/ui"
# New terrain is an optional mod download, not part of every bug-fix update.
for ui_entry in "$N/ui/"*; do
  [ "$(basename "$ui_entry")" = terrain ] && continue
  cp -r "$ui_entry" "$A/ui/"
done
if [ -f data/approved-clubhouse.json ]; then
  [ -f art_local/release-clubhouse/index.tsv ] || { echo "approved clubhouse pack missing: copy app/ui/clubhouse from the release into art_local/release-clubhouse"; exit 3; }
  mkdir -p "$A/ui/clubhouse"
  cp -r art_local/release-clubhouse/. "$A/ui/clubhouse/"
fi
cp -r mods/. "$A/mods/"
cp mods/load_order.toml "$A/mods/load_order.toml"
rm -rf "$A/mods/examples"
mkdir -p "$A/mods-examples" && cp -r mods/examples/. "$A/mods-examples/"
cp -r data/tables "$A/data/"
cp data/living_club.json "$A/data/"
cp data/lighting-windows.csv data/weather-air.csv data/weather-atmosphere.csv data/weather-climate.csv data/weather-surfaces.csv "$A/data/"
# Compatibility copies for the fixed data/tables whitelist in the 0.1.16 updater.
mkdir -p "$A/data/tables/_release"
cp data/living_club.json data/lighting-windows.csv data/weather-air.csv data/weather-atmosphere.csv data/weather-climate.csv data/weather-surfaces.csv "$A/data/tables/_release/"
cp data/course-lights.csv "$A/data/tables/_release/"
cp -r data/course-lamps "$A/data/tables/_release/"
cp data/mods.ini "$A/mods.ini.default"   # never mods.ini: unzipping over an install would replace the player's (task-114i)
cp tools/release/setup/golf.ini.template "$A/golf.ini.template"
echo "SimGolf Remastered $VER (git $(git rev-parse --short HEAD))" > "$A/version.txt"
P=tools/release/public
cp $P/README.md "$STAGE/README.txt"
cp $P/INSTALL.md "$STAGE/INSTALL.txt"
cp $P/CHANGELOG.md "$STAGE/CHANGELOG.txt"
cp $P/CREDITS.md "$STAGE/CREDITS.txt"
cp $P/LICENSE "$STAGE/LICENSE.txt"
cp port/LICENSE "$STAGE/LICENSE-simgolf-web.txt"
mkdir -p "$STAGE/licenses"
cp port/third_party/unshield/LICENSE "$STAGE/licenses/unshield.txt"
cp port/third_party/zlib/LICENSE "$STAGE/licenses/zlib.txt"
cp port/third_party/tomlc99/LICENSE "$STAGE/licenses/tomlc99.txt"
cp port/third_party/fonts/LICENSE "$STAGE/licenses/liberation-fonts.txt"
cp port/third_party/fonts/ComicNeue-OFL.txt "$STAGE/licenses/ComicNeue-OFL.txt"
cp data/fonts/plus-jakarta-sans/OFL.txt "$STAGE/licenses/PlusJakartaSans-OFL.txt"
ZIP=$OUT/SimGolf-Remastered-v$VER-windows.zip
rm -f "$ZIP"
(cd "$OUT/stage" && /c/Windows/System32/tar.exe -a -cf "../SimGolf-Remastered-v$VER-windows.zip" "SimGolf-Remastered-v$VER")
# GitHub release assets must stay below 2 GiB. Reject an undeliverable package.
[ "$(wc -c < "$ZIP")" -lt 2147483648 ] || { echo "release zip exceeds the 2 GiB asset limit"; exit 1; }
(cd "$OUT" && sha256sum "SimGolf-Remastered-v$VER-windows.zip" > "SimGolf-Remastered-v$VER-windows.zip.sha256")
ls -la "$ZIP"
cat "$ZIP.sha256"

if [ -f "$N/ui/terrain/index.tsv" ]; then
  TERRAIN_STAGE="$OUT/terrain-stage/SimGolf-Remastered-Terrain-v$VER"
  # Only clear this script's fixed staging directory inside the workspace.
  terrain_root=$(realpath -m "$OUT/terrain-stage")
  [ "$terrain_root" = "$PWD/dist/release/terrain-stage" ] || exit 2
  rm -rf "$terrain_root"
  mkdir -p "$TERRAIN_STAGE/ui"
  cp -r "$N/ui/terrain" "$TERRAIN_STAGE/ui/"
  cp tools/release/public/TERRAIN-MOD.txt "$TERRAIN_STAGE/README.txt"
  TERRAIN_ZIP="$OUT/SimGolf-Remastered-Terrain-v$VER.zip"
  (cd "$OUT/terrain-stage" && /c/Windows/System32/tar.exe -a -cf "../SimGolf-Remastered-Terrain-v$VER.zip" "SimGolf-Remastered-Terrain-v$VER")
  [ "$(wc -c < "$TERRAIN_ZIP")" -lt 2147483648 ] || { echo "terrain mod exceeds the 2 GiB asset limit"; exit 1; }
  (cd "$OUT" && sha256sum "SimGolf-Remastered-Terrain-v$VER.zip" > "SimGolf-Remastered-Terrain-v$VER.zip.sha256")
  ls -la "$TERRAIN_ZIP"
fi
