# unreal-debugger - the Qt debugger

eCat3's debugger window (Mikhail Revzin, <https://github.com/Ptr314/ecat3>, GPL-3.0) over unreal-ng: one window for
the machine, a tab per processor (the main Z80, a General Sound card's Z80: its step buttons step the card's CPU, in
this process and over the WebAPI's `?cpu=gs`), dumps of every address space and memory
page, stack, breakpoints, history and a Devices tab with every report the machine has (the device catalog,
`GET /state/devices`). Ported and measured in [POC 041](../../tools/poc/041-ecat-debugger/README.md); the port is ours
and is adapted freely.

## Layout

| Folder | What |
|--------|------|
| `ecat/` | the window and its views, from ecat3 `dev` @ 27a98108 (`src/dialogs/debugger/`, `emulator/disasm_buffer.*`), plus our own paged-memory tab and bank strip |
| `shimapi/ecatshim.h` | the part of ecat's core API the views call (Emulator, CPU, DisAsm, devices), declared over nothing |
| `shim/` | ecat's header names (`emulator/core.h`, ...) standing for that API |
| `adapter/` | the API over unreal-ng: `unrealmachine.cpp` (the core in this process), `remotemachine.cpp` (a running emulator through its WebAPI) |
| `guiplane/` | the window on the GUI control plane: `debugger`, `disassembly`, `memory` (views' side, no core header) |
| `main.cpp` | the program: options, the screen window, the checks |

The views and the adapter never share a translation unit: ecat's Emulator / CPU / Memory (namespace `ecatshim`) and
the core's classes of the same names would clash.

## Time travel

While the machine has a recorded history (TTD), the toolbar shows a time group after the forward steps, the same as
unreal-qt's Embedded debugger ([TTD branches: debuggers](../../docs/inprogress/2026-11-08-ttd-branches/debuggers.md)):

| Button | Key | Verb |
|--------|-----|------|
| Step Back | Shift+F7 | `step-instruction {dir: back}` |
| Step Over Back | Shift+F8 | `reverse-step-over` |
| Run Back | Shift+F9 | `reverse-continue {pcs}`: the main CPU's breakpoints |
| Frame Back | - | `step-back` |
| Branch Here / Continue Rec | - | `seek` to the end when needed, then `resume` |

The state label adds the moment (`Recording`, `History: frame 120 of 400, replaying`, `Live, not recorded`, the
branch and `fork here: N other futures`); with branches a drop-down lists them (the active lineage first) with Switch,
Rename, Tags, Note, Pin, Delete and Save Lineage. Nothing records unless asked: only Branch Here / Continue Rec.
Every action is a TTDControl verb through `Emulator::ttd_call` (`ecat/dialogs/debugger/ttdcontrols.cpp`): in process
directly, over the WebAPI as the `/ttd/...` routes (`GET` for `status` / `position` / `branches`, the branch routes
for switch / update / delete, `POST /ttd/<verb>` for the rest).

## Run

```text
unreal-debugger [--model PENTAGON] [--card neogs|multisound | --no-gs] [file.sna|.z80|.tap|.trd|...]
unreal-debugger --connect 127.0.0.1:8090 [--emulator id]   a running emulator (unreal-qt) through its WebAPI
unreal-debugger --zoom 1.5                                  the window's scale (Ctrl+= / Ctrl+- / Ctrl+0)
unreal-debugger --self-check                                the debugger API and the window's editors; exit 2 on a failure
unreal-debugger --screenshot out.png                        stop, step, breakpoint; saves the window and exits
unreal-debugger --list-memory                               the dump tabs and the "+" menu the window builds
unreal-debugger --measure 10                                frames/s and CPU: screen only, debug mode, window
```

Run from the build's `bin/` (the core finds `data/` there; in the macOS package, `UnrealNG.app/Contents/Resources`).

The window is on the GUI control plane as program `unreal-debugger` (`unreal-gui list`, `unreal-gui memory.goto 0x6000`):
the same `debugger`, `disassembly` and `memory` commands as unreal-qt's debugger, on the listing and the dump of the
processor in front. `UNREAL_GUI_CONTROL_DIR` and `UNREAL_GUI_CONTROL_PORT` as for every GUI program; see
[the GUI control plane](../../docs/emulator/design/gui-control/README.md#6-interfaces). Check:
`python3 tools/guicontrol/verify_debugger.py`.
