#!/usr/bin/env bash
# Records the input sessions of the TTD v2 Phase 1 experiments.
#
# The sessions are cases of the TTD benchmark matrix (core-benchmarks
# TTDMatrix/*, tools/verification/ttd-bench): replayable workloads (fixed start
# state, scripted input, frozen RTC, zeroed power-on RAM), so every run
# produces byte-identical files. They are written to
# <repo>/scratch/ttd-experiments/sessions/ (git-ignored), next to the fixture
# corpus in testdata/ttd/ that the experiments read as is.
#
# Usage: record-datasets.sh [build-dir]   (default: cmake-build-agent-release,
#        configured with -DBENCHMARKS=ON)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../../../.." && pwd)"
BUILD="${1:-$ROOT/cmake-build-agent-release}"
BENCH="$BUILD/bin/core-benchmarks"
OUT="$ROOT/scratch/ttd-experiments/sessions"
FRAMES=1500

[ -x "$BENCH" ] || { echo "core-benchmarks not found in $BUILD (configure with -DBENCHMARKS=ON)"; exit 1; }
mkdir -p "$OUT"

run() {   # $1 = matrix set, $2 = case filter (regex on <configuration>/<workload>)
    UNREAL_TTD_BENCH_SET="$1" UNREAL_TTD_BENCH_FRAMES=$FRAMES UNREAL_TTD_BENCH_SEEKS=0 \
    UNREAL_TTD_BENCH_OVERHEAD=0 UNREAL_TTD_BENCH_KEEP_SESSIONS="$OUT" \
        "$BENCH" --benchmark_filter="TTDMatrix/v1/($2)/" >/dev/null
}

# Large RAM and turbo (the configurations Phase 1 is about)
run turbo 'ATM3/idle|ATM3\+tsfm\+gs512\+moon/idle|ATM710-turbo/idle|SCORPION/idle|PENTAGON/demo'
# Workloads with memory churn and device memory
run full 'PENTAGON/game|PENTAGON/demo2|PENTAGON\+gs512/gs-upload|PENTAGON\+moon/moon-upload|PENTAGON\+beta/disk-loading|PENTAGON1024/idle|PROFI/idle'

ls -la "$OUT"
