#!/bin/bash

# ==============================================================================
# Script: copy_to_ram.sh
# Description: Syncs project source to RAM disk, excluding build artifacts and
#              large generated files.
#
# Exclusions:
#   - Build directories (cmake-build-*, build-*)
#   - IDE files (.idea, .vscode)
#   - Git directory (.git)
#   - tools/ directory (not needed for build, contains 1.9TB of test data)
#   - Large binary test assets
#   - Documentation artifacts
#
# Usage:
#   ./copy_to_ram.sh
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if ! is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "ERROR: RAM disk not mounted at $RAM_DISK_MOUNT_POINT"
    echo "Run create_ram_disk.sh first."
    exit 1
fi

echo "=== Syncing Project to RAM Disk ==="
echo "Source: $PROJECT_ROOT"
echo "Target: $RAM_PROJECT_ROOT"

# Check available space
if is_macos; then
    AVAIL_MB=$(df -m "$RAM_DISK_MOUNT_POINT" | tail -1 | awk '{print $4}')
else
    AVAIL_MB=$(df -BM "$RAM_DISK_MOUNT_POINT" | tail -1 | awk '{print $4}' | sed 's/M//')
fi
echo "Available space: ${AVAIL_MB}MB"
echo "===================================="

SYNC_START=$(date +%s)

rsync -a --delete \
    --exclude='.git' \
    --exclude='.idea' \
    --exclude='.vscode' \
    --exclude='.specstory' \
    --exclude='cmake-build-*' \
    --exclude='build-*' \
    --exclude='build' \
    --exclude='/tools' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.so' \
    --exclude='*.dylib' \
    --exclude='node_modules' \
    --exclude='__pycache__' \
    --exclude='*.pyc' \
    --exclude='docs/development-saga' \
    --exclude='docs/reviews' \
    --exclude='history' \
    --exclude='other' \
    --exclude='*.patch' \
    --exclude='*.sna' \
    --exclude='*.z80' \
    --exclude='*.tap' \
    --exclude='*.trd' \
    --exclude='*.scl' \
    "$PROJECT_ROOT/" "$RAM_PROJECT_ROOT/"

SYNC_END=$(date +%s)
SYNC_DURATION=$((SYNC_END - SYNC_START))

# Get size
if is_macos; then
    SIZE=$(du -sh "$RAM_PROJECT_ROOT" | cut -f1)
else
    SIZE=$(du -sh "$RAM_PROJECT_ROOT" | cut -f1)
fi

echo "✓ Sync complete in $(format_duration $SYNC_DURATION)"
echo "  Project size on RAM: $SIZE"
