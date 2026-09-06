#!/bin/bash
#
# run-tests-parallel.sh - Execute GTest tests with parallel sharding
#
# Uses GTest's built-in sharding mechanism to split tests across multiple
# processes, achieving a multi-x speedup (40s -> ~7s warm on a 20-core
# Apple Silicon machine).
#
# How it works:
#   - GTEST_TOTAL_SHARDS tells GTest the total number of parallel runners
#   - GTEST_SHARD_INDEX tells each runner which subset of tests to run
#   - GTest automatically distributes tests evenly across shards
#
# Usage:
#   ./scripts/run-tests-parallel.sh                    # Uses default binary
#   ./scripts/run-tests-parallel.sh ./build/bin/core-tests  # Explicit path
#   ./scripts/run-tests-parallel.sh ./build/bin/core-tests 16   # Explicit shard count
#   TEST_SHARDS=16 ./scripts/run-tests-parallel.sh     # Shard count via env
#
# Shards default to auto-detection from the logical CPU count
# (DetectShardCount below): every logical core counts as one worker - no
# P/E core weighting (Linux: capped by the cgroup v2 CPU quota). See the
# comment above DetectShardCount to switch back to Apple Silicon P/E
# weighted detection.
#
# Requirements:
#   - Tests must be isolated (no shared global state between tests)
#   - Tests must not write to fixed temp file paths
#
# Performance (2154 tests, 16P+4E Mac Studio, warm):
#   - Sequential: ~40s
#   - 20-way parallel (all logical cores): ~5-7s
#

set -e  # Exit on any error

# Configuration
BINARY="${1:-./build/bin/core-tests}"  # Test binary path (default: standard location)
# Shard count: 2nd argument > TEST_SHARDS env > auto-detect from CPU
SHARDS="${2:-${TEST_SHARDS:-auto}}"

# Auto-detect the number of test workers from the CPU count. Every logical
# core counts as one worker; P/E core classes are deliberately NOT weighted:
# the suite is tail-latency bound (wall time = slowest shard, since GTest
# shards by test count, not runtime), and finer sharding shortens the tail
# even when some shards land on slower E cores (measured on 16P+4E: 17-way
# critical path 4.8s vs 20-way 3.9s).
#   - macOS: hw.ncpu (all logical cores, Intel and Apple Silicon alike).
#   - other Unix: online logical CPU count.
#   - Linux: nproc (affinity-aware), capped by the cgroup v2 CPU quota
#     so containers and CI runners do not oversubscribe.
#
# To switch back to weighted mode (de-rate Apple Silicon E cores to ~1/3 of
# a P core - e.g. when tests share the machine with heavy builds and
# aggregate throughput matters more than the shard tail), replace the
# Darwin branch below with:
#     p=$(sysctl -n hw.perflevel0.logicalcpu 2>/dev/null || echo 0)
#     e=$(sysctl -n hw.perflevel1.logicalcpu 2>/dev/null || echo 0)
#     if [ "$p" -gt 0 ]; then
#         echo $(( p + e / 3 ))
#         return
#     fi
#     sysctl -n hw.ncpu
#     return
DetectShardCount()
{
    if [ "$(uname)" = "Darwin" ]; then
        sysctl -n hw.ncpu
        return
    fi

    cpus=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
    if [ -r /sys/fs/cgroup/cpu.max ]; then
        read -r quota period < /sys/fs/cgroup/cpu.max
        if [ "$quota" != "max" ] && [ -n "$period" ] && [ "$period" -gt 0 ]; then
            cg=$(( quota / period ))
            if [ "$cg" -ge 1 ] && [ "$cg" -lt "$cpus" ]; then
                cpus=$cg
            fi
        fi
    fi
    echo "$cpus"
}

if [ "$SHARDS" = "auto" ]; then
    SHARDS=$(DetectShardCount)
    echo "Auto-detected $SHARDS shards from logical CPU count"
fi
if ! [ "$SHARDS" -ge 1 ] 2>/dev/null; then
    echo "Error: invalid shard count: $SHARDS"
    exit 1
fi

# Validate binary exists and is executable
if [ ! -x "$BINARY" ]; then
    echo "Error: Test binary not found or not executable: $BINARY"
    echo ""
    echo "Build with:"
    echo "  cmake --build build --target core-tests"
    exit 1
fi

echo "=========================================="
echo "Running $SHARDS-way parallel tests"
echo "Binary: $BINARY"
echo "=========================================="

# Normalize the binary path (drop "." segments) where realpath is available,
# so exe-path-based test helpers never see a "./"-containing executable path
if command -v realpath > /dev/null 2>&1; then
    BINARY=$(realpath "$BINARY")
fi

# Launch all shards in parallel
# Each shard runs a different subset of tests based on GTEST_SHARD_INDEX
pids=""
for i in $(seq 0 $((SHARDS-1))); do
    echo "Starting shard $i/$((SHARDS-1))..."
    GTEST_TOTAL_SHARDS=$SHARDS GTEST_SHARD_INDEX=$i "$BINARY" &
    pids="$pids $i:$!"
done

# Wait for every shard and track failures (a bare `wait` would return 0 even
# when children failed, silently masking red test runs)
failedShards=""
for entry in $pids; do
    i=${entry%%:*}
    pid=${entry##*:}
    if ! wait "$pid"; then
        failedShards="$failedShards $i"
        echo "Shard $i FAILED"
    fi
done

echo "=========================================="
if [ -n "$failedShards" ]; then
    echo "FAILED shards:$failedShards"
    echo "=========================================="
    exit 1
fi
echo "All $SHARDS shards complete."
echo "=========================================="
