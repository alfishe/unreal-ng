# Unreal-NG Agent Rules

> **CRITICAL**
> - **NEVER commit without explicit user request** (each permission is one-time only)
> - **All builds**: `tools/build/build.sh` or `tools/build/build.sh <target>`
> - **All tests**: `tools/build/test.sh` or `tools/build/test.sh --gtest_filter='*Foo*'`
> - **Check slot status**: `tools/build/slot.sh --status`
>
> The wrappers handle everything: job limits, queuing, priority, CMake re-configure.
> Run as a **background command** (builds can queue and take minutes).
> Do NOT call `ninja` / `cmake --build` directly. Details: [`tools/build/README.md`](tools/build/README.md).

## Before any commit

| What changed | Required checks |
|--------------|-----------------|
| `core/` (engine, automation, tests) | `tools/build/build.sh` (zero warnings) + `tools/build/test.sh` |
| Only Qt apps (`unreal-qt/`, `unreal-*-viewer/`, `unreal-videowall/`) | `tools/build/build.sh` (zero warnings). No `core-tests` needed. |
| Documentation only (`docs/`, `.recipe/`, `*.md`) | `python3 tools/fix-absolute-paths.py --path <files>` (no build) |
| Only `tools/` scripts | Run the script itself (its tests or a dry run) |
| Mixed or other | Proportional checks; state what you ran |

Report results. Wait for explicit "commit" instruction.

## Project Structure

| Directory | Description |
|-----------|-------------|
| `core/` | Core emulator engine (CPU, memory, I/O). Decoupled from GUI. |
| `core/automation/` | Automation interfaces (CLI, Python, Lua, WebAPI). |
| `core/benchmarks/` | Performance benchmarks (Google Benchmark). |
| `core/tests/` | Unit and integration tests (GTest). |
| `unreal-qt/` | Qt desktop UI (Debugger, Memory Viewer, Screen Viewer). |
| `unreal-screen-viewer/` | Standalone Screen Viewer. |
| `unreal-videowall/` | Standalone Video Wall. |
| `scratch/` | Git-ignored. ALL test artifacts go here. |
| `docs/` | Documentation. `docs/inprogress/` for active design work. |
| `.recipe/` | AI agent recipes (MCP/WebAPI automation). |
| `tools/` | Build scripts, verification tools. `tools/poc/` for experiments. |
| `testdata/` | Test fixtures, disk images, ROMs. |
| `lib/` | Third-party dependencies (GTest, Google Benchmark). |

## Building

Always use `tools/build/build.sh`. It:
- Re-runs CMake configure (catches new files from merge/rebase)
- Applies job limits and priority
- Queues if slots are busy

```bash
tools/build/build.sh                # full build
tools/build/build.sh core-tests     # single target
```

Benchmarks need one manual configure with `-DBENCHMARKS=ON`, then `build.sh` keeps it.

### Linux (gcc) check

CI uses gcc (stricter than clang). After C++ changes: `docker/linux/build.sh --test`.

## Writing Tests

Full guide: [`core/tests/README.md`](core/tests/README.md). Non-negotiables:

- **Never `sleep_for`** - use `TestWait::For` / `ForAtLeast` / `ForExactly`
- **Under 50 ms per test** (boot-bound tests need a justifying comment)
- **Stop looping once assertion passes** - keep diagnostics on failure path
- **`EnableTurboMode()`** on boot-bound tests (not when asserting on pixels)
- **Scratch files**: `TestPathHelper::GetUniqueTestScratchPath()`
- **Avoid `vector::resize(n,0)`** on large buffers - use `ZeroInitBuffer`

## Running Tests

```bash
tools/build/test.sh                         # build + run all
tools/build/test.sh --gtest_filter='*Foo*'  # build + run filtered
```

**Stale binary trap**: plain `build.sh` does NOT update `core-tests`. After editing test code, either:
- Use `tools/build/build.sh core-tests` explicitly, or
- Use `tools/build/test.sh` (it builds `core-tests` automatically)

## WebAPI Testing

See [`.recipe/testing/webapi-verification.md`](.recipe/testing/webapi-verification.md).

Models list: `GET /api/v1/emulator/models`. Per-machine recipes: [`.recipe/machines/`](.recipe/machines/).

## macOS specifics

- **zsh does not word-split**: `F="a.md b.md"; git diff -- $F` passes ONE path. Use arrays: `F=(a.md b.md); git diff -- $F`
- **BSD sed**: `sed -i ''` (empty backup suffix required)
- **Core count**: `sysctl -n hw.ncpu`

## Guidelines

- **Artifacts**: Write to `scratch/`, not project root
- **Naming**: PascalCase methods, camelCase fields. No underscores except `*_test.cpp` / `ClassName_Test`
- **Recipes**: Check `.recipe/` before building automation workflows
- **Performance**: Hot-path changes need A/B benchmarks. See `docs/guidelines/performance-guidelines.md`
- **Cross-platform**: Zero warnings on gcc/clang/mingw/msvc. See `docs/guidelines/cross-platform-compatibility.md`
- **Coding**: See `docs/guidelines/coding-guidelines.md`
- **Environment variables**: all of them in [`docs/emulator/environment-variables.md`](../docs/emulator/environment-variables.md); a new variable gets a row there
