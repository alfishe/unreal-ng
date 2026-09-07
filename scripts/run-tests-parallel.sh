#!/bin/bash
#
# run-tests-parallel.sh - Execute GTest tests with parallel sharding
#
# Uses GTest's built-in sharding mechanism to split tests across multiple
# processes, achieving ~3x speedup on 4-core machines (37s -> 12s).
#
# How it works:
#   - GTEST_TOTAL_SHARDS tells GTest the total number of parallel runners
#   - GTEST_SHARD_INDEX tells each runner which subset of tests to run
#   - GTest automatically distributes tests evenly across shards
#
# Usage:
#   ./scripts/run-tests-parallel.sh                    # Uses default binary
#   ./scripts/run-tests-parallel.sh ./build/bin/core-tests  # Explicit path
#
# Requirements:
#   - Tests must be isolated (no shared global state between tests)
#   - Tests must not write to fixed temp file paths
#
# Performance:
#   - Sequential: ~37s (76% CPU on single core)
#   - 4-way parallel: ~12s (250% CPU across 4 cores)
#   - Speedup: ~3.2x
#

set -e  # Exit on any error

# Configuration
BINARY="${1:-./build/bin/core-tests}"  # Test binary path (default: standard location)
SHARDS=4                                # Number of parallel shards (tune for your CPU)

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

# Launch all shards in parallel
# Each shard runs a different subset of tests based on GTEST_SHARD_INDEX
for i in $(seq 0 $((SHARDS-1))); do
    echo "Starting shard $i/$((SHARDS-1))..."
    GTEST_TOTAL_SHARDS=$SHARDS GTEST_SHARD_INDEX=$i "$BINARY" &
done

# Wait for all shards to complete
wait

echo "=========================================="
echo "All $SHARDS shards complete."
echo "=========================================="
