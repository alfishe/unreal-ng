# Unreal-NG Agent Rules

## Project Structure (What we have and where)
- **`core/`**: The core emulator engine (CPU, memory, I/O devices, hardware subsystems). This is completely decoupled from any GUI.
  - **`core/automation/`**: Automation interfaces (CLI, Python, Lua, WebAPI).
  - **`core/benchmarks/`**: Performance benchmarks using Google Benchmark.
  - **`core/tests/`**: Unit and integration tests (GTest). This is where all the `core-tests` live to ensure high-fidelity emulation accuracy.
- **`unreal-qt/`**: The Qt-based desktop UI (Debugger, Memory Viewer, Screen Viewer).
- **`unreal-screen-viewer/`**: Standalone Screen Viewer application.
- **`unreal-videowall/`**: Standalone Video Wall application.
- **`scratch/`**: Git-ignored directory for temporary files, test artifacts, and logs. Do NOT write test artifacts to the project root.
- **`docs/`**: Project documentation, reference materials, and design specs.
  - **`docs/inprogress/`**: Active design documents, brainstorming, and research.
- **`tools/`**: Tooling and utilities for verification, builds, etc.
- **`testdata/`**: Test fixtures, disk images, and ROMs.
- **`lib/`**: Third-party dependencies and submodules (e.g., GTest, Google Benchmark).
- **`poc/`**: Proof of Concept directory for isolated throwaway code, experiments, and non-production research.

## Building the Project
We use CMake with Ninja for building:
```bash
# Configure the build system
cmake -S . -B cmake-build-release -G Ninja

# Build the emulator and tests
ninja -C cmake-build-release
```

## Running Tests & Benchmarks
Tests are executed using the `core-tests` binary, and benchmarks via `core-benchmarks`:
```bash
# Run all tests
./cmake-build-release/bin/core-tests

# Run specific tests
./cmake-build-release/bin/core-tests --gtest_filter="*TestName*"

# Run all benchmarks
./cmake-build-release/bin/core-benchmarks

# Run specific benchmarks
./cmake-build-release/bin/core-benchmarks --benchmark_filter="*BenchName*"
```

## Agent Rules & Guidelines
- **Test Artifacts**: ALL test artifacts and temporary files (e.g. `.wav`, `.trd`, `.sna`) MUST be written to the `scratch/` directory. Do not clutter the project root. Use `TestPathHelper::GetTestScratchPath()` for this.
- **Naming Conventions**: Do not use underscores in file names or C++ class/struct/method names unless specified. Use PascalCase for methods and camelCase for variables/fields.
- **Documentation Rules**: Documentation files must use lowercase with hyphens (kebab-case). Ongoing design and analysis must go into `docs/inprogress/` following specific date-prefixed directory naming rules. See `docs/inprogress/README.md` for details.
- **Coding Guidelines**: For detailed coding guidelines, see `docs/coding-guidelines.md`.
