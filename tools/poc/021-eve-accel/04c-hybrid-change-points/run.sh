#!/bin/sh
# 04c: eve-emu with the Metal backend - batches cut at change points go to the GPU.
#   04c-hybrid-change-points/run.sh check   picture hashes on all captures (the gate)
#   04c-hybrid-change-points/run.sh time    timings (no hashes), twice; GPU threshold sweep
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
mkdir -p "$OUT"
export EVE_ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
case "$1" in
check)
    {
        "$POC/common/replay.sh" gpu boot --stats
        "$POC/common/replay.sh" gpu zuma --stats
        "$POC/common/replay.sh" gpu play --stats
        EVE_POC_DEFER=1 "$POC/common/replay.sh" gpu play --frames 4000 --stats
    } 2>&1 | tee "$OUT/check.txt"
    ;;
time)
    for run in 1 2; do
        "$POC/common/wait-load.sh" 600 12
        for trace in play boot zuma; do
            frames=""
            [ "$trace" = play ] && frames="--frames 4000"
            for min in 16 64 256; do
                # shellcheck disable=SC2086
                EVE_POC_GPU_MIN=$min "$POC/common/replay.sh" gpu $trace --no-hash $frames --stats
            done
            # shellcheck disable=SC2086
            EVE_POC_DEFER=1 EVE_POC_GPU_MIN=64 "$POC/common/replay.sh" gpu $trace --no-hash $frames --stats
            # shellcheck disable=SC2086
            EVE_POC_DEFER=1 EVE_POC_GPU_MIN=64 "$POC/common/replay.sh" gpu $trace --no-hash $frames --threads 8 --stats
        done
    done 2>&1 | tee "$OUT/time.txt"
    ;;
*)
    echo "usage: $0 check|time"; exit 2 ;;
esac
