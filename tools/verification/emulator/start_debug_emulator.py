#!/usr/bin/env python3
"""
Start unreal-qt in debug mode with WebAPI and CLI enabled.
Ensures fresh build and verifies service availability.
"""

import os
import sys
import subprocess
import time
import socket
from pathlib import Path
from typing import Optional
import requests


def find_project_root() -> Path:
    """Find project root by searching upward for CMakeLists.txt with project() command."""
    current = Path(__file__).resolve().parent
    max_depth = 10
    
    for _ in range(max_depth):
        cmake_file = current / "CMakeLists.txt"
        if cmake_file.exists():
            try:
                content = cmake_file.read_text()
                if "project(" in content or "PROJECT(" in content:
                    return current
            except:
                pass
        
        parent = current.parent
        if parent == current:  # Reached filesystem root
            break
        current = parent
    
    raise RuntimeError("Could not find project root (no CMakeLists.txt with project())")


def check_port(port: int, timeout: float = 1.0) -> bool:
    """Check if a TCP port is accepting connections."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        result = sock.connect_ex(('localhost', port))
        return result == 0
    finally:
        sock.close()


def kill_existing_instances():
    """Kill any existing unreal-qt processes."""
    try:
        subprocess.run(['pkill', '-9', 'unreal-qt'], 
                      stdout=subprocess.DEVNULL, 
                      stderr=subprocess.DEVNULL)
        time.sleep(2)
    except:
        pass


def main():
    print("=" * 60)
    print("unreal-qt Debug Emulator Startup")
    print("=" * 60)
    print()
    
    # Find project root
    try:
        project_root = find_project_root()
        print(f"✓ Project root: {project_root}")
    except RuntimeError as e:
        print(f"✗ ERROR: {e}")
        return 1
    
    # Setup paths
    build_dir = project_root / "cmake-build-debug"
    bin_dir = build_dir / "bin"
    unreal_qt = bin_dir / "unreal-qt"
    log_file = Path("/tmp/unreal-qt-debug.log")
    
    # Create build directory
    if not build_dir.exists():
        print(f"Creating build directory: {build_dir}")
        build_dir.mkdir(parents=True)
    
    # Configure CMake if needed
    cmake_cache = build_dir / "CMakeCache.txt"
    if not cmake_cache.exists():
        print("Configuring CMake for Debug build...")
        result = subprocess.run(
            ['cmake', '-DCMAKE_BUILD_TYPE=Debug', str(project_root)],
            cwd=build_dir,
            capture_output=True,
            text=True
        )
        if result.returncode != 0:
            print(f"✗ CMake configuration failed:")
            print(result.stderr)
            return 1
        print("✓ CMake configured")
    
    # Build unreal-qt
    print("Building unreal-qt (Debug)...")
    result = subprocess.run(
        ['cmake', '--build', '.', '--target', 'unreal-qt', '-j8'],
        cwd=build_dir,
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print(f"✗ Build failed:")
        print(result.stderr)
        return 1
    
    print("✓ Build completed")
    
    # Verify binary exists
    if not unreal_qt.exists():
        print(f"✗ ERROR: Binary not found at {unreal_qt}")
        return 1
    
    stat = unreal_qt.stat()
    print(f"✓ Binary: {unreal_qt}")
    print(f"  Size: {stat.st_size:,} bytes")
    print(f"  Modified: {time.ctime(stat.st_mtime)}")
    print()
    
    # Kill existing instances
    print("Stopping any existing unreal-qt instances...")
    kill_existing_instances()
    print("✓ Cleanup complete")
    print()
    
    # Start unreal-qt
    print(f"Starting unreal-qt with WebAPI...")
    print(f"Log file: {log_file}")
    
    with open(log_file, 'w') as log:
        process = subprocess.Popen(
            [str(unreal_qt), '--webapi', '--webapi-port', '8090'],
            stdout=log,
            stderr=subprocess.STDOUT,
            cwd=project_root
        )
    
    pid = process.pid
    print(f"✓ Started with PID: {pid}")
    print()
    
    # Wait for services
    print("Waiting for services to start...")
    time.sleep(5)
    
    # Check CLI port
    print("Checking CLI port 8765... ", end='', flush=True)
    if check_port(8765):
        print("✓ Ready")
    else:
        print("✗ Not available (WARNING)")
    
    # Check WebAPI port
    print("Checking WebAPI port 8090... ", end='', flush=True)
    try:
        response = requests.get('http://localhost:8090/api/v1/emulator', timeout=2)
        if response.status_code == 200:
            print("✓ Ready")
        else:
            print(f"✗ HTTP {response.status_code}")
            print()
            print("ERROR: WebAPI not responding correctly")
            print(f"Check log: tail -f {log_file}")
            return 1
    except Exception as e:
        print(f"✗ Failed: {e}")
        print()
        print("ERROR: WebAPI not accessible")
        print(f"Check log: tail -f {log_file}")
        return 1
    
    print()
    print("=" * 60)
    print("✓ unreal-qt is running and ready")
    print("=" * 60)
    print(f"  PID: {pid}")
    print(f"  CLI: telnet localhost 8765")
    print(f"  WebAPI: http://localhost:8090")
    print(f"  Log: {log_file}")
    print()
    print(f"To stop: kill {pid}")
    print(f"To view logs: tail -f {log_file}")
    print("=" * 60)
    
    # Write PID to file for easy access
    pid_file = Path("/tmp/unreal-qt.pid")
    pid_file.write_text(str(pid))
    print(f"PID file: {pid_file}")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
