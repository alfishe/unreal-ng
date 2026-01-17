#!/bin/bash

# ==============================================================================
# Script: create_ram_disk.sh
# Description: Creates and mounts a RAM disk for high-speed Docker builds.
#
# Why RAM Disk:
#   - Docker volume mounts on macOS have significant I/O overhead
#   - RAM disk provides native-speed I/O for build operations
#   - Significantly faster than bind-mounting from external drives
#
# Arguments:
#   $1 (Optional): RAM disk size in GiB (Default: 6 for Docker builds)
#
# Usage:
#   ./create_ram_disk.sh       # 6GB default
#   ./create_ram_disk.sh 8     # 8GB
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

RAM_SIZE_GIB=${1:-$DEFAULT_RAM_SIZE_GIB}

echo "=== Creating RAM Disk for Docker Build ==="
echo "Size: ${RAM_SIZE_GIB}GiB"
echo "Mount: $RAM_DISK_MOUNT_POINT"
echo "==========================================="

if is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "RAM disk already mounted at $RAM_DISK_MOUNT_POINT"
    echo "Use cleanup_ram_disk.sh to unmount first if you need to resize."
    exit 0
fi

if is_macos; then
    SECTORS=$(gib_to_sectors $RAM_SIZE_GIB)
    
    # Create RAM disk device
    DEVICE=$(hdiutil attach -nomount ram://$SECTORS)
    DEVICE=$(echo "$DEVICE" | tr -d '[:space:]')
    
    echo "Created device: $DEVICE"
    
    # Format as APFS
    diskutil erasevolume APFS "$RAM_DISK_NAME" "$DEVICE" > /dev/null
    
    echo "✓ RAM disk created and mounted at $RAM_DISK_MOUNT_POINT"
    
elif is_linux; then
    # Create mount point
    sudo mkdir -p "$RAM_DISK_MOUNT_POINT"
    
    # Mount tmpfs
    RAM_SIZE_MB=$((RAM_SIZE_GIB * 1024))
    sudo mount -t tmpfs -o size=${RAM_SIZE_MB}M tmpfs "$RAM_DISK_MOUNT_POINT"
    
    # Make writable by current user
    sudo chown "$(whoami):$(id -gn)" "$RAM_DISK_MOUNT_POINT"
    
    echo "✓ RAM disk (tmpfs) mounted at $RAM_DISK_MOUNT_POINT"
else
    echo "ERROR: Unsupported platform"
    exit 1
fi

# Create project directory structure
mkdir -p "$RAM_PROJECT_ROOT"
mkdir -p "$RAM_LOG_DIR"
mkdir -p "$RAM_BUILD_DIR"

echo "✓ Directory structure created"
