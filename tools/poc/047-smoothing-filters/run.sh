#!/bin/bash
# POC 047: clips through the filters, 4 at a time: run.sh [clip ...]
# FRAMES=<dir>: the captures (default POC 049's). Each clip: grid.mp4 + single videos of every filter, and
# grid-short.mp4 (the shortlist, 3x).
cd "$(dirname "$0")/../../.."
D=scratch/2026-10-28-poc-047
F=${FRAMES:-scratch/2026-10-26-poc-046/frames}
SHORT=${SHORT:-off,blend-narrow,interpolate,interp-rows}
mkdir -p $D
tools/verification/smoothmotion/compile-tool.sh tools/verification/smoothmotion/filters.cpp $D/filters || exit 1
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
