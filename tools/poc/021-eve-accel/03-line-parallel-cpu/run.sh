#!/bin/sh
# 03: lines in parallel on the CPU.
#   03-line-parallel-cpu/run.sh check    picture hashes with 8 threads (+ deferred writes) on all captures
#   03-line-parallel-cpu/run.sh scale    1/2/4/8/16 threads, twice, play (4000 frames), boot, zuma
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
mkdir -p "$OUT"
case "$1" in
check)
    {
        "$POC/common/replay.sh" threads boot --threads 8 --stats
        "$POC/common/replay.sh" threads zuma --threads 8 --stats
        EVE_POC_DEFER=1 "$POC/common/replay.sh" threads boot --threads 8 --stats
        EVE_POC_DEFER=1 "$POC/common/replay.sh" threads zuma --threads 8 --stats
        EVE_POC_DEFER=1 "$POC/common/replay.sh" threads play --threads 8 --stats
    } 2>&1 | tee "$OUT/check.txt"
    ;;
scale)
    for run in 1 2; do
        "$POC/common/wait-load.sh" 600 12
        for trace in play boot zuma; do
            frames=""
            [ "$trace" = play ] && frames="--frames 4000"
            for t in 1 2 4 8 16; do
                # shellcheck disable=SC2086
                "$POC/common/replay.sh" threads $trace --no-hash $frames --threads $t --stats
            done
            # shellcheck disable=SC2086
            EVE_POC_DEFER=1 "$POC/common/replay.sh" threads $trace --no-hash $frames --threads 8 --stats
        done
    done 2>&1 | tee "$OUT/scale.txt"
    ;;
*)
    echo "usage: $0 check|scale"; exit 2 ;;
esac
