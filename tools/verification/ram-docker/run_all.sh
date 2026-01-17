#!/bin/bash

# ==============================================================================
# Script: run_all.sh
# Description: Orchestrator for complete Docker build verification cycle.
#
# Workflow:
#   1. Ensure Docker image exists (build if needed)
#   2. Create/mount RAM disk
#   3. Sync project to RAM disk
#   4. Build all components inside Docker
#   5. Cleanup (unless --keep specified)
#
# Arguments:
#   $1 (Optional): RAM disk size in GiB (Default: 6)
#   --keep: Keep RAM disk mounted after completion
#   --rebuild-image: Force rebuild Docker image
#
# Usage:
#   ./run_all.sh                    # Full cycle
#   ./run_all.sh 8                  # 8GB RAM disk
#   ./run_all.sh --keep             # Keep RAM disk mounted
#   ./run_all.sh --rebuild-image    # Force rebuild Docker image
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Parse arguments
RAM_SIZE_GIB=$DEFAULT_RAM_SIZE_GIB
KEEP_RAM=false
REBUILD_IMAGE=false

for arg in "$@"; do
    case $arg in
        --keep) KEEP_RAM=true ;;
        --rebuild-image) REBUILD_IMAGE=true ;;
        [0-9]*) RAM_SIZE_GIB=$arg ;;
    esac
done

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║         Unreal-NG Docker Build Verification                 ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║  Platform:    $(printf '%-45s' "$(get_platform_arch)")║"
echo "║  RAM Size:    $(printf '%-45s' "${RAM_SIZE_GIB}GiB")║"
echo "║  Docker:      $(printf '%-45s' "$DOCKER_FULL_IMAGE")║"
echo "║  Project:     $(printf '%-45s' "$(basename $PROJECT_ROOT)")║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

TOTAL_START=$(date +%s)

# 1) Ensure Docker image exists
echo "Step 1/5: Ensuring Docker image..."
if [[ "$REBUILD_IMAGE" == "true" ]]; then
    "${SCRIPT_DIR}/ensure_image.sh" --rebuild
else
    "${SCRIPT_DIR}/ensure_image.sh"
fi
echo ""

# 2) Create RAM disk
echo "Step 2/5: Creating RAM disk..."
"${SCRIPT_DIR}/create_ram_disk.sh" "$RAM_SIZE_GIB"
echo ""

# 3) Wipe any stale content
echo "Step 3/5: Preparing RAM disk..."
"${SCRIPT_DIR}/cleanup_ram_disk.sh" --wipe
echo ""

# 4) Copy project to RAM
echo "Step 4/5: Syncing project..."
"${SCRIPT_DIR}/copy_to_ram.sh"
echo ""

# 5) Build in Docker
echo "Step 5/5: Building in Docker..."
if ! "${SCRIPT_DIR}/build_in_docker.sh"; then
    echo ""
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║  ❌ BUILD FAILED                                             ║"
    echo "║  RAM disk kept mounted for inspection                        ║"
    echo "║  Logs: $(printf '%-50s' "$RAM_LOG_DIR")║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    exit 1
fi
echo ""

TOTAL_END=$(date +%s)
TOTAL_DURATION=$((TOTAL_END - TOTAL_START))

# Cleanup
if [[ "$KEEP_RAM" == "true" ]]; then
    echo "Keeping RAM disk mounted at $RAM_DISK_MOUNT_POINT"
else
    "${SCRIPT_DIR}/cleanup_ram_disk.sh"
fi

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  ✅ BUILD VERIFICATION COMPLETE                              ║"
echo "║  Total Time: $(printf '%-47s' "$(format_duration $TOTAL_DURATION)")║"
echo "╚══════════════════════════════════════════════════════════════╝"
