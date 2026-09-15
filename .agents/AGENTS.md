# Unreal-NG Agent Rules

> **CRITICAL**: NEVER commit changes without explicit user request.
> - Each commit permission is **one-time only** — no blanket permissions
> - Steps before any commit:
>   1. Run quality checks:
>      - Build: `ninja -C cmake-build-release` must pass
>      - Tests: `./cmake-build-release/bin/core-tests` must pass
>      - No compiler warnings (zero-warnings policy)
>      - Documentation: verify all cross-references and links are valid
>   2. Report results and wait for explicit "commit" instruction

## Project Structure (What we have and where)

| Directory | Description |
|-----------|-------------|
| **`core/`** | The core emulator engine (CPU, memory, I/O devices). Decoupled from GUI. |
| ↳ **`core/automation/`** | Automation interfaces (CLI, Python, Lua, WebAPI). |
| ↳ **`core/benchmarks/`** | Performance benchmarks using Google Benchmark. |
| ↳ **`core/tests/`** | Unit and integration tests (GTest) to ensure high-fidelity accuracy. |
| **`unreal-qt/`** | The Qt-based desktop UI (Debugger, Memory Viewer, Screen Viewer). |
| **`unreal-screen-viewer/`**| Standalone Screen Viewer application. |
| **`unreal-videowall/`** | Standalone Video Wall application. |
| **`scratch/`** | Git-ignored dir for test artifacts and logs. Do NOT write artifacts to root. |
| **`docs/`** | Project documentation, reference materials, and design specs. |
| ↳ **`docs/inprogress/`** | Active design documents, brainstorming, and research. |
| **`tools/`** | Tooling and utilities for verification, builds, etc. |
| **`testdata/`** | Test fixtures, disk images, and ROMs. |
| **`lib/`** | Third-party dependencies and submodules (e.g., GTest, Google Benchmark). |
| **`tools/poc/`** | Proof of Concept directory for isolated throwaway code and experiments. |

## Building the Project
We use CMake with Ninja for building:
```bash
# Configure the build system
cmake -S . -B cmake-build-release -G Ninja

# Build the main applications (unreal-qt, unreal-mcp-bridge, etc.)
ninja -C cmake-build-release
```

## Writing Tests
Full guide: [`core/tests/README.md`](../core/tests/README.md). Non-negotiables:

- **Never `sleep_for` to wait.** Use `TestWait::For` / `ForAtLeast` / `ForExactly`
  (`core/tests/_helpers/testwaithelper.h`). A fixed sleep is simultaneously the
  slowest and the flakiest way to synchronise.
- **Under 50 ms per test.** Slower needs a comment justifying it; booting a real
  ROM to a machine state is about the only good reason.
- **Stop looping once the assertion can no longer fail** - keep the full work on
  the failing path so diagnostics stay intact.
- **`EnableTurboMode()` on boot-bound tests** (~2.7x: mutes audio, decimates
  rendering). Never in a test that asserts on rendered pixels.
- **Know what your assertion can see**: frame-boundary VRAM reads miss
  intra-frame changes, and breakpoints do not fire under `RunNFrames`
  (`skipBreakpoints = true` by default).
- **Scratch files need per-process unique names** - `TestPathHelper::GetUniqueTestScratchPath()`.
- **Avoid `std::vector::resize(n, 0)` on large POD buffers** (~58 ms/24 MiB at `-O0`) — use value-initialization (`new T[n]()`) or `ZeroInitBuffer` for sub-millisecond zero-fill in both Debug and Release.

## Running Tests & Benchmarks
Tests and benchmarks are opt-in (`-DTESTS=ON`, `-DBENCHMARKS=ON`) to keep standard dev builds fast:
```bash
# Configure with tests enabled
cmake -S . -B cmake-build-release -G Ninja -DTESTS=ON

# Run all tests in parallel (automatically builds core-tests on demand)
cmake --build cmake-build-release --target test-parallel
# Or run tests sequentially:
ninja -C cmake-build-release core-tests && ./cmake-build-release/bin/core-tests

# Run specific tests
./cmake-build-release/bin/core-tests --gtest_filter="*TestName*"

# Configure with benchmarks enabled
cmake -S . -B cmake-build-release -G Ninja -DBENCHMARKS=ON

# Build and run benchmarks
ninja -C cmake-build-release core-benchmarks
./cmake-build-release/bin/core-benchmarks --benchmark_filter="*BenchName*"
```


### Parallel Test Execution (GTest Sharding)
The `test-parallel` CMake target uses GTest's built-in sharding to split tests across 4 processes:
- **Sequential:** ~37s at 76% CPU
- **Parallel (4-way):** ~12s at 250% CPU
- **Speedup:** ~3x on 4+ core machines

Manual sharding (useful for CI pipelines):
```bash
for i in 0 1 2 3; do
  GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=$i ./cmake-build-release/bin/core-tests &
done
wait
```

## WebAPI Verification Testing

When testing WebAPI changes, follow this sequence to ensure a fresh emulator instance:

```bash
# 1. Kill any stale emulator instances (only one can bind port 8090)
pkill -9 unreal-qt 2>/dev/null || true
sleep 1

# 2. Verify port 8090 is free
lsof -i :8090 2>/dev/null && echo "WARNING: Port 8090 still in use!" || echo "Port 8090 is free"

# 3. Start the freshly built emulator (macOS path)
./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
sleep 4

# 4. Verify WebAPI is responding
curl -s http://localhost:8090/api/v1/emulator | jq .

# 5. Create an emulator instance
curl -s -X POST "http://localhost:8090/api/v1/emulator/start" \
  -H "Content-Type: application/json" \
  -d '{"model": "128k"}' | jq .
# Save the returned "id" for subsequent calls

# 6. Test your feature (example: symbolic disassembly with labels)
EMU_ID="<id-from-step-5>"
curl -s -X POST "http://localhost:8090/api/v1/emulator/$EMU_ID/labels" \
  -H "Content-Type: application/json" \
  -d '{"name": "TEST_LABEL", "address": 4, "type": "code"}'
curl -s "http://localhost:8090/api/v1/emulator/$EMU_ID/disasm?address=0&count=10" | jq '.instructions[]'

# 7. Cleanup when done
pkill -9 unreal-qt 2>/dev/null || true
```

**Platform-specific binary paths:**
- macOS: `./cmake-build-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt`
- Linux: `./cmake-build-release/bin/unreal-qt`
- Windows: `./cmake-build-release/bin/unreal-qt.exe`

**Available models:** `PENTAGON`, `48K`, `128k`, `PLUS2`, `PLUS2A`, `PLUS3`, `SCORPION`, `ATM1`, `ATM2`, `ATM3`, `PROFI`

## Agent Rules & Guidelines
- **Test Artifacts**: ALL test artifacts and temporary files (e.g. `.wav`, `.trd`, `.sna`) MUST be written to the `scratch/` directory. Do not clutter the project root. Use `TestPathHelper::GetTestScratchPath()` for this.
- **Naming Conventions**: Do not use underscores in file names or C++ class/struct/method names. Use PascalCase for methods and camelCase for variables/fields. **Exception**: Test files use `*_test.cpp` suffix and test classes use `ClassName_Test` pattern.
- **Testing**: See `core/tests/README.md` for test patterns (CUT pattern, fixtures, helpers).
- **Documentation Rules**: Documentation files must use lowercase with hyphens (kebab-case). Ongoing design and analysis must go into `docs/inprogress/` following specific date-prefixed directory naming rules. See `docs/inprogress/README.md` for details.
- **Coding Guidelines**: For detailed coding guidelines, see `docs/guidelines/coding-guidelines.md`.
- **Cross-Platform & Compatibility**: The codebase MUST be cross-platform (Windows, macOS, Linux) and cross-compiler compatible (gcc, clang, mingw, msvc) with **ZERO warnings** allowed. See `docs/guidelines/cross-platform-compatibility.md` for environmental constraints.
