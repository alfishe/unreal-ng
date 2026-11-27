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

## Twin timeline

With a sound card's CPU (GS, NeoGS, the ZX-MultiSound's GS) a full-width strip sits between the toolbar and the
panes: the twin timeline ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A5.1, C03; [gs-debugger design](../../docs/inprogress/2026-09-27-gs-debugger/design.md) §5.4, §7.2). It is opt-in:
the toolbar's checkable **GS timeline** (shown only with a card CPU), off by default and kept in the setup
(`Debugger/show_gs_timeline`); off, the strip is hidden, nothing is read and the timeline is not armed. Without a card
CPU (no card, the lightweight player) it is hidden. Both CPUs on one clock, main T relative to NOW:

| Row | Paused | Running |
|-----|--------|---------|
| MAIN | the recent main instructions as blocks, width = their measured T; the current instruction's bus cycles (`OCF 4`, `OD 3`, `IO 4`) when a card's stop parked the main CPU inside one; the next instruction outlined; ● breakpoint, ⇄ host port access (`OUT #BB <- #2A at #800A`), INT / NMI above the lane | the GS port accesses per frame, stacked: commands, data writes, data reads, status polls |
| NOW | the axis (± T), the NOW line; `T 34,995.6 · frame 412 · gap 0.4 T` (the gap only when not zero, also a dashed line in the main lane) | the frames read |
| CARD | the card's blocks on the main clock (exact in card cycles: the tooltip gives them and the clock); the next instruction outlined; INT / NMI and the NeoGS ◆ clock change (`12.0→24.0 MHz at cycle 118,400`) below the lane | busy (tinted) over idle steps per frame, interrupts as a mark on top |

Each lane's readout is on the right (`6.6 / 11 T · 60% · port write`, `cycle 119,985 · 12.0 MHz`; running: `1.0 GS
port accesses / frame`, `busy 12% · 1.0 INT / frame`). Every element has its tooltip. The wheel zooms around the
pointer (± 60 T at first, 4 to 100,000 T); zoomed out, a lane whose blocks are under 4 px merges them into a density
bar. A drag pans, a double-click recenters on NOW, a click on a block shows that CPU's listing at it. With two cards
the card lane follows the card CPU in front.

It reads the core's node (`timeline::Read`; `Emulator::read_timeline` in the shim; over the WebAPI
`GET /debug/timeline?cpu=&span=&recent=&density=&client=&lease_ms=&arm=&wait_ms=0`, no session) at every stop and,
running, every `live_card_ms` (250 ms in this process, 1 s over the WebAPI): a running machine is never paused for
it. A paused read never waits for the machine (`waitMs` / `wait_ms` 0): when another client holds it for a moment or
its stop is not parked yet, the last picture stays and the read is tried again every 40 ms (at most 25 times). The
histories the timeline needs record only while a client holds its lease: the strip takes it when shown, renews it
every 5 s and releases it when hidden, when the window closes and when the connection is lost (client
`ecat-<pid>`; in this process until released, over the WebAPI a 15 s lease that runs out by itself, `POST
/debug/timeline/arm`).

## Card tabs

With a sound card fitted the side tabs add GS log, GS protocol, GS monitor, GS send, GS firmware, GS hardware and GS stats (the catalog
reports `gs-commands`, `gs`, `gs-firmware`, `gs-stats`; a line over each names the card read), and after the card's
own tabs the analysis tabs GS dispatch, GS unknown, GS objects, GS writers, GS profiler, GS events, GS coverage and
GS heat map (below). On a NeoGS ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
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

**The GS monitor** ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A5.3, C10; gs-debugger P5.6): a card tab after GS protocol, with every card that carries a General Sound (GS, NeoGS,
the lightweight player, the MultiSound's GS), to watch next to the game while it plays. Top to bottom: the protocol
strip (`#2A Set module master volume: not taken yet, then reply 1 of 1 due` / `status #01 (cmd) · mailbox: command
#2A, data #3F, to host #FF · last send #2C -> #01 (done)`) with the machine's **Pause** / **Continue** (the toolbar's
F12 / F9); the error tile, the command log's anomalies and the firmware's unknown commands counted since its
**Clear** (which resets the tab's counters only), the last anomaly's text and frame, tinted while not zero; the
command log's newest records (Frame, T, Kind, Command, Name, Text; anomalies tinted), following the tail: scrolled up
it stops, **Follow** resumes. The log reads after a cursor (the history's `since`): it fills with the newest 200
records, then each read takes up to 1600 records after the newest row shown, so none is lost between two reads; the
ones gone from the history before they were read (evicted from the ring) are counted as missed in the tile, and a
cleared or reconfigured history (`restarted`) starts the table afresh. With the command log off the tab says so, and **Turn on** sets history
`<slot>.commands` mode ring. Read once at every stop and, only while the tab is in sight and the machine runs, every
250 ms in process (`Emulator::read_gs_monitor` where the machine stands still) and every second over the WebAPI
(plain GETs of `state/audio/gs` or `state/audio/multisound`, `history/<device>?last=1600&since=S`,
`state/audio/gs/firmware?object=unknown_commands`); nothing pauses. Turn on is `Emulator::gs_command_log` (over the
WebAPI `POST history/<device>/config {mode}`).

A log row's context menu (gui-card-debugger A5.4): **Break when the game sends #NN** (an event breakpoint
`host_command` = #NN on the main CPU), **Break when the firmware takes #NN** (`card_command` on the card's CPU, `gs`
or `ms`; disabled on the lightweight player, which runs no firmware CPU), **Break on data byte N of this upload...**
(only on an upload's block row: `host_data` on the main CPU with the hit policy `N`, the Nth data byte the host writes
from the moment it is set, so set it before the upload starts), **Go to this moment** (a TTD seek to the row's frame
and T; enabled only when the recording holds that moment, otherwise the tooltip says why) and **Copy** (the selected
rows, tab-separated; Ctrl+C on the log too). The command breaks are on the rows of a command (the command, its
parameters, replies, the card taking it, its block). The breakpoints are the CPUs' event breakpoints the Breakpoints
tab lists and edits (in process through the core's manager, over the WebAPI `POST breakpoints` with `hits`); an equal
one already there is taken, enabled again and its hit count reset, instead of a second. The line over the log says
what was done. Over the WebAPI the history's time units are not known: on a turbo-clocked model (Scorpion, ATM,
TS-Conf) the seek lands earlier in the frame than the row.

**The GS send tab** ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A5.3 "Send console", C11; gs-debugger P5.6): a card tab right after GS monitor, not a dialog; the monitor's
**Send...** and **Ctrl+Shift+S** bring it to the front. The command is picked from the command set of the profile
decoding the card (`#2A Set module master volume`), or typed as any byte (#NN, $NN, 0xNN, decimal); a field per
parameter named by the profile (`volume (before)` for a byte written before the command), its hint from the kind;
**More bytes** for a variable count or a byte the profile does not name; **Replies** (empty: the profile's count);
a note with what the replies mean, a transfer the command starts, `(not in the programming guide)`. **Send** queues
the bytes through the card's mailbox as the host would (`gs send`: the command log marks them automation, TTD records
them) and never waits: the table below (#, State, Command → replies; newest first, the last 16, failed rows tinted)
follows each send by its id to `done` with its replies decoded (`#23 Get number of RAM pages -> #03 (pages)`). A
field that is not a byte is refused with its name; on a paused machine a send waits in the queue until it runs (the
tab says so). The MultiSound's card has no send (the planes address the GS slot's card): the tab says so. Shim
`Emulator::gs_commands`, `gs_send`, `gs_conversation`: in process `GSFirmwareObjectsRead` (object `commands`),
`GSSend` with no wait and `GSConversationRead` under a lease of the context; over the WebAPI `GET
state/audio/gs/firmware?object=commands` and `POST control/audio/gs {action: send, value, pre, params, replies,
wait_ms: 0}` / `{action: conversation, send_id}`. Read at every stop and, while in sight and the machine runs, with
the other card tabs (the command set is reloaded only when its profile changes).

**The analysis tabs** ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A5.2a; gs-debugger requirements M3, M4, M5, AU6, X7): GS dispatch, GS unknown, GS objects and GS writers, after the
card tabs (eCat has no View menu to keep them hidden until opened: they come with the card, as the other card tabs),
with every card that carries a General Sound; each reads its card (the General Sound's or the MultiSound's GS):

| Tab | What |
|-----|------|
| GS dispatch | the profile stack (`Profile stack: GS v1.05a ▸ GP gscode.bin`: the ROM's profile, the uploaded code's the command log's decoder took on top), the table's place and counts; a row an entry: Command, Name, Handler, Label (the card's symbol there), Baseline (the entry as the firmware started: a GS's ROM, a NeoGS's RAM copy at its start), Patched by. A patched entry is tinted and bold, with the #14 upload that wrote its slot (`GP gscode.bin upload @#4000, 3,212 B, sha 3fa1..., frame 412`; the upload whose profile names the command, else the newest that covers the slot; `no upload seen`); the stub's entries dimmed, **Named and patched only** hides them |
| GS unknown | the commands the decoding profile does not name (AU6), a row a command: Since Clear (`×3`), Total, Dispatch (the handler and label, `the unknown-command stub`). **Clear** sets the tab's baseline (the core has no verb to clear the counts: the command log keeps them from its start); with the log off the tab says so, with **Turn on** |
| GS objects | **Read as** the active profile or a profile that declares structs; a table per struct instance (columns the schema's fields, rows the instances: `smp: struct smp, 32 x 64 bytes @#xxxx`), then the profile's variables (memory, register, operand); a value changed since the last read in the changed colour. The built-in tables' window variables stay in GS firmware, which now shows only the profile and the variables (no instances, dispatch or unknown commands: these tabs have them) |
| GS writers | a row a port: the host's `#B3` / `#BB` / `#33` and the card's `#00`, `#03`, `#05`, `#06-#09` (and any other port written): Register, Side, Value, Writer PC, Label, Frame (and the host's T), Source, Writes read. The host's #B3 / #BB from the command log (its records carry the main CPU's PC; #33 bit 7 its reset marker); the card's own OUTs and the host's #33 from the GS port trace (the card CPU's PC; the host's writer of #33 is not kept, said in the cell's tooltip). **Turn on the log**, **Start the trace** when off |

Read once at every stop and, only while in sight and the machine runs, every 250 ms in process and every second over
the WebAPI; nothing pauses. In process at a coherent moment: `Emulator::read_gs_firmware` (GSFirmwareObjectsRead, the
card's report for the profile and the uploads, the card's label manager), `read_gs_writes` (the command log history,
the port trace's host and card sides apart: `getPortTraceSide`), `gs_firmware_profiles`, `gs_port_trace`. Over the
WebAPI plain GETs: `state/audio/gs/firmware[?profile=][&cpu=ms]`, `state/audio/gs` (or `state/audio/multisound`),
`labels?cpu=&active=true` (kept 30 s), `labels/resolve?address=` (the writers' PCs, kept 30 s),
`history/<device>?last=400`, `state/audio/gs/porttrace?events=3000` (no side filter on the route: the DAC fetches
crowd the card's port writes out; the GS slot's card only, not the MultiSound's), `state/audio/gs/firmware/profiles`.

**The profiler and analysis tabs** ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A5.2a rows Profiler, Events, Coverage, Heat map; gs-debugger requirements X2, X8, X9, X10, X11, X20): GS profiler,
GS events, GS coverage and GS heat map after GS writers, on the card CPU's profiler (`gs profile`, `gs events`) and
analysis (`gs coverage`, `gs heatmap`). Both are off by default and slow the card while on (the profiler about x1.9,
the analysis about x2.5): each tab says so and offers its switch. The lightweight player has no card CPU: each tab
says `not available`.

| Tab | What |
|-----|------|
| GS profiler | the ISR budget as a bar (filled the average, a red mark the most) and two lines, last frame and since start: `212 / 320 avg · 301 max · 34% headroom · ISRs · overruns · latency avg, max · lost`; why a part is empty (`Interrupts off: the firmware idles with interrupts disabled ... N interrupt requests lost`, `No routine called yet: the idle loop calls nothing ...`, the command in progress); the host commands' cost (Command, Name, Handler, Count, Avg, Min, Max, Total); the routines by inclusive cycles (Address, Page `ROM 0` / `RAM 3`, Label, Calls, Inclusive, Exclusive, Avg, Max, %; the interrupt entries marked); the activity per frame as a graph (busy %, ISR %; 128 frames) over a table of the newest 64 (ISR / idle / halt / busy %, commands, INT, overruns, lost, ISR max). **Start** (a new session) / **Stop** / **Clear** |
| GS events | the last complete host frame on a canvas: a row an interrupt period (2 px), a column a card cycle of it scaled to the width; the ISR as a bar from its accept to its end (NMI purple, still running lighter, past its period a red tail), dots for the host's #B3 write / read, #BB command / status, #33, DAC fetches (hue by channel), page and volume writes, ZX-DMA reads and writes; a legend; a tooltip with the period, the cycle, the bar and the dots there. Hints: the profiler off (**Start the profiler**), the port trace off (**Start the trace**: no dots without it), no ISR bar (interrupts off) |
| GS coverage | per space (ROM / flash, RAM) a total row and a row a marked page: Exec, Code, Read, Write, DAC, Written bytes, the code / data / DAC ranges (offsets in the 16 KB page, 16 each, `…` when cut); the command handlers executed (Command, Name, Handler, Where) and the ones not (dimmed); the reads of RAM never written (Address, Place `ram 2:#0100`, PC, Label, Frame, Kind; the written stamps start at On). **On** / **Off** / **Reset** / **Release**; **Break on new code** shown disabled (the core has no such breakpoint yet) |
| GS heat map | the card RAM as a grid (16,384 cells at most, square, cells of 16 B or more), each channel in its colour (fetch blue, read green, write red, DAC amber, SD-DMA purple, ZX-DMA teal), **Channel** all (added) or one; **Decay** (heat lost per frame, applied on edit); a tooltip with the cell's RAM range and each channel's heat; the DAC ranges of the last 50 frames (the samples playing). **On** when off (shared with GS coverage) |

One read is one core report as JSON (`Emulator::read_gs_report`): in process `DeviceState::GsProfile` / `GsEvents` /
`GsCoverage` / `GsHeatmap` on the window's thread (they never park the machine); over the WebAPI `GET
state/audio/gs/{profile,events,coverage,heatmap}?cpu=&...` (heat map `cells=16384&format=base64&recent=dac`). The
switches: `Emulator::gs_profiler` (`GSProfileControl`; `POST control/audio/gs {action: profile_start|profile_stop|
profile_clear, cpu}`) and `gs_analysis` (`GSAnalysisControl`; `POST control/audio/gs/analysis {action, cpu, decay}`).
The never-written reads' labels are the card CPU's own symbols (its disassembler, exact address). Same cadence as the
other analysis tabs: at stops, and while in sight and running every 250 ms in process, every second over the WebAPI.

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

**The main CPU with a card** ([gui-card-debugger](../../docs/inprogress/2026-09-28-debugger-model/gui-card-debugger.md)
A6 S1-S4; gs-debugger P5.2, P5.5):

| Where | What |
|-------|------|
| State label | the card line (`GS Z80 12 MHz · PC #0C32 · cycle 119,985 · MPAG #03 · Symbols: GS v1.05a`) at every stop and, while the machine runs, `Running · <card line>` read again every 250 ms in process and every second over the WebAPI (a plain `GET state/audio/gs`; nothing pauses); after a card's stop also where the main CPU stands inside its instruction (`main #8003: T 17 of 16 + 1 T wait, memory write; ...`, the wait a device's /WAIT such as ZX-DMA) |
| Card CPU's registers | row `T`: the card's cycle in the ZX frame and the change since the value shown before, `c119985 Δ12` (`CPU::card_cycle`: in process `GeneralSoundCard::cardCycles`; over the WebAPI the `gs` report's `cpu.frame_cycle`, the change then taken within one frame) |
| Main CPU's listing and dump | the ZX-DMA banner, a line over each while the NeoGS's ZX module is on (`ZX-DMA active: reads at #0000-#3FFF return card RAM @#012345; next byte #A7`, the RAM-paged and Watch wordings too); in Divert with the ROM paged the listing's rows `#0000-#3FFF` have their bytes in amber, `card bytes at run time` on the first and the top row, the reason in the tooltip. `Emulator::zxdma_banner` (in process `DeviceState::ZxDmaBanner` at a coherent moment; over the WebAPI the `gs` report's `neogs.dma.zx`), read at the stops and with the card line |
| GS hardware (NeoGS) | the board's Paging group has `MPAGEX` and `extended paging` (the header `MPAG=#03 MPAGEX=#05` while EXPAG is on) |

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

## Remote mode (`--connect`)

The views call the shim API synchronously and never wait on the network: `adapter/remotemachine.cpp` answers every
call from what it has and fetches the rest in the background (`Net`).

- **Lanes.** Three threads, each with its own keep-alive connection. Lane 0 runs the commands (run control, writes,
  TTD moves) in order, then the snapshot (`GET debug/snapshot`: state, registers, the CPU's 64 KB), then reads; the
  other lanes take reads only. The snapshot is never fetched while a command is queued or runs, so a stop is
  never shown from before the command that made it. No thread per request.
- **Reads** (`Net::get`): a cache keyed by method, path and body, stale while it revalidates. An answer is current
  while it belongs to the snapshot generation in place, no command was queued after it was asked, nothing
  invalidated it, and it is not older than its maximum age (1 s by default, 500 ms for the timeline, conversation,
  catalog and histories). A stale read returns what it had and queues one fetch (deduplicated). A read with no
  answer yet returns `kPending`; the views keep what they show (pages, regions, labels).
- **Commands** (`Net::send`, `Net::task`): queued, `kQueued` at once; their `done` runs on the GUI thread. A
  refused command without a `done` shows in the state line for 5 s (`debug_link_text`). Run control marks the
  machine *under way* (`debug_stepping`) until a snapshot taken after the command has arrived.
- **Arrivals.** What came (a changed answer, a snapshot taken after a command, a read that had waited, a command's
  end) is posted to the GUI thread as one call: the snapshot is installed, the generation moves, and the window
  repaints its views (`follow_data`, coalesced to 30 ms while paused).
- **Waiting.** A read waiting over 1 s or a command over 2 s shows as `Waiting for the emulator: N s` in the state
  line; run control is disabled while a step is under way. A stalled emulator is waited for, never declared gone
  while it holds the connection; one that is gone is looked for again once a second (the connect retry on its own
  thread), never started by the debugger.
- **Asynchronous shim calls** (`debug_card_run_to_async`, `gs_send_async`, `ttd_call_async`): the answer comes later
  on the GUI thread; in process at once. The callers guard themselves with `QPointer`. TTD moves (`TtdControls`)
  chain their follow-ups: Branch Here's seek, resume and media question; a card step at the history's end steps on
  live once its answer has come.
- **Run-to.** `debug_run_to` / `drop_run_to` / `clear_run_to` run as tasks on lane 0 under `m_run_to_mutex`, in
  order with the other commands; the stop's sequence mark is unchanged.
- **Synchronous mode** (`RemoteOptions::synchronous`): every call waits for its answer, as before. Used by
  `--self-check`, `--screenshot`, `--measure` and `--list-memory`, which read back what they have just written.
- **Closing.** The last commands (run-to removed, debug mode and histories restored) get up to 2 s, then the requests
  in flight are aborted (checked every 100 ms), the waiter's long poll too.

`UNREAL_DEBUGGER_NET_TRACE=1` prints each request's end on stderr (lane, time, request, status, duration).

Checks:

- `--self-check` with `--connect`: its last block, `async connection`, opens a second, asynchronous connection
  through `LatencyProxy` (`latencycheck.cpp`). It delays answers by 300 ms and then stalls them. The block checks
  that a step shows under way at once, that a stall shows as a wait, that the machine is held again afterwards,
  and that closing a stalled connection ends in bounded time.
- `--gui-gap-check [--gap-latency-ms 300] [--gap-stall-ms 6000] [--gap-limit-ms 0]` with `--connect`: the window
  over the same proxy. It presses pause, step, step over, step out, run and pause with no delay, with the delay,
  during a stall and after it. Each phase prints the longest gap of the GUI event loop (a 1 ms timer) and the GUI
  thread's CPU time in that gap. With a limit given, the check fails above it.

Measured on an M3 Ultra (load 30-80 from other sessions), master versus this design, longest event-loop gap:

| Phase | master | async |
|-------|--------|-------|
| no delay | 505 ms | 11 ms |
| answers +300 ms | 184 s (one pause) | 19 ms |
| stalled 6 s | 20 s (then "Waiting for <id>") | 9 ms |
| after the stall | 1.1 s | 12 ms |

## Run

```text
unreal-debugger [--model PENTAGON] [--card neogs|gs|gs-lw|multisound | --no-gs] [file.sna|.z80|.tap|.trd|...]
unreal-debugger --connect 127.0.0.1:8090 [--emulator id]   a running emulator (unreal-qt) through its WebAPI
unreal-debugger --zoom 1.5                                  the window's scale (Ctrl+= / Ctrl+- / Ctrl+0)
unreal-debugger --self-check                                the debugger API and the window's editors; exit 2 on a failure
unreal-debugger --self-check --timeline-shots DIR          also saves the twin timeline paused and running as PNGs
unreal-debugger --screenshot out.png                        stop, step, breakpoint; saves the window and exits
unreal-debugger --list-memory                               the dump tabs and the "+" menu the window builds
unreal-debugger --measure 10                                frames/s and CPU: screen only, debug mode, window
unreal-debugger --connect ... --gui-gap-check               the GUI event loop's longest gap, delayed and stalled
```

Run from the build's `bin/` (the core finds `data/` there; in the macOS package, `UnrealNG.app/Contents/Resources`).

The window is on the GUI control plane as program `unreal-debugger` (`unreal-gui list`, `unreal-gui memory.goto 0x6000`):
the same `debugger`, `disassembly` and `memory` commands as unreal-qt's debugger, on the listing and the dump of the
processor in front. `UNREAL_GUI_CONTROL_DIR` and `UNREAL_GUI_CONTROL_PORT` as for every GUI program; see
[the GUI control plane](../../docs/emulator/design/gui-control/README.md#6-interfaces). Check:
`python3 tools/guicontrol/verify_debugger.py`.
