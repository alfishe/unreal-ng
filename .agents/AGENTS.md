# Unreal-NG Agent Rules

> **CRITICAL**: NEVER commit changes without explicit user request.
> - Each commit permission is **one-time only** — no blanket permissions
> - **Cap build/test parallelism at 50% of logical cores.** Several agents build on this
>   machine at once — each one launching an unbounded `ninja`/`cmake --build`/`ctest` job
>   count stacks up across agents and brings the machine to a crawl. `tools/build/` (next
>   point) applies the cap for you; for anything that bypasses it (benchmarks, a manual
>   `cmake` configure step) pass an explicit `-j` computed as half the logical cores:
>   ```bash
>   JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))
>   ```
> - **Builds and test runs go through `tools/build/`** — at most 2 builds and 1 test run at
>   once across all agents, each at lowered priority (`nice 10`) with `-j` at half the
>   cores. If the slots are busy the command queues and says so; it does not fail:
>   ```bash
>   tools/build/build.sh                        # full build (the pre-commit one)
>   tools/build/build.sh core-tests             # one target while iterating
>   tools/build/test.sh                         # builds core-tests, then test-parallel
>   tools/build/test.sh --gtest_filter='*Foo*'  # builds core-tests, runs just those tests
>   tools/build/slot.sh --status                # who holds the slots
>   ```
>   Start them as a **background command** and wait for completion (a queued full build can
>   outlast a foreground tool call); a cancelled or timed-out call kills the build and frees
>   the slot within seconds. Do not bypass the wrapper with a bare `ninja` / `cmake --build`.
>   Benchmarks and A/B timing runs are the exception: run them on a quiet machine without
>   lowered priority (`UNREAL_NICE=0`). Details: [`tools/build/README.md`](../tools/build/README.md).
> - `tools/build/build.sh` re-runs the CMake configure step itself (sources are globbed: a file
>   that arrived with a merge or rebase is otherwise missing and the link fails on undefined
>   symbols) and configures a missing build directory. Do not run `cmake` by hand for ordinary
>   builds; `UNREAL_NO_CONFIGURE=1` skips it.
> - Steps before any commit:
>   1. Run the checks that match what changed:
>      - **C++ code in `core/` or a client** (`unreal-qt/`, `unreal-screen-viewer/`,
>        `unreal-videowall/`, `testclient/`, the automation modules in `core/automation/`):
>        **mandatory** full build `tools/build/build.sh` with zero compiler
>        warnings, and `core-tests` must pass (`tools/build/test.sh`)
>      - **Documentation only** (`docs/`, `.recipe/`, other `*.md`): no build, no tests. Verify that
>        cross-references and links resolve and that no machine-specific absolute paths slipped in
>        (`python3 tools/fix-absolute-paths.py --path <changed files>`, dry run by default)
>      - **Only `tools/` scripts and utilities**: no full build, no `core-tests`. Run the changed
>        script or utility itself (its own tests, a dry run, or building just its target)
>      - **Anything else** (test data, INI configs, CMake-only or CI changes, mixed changes without
>        C++): the agent picks checks in proportion to the risk and says what it ran and why
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
| **`/.recipe/`** | AI Agent Recipe Library (copy-pasteable automation recipes for MCP/WebAPI). |
| **`tools/`** | Tooling and utilities for verification, builds, etc. |
| **`testdata/`** | Test fixtures, disk images, and ROMs. |
| **`lib/`** | Third-party dependencies and submodules (e.g., GTest, Google Benchmark). |
| **`tools/poc/`** | Proof of Concept directory for isolated throwaway code and experiments. |

## Building the Project
We use CMake with Ninja for building. **Run builds through `tools/build/build.sh`** (it applies
the limits above); the raw commands below are what it runs, except that the script also re-runs the
configure step first (see "Why `build.sh` configures every time" below). **Cap `-j` at 50% of logical cores** — multiple agents
build concurrently on this machine, and unbounded job counts pile up and stall everything:
```bash
# Configure the build system
cmake -S . -B cmake-build-agent-release -G Ninja

# Compute a job cap at 50% of logical cores (min 1)
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))

# Build the main applications (unreal-qt, unreal-mcp-bridge, etc.)
ninja -C cmake-build-agent-release -j "$JOBS"
```

### Why `build.sh` configures every time
Sources are collected with `file(GLOB ...)`, so CMake only learns about a new `.cpp`/`.h` when it runs
again. Ninja re-runs CMake by itself only when a `CMakeLists.txt` or `*.cmake` file changed — **not**
when a merge, rebase, `git pull` or another agent's landing brings in a new source file. Symptom: the
compile succeeds, the link fails with `symbol(s) not found` / `undefined reference` for a class that
plainly exists in the tree (seen with `Usart8251` / `TTDPit8253` after a rebase), or a new test file
is silently missing from `core-tests`.

`tools/build/build.sh` therefore runs `cmake -S . -B <build dir>` before every `ninja` (seconds, the
cache and your `-D` options are kept) and configures a missing build dir with `-G Ninja -DTESTS=ON`.
Its output goes to `<build dir>/configure.log` and is printed only if the configure fails.
`tools/build/test.sh` goes through `build.sh`, so it is covered too.

- Use `tools/build/build.sh` for every build; do not call `ninja` or `cmake --build` directly - that
  skips the re-configure and brings the symptom above back.
- After a rebase / merge / pull, nothing special is needed. If you must bypass the wrapper (benchmarks),
  run `cmake -S . -B <build dir>` yourself first.
- `UNREAL_NO_CONFIGURE=1 tools/build/build.sh ...` skips the step (only for a tight edit-compile loop on
  a tree that did not change under you).
- A new build dir needs no manual `cmake` call. Benchmarks (`-DBENCHMARKS=ON`) are the exception: one
  manual configure with that flag, later `build.sh` runs keep it.

### Linux (gcc) build check
CI builds on Linux with gcc, which is stricter than Apple clang (missing standard includes, deprecated
conversions). After touching C++ that could differ by compiler, reproduce it locally with the CI image,
natively on the host architecture: `docker/linux/build.sh --test` (see `docker/linux/README.md`).
Output lands in `scratch/linux-*`; delete it when done.

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
cmake -S . -B cmake-build-agent-release -G Ninja -DTESTS=ON

# Job cap at 50% of logical cores (min 1) — keep this below full core count so
# concurrent agent builds on this machine don't starve each other
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))

# Run all tests in parallel (automatically builds core-tests on demand)
cmake --build cmake-build-agent-release --target test-parallel -- -j "$JOBS"
# Or run tests sequentially:
ninja -C cmake-build-agent-release -j "$JOBS" core-tests && ./cmake-build-agent-release/bin/core-tests

# Run specific tests
./cmake-build-agent-release/bin/core-tests --gtest_filter="*TestName*"

# Configure with benchmarks enabled
cmake -S . -B cmake-build-agent-release -G Ninja -DBENCHMARKS=ON

# Build and run benchmarks
ninja -C cmake-build-agent-release -j "$JOBS" core-benchmarks
./cmake-build-agent-release/bin/core-benchmarks --benchmark_filter="*BenchName*"
```

### Default build vs test build — a stale-binary trap
**The default target (`ninja -C cmake-build-agent-release` with no target) NEVER builds or
updates `bin/core-tests` — even in a build dir configured with `-DTESTS=ON`.** The test
binary is outside the default `all` target on purpose (see the opt-in above). Consequences:

- After changing production code **or any header tests include** (`core/tests/_helpers/*`,
  anything pulled in by test sources), a plain `ninja` leaves `bin/core-tests` untouched.
- Running `./cmake-build-agent-release/bin/core-tests` then executes a **stale binary** and
  you test yesterday's code. This really happens: a header fix once looked "flaky" for an
  hour because every verification loop ran a two-hour-old `core-tests`.

Rules of thumb:

| You want | Command |
|---|---|
| Build / update the test binary | `tools/build/build.sh core-tests` (explicit target) |
| Build + run everything | `tools/build/test.sh` (builds `core-tests` itself, so never stale) |
| Run a filter after edits | rebuild via `core-tests` target **first**, then `--gtest_filter=...` |
| Production check only | plain `tools/build/build.sh` (no test binary update) |
| Tree changed under you (rebase, merge, pull) | nothing extra: `build.sh` / `test.sh` re-run CMake themselves |



### Parallel Test Execution (GTest Sharding)
The `test-parallel` CMake target uses GTest's built-in sharding to split tests across 4 processes:
- **Sequential:** ~37s at 76% CPU
- **Parallel (4-way):** ~12s at 250% CPU
- **Speedup:** ~3x on 4+ core machines

Manual sharding (useful for CI pipelines):
```bash
for i in 0 1 2 3; do
  GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=$i ./cmake-build-agent-release/bin/core-tests &
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
./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
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
- macOS: `./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt`
- Linux: `./cmake-build-agent-release/bin/unreal-qt`
- Windows: `./cmake-build-agent-release/bin/unreal-qt.exe`

**Available models (short names):** `PENTAGON`, `48K`, `128k`, `PLUS2`, `PLUS2A`, `PLUS3`, `TSL` (alias `TSCONF`), `SPRINTER`, `ATM3` (ZX-Evo), `ATM710`, `ATM450`, `PROFI` (v5, alias `PROFI5`), `PROFI3` (v3), `SCORPION`, `PROFSCORP`, `GMX`, `KAY`, `QUORUM`, `LSY256`, `PHOENIX`

**ZX-Poly configurations** (four synchronized instances of one base model, created by name like any model): `ZXPOLY-48K`, `ZXPOLY-128K`, `ZXPOLY-PENTAGON` — see [`.recipe/machines/zxpoly.md`](../.recipe/machines/zxpoly.md).

**Machine variants** (a base model with a fixed board, created by name like any model; Machine menu entry in unreal-qt): `TSL-VDAC2` (alias `TSCONF-VDAC2`: TS-Conf with the VDAC2 card, FT812 graphics on the IDE connector) — see [`.recipe/machines/tsconf-vdac2.md`](../.recipe/machines/tsconf-vdac2.md); `PROFI-PLUS` (alias `PROFIPLUS`: a Profi v5 with Djoni's V0.03 port decoder, `[PROFI] ExtPorts=v003`, running ROM BIOS Plus 0.41h1 `rom/profi/bios-plus-041h1.rom` - PQ-DOS and DOS Navigator; its 8255 parallel port and 8253 / 8251 serial port are emulated, `state profi` reports them) — see [`.recipe/machines/profi.md`](../.recipe/machines/profi.md). The table is `core/src/emulator/machinevariants.cpp`.

> Runtime-authoritative list: `GET /api/v1/emulator/models` — each entry carries a `creatable` flag. Creatable on `master`: `PENTAGON`, `48K`, `128k`, `PLUS2` (grey +2: 128K hardware, Amstrad ROM), `PLUS2A` (the +3 without its floppy controller), `PLUS3` (uPD765A floppy controller, see `docs/inprogress/2026-09-28-plus3-upd765/`), `ATM710`, `ATM3` (ZX-Evo; decoders landed with the ATM Turbo 2+/3 clone support, configs ship as `configs/atm710` + `configs/atm3`), `SCORPION`, `PROFSCORP`, `ATM450` (ATM Turbo 2 v4.50, 512K: boots the system ROM menu from `rom/atm1.rom`, see `.recipe/machines/atm.md` and `docs/inprogress/2026-10-01-atm450/`), `PROFI` (Profi v5, 1024K; IDE works: `[HDD] Scheme=PROFI`, slots `ide0.master` / `ide0.slave`, see `.recipe/machines/profi.md` and `docs/inprogress/2026-09-21-profi/`), `PROFI3` (Profi v3, 512K: Kramis BIOS, monochrome hi-res, no palette / extended ports / RTC / IDE; `docs/inprogress/2026-10-01-profi-v3-v5/`), `TSL` (TS-Conf, alias `TSCONF`: TS-BIOS from `rom/zxevo.rom`, TSU, DMA, SD slot `sd.zc`, `.spg` programs; see `.recipe/machines/tsconf.md` and `docs/inprogress/2026-09-27-tsconf/`), `SPRINTER` (Peters Plus Sprinter Sp2000: firmware 3.06 Hotfix 2 from `rom/sprinter/sp2k-3.06-hf2.rom` by default (owner decision 2026-10-03: back from 3.07 BETA 1, whose floppy driver changes IY so DSS 1.71.57 cannot start programs from a floppy, until the BIOS author publishes his fixed build) on its own Z84C15 CPU library, with the renderer, floppy, IDE (two channels), PS/2 keyboard and serial mouse: DSS 1.71 boots from a hard-disk image (needs BIOS 3.06+), DSS 1.62 from a 1.44 MB floppy (drive A; drive B on BIOS 3.04) or a hard disk; BIOS 3.04 (`sp2k-3.04.rom`, DSS 1.62 only) and 3.07 BETA 1 (`sp2k-3.07-beta1.rom`, `known_issues` warning) stay selectable; block accelerator (S5), AY + Covox-Blaster (S6), TTD (S7); BIOS selectable at create / runtime (`"sprinter":{"bios":"3.04"}`, `POST /sprinter/bios`); recipes `.recipe/machines/sprinter.md` (video modes, palettes, video RAM region `vram`, change log), `sprinter-accelerator.md`, `sprinter-sound.md`, `.recipe/media/sprinter-hdd.md`; see `docs/inprogress/2026-09-28-sprinter/`). NOT creatable (no port-decoder factory case yet): `GMX`, `KAY`, `QUORUM`, `LSY256`, `PHOENIX`, `NEXT` — a create request for them fails with HTTP 400 + reason, never a silent 48K fallback. Build fingerprint: `GET /api/v1/emulator/status` -> `server.git_branch`/`server.git_commit`. MCP clients get identical data: `emulator_manage` action `list_models` / action `server` (all automation modules serve the same information from the same source).

## macOS specifics (the development host)
- **The shell is zsh: unquoted variables are not word-split.** `F="a.md b.md"; git diff -- $F` passes
  ONE path, `a.md b.md`, and a check on a path that does not exist passes silently (this let a
  master landing skip its "no foreign edits" check). Pass path lists as an array
  (`F=(a.md b.md); git diff -- $F`) or from a file (`git diff -- $(cat files.txt)`).
- **BSD tools:** `sed -i ''` (the empty backup suffix is required), `sysctl -n hw.ncpu` for the core
  count, `sysctl -n vm.loadavg` for the load (`{ 1min 5min 15min }`).

## Agent Rules & Guidelines
- **Test Artifacts**: ALL test artifacts and temporary files (e.g. `.wav`, `.trd`, `.sna`) MUST be written to the `scratch/` directory. Do not clutter the project root. Use `TestPathHelper::GetTestScratchPath()` for this.
- **Naming Conventions**: Do not use underscores in file names or C++ class/struct/method names. Use PascalCase for methods and camelCase for variables/fields. **Exception**: Test files use `*_test.cpp` suffix and test classes use `ClassName_Test` pattern.
- **AI Agent Recipes**: Operational recipes for driving the emulator via MCP / WebAPI (media loading, TTD, port tracing, memory profiling, autostart, TR-DOS) live in `/.recipe/` (`/.recipe/README.md`). Always check `/.recipe/` before constructing automation workflows.
- **Testing**: See `core/tests/README.md` for test patterns (CUT pattern, fixtures, helpers).
- **Documentation Rules**: Documentation files must use lowercase with hyphens (kebab-case). Ongoing design and analysis must go into `docs/inprogress/` following specific date-prefixed directory naming rules. See `docs/inprogress/README.md` for details.
- **Coding Guidelines**: For detailed coding guidelines, see `docs/guidelines/coding-guidelines.md`.
- **Performance**: A change to a hot path (per instruction, per memory or port access) must cost nothing for machines that do not use it and needs an A/B benchmark; patterns (combined gate, interface selection, templates) and the measurement procedure: `docs/guidelines/performance-guidelines.md`.
- **Cross-Platform & Compatibility**: The codebase MUST be cross-platform (Windows, macOS, Linux) and cross-compiler compatible (gcc, clang, mingw, msvc) with **ZERO warnings** allowed. See `docs/guidelines/cross-platform-compatibility.md` for environmental constraints.
