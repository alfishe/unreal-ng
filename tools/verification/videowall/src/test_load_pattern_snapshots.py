#!/usr/bin/env python3
"""
Test: Load Pattern Snapshots to Videowall (8x6 Matrix)

This test loads snapshots in various patterns to a fullscreen videowall with 48 emulator instances.
Requires exactly 48 instances arranged in an 8x6 grid.
Uses whitelist filtering to restrict which snapshots can be loaded.

Usage:
    python test_load_pattern_snapshots.py --url http://localhost:8090 --pattern <pattern_name> [--whitelist FILE]

Available patterns:
    all_same        - All instances load the same snapshot
    columns         - Same snapshot per column (6 instances per column)
    rows            - Same snapshot per row (8 instances per row)
    diagonal_down   - Diagonal lines (top-left to bottom-right)
    diagonal_up     - Diagonal lines (top-right to bottom-left)
    circles         - Concentric circles from center
    spiral          - Spiral pattern from center outward
    checkerboard    - Alternating pattern like a checkerboard
    waves_horizontal- Horizontal wave pattern
    waves_vertical  - Vertical wave pattern
"""

import argparse
import sys
import time
from typing import List, Dict, Any
from enum import Enum

from webapi_client import WebAPIClient, TestDataHelper


class Pattern(Enum):
    ALL_SAME = "all_same"
    COLUMNS = "columns"
    ROWS = "rows"
    DIAGONAL_DOWN = "diagonal_down"
    DIAGONAL_UP = "diagonal_up"
    CIRCLES = "circles"
    SPIRAL = "spiral"
    CHECKERBOARD = "checkerboard"
    WAVES_HORIZONTAL = "waves_horizontal"
    WAVES_VERTICAL = "waves_vertical"


def log(message: str):
    """Print timestamped log message."""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}")


def get_pattern_description(pattern: Pattern) -> str:
    """Get human-readable description of a pattern."""
    descriptions = {
        Pattern.ALL_SAME: "All instances load the same snapshot",
        Pattern.COLUMNS: "Same snapshot per column (6 instances each)",
        Pattern.ROWS: "Same snapshot per row (8 instances each)",
        Pattern.DIAGONAL_DOWN: "Diagonal lines (top-right to bottom-left)",
        Pattern.DIAGONAL_UP: "Diagonal lines (top-left to bottom-right)",
        Pattern.CIRCLES: "Concentric circles from center",
        Pattern.SPIRAL: "Spiral pattern from center outward",
        Pattern.CHECKERBOARD: "Alternating checkerboard pattern",
        Pattern.WAVES_HORIZONTAL: "Horizontal wave pattern",
        Pattern.WAVES_VERTICAL: "Vertical wave pattern",
    }
    return descriptions.get(pattern, str(pattern))


def arrange_instances_grid(emulators: List[Dict[str, Any]]) -> List[List[str]]:
    """
    Arrange emulator instances into an 8x6 grid.
    First 8 emulators = first row, next 8 = second row, etc.
    Returns a 6x8 grid (rows x columns) of emulator IDs.
    """
    if len(emulators) != 48:
        raise ValueError(f"Expected 48 emulator instances, got {len(emulators)}")

    # Use emulators in their original order (no sorting)
    # First 8 = row 0, next 8 = row 1, etc.

    # Create 6 rows × 8 columns grid
    grid = []
    for row in range(6):
        grid_row = []
        for col in range(8):
            idx = row * 8 + col
            grid_row.append(emulators[idx]['id'])
        grid.append(grid_row)

    return grid


def generate_pattern_assignments(grid: List[List[str]], pattern: Pattern, snapshots: List[str]) -> Dict[str, str]:
    """
    Generate snapshot assignments for each emulator instance based on the pattern.

    Args:
        grid: 6x8 grid of emulator IDs (rows x columns)
        pattern: Pattern to use for assignment
        snapshots: List of available snapshot paths

    Returns:
        Dict mapping emulator_id to snapshot_path
    """
    assignments = {}

    if pattern == Pattern.ALL_SAME:
        # All instances get the same snapshot
        snapshot = snapshots[0] if snapshots else None
        for row in grid:
            for emu_id in row:
                assignments[emu_id] = snapshot

    elif pattern == Pattern.COLUMNS:
        # Each column gets the same snapshot
        for col in range(8):
            snapshot = snapshots[col % len(snapshots)]
            for row in range(6):
                assignments[grid[row][col]] = snapshot

    elif pattern == Pattern.ROWS:
        # Each row gets the same snapshot
        for row in range(6):
            snapshot = snapshots[row % len(snapshots)]
            for col in range(8):
                assignments[grid[row][col]] = snapshot

    elif pattern == Pattern.DIAGONAL_DOWN:
        # Diagonal lines: top-right to bottom-left
        for row in range(6):
            for col in range(8):
                diagonal_idx = (row - col) % len(snapshots)
                assignments[grid[row][col]] = snapshots[diagonal_idx]

    elif pattern == Pattern.DIAGONAL_UP:
        # Diagonal lines: top-left to bottom-right
        for row in range(6):
            for col in range(8):
                diagonal_idx = (row + col) % len(snapshots)
                assignments[grid[row][col]] = snapshots[diagonal_idx]

    elif pattern == Pattern.CIRCLES:
        # Concentric circles from center
        center_row, center_col = 2.5, 3.5  # Center of 6x8 grid
        for row in range(6):
            for col in range(8):
                # Calculate distance from center
                distance = ((row - center_row) ** 2 + (col - center_col) ** 2) ** 0.5
                circle_idx = int(distance) % len(snapshots)
                assignments[grid[row][col]] = snapshots[circle_idx]

    elif pattern == Pattern.SPIRAL:
        # Spiral pattern from center outward
        center_row, center_col = 2, 3  # Integer center
        directions = [(0, 1), (1, 0), (0, -1), (-1, 0)]  # right, down, left, up
        row, col = center_row, center_col
        dir_idx = 0
        steps = 1
        step_count = 0
        snapshot_idx = 0

        # Fill center first
        assignments[grid[row][col]] = snapshots[snapshot_idx % len(snapshots)]
        snapshot_idx += 1

        while len(assignments) < 48:
            # Move in current direction for 'steps' steps
            for _ in range(steps):
                dr, dc = directions[dir_idx]
                row += dr
                col += dc

                if 0 <= row < 6 and 0 <= col < 8:
                    emu_id = grid[row][col]
                    if emu_id not in assignments:
                        assignments[emu_id] = snapshots[snapshot_idx % len(snapshots)]
                        snapshot_idx += 1

            # Change direction
            dir_idx = (dir_idx + 1) % 4

            # Increase steps every two direction changes
            if dir_idx % 2 == 0:
                steps += 1

    elif pattern == Pattern.CHECKERBOARD:
        # Alternating checkerboard pattern
        for row in range(6):
            for col in range(8):
                pattern_idx = (row + col) % 2
                assignments[grid[row][col]] = snapshots[pattern_idx % len(snapshots)]

    elif pattern == Pattern.WAVES_HORIZONTAL:
        # Horizontal wave pattern
        for row in range(6):
            for col in range(8):
                wave_idx = int(3 * (1 + (col / 7.0) * 2) * (row / 5.0))
                assignments[grid[row][col]] = snapshots[wave_idx % len(snapshots)]

    elif pattern == Pattern.WAVES_VERTICAL:
        # Vertical wave pattern
        for row in range(6):
            for col in range(8):
                wave_idx = int(3 * (1 + (row / 5.0) * 2) * (col / 7.0))
                assignments[grid[row][col]] = snapshots[wave_idx % len(snapshots)]

    return assignments


def run_test(base_url: str, pattern: Pattern, whitelist_file: str) -> bool:
    """
    Run the pattern snapshot loading test.

    Args:
        base_url: WebAPI server URL
        pattern: Pattern to use for loading
        whitelist_file: Path to whitelist file containing allowed snapshots

    Returns:
        True if test passed, False otherwise
    """
    client = WebAPIClient(base_url)
    testdata = TestDataHelper(whitelist_file)

    log(f"Connecting to WebAPI at {base_url}...")

    # Check server health
    if not client.check_health():
        log("ERROR: WebAPI server is not reachable")
        return False

    log("WebAPI server is healthy")

    # Step 1: Get emulator instances
    log("Step 1: Getting emulator instances...")
    emulators = client.list_emulators()

    if len(emulators) != 48:
        log(f"ERROR: Expected 48 emulator instances for 8x6 videowall, got {len(emulators)}")
        log("Please ensure the videowall is in fullscreen mode with 48 instances.")
        return False

    log(f"Found {len(emulators)} emulator instances (perfect for 8x6 videowall)")

    # Step 2: Arrange instances into grid
    log("Step 2: Arranging instances into 8x6 grid...")
    try:
        grid = arrange_instances_grid(emulators)
        log("✓ Instances arranged into grid")
    except ValueError as e:
        log(f"ERROR: {e}")
        return False

    # Step 3: Get available snapshots
    log("Step 3: Getting available snapshots...")
    snapshots = testdata.get_snapshots()
    if not snapshots:
        log("ERROR: No snapshots available")
        return False

    log(f"Found {len(snapshots)} snapshots in whitelist")

    # Step 4: Generate pattern assignments
    log(f"Step 4: Generating {pattern.value} pattern assignments...")
    assignments = generate_pattern_assignments(grid, pattern, snapshots)
    log("✓ Pattern assignments generated")

    # Note: Sound is now disabled at emulator creation in VideoWallWindow::addEmulatorTile()
    # No need to disable sound via WebAPI anymore

    # Step 5: Load snapshots according to pattern
    log(f"Step 5: Loading snapshots using {pattern.value} pattern...")
    load_success = 0

    for emu_id, snapshot_path in assignments.items():
        snapshot_name = snapshot_path.split('/')[-1]
        log(f"  Loading '{snapshot_name}' to {emu_id[:8]}...")

        if client.load_snapshot(emu_id, snapshot_path):
            log("  ✓ Loaded successfully")
            load_success += 1
        else:
            log("  ✗ Failed to load snapshot")
    log(f"Snapshots loaded on {load_success}/48 instances")

    # Step 6: Ensure all instances are running
    log("Step 6: Ensuring all instances are running...")
    start_success = 0

    for emu_id in assignments.keys():
        emu_info = client.get_emulator(emu_id)
        if not emu_info:
            log(f"  ✗ {emu_id[:8]}... failed to get status")
            continue

        # Use boolean fields
        is_running = emu_info.get('is_running', False)
        is_paused = emu_info.get('is_paused', False)

        if is_running and not is_paused:
            log(f"  ✓ {emu_id[:8]}... already running")
            start_success += 1
        elif is_paused:
            if client.resume_emulator(emu_id):
                log(f"  ✓ {emu_id[:8]}... resumed")
                start_success += 1
            else:
                log(f"  ✗ {emu_id[:8]}... failed to resume")
        else:
            if client.start_emulator(emu_id):
                log(f"  ✓ {emu_id[:8]}... started")
                start_success += 1
            else:
                log(f"  ✗ {emu_id[:8]}... failed to start")

    log(f"Started/running: {start_success}/48 instances")

    # Summary
    log("\n" + "=" * 60)
    log("PATTERN TEST SUMMARY")
    log("=" * 60)
    log(f"  Pattern:           {pattern.value}")
    log(f"  Description:       {get_pattern_description(pattern)}")
    log(f"  Total instances:   48")
    log(f"  Available snapshots: {len(snapshots)}")
    log(f"  Snapshots loaded:  {load_success}/48")
    log(f"  Running/resumed:   {start_success}/48")

    # Determine overall success
    all_passed = (load_success == 48 and start_success == 48)

    if all_passed:
        log("\n✅ TEST PASSED: Pattern loaded successfully")
    else:
        log("\n⚠️  TEST PARTIAL: Some operations failed")

    return all_passed


def main():
    parser = argparse.ArgumentParser(
        description="Load snapshots in patterns to 8x6 videowall (48 instances)"
    )
    parser.add_argument(
        "--url",
        default="http://localhost:8090",
        help="WebAPI base URL (default: http://localhost:8090)"
    )
    parser.add_argument(
        "--pattern",
        required=True,
        choices=[p.value for p in Pattern],
        help="Pattern to use for loading snapshots"
    )
    parser.add_argument(
        "--whitelist",
        default="../whitelist.txt",
        help="Path to whitelist file containing allowed snapshot filenames. "
             "Each line must contain exact filename with extension (.sna or .z80). "
             "Lines starting with # are comments. "
             "Defaults to ../whitelist.txt."
    )

    args = parser.parse_args()

    # Validate pattern
    try:
        pattern = Pattern(args.pattern)
    except ValueError:
        log(f"ERROR: Invalid pattern '{args.pattern}'")
        sys.exit(1)

    log("=" * 60)
    log("VIDEOWALL PATTERN TEST: Load Pattern Snapshots")
    log("=" * 60)
    log(f"Pattern: {pattern.value}")
    log(f"Description: {get_pattern_description(pattern)}")
    log(f"Using whitelist: {args.whitelist}")
    log("=" * 60)

    try:
        success = run_test(args.url, pattern, args.whitelist)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        log("\nTest interrupted by user")
        sys.exit(130)
    except Exception as e:
        log(f"\nERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()