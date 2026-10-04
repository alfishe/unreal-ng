# TODO: debugger snapshot and binary memory reads (D7 + E3)

Design: [tdd.md](tdd.md). Branch `debugger-snapshot` (worktree `scratch/wt-snapshot`), from master `98643fd3b`.
Each phase ends green (full build with zero warnings, core-tests) before the next one starts.

| Phase | Work | Status |
|---|---|---|
| S0 | Design (this folder) | done 2026-10-04 |
| S1 | Core `MemoryRead::Bytes` (spaces from D8's `MemorySearch`); `MemoryRead_Test` | done 2026-10-04 (`d1ff22893`) |
| S2 | `format=binary` on the five WebAPI memory reads, the `X-Unreal-*` headers, the 16-bit `length` fix; OpenAPI; binary-vs-JSON tests | done 2026-10-04 (`31a018860`): live-checked with curl (binary == JSON bytes, wrap, pages); the region read had binary already and gains the headers |
| S3 | Lua / Python `mem_read_bytes`, CLI `memory save --space`; docs of each surface | done 2026-10-04 (`cdfc798d0`); `memory save <space>:<addr>:<len> <file>` instead of a `--space` flag |
| S4 | Core `DebugSnapshot::Registers` / `Disasm`; `GET /registers` and `/disasm` switched to them with a golden JSON test | done 2026-10-04 (`a0f98f0af`): /registers and /disasm JSON byte-identical before / after (live golden capture) |
| S5 | `seq` and `prev_regs` in `Emulator` (stops: confirmed park, end of a direct run); tests | done 2026-10-04 (`b4d6d9601`): also the last stop's reason (`pause`) |
| S6 | `MainLoop::RunAtFrameBoundary`; `DebugSnapshot::Build` (paused / frame paths, limits); `DebugSnapshot_Test` | done 2026-10-04 (`8cf620fe7`): mutation check (no frame tasks -> the running test fails) |
| S7 | `GET /debug/snapshot` + OpenAPI; MCP `inspect_state` `snapshot`; Lua / Python `debug_snapshot`; CLI `snapshot`; tests per surface | done 2026-10-04: the CLI command is `debug-snapshot` (`snapshot` is the existing .sna command); live-checked over WebAPI and CLI; MCP has a unit test; Lua / Python are compile-checked only (core-tests has no Lua / Python harness; the core they call is tested) |
| S8 | Docs: interface docs, MCP tool text, recipe `.recipe/analysis/debugger-snapshot.md`, gap analysis rows D7 / E3, PLAN.md | done 2026-10-04: interface docs, MCP text, recipe, gap analysis D7 / E3, PLAN #49 |
| S9 | Merge master, full build + tests, land on master, push | master merged (`893cd2d6e`), full build 0 warnings, core-tests green; landing waits for the owner |
