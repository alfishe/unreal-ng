#!/usr/bin/env python3
"""
Test: Load Random Snapshots to All Videowall Instances

This test scenario:
1. Gets list of all emulator instances (regardless of state)
2. Disables sound feature on every instance
3. Loads a random snapshot from testdata to each instance (filtered by whitelist)
4. Ensures all instances are started/unpaused

The test uses a whitelist to restrict which snapshots can be loaded. By default,
it uses ../whitelist.txt. Only snapshots with exact filename matches (including
extension) from the whitelist are allowed.

Usage:
    python test_load_random_snapshots.py [--url http://localhost:8090] [--whitelist FILE]

Options:
    --whitelist FILE    Path to whitelist file containing allowed snapshot filenames.
                       Each line must contain exact filename with extension (.sna or .z80).
                       Lines starting with # are comments. Defaults to ../whitelist.txt.
"""

import argparse
import sys
import time
from typing import List

from webapi_client import WebAPIClient, TestDataHelper


def log(message: str):
    """Print timestamped log message."""
    timestamp = time.strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}")


def run_test(base_url: str, whitelist_file: str) -> bool:
    """
    Run the random snapshot loading test.

    Args:
        base_url: WebAPI server URL
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
    
    # Step 1: Get list of emulator instances
    log("Step 1: Getting list of emulator instances...")
    emulators = client.list_emulators()
    
    if not emulators:
        log("WARNING: No emulator instances found. Test requires at least one instance.")
        log("Please start the videowall application first.")
        return False
    
    log(f"Found {len(emulators)} emulator instance(s):")
    for emu in emulators:
        # Compute real status from boolean fields
        is_running = emu.get('is_running', False)
        is_paused = emu.get('is_paused', False)
        
        if is_running and not is_paused:
            real_status = 'running'
        elif is_paused:
            real_status = 'paused'
        elif is_running and is_paused:
            real_status = 'paused (running)'
        else:
            real_status = 'stopped'
            
        log(f"  - {emu.get('id', 'unknown')} (status: {real_status})")
    
    emulator_ids = [emu['id'] for emu in emulators]
    
    # Step 2: Disable sound on all instances
    log("\nStep 2: Disabling sound on all instances...")
    sound_success = 0
    for emu_id in emulator_ids:
        if client.disable_sound(emu_id):
            log(f"  ✓ Disabled sound on {emu_id[:8]}...")
            sound_success += 1
        else:
            log(f"  ✗ Failed to disable sound on {emu_id[:8]}...")
    
    log(f"Sound disabled on {sound_success}/{len(emulator_ids)} instances")
    
    # Step 3: Load random snapshots to each instance
    log("\nStep 3: Loading random snapshots to each instance...")
    
    # Get random snapshots for each emulator
    snapshots = testdata.get_random_snapshots(len(emulator_ids))
    
    load_success = 0
    for emu_id, snapshot_path in zip(emulator_ids, snapshots):
        snapshot_name = snapshot_path.split('/')[-1]
        log(f"  Loading '{snapshot_name}' to {emu_id[:8]}...")
        
        if client.load_snapshot(emu_id, snapshot_path):
            log(f"  ✓ Loaded successfully")
            load_success += 1
        else:
            log(f"  ✗ Failed to load snapshot")
    
    log(f"Snapshots loaded on {load_success}/{len(emulator_ids)} instances")
    
    # Step 4: Ensure all instances are started/unpaused
    log("\nStep 4: Ensuring all instances are started/running...")
    
    start_success = 0
    for emu_id in emulator_ids:
        # Get current status
        emu_info = client.get_emulator(emu_id)
        if not emu_info:
            log(f"  ✗ {emu_id[:8]}... failed to get status")
            continue
            
        # Use boolean fields instead of state string comparison
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
            # Not running, try to start
            if client.start_emulator(emu_id):
                log(f"  ✓ {emu_id[:8]}... started")
                start_success += 1
            else:
                log(f"  ✗ {emu_id[:8]}... failed to start")
    
    log(f"Started/running: {start_success}/{len(emulator_ids)} instances")
    
    # Summary
    log("\n" + "=" * 50)
    log("TEST SUMMARY")
    log("=" * 50)
    log(f"  Total instances:    {len(emulator_ids)}")
    log(f"  Sound disabled:     {sound_success}/{len(emulator_ids)}")
    log(f"  Snapshots loaded:   {load_success}/{len(emulator_ids)}")
    log(f"  Running/resumed:    {start_success}/{len(emulator_ids)}")
    
    # Determine overall success
    all_passed = (sound_success == len(emulator_ids) and 
                  load_success == len(emulator_ids) and 
                  start_success == len(emulator_ids))
    
    if all_passed:
        log("\n✅ TEST PASSED: All operations successful")
    else:
        log("\n⚠️  TEST PARTIAL: Some operations failed")
    
    return all_passed


def main():
    parser = argparse.ArgumentParser(
        description="Load random snapshots to all videowall emulator instances"
    )
    parser.add_argument(
        "--url",
        default="http://localhost:8090",
        help="WebAPI base URL (default: http://localhost:8090)"
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

    log("=" * 50)
    log("VIDEOWALL TEST: Load Random Snapshots")
    log(f"Using whitelist: {args.whitelist}")
    log("=" * 50)

    try:
        success = run_test(args.url, args.whitelist)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        log("\nTest interrupted by user")
        sys.exit(130)
    except Exception as e:
        log(f"\nERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
