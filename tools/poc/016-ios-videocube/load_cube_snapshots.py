#!/usr/bin/env python3
"""
3D Video Cube Non-Interactive Automated Snapshot Loader
=========================================================
Automated script to populate the 6 faces of the 3D Video Cube iOS app via WebAPI.

Usage:
    python3 load_cube_snapshots.py <IP_ADDRESS> [SNAPSHOTS_DIR] [--port PORT] [--whitelist WHITELIST_FILE]

Example:
    python3 load_cube_snapshots.py 127.0.0.1
    python3 load_cube_snapshots.py 192.168.1.50 /path/to/sna/folder
"""

import argparse
import sys
import os
import time
import json
import urllib.request
import urllib.error


def log(msg: str, status: str = "INFO"):
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] [{status}] {msg}")


def find_repo_root() -> str:
    """Dynamically locate repository root directory."""
    current = os.path.dirname(os.path.abspath(__file__))
    while current and current != os.path.dirname(current):
        if os.path.exists(os.path.join(current, "CMakeLists.txt")) and os.path.exists(os.path.join(current, "testdata")):
            return current
        current = os.path.dirname(current)
    return os.path.dirname(os.path.abspath(__file__))


def load_whitelist(whitelist_path: str) -> set:
    """Load allowed snapshot filenames from a whitelist file."""
    if not os.path.isfile(whitelist_path):
        log(f"Whitelist file not found at '{whitelist_path}', defaulting to all files.", "WARN")
        return set()

    allowed = set()
    with open(whitelist_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                allowed.add(line)
    log(f"Loaded {len(allowed)} snapshot whitelist rule(s) from '{whitelist_path}'.", "INFO")
    return allowed


def get_emulators(base_url: str) -> list:
    """Query WebAPI for list of running emulator instances."""
    url = f"{base_url}/api/v1/emulator"
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            if resp.status == 200:
                data = json.loads(resp.read().decode("utf-8"))
                return data.get("emulators", [])
    except Exception as e:
        log(f"Failed to query emulators at {url}: {e}", "ERROR")
    return []


def push_snapshot(base_url: str, emu_id: str, filepath: str) -> bool:
    """Upload a raw binary snapshot file to a specific emulator instance via WebAPI."""
    filename = os.path.basename(filepath)
    url = f"{base_url}/api/v1/emulator/{emu_id}/snapshot/load"

    try:
        with open(filepath, "rb") as f:
            snapshot_bytes = f.read()

        req = urllib.request.Request(
            url,
            data=snapshot_bytes,
            headers={
                "Content-Type": "application/octet-stream",
                "X-Filename": filename,
                "Content-Length": str(len(snapshot_bytes))
            },
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            return 200 <= resp.status < 300
    except Exception as e:
        log(f"Error uploading '{filename}' to instance {emu_id[:8]}: {e}", "ERROR")
        return False


def main():
    repo_root = find_repo_root()
    default_sna_dir = os.path.join(repo_root, "testdata", "loaders", "sna")
    default_whitelist = os.path.join(repo_root, "tools", "verification", "videowall", "whitelist.txt")

    parser = argparse.ArgumentParser(description="Non-interactive snapshot loader for 3D Video Cube iOS host.")
    parser.add_argument("ip", nargs="?", default="127.0.0.1", help="Target IP address of the 3D Video Cube host (default: 127.0.0.1)")
    parser.add_argument("snapshots_dir", nargs="?", default=default_sna_dir, help=f"Path to directory containing .sna/.z80 snapshots (default: {default_sna_dir})")
    parser.add_argument("--port", type=int, default=8090, help="WebAPI port (default: 8090)")
    parser.add_argument("--whitelist", default=default_whitelist, help=f"Path to whitelist.txt (default: {default_whitelist})")

    args = parser.parse_args()

    base_url = f"http://{args.ip}:{args.port}"
    log(f"Connecting to 3D Video Cube WebAPI at {base_url}...", "INFO")

    emulators = get_emulators(base_url)
    if not emulators:
        log("No emulator instances returned from 3D Video Cube host. Exiting.", "ERROR")
        sys.exit(1)

    log(f"Detected {len(emulators)} running emulator instance(s).", "SUCCESS")

    if not os.path.isdir(args.snapshots_dir):
        log(f"Snapshots directory '{args.snapshots_dir}' does not exist.", "ERROR")
        sys.exit(1)

    whitelist = load_whitelist(args.whitelist)

    # Discover and filter candidate snapshot files
    all_files = sorted(os.listdir(args.snapshots_dir))
    candidate_files = []
    for fname in all_files:
        if fname.lower().endswith((".sna", ".z80")):
            if not whitelist or fname in whitelist:
                candidate_files.append(os.path.join(args.snapshots_dir, fname))

    if not candidate_files:
        log(f"No whitelisted snapshot files found in '{args.snapshots_dir}'.", "ERROR")
        sys.exit(1)

    log(f"Found {len(candidate_files)} whitelisted snapshot(s) to load.", "INFO")

    face_names = ["Face 0 (Front)", "Face 1 (Back)", "Face 2 (Left)", "Face 3 (Right)", "Face 4 (Top)", "Face 5 (Bottom)"]

    success_count = 0
    for idx, emu in enumerate(emulators[:6]):
        emu_id = emu.get("id", "")
        face_label = face_names[idx] if idx < len(face_names) else f"Face {idx}"

        # Assign snapshot (cycling through available candidate files if fewer than 6)
        snap_file = candidate_files[idx % len(candidate_files)]
        snap_name = os.path.basename(snap_file)

        log(f"Loading '{snap_name}' onto {face_label} (Instance #{emu_id[:8]})...", "INFO")
        if push_snapshot(base_url, emu_id, snap_file):
            log(f"-> {face_label}: SUCCESS ({snap_name})", "SUCCESS")
            success_count += 1
        else:
            log(f"-> {face_label}: FAILED", "ERROR")

    log(f"Snapshot loading complete: {success_count}/{min(len(emulators), 6)} faces loaded.", "SUCCESS" if success_count > 0 else "ERROR")
    sys.exit(0 if success_count > 0 else 1)


if __name__ == "__main__":
    main()
