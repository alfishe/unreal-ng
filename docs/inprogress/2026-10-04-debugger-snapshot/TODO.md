# TODO: debugger snapshot and binary memory reads (D7 + E3)

Design: [tdd.md](tdd.md). Branch `debugger-snapshot` (worktree `scratch/wt-snapshot`), from master `98643fd3b`.
Each phase ends green (full build with zero warnings, core-tests) before the next one starts.

| Phase | Work | Status |
|---|---|---|
| S0 | Design (this folder) | done 2026-10-04 |
| S1 | Core `MemoryRead::Bytes` (spaces from D8's `MemorySearch`); `MemoryRead_Test` | open |
| S2 | `format=binary` on the five WebAPI memory reads, the `X-Unreal-*` headers, the 16-bit `length` fix; OpenAPI; binary-vs-JSON tests | open |
| S3 | Lua / Python `mem_read_bytes`, CLI `memory save --space`; docs of each surface | open |
| S4 | Core `DebugSnapshot::Registers` / `Disasm`; `GET /registers` and `/disasm` switched to them with a golden JSON test | open |
| S5 | `seq` and `prev_regs` in `Emulator` (stops: confirmed park, end of a direct run); tests | open |
| S6 | `MainLoop::RunAtFrameBoundary`; `DebugSnapshot::Build` (paused / frame paths, limits); `DebugSnapshot_Test` | open |
| S7 | `GET /debug/snapshot` + OpenAPI; MCP `inspect_state` `snapshot`; Lua / Python `debug_snapshot`; CLI `snapshot`; tests per surface | open |
| S8 | Docs: interface docs, MCP tool text, recipe `.recipe/analysis/debugger-snapshot.md`, gap analysis rows D7 / E3, PLAN.md | open |
| S9 | Merge master, full build + tests, land on master, push | open |
