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
| `core/automation/mcp/` (or a WebAPI endpoint an MCP tool forwards to) | the above + `tools/verification/mcp/run_all.py --launch <unreal-qt>` ([MCP testing](docs/guidelines/mcp-testing-guidelines.md)) |
| Only Qt apps (`unreal-qt/`, `unreal-*-viewer/`, `unreal-videowall/`) | `tools/build/build.sh` (zero warnings). No `core-tests` needed. |
| Documentation only (`docs/`, `.recipe/`, `*.md`) | `python3 tools/fix-absolute-paths.py --path <files>` (no build) |
| Only `tools/`, `plugins/` or `utilities/` scripts | Run the script itself (its tests or a dry run) |
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
| `tools/` | Internal: build scripts, verification tools. `tools/poc/` for numbered POCs (index in `tools/poc/README.md`). |
| `plugins/` | Extensions loaded by other programs (Wireshark extcap, ImHex patterns, IDA script). |
| `utilities/` | Stand-alone user programs and scripts (disk image tools, port trace, monitors, NeoGS media). |
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

## Landing on master

Only your own commits, only from a worktree (`scratch/wt-<name>`), never a branch switched in the main checkout.
`tools/build/land.sh` does it: compare-and-swap on master, the main checkout follows through a private index (other
sessions' uncommitted edits stay byte for byte), `--push` publishes exactly the landed SHA.

```bash
git fetch github master && git merge --no-edit github/master   # never merge local master: it may hold others' commits
tools/build/build.sh && tools/build/land.sh --push "$(git rev-parse master)"
tools/build/land.sh --publish-only                              # push HEAD without moving local master
```

It refuses when a push would publish another session's unpushed commits. Do not push `master` by hand, do not
merge or land other sessions' branches. Details: [`tools/build/README.md`](tools/build/README.md#landing-on-master).

## Shared machines (scratch, scratchpads, remote hosts)

Several sessions and their agents work on the same machines at once; nothing outside your own worktree is yours.

- **Your files live in your worktree.** Scripts, patches, bundles and logs go in `scratch/wt-<name>/scratch/`,
  named after the task. A session scratchpad or `/tmp` may be shared with other sessions: never keep a script there
  that you will run again.
- **Run only what you wrote for this task.** Before running a script, read it; a file with a familiar name in a shared
  place may be another session's and may act on its worktree (a `sync.sh` once reset another agent's remote worktree).
- **On a remote host** (build hosts, the Windows host) touch only the worktree, branch and build folder you created
  there, named after your task (`scratch/wt-<name>`); never reset, sync, delete or reuse another one, even if it looks
  stale. Remove yours when done; leave the host's master fast-forward only.
- **Something of yours vanished or changed**: stop, report it, do not "fix" it by overwriting others' state.

## Writing Tests

Full guide: [`core/tests/README.md`](core/tests/README.md). Non-negotiables:

- **Never `sleep_for`** - use `TestWait::For` / `ForAtLeast` / `ForExactly`
- **Under 50 ms per test** (boot-bound tests need a justifying comment)
- **Stop looping once assertion passes** - keep diagnostics on failure path
- **`EnableTurboMode()`** on boot-bound tests (not when asserting on pixels)
- **Scratch files**: `TestPathHelper::GetUniqueTestScratchPath()`
- **Avoid `vector::resize(n,0)`** on large buffers - use `ZeroInitBuffer`
- **Second thread in a test**: wait for it to start, bound the loop by both sides, assert the race happened ([concurrency tests](core/tests/README.md#concurrency-tests))

## Running Tests

```bash
tools/build/test.sh                         # build + run all
tools/build/test.sh --gtest_filter='*Foo*'  # build + run filtered
```

**Stale binary trap**: plain `build.sh` does NOT update `core-tests`. After editing test code, either:
- Use `tools/build/build.sh core-tests` explicitly, or
- Use `tools/build/test.sh` (it builds `core-tests` automatically)

## Memory, thread and lifetime bugs

Use-after-free, heap corruption, races, deadlocks, leaks: [`docs/testing/sanitizers.md`](docs/testing/sanitizers.md)
(ASan + UBSan / TSan builds via `tools/sanitize/configure.sh`, the WebAPI stress `tools/sanitize/stress_round.sh`,
`leaks`, hang sampling, crash reports via `tools/sanitize/ips_stacks.py`). A crash in `free`/`malloc` is heap damage
done earlier: reproduce under ASan, do not patch the crashing frame.

## MCP Testing

How and when: [`docs/guidelines/mcp-testing-guidelines.md`](docs/guidelines/mcp-testing-guidelines.md). Unit tests
`--gtest_filter='*Mcp*'`; live scripts per block of tools in [`tools/verification/mcp/`](tools/verification/mcp/README.md)
(`run_all.py --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt`, own ports, ~5 s). An
agent may see only the text of an answer: check `content[]` text, not just `structuredContent`.

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
- **Automation planes**: every capability on CLI, Python, Lua, MCP, REST and WebSocket; docs chain `command-interface.md` -> module doc -> plane docs; WebSocket adds to REST (every route is a socket `call`, changes are topics), async I/O, no threads per client. See `docs/guidelines/automation-planes-guidelines.md`
- **New platforms**: buses, slots, cards and media go through their managers, no ad hoc integration. See `docs/guidelines/new-platform-guidelines.md`
- **Cross-platform**: Zero warnings on gcc/clang/mingw/msvc. See `docs/guidelines/cross-platform-compatibility.md`
- **Coding**: See `docs/guidelines/coding-guidelines.md`
- **POCs**: every POC in `tools/poc/NNN-name/`, registered in the index of `tools/poc/README.md` (next free number), charter (goals, requirements, constraints, acceptance criteria) before measuring. See `docs/guidelines/poc-guidelines.md`
- **Environment variables**: all of them in [`docs/emulator/environment-variables.md`](docs/emulator/environment-variables.md); a new variable gets a row there
