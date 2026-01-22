#!/bin/bash
#
# Videowall Pattern Cycling Script
# Cycles through all available patterns with configurable delay
#
# Usage: ./cycle_patterns.sh [delay_seconds]
# Default delay: 15 seconds
#
# This script will:
# - Cycle through all 10 available patterns in order
# - Show progress and pattern descriptions
# - Sleep for specified delay between patterns
# - Loop forever until interrupted with Ctrl+C
#
# Requires: videowall with 48 instances running, pattern script in same directory
#

# Configuration
DEFAULT_DELAY=15
URL="http://localhost:8090"

# Available patterns (must match the Pattern enum in test_load_pattern_snapshots.py)
PATTERNS=(
    "all_same"
    "columns"
    "rows"
    "diagonal_down"
    "diagonal_up"
    "circles"
    "spiral"
    "checkerboard"
    "waves_horizontal"
    "waves_vertical"
)

# Pattern descriptions (indexed by position in PATTERNS array)
PATTERN_DESCRIPTIONS=(
    "All instances load the same snapshot"        # all_same
    "Same snapshot per column (6 instances each)" # columns
    "Same snapshot per row (8 instances each)"    # rows
    "Diagonal lines (top-right to bottom-left)"   # diagonal_down
    "Diagonal lines (top-left to bottom-right)"   # diagonal_up
    "Concentric circles from center"              # circles
    "Spiral pattern from center outward"          # spiral
    "Alternating checkerboard pattern"            # checkerboard
    "Horizontal wave pattern"                     # waves_horizontal
    "Vertical wave pattern"                       # waves_vertical
)

# Parse arguments
DELAY=${1:-$DEFAULT_DELAY}

# Validate delay is a number
if ! [[ "$DELAY" =~ ^[0-9]+$ ]]; then
    echo "ERROR: Delay must be a positive integer (seconds)"
    echo "Usage: $0 [delay_seconds]"
    echo "Default delay: $DEFAULT_DELAY seconds"
    exit 1
fi

# Check if pattern script exists
if [ ! -f "./test_load_pattern_snapshots.py" ]; then
    echo "ERROR: test_load_pattern_snapshots.py not found in current directory"
    exit 1
fi

# Function to get current timestamp
get_timestamp() {
    date '+%Y-%m-%d %H:%M:%S'
}

# Function to handle interrupt
cleanup() {
    echo ""
    echo "$(get_timestamp) - Pattern cycling stopped by user"
    exit 0
}

# Set up signal handler
trap cleanup SIGINT SIGTERM

# Main cycling loop
echo "$(get_timestamp) - Starting videowall pattern cycling"
echo "$(get_timestamp) - Delay between patterns: ${DELAY} seconds"
echo "$(get_timestamp) - Available patterns: ${#PATTERNS[@]}"
echo "$(get_timestamp) - Press Ctrl+C to stop"
echo ""

PATTERN_COUNT=${#PATTERNS[@]}
CYCLE_COUNT=0

while true; do
    CYCLE_COUNT=$((CYCLE_COUNT + 1))

    for i in "${!PATTERNS[@]}"; do
        PATTERN="${PATTERNS[$i]}"
        DESCRIPTION="${PATTERN_DESCRIPTIONS[$i]}"

        echo "$(get_timestamp) - [Cycle $CYCLE_COUNT, Pattern $((i+1))/$PATTERN_COUNT]"
        echo "$(get_timestamp) - Loading pattern: $PATTERN"
        echo "$(get_timestamp) - Description: $DESCRIPTION"

        # Run the pattern loading script
        if python3 ./test_load_pattern_snapshots.py --url "$URL" --pattern "$PATTERN"; then
            echo "$(get_timestamp) - ✓ Pattern '$PATTERN' loaded successfully"
        else
            echo "$(get_timestamp) - ✗ Failed to load pattern '$PATTERN'"
            echo "$(get_timestamp) - Continuing with next pattern..."
        fi

        # Sleep between patterns (but not after the last pattern in a cycle)
        if [ $i -lt $((PATTERN_COUNT - 1)) ]; then
            echo "$(get_timestamp) - Sleeping ${DELAY} seconds before next pattern..."
            sleep "$DELAY"
        fi

        echo ""
    done

    echo "$(get_timestamp) - Completed cycle $CYCLE_COUNT, starting next cycle..."
    echo ""
done