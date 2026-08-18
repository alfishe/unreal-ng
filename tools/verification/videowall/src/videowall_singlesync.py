#!/usr/bin/env python3
import requests
import time
import argparse
import sys
import os
import json

def set_videowall_single_sync(base_url, enable, emulator_id=""):
    url = f"{base_url}/api/v1/videowall/singlesync"
    payload = {
        "enable": enable
    }
    if emulator_id:
        payload["emulator_id"] = emulator_id
        
    print(f"Sending request to {url}: {payload}")
    
    try:
        response = requests.post(url, json=payload, timeout=5)
        response.raise_for_status()
        result = response.json()
        print(f"Success: {result.get('success')}")
        return result.get('success', False)
    except requests.exceptions.RequestException as e:
        print(f"Error communicating with WebAPI: {e}")
        if hasattr(e, 'response') and e.response is not None:
            print(f"Server response: {e.response.text}")
        return False

def load_snapshot(base_url, emulator_id, snapshot_path):
    url = f"{base_url}/api/v1/emulator/{emulator_id}/snapshot/load"
    payload = {
        "path": os.path.abspath(snapshot_path)
    }
    
    print(f"Loading snapshot via {url}")
    
    try:
        response = requests.post(url, json=payload, timeout=5)
        response.raise_for_status()
        result = response.json()
        print(f"Snapshot loaded successfully")
        return True
    except requests.exceptions.RequestException as e:
        print(f"Error loading snapshot: {e}")
        if hasattr(e, 'response') and e.response is not None:
            print(f"Server response: {e.response.text}")
        return False

def get_emulator_id(base_url):
    url = f"{base_url}/api/v1/emulator"
    try:
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        emulators = response.json().get('emulators', [])
        if not emulators:
            print("No emulators found running.")
            return None
        # Return first emulator ID
        return emulators[0]['id']
    except requests.exceptions.RequestException as e:
        print(f"Error getting emulators: {e}")
        return None

def main():
    parser = argparse.ArgumentParser(description="Test Videowall Single Sync Mode")
    parser.add_argument("snapshot_pos", nargs="?", help="Path to snapshot to load on the target emulator (SNA/Z80)")
    parser.add_argument("--url", default="http://localhost:8090", help="Base URL of WebAPI (default: http://localhost:8090)")
    parser.add_argument("--on", action="store_true", help="Enable single sync mode")
    parser.add_argument("--off", action="store_true", help="Disable single sync mode")
    parser.add_argument("--id", help="Target emulator ID (optional)")
    parser.add_argument("--snapshot", help="Path to snapshot to load on the target emulator (SNA/Z80)")
    
    args = parser.parse_args()
    
    snapshot_path = args.snapshot or args.snapshot_pos
    
    # Default to enabling single sync if positional snapshot is provided and --off wasn't explicitly requested
    if snapshot_path and not args.off:
        enable = True
    elif args.off:
        enable = False
    elif args.on:
        enable = True
    else:
        print("Please specify --on, --off, or pass a snapshot file path.")
        parser.print_help()
        sys.exit(1)
    
    # If no ID specified but we're trying to enable, get the first running emulator
    target_id = args.id
    if enable and not target_id:
        target_id = get_emulator_id(args.url)
        if not target_id:
            print("Failed to auto-detect emulator ID.")
            sys.exit(1)
        print(f"Auto-detected emulator ID: {target_id}")

    # Step 1: Set Videowall Sync Mode
    success = set_videowall_single_sync(args.url, enable, target_id)
    if not success:
        sys.exit(1)
        
    # Step 2: Optionally load snapshot
    if enable and snapshot_path and target_id:
        # Give UI a moment to transition
        time.sleep(0.5)
        load_snapshot(args.url, target_id, snapshot_path)
        
if __name__ == "__main__":
    main()
