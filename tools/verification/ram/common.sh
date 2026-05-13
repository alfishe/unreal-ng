#!/bin/bash

# ==============================================================================
# Script: common.sh
# Description: Shared utility functions and configuration for RAM disk scripts.
#              This script is intended to be sourced by other scripts in the 
#              verification/ram directory.
#
# Functionality:
#   - Automatic project root discovery (upwards search).
#   - OS detection (macOS, Linux, WSL).
#   - Cross-platform RAM disk path and mount point configuration.
#   - Project and log directory paths on the RAM disk.
#   - Resource availability checks (CMake, rsync).
#   - Parallel build job calculation.
#
# Environment:
#   - Supported: macOS, Ubuntu/Debian (Linux), WSL (Windows).
# ==============================================================================

# Function to find project root by searching upwards
find_project_root() {
    local current_dir="$1"
    for ((i=0; i<10; i++)); do
        # We look for the root CMakeLists.txt that defines 'project (unreal)'
        if [[ -f "$current_dir/CMakeLists.txt" ]] && grep -q "project (unreal)" "$current_dir/CMakeLists.txt"; then
            echo "$current_dir"
            return 0
        fi
        local parent_dir="$(cd "$current_dir/.." && pwd)"
        if [[ "$parent_dir" == "$current_dir" ]]; then break; fi
        current_dir="$parent_dir"
    done
    return 1
}

# Try to find root starting from current working directory
PROJECT_ROOT=$(find_project_root "$(pwd)")
# If not found, try from the script's directory
if [[ -z "$PROJECT_ROOT" ]]; then
    _COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT=$(find_project_root "$_COMMON_DIR")
fi

if [[ -z "$PROJECT_ROOT" ]]; then
    echo "ERROR: Could not find project root (containing CMakeLists.txt with 'project (unreal)')"
    exit 1
fi

export PROJECT_ROOT

# OS Detection
is_macos() {
    [[ "$(uname)" == "Darwin" ]]
}

is_linux() {
    [[ "$(uname)" == "Linux" ]]
}

is_wsl() {
    if is_linux; then
        grep -qi microsoft /proc/version 2>/dev/null
    else
        false
    fi
}

# Configuration
RAM_DISK_NAME="UnrealRAM"
if is_macos; then
    RAM_DISK_MOUNT_POINT="/Volumes/${RAM_DISK_NAME}"
else
    # For Linux and WSL, we use a mount point in /mnt
    RAM_DISK_MOUNT_POINT="/mnt/${RAM_DISK_NAME}"
fi
DEFAULT_RAM_SIZE_GIB=4

RAM_PROJECT_ROOT="${RAM_DISK_MOUNT_POINT}/unreal-ng"
RAM_LOG_DIR="${RAM_PROJECT_ROOT}/build-logs"

# Check for required tools
for tool in cmake rsync; do
    if ! command -v "$tool" &> /dev/null; then
        echo "ERROR: $tool is not installed or not in PATH."
        exit 1
    fi
done

# Helper to get sectors from GiB (macOS only)
gib_to_sectors() {
    local gib=$1
    echo $(( gib * 1024 * 1024 * 1024 / 512 ))
}

# Check if a path is a mount point
is_mounted() {
    local path="${1%/}"
    if is_macos; then
        mount | grep -q "on $path " 2>/dev/null
    else
        if command -v mountpoint &> /dev/null; then
            mountpoint -q "$path" 2>/dev/null
        else
            mount | grep -q " $path " 2>/dev/null
        fi
    fi
}

# Get number of parallel jobs
get_parallel_jobs() {
    if is_macos; then
        sysctl -n hw.ncpu
    else
        nproc
    fi
}
