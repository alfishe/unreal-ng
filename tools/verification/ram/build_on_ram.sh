#!/bin/bash

# ==============================================================================
# Script: build_on_ram.sh
# Description: Executes high-speed compilation of all major project components 
#              within the RAM disk environment.
#
# Components Built:
#   1. core (Library, Tests, Benchmarks)
#   2. automation (Plugin/Library)
#   3. testclient (CLI tool)
#   4. unreal-qt (Main Application)
#
# Metrics Collected:
#   - Build duration for each component.
#   - Final artifact size (executable or static library).
#   - Overall build directory size before cleanup.
#
# Space Management:
#   - Automatically deletes build artifacts (build directories) after each 
#     successful target compilation to minimize peak RAM usage.
#   - Preserves build logs in the build-logs directory on the RAM disk.
#
# Error Handling:
#   - Continues building subsequent targets even if one fails.
#   - Exits with a non-zero status if any target fails.
#
# Performance Tracking:
#   - Tracks and reports the execution time for each build target.
#   - Explicitly performs Release builds (-DCMAKE_BUILD_TYPE=Release).
#
# Checks Performed:
#   - Verifies RAM disk mount.
#   - Verifies project existence on the RAM disk.
#
# Success Output:
#   - Prints a summary report of all build targets and their status.
#
# Usage:
#   ./build_on_ram.sh
# ==============================================================================

# No set -e, as we want to continue even if a build fails
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

if ! is_mounted "$RAM_DISK_MOUNT_POINT"; then
    echo "ERROR: RAM disk not mounted at $RAM_DISK_MOUNT_POINT. Please run create_ram_disk.sh first."
    exit 1
fi

if [[ ! -d "$RAM_PROJECT_ROOT" ]]; then
    echo "ERROR: Project not found on RAM disk at $RAM_PROJECT_ROOT."
    exit 1
fi

# Create log directory on RAM disk
mkdir -p "$RAM_LOG_DIR"

PARALLEL_JOBS=$(get_parallel_jobs)

# Track build results
TARGETS=()
STATUSES=()
DURATIONS=()
ARTIFACT_SIZES=()
BUILD_SIZES=()
ANY_FAILED=0

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
    # Use awk for floating point math and formatting
    awk -v b="$bytes" 'BEGIN {
        if (b >= 1073741824)
            printf "%.2f GiB", b / 1073741824
        else
            printf "%.2f MiB", b / 1048576
    }'
}

get_artifact_size() {
    local build_dir="$1"
    local target="$2"
    local artifact_path=""

    # Find the artifact. We look for:
    # 1. Executables (target or target.exe)
    # 2. Static libraries (libtarget.a or target.lib)
    # 3. Shared libraries (libtarget.so or libtarget.dylib or target.dll)
    # We use -type f and head -n 1 to pick the first match.
    artifact_path=$(find "$build_dir" -type f \( \
        -name "$target" -o \
        -name "$target.exe" -o \
        -name "lib$target.a" -o \
        -name "lib$target.so" -o \
        -name "lib$target.dylib" -o \
        -name "$target.lib" -o \
        -name "$target.dll" \
    \) 2>/dev/null | head -n 1)

    if [[ -n "$artifact_path" && -f "$artifact_path" ]]; then
        wc -c < "$artifact_path" | tr -d ' '
    else
        echo 0
    fi
}

get_dir_size() {
    local dir="$1"
    if [[ -d "$dir" ]]; then
        # du -sk returns size in KiB.
        local kib=$(du -sk "$dir" | awk '{print $1}')
        echo $((kib * 1024))
    else
        echo 0
    fi
}

build_target() {
    local source_dir="$1"
    local build_dir="$2"
    local target="$3"
    local cmake_args="$4 -DCMAKE_BUILD_TYPE=Release"
    local log_file="${RAM_LOG_DIR}/${target}.log"

    TARGETS+=("$target")
    local start_time=$(date +%s)

    # Initialize/Clear log file
    : > "$log_file"

    echo "--- Building target: $target in $source_dir ---"
    echo "Logs saved to: $log_file"
    
    local status=0
    
    if [[ ! -d "$build_dir" ]]; then
        echo "Configuring (Release)..." | tee -a "$log_file"
        if ! cmake -S "$source_dir" -B "$build_dir" $cmake_args 2>&1 | tee -a "$log_file"; then
            status=1
        fi
    fi

    if [[ $status -eq 0 ]]; then
        echo "Building (Release)..." | tee -a "$log_file"
        if ! cmake --build "$build_dir" --target "$target" --parallel "$PARALLEL_JOBS" 2>&1 | tee -a "$log_file"; then
            status=1
        fi
    fi

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    DURATIONS+=("$(format_duration $duration)")

    local artifact_size_bytes=0
    local build_dir_size_bytes=0
    if [[ $status -eq 0 ]]; then
        artifact_size_bytes=$(get_artifact_size "$build_dir" "$target")
        build_dir_size_bytes=$(get_dir_size "$build_dir")
        
        local artifact_size_formatted=$(format_size "$artifact_size_bytes")
        local build_dir_size_formatted=$(format_size "$build_dir_size_bytes")
        
        ARTIFACT_SIZES+=("$artifact_size_formatted")
        BUILD_SIZES+=("$build_dir_size_formatted")
        
        echo "Done building $target in $(format_duration $duration). Artifact: $artifact_size_formatted, Build Folder: $build_dir_size_formatted" | tee -a "$log_file"
        STATUSES+=("SUCCESS")
    else
        ARTIFACT_SIZES+=("N/A")
        BUILD_SIZES+=("N/A")
        echo "FAILED building $target after $(format_duration $duration)." | tee -a "$log_file"
        STATUSES+=("FAILED")
        ANY_FAILED=1
    fi

    echo "Cleaning up build directory: $build_dir"
    rm -rf "$build_dir"
    echo
    
    return $status
}

# 1) Core
build_target "$RAM_PROJECT_ROOT" "$RAM_PROJECT_ROOT/build-root" "core" "-DTESTS=ON -DBENCHMARKS=ON"

# 2) Automation (part of unreal-qt project)
build_target "$RAM_PROJECT_ROOT/unreal-qt" "$RAM_PROJECT_ROOT/unreal-qt/build-qt" "automation" ""

# 3) Unit tests
build_target "$RAM_PROJECT_ROOT" "$RAM_PROJECT_ROOT/build-root" "core-tests" "-DTESTS=ON -DBENCHMARKS=ON"

# 4) Benchmarks
build_target "$RAM_PROJECT_ROOT" "$RAM_PROJECT_ROOT/build-root" "core-benchmarks" "-DTESTS=ON -DBENCHMARKS=ON"

# 5) Testclient
build_target "$RAM_PROJECT_ROOT" "$RAM_PROJECT_ROOT/build-root" "testclient" "-DTESTS=ON -DBENCHMARKS=ON"

# 6) Unreal-qt
build_target "$RAM_PROJECT_ROOT/unreal-qt" "$RAM_PROJECT_ROOT/unreal-qt/build-qt" "unreal-qt" ""

echo "--------------------------------------------------------------------------------------------"
echo "Build Summary:"
for i in "${!TARGETS[@]}"; do
    printf " - %-16s [%-7s] (Time: %8s, Artifact: %10s, Build Folder: %10s)\n" \
        "${TARGETS[$i]}" "${STATUSES[$i]}" "${DURATIONS[$i]}" "${ARTIFACT_SIZES[$i]}" "${BUILD_SIZES[$i]}"
done
echo "--------------------------------------------------------------------------------------------"

if [[ $ANY_FAILED -ne 0 ]]; then
    echo "Build process completed with ERRORS."
    exit 1
else
    echo "All components built successfully on RAM disk."
    exit 0
fi
