#!/bin/bash
# Release build of the game for the Playdate device, with a new build number.
#
#   scripts/release-device.sh                 -> build-release/quake_DEVICE.pdx
#   BUILD_DIR=build-foo scripts/release-device.sh
#
# What it does:
#   1. removes the release build directory and the stale pdex.* files that pdc would bundle, so
#      everything is rebuilt and the post-build step that hands out the build number always runs;
#   2. configures a Release device build (no profiler, no benchmark) and builds it;
#   3. ships Source/id1/pak0_demo.pak, if there is one, as id1/pak0.pak in the .pdx, without id1/music
#      (-DPD_RELEASE=ON; every other build leaves pak0_demo.pak out), and checks that it did;
#   4. checks that the build number in the built .pdx's pdxinfo went up (port/boards/playdate/
#      pdx_buildnumber.cmake sets it to one more than the larger of .build_number and the number in
#      Source/pdxinfo), and writes that number into Source/pdxinfo, so the number of the release is
#      recorded in the source tree and the next build counts on from it.
#
# Install the result with the SDK's tools or copy it to the device's Games folder; scripts/install-device.sh
# installs build-dev/quake_DEVICE.pdx.
set -e
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build-release}
BOARD=port/boards/playdate
SOURCE_PDXINFO=$BOARD/Source/pdxinfo
COUNTER=$BOARD/.build_number
PDX=$BUILD_DIR/quake_DEVICE.pdx

number_in() { sed -n 's/^buildNumber=\([0-9][0-9]*\).*/\1/p' "$1" | head -n 1; }

[ -f "$SOURCE_PDXINFO" ] || { echo "$SOURCE_PDXINFO not found"; exit 1; }

# the number the previous build ended with: the larger of the counter and Source/pdxinfo
old=$(number_in "$SOURCE_PDXINFO"); old=${old:-0}
if [ -f "$COUNTER" ]; then
  c=$(sed -n '1s/[^0-9]//gp' "$COUNTER"); c=${c:-0}
  [ "$c" -gt "$old" ] && old=$c
fi

echo "== Cleaning $BUILD_DIR and stale pdex.* =="
rm -rf "$BUILD_DIR" "$BOARD"/Source/pdex.*

echo "== Configuring (Release, device) =="
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE=$BOARD/toolchain.cmake -DBOARD_NAME=playdate \
  -DCMAKE_BUILD_TYPE=Release -DPD_RELEASE=ON -DPD_PROFILE=OFF -DPD_BENCH=OFF

echo "== Building =="
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)"

[ -f "$PDX/pdxinfo" ] || { echo "$PDX/pdxinfo was not produced"; exit 1; }
# the demo data replaced pak0.pak (pdx_pak.cmake); without a pak0_demo.pak the .pdx keeps pak0.pak
DEMO_PAK=$BOARD/Source/id1/pak0_demo.pak
if [ -f "$DEMO_PAK" ]; then
  [ ! -e "$PDX/id1/pak0_demo.pak" ] && cmp -s "$DEMO_PAK" "$PDX/id1/pak0.pak" \
    || { echo "$PDX/id1/pak0.pak is not $DEMO_PAK (or pak0_demo.pak is still in the .pdx)"; exit 1; }
  [ ! -e "$PDX/id1/music" ] || { echo "$PDX/id1/music is still in the shareware release"; exit 1; }
  pak="id1/pak0.pak is pak0_demo.pak"
else
  pak="no $DEMO_PAK, id1/pak0.pak as it is in Source/id1"
fi

new=$(number_in "$PDX/pdxinfo")
if [ -z "$new" ] || [ "$new" -le "$old" ]; then
  echo "The build number did not go up (was $old, pdxinfo says '${new:-none}'): not recording it."
  exit 1
fi

# record it in Source/pdxinfo
if grep -q '^buildNumber=' "$SOURCE_PDXINFO"; then
  sed -i.bak "s/^buildNumber=.*/buildNumber=$new/" "$SOURCE_PDXINFO" && rm -f "$SOURCE_PDXINFO.bak"
else
  printf 'buildNumber=%s\n' "$new" >> "$SOURCE_PDXINFO"
fi

version=$(sed -n 's/^version=//p' "$SOURCE_PDXINFO" | head -n 1)
echo
echo "Release build done: $PDX"
echo "  version $version, build number $old -> $new (also written to $SOURCE_PDXINFO)"
echo "  game data: $pak"
