# Verification Tools

This directory contains scripts for automating build verification and performance testing of the Unreal project.

## Scripts Overview

### Build Matrix Testing

#### `test_build_combinations-unreal-qt.sh`
Tests multiple CMake build combinations for the `unreal-qt` project to ensure that optional features (Lua, Python, WebAPI, CLI) do not break the build when enabled or disabled in various permutations.

*   **Use-case:** Pre-commit checks, CI/CD validation.
*   **Usage:**
    ```bash
    ./test_build_combinations-unreal-qt.sh
    ```
*   **Output:** Logs results to `unreal-qt/build_combinations.log` and the console.

---

### High-Performance RAM Builds

Located in the `ram/` subdirectory, these scripts allow you to build the entire project on a RAM disk, significantly reducing build times and SSD wear.

#### `ram/run_all.sh` (Primary Orchestrator)
Performs the full workflow: creating the RAM disk, copying the project, building all components, and cleaning up.

*   **Usage:**
    ```bash
    # Run with default 4GiB RAM disk and cleanup at the end
    ./ram/run_all.sh

    # Run with 8GiB RAM disk and keep it mounted after build
    ./ram/run_all.sh 8 --keep
    ```
*   **Behavior on Failure:** The script attempts to build all components even if one or more fails. If any target fails, the script will automatically skip the unmount step, even if `--keep` was not specified. This ensures that logs and build artifacts remain accessible for troubleshooting.
*   **Logs:** Build logs for each component are preserved on the RAM disk in the `unreal-ng/build-logs` directory.

#### Individual RAM Scripts
*   **`ram/create_ram_disk.sh [size]`**: Creates a RAM disk (macOS: HFS+, Linux: tmpfs).
*   **`ram/copy_to_ram.sh`**: Syncs project source to RAM disk (excludes `.git` and build dirs).
*   **`ram/build_on_ram.sh`**: Builds all project components on the RAM disk.
    *   **Performance Tracking:** Reports execution time, primary artifact size, and total build directory size for each component in a final summary.
    *   **Build Type:** Explicitly performs **Release** builds.
    *   **Resilience:** Continues even if some targets fail.
    *   **Logs:** Saves duplicated output to target-specific logs in `build-logs`.
*   **`ram/cleanup_ram_disk.sh [--wipe]`**: Unmounts the disk or wipes its content.

---

## Compatibility
All scripts are designed to be cross-platform and have been tested on:
*   **macOS** (using `hdiutil` and `sysctl`)
*   **Linux (Ubuntu/Debian)** (using `mount -t tmpfs` and `nproc`)
*   **WSL (Windows Subsystem for Linux)**

## Requirements
*   `cmake`
*   `rsync`
*   `sudo` privileges (required on Linux/WSL for mounting/unmounting)
