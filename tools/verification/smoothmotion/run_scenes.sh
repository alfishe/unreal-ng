#!/bin/bash
# The synthetic scenes end to end: capture through the WebAPI of a running emulator (muted by capture.py), render
# every POC 047 filter, measure, check against the gates and the baseline.
#   run_scenes.sh <webapi port> [--update]      (results in scratch/smoothmotion/)
set -e
cd "$(dirname "$0")/../../.."
PORT=${1:?webapi port}
UPDATE=$2
O=scratch/smoothmotion
T=testdata/video/smoothmotion
mkdir -p $O
clang++ -std=c++20 -O2 -Wall -Wextra -Icore/src tools/poc/047-smoothing-filters/filters.cpp \
    core/src/emulator/video/smoothmotion.cpp core/src/emulator/video/smoothlayers.cpp core/src/common/threadpool.cpp \
    core/src/common/threadhelper.cpp -o $O/filters
results=()
for m in speeds scroll cadence dots; do
    python3 -I tools/poc/047-smoothing-filters/capture.py --port "$PORT" --out $O --warmup 50 --frames 300 $T/smooth-$m.sna
    mkdir -p $O/$m
    FILTERS_ONLY=off,blend,blend-linear,blend-narrow,shutter,interpolate,pan-global,pan-strips,interp-rows,extrapolate,game-cadence,game-carry \
        FILTERS_RAW=1 nice $O/filters $O/smooth-$m.rgba 352 288 300 $O/$m data/rom/48.rom > $O/$m/filters.log
    rm -f $O/$m/*.mp4
    echo "== $m"
    python3 -I tools/verification/smoothmotion/smoothness.py --source $O/smooth-$m.rgba --truth $T/truth/$m.json \
        --out-dir $O/$m --json $O/$m.json 2>&1 | grep -v "^  region"
    results+=($O/$m.json)
done
python3 -I tools/verification/smoothmotion/regress.py --baseline tools/verification/smoothmotion/baseline.json \
    $UPDATE "${results[@]}"
