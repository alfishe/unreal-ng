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

### Tape Fixture Vetting

Tools live in per-format subfolders — `tape/tzx/` and `tape/tap/` (CSW joins when P3 lands).

#### `tape/tzx/tzx-blockscan.py`
Walks `.tzx` files block-by-block per the TZX 1.13/1.20 layout and prints a per-block-type histogram per file. Detects bad magic, truncated blocks, and unknown block ids; tolerates known preservation-tool variants (BASin's count-less `0x30`, the u16-payload `0x32` seen on SU-archive files).

*   **Use-case:** pre-vetting tape fixtures for `testdata/loaders/tzx/` before they reach the emulator.
*   **Usage:**
    ```bash
    python3 tools/verification/tape/tzx/tzx-blockscan.py 'testdata/loaders/tzx/*.tzx'
    ```
*   **Exit status:** `0` only if every file walks cleanly to EOF (gate-friendly); `1` on any bad/unsupported/truncated file.
*   **Note:** a fast pre-vet only — the emulator's `LoaderTZX` remains the authoritative parser.

#### `tape/tzx/tzx-trace.py`
Stepwise trace of one file's block walk using `tzx-blockscan.py`'s own skip table: offset, block id, body length and the first body bytes for every block, ending at EOF or the exact block that breaks the walk.

*   **Use-case:** debugging a file the block scanner rejects — pinpoints the offending block instead of just failing the file.
*   **Usage:**
    ```bash
    python3 tools/verification/tape/tzx/tzx-trace.py testdata/loaders/tzx/parallax-demo.tzx
    ```
*   **Exit status:** `0` if every file walks to EOF; `1` on any unsupported/truncated block; `2` with no arguments.

#### `tape/tzx/tzx-fixture-verify.py`
Loads every fixture in `testdata/loaders/tzx/` through the running emulator's own tape loader via WebAPI (`POST /emulator/:id/tape/load` + `GET /emulator/:id/tape`) and prints each catalog's block count and fast-load verdict.

*   **Use-case:** the authoritative fixture gate — the emulator's parser, not a reimplementation, decides what is loadable.
*   **Usage:** start an emulator with WebAPI on `localhost:8090` first, then:
    ```bash
    python3 tools/verification/tape/tzx/tzx-fixture-verify.py
    ```
*   **Exit status:** `0` when every fixture loads and produces a block catalog; `1` on any load failure or empty catalog; `2` on environment errors (no fixtures, WebAPI unreachable).
*   **Note:** creates and stops its own emulator instance (`--model 128k` default; `--model <name>` to override).

#### `tape/tap/tap-audit.py`
Independent framing audit of `.tap` dumps: walks every block (u16 length + body, ROM header decode, XOR parity) with a parser that shares no code with the emulator, so the catalog can be vetted without the loader checking itself.

*   **Use-case:** pre-vetting `.tap` fixtures and cross-checking the Tape Manager catalog against real-world dumps (r8: all 26 repo files audit clean; the DIZZY_X headerless rows are genuine custom-loader payloads, not parser misses).
*   **Usage:**
    ```bash
    python3 tools/verification/tape/tap/tap-audit.py testdata/loaders/tap/*.tap
    ```
*   **Exit status:** `0` on a clean walk; `1` on framing anomalies (truncated block, trailing byte, zero-length block); `2` on usage/environment errors. Non-standard flag bytes and invalid parity are printed as notes (`~`) without failing — the catalog handles them by design (Custom kind, checksum INVALID).
*   **Note:** a fast pre-vet only — the emulator's `LoaderTAP` remains the authoritative parser.

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
