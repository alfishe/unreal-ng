#!/usr/bin/env bash
# Records the input sessions of experiment E7 with a write journal ring large
# enough never to wrap (1 GB), so each session holds its whole write history:
# - the real-use sessions of E6 (Pentagon 128 at the BASIC prompt, a game with
#   scripted input, 7th Reality, Across the Edge, Eye Ache, ZX-Evo at the BASIC
#   prompt), 1 minute (3,000 frames) and 5 minutes (15,000 frames);
# - the matrix cases with the most writes, 1 minute: 48K at the BASIC prompt,
#   ATM Turbo 2 (v4.50), ZX-Evo with General Sound 512, MoonSound, TurboSound FM.
#
# Writes <repo>/scratch/ttd-experiments/e7/<length>/: the saved sessions
# (*.ttd) and the benchmark's metrics (bench.json, BM-5 replay timings among
# them). The workloads are replayable: a re-run gives byte-identical sessions.
#
# Usage: record-sessions.sh [build dir]   (default: <repo>/cmake-build-agent-release)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../../../.." && pwd)"
BUILD="${1:-$ROOT/cmake-build-agent-release}"
BENCH="$BUILD/bin/core-benchmarks"
REAL='PENTAGON/(idle|game|demo|demo2|demo-eyeache)|ATM3/idle'
HEAVY='48K/idle|ATM450/idle|ATM3\+(gs512|moon|tsfm)/idle'

[ -x "$BENCH" ] || { echo "core-benchmarks not found in $BUILD (configure with -DBENCHMARKS=ON)"; exit 1; }

record() {   # <name> <frames> <case regex>
    local out="$ROOT/scratch/ttd-experiments/e7/$1"
    mkdir -p "$out"
    UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_FRAMES="$2" UNREAL_TTD_BENCH_SEEKS=200 \
    UNREAL_TTD_BENCH_OVERHEAD=0 UNREAL_TTD_BENCH_JOURNAL_MB=1024 UNREAL_TTD_BENCH_KEEP_SESSIONS="$out" \
        nice -n "${UNREAL_NICE:-10}" "$BENCH" --benchmark_filter="TTDMatrix/v1/($3)/" \
        --benchmark_format=json --benchmark_out="$out/bench.json" >/dev/null
    echo "$1: $(ls "$out"/*.ttd | wc -l | tr -d ' ') sessions in $out"
}

record 1min 3000 "$REAL|$HEAVY"
record 5min 15000 "$REAL"
