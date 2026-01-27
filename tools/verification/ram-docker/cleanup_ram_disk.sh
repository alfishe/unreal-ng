#!/bin/bash

# ==============================================================================
# Script: cleanup_ram_disk.sh
# Description: Cleans up build artifacts or completely unmounts the RAM disk.
#
# Modes:
#   (no args)  - Unmount RAM disk and free memory
#   --wipe     - Only wipe contents, keep RAM disk mounted
#
# Usage:
#   ./cleanup_ram_disk.sh          # Unmount
#   ./cleanup_ram_disk.sh --wipe   # Just wipe, keep mounted
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

MODE="${1:-unmount}"

if ! is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "RAM disk not mounted at $RAM_DISK_MOUNT_POINT. Nothing to do."
    exit 0
fi

if [[ "$MODE" == "--wipe" ]]; then
    echo "=== Wiping RAM Disk Contents ==="
    rm -rf "${RAM_DISK_MOUNT_POINT:?}"/*
    mkdir -p "$RAM_PROJECT_ROOT"
    mkdir -p "$RAM_LOG_DIR"
    mkdir -p "$RAM_BUILD_DIR"
    echo "✓ RAM disk wiped but still mounted"
else
    echo "=== Unmounting RAM Disk ==="
    
    if is_macos; then
        # Get device before unmounting
        DEVICE=$(mount | grep " $RAM_DISK_MOUNT_POINT " | awk '{print $1}')
        
        # Try unmount with retries (Docker may still be releasing the volume)
        UNMOUNT_SUCCESS=false
        for attempt in {1..3}; do
            if umount "$RAM_DISK_MOUNT_POINT" 2>/dev/null || diskutil unmount "$RAM_DISK_MOUNT_POINT" 2>/dev/null; then
                UNMOUNT_SUCCESS=true
                break
            fi
            
            if [[ $attempt -lt 3 ]]; then
                echo "⚠️  Unmount failed (attempt $attempt/3). Waiting for Docker to release volume..."
                sleep 2
            fi
        done
        
        if [[ "$UNMOUNT_SUCCESS" == "true" ]]; then
            # Eject device to free RAM
            if [[ -n "$DEVICE" ]]; then
                hdiutil detach "$DEVICE" 2>/dev/null || true
            fi
            echo "✓ RAM disk unmounted and memory freed"
        else
            echo ""
            echo "⚠️  RAM disk still in use by Docker (PID holding reference)"
            echo "   This is normal on macOS - Docker's VM takes time to release volumes"
            echo "   RAM disk will be released when Docker restarts or after idle time"
            echo ""
            echo "   To force unmount:"
            echo "     1. Restart Docker Desktop"
            echo "     2. Then run: ./cleanup_ram_disk.sh"
            echo ""
            echo "   RAM disk: $RAM_DISK_MOUNT_POINT"
            exit 0  # Don't fail - build was successful
        fi
        
    elif is_linux; then
        sudo umount "$RAM_DISK_MOUNT_POINT"
        sudo rm -rf "$RAM_DISK_MOUNT_POINT"
        echo "✓ RAM disk (tmpfs) unmounted"
    fi
fi
