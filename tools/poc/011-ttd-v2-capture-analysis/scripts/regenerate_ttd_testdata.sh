#!/bin/bash
# Regenerate TTD test files from .sna snapshots using CLI automation
# Run this with the emulator available (e.g., after starting unreal-qt)

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUTPUT_DIR="$REPO_ROOT/testdata/ttd"
SNA_DIR="$REPO_ROOT/testdata/loaders/sna"

# Check if CLI exists
CLI="$REPO_ROOT/cmake-build-release/bin/unreal-cli"
if [[ ! -x "$CLI" ]]; then
    echo "Error: unreal-cli not found at $CLI"
    echo "Build with: cmake --build cmake-build-release --target unreal-cli"
    exit 1
fi

echo "Regenerating TTD test files..."

# Function to record TTD from a snapshot
record_ttd() {
    local sna="$1"
    local output="$2"
    local frames="${3:-300}"  # Default 300 frames (~6 sec)

    echo "Recording $output from $sna ($frames frames)..."

    "$CLI" --headless --model pentagon \
        --load "$sna" \
        --ttd-start \
        --run-frames "$frames" \
        --ttd-save "$output" \
        --quit
}

# Record each test file
record_ttd "$SNA_DIR/7threality.sna" "$OUTPUT_DIR/demo_7threality.ttd" 300

# For idle session, use any simple snapshot with minimal activity
record_ttd "$SNA_DIR/z80flags.sna" "$OUTPUT_DIR/idle_session.ttd" 300

# Active demo - use a demo with lots of activity
record_ttd "$SNA_DIR/scroller_by_demarche.sna" "$OUTPUT_DIR/active_demo.ttd" 300

# Heavy demo - use eyeache or similar
record_ttd "$SNA_DIR/eyeache1.sna" "$OUTPUT_DIR/demo_across-the-edge-second.ttd" 300

echo "Done. TTD test files regenerated in $OUTPUT_DIR"
ls -la "$OUTPUT_DIR"/*.ttd
