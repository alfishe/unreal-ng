# Unreal-NG Agent Rules

## Project Structure (What we have and where)
- **`core/`**: The core emulator engine (CPU, memory, I/O devices, hardware subsystems). This is completely decoupled from any GUI.
- **`core/tests/`**: Unit and integration tests (GTest). This is where all the `core-tests` live to ensure high-fidelity emulation accuracy.
- **`unreal-qt/`**: The Qt-based desktop UI (Debugger, Memory Viewer, Screen Viewer).
- **`scratch/`**: Git-ignored directory for temporary files, test artifacts, and logs. Do NOT write test artifacts to the project root.
- **`docs/`**: Project documentation, reference materials, and design specs.
- **`tools/`**: Tooling and utilities for verification, builds, etc.

## Building the Project
We use CMake with Ninja for building:
```bash
# Configure the build system
cmake -S . -B cmake-build-release -G Ninja

# Build the emulator and tests
ninja -C cmake-build-release
```

## Running Tests
Tests are executed using the `core-tests` binary. They should be built first:
```bash
# Run all tests
./cmake-build-release/bin/core-tests

# Run specific tests
./cmake-build-release/bin/core-tests --gtest_filter="*TestName*"
```

## Agent Rules & Guidelines
- **Test Artifacts**: ALL test artifacts and temporary files (e.g. `.wav`, `.trd`, `.sna`) MUST be written to the `scratch/` directory. Do not clutter the project root. Use `TestPathHelper::GetTestScratchPath()` for this.
- **Naming Conventions**: Do not use underscores in file names or C++ class/struct/method names unless specified. Use PascalCase for methods and camelCase for variables/fields.
- **Coding Guidelines**: For detailed coding guidelines, see `docs/coding_guidelines.md`.
