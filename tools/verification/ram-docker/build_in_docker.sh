#!/bin/bash

# ==============================================================================
# Script: build_in_docker.sh
# Description: Executes the build inside Docker container with RAM disk mount.
#
# Build Process:
#   1. Mounts RAM disk project as /workspace inside container
#   2. Creates build directory inside container
#   3. Runs CMake configure and build
#   4. Reports build status and artifact sizes
#
# Components Built:
#   - unreal-qt (main application)
#   - automation (plugin)
#   - core-tests (unit tests)
#
# Usage:
#   ./build_in_docker.sh
# ==============================================================================

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

check_docker

if ! docker_image_exists; then
    echo "ERROR: Docker image not found: $DOCKER_FULL_IMAGE"
    echo "Run ensure_image.sh first."
    exit 1
fi

if ! is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "ERROR: RAM disk not mounted at $RAM_DISK_MOUNT_POINT"
    echo "Run create_ram_disk.sh first."
    exit 1
fi

if [[ ! -d "$RAM_PROJECT_ROOT" ]]; then
    echo "ERROR: Project not found on RAM disk at $RAM_PROJECT_ROOT"
    echo "Run copy_to_ram.sh first."
    exit 1
fi

# Verify Docker can access the RAM disk volume (macOS VirtioFS can have stale mounts)
verify_docker_volume_access() {
    docker run --rm -v "$RAM_DISK_MOUNT_POINT:/test:ro" alpine:latest ls /test >/dev/null 2>&1
}

echo "Verifying Docker volume access..."
if ! verify_docker_volume_access; then
    echo "⚠️  Docker volume access failed - forcing RAM disk remount..."
    
    # Force remount: unmount, recreate, resync
    "${SCRIPT_DIR}/cleanup_ram_disk.sh" 2>/dev/null || true
    sleep 1
    "${SCRIPT_DIR}/create_ram_disk.sh" "$DEFAULT_RAM_SIZE_GIB"
    "${SCRIPT_DIR}/copy_to_ram.sh"
    
    # Retry verification
    echo "Retrying Docker volume access..."
    if ! verify_docker_volume_access; then
        echo ""
        echo "╔══════════════════════════════════════════════════════════════╗"
        echo "║  ❌ DOCKER VOLUME ACCESS STILL FAILING                       ║"
        echo "║                                                              ║"
        echo "║  Force remount did not resolve the issue.                    ║"
        echo "║  Try: Restart Docker Desktop, then retry.                    ║"
        echo "╚══════════════════════════════════════════════════════════════╝"
        exit 1
    fi
fi
echo "✓ Docker can access RAM disk volume"
echo ""

mkdir -p "$RAM_LOG_DIR"

PARALLEL_JOBS=$(get_parallel_jobs)

# Track build results
TARGETS=()
STATUSES=()
DURATIONS=()
ANY_FAILED=0

build_target() {
    local source_subdir="$1"
    local target="$2"
    local cmake_args="$3"
    local log_file="${RAM_LOG_DIR}/${target}.log"
    
    TARGETS+=("$target")
    local start_time=$(date +%s)
    
    : > "$log_file"
    
    echo "--- Building target: $target ---"
    echo "Logs: $log_file"
    
    local container_source="/workspace"
    if [[ -n "$source_subdir" ]]; then
        container_source="/workspace/$source_subdir"
    fi
    local container_build="/workspace/build-docker-${target}"
    
    local status=0
    
    # Run CMake configure and build inside Docker
    docker run --rm \
        --platform "linux/$(get_platform_arch)" \
        -v "$RAM_PROJECT_ROOT:/workspace:rw" \
        -w "$container_source" \
        "$DOCKER_FULL_IMAGE" \
        bash -c "
            set -e
            mkdir -p $container_build
            cd $container_build
            echo 'Configuring...'
            cmake -G Ninja $container_source $cmake_args -DCMAKE_BUILD_TYPE=Release
            echo 'Building...'
            cmake --build . --target $target --parallel $PARALLEL_JOBS
            echo 'Build complete!'
        " 2>&1 | tee -a "$log_file"
    
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        status=1
    fi
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    DURATIONS+=("$(format_duration $duration)")
    
    if [[ $status -eq 0 ]]; then
        echo "✓ $target built successfully in $(format_duration $duration)"
        STATUSES+=("SUCCESS")
    else
        echo "✗ $target FAILED after $(format_duration $duration)"
        STATUSES+=("FAILED")
        ANY_FAILED=1
    fi
    
    # Cleanup build directory to save RAM
    rm -rf "${RAM_PROJECT_ROOT}/build-docker-${target}"
    echo ""
    
    return $status
}

echo "=== Docker Build on RAM Disk ==="
echo "Image:    $DOCKER_FULL_IMAGE"
echo "Platform: $(get_platform_arch)"
echo "Jobs:     $PARALLEL_JOBS"
echo "================================="
echo ""

# Build targets (matching ram/build_on_ram.sh)
# 1) Core
build_target "" "core" "-DTESTS=ON -DBENCHMARKS=ON"

# 2) Automation (part of unreal-qt project)
build_target "unreal-qt" "automation" ""

# 3) Unit tests
build_target "" "core-tests" "-DTESTS=ON -DBENCHMARKS=ON"

# 4) Benchmarks
build_target "" "core-benchmarks" "-DTESTS=ON -DBENCHMARKS=ON"

# 5) Testclient
build_target "" "testclient" "-DTESTS=ON -DBENCHMARKS=ON"

# 6) Unreal-qt
build_target "unreal-qt" "unreal-qt" ""

# Summary
echo "--------------------------------------------------------------------------------------------"
echo "Build Summary:"
for i in "${!TARGETS[@]}"; do
    printf " - %-16s [%-7s] (Time: %s)\n" \
        "${TARGETS[$i]}" "${STATUSES[$i]}" "${DURATIONS[$i]}"
done
echo "--------------------------------------------------------------------------------------------"

if [[ $ANY_FAILED -ne 0 ]]; then
    echo "Build process completed with ERRORS."
    exit 1
else
    echo "✓ All components built successfully in Docker!"
    exit 0
fi
