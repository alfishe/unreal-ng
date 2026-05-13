#!/bin/bash

# ==============================================================================
# Script: cleanup_ram_disk.sh
# Description: Manages the lifecycle and cleanup of the RAM disk.
#
# Modes:
#   1. Default: Unmounts the RAM disk and frees the system memory.
#   2. Wipe (--wipe): Deletes all files on the RAM disk but keeps it mounted.
#
# Functionality:
#   - Cross-platform unmounting (diskutil on macOS, umount on Linux).
#   - Cleanup of temporary mount points on Linux.
#
# Usage:
#   ./cleanup_ram_disk.sh         # Unmount and free memory
#   ./cleanup_ram_disk.sh --wipe  # Clean files, keep disk mounted
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

wipe_only=false
if [[ "$1" == "--wipe" ]]; then
    wipe_only=true
fi

if ! is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "RAM disk not mounted at $RAM_DISK_MOUNT_POINT. Nothing to do."
    exit 0
fi

if [ "$wipe_only" = true ]; then
    echo "Wiping RAM disk content at $RAM_DISK_MOUNT_POINT..."
    # Use sudo if we are on Linux to ensure we can clean up everything
    if is_macos; then
        rm -rf "${RAM_DISK_MOUNT_POINT:?}"/*
    else
        sudo rm -rf "${RAM_DISK_MOUNT_POINT:?}"/*
    fi
    echo "RAM disk wiped."
else
    echo "Unmounting RAM disk $RAM_DISK_NAME..."
    if is_macos; then
        diskutil eject "$RAM_DISK_MOUNT_POINT"
    else
        sudo umount "$RAM_DISK_MOUNT_POINT"
        # Optional: remove the mount point directory if it's empty
        if [[ -d "$RAM_DISK_MOUNT_POINT" ]]; then
            sudo rmdir "$RAM_DISK_MOUNT_POINT" || echo "Warning: Could not remove $RAM_DISK_MOUNT_POINT"
        fi
    fi
    echo "RAM disk unmounted and memory freed."
fi
