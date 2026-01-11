#!/bin/bash

# ==============================================================================
# Script: copy_to_ram.sh
# Description: Synchronizes the project source code to the mounted RAM disk.
#              Uses rsync for efficient copying, excluding unnecessary files.
#
# Exclusions:
#   - Build directories (build*, cmake-build-*)
#   - .git history
#
# Checks Performed:
#   - Verifies that the RAM disk is actually mounted.
#
# Usage:
#   ./copy_to_ram.sh
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if ! is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "ERROR: RAM disk not mounted at $RAM_DISK_MOUNT_POINT. Please run create_ram_disk.sh first."
    exit 1
fi

mkdir -p "$RAM_PROJECT_ROOT"

echo "Copying project from $PROJECT_ROOT to $RAM_PROJECT_ROOT..."
# Using rsync to copy files, excluding build directories
rsync -a \
    --exclude='build*/' \
    --exclude='cmake-build-*/' \
    --exclude='.git/' \
    "$PROJECT_ROOT/" "$RAM_PROJECT_ROOT/"

echo "Project copied to RAM disk."
