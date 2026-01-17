#!/bin/bash

# ==============================================================================
# Script: common.sh
# Description: Shared utility functions and configuration for Docker build 
#              verification scripts. Intended to be sourced by other scripts.
#
# Features:
#   - Automatic project root discovery
#   - Docker/platform detection (ARM64/AMD64)
#   - RAM disk configuration (for macOS/Linux)
#   - Docker image management
# ==============================================================================

# Find project root by searching upwards
find_project_root() {
    local current_dir="$1"
    for ((i=0; i<10; i++)); do
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
is_macos() { [[ "$(uname)" == "Darwin" ]]; }
is_linux() { [[ "$(uname)" == "Linux" ]]; }

# Platform detection (ARM64 vs AMD64)
get_platform_arch() {
    local arch=$(uname -m)
    case "$arch" in
        x86_64|amd64) echo "amd64" ;;
        arm64|aarch64) echo "arm64" ;;
        *) echo "unknown" ;;
    esac
}

# Docker image configuration
DOCKER_IMAGE_NAME="unrealng-build"
DOCKER_IMAGE_TAG="qt6.9.3"
DOCKER_FULL_IMAGE="${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}"
DOCKERFILE_PATH="${PROJECT_ROOT}/docker/Dockerfile.universal"

# RAM disk configuration
RAM_DISK_NAME="UnrealDockerRAM"
if is_macos; then
    RAM_DISK_MOUNT_POINT="/Volumes/${RAM_DISK_NAME}"
else
    RAM_DISK_MOUNT_POINT="/mnt/${RAM_DISK_NAME}"
fi
DEFAULT_RAM_SIZE_GIB=6  # Docker builds need more space

RAM_PROJECT_ROOT="${RAM_DISK_MOUNT_POINT}/unreal-ng"
RAM_LOG_DIR="${RAM_PROJECT_ROOT}/build-logs"
RAM_BUILD_DIR="${RAM_PROJECT_ROOT}/build-docker"

# Check if Docker is available
check_docker() {
    if ! command -v docker &> /dev/null; then
        echo "ERROR: Docker is not installed or not in PATH."
        exit 1
    fi
    if ! docker info &> /dev/null; then
        echo "ERROR: Docker daemon is not running."
        exit 1
    fi
}

# Check if our Docker image exists for the current platform
docker_image_exists() {
    local platform_arch=$(get_platform_arch)
    docker image inspect "${DOCKER_FULL_IMAGE}" &> /dev/null
}

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

# Format helpers
format_duration() {
    local seconds=$1
    local h=$((seconds / 3600))
    local m=$(( (seconds % 3600) / 60 ))
    local s=$((seconds % 60))
    if [ $h -gt 0 ]; then
        printf "%dh %dm %ds" $h $m $s
    elif [ $m -gt 0 ]; then
        printf "%dm %ds" $m $s
    else
        printf "%ds" $s
    fi
}

format_size() {
    local bytes=$1
    if [[ -z "$bytes" || "$bytes" -eq 0 ]]; then
        echo "N/A"
        return
    fi
    awk -v b="$bytes" 'BEGIN {
        if (b >= 1073741824)
            printf "%.2f GiB", b / 1073741824
        else
            printf "%.2f MiB", b / 1048576
    }'
}
