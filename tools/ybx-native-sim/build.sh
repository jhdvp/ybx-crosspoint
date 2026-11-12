#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT="$(CDPATH= cd -- "$HERE/../.." && pwd)"
UI="$ROOT/freeink-sdk/libs/ui/FreeInkUI"
BUILD="${YBX_SIM_BUILD_DIR:-/tmp/ybx-native-freeink-sim}"
FONT="/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
[ -r "$FONT" ] || { echo "Missing local Unicode TTF: $FONT" >&2; exit 1; }
mkdir -p "$BUILD"
for config in small:13 body:17 title:24; do
 name="${config%%:*}"
 size="${config##*:}"
 case "$name" in small) sym=SimSmall;; body) sym=SimBody;; title) sym=SimTitle;; esac
 python3 "$UI/tools/gen_font.py" --ttf "$FONT" --first 0x20 --last 0x451 --size "$size" \
  --name "$sym" --out "$BUILD/sim_$name.h"
done
clang++ -std=c++17 -O2 -Wall -Wextra \
 -I"$UI/include" -I"$ROOT/src" -I"$BUILD" \
 "$UI/src/FreeInkUI.cpp" "$HERE/renderer.cpp" -o "$BUILD/ybx-native-render"
echo "Built $BUILD/ybx-native-render"
