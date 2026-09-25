# 018 - TUI Debuggers (classic + TSConf fork)

Standalone recreations of the UnrealSpeccy debugger UIs as cross-platform C++20
terminal applications, driven entirely through a swappable backend interface:
stubbed golden data today, live emulator integration through WebAPI already
working.

Two independent projects (no shared files, each with its own vendored libs):

| App | Layout | Spec | Binary |
|-----|--------|------|--------|
| [classic/](classic/) | 80x30, TDD-DBG-01 | [TDD-DBG-01](../../../docs/inprogress/2026-09-24-tui-debugger/TDD-DBG-01_unreal-speccy-debugger-tui.md) | `dbgclassic` |
| [tsconf/](tsconf/) | 157x30 fork, TDD-DBG-02 | [TDD-DBG-02](../../../docs/inprogress/2026-09-24-tui-debugger/TDD-DBG-02_unreal-tsconf-debugger-tui.md) | `dbgtsconf` |

## Architecture

```
painters (panels/*.cpp)          -- paint chars+attrs into a cell grid
        |
        v
TextScreen (screen/textscreen.*) -- 80x30 / 157x30 cell grid, CP437 charset
        |
        v
frame-render (tui/)              -- grid -> FTXUI Element (UTF-8 + 16-color palette)
        |
        v
FTXUI loop (main.cpp)            -- key/mouse dispatch, repaint on change

IDebuggerBackend (backend/debugger-backend.h)   -- THE TRANSPORT SEAM
        |
        +-- MockBackend  - vendored z80ex machine + golden sample test set
        +-- RestBackend  - live unreal-ng WebAPI client (http-client + json-mini)
        +-- WebSocket / Ipc - planned transports (factory stubs)
```

Every value the screen can show and every command the UI issues flows through
`IDebuggerBackend`; swapping the transport means constructing a different
backend instance, nothing in the UI layer changes. Fork additions
(`GetPcHistory`, `GetTsConf`) ship with default no-op implementations, so
existing backends compile untouched and degrade gracefully (a plain 128K
machine has no TSConf block - the board band stays blank).

## Backend status

| Capability | mock | rest (live WebAPI) |
|------------|------|--------------------|
| regs / mem read+write / disasm | yes | yes (verified) |
| step / run-until-break / breakpoints | yes | yes (verified) |
| conditional bp expressions, assemble, find, labels | yes (backend-level) | partial (endpoint gaps documented in `rest-backend.cpp`) |
| PC history / TSConf registers | yes (M1-fetch ring / seed) | not yet (needs server endpoints) |

Verified against a live emulator instance: instance lifecycle, memory and
register round-trips, disassembly, 3x step, exact run-to landing, breakpoint at
batch boundary (see `scratch/poc018-rest-smoke.cpp`).

## Build and run

```bash
cd tools/poc/018-tui-debuggers/classic   # or tsconf
cmake -S . -B build -G Ninja
ninja -C build
./build/dbgclassic                       # mock backend, needs >=80x30 terminal
./build/dbgclassic --backend rest        # live emulator at http://localhost:8090
./build/dbgclassic --backend rest --endpoint http://host:8090
```

Zero warnings on gcc/clang/msvc (`-Wall -Wextra -Wpedantic` / `/W4`).
Vendored: z80ex 1.1.21 (GPLv2), FTXUI 7.1.0 (MIT) in each project's `lib/`.

## Results

- tsconf fork band (PC history + TSConf register board + banks window) is
  **cell-exact against the oracle dumps** - chars and attrs - enforced by
  `tsconf/tests/test-main.cpp` against `tsconf/tests/golden/`.
- Both apps verified interactively under a PTY on mock and REST backends
  (run/step/cursor/bpx/continue/focus/mouse all repaint correctly).
- Oracle: `docs/inprogress/2026-09-24-tui-debugger/unreal_dbg_render.py`.

## Known v0 limitations

- Classic-band painters (regs/trace/mem/side in the 80-col area) are
  v0-approximate: geometry matches, attributes are not yet golden-exact
  (the pending "panels" milestone).
- Bare `r` (run) over REST blocks while it streams step batches up to the
  50M-tick cap; prefer breakpoints / run-to in that mode.
- `src/ui/*` subsystem files (modals, dialogs, bp/labels UI) are scaffolded
  stubs; the underlying capabilities already exist at the backend seam.
