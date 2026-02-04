#!/usr/bin/env python3
"""
Test: Load Pattern Snapshots to Videowall (15x11 Matrix)

This test loads snapshots in various patterns to a fullscreen videowall with 165 emulator instances.
Requires exactly 165 instances arranged in a 15x11 grid.
Uses whitelist filtering to restrict which snapshots can be loaded.

Usage:
    python test_load_pattern_snapshots.py --url http://localhost:8090 --pattern <pattern_name> [--whitelist FILE]

Available patterns:
    all_same        - All instances load the same snapshot
    columns         - Same snapshot per column (11 instances per column)
    rows            - Same snapshot per row (15 instances per row)
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
from typing import List, Dict, Any, Tuple
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
        Pattern.COLUMNS: "Same snapshot per column (11 instances each)",
        Pattern.ROWS: "Same snapshot per row (15 instances each)",
        Pattern.DIAGONAL_DOWN: "Diagonal lines (top-right to bottom-left)",
        Pattern.DIAGONAL_UP: "Diagonal lines (top-left to bottom-right)",
        Pattern.CIRCLES: "Concentric circles from center",
        Pattern.SPIRAL: "Spiral pattern from center outward",
        Pattern.CHECKERBOARD: "Alternating checkerboard pattern",
        Pattern.WAVES_HORIZONTAL: "Horizontal wave pattern",
        Pattern.WAVES_VERTICAL: "Vertical wave pattern",
    }
    return descriptions.get(pattern, str(pattern))


def arrange_instances_grid(emulators: List[Dict[str, Any]]) -> Tuple[List[List[str]], int, int]:
    """
    Arrange emulator instances into a grid.
    Grid dimensions are calculated dynamically from the number of instances.
    Returns (grid, rows, cols) tuple.
    """
    count = len(emulators)
    if count == 0:
        return [], 0, 0
    
    # Calculate grid dimensions (same logic as C++ TileLayoutManager)
    import math
    cols = int(math.ceil(math.sqrt(count)))
    rows = int(math.ceil(count / cols))
    
    # But for fullscreen, we need to detect actual arrangement
    # Grid configurations for common resolutions and tile sizes
    # Both ceiling division (fills screen) and floor division (exact fit) are supported
    known_grids = {
        # Ceiling division: cols = ceil(W/tileW), rows = ceil(H/tileH)
        180: (15, 12),  # 4K with 256x196 tiles (ceiling)
        90: (10, 9),    # QHD with 256x196 tiles (ceiling)
        48: (8, 6),     # FullHD with 256x196 tiles (ceiling) / 4K with 512x384 (ceiling)
        25: (5, 5),     # QHD with 512x384 tiles (ceiling)
        12: (4, 3),     # FullHD with 512x384 tiles (ceiling)
        # Floor division: cols = floor(W/tileW), rows = floor(H/tileH)
        165: (15, 11),  # 4K with 256x196 tiles (floor)
        80: (10, 8),    # QHD with 256x196 tiles (floor)
        35: (7, 5),     # FullHD with 256x196 tiles (floor) / 4K with 512x384 (floor)
        20: (5, 4),     # QHD with 512x384 tiles (floor)
        6: (3, 2),      # FullHD with 512x384 tiles (floor)
    }
    
    if count in known_grids:
        cols, rows = known_grids[count]
    
    # Create grid
    grid = []
    idx = 0
    for row in range(rows):
        grid_row = []
        for col in range(cols):
            if idx < count:
                grid_row.append(emulators[idx]['id'])
                idx += 1
            else:
                grid_row.append(None)  # Empty cell
        grid.append(grid_row)

    return grid, rows, cols


def generate_pattern_assignments(grid: List[List[str]], rows: int, cols: int, pattern: Pattern, snapshots: List[str]) -> Dict[str, str]:
    """
    Generate snapshot assignments for each emulator instance based on the pattern.

    Args:
        grid: Grid of emulator IDs (rows x columns)
        rows: Number of rows in the grid
        cols: Number of columns in the grid
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
        for col in range(cols):
            snapshot = snapshots[col % len(snapshots)]
            for row in range(rows):
                emu_id = grid[row][col]
                if emu_id:
                    assignments[emu_id] = snapshot

    elif pattern == Pattern.ROWS:
        # Each row gets the same snapshot
        for row in range(rows):
            snapshot = snapshots[row % len(snapshots)]
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    assignments[emu_id] = snapshot

    elif pattern == Pattern.DIAGONAL_DOWN:
        # Diagonal lines: top-right to bottom-left
        for row in range(rows):
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    diagonal_idx = (row - col) % len(snapshots)
                    assignments[emu_id] = snapshots[diagonal_idx]

    elif pattern == Pattern.DIAGONAL_UP:
        # Diagonal lines: top-left to bottom-right
        for row in range(rows):
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    diagonal_idx = (row + col) % len(snapshots)
                    assignments[emu_id] = snapshots[diagonal_idx]

    elif pattern == Pattern.CIRCLES:
        # Concentric circles from center
        center_row, center_col = (rows - 1) / 2.0, (cols - 1) / 2.0
        for row in range(rows):
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    distance = ((row - center_row) ** 2 + (col - center_col) ** 2) ** 0.5
                    circle_idx = int(distance) % len(snapshots)
                    assignments[emu_id] = snapshots[circle_idx]

    elif pattern == Pattern.SPIRAL:
        # Spiral pattern from center outward
        center_row, center_col = rows // 2, cols // 2
        total_tiles = sum(1 for r in grid for c in r if c)
        directions = [(0, 1), (1, 0), (0, -1), (-1, 0)]  # right, down, left, up
        row, col = center_row, center_col
        dir_idx = 0
        steps = 1
        snapshot_idx = 0

        # Fill center first
        if 0 <= row < rows and 0 <= col < cols and grid[row][col]:
            assignments[grid[row][col]] = snapshots[snapshot_idx % len(snapshots)]
            snapshot_idx += 1

        while len(assignments) < total_tiles:
            # Move in current direction for 'steps' steps
            for _ in range(steps):
                dr, dc = directions[dir_idx]
                row += dr
                col += dc

                if 0 <= row < rows and 0 <= col < cols:
                    emu_id = grid[row][col]
                    if emu_id and emu_id not in assignments:
                        assignments[emu_id] = snapshots[snapshot_idx % len(snapshots)]
                        snapshot_idx += 1

            # Change direction
            dir_idx = (dir_idx + 1) % 4

            # Increase steps every two direction changes
            if dir_idx % 2 == 0:
                steps += 1

    elif pattern == Pattern.CHECKERBOARD:
        # Alternating checkerboard pattern
        for row in range(rows):
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    pattern_idx = (row + col) % 2
                    assignments[emu_id] = snapshots[pattern_idx % len(snapshots)]

    elif pattern == Pattern.WAVES_HORIZONTAL:
        # Horizontal wave pattern
        for row in range(rows):
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    wave_idx = int(3 * (1 + (col / max(cols - 1, 1)) * 2) * (row / max(rows - 1, 1)))
                    assignments[emu_id] = snapshots[wave_idx % len(snapshots)]

    elif pattern == Pattern.WAVES_VERTICAL:
        # Vertical wave pattern
        for row in range(rows):
            for col in range(cols):
                emu_id = grid[row][col]
                if emu_id:
                    wave_idx = int(3 * (1 + (row / max(rows - 1, 1)) * 2) * (col / max(cols - 1, 1)))
                    assignments[emu_id] = snapshots[wave_idx % len(snapshots)]

    return assignments


def run_test(base_url: str, pattern: Pattern, whitelist_file: str, basepath: str = None) -> bool:
    """
    Run the pattern snapshot loading test.

    Args:
        base_url: WebAPI server URL
        pattern: Pattern to use for loading
        whitelist_file: Path to whitelist file containing allowed snapshots
        basepath: Optional remote project root path for cross-machine setups

    Returns:
        True if test passed, False otherwise
    """
    client = WebAPIClient(base_url)
    testdata = TestDataHelper(whitelist_file, basepath=basepath)

    log(f"Connecting to WebAPI at {base_url}...")

    # Check server health
    if not client.check_health():
        log("ERROR: WebAPI server is not reachable")
        return False

    log("WebAPI server is healthy")

    # Step 1: Get emulator instances
    log("Step 1: Getting emulator instances...")
    emulators = client.list_emulators()

    count = len(emulators)
    if count == 0:
        log("ERROR: No emulator instances found")
        log("Please ensure the videowall is running in fullscreen mode.")
        return False

    log(f"Found {count} emulator instances")

    # Step 2: Arrange instances into grid
    log("Step 2: Arranging instances into grid...")
    try:
        grid, rows, cols = arrange_instances_grid(emulators)
        log(f"✓ Instances arranged into {cols}x{rows} grid ({count} tiles)")
        log(f"")
        content = f"  DETECTED MATRIX: {cols} cols × {rows} rows = {count} tiles  "
        border = "═" * len(content)
        log(f"  ╔{border}╗")
        log(f"  ║{content}║")
        log(f"  ╚{border}╝")
        log(f"")
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
    if snapshots:
        log(f"  (First path: {snapshots[0]})")

    # Step 4: Generate pattern assignments
    log(f"Step 4: Generating {pattern.value} pattern assignments...")
    assignments = generate_pattern_assignments(grid, rows, cols, pattern, snapshots)
    log("✓ Pattern assignments generated")

    # Note: Sound is now disabled at emulator creation in VideoWallWindow::addEmulatorTile()
    # No need to disable sound via WebAPI anymore

    # Step 5: Pause all instances first (for synchronized loading)
    log("Step 5: Pausing all instances for synchronized loading...")
    pause_success = 0
    for emu_id in assignments.keys():
        if client.pause_emulator(emu_id):
            pause_success += 1
    log(f"Paused {pause_success}/{count} instances")

    # Step 6: Load snapshots according to pattern (while paused)
    log(f"Step 6: Loading snapshots using {pattern.value} pattern...")
    load_success = 0

    for emu_id, snapshot_path in assignments.items():
        snapshot_name = snapshot_path.split('/')[-1]
        log(f"  Loading '{snapshot_name}' to {emu_id[:8]}...")

        if client.load_snapshot(emu_id, snapshot_path):
            log("  ✓ Loaded successfully")
            load_success += 1
        else:
            log("  ✗ Failed to load snapshot")
    log(f"Snapshots loaded on {load_success}/{count} instances")

    # Step 7: Resume all instances (synchronized start)
    log("Step 7: Resuming all instances for synchronized playback...")
    start_success = 0

    for emu_id in assignments.keys():
        if client.resume_emulator(emu_id):
            start_success += 1
        else:
            log(f"  ✗ {emu_id[:8]}... failed to resume")

    log(f"Resumed {start_success}/{count} instances")

    # Summary
    log("\n" + "=" * 60)
    log("PATTERN TEST SUMMARY")
    log("=" * 60)
    log(f"  Pattern:           {pattern.value}")
    log(f"  Description:       {get_pattern_description(pattern)}")
    log(f"  Total instances:   {count}")
    log(f"  Grid size:         {cols}x{rows}")
    log(f"  Available snapshots: {len(snapshots)}")
    log(f"  Snapshots loaded:  {load_success}/{count}")
    log(f"  Running/resumed:   {start_success}/{count}")

    # Determine overall success
    all_passed = (load_success == count and start_success == count)

    if all_passed:
        log("\n✅ TEST PASSED: Pattern loaded successfully")
    else:
        log("\n⚠️  TEST PARTIAL: Some operations failed")

    return all_passed


def main():
    parser = argparse.ArgumentParser(
        description="Load snapshots in patterns to 15x11 videowall (165 instances)"
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
    parser.add_argument(
        "--basepath",
        default=None,
        help="Remote project root path for cross-machine setups. "
             "When specified, snapshot paths sent to the emulator will use this basepath. "
             "Example: O:/Projects/unreal-ng for Windows remote."
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
    if args.basepath:
        log(f"Remote basepath: {args.basepath}")
    log("=" * 60)

    try:
        success = run_test(args.url, pattern, args.whitelist, args.basepath)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        log("\nTest interrupted by user")
        sys.exit(130)
    except Exception as e:
        log(f"\nERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()