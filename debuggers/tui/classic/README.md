# unreal-tui-debugger - the classic UnrealSpeccy debugger (80x30)

C++20/FTXUI recreation of the classic 80x30 debugger screen (TDD-DBG-01) with a swappable `IDebuggerBackend` seam,
over a running unreal-ng through its WebAPI (the WebSocket `/api/v1/ws` by default, REST on request) or over the mock
machine of its tests (vendored z80ex + the golden sample set). Started as POC 018a
([tools/poc/018-tui-debuggers](../../../tools/poc/018-tui-debuggers/README.md)); the build, the sources it shares with
the TS-Conf fork and the transports: [../../README.md](../../README.md).

## Build and run

```bash
tools/build/build.sh unreal-tui-debugger tui-debugger-tests
cmake-build-agent-release/bin/unreal-tui-debugger                 # ws://localhost:8090, terminal >= 80x30
cmake-build-agent-release/bin/unreal-tui-debugger --backend rest  # the same machine through REST requests
cmake-build-agent-release/bin/unreal-tui-debugger --backend mock  # the built-in machine, no emulator
cmake-build-agent-release/bin/unreal-tui-debugger-tests
```

`--endpoint <url>` points at another emulator (`ws://host:port`, `http://host:port` for `rest`). The debugger attaches
to the emulator's first instance (or creates a 128K one) and pauses it; an emulator that does not answer is reported
at once.

## Keys

UnrealSpeccy's: F7 step, F8 step over, F11 exit subroutine, F4 run to cursor, Esc continue / pause, Space breakpoint,
G go to, Home cursor to PC, Z PC to cursor, Tab next window, Alt+F4 exit; F10 opens the system menu with every command.
The full table: [../../README.md](../../README.md#run).

## Source map

| Area | Files | Status |
|------|-------|--------|
| Data model | `src/model/model.h`, `common/src/model/hexfmt.cpp` | done |
| Screen model | `src/screen/` (TextScreen, palette, CP437 glyphs) | done |
| Backend seam | `src/backend/debugger-backend.h` | done |
| Mock backend | `mock-backend.cpp`, `sample-state.*` | done - z80ex exec, bp, conditions, ripper |
| Disasm (Unreal format) | `disasm-unreal.cpp` | done - oracle-exact |
| Assembler subset | `asm-subset.cpp` | done - 26+4 dialog cases verified |
| Condition expr lang | `expr-lang.cpp` | done - compile/eval/decompile |
| WebAPI backend | `common/src/backend/webapi-backend.cpp` over `ws-client.cpp` (default) or `http-client.cpp` | done - verified vs live emulator |
| Painters | `src/panels/regs/trace/side.cpp` | v0 - geometry exact, attrs approximate |
| Frame render / loop | `src/tui/frame-render.cpp`, `src/main.cpp` | done |
| UI subsystems | `src/ui/*.cpp`, `panels/memory|preview.cpp` | scaffolded stubs (pending milestone) |

Oracle for the golden sample and screen layout:
`docs/inprogress/2026-09-24-tui-debugger/unreal_dbg_render.py`.
Files under `src/` are this app's own; the rest (screen, backend, panels, ui, tui) live in `../common/src/`.

## Tests

`tests/test-main.cpp`: widget traversal, focus backgrounds, read-only widgets, field navigation, editing and the
mouse over the mock backend (`unreal-tui-debugger-tests`, run by CI on every platform). The golden-exact panel compare
of the classic band is still to come; the TS-Conf fork (`../tsconf/`) carries the pattern against the same oracle.

## Next steps

1. Golden-exact attributes for the classic band (regs/trace/mem/side).
2. Wire `src/ui/` modals + full section-9 key map onto the existing backend
   capabilities (expr/asm/find are already implemented at the seam).
3. Stops pushed by the emulator: subscribe to the `debug.events` topic on the same socket instead of polling.
