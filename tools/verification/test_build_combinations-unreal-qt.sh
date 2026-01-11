#!/bin/bash

# ==============================================================================
# Script: test_build_combinations-unreal-qt.sh
# Description: Automatically tests various CMake build combinations for the 
#              unreal-qt project. It iterates through different automation 
#              options (LUA, PYTHON, WEBAPI, CLI) to ensure that the project 
#              compiles correctly with any valid combination of features.
#
# Use Cases:
#   - CI/CD pipelines to prevent build regressions.
#   - Pre-commit verification when changing automation-related code.
#   - Ensuring that optional features don't break the core build.
#
# Usage:
#   ./test_build_combinations-unreal-qt.sh
#
# Performance:
#   - Uses all available CPU cores for parallel builds.
#   - Cleans build directory between combinations to ensure isolation.
#
# Checks Performed:
#   - Finds project root recursively (up to 10 levels).
#   - Verifies CMake configuration success for each combination.
#   - Verifies successful compilation for each combination.
#   - Logs all output to both console and 'build_combinations.log'.
# ==============================================================================

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Path Discovery ---
# Get the absolute path to the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Function to find project root by searching upwards
find_project_root() {
    local current_dir="$1"
    for ((i=0; i<10; i++)); do
        # We look for the root CMakeLists.txt that defines 'project (unreal)'
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

# Try to find root starting from current working directory, then from script directory
PROJECT_ROOT=$(find_project_root "$(pwd)")
if [[ -z "$PROJECT_ROOT" ]]; then
    PROJECT_ROOT=$(find_project_root "$SCRIPT_DIR")
fi

if [[ -z "$PROJECT_ROOT" ]]; then
    echo "ERROR: Could not find project root (containing CMakeLists.txt with 'project (unreal)')"
    echo "Searched 10 levels up from PWD ($(pwd)) and script directory ($SCRIPT_DIR)"
    exit 1
fi

# The main CMake source directory is unreal-qt
SOURCE_DIR="${PROJECT_ROOT}/unreal-qt"

# --- Configuration ---
BUILD_DIR="${SOURCE_DIR}/cmake-build-test-combinations"
LOG_FILE="${SOURCE_DIR}/build_combinations.log"
# Check OS for parallel jobs command
if [[ "$(uname)" == "Darwin" ]]; then
    PARALLEL_JOBS=$(sysctl -n hw.ncpu)
else
    PARALLEL_JOBS=$(nproc)
fi


# --- Helper Functions ---
function print_header() {
    echo "================================================================================"
    echo "$1"
    echo "================================================================================"
}

function run_build() {
    local combination_name="$1"
    shift
    local cmake_args="$@"

    print_header "Testing Combination: $combination_name"
    echo "CMake arguments: $cmake_args"

    # Clean and Configure
    echo "--- Configuring ---"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    {
        cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" $cmake_args
    } || {
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        echo "ERROR: CMake configuration failed for combination: $combination_name"
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        # Since set -e is active, the script will exit.
        exit 1
    }

    # Build
    echo "--- Building ---"
    {
        cmake --build "$BUILD_DIR" --parallel "$PARALLEL_JOBS"
    } || {
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        echo "ERROR: Build failed for combination: $combination_name"
        echo "See $BUILD_DIR/CMakeFiles/CMakeOutput.log and $BUILD_DIR/CMakeFiles/CMakeError.log"
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        exit 1
    }

    echo "--- Success for combination: $combination_name ---"
    echo
}


# --- Main Script ---

# Clear log file
> "$LOG_FILE"

# Redirect all output to both console and log file
exec > >(tee -a "$LOG_FILE") 2>&1

print_header "Starting CMake build matrix test"

# --- Test with ENABLE_AUTOMATION=OFF ---
run_build "ENABLE_AUTOMATION=OFF" "-DENABLE_AUTOMATION=OFF"


# --- Test with ENABLE_AUTOMATION=ON ---
AUTOMATION_OPTIONS=(
    "ENABLE_LUA_AUTOMATION"
    "ENABLE_PYTHON_AUTOMATION"
    "ENABLE_WEBAPI_AUTOMATION"
    "ENABLE_CLI_AUTOMATION"
)
NUM_OPTIONS=${#AUTOMATION_OPTIONS[@]}
TOTAL_COMBINATIONS=$((1 << NUM_OPTIONS))

for ((i=0; i<$TOTAL_COMBINATIONS; i++)); do
    combo_string_array=()
    cmake_args_array=("-DENABLE_AUTOMATION=ON")

    for ((j=0; j<$NUM_OPTIONS; j++)); do
        option_name=${AUTOMATION_OPTIONS[$j]}
        if (( (i >> j) & 1 )); then
            option_value="ON"
        else
            option_value="OFF"
        fi
        combo_string_array+=("$option_name=$option_value")
        cmake_args_array+=("-D$option_name=$option_value")
    done

    combination_name=$(IFS=,; echo "${combo_string_array[*]}")
    cmake_args=$(IFS=' '; echo "${cmake_args_array[*]}")

    run_build "$combination_name" "$cmake_args"
done

print_header "All build combinations passed successfully!"
