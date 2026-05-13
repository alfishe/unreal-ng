#!/bin/bash

# ==============================================================================
# Script: run_all.sh
# Description: Orchestrator script that performs a complete high-speed build 
#              cycle using a RAM disk.
#
# Workflow:
#   1. Create/Mount RAM disk.
#   2. Wipe any existing stale content.
#   3. Sync project source to RAM disk.
#   4. Build all project components.
#   5. Unmount RAM disk (unless --keep is specified or build fails).
#
# Error Handling:
#   - The script attempts to build all components even if one or more fails.
#   - If any build target fails, the script exits with an error and DOES NOT 
#     unmount the RAM disk, allowing the user to inspect logs in:
#     /Volumes/UnrealRAM/unreal-ng/build-logs/ (macOS)
#     /mnt/UnrealRAM/unreal-ng/build-logs/ (Linux)
#
# Arguments:
#   $1 (Optional): RAM disk size in GiB (Default: 4).
#   $2 (Optional): --keep (Keeps the RAM disk mounted after completion).
#
# Usage:
#   ./run_all.sh              # Full cycle with cleanup
#   ./run_all.sh 8 --keep     # 8GB RAM disk, do not unmount at the end
# ==============================================================================

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure we have common.sh
if [[ ! -f "${SCRIPT_DIR}/common.sh" ]]; then
    echo "ERROR: common.sh not found in ${SCRIPT_DIR}"
    exit 1
fi

source "${SCRIPT_DIR}/common.sh"

RAM_SIZE_GIB=${1:-$DEFAULT_RAM_SIZE_GIB}

echo "=== Unreal RAM Build Orchestrator ==="
echo "Project Root: $PROJECT_ROOT"
echo "RAM Size:     ${RAM_SIZE_GIB}GiB"
echo "====================================="

# 1) Create RAM disk
"${SCRIPT_DIR}/create_ram_disk.sh" "$RAM_SIZE_GIB"

# 4) Wipe the disk before next run (if it was already there and we are reusing it)
# Since create_ram_disk.sh skips if it exists, we might want to wipe it.
# However, if it was JUST created, it's already empty.
# To be safe and follow requirement 4:
"${SCRIPT_DIR}/cleanup_ram_disk.sh" --wipe

# 2) Copy content of this project
"${SCRIPT_DIR}/copy_to_ram.sh"

# 3) Execute build on ram disk for each component individually
if ! "${SCRIPT_DIR}/build_on_ram.sh"; then
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "ERROR: Build failed! RAM disk kept mounted at $RAM_DISK_MOUNT_POINT"
    echo "Check logs in: $RAM_LOG_DIR"
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    exit 1
fi

# 5) Unmount ram disk and free the memory
if [[ "$2" == "--keep" ]]; then
    echo "Keeping RAM disk mounted at $RAM_DISK_MOUNT_POINT"
else
    "${SCRIPT_DIR}/cleanup_ram_disk.sh"
fi

echo "=== RAM Build Process Completed Successfully ==="
