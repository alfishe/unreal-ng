#!/bin/bash
# POC 039: clips through the filters, 4 at a time: run.sh [clip ...]
# FRAMES=<dir>: the captures (default POC 038's). Each clip: grid.mp4 + single videos of every filter, and
# grid-short.mp4 (the shortlist, 3x).
cd "$(dirname "$0")/../../.."
D=scratch/2026-10-28-poc-039
F=${FRAMES:-scratch/2026-10-26-poc-038/frames}
SHORT=${SHORT:-off,blend-narrow,interpolate,interp-rows}
mkdir -p $D
clang++ -std=c++20 -O2 -Wall -Wextra -Icore/src tools/poc/039-smoothing-filters/filters.cpp \
    core/src/emulator/video/smoothmotion.cpp core/src/emulator/video/smoothlayers.cpp -o $D/filters || exit 1
clips=("$@")
[ ${#clips[@]} -eq 0 ] && clips=($(ls $F | sed -n 's/\.rgba$//p'))
one()
{
    local c=$1 n
    n=$(python3 -c "import json; print(json.load(open('$F/$c.json'))['count'])")
    mkdir -p "$D/$c"
    nice "$D/filters" "$F/$c.rgba" 352 288 "$n" "$D/$c" data/rom/48.rom
    FILTERS_ONLY=$SHORT FILTERS_GRID=grid-short nice "$D/filters" "$F/$c.rgba" 352 288 "$n" "$D/$c" data/rom/48.rom
}
for c in "${clips[@]}"; do
    one "$c" &
    while [ "$(jobs -rp | wc -l)" -ge 4 ]; do sleep 1; done  # bash 3.2 (macOS): no wait -n
done
wait
