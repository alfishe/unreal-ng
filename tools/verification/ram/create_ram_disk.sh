#!/bin/bash

# ==============================================================================
# Script: create_ram_disk.sh
# Description: Creates and formats a RAM disk for high-performance builds.
#              Uses hdiutil on macOS and tmpfs on Linux/WSL.
#
# Arguments:
#   $1 (Optional): RAM disk size in GiB (Default: 4 GiB).
#
# Checks Performed:
#   - Skips creation if a RAM disk is already mounted at the target path.
#   - Verifies OS compatibility.
#   - On Linux/WSL, ensures the mount point exists and the user has ownership.
#
# Usage:
#   ./create_ram_disk.sh [size_in_gib]
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

RAM_SIZE_GIB=${1:-$DEFAULT_RAM_SIZE_GIB}

if is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "RAM disk already exists and is mounted at $RAM_DISK_MOUNT_POINT"
    exit 0
fi

if is_macos; then
    SECTORS=$(gib_to_sectors "$RAM_SIZE_GIB")
    echo "Creating RAM disk of ${RAM_SIZE_GIB}GiB (${SECTORS} sectors) on macOS..."
    
    # hdiutil attach returns the device path (e.g., /dev/disk4)
    DEVICE=$(hdiutil attach -nomount "ram://${SECTORS}" | xargs echo)
    
    if [[ -z "$DEVICE" ]]; then
        echo "ERROR: Failed to create RAM disk device."
        exit 1
    fi
    
    echo "Formatting $DEVICE as HFS+ with name $RAM_DISK_NAME..."
    diskutil erasevolume HFS+ "$RAM_DISK_NAME" "$DEVICE"
elif is_linux; then
    echo "Creating RAM disk of ${RAM_SIZE_GIB}GiB on Linux/WSL..."
    
    if [[ ! -d "$RAM_DISK_MOUNT_POINT" ]]; then
        echo "Creating mount point $RAM_DISK_MOUNT_POINT (may require sudo)..."
        sudo mkdir -p "$RAM_DISK_MOUNT_POINT"
    fi
    
    echo "Mounting tmpfs to $RAM_DISK_MOUNT_POINT (requires sudo)..."
    sudo mount -t tmpfs -o size=${RAM_SIZE_GIB}G tmpfs "$RAM_DISK_MOUNT_POINT"
    
    # Change ownership to current user so we can write to it without sudo later
    sudo chown "$USER" "$RAM_DISK_MOUNT_POINT"
else
    echo "ERROR: Unsupported OS: $(uname)"
    exit 1
fi

echo "RAM disk created and mounted at $RAM_DISK_MOUNT_POINT"
