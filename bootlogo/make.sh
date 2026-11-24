#!/bin/bash
# Builds bootanimation.zip (720x1600): logo fades in over black, then holds until boot finishes.
# Usage: ./make.sh [logo.png]   Zip must be STORED (zip -0) or bootanimation refuses to play it.
set -euo pipefail
cd "$(dirname "$0")"
LOGO="${1:-/home/casper/Downloads/Stealthy hooded ghost tactical emblem.png}"
W=720; H=1600; SIZE=440; FADE=15   # SIZE = logo width px on screen
rm -rf build && mkdir -p build/part0 build/part1
magick "$LOGO" -background black -alpha remove -alpha off -resize ${SIZE}x${SIZE} build/logo.png
for i in $(seq 0 $((FADE-1))); do   # brightness ramps 0 -> 100%
  magick -size ${W}x${H} xc:black build/logo.png -gravity center -composite \
    -evaluate Multiply $(awk "BEGIN{printf \"%.3f\", $i/($FADE-1)}") PNG24:build/part0/$(printf %05d $i).png
done
cp build/part0/$(printf %05d $((FADE-1))).png build/part1/00000.png
printf '%s %s 30\np 1 0 part0\np 0 0 part1\n' $W $H > build/desc.txt
(cd build && rm -f ../bootanimation.zip && zip -q -0 -r ../bootanimation.zip desc.txt part0 part1)
ls -la bootanimation.zip
