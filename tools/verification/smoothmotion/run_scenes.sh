#!/bin/bash
# The synthetic scenes end to end: capture through the WebAPI of a running emulator (a Pentagon 128K instance of its
# own, muted, removed after), render every filter of filters.cpp, measure, check against the gates and the baseline.
#   run_scenes.sh <webapi port> [--update] [scene ...]      (results in scratch/smoothmotion/)
# 400 frames from the 50th (the programs set up for up to 35 frames): speeds restarts its balls every 386 pictures,
# so every ball crosses at least once.
set -e
cd "$(dirname "$0")/../../.."
PORT=${1:?webapi port}
shift
UPDATE=
if [ "${1:-}" = "--update" ]; then UPDATE=--update; shift; fi
SCENES=("$@")
[ ${#SCENES[@]} -eq 0 ] && SCENES=(speeds scroll cadence dots mixed)
O=scratch/smoothmotion
T=testdata/video/smoothmotion
FRAMES=400
mkdir -p $O
tools/verification/smoothmotion/compile-tool.sh tools/verification/smoothmotion/filters.cpp $O/filters
results=()
for m in "${SCENES[@]}"; do
    python3 -I tools/smoothmotion/capture.py --port "$PORT" --model PENTAGON:128 --out $O --warmup 50 \
        --frames $FRAMES $T/bin/smooth-$m.sna
    mkdir -p $O/$m
    FILTERS_ONLY=off,blend,blend-linear,blend-narrow,shutter,interpolate,layers,pan-global,pan-strips,interp-rows,extrapolate,game-cadence,game-carry \
        FILTERS_RAW=1 nice $O/filters $O/smooth-$m.rgba 352 288 $FRAMES $O/$m data/rom/48.rom > $O/$m/filters.log
    rm -f $O/$m/*.mp4
    echo "== $m"
    python3 -I tools/verification/smoothmotion/smoothness.py --source $O/smooth-$m.rgba --truth $T/truth/$m.json \
        --out-dir $O/$m --json $O/$m.json 2>&1 | grep -v "^  region"
    results+=($O/$m.json)
done
python3 -I tools/verification/smoothmotion/regress.py --baseline tools/verification/smoothmotion/baseline.json \
    $UPDATE "${results[@]}"
