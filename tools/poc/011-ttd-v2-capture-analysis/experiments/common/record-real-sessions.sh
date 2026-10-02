#!/usr/bin/env bash
# Records the "real use" sessions of experiments E5 and E6 with the TTD
# benchmark harness: Pentagon 128 at the BASIC prompt, a game with scripted
# input, three demos (7th Reality, Across the Edge, Eye Ache) and ZX-Evo at
# the BASIC prompt; 1 minute (3,000 frames) and 5 minutes (15,000 frames) each.
#
# Writes <repo>/scratch/ttd-experiments/real/<length>/: the saved sessions
# (*.ttd) and the benchmark's metrics with the heap split (bench.json).
# The workloads are replayable, so a re-run gives byte-identical sessions.
#
# Usage: record-real-sessions.sh [build dir]   (default: <repo>/cmake-build-agent-release)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../../../.." && pwd)"
BUILD="${1:-$ROOT/cmake-build-agent-release}"
BENCH="$BUILD/bin/core-benchmarks"
CASES='PENTAGON/(idle|game|demo|demo2|demo-eyeache)|ATM3/idle'

[ -x "$BENCH" ] || { echo "core-benchmarks not found in $BUILD (configure with -DBENCHMARKS=ON)"; exit 1; }

for spec in 1min:3000 5min:15000; do
    name="${spec%%:*}"
    frames="${spec##*:}"
    out="$ROOT/scratch/ttd-experiments/real/$name"
    mkdir -p "$out"
    UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_FRAMES="$frames" UNREAL_TTD_BENCH_SEEKS=20 \
    UNREAL_TTD_BENCH_OVERHEAD=0 UNREAL_TTD_BENCH_KEEP_SESSIONS="$out" \
        "$BENCH" --benchmark_filter="TTDMatrix/v1/($CASES)/" \
        --benchmark_format=json --benchmark_out="$out/bench.json" >/dev/null
    echo "$name: $(ls "$out"/*.ttd | wc -l | tr -d ' ') sessions in $out"
done
