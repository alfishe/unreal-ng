# unreal-debugger - the Qt debugger

eCat3's debugger window (Mikhail Revzin, <https://github.com/Ptr314/ecat3>, GPL-3.0) over unreal-ng: one window for
the machine, a tab per processor (the main Z80, a General Sound card's Z80: its step buttons step the card's CPU, in
this process and over the WebAPI's `?cpu=gs`), dumps of every address space and memory
page, stack, breakpoints, history and a Devices tab with every report the machine has (the device catalog,
`GET /state/devices`); on a ZX Spectrum Next also NextREG, MMU, Copper, Sprites, DMA and CTC / IM2 tabs, each a catalog report
read whole (fields, its list as a table, values changed since the last stop in the changed colour). Ported and measured in [POC 041](../../tools/poc/041-ecat-debugger/README.md); the port is ours
and is adapted freely.

## Layout

| Folder | What |
|--------|------|
| `ecat/` | the window and its views, from ecat3 `dev` @ 27a98108 (`src/dialogs/debugger/`, `emulator/disasm_buffer.*`), plus our own paged-memory tab, bank strip and Next tabs (`nextviews.*`) |
| `shimapi/ecatshim.h` | the part of ecat's core API the views call (Emulator, CPU, DisAsm, devices), declared over nothing |
| `shim/` | ecat's header names (`emulator/core.h`, ...) standing for that API |
| `adapter/` | the API over unreal-ng: `unrealmachine.cpp` (the core in this process), `remotemachine.cpp` (a running emulator through its WebAPI) |
| `guiplane/` | the window on the GUI control plane: `debugger`, `disassembly`, `memory` (views' side, no core header) |
| `main.cpp` | the program: options, the screen window, the checks |

The views and the adapter never share a translation unit: ecat's Emulator / CPU / Memory (namespace `ecatshim`) and
the core's classes of the same names would clash.

## Card tabs

With a sound card fitted the side tabs add GS log, GS protocol, GS firmware, GS hardware and GS stats (the catalog
reports `gs-commands`, `gs`, `gs-firmware`, `gs-stats`; a line over each names the card read). On a NeoGS ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
B3; gs-debugger TODO P5.5) the NeoGS tabs show the core's device boards, the same data as unreal-qt's Embedded
debugger (`DebugBoards::Read`, `Emulator::read_board` in the shim; over the WebAPI `GET /debug/boards/{board}`), with
the `gs_stats` board's counters:

| Tab | Boards | What |
|-----|--------|------|
| GS hardware | `neogs` | Config (GSCFG0), Paging (windows, MPAG), Clock with its switch count, DAC mode (the channels in the table), Output (DAC levels, listening mode), Interrupts, SPI (SCTRL), Status (SSTAT), LED, Mailbox, Firmware; the mailbox, interrupt and SPI counters |
| NeoGS DMA | `neogs_dma` | the ZX / SD / MP3 modules as rows (selected, running, address, page, starts, done, bytes, errors, aborts, stall clocks, last), the ZX-DMA block, the DMA rates and errors; under them the ZX-DMA trace (Trace ▸) |
| SD · MP3 · Flash | `neogs_sd`, `neogs_mp3`, `neogs_flash` | the SD card of slot `sd.ngs` (medium, protocol), the decoder and its SCI registers, the flash chip; each with its counters |

The last two come only with a NeoGS. Insert image..., Insert folder..., Eject and Save flash are the boards' actions
(`Emulator::board_action`; over the WebAPI `POST /debug/boards/{board}/actions/{action} {arg}`). A machine whose
boards cannot be read falls back on the `gs` report's `neogs` part. On a GS, GS hardware is the report as it is.

**The ZX-DMA trace** ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md) B3.2;
gs-debugger N6): the DMA tab's **Trace ▸** expands, in place, a table of the bytes the Spectrum moved through the
ZX-DMA window, a row a byte: Frame, Host T, Card cycle, Dir (`read` / `write`, `dropped` in red), Card address
(`#013FF9, page 4`), Value, Wait (T), Host PC. Over it a header line (capturing or not, the bytes kept, the module's
read / written / dropped / wait totals, the note when the ring evicted events; not capturing, how to start) with
**Start**, **Stop** and **Clear**: the GS port trace's capture, which records them (the DAC fetches share its ring).
Each refresh of the tab (at a stop, once a second while running) appends the rows after the last one seen (`since`),
keeps the newest 4,000 and follows the tail unless scrolled up. Double-click a Host PC: the main CPU's listing there;
a card address: its page of `neogs.ram` in the pages tab. The core's report `gs-zxdma` (`Emulator::read_zxdma_trace`,
`zxdma_trace_capture` in the shim; over the WebAPI `GET state/audio/gs/zxdma?last=&since=` and `POST
control/audio/gs/porttrace {action}`). Not there without a NeoGS.

**The lightweight player** (`--card gs-lw`; [gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A10, requirements W2-W3) has no CPU: no card CPU tab, no card run controls. Its two tabs, read-only, come only with it
(the `gs` report's `interpreter` and `player` parts); Log, Protocol, Firmware (the window variables the interpreter
mirrors), Hardware and Stats stay:

| Tab | What |
|-----|------|
| GS interpreter | the state diagram `Idle Command Params Upload Reply` with the current state marked `●`, the ready line (`Upload, 2,000 of 3,196 bytes`); the command and its name, bytes expected / received, the selector; the params and replies queues of 16, the reply pending and the next byte; the upload (module or FX, received of total, %) |
| GS player | the song (state, playing, position / length, pattern of patterns, row, tick, speed, BPM, MTSTAT), the module row (`1 (P)` or `(S)`, its name and length), the FX; the four channels as a table (sample, volume, period, note, rate Hz); the samples as a table (number, name, volume, note, priority, rate Hz, channel, length, the current effect `*`, the loop `(L)`) |

**The MultiSound's GS** (`--card multisound`, CPU `ms`): the card tabs read its reports: GS log `ms-gs-commands`, GS
protocol and GS hardware the `multisound` report's `gs` part, GS firmware `ms-gs-firmware` and GS stats `ms-gs-stats`
(the core reads them with `cpu=ms`: over the WebAPI `state/audio/gs/firmware?cpu=ms`, `state/audio/gs/stats?cpu=ms`);
a line over each tab names the card, and the state's card line reads `MultiSound GS Z80 ...`. With a General Sound and
a MultiSound's GS both, the tabs follow the card CPU's tab in front; with the main CPU in front, the first card CPU (the
same card the card runs act on); with one of them, that one.

## Breakpoints

The Breakpoints tab lists every processor's breakpoints, a row each with its CPU (`main`, `gs`, `ms`): the
execution ones (F2 at the cursor), then the event ones with an on / off box. The toolbar's **Events** menu (and the
tab's context menu) adds an event breakpoint ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A4 `W.bp kinds`, A6 S5), its kinds in plain words in four groups: **Card** (the card takes an interrupt, an NMI, is
reset, switches a page, fetches a DAC sample; the NeoGS's clock, DMA and GSCFG0) and **Protocol, card side** (the
card takes a command, a data byte, writes its reply) on the card's CPU, only with a card; **Protocol, host side** (the
host sends a command, a data byte, reads the reply) and **Machine** (the interrupts, the WD1793; the Next's, TS-Conf's
and the Sprinter's own events) on the main CPU. Each asks for the event's byte: empty for any, `#0F`, or `#80/#E0`
(value / mask). They are the core's `BRK_EVENT` breakpoints (`CPU::add_event_breakpoint` and its kin in the shim;
over the WebAPI `POST /breakpoints {"type": "event", "event", "value", "value_mask", "cpu"}`).

## Run control of a card

With a sound card fitted ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A4 `Toolbar`, `Menu / shortcut`, A6 S6; gs-debugger TODO P5.4) the toolbar adds, after Run to cursor:

| Control | Key | What |
|---------|-----|------|
| Step other (`Step main` on a card's tab, `Step gs` / `Step ms` on main's) | Alt+F11 | one instruction of the CPU not in front: the main CPU's own step, or the card's (`RunControl::CardStep`); the tab in front stays |
| Run to ▾ Card cycle... | - | the card to its cycle N of the frame (asked; plain digits decimal, `#`, `$` or `0x` hex), parked at the next card instruction boundary |
| Run to ▾ Main T... | - | the main CPU to T N of the frame, the card following it (it catches up lazily: its own cycle may lag) |
| Run to ▾ Next card interrupt | - | the card to the first instruction of its next interrupt handler |
| Run to ▾ Next host command | - | the machine to the host's next command write (OUT #BB), the card not having taken it yet |
| - | Ctrl+1 / Ctrl+2 | the main CPU's tab / the card CPU's tab in front |

The menu also holds Run to cursor (F4). The card runs act on the card in front, else the first card; they are
`RunControl::CardRunTo` (`Emulator::debug_card_run_to` in the shim; over the WebAPI `POST /card_run_to {cpu, until,
value}`), end at any CPU's breakpoint, and time out after 5 s (an idle card takes no interrupt and gets no command:
that is said, not a failure). The core's message (where it stopped, a refusal, a timeout) joins the state label until
the next command. Without a card none of them is shown.

## Time travel

> TTD index: [docs/emulator/design/debugger/time-travel-debug/README.md](../../docs/emulator/design/debugger/time-travel-debug/README.md)

While the machine has a recorded history (TTD), the toolbar shows a time group after the forward steps, the same as
unreal-qt's Embedded debugger ([TTD branches: debuggers](../../docs/inprogress/2026-11-08-ttd-branches/debuggers.md);
both route the group to a sound card's CPU when its tab is in front: Embedded has processor tabs too):

| Button | Key | Verb |
|--------|-----|------|
| Step Back | Shift+F7 | `step-instruction {dir: back}` |
| Step Over Back | Shift+F8 | `reverse-step-over` |
| Run Back | Shift+F9 | `reverse-continue {pcs, condition}`: the main CPU's breakpoints; a breakpoint's condition (set by another client: unreal-qt, WebAPI, MCP) joined in, so it stops only where the condition holds, as running forward |
| Frame Back | - | `step-back` |
| Branch Here / Continue Rec | - | `seek` to the end when needed, then `resume` |

The state label adds the moment (`Recording`, `History: frame 120 of 400, replaying`, `Live, not recorded`, the
branch and `fork here: N other futures`); with branches a drop-down lists them (the active lineage first) with Switch,
Rename, Tags, Note, Pin, Delete and Save Lineage. Nothing records unless asked: only Branch Here / Continue Rec.
Every action is a TTDControl verb through `Emulator::ttd_call` (`ecat/dialogs/debugger/ttdcontrols.cpp`): in process
directly, over the WebAPI as the `/ttd/...` routes (`GET` for `status` / `position` / `branches`, the branch routes
for switch / update / delete, `POST /ttd/<verb>` for the rest).

**On a sound card's CPU.** With the card's tab in front (`gs`: the General Sound or NeoGS; `ms`: the MultiSound's
GS), the time group acts on the card, by card instructions
([gs-debugger design §8](../../docs/inprogress/2026-09-27-gs-debugger/design.md)):

| Button | Key | Verb on the card's tab |
|--------|-----|------------------------|
| Step Back | Shift+F7 | `step-instruction {cpu, dir: back}` |
| Step (in the past only) | F7 | `step-instruction {cpu, dir: forward}`: one card instruction forward through the history; live, the card's own step |
| Step over / Step out (in the past only) | F8 / Ctrl+F8 | `step-instruction {cpu, kind: over \| out}`: the card's step through the history, where a live card step would end (its breakpoints end it first); live, the card's own step |
| Run to cursor (in the past only) | F4 | `step-instruction {cpu, kind: to, pc}`: the card's next instruction at the cursor, through the history (its breakpoints end it first; never reached: said, nothing moves); live, the card's own run to the cursor |
| Run Back | Shift+F9 | `reverse-continue {cpu}`: back to the previous hit of the card's own breakpoints |
| Step Over Back | Shift+F8 | `reverse-step-over {cpu}`: one card instruction back, or back past a whole card routine when that instruction is its return |
| Frame Back | - | `step-back`, as on the main CPU's tab |

The state label then names both CPUs: `gs at instruction 1200, PC #0A3C; main CPU PC #8009, +7 T into the next
instruction` (the main CPU's tab shows its registers at its last completed instruction; `at its instruction boundary`
when the card's moment is not inside one). Without a card position the card's tab adds where the card stands
(`gs at instruction 1200, PC #0A3C`, or `gs: no instruction counts in this recording`). A refusal (no card
breakpoint, no counts) and an answer that moved nothing (the start or the end of the history, no earlier hit) are
said in a message with the core's words. At the history's end (`at_end`) F7, F8, Ctrl+F8 and F4 run the card's
live step instead, unrecorded, as the main CPU's steps do there; a move in time right after such a live step works (the
machine first leaves the card's run, up to the next main CPU instruction). The card's disassembly shows the card's own labels
(its firmware symbols), never the main CPU's. The main CPU's tab acts on the main CPU as before; any main-CPU move ends
the card position. A move in time or a pause keeps the tab in front (only a processor's own stop, a breakpoint or
a step, brings its tab forward).

## Run

```text
unreal-debugger [--model PENTAGON] [--card neogs|gs|gs-lw|multisound | --no-gs] [file.sna|.z80|.tap|.trd|...]
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
