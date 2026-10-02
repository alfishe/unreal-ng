#!/bin/sh
# 01: baseline real-time factor of the vendored eve-emu (NEON and the plain C++ fallback)
# on the three captures, twice each, plus a CPU profile of the play capture.
#   01-baseline/run.sh            timings -> out/timing.txt
#   01-baseline/run.sh profile [play|zuma]   `sample` of a replay -> out/profile-<trace>.txt
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
mkdir -p "$OUT"
case "$1" in
profile)
    trace=${2:-play}
    # the replay reads the whole capture first: start sampling once that is done
    "$POC/common/replay.sh" base "$trace" --no-hash --frames 4000 > "$OUT/profile-run-$trace.txt" 2>&1 &
    pid=$!
    sleep 6
    sample "$pid" 10 -file "$OUT/profile-$trace.txt" > /dev/null 2>&1
    wait "$pid"
    echo "profile written to $OUT/profile-$trace.txt"
    ;;
*)
    for run in 1 2; do
        "$POC/common/wait-load.sh" 600 12
        for v in base scalar; do
            "$POC/common/replay.sh" $v boot --no-hash
            "$POC/common/replay.sh" $v play --no-hash --frames 4000
            "$POC/common/replay.sh" $v zuma --no-hash
        done
    done 2>&1 | tee "$OUT/timing.txt"
    ;;
esac
