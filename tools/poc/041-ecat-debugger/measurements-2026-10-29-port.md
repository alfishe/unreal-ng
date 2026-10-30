# Measurements 2026-10-29: the port (goals 1-3)

## Setup

- macOS 15 (Darwin 24.6), Apple Silicon, Qt 6 Widgets, release build `cmake-build-agent-release`, target
  `ecat-debugger-poc` (`-DBUILD_ECAT_DEBUGGER_POC=ON`).
- eCat3 branch `dev` at `27a98108`, worktree `/Volumes/TB4-4Tb/Projects/emulators/github/ecat3-dev`.
- Copied as they are into `cpp/ecat/`: `dialogs/debugger/*` (celleditor, debuggerwindow, debugstyle, debugutil,
  disasmview, memoryview, memorywindow, registersview, sideviews), `dialogs/numfmt.h`, `dialogs/genericdbgwnd.*`,
  `emulator/disasm_buffer.*`. The icons used by the window are aliased in `cpp/resources/ecat.qrc`.
- The shim (`cpp/shimapi/ecatshim.h`, `cpp/shim/emulator/*.h`) declares the part of ecat's `Emulator` / `CPU` /
  `DisAsm` / `AddressableDevice` / `MemoryMapper` / `DeviceManager` API the views call, in namespace `ecatshim`;
  the adapter (`cpp/adapter/unrealmachine.cpp`) implements it over the core. The two never share a translation
  unit (unreal-ng's `Emulator` and `Memory` would clash with ecat's).
- Raw data: `scratch/2026-10-29-poc-041/` (build logs, self-check output, screenshots, profiles).

## Lines changed in the ported views (goal 1)

`git diff --no-index --numstat` of every ported file against `ecat3-dev/src`:

| File | Upstream lines | Added | Removed |
|------|---------------:|------:|--------:|
| `dialogs/debugger/debuggerwindow.cpp` | 523 | 1 | 1 |
| `dialogs/debugger/disasmview.cpp` | 568 | 63 | 11 |
| `dialogs/debugger/disasmview.h` | 107 | 10 | 0 |
| `dialogs/debugger/memoryview.cpp` | 730 | 81 | 8 |
| `dialogs/debugger/memoryview.h` | 165 | 8 | 0 |
| `dialogs/debugger/registersview.cpp` | 294 | 69 | 6 |
| `dialogs/debugger/registersview.h` | 95 | 12 | 0 |
| `dialogs/debugger/sideviews.cpp` | 346 | 34 | 2 |
| `dialogs/debugger/sideviews.h` | 102 | 10 | 1 |
| the other 13 files | | 0 | 0 |
| **all 22 files** | **3 865** | **288** | **29** |

7.5 % of the upstream lines. The views compiled and ran over the shim with **zero** edits; every one of the 288
lines is the partial repaint the owner asked for on 2026-10-29 ("full repaints must not happen, especially not by
the timer"), marked `POC 041` in the code:

- each view (listing, registers, flags, memory dump, stack) keeps what every row / cell shows and repaints only the
  rows and cells that differ; nothing when nothing differs; a hidden view reads nothing;
- the listing repaints its arrow strip only when the arrows change;
- the views paint every pixel (`WA_OpaquePaintEvent`), so Qt no longer repaints their parents beneath them;
- paint loops skip the rows outside the region asked for;
- `FlagsView::refresh` asks for a new layout only when the set of flags changes: the per-tick `updateGeometry()`
  made both tab widgets above it repaint whole every 200 ms;
- the one direct full repaint in `debuggerwindow.cpp` (after "clear breakpoints") goes through the same diff.

## Features (goals 2, 3)

`ecat-debugger-poc --self-check` drives the window's API (the calls the toolbar and the views make) and checks the
result. Pentagon with NeoGS in `zxbus.1`: 20 of 20; 48K and 128K: 16 of 16 (the 4 GS checks skipped: these models have
no ZX-bus slot).

| Check | Pentagon + GS | 48K | 128K |
|-------|:---:|:---:|:---:|
| pause | ok | ok | ok |
| step (R moved) | ok | ok | ok |
| register edit (HL) | ok | ok | ok |
| flag edit (C) | ok | ok | ok |
| memory edit in the CPU's space (#C000) | ok | ok | ok |
| page map: the owner of #C000 holds its byte | ok | ok | ok |
| history (PC history armed) | ok | ok | ok |
| run to cursor | ok | ok | ok |
| run-to breakpoint gone after the stop | ok | ok | ok |
| breakpoint add at #0038 (IM 1 handler) | ok | ok | ok |
| breakpoint hit at #0038 | ok | ok | ok |
| breakpoint remove | ok | ok | ok |
| step out of the interrupt handler | ok | ok | ok |
| step over a CALL (CALL #C010 at #C000, PC lands at #C003) | ok | ok | ok |
| GS registers and memory | ok | - | - |
| GS memory edit (#4100, card RAM) | ok | - | - |
| GS register edit (DE) | ok | - | - |
| GS breakpoint stops the machine, GS tab in front | ok | - | - |
| devices tab has fields (more than 20) | ok | ok | ok |
| run again | ok | ok | ok |

Seen in the window (screenshots `window-pentagon.png`, `window-pentagon-gs.png`, and in scratch for 48K, 128K with a
`.z80` and Pentagon autostarting a `.trd`):

- the listing follows PC, with jump arrows in lanes, PC and breakpoint marks, the address, the bytes, and colour by
  flow. unreal-ng's labels appear in the operands (`JP CALL_ROM1_CONT (#005C)`, `LD hl,SWAP (#5B00)`);
- registers in hex and binary, changed values red, flags line `- Z 5 H - P - -`;
- memory tabs: `cpu`, `gs`, `mapper` (the Z80's 64 KB as its page map, the owner `rom + 8000` in the status line),
  `ram`, `rom`, `neogs.ram`, `neogs.flash`;
- the GS Z80 as the second processor tab `gs : Z80 (General Sound)`: the NeoGS firmware listing with arrows, its
  registers, its stack (SP #4400) in the side tab;
- Stack, Breakpoints, History, Devices side tabs (devices: execution, slots, ay, fdc, gs-card, flattened from the
  core's `DeviceState` reports).

Not covered by automation: an instruction's bytes edited in place in the listing, the dump's search / load / save,
the devices tab seen on screen (the screenshot's tab switch did not take; its fields are checked through the API).
