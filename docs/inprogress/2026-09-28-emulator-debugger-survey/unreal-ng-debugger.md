# unreal-ng debugger — capability survey

**Source:** this repository (unreal-ng, descended from Unreal Speccy) · `master` at `35e9d050` (2026-09-28) plus the uncommitted working-tree docs of that day · C++17/20 core (`core/`), Qt 6 desktop UI (`unreal-qt/`), Drogon HTTP server, sol2 Lua, pybind11 Python.
**Surveyed:** 2026-09-28 (source reading; every `shipped` row checked in code, not only in docs)
**Scope note:** a headless core debugger (`core/src/debugger/`) with breakpoints, time-travel debugging (TTD), analyzers, labels, disassembler, assembler and listings; profilers and tracers in `core/src/emulator/`; ten reachable surfaces (Qt GUI, telnet CLI, WebAPI + OpenAPI, MCP, Lua, Python, DeZog DZRP, ZEsarUX ZRCP, GDB RSP, plus a TUI proof of concept). A large designed-but-unbuilt layer sits on top: conditional-breakpoint engine (PLAN #6), multi-CPU debugger model with the GS card debugger (#45), video debug translation (#42), TUI (#49), devtools/DAP/LSP (#51).

Paths are relative to the repository root. `PLAN #N` means row N of `docs/inprogress/PLAN.md`.

**Status legend:** `shipped` = in code on `master` (cited file:line) · `in progress` = on a branch / worktree / proof of concept, or partial · `designed` = a design doc exists, no code (doc + section cited) · `planned` = only a PLAN row · `idea` = mentioned in passing.

**Surfaces legend:** Qt = `unreal-qt` GUI · CLI = telnet CLI on TCP 8765 · Web = WebAPI on 8090 (+ OpenAPI) · MCP = MCP server on 8092 (+ stdio bridge) · Lua · Py = Python · DZRP = DeZog protocol on 12000 · ZRCP = ZEsarUX remote protocol on 10000 · GDB = GDB RSP stub on 2000 · TUI = `tools/poc/018-tui-debuggers` over the WebAPI.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Status | Surfaces | Where |
|---|---|---|---|---|---|
| CPU & registers | Register read | AF BC DE HL, the alternate set, IX IY SP PC I R, IM, IFF, T; flags string in Qt | shipped | Qt, CLI, Web, MCP, Lua, Py, DZRP, ZRCP, GDB, TUI | `unreal-qt/src/debugger/widgets/registerswidget.cpp:208-255`; `core/automation/webapi/src/emulator_api.h:294`; `core/automation/gdb/src/gdbtarget_z80.cpp:15-101` |
| CPU & registers | Register write | Set any register by name | shipped (not in Qt) | CLI, Web, Lua, Py, DZRP, ZRCP, GDB | `core/automation/cli/src/commands/cli-processor-memory.cpp:1140`; `emulator_api.h:295` (`PUT /registers/{name}`) |
| CPU & registers | Register to memory / disassembly | Double-click or context menu on a register pair shows it in the hex view or disassembler | shipped | Qt | `registerswidget.cpp:151-152`, `:258-303` |
| CPU & registers | Changed-value highlight, flag checkboxes, IFF/ISR popup | Xpeccy parity Phase 3 | designed | Qt | `docs/inprogress/2026-08-26-debugger-enhancements/features-parity.md` "Implementation Phases"; PLAN #46 |
| CPU & registers | Several CPUs (main + GS/NeoGS card Z80) | One debug target per CPU, one pause, one timeline | designed | all (via `cpu=` selector) | `docs/inprogress/2026-09-27-gs-debugger/design.md` §3.1-§5; PLAN #45 |
| CPU & registers | GS card CPU outside view | GS `pc`, `sp`, `af`, `halted` only | shipped | Web, CLI, Lua, Py, MCP | `core/automation/webapi/src/api/state_audio_api.cpp:954-960` |
| Disassembly | Z80 disassembler | Every prefix (CB, DD, ED, FD, DDCB, FDCB); undocumented opcodes (`sll`, `out (c),0`, IXH/IXL); T-state fields in the table | shipped | Qt, CLI, Web, MCP, Lua, Py, ZRCP, TUI | `core/src/debugger/disassembler/z80disasm.h:21-28`, `:290-296`; `z80disasm.cpp:387-394`, `:1014` |
| Disassembly | Runtime annotations | "Will jump / Won't jump to $X", "Looping to …, B=", "BC=", "Returning to $X", IX/IY effective address | shipped | Qt, Web | `z80disasm.cpp:2094-2249` |
| Disassembly | Symbolic disassembly | Label on the line; operand shown as `label (#ADDR)` | shipped | Qt, CLI, Web, MCP, Lua, Py | `z80disasm.cpp:2051-2060`, `:2956-2967` |
| Disassembly | Qt disassembler widget | Breakpoint dot, address, bytes, label, mnemonic, runtime hints, IX/IY target label; ROM/RAM bank indicator with ROM title; Ctrl+G go-to (`0x`/`$`/`#`/decimal); Enter returns to PC; margin click toggles an execute breakpoint | shipped | Qt | `unreal-qt/src/debugger/disassemblerwidget.cpp:274-361`, `:749-810`, `:948-975`, `:1023-1032` |
| Disassembly | Page disassembly | Disassemble a physical page, not only the Z80 window | shipped | CLI, Web, Lua, Py | `emulator_api.h:308-309` |
| Disassembly | Control-flow decoder | Zero-allocation decoder: type, target (incl. `JP (HL)`, RET via SP), taken/not-taken from F/B; used by the call trace | shipped | (internal) | `core/src/debugger/disassembler/z80cfdecoder.h:3-57`; `core/src/emulator/memory/calltrace.cpp:490` |
| Disassembly | "Document" disassembly (REDasm-style listing view, jump arrows) | Stub / unused views | in progress (stub) | Qt | `core/src/debugger/disassembler/documentdisasm.cpp:14-22`; `unreal-qt/src/debugger/disassemblerlistingview.cpp:8-34` |
| Disassembly | In-process assembler | Two-pass text assembler: documented Z80 incl. DD/FD, ORG/EQU/DB/DW/DS, labels, `$`, expressions; returns bytes + symbols + listing; optional write to memory | shipped | CLI, Web, MCP, Lua, Py | `core/src/debugger/assembler/z80textassembler.h:26-55`; `core/automation/webapi/src/api/debug_api.cpp:3723-3840` |
| Memory views | Memory read/write, block, word, hex dump | By Z80 address or by `{type}/{page}/{offset}`; ROM write protect toggle | shipped | CLI, Web, MCP, Lua, Py, DZRP, ZRCP, GDB | `emulator_api.h:193-203`, `:296-301` |
| Memory views | Qt hex view | Read-only QHexView, 8 bytes/line, 16K bank containing PC, jumps to PC on pause | shipped | Qt | `unreal-qt/src/debugger/debuggerwindow.cpp:206-212`, `:575-607` |
| Memory views | Memory search | Byte pattern / text, start/end/max/alignment; no wildcards | shipped | CLI, Web, MCP, Lua, Py | `core/automation/webapi/src/api/state_memory_api.cpp:735-738` |
| Memory views | Paging / memory map | Four Z80 slots with ROM/RAM page names; page grid showing mapped and active pages | shipped | Qt, CLI, Web, MCP, Lua, Py, GDB | `unreal-qt/src/debugger/widgets/memorypageswidget.cpp:98-132`; `memorypagesviswidget.cpp:210-238`; `emulator_api.h:187-190` |
| Memory views | Stack view | 4 entries in Qt; `stack` aspect in MCP | shipped | Qt, MCP | `unreal-qt/src/debugger/widgets/stackwidget.cpp:152-162`; `core/automation/mcp/src/mcp-tools.cpp:884` |
| Memory views | Server-side memory dump to file (`POST /memory/dump`) | TD-3 phase 2 | planned | Web | PLAN #4 |
| Memory views | Memory watcher widget / expression watches | Watch window | designed | Qt | `docs/inprogress/2026-08-26-debugger-enhancements/xpeccy-comparison.md` "Memory Watcher"; `docs/inprogress/2026-09-28-debugger-model/widget-catalog.md` §1 (`W.watch`) |
| Navigation & bookmarks | Go to address, return to PC | Ctrl+G, Enter | shipped | Qt | `unreal-qt/src/debugger/disassemblerwidget.h:48-73` |
| Navigation & bookmarks | Address history, marks 1-5, follow operand, go to PC | Xpeccy parity Phase 1 | designed | Qt | `docs/inprogress/2026-08-26-debugger-enhancements/proposal.md` "High Priority (Parity)"; PLAN #46 |
| Navigation & bookmarks | TTD bookmarks | Named, unique, ≤ 63 chars, advisory positions; seek to bookmark | shipped | CLI, Web, MCP, Lua, Py | `core/src/debugger/ttd/ttdbookmarks.h:4-38`; `timetravelmanager.h:871-895` |
| Symbols & labels | Label store | name, Z80 address, bank, bankOffset, bankType, type (code/data/const), module, comment, active; filters by module/bank/type/range | shipped | Qt, CLI, Web, MCP, Lua, Py | `core/src/debugger/labels/labelmanager.h:19-112` |
| Symbols & labels | Symbol file import | Generic `.map` (with `RAM2:4000` bank prefix), simple `.sym`, VICE (`.vice` / `al` sniff), SJASM `EQU` (only for `.s`/`.asm`), z88dk `DEFC` (only for `.z88`) | shipped (partly broken, §6) | Qt, CLI, Web, MCP, Lua, Py | `labelmanager.cpp:449-484`, `:496-560`, `:628`, `:701`, `:753`, `:812` |
| Symbols & labels | Symbol file export | Always one format: `ADDR NAME (type) ; comment` | shipped | Qt, CLI, Web, Lua, Py | `labelmanager.cpp:397-439` |
| Symbols & labels | Label editor | Sortable table (Label, Address, Bank, Bank Offset, RAM/ROM, Type, Comment); add/edit/delete; load/save; drag-and-drop of symbol files on the main window | shipped | Qt | `unreal-qt/src/debugger/labeleditor.cpp:16-21`, `:148-169`; `unreal-qt/src/mainwindow.cpp:2023-2026` |
| Symbols & labels | ROM symbol tables | `48k_rom.map`, `128k_rom.map`, variables maps, `sos.l` in `data/symbols/` — not auto-loaded | shipped (data only) | — | `data/symbols/` |
| Symbols & labels | sjasmplus SLD, per-CPU label sets, paging-aware lookup, auto-reload | `libunreal-debuginfo` (SLD/.lst/.sym/.cdb); GS firmware `.map` by ROM SHA-256 | designed | all | `docs/emulator/design/debugger/label-manager.md` (task 0.2.2, "Future"); `docs/inprogress/2026-09-21-devtools-roadmap/unreal-ng-developer-toolchain-design.md` §8.1; `docs/inprogress/2026-09-27-gs-debugger/design.md` §6.2 |
| Source-level | sjasmplus `.lst` listing | Address→line map (64K), next/previous code line; `source_at`, `step_line`, `run_to_line` | shipped | CLI, Web, MCP, Lua, Py | `core/src/debugger/listing/listingparser.h:31-85`; `emulator_api.h:445-448` |
| Source-level | Source-level debugging through DeZog | DeZog reads SLD itself; unreal-ng serves DZRP | shipped (DeZog side) | DZRP | `core/automation/dezog/src/dzrpserver.cpp:279-323` |
| Breakpoints | Execution | Per Z80 address (any bank) or per physical page (`BRK_MATCH_BANK_ADDR`) | shipped | Qt, CLI, Web, MCP, Lua, Py, DZRP, ZRCP, GDB, TUI | `core/src/debugger/breakpoints/breakpointmanager.h:19-47`, `:233-246` |
| Breakpoints | Memory read / write | Same keying; combined r/w (`wp`) | shipped | Qt, CLI, Web, MCP, Lua, Py, DZRP, ZRCP, GDB | `breakpointmanager.h:27-31`; `core/src/emulator/memory/memory.cpp:328-355`, `:448-472` |
| Breakpoints | Port IN / OUT | One exact 16-bit port per breakpoint, no mask | shipped | Qt, CLI, Web, Lua, Py, GDB (`monitor bport`) | `breakpointmanager.h:33-36`; `breakpointmanager.cpp:1444-1468`; `core/src/emulator/ports/portdecoder.cpp:162`, `:270` |
| Breakpoints | Keyboard press/release | Type exists, cannot be added (AddBreakpoint only routes memory and I/O) | in progress (stub) | Qt editor lists it | `breakpointmanager.h:38-41`; `breakpointmanager.cpp:68-85` |
| Breakpoints | Groups | Assign, list, activate/deactivate/remove a group | shipped | Qt, CLI, Web | `breakpointmanager.cpp:948-1047`; `unreal-qt/src/debugger/breakpointgroupdialog.cpp` |
| Breakpoints | Owners, hidden and "silent" analyzer breakpoints | `owner` field ("interactive", "analyzer_manager", "gdb"); analyzer hits never pause; hidden step-over/step-out breakpoints | shipped | (internal) | `breakpointmanager.h:52-80`; `core/src/emulator/cpu/z80.cpp:262-318`; `core/src/debugger/analyzers/analyzermanager.h:189-207` |
| Breakpoints | Last-triggered info | ID, address, kind of the last hit | shipped | CLI, Web, Lua, Py | `breakpointmanager.h:163`, `:188-211` |
| Breakpoints | Breakpoint dialog | Table ID/Type/Address/Access/Status/Group/Notes; add/edit/delete/enable/disable/group; search, group filter; Ctrl+N/E/T/F | shipped | Qt | `unreal-qt/src/debugger/breakpointdialog.cpp:121-301` |
| Breakpoints | Ranges, conditions, hit counts, ΔT, port masks, IRQ/NMI breakpoints, actions (log/screenshot/count) | New condition engine (`bpcondition`), slices 1a-1h | designed | all | `docs/inprogress/2026-08-17-conditional-breakpoints/design.md` §3-§4, `implementation-plan.md` §3; PLAN #6 |
| Breakpoints | Conditions + pass counts (ZEsarUX dialect) | `or/and/not`, comparisons, `+ - * /`, registers, `PEEK/PEEKW`; pass counts per entry | shipped (ZRCP only) | ZRCP | `core/automation/zesarux/include/zesaruxcondition.h:1-55`; `core/automation/zesarux/src/zrcpserver.cpp:605` |
| Breakpoints | Event/protocol breakpoints (INT, reset, page switch, clock switch, GS host command/data) | Part of the debugger model | designed | all | `docs/inprogress/2026-09-28-debugger-model/rules.md` §5.4 |
| Breakpoints | Persistence (`bpx.ini` import + JSON) | Qt `saveState` is a TODO stub | designed | Qt, all | `unreal-qt/src/debugger/debuggerwindow.cpp:648-652`; `debugger-model/rules.md` §5.5; `2026-08-26-breakpoint-enhancements/design.md` "Serialization" |
| Conditions & expressions | Expression evaluator (xpeccy `xexpr`-style RPN) | Registers, pseudo-vars (RD WR IN OUT VAL DOS SLOT FRAME RAYX RAYY HITS), labels, memory reads | designed | all | `docs/inprogress/2026-08-26-expression-evaluator/design.md`; PLAN #6 |
| Conditions & expressions | `run_until_condition(predicate)` | Host-language predicate polled per instruction, skips breakpoints | shipped | Lua, Py | `core/src/emulator/emulator.cpp:2441-2484`; `core/automation/lua/src/emulator/lua_emulator.h:951`; `core/automation/python/src/emulator/python_emulator.h:323` |
| Watchpoints & watches | Watchpoints | Plain read/write breakpoints; GDB Z2/Z3/Z4 expand a range to per-byte breakpoints | shipped | CLI (`wp`), Web, DZRP (42/43), GDB | `core/automation/cli/src/commands/cli-processor-breakpoint.cpp:125-215`; `core/automation/gdb/src/gdbserver.cpp:1313-1398` |
| Watchpoints & watches | Reverse watchpoints (find last write/read/exec/IO) | TTD probe, independent from BreakpointManager | shipped | CLI, Web, MCP, Lua, Py, GDB (`monitor ttd findlast`) | `core/src/debugger/ttd/ttdprobe.h:40-87`; `timetravelmanager.h:1094` |
| Execution control | Pause / resume / reset / NMI / MNI | MNI = NMI into the service monitor on models that have one | shipped | Qt, CLI, Web, MCP, Lua, Py, DZRP, ZRCP, GDB | `core/src/emulator/emulator.cpp:861`, `:1130`, `:1209`; `unreal-qt/src/menumanager.cpp:740-746` |
| Execution control | Step in, step N, step over, step out | Step over covers CALL/RST/DJNZ/block ops with exclusion ranges; step out tracks SP and recognizes RET/RETI/RETN incl. ED aliases | shipped | Qt, CLI, Web, MCP, Lua, Py, DZRP, ZRCP, GDB | `emulator.cpp:2046-2085`, `:2486-2719`; `core/src/debugger/disassembler/z80disasm.cpp:2309-2352`, `:3070-3175` |
| Execution control | Frame step, run N frames, run T-states, to scanline, N scanlines, to screen pixel, to interrupt | Beam- and frame-granular stepping; drift-free frame step | shipped | Qt ("Adv Step"), CLI, Web, MCP, Lua, Py | `emulator.cpp:2087-2439`; `unreal-qt/src/debugger/debuggerwindow.cpp:110-114`, `:1198-1247` |
| Execution control | Run to cursor | Menu entry exists, disabled (`// TODO`) | idea | Qt | `unreal-qt/src/menumanager.cpp:884-887` |
| Execution control | Speed control presets | Step by full speed / frame / opcodes / T-states / scanlines | shipped | Qt | `unreal-qt/src/debugger/widgets/speedcontrolwidget.h:18-24` |
| Execution control | Step coordinator across CPUs, main-CPU instruction progress | Card follows the main CPU; tight catch-up | designed | all | `docs/inprogress/2026-09-27-gs-debugger/design.md` §5.2-§5.4 |
| Tracing & logging | Call trace | Taken JP/JR/CALL/RST/RET/RETI/DJNZ with banks, SP, top stack words, loop compression; up to 1 GiB; YAML export | shipped | Qt (toggle), CLI, Web, MCP, Py | `core/src/emulator/memory/calltrace.h:20-161`; `calltrace.cpp:357-380` |
| Tracing & logging | Opcode profiler + last-N trace | Counters for 1792 opcode variants; 10,000-entry ring (pc, prefix, opcode, F, A, frame, T) | shipped | Qt (toggle), CLI, Web, MCP, Py | `core/src/emulator/cpu/opcode_profiler.h:16-65` |
| Tracing & logging | Port trace (Port Diagnostic Recorder) | 24-byte events: T-state, frame, raw/decoded port, PC, value, decode rule, device id, flags; 1M ring; filters/presets; JSON/CSV/PTRC/PTR2 export | shipped | CLI, Web, MCP, Lua, Py | `core/src/emulator/ports/portdiagrecorder.h:29-287`; `emulator_api.h:344-355` |
| Tracing & logging | GS port trace | Host ports #B3/#BB/#33 and card ports 0x00-0x0B | shipped | Web, MCP, Lua, Py | `core/src/emulator/sound/chips/gs/gsporttrace.h:1-14`, `:135` |
| Tracing & logging | AY register-write log | Ring of (frame, T, PC, port, chip, reg, value) | shipped | CLI, Web, Lua, Py | `core/src/debugger/analyzers/aylog/ayloganalyzer.h:16-64` |
| Tracing & logging | Continuous instruction trace to file | — | idea | — | none in code (§11) |
| Tracing & logging | GS command log (decoded host↔card protocol) | Ring owned by SoundManager | designed | all | `docs/inprogress/2026-09-27-gs-debugger/design.md` §6.3 |
| Tracing & logging | Port-code trace (Sprinter/infrastructure) | Listed under shared machine pieces | planned | — | PLAN #60 |
| History / rewind / time travel | TTD record / stop / invalidate | Per-frame checkpoints, key frame every 50; recording lock forces 1x and refuses turbo/fast tape/fast disk | shipped | Qt, CLI, Web, MCP, Lua, Py | `core/src/debugger/ttd/timetravelmanager.h:320-362`; `timetravelmanager.cpp:478-518` |
| History / rewind / time travel | Seek to frame / T-state | Frame target shows the final picture; T-state target draws the beam up to T over the previous frame (raster-accurate) | shipped | Qt (scrubber), CLI, Web, MCP, Lua, Py, GDB (`monitor ttd seek`) | `timetravelmanager.h:830`, `:1039-1044`, `:1465-1477` |
| History / rewind / time travel | Step back / forward (frame, instruction, N instructions, N T-states) | Lands on M1 boundaries | shipped | Qt (frame only), CLI, Web, MCP, Lua, Py, GDB (`bs`), DZRP (history) | `timetravelmanager.h:698-705`, `:1130-1202` |
| History / rewind / time travel | Reverse continue | Back to the previous hit of a PC set; reports the searched window | shipped | CLI, Web, MCP, Lua, Py, GDB (`bc`) | `timetravelmanager.h:1210-1227`; `core/automation/gdb/src/gdbserver.cpp:1649` |
| History / rewind / time travel | Find last access | Write/Read/Execute/IO on an address range, value filter, PC range, physical page, before time T | shipped | CLI, Web, MCP, Lua, Py, GDB | `ttdprobe.h:40-87` |
| History / rewind / time travel | Resume from the past (branch) | Truncates the future; debugger edits journaled as replay barriers | shipped | Qt ("Rec From Here"), CLI, Web, MCP, Lua, Py | `timetravelmanager.h:927`, `:955`; `ttdexternalevents.h` (`DebuggerEdit`) |
| History / rewind / time travel | Input journal + external-event barriers | Keyboard matrix, Kempston mouse; tape control, disk write, debugger edit, reset | shipped | (internal) | `ttdinputjournal.h:3-4`; `ttdexternalevents.h:13-32` |
| History / rewind / time travel | `.ttd` save / load | Magic `TTDD`, schema v1, Kaitai description | shipped | Qt, CLI, Web, MCP, Lua, Py | `ttddumpformat.h:32-124`; `ttd.ksy:79-141` |
| History / rewind / time travel | Coverage index | Per-frame executed/written/read physical keys, zstd blocks, ≈307 B/frame; probe/scan/summary; prunes reverse search | shipped | CLI, Web, MCP, Lua, Py | `core/src/debugger/ttd/ttdcoverageindex.h:16-111` |
| History / rewind / time travel | Per-instruction frame cache | Full register set, opcode bytes, (SP), banks, memory/port writes for one frame | shipped | DZRP (history 0xE0/0xE1) | `core/src/debugger/ttd/timetravelframecache.h:24-90` |
| History / rewind / time travel | Timeline summary `GET /ttd/timeline` | O(limit) downsampled dirty pages / writes / key frames / markers | designed | Web, Qt | `docs/inprogress/2026-09-14-automation-triage-gaps/designs/ttd-timeline-summary-design.md`; PLAN #7 |
| History / rewind / time travel | TTD v2: device RAM regions, device table, determinism inputs, memory budget, chunked container + disk mode | V1-V5 | designed (V0 done) | all | `docs/inprogress/2026-09-25-ttd-v2-migration/migration-trajectory.md`; PLAN #40 |
| History / rewind / time travel | DeZog live history (non-destructive browse/resume) | Fix for "Reached end of instruction history" | designed | DZRP | `docs/inprogress/2026-08-27-dezog-integration/reverse-debugging.md` §6 |
| Video, raster & beam | Beam position report | line, T in line, beamX, vertical/horizontal zone, in paper, paper x/y, frame timing, raster | shipped | CLI, Web, Lua, Py, MCP (`timing`) | `core/src/emulator/video/screen.h:296-311`, `:890`; `screen.cpp:1094-1142`; `core/automation/webapi/src/api/state_screen_api.cpp:256-316` |
| Video, raster & beam | ULA beam widget | Raster with VSync/VBlank/HBlank areas; "Frame, T-state, Line, Pos, Pixel" and zone | shipped | Qt | `unreal-qt/src/debugger/widgets/ulabeamwidget.cpp:61-256` |
| Video, raster & beam | Border timing widget | Stub: records `t % frame`, border color hard-coded 0 | in progress (stub) | Qt | `unreal-qt/src/debugger/widgets/bordertimingwidget.cpp:23`, `:67-81` |
| Video, raster & beam | Mode-aware screen report | Mode, size, border, active/shadow screen and pages, contention, flash, 7FFD/EFF7/DFFD/FF77 latches | shipped | CLI, Web, MCP, Lua, Py | `core/src/emulator/state/devicestate.h:83-85`; `devicestate.cpp:1349-1478` |
| Video, raster & beam | Screen digest | Hash of the screen for regression checks | shipped | CLI, Web, MCP, Lua, Py | `emulator_api.h:207-212` |
| Video, raster & beam | Video mappers (beam→where, beam→fetch, pixel→memory, memory→pixels), pixel inspector, text-mode OCR | `IVideoMapper` + `VideoMapService` for ZX, AlCo, ATM, Profi HR | designed (phase 0 done) | Web, Qt, all | `docs/inprogress/2026-09-27-video-debug-translation/design.md` (`:187-195`, `:418-500`); PLAN #42 |
| Video, raster & beam | Beam-to-execution correlation, interrupt analyzer | — | designed | — | `docs/inprogress/2026-01-14-analyzers/`; PLAN #29 |
| Sound & device views | AY / TurboSound, TSFM, WD1793 + uPD765A, GS / NeoGS, Covox, MoonSound (FM + PCM), contention | `DeviceState::*` reports shared by all surfaces | shipped | CLI, Web, MCP, Lua, Py | `core/src/emulator/state/devicestate.h:35-96`; `devicestate.cpp:567-1518` |
| Sound & device views | FDC status and floppy widgets | "FDC A: H0 T00 S01", motor, drive/track/head/sector | shipped | Qt | `unreal-qt/src/debugger/widgets/fdcstatuswidget.cpp:21-44`; `floppydiskwidget.cpp:38-57` |
| Sound & device views | Beeper, ports map, paging, keyboard, mouse, tape status | Separate routes, not `DeviceState` | shipped | CLI, Web, MCP, Lua, Py | `emulator_api.h:72-74`, `:215`, `:228`, `:384-398` |
| Sound & device views | Device boards (beta128, ay, gs, neogs, sd, mp3, flash, tsconf) | Widget catalog `W.board.*` | designed | Qt, TUI, Web | `docs/inprogress/2026-09-28-debugger-model/widget-catalog.md` §4 |
| Sound & device views | Sound oscilloscope + audio-quality taps, `analyze_audio` MCP tool, VCD export | — | designed (draft) | Qt, MCP | `docs/inprogress/2026-09-16-sound-oscilloscope/architecture.md` §5, §9; PLAN #48 |
| Profiling, heat maps, coverage | Memory access tracker | R/W/X counters per Z80 address, per physical byte, per page; regions/ports with caller and value maps; frame/interrupt segments; YAML + "UZVD" dump | shipped | Qt (heatmap), CLI, Web, MCP, Py | `core/src/emulator/memory/memoryaccesstracker.h:37-464` |
| Profiling, heat maps, coverage | Memory heatmap window | R/W/X layers on 16K banks; page grid; toggles for tracking/call trace/opcode profiler | shipped | Qt | `unreal-qt/src/debugger/debugvisualizationwindow.cpp:27-39`, `:83`, `:349-419`; `widgets/memory16kbwidget.cpp:273-287` |
| Profiling, heat maps, coverage | Coverage analyzer | 64K executed bitmap, ranges, gaps | shipped | CLI, Web, MCP, Lua, Py | `core/src/debugger/analyzers/coverage/coverageanalyzer.h:15-72` |
| Profiling, heat maps, coverage | Frame cost | Work vs HALT-idle T-states per frame | shipped | CLI, Web, MCP, Lua, Py | `core/automation/webapi/src/api/profiler_api.cpp:1461-1464` |
| Profiling, heat maps, coverage | Unified profiler session | Start/stop/status all profilers; `profile_report` fan-out | shipped | Web, MCP, Py | `emulator_api.h:314-364`; `core/automation/mcp/src/mcp-analysis.cpp:358-623` |
| Profiling, heat maps, coverage | Contention statistics | Per kind fetch/read/write/io, in the contention report | shipped | CLI, Web, MCP, Lua, Py | `devicestate.cpp:1518`; `docs/inprogress/2026-09-28-m1-contention/design.md` §7.2; PLAN #61 |
| Profiling, heat maps, coverage | Realtime monitoring / segmentation widget | — | designed | Qt | `docs/inprogress/2026-02-23-realtime-monitoring/`; PLAN #28 |
| Analyzers | TR-DOS analyzer | Silent page-specific breakpoints in the DOS ROM + WD1793 observer; semantic events (entry/exit, command, file found, module load/save, FDC commands, sector transfer, errors, loader/protection detected); raw FDC + breakpoint layers | shipped | CLI, Web, Py | `core/src/debugger/analyzers/trdos/trdosanalyzer.h:40-113`; `trdosevent.h:9-146` |
| Analyzers | ROM print detector + screen OCR | RST 10 / PRINT-OUT / PRINT-A-2 capture (not registered at runtime); ROM-font OCR of the 32×24 grid | shipped (OCR) / in progress (detector) | CLI, Web, MCP, Py | `core/src/debugger/analyzers/rom-print/romprintdetector.cpp:33-35`; `screenocr.h:10-58` |
| Analyzers | Audio capture | Post-mix stereo PCM, ≤ 30 s at 96 kHz | shipped | CLI, Web, MCP, Lua, Py | `core/src/debugger/analyzers/audiocapture/audiocaptureanalyzer.h:14-57` |
| Analyzers | BASIC tools | Tokenizer/injector, detokenizer, EditorMonitor "cyclogram" of ROM editor events, ROM control points by signature, key decoder, verified command typer | shipped | Web, CLI, MCP (`type_input`), Py | `core/src/debugger/analyzers/basic-lang/*.h`; `commandtyper.h:19-127` |
| Analyzers | Analyzer framework | Activate/deactivate by name, events, raw layers, per-instruction / per-frame / audio subscriptions | shipped | CLI, Web, Py | `core/src/debugger/analyzers/analyzermanager.h:44-207`; `emulator_api.h:148-159` |
| Analyzers | GigaScreen analyzer, look-ahead divergence monitor, metadata triggers | ZX DLSS program | designed / in progress (Python POC, plane B worktree) | Web, MCP | `docs/inprogress/2026-09-27-zxdlss-gigascreen/design-analysis.md` §11; `tools/poc/019-zxdlss-gigascreen/`; PLAN #56 |
| Scripting, automation & remote debug | CLI | Telnet, 99 command names / 80 handlers; embedded `lua` / `python` exec | shipped | CLI | `core/automation/cli/src/automation-cli.cpp:24`; `cli-processor.cpp:80-228` |
| Scripting, automation & remote debug | WebAPI + OpenAPI + Swagger | 257 routes in `emulator_api.h`; OpenAPI assembled from 27 fragments; `/api/v1/docs` | shipped | Web | `core/automation/webapi/src/emulator_api.h:15-454`; `openapi_spec.cpp:155-187` |
| Scripting, automation & remote debug | MCP server + stdio bridge | 12 smart tools + `search_api`/`invoke_api`; 7 resources; progress over SSE | shipped | MCP | `core/automation/mcp/src/mcp-tools.cpp:2719-2745`; `mcp-resources.cpp:273-332`; `core/automation/mcp/bridge/src/main.cpp:22` |
| Scripting, automation & remote debug | Lua / Python | ≈210 Lua and ≈287 Python functions; scripts from CLI or WebAPI | shipped | Lua, Py | `lua_emulator.h`; `python_emulator.h`; `core/automation/python/src/automation-python.cpp:21` |
| Scripting, automation & remote debug | DeZog DZRP 2.2.0 | INIT, registers, memory, banks/slots, breakpoints, watchpoints, ports, state, + history extension 0xE0/0xE1 | shipped | DZRP | `core/automation/dezog/include/dzrptypes.h:8-60`; `dzrpserver.cpp:279-323` |
| Scripting, automation & remote debug | ZEsarUX ZRCP | Text protocol for DeZog `zrcp` mode incl. conditions, pass counts, cpu-history, code coverage | shipped | ZRCP | `core/automation/zesarux/src/zrcpserver.cpp:479-722` |
| Scripting, automation & remote debug | GDB RSP stub | `g/G/p/P/m/M/X/c/s/Z0-Z4`, `bs/bc`, qXfer target.xml with a paging feature, `monitor` commands | shipped | GDB | `core/automation/gdb/src/gdbserver.cpp:399-680`; `gdbtarget_z80.cpp:15-101` |
| Scripting, automation & remote debug | Event push (WebSocket subscribe, breakpoint callbacks) | Endpoint exists; `publishToSubscribers` has no caller | in progress (skeleton) | Web | `core/automation/webapi/src/emulator_websocket.cpp:69-97`; PLAN #23 |
| Scripting, automation & remote debug | `DebugService` + one serializer + `cpu=` selector + seq-numbered events | Debugger model protocol | designed | all | `docs/inprogress/2026-09-28-debugger-model/protocol.md` §0-§8 |
| Scripting, automation & remote debug | `unreal-devd`, DAP, LSP, VS Code extension, trigger engine | Devtools program | designed | new | `docs/inprogress/2026-09-21-devtools-roadmap/unreal-ng-developer-toolchain-design.md` §1, §9, §12; PLAN #51 |
| Scripting, automation & remote debug | Schema-driven generation of all surfaces | — | planned | all | PLAN #22 |
| Scripting, automation & remote debug | `GET /capabilities` | — | planned | Web | PLAN #9 |
| Scripting, automation & remote debug | Recipe library | 31 recipes: TTD, port trace, memory counters, protection forensics, media, machines | shipped (docs) | MCP, Web | `.recipe/README.md`; PLAN #34 |
| Import / export & persistence | Snapshots | `.sna`, `.z80` (v3) load/save | shipped | Qt, CLI, Web, MCP, Lua, Py | `core/src/emulator/emulator.cpp:1417`, `:1546` |
| Import / export & persistence | Media manager | One slot vocabulary (`list/info/insert/eject/create/save/export/discard/rescan/protect/swap`); TTD guard (409 while recording) | shipped | Qt, CLI, Web, MCP, Lua, Py | `core/src/emulator/media/mediamanager.h`; `docs/features/media.md`; PLAN #58 |
| Import / export & persistence | Media history (versions, diff, read-block at frame) | H1-H5 | designed | all | `docs/inprogress/2026-09-28-storage-manager/media-history-design.md` §6 |
| Capture | Screenshot | GIF/PNG, screen only or full framebuffer, base64 or file; Qt F12 to clipboard | shipped | Qt, CLI, Web, MCP, Py | `core/src/emulator/video/screencapture.h:19-58`; `unreal-qt/src/mainwindow.cpp:3301-3316` |
| Capture | Video/audio recording | Single/multi-track, channel split, audio-only; native (VideoToolbox, NVENC/MF) or ffmpeg | shipped | Qt, CLI, Web, MCP, Lua, Py | `core/recording/src/recordingmanager.h:25-77`; `unreal-qt/src/debugger/widgets/videorecordingwidget.cpp:901-956` |
| Capture | Raw framebuffer / numpy | — | planned | Web, Py | PLAN #26 |
| UI conveniences | HUD overlay | Toasts, indicators (fdd, speed, pause), stats, banners; not in recordings; Ctrl+Shift+H | shipped (r1) | Qt | `unreal-qt/src/hud/core/hudsnapshot.h:14-24`; `hudmodel.h:151-166` |
| UI conveniences | Feature gating | `debugmode`, `breakpoints`, `memorytracking`, `calltrace`, `opcodeprofiler`, `timetravel`, `porttrace`, `contention`… default off | shipped | CLI, Web, Lua, Py | `core/src/base/featuremanager.h:23-118` |
| UI conveniences | TUI debugger (Unreal Speccy and TSConf monitor layouts) | FTXUI POC with mock and REST backends | in progress (POC) | TUI | `tools/poc/018-tui-debuggers/README.md`; PLAN #49 |
| UI conveniences | Skins over one model (pixel-faithful Unreal, modern Qt, terminal, browser) | Debugger model | designed | all | `docs/inprogress/2026-09-28-debugger-model/README.md` "The idea" |

## 2. Automation surfaces and their parity

**Architecture.** Each surface is a thin client of the same core objects (`Emulator`, `DebugManager`, `TimeTravelManager`, `DeviceState`). The WebAPI is the widest one; MCP calls the WebAPI through a loopback HTTP client (`core/automation/mcp/src/webapi-client.cpp:33`), so every MCP tool is also a WebAPI route. Surfaces are built by default except Python (`ENABLE_PYTHON_AUTOMATION` OFF in `CMakeLists.txt:29` and `core/automation/CMakeLists.txt:18`; never set in `.github/workflows/cmake-ci.yml`, PLAN #19).

| Surface | Transport / default port | Entry point | Size today |
|---|---|---|---|
| CLI | raw TCP with telnet negotiation, 8765 | `core/automation/cli/src/automation-cli.cpp:24`, `:430-438` | 99 command names / 80 handlers (`cli-processor.cpp:80-228`) |
| WebAPI | HTTP (Drogon), 8090, `0.0.0.0` | `core/automation/webapi/src/automation-webapi.cpp:197`, `:334` | 257 routes in `emulator_api.h` + 8 interpreter routes (`include/interpreter_api.h:29-52`) |
| OpenAPI | `/api/v1/openapi.json`, Swagger at `/api/v1/docs` | `openapi_spec.cpp:155-187`, 27 fragments in `src/openapi/` | hand-maintained; checker `tools/verification/webapi/verify_openapi_coverage.py` |
| MCP | Streamable HTTP `/mcp` on 8092 (also 8090); stateless; SSE only for progress | `core/automation/mcp/src/automation-mcp.cpp:107`, `:135-199`, `:279` | 12 smart tools + 2 router tools; 7 resources; no prompts (`mcp-dispatcher.cpp:229-243`) |
| MCP bridge | stdio ↔ HTTP pipe, `--url` | `core/automation/mcp/bridge/src/main.cpp:22-28` | — |
| Lua | sol2, embedded | `core/automation/lua/src/automation-lua.cpp:193-217` | ≈210 functions + 17 `porttrace_*` (`bindings/lua_porttrace.h:134`) |
| Python | pybind11 embedded module `unreal_emulator` | `core/automation/python/src/automation-python.cpp:21`, `:364-417` | ≈287 functions, direct `BreakpointManager` object (`:392`) |
| DeZog DZRP | TCP 12000 (`UNREAL_DEZOG_PORT`) | `core/automation/dezog/include/dzrptypes.h:13` | §3 |
| ZEsarUX ZRCP | TCP 10000 (`UNREAL_ZRCP_PORT`) | `core/automation/zesarux/include/zrcptypes.h:16` | §3 |
| GDB RSP | TCP 2000 | `core/automation/gdb/include/automation-gdb.h:52` | §3 |

**MCP tools** (`mcp-tools.cpp:2719-2745`):
- `emulator_manage`: create, switch_model, list, list_models, server, status, start/stop/pause/resume/reset/destroy, and `gs_*` card controls (`:100`, `:160`).
- `load_software`: path, drive, play, autostart (`:435`).
- `control_execution`: run, pause, resume, step, step_n, step_over, step_out, run_frame(s), run_tstates, run_to_interrupt, bp_add/remove/enable/disable/clear/list (`:587`, `:613`).
- `inspect_state` with 30 aspects: machine, registers, memory, memory_map, disasm, stack, breakpoints, memory_banks, paging, ports, video, screen, screen_flash, screen_ocr, screen_image, screen_digest, timing, rom, the `audio_*` family, fdc, mouse, ttd, contention (`:884`, `:937`).
- `type_input` and `mouse_input` (`:1693-1858`).
- `time_travel` with every TTD verb (`:2154`, `:2254`).
- `manage_symbols` (`mcp-symbols.cpp:32`, `:58`), `debug_code` (disassemble, assemble, find_bytes, trace; `mcp-analysis.cpp:39`, `:78`), `analyze_performance` (`:313`, `:343`), `capture_media` (`mcp-media.cpp:69`, `:117`), `media` (`mcp-slots.cpp:25-37`, `:150`).
- The router tools `search_api` and `invoke_api` expose the whole WebAPI through its OpenAPI (`mcp-router.cpp:499`, `:630`).

**Parity.** The core objects are shared, so the same feature returns the same data everywhere. The per-surface wiring is still written by hand: PLAN #22 (schema-driven generation) is deferred, and PLAN notes that "#42a just touched 4 surfaces for one feature". Known holes:

| Feature | Qt | CLI | Web | MCP | Lua | Py |
|---|---|---|---|---|---|---|
| Register write | — | ✓ | ✓ | — | ✓ | ✓ |
| Memory edit | — | ✓ | ✓ | via `invoke_api` | ✓ | ✓ |
| Keyboard injection | host keys | ✓ | ✓ | ✓ | — | ✓ |
| Call trace / opcode / memory profiler | toggles + heatmap | ✓ | ✓ | ✓ | — | ✓ |
| Analyzer manager | — | ✓ | ✓ | partial | — | ✓ |
| TTD instruction stepping, reverse search, bookmarks, coverage | — | ✓ | ✓ | ✓ | ✓ | ✓ |
| Labels, listing, assembler | labels only | ✓ | ✓ | ✓ | ✓ | ✓ |
| Device state reports | FDC only | ✓ | ✓ | ✓ | ✓ | ✓ |

(Rows are from `lua_emulator.h` / `python_emulator.h` binding names and `unreal-qt/src` greps; `docs/inprogress/2026-08-26-automation-gaps/feature-parity.md` is partly stale, see §22.)

**Documentation.** `docs/emulator/design/control-interfaces/` holds the specs: `command-interface.md` (4328 lines, the unified command set with many "Planned" sections), `cli-interface.md`, `webapi-interface.md`, `lua-interface.md`, `python-interface.md`, `gdb-protocol.md`, `udb-protocol.md` (a custom binary protocol, "Planned Q3-Q4 2026"). The user-facing references are `docs/features/automation.md`, `docs/features/media.md` and `docs/features/mcp/`.

## 3. Remote debug protocols

**DeZog DZRP 2.2.0** (`core/automation/dezog/`):
- Commands handled (`src/dzrpserver.cpp:279-323`): 1 INIT, 2 CLOSE, 3 GET_REGISTERS, 4 SET_REGISTER, 5 WRITE_BANK, 6 CONTINUE, 7 PAUSE, 8 READ_MEM, 9 WRITE_MEM, 10 SET_SLOT, 12 SET_BORDER, 20 READ_PORT, 21 WRITE_PORT, 24 GET_SUPPORTED_COMMANDS, 40/41 ADD/REMOVE_BREAKPOINT, 42/43 ADD/REMOVE_WATCHPOINT, 50/51 READ/WRITE_STATE.
- unreal-ng extension: 0xE0 GET_HISTORY_INFO and 0xE1 GET_HISTORY_ENTRY (`include/dzrptypes.h:48-60`). DeZog's backward stepping reads the TTD frame cache, recorded in `DebuggerLive` mode (`src/dezogdebugadapter.cpp:1018`, `:1139`). The feature is gated by `UNREAL_DEZOG_HISTORY` (`include/dezogdebugadapter.h:120`).
- Not implemented: TBBLUE_REG (11), sprite commands (16-19), EXEC_ASM (22), INTERRUPT_ON_OFF (23). They get an empty ACK.
- DeZog end-to-end, including backward debugging, was verified by hand on 2026-09-16 (`docs/inprogress/2026-08-27-dezog-integration/TODO.md`). Only the user guide is left (PLAN #17).

**ZEsarUX ZRCP** (`core/automation/zesarux/src/zrcpserver.cpp:479-722`):
- Covers enough of ZEsarUX for DeZog's `zrcp` mode: registers, memory, disassemble, cpu-step/step-over, breakpoints with actions and pass counts (`:605`), memory breakpoints, cpu-history, extended-stack, cpu-code-coverage, memory pages, partial T-state counters.
- It is the only surface today with conditional breakpoints. The dialect: `or/and/not`, comparisons, `+ - * /`, registers, `PEEK/PEEKW`, and a parse failure evaluates to true (`include/zesaruxcondition.h:1-55`).

**GDB RSP stub** (`core/automation/gdb/src/gdbserver.cpp`):
- Packets: `? g G p P m M X c C s S bs bc Z z D k H T QStartNoAckMode` (`:399-473`).
- Breakpoints and watchpoints: Z0/Z1 are execution breakpoints; Z2/Z3/Z4 are write/read/access watchpoints over a range (`:1313-1398`).
- Capabilities: qSupported advertises PacketSize=4000, qXfer features/osdata/threads, swbreak/hwbreak, `vContSupported`, and `ReverseStep+`/`ReverseContinue+` when TTD is on (`:543-561`).
- Target description: target.xml has `org.gnu.gdb.z80.cpu` (regs 0-14) and `org.gnu.gdb.z80.paging` (`p7ffd`, `p1ffd`, `pfe`) (`src/gdbtarget_z80.cpp:15-101`).
- `monitor` commands: help, model, status, reset, instances, frame, bankinfo, load, `ttd status|start|stop|seek|findlast`, bport (`:665-680`).
- Tests: `core/tests/gdb/gdbpacket_fuzz_test.cpp`, `core/automation/gdb/tests/test_gdb_integration.py`.

**Planned.** The debugger model runs one GDB and one DeZog instance per CPU: GDB on 2000/2001, DeZog on 12000/12001 (`docs/inprogress/2026-09-28-debugger-model/protocol.md` §6.6). The devtools program adds a DAP adapter with stepBack and reverseContinue over TTD, with OS tasks and the GS Z80 as threads (`unreal-ng-developer-toolchain-design.md` §12).

## 4. CPU, registers and memory views (Qt)

**The debugger window** opens with Ctrl+1 (`unreal-qt/src/menumanager.cpp:857`). It holds the registers, memory pages, stack, hex view, disassembler and FDC status widgets (`unreal-qt/src/ui/debuggerwindow.ui:43-167`).
- Toolbar: Continue/Pause; Step In F11, Step Over F10, Step Out Shift+F11, Frame step F9 (`debuggerwindow.cpp:85-102`).
- "Wait INT" and the "Adv Step" menu: Run T-States, Run to Scanline (0-319), Run N Scanlines, Run to Screen Pixel, Run to Interrupt (`:110-114`).
- Also on the toolbar: Reset, Labels, Breakpoints, Visualization.
- A speed-control strip sits under the toolbar (`:175-181`). UI refresh is skipped while the machine runs (`:550`).

**What the Qt debugger does not do:**
- Every widget is read-only: registers, memory, disassembly (`disassemblerwidget.cpp:47`) and stack. Registers have no change highlighting (`registerswidget.cpp:208-255`).
- No navigation history.
- No watch window.
- No device panels other than the FDC. No AY, GS or FM panels in Qt, although `DeviceState` provides them to the other surfaces.
- The stack view shows only 4 entries.

**Planned.** Xpeccy parity (PLAN #46, `docs/inprogress/2026-08-26-debugger-enhancements/`) lists nine "High Priority (Parity)" rows, all TODO (`proposal.md:22-32`): stack depth 9, address history, marks 1-5, go-to-PC, follow operand, flag checkboxes, IFF1/IFF2 + ISR, signal indicators, port watch. The phases (`features-parity.md` "Implementation Phases"):
1. Navigation.
2. Stack.
3. Flags, IFF and ISR popup.
4. System status: slots, signals, frame/T, beam.
5. Port watch: PortRegistry, decode masks, change highlighting.

A new uncommitted section, "Open design questions (added 2026-09-28)" (`features-parity.md:154-180`), asks about paging for history and marks, and whether marks should be LabelManager labels. The debugger model (§15) replaces the per-widget view with a widget catalog: `W.regs`, `W.disasm`, `W.mem`, `W.pages`, `W.stack`, `W.calls`, `W.watch`, `W.time`, `W.pchist` per CPU (`widget-catalog.md` §1).

## 5. Disassembler, assembler and listings

- **Disassembler** (`core/src/debugger/disassembler/z80disasm.*`).
  - Table-driven over all seven prefix groups, with undocumented opcodes; the tables cite z80undoc (`z80disasm.cpp:20-21`).
  - `OpCode` holds `t`, `met_t` and `notmet_t` (`z80disasm.h:21-28`), but no surface shows the timings.
  - `DecodedInstruction` carries jump/call/ret/rst/block/IO/djnz flags, targets, a displacement, and label/annotation/comment strings (`z80disasm.h:31-143`).
  - Runtime mode reads the live registers to predict branches (`z80disasm.cpp:2094-2249`).
  - Step helpers: `shouldStepOver`, `getNextInstructionAddress`, `getStepOverExclusionRanges` (`z80disasm.h:322-328`).
- **Control-flow decoder** (`z80cfdecoder.h:3-57`): about 5-15 ns per instruction; this is what keeps the call trace cheap.
- **Assembler.** `Z80TextAssembler` is two-pass, with ORG, EQU/`=`, DB/DEFB/BYTE, DW/DEFW/WORD and DS/DEFS/BLOCK.
  - Literals: `0x`, `$`, `#`, `%`, `0b`, char. Operators: `+ - * / & | ^ << >> ~`.
  - Not supported: macros, IF, INCLUDE, undocumented mnemonics (`z80textassembler.h:39-55`).
  - `POST /assemble {code, address, write?}` can write the bytes back (`debug_api.cpp:3723-3840`).
  - The legacy Unreal Speccy `Z80Assembler` (`z80asm.h:9-15`) has no users.
- **Listings.** `ListingParser` reads sjasmplus `.lst` rows (`listingparser.h:31-85`): a flat 64K address→line map, one source path, and no bank or include mapping.
  - Surfaces: `listing load/source_at/step_line/run_to_line` on CLI, Web, MCP, Lua and Py. Not used by Qt, GDB or DZRP.
- **Planned.** `libunreal-debuginfo` is a normalized debug-info model (SLD v1, `.lst`, `.sym`, `.cdb`/`.adb`) with page-aware line lookup and staleness hashes (`unreal-ng-developer-toolchain-design.md` §8.1-§8.3). Source triggers `@break @log @watch @assert @budget @trace` would come from SLD `K` records (§9.2). Both belong to PLAN #51.

## 6. Symbols and labels

**Model.** `LabelManager` keeps one map keyed by Z80 address plus a name map (`labelmanager.h:57-58`). `GetAllLabelsAtAddress` exists (`:93`), but every lookup, including symbolic disassembly, uses the Z80 address only and ignores the bank.

**Formats.** The loader picks a parser by file extension (`labelmanager.cpp:449-463`) or by sniffing the content for "Memory map" or VICE `al` (`:465-484`).

| Format | Status | Note |
|---|---|---|
| Generic `.map` `ADDR NAME [(TYPE)] [;comment]` with `RAM2:4000` / `ROM1:0000` prefix | works | `:496-606`; the Z80 address is the offset as written |
| Simple `.sym` `ADDR NAME` | works | `:628` |
| sjasmplus `--sym` (`label: EQU 0x…`) | **broken** | `.sym` goes to the simple parser, which fails the hex parse and skips the line silently but returns true (`:683-691`) |
| SJASM `NAME EQU $ADDR` | only as `.s` / `.asm` | `:753-805`; no tests |
| VICE `al C:addr .name` | works | `:701`; `.lbl` only if sniffed |
| z88dk `DEFC name = $addr` | only as `.z88` | `:812`; the real z88dk `.map` is not supported |
| sjasmplus `.sld`, SkoolKit, MAME, Unreal `user.l` / `sos.l`, XAS/ALASM in-RAM tables | not supported | SLD postponed (`docs/emulator/design/debugger/label-manager.md` task 0.2.2); Unreal formats in `docs/inprogress/2026-09-24-tui-debugger/TDD-DBG-01_unreal-speccy-debugger-tui.md` §11 |

**Planned.**
- The GS debugger adds per-CPU label sets keyed by physical address (`ROM0:0038`), loaded from `data/symbols/gs/` and chosen by ROM SHA-256 (`docs/inprogress/2026-09-27-gs-debugger/design.md` §3.3, §6.2).
- The debugger model adds `?cpu=` to every label route (`protocol.md` §2).

## 7. Breakpoints today

**Descriptor** (`breakpointmanager.h:52-80`): `breakpointID` (uint16), `keyAddress`, `type` (MEMORY / IO / KEYBOARD), `matchType` (ADDR / BANK_ADDR), access bits, `z80address`, `page`, `pageType` (ROM/RAM/CACHE), `bankOffset`, `active`, `hidden`, `owner`, `note`, `group`.

**Keying** (`breakpointmanager.cpp:1404-1411`, `:1477-1518`):
- An any-bank breakpoint has the key `0xFFFF0000 | addr`.
- A page breakpoint has the key `(pageType<<24) | (page<<16) | z80address`.
- Lookup maps the address to its physical page, tries the page key first, then the any-bank key.
- Only one descriptor exists per key. A duplicate add returns the existing ID (`:1416-1421`).
- The page key embeds the Z80 address, not the offset in the page, so a page breakpoint only matches while its page sits in the same slot (*(inferred)*; confirmed as "key fix needed" in `docs/inprogress/2026-08-17-conditional-breakpoints/design.md` §5.2).

**Hot path (Phase 0, shipped 2026-08-19):**
- `BreakpointHotState` holds per-kind flags (`hasExec/hasRead/hasWrite/hasPortIn/hasPortOut`) and a 64 KB byte filter with exec/read/write bits (`breakpointmanager.h:120-132`). `RebuildFilters()` rebuilds both after every change (`cpp:1549-1600`).
- Each `Handle*` call runs these checks in order (`:1157-1353`):
  1. Return at once during TTD replay (`ttdReplayActive`).
  2. Test the kind flag.
  3. Test the address bit.
  4. Only then do the map lookup.
- Measured miss path: 16.5→1.4 ns, 17→1.2 ns and 25→1.4 ns (12-18×). The hit path stays ≈ 7 ns (`docs/inprogress/2026-08-17-conditional-breakpoints/implementation-plan.md` §2).
- The benchmark gates at ≤ 5 ns for a miss and ≤ 200 ns for a hit (`core/tests/benchmarks/breakpoint_hotpath_bench.cpp:14-32`).
- One level up, the checks run only in debug mode. `Z80::RunInstructionStartHooks` tests `cpu.isDebugMode && !skipBreakpoints` (`z80.cpp:262`). Memory checks run only when `debugMode && kBreakpoints` (`memory.cpp:102`). The fast memory interface is swapped in when debug mode is off (`core/src/base/featuremanager.cpp:644-653`).

**On a hit** (`z80.cpp:276-316`):
1. `AnalyzerManager::dispatchBreakpointHit` always runs.
2. An analyzer-owned breakpoint does not pause.
3. Any other breakpoint calls `emulator.Pause()`, posts `NC_EXECUTION_BREAKPOINT(id, addr, isHidden)` to the MessageCenter, and waits.

There are no hit counts, conditions, actions or script callbacks.

**Management:**
- Add, get, remove by ID / address / port / type (`breakpointmanager.h:180-270`).
- Activate or deactivate one, all, by type, by memory type or by I/O type (`:254-263`).
- Groups (`cpp:948-1047`).
- IDs are `max+1` and throw at 0xFFFF (`:1362-1385`).

**Front-ends:**
- CLI: `bp`, `bplist`, `wp`, `bport`, `bpclear`, `bpgroup`, `bpon`, `bpoff` (`cli-processor.cpp:110-119`).
- WebAPI `/breakpoints/*` (`emulator_api.h:284-290`).
- MCP `control_execution` bp actions.
- Lua/Py `bp`, `bp_read`, `bp_write`, `bp_port_in`, `bp_port_out`, …
- DZRP 40-43; GDB Z0-Z4 (owner "gdb").
- Qt: breakpoint dialog, editor (Memory / Port / Keyboard, R/W/X or In/Out, group, note, active; no condition or hit field) and group dialog.

## 8. Breakpoints after the planned engine (PLAN #6, #46)

Three designs feed PLAN #6, the only P0 left in the feature-parity matrix and the piece other work reuses most (PLAN "Sequencing rationale" 4):

- **Conditional breakpoints** (`docs/inprogress/2026-08-17-conditional-breakpoints/`).
  - Features F1-F13 (`design.md` §3): conditions, ranges, slot filter, physical-page breakpoints, hit-count policies (always / ==N / every N / ≥N), an ERROR state, port masks, ΔT, persistence, canned helpers, screen regions, device triggers, DeZog fast conditions.
  - Grammar (`design.md` §4.2), compiled to RPN bytecode with fast predicates:
    - Operands: registers including primed ones; flag names (`CY` is carry, bare `C` is the register); `(expr)` and `(expr).w` memory reads; `MRA MWA MRV MWV PRA PWA PRV PWV`; `ADDR VAL PORT`; `PG0-3 ROMPG DOS SHADOW`; `T FRAME DT`; `IFF1 IFF2 HALTED`; labels.
    - Literals: `$`, `#`, `0x`, `h`.
    - Precedence, highest first: `! ~` → `* / %` → `+ -` → `<< >>` → `& ^ |` → comparisons → `&& ||`.
  - Slices (`implementation-plan.md` §3): 1a key re-encoding, 1b per-page slices, 1c ranges, 1d `bpcondition.{h,cpp}`, 1e wiring into `Handle*`, 1f hit counters + ΔT, 1g port masks, 1h front-ends. None are done. Still open from Phase 0: the BANK_CACHE page bug and a macro benchmark.
- **Expression evaluator** (`docs/inprogress/2026-08-26-expression-evaluator/design.md`). An xpeccy-`xexpr`-style evaluator in `core/src/debugger/expression/`.
  - Opcodes NUM, REG, LABEL, VAR, unary, `MRDB`/`MRDW`, and RAYHIT.
  - Variables RD, WR, MDT, IN, OUT, VAL, DOS, SLOT0-3, FRAME, RAYX, RAYY, HITS.
  - Name lookup order is register → pseudo-variable → label, and `.name` forces a register.
  - Adds `hitCount`, `skipCount`, `edgeTrigger` and the actions BREAK / LOG / SCREENSHOT / COUNT to the breakpoint descriptor.
- **Breakpoint enhancements** (`docs/inprogress/2026-08-26-breakpoint-enhancements/design.md`):
  - §1 ranges (`z80addressEnd`).
  - §2 conditions plus hit/fire/skip counts, edge trigger and actions.
  - §3 `BRK_IRQ` for INT and NMI.
  - §4 port masks.
  - §5 a global `BRK_CONDITION` checked on every instruction.
  - JSON serialization with `bpx.ini` compatibility.
  - Priority order: ranges → hits → conditions → IRQ.
- **Debugger model rules** (`docs/inprogress/2026-09-28-debugger-model/rules.md`):
  - §5.3 keeps the Unreal operators and adds `W(x)`, `T`, `FRAME`, `MPAG`, `GSCFG0`, `CMD/DATA/STATUS`, cross-CPU `main.X` / `gs.X`, and hit counts.
  - §5.4 adds event and protocol breakpoints (int/nmi, reset, page_switch, clock_switch, DMA events, host_command, host_data, card_command_read).
  - §5.5 stores breakpoints as a classic `bpx.ini` import plus native JSON.
- **Consumers waiting on #6:**
  - `run_until_condition` as an expression;
  - DeZog fast conditions;
  - the trigger engine (#51, roadmap `02` §4-§13), with actions observe / annotate / mutate / control / delegate and live plus retroactive TTD back ends;
  - the NedoOS struct DSL;
  - ZX DLSS triggers (TR-1..TR-13 in `docs/inprogress/2026-09-27-metadata-manager/trigger-integration.md` §2).

## 9. Execution control

- **Stepping primitive.** `ExecuteStep` runs `Z80::StepInstruction` and then the frame-boundary work (`emulator.cpp:2015-2036`).
  - Step in skips breakpoints (`:2046-2063`).
  - `RunNCPUCycles` counts instructions despite its name (`:2065-2085`).
  - Frame step returns to a persistent T-state target to avoid drift (`:2087-2143`).
  - `RunNFrames` skips breakpoints by default (`:2145-2192`).
  - `RunTStates`, `RunUntilScanline`, `RunNScanlines`, `RunUntilNextScreenPixel`, and `RunUntilInterrupt` (limited to 2 frames) are at `:2194-2439`.
- **Step over** (`:2486-2631`).
  - For CALL, RST, block instructions and DJNZ, it temporarily disables exec breakpoints inside the exclusion ranges: the called routine (analyzed 5 levels deep), the DJNZ loop body, or the instruction's own bytes.
  - It then sets a hidden "StepOver" breakpoint and resumes without blocking.
  - `CancelPendingStepOver` cleans up orphans (`:1993-2013`).
- **Step out** (`:2636-2719`) runs `RunUntilCondition(sp >= entrySP && IsReturnInstruction)` with a limit of 100 frames, then steps once.
- **Not in core:** run to cursor (the Qt menu entry is disabled at `menumanager.cpp:884-887`), run to line only through listings, and no "run until expression".
- **Hotkeys.** The main menu and the debugger window disagree:
  - Menu: Resume F7, Step In F8, Step Over F10, Step Out Shift+F8, Run to Cursor F9 (disabled), MNI F11 (`menumanager.cpp:551`, `:744`, `:867-885`).
  - Window: Step In F11, Step Over F10, Step Out Shift+F11, Frame step F9 (`debuggerwindow.cpp:85-102`).
  - The comment at `menumanager.cpp:740-742` documents that the window deliberately takes over F11.
- **Planned.** The GS debugger's `StepCoordinator` runs every step and run request on the emulation thread for all CPUs. It adds main-CPU instruction progress (elapsed/total T), clock-ratio changes and a determinism check S8 (`docs/inprogress/2026-09-27-gs-debugger/design.md` §5.3-§5.6).

## 10. Time-travel debugging (TTD)

**Current state** (`core/src/debugger/ttd/`; design `docs/emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md` v2.2).

- **Sessions.** The states are Idle, Recording and Detached (`timetravelmanager.h:79-84`).
  - Two record modes: `Session` for the scrubber and automation, and `DebuggerLive` for DeZog, which never stops (`:111-115`).
  - `TTDSessionInfo` reports checkpoint count, page-store bytes, session heap, key/delta counts, compression ratio, write-journal completeness and gap reason, provenance, coverage index size, bookmarks, and the last drop reason (`:118-213`).
- **Storage.**
  - One checkpoint per frame, with a key frame every 50 (`:320`).
  - 4 KB pages stored Full / XorPrev / Zero with zstd-1 and CRC32C (`ttdcodecpagestore.h:1-44`).
  - A dirty bit per 16 KB page (`ttddirtytracker.h`).
  - Model-specific peripheral blobs, up to 64 per checkpoint (`ttdperipheralregistry.h`; `ttddumpformat.h:124`). Serializers exist for ATM/Evo paging, the Evo SD card, the +3 FDC and paging, Profi paging, and the Scorpion ProfROM (`ttd/atm`, `plus3`, `profi`, `scorpion`).
  - A 64 MB write-journal ring of 12-byte records with 40-bit time (`timetravelmanager.cpp:231`, `ttdwritejournal.h:14-56`).
  - No memory budget is enforced yet (`ttdcodecpagestore.h:179`).
- **Time.** Positions are counted at the model's top CPU clock. Positions inside a frame are reached by replay, down to the T-state (`timetravelmanager.h:714-729`).
- **Navigation.**
  - Seek, and step back/forward by frame, instruction, N instructions or N T-states (`:698-1202`).
  - Reverse-step picks its strategy by N (`kReverseSeqStepMaxN=4`, `kReverseM1ListLargeN=64`, `:1175-1176`).
  - `ReverseContinue` over a PC set reports the window it searched (retired PLAN #25).
  - `FindLastAccess` searches for write/read/exec/IO with value, PC-range and physical-page filters (`ttdprobe.h:60-87`). It has no register-value condition.
  - Coverage probe, scan (limit 200) and summary (limit 100) (`timetravelmanager.h:1100-1118`).
  - Bookmarks (`:871-895`).
- **Rendering.** Seeking to a T-state shows the raster up to that point (`:1465-1477`, `3cbfbd39`).
- **Rules that keep replay honest:**
  - While a user recording runs, these actions are refused: snapshot/tape/disk/ROM load, disk create, invalidate, turning off TTD or debug mode, changing the write journal, and switching the GS card (`:215-227`).
  - A reset stops the recording but keeps the history.
  - Devices TTD cannot follow (SD, IDE) call `RequestInvalidation` (`:357-362`).
  - External events (tape control, disk write, debugger edit, reset) are replay barriers: a seek stops at one (`ttdexternalevents.h:13-32`).
  - Debugger memory edits go through `Emulator::EditMemoryFromTool` (`emulator.h:201`) and are journaled.
- **Surfaces:**
  - WebAPI: 22 routes (`emulator_api.h:403-427`).
  - CLI `ttd` (`cli-processor-ttd.cpp:78-154`).
  - MCP `time_travel`, plus the `inspect_state` `ttd` aspect (retired PLAN #2).
  - Lua/Py `ttd_*`.
  - GDB `bs`/`bc` and `monitor ttd`.
  - DZRP history 0xE0/0xE1.
  - Qt:
    - A toolbar toggle with a breathing icon and a tooltip (timecode, frame range, memory) (`unreal-qt/src/toolbarmanager.cpp:98-107`, `:317-380`).
    - The TTD panel: record, load, export, clear, |<, −1F, slider, +1F, >|, "Rec From Here", and a status line with frame range, memory and journal coverage (`unreal-qt/src/widgets/ttdwidget.cpp:97-212`, `:386-453`).
    - The Run menu disables turbo and fast modes while recording (`menumanager.cpp:1205-1231`).
  - The Qt panel has **no** instruction step, reverse continue, find-last, bookmarks or coverage.
- **Docs:** `time-travel-ux.md` (Qt UX draft), `overhead-and-gating.md` (measured cost), `ttd-container-format.md` (§2 is what ships), `ttd-use-cases.md`, `gdb-reverse-debugging-tdd.md`, `implementation-plan.md`. The TTD docs truth pass (retired PLAN #1) rebuilt the TTD sections of the interface references from the real handlers.
- **Tests:** about 40 files in `core/tests/debugger/ttd/`, including a state-completeness test and the `TTD_Corpus_Test`.

**Planned.**
- **PLAN #40, TTD v2** (`docs/inprogress/2026-09-25-ttd-v2-migration/`, V0 retired 2026-09-28):
  - V1: device RAM as regions (GS RAM, MoonSound wave SRAM, NeoGS/TSConf/ZX-Poly), with a per-piece chain cap instead of whole-RAM key frames.
  - V1b: checkpoints inside a frame (conditional).
  - V2: a device table.
  - V3: determinism inputs stored in the file.
  - V4: a memory budget.
  - V5: a chunked, checksummed, versioned container plus disk mode.
  - V6: cleanup.
- **PLAN #7:** `GET /ttd/timeline`. A 2026-09-28 note in the design says two of its data sources do not exist, so it needs revising first (`ttd-timeline-summary-design.md:9-16`).
- **GS debugger:** card reverse-step (checkpoint, then replay to instruction N−1), a card write journal, and card breakpoints in reverse search (`gs-debugger/design.md` §8).
- **Devtools:** edit-and-replay (rebuild, restore checkpoint N, inject the binary, replay the input journal, report the first divergence at a source line) (`unreal-ng-developer-toolchain-design.md` §13.1).
- **Media history:** TTD v2 checkpoints would reference media versions (`media-history-design.md` §7).

## 11. Tracing, profiling and coverage

| Tool | Feature flag | Records | Output | Code |
|---|---|---|---|---|
| Memory access tracker | `memorytracking` / `memtrack` | uint32 R/W/X per Z80 address, per physical byte, per page; page activity bitmaps; regions/ports with caller and value maps (≤ 100 each); Frame/Interrupt/Custom segments | YAML, binary "UZVD" visualization dump, text reports | `core/src/emulator/memory/memoryaccesstracker.h:37-464` |
| Call trace | `calltrace` / `ct` | Taken control transfers with m1_pc, target, opcode bytes, F, 4 bank mappings, SP, 3 stack words for returns, loop_count; hot buffer 1024, cold 1M → 1 GiB | YAML (`idx, m1_pc, type, target, flags, sp, opcodes`) | `core/src/emulator/memory/calltrace.h:20-161`; `calltrace.cpp:357-380` |
| Opcode profiler | `opcodeprofiler` / `op` | Counters for 1792 opcode variants; 10,000-entry trace ring | YAML with `recent_trace:` | `core/src/emulator/cpu/opcode_profiler.h:16-65`; `opcode_profiler.cpp:362` |
| Port Diagnostic Recorder | `porttrace` / `pt` | 24-byte events with 0x17 device ids (ULA, 7FFD, 1FFD, AY, WD1793, Beta128, Covox, ATM, Evo, GS, SD…), flags (IN/OUT, decoded, handler, Beta128-gated, TR-DOS active, full-decode claim); per-frame `PortActivitySummary` | JSON `unreal-ng-porttrace-v1`, CSV, `PTRC`, zstd `PTR2`; reload of the binary formats | `core/src/emulator/ports/portdiagrecorder.h:29-287` |
| GS port trace | `porttrace_gs` / `ptgs` | Host and card-side GS ports | events | `core/src/emulator/sound/chips/gs/gsporttrace.h` |
| Coverage analyzer | (analyzer) | 64K executed bitmap, instruction count | ranges, gaps | `core/src/debugger/analyzers/coverage/coverageanalyzer.h:15-72` |
| TTD coverage index | `SetEnableCoverageIndex` | Executed/Written/Read physical keys per frame | probe/scan/summary | `core/src/debugger/ttd/ttdcoverageindex.h:58-111` |
| Frame cost | — | Work vs HALT-idle T-states | JSON | `profiler_api.cpp:1461-1464` |

Enabling `breakpoints`, `calltrace`, `memorytracking` or `timetravel` also turns on `debugmode` (`featuremanager.cpp:172-194`). Every debug feature is off by default (`:395-490`).

Port attribution for ATM710 and ZX-Evo was finished in retired PLAN #8. The recipes are `.recipe/analysis/port-trace.md` and `memory-counters.md`.

Nothing writes a continuous per-instruction trace log to a file. The closest things are the opcode ring (last 10,000 instructions), the call trace (control flow only) and the TTD frame cache (one frame, in memory).

**Planned:**
- A heat map in the Qt debugger (Xpeccy parity, low priority).
- Change-tracking heat maps for disks (PLAN #33).
- Flame graphs (PLAN #32).
- Interrupt analyzer, routine classifiers, block segmentation and beam-to-execution correlation (PLAN #29, `docs/inprogress/2026-01-14-analyzers/`).
- Realtime segmentation widget (PLAN #28).

## 12. Analyzers

- **Framework** (`analyzermanager.h`):
  - Register and activate analyzers by name.
  - Hot-path subscriptions use raw function pointers: CPU step, memory read, memory write (`:149-166`).
  - Warm-path subscriptions use `std::function`: video line, audio sample (`:174-181`).
  - Silent breakpoints are owned by `"analyzer_manager"` and removed automatically (`:189-207`, `:307`).
  - The first breakpoint request turns on the `breakpoints` feature (`analyzermanager.cpp:43-55`).
  - Frame dispatch: `mainloop.cpp:548`, `:754`. CPU-step dispatch runs even outside debug mode when there are subscribers (`z80.cpp:327-335`).
  - Built-in analyzers: `trdos`, `coverage`, `aylog`, `audiocapture`, `editor-input` (`core/src/debugger/debugmanager.cpp:38-46`).
- **TR-DOS analyzer** (docs: `docs/emulator/design/debugger/analyzers/trdos/tdd-use-cases.md`):
  - Silent breakpoints in the DOS ROM at $3D00, $3D03, $3D13, $3D1A, $3D21, $3D2F and $0077, plus the internal $030A, $02CB and $02EF, page-specific with a fallback (`trdosanalyzer.h:48-60`; `.cpp:234-252`).
  - Also a WD1793 observer.
  - Keeps 4096 raw and 2048 semantic events.
  - Query by time with `getEventsSince(tstate)`.
- **ROM print detector:** captures RST 10, PRINT-OUT and PRINT-A-2 output as lines of text. It is used only in tests and not registered at runtime. **Screen OCR** matches the ROM font against the 32×24 grid and serves `capture/ocr` and MCP `screen_ocr`.
- **BASIC tools:**
  - EditorMonitor records the ROM editor's control points (≈ 24 points, found by ROM signature for 48 BASIC, 128 editor, +3, +2 and TR-DOS) as a "cyclogram" of up to 8192 events (`editormonitor.h:15-71`; `romcontrolpoints.h:29-67`).
  - CommandTyper types through the keyboard matrix and checks every key against those events (`commandtyper.h:19-127`).
  - ZxKeyDecoder inverts the ROM key tables (`zxkeydecoder.h:14-66`).
  - BasicEncoder and BasicExtractor tokenize and detokenize programs (`/basic/*`).
- **Audio capture and AY log:** see §1.
- **Planned:**
  - GigaScreen analyzer, class-map overlay, per-pixel decision trace and a Look-Ahead divergence monitor (`docs/inprogress/2026-09-27-lookahead-manager/design.md` §2.6), in PLAN #56.
  - NedoOS syscall interceptor and OS views in PLAN #51 (`docs/inprogress/2026-09-17-nedoos-future-support/`).

## 13. Video, raster and beam

**Current state.**
- `Screen::DescribeBeam(tInFrame)` is the single beam model (`screen.cpp:1094-1142`).
  - It reports the line, the T-state in the line, beamX (2 dots per T), the vertical zone (vsync / vblank / top_border / screen / bottom_border / beyond_raster) and the horizontal zone (left_border / paper / right_border / hblank), in the active mode's geometry (`screen.h:296-311`).
  - It applies the P384 and AlCo timing overrides (`:1085-1092`).
  - It is served by `GET /video/beam` (`state_screen_api.cpp:256-316`), CLI `beam`, and Lua/Py `beam_position`.
- The Qt ULA beam widget draws the frame with blanking areas and the current zone (`ulabeamwidget.cpp:61-256`); since `a0a12def` it follows each mode's window.
- The mode-aware screen report (`DeviceState::Screen`, retired PLAN #42a, `f0ff08e5`) covers modes ZX48 … PROFIHR (`screen.h:36-66`).
- Paper-start constants are calibrated in `core/src/emulator/config.cpp:944-950`. No API field reports "INT → first pixel".
- Run to scanline and run to pixel give beam-precise stepping.
- A TTD seek to a T-state renders the partial raster.

**Planned (PLAN #42, `docs/inprogress/2026-09-27-video-debug-translation/design.md`):**
- `IVideoMapper` per video family with Layout, BeamAt, SourcesAt, PixelsFor and TextAt; FetchAt is deferred (`:187-195`).
- A `VideoMapService` with four queries: beam→where, beam→fetch, pixel→memory, memory→pixels (`:418-426`).
- A mode catalog (`:428-461`).
- Endpoints `/video/layout`, `/pixel`, `/address`, `/text`.
- A Qt pixel inspector (all sources of a clicked pixel, jump to memory) and, later, a memory-viewer overlay (`:463-500`).
- Phase 0 is done: the beam copies were merged into `DescribeBeam` and INT was recalibrated (`:542-548`).
- Beam-to-execution correlation (PLAN #29) becomes cheap on top of this.

## 14. Device state reports

`DeviceState` (`core/src/emulator/state/devicestate.h:33-96`) builds one structured report per device and a `ToText` rendering for the CLI. Every surface renders the same data (retired PLAN #20; #11 for MoonSound).

| Report | Header | Implementation | Notes |
|---|---|---|---|
| `Ay`, `AyChip` | `:35-36` | `devicestate.cpp:567`, `:611` | AY / TurboSound |
| `Fm`, `FmChip` | `:37-38` | `:622`, `:1240` | TurboSound FM |
| `Fdc` | `:39` | `:1253` (uPD765 helper `:406`) | WD1793 and uPD765A |
| `Gs` | `:49` | `:659` | GS, light GS, NeoGS (GSCFG0 decoded), optional RAM window |
| `Covox` | `:56` | `:819` | fitment, decoded ports from the port map, DAC latches |
| `MoonSound`, `MoonSoundFm`, `MoonSoundPcm` | `:71-73` | `:1019`, `:1071`, `:1164` | every FM operator, PCM levels in dB, via a side-effect-free libopl4 peek |
| `Screen`, `ScreenMode`, `ScreenFlash` | `:83-85` | `:1349`, `:1409`, `:1478` | mode-aware |
| `Contention` | `:92` | `:1518` | rule, switch, memory interface, I/O rule, slots, floating-bus latch, statistics |

Beeper, tape, keyboard, mouse, paging and ports have their own routes (`emulator_api.h:72-74`, `:187-190`, `:215`, `:228`, `:384-398`). CMOS and SD have no report. The Qt GUI shows only the FDC.

**Planned.** Device boards `W.board.beta128/ay/tsconf/gs/neogs/neogs_dma/sd/mp3/flash` (`widget-catalog.md` §4). NeoGS card statistics would become the `W.stats` widget (`docs/inprogress/2026-09-19-general-sound/neogs-automation-design.md`). The LW card gets a read-only inspector (`gs-debugger/requirements.md` §4.11).

## 15. Multi-CPU: GS debugger and the debugger model (PLAN #45, #46, #49)

**Current state.**
- The GS/NeoGS card Z80 is visible only from outside: PC/SP/AF/halted and the port trace.
- Card controls work over automation: `gs_send_command`, `gs_nmi`, `gs_reset`, and so on.
- The five GS host stimuli are TTD live inputs, applied on the machine thread (retired PLAN #44, `8dacbc82`).

**Designed, no code** (the type names return no grep hits):
- **GS debugger** (`docs/inprogress/2026-09-27-gs-debugger/`, requirements rev. 3, design rev. 2).
  - `IDebugTarget` (§3.1): id, clock, cycles, registers, side-effect-free `peek`/`poke` and paged `peekPage`/`pokePage`, a memory-window map.
  - One `TargetDebugContext` per CPU, each with its own BreakpointManager, LabelManager and disassembler (§3.2).
  - Physical addressing such as `ROM0:0038` (§3.3).
  - Debug variants of the card bus callbacks, swapped in only while card debugging runs (§4.1).
  - "The main CPU leads, the card follows" (§5.1).
  - Tight catch-up (§5.2): `gs->flush()` in the debug memory path, so the card is at most one bus cycle (3-6 T) behind, and only when card breakpoints exist.
  - `StepCoordinator` (§5.3).
  - Firmware profiles, `GSCommandInfo` tables chosen by ROM SHA-256 (§6.1).
  - Command log (§6.3).
  - TTD on the card (§8).
  - Phases 1-6 (§10). Gated on PLAN #40-V1 and NeoGS phase 0.
- **Debugger model** (`docs/inprogress/2026-09-28-debugger-model/`, draft).
  - "One protocol. One set of fields. One set of rules. Any number of looks."
  - Skins: pixel-faithful Unreal, modern Qt, terminal, browser, task skins. GDB and DeZog count as skins too.
  - New `core/src/debugger/model/`: `DebugService`, the `debugjson` serializer, `DebugEventBridge` (`protocol.md` §1).
  - Every route gets a `cpu=main|gs|neogs|card` selector.
  - Push events on the WebSocket topics `debug`, `timeline`, `cmdlog`, `stats` and `ttd`, numbered with `seq` and recovered from a snapshot after a gap (§5).
  - Additive changes only (§7). Nothing on the fast path (§8).
  - `rules.md` §13 decides, per Unreal quirk Q1-Q12, whether to keep the classic behavior or the improved one.
  - GUI briefs: `gui-main-debugger.md` (personas J1-J6, workspaces, acceptance) and `gui-card-debugger.md` (GS twin timeline, NeoGS DMA/SD/MP3/flash tabs).
- **TUI** (PLAN #49).
  - Specs: `TDD-DBG-01` is a cell-exact 80×30 Unreal Speccy 0.39 monitor; `TDD-DBG-02` is the 157×30 TSConf variant.
  - POC `tools/poc/018-tui-debuggers`: `dbgclassic` and `dbgtsconf` on FTXUI plus an SDL pixel renderer, through `IDebuggerBackend` with a mock back end (z80ex + golden data) and a REST back end.
  - Verified live over REST: regs, memory, disasm, step, run-until-break, breakpoints.
  - Missing: PC history and TSConf registers, which need server routes.
  - The widget set becomes the content source for the debugger model.

## 16. HUD, sound oscilloscope and other front-ends

- **HUD** (r1 shipped; `unreal-qt/src/hud/`).
  - Element kinds: Toast, Indicator, Stats, Banner, Image, Tile, Tilemap, Text.
  - Driven by MessageCenter topics: FDD, emulator state, breakpoints, CPU step, reset, speed, recording, file load, page changes, audio activity (`hudmodel.h:151-166`).
  - Drawn after the CRT pass, so it never gets into recordings (`unreal-qt/src/hud/README.md:7-11`).
  - Still open (PLAN #30): Phase 4, moving `hud/core` to `core/src/presentation/hud/` with `NC_HUD_CHANGED`; also `hud notify/state` and `/hud` automation (`docs/inprogress/2026-09-07-hud-layer/TODO.md`).
- **Sound oscilloscope** (PLAN #48, draft, goals not signed off).
  - Event-driven capture of AY, beeper and Covox writes, with PC and T-state correlation.
  - A compressed long store reusing `ttdcompression.h`.
  - Chip views and VCD export.
  - Analysis taps and an `analyze_audio` MCP tool (arm, capture, metrics, spectrum, compare, reference, series, correlate).
  - A Qt `SoundScopeDock` (`docs/inprogress/2026-09-16-sound-oscilloscope/architecture.md` §3-§9).
- **Other apps.**
  - `unreal-screen-viewer` reads screen pages 5 and 7 over shared memory and finds instances through the WebAPI (`unreal-screen-viewer/README.md:5-12`).
  - `unreal-videowall` is a grid of instances.
  - Neither is a debugger.

## 17. Devtools program, ZX DLSS and the media manager

- **Devtools** (PLAN #51, XL, design only).
  - `unreal-devd` sits between the core and IDEs and speaks LSP and DAP; a thin VS Code extension on top.
  - API upgrades R1-R8, among them object sets, an event stream, journaled mutations and versioning (§8).
  - `libunreal-debuginfo`.
  - Trigger engine with a replay contract (§9.3).
  - DAP with reverse execution (§12).
  - Declarative OS descriptors (§14).
  - Phases 0-7 (`prioritized-roadmap.md` §4). Its trigger is #6 plus #23 landing.
  - NedoOS: struct catalog and DSL, a BDOS `RST 0x10` interceptor, and scheduler / page / syscall visualizations. Devtools §17 asks for a truth pass on these docs.
- **ZX DLSS / Look-Ahead / Metadata** (PLAN #56; no code on master).
  - The Python POC is at v10 in `tools/poc/019-zxdlss-gigascreen`, and plane-B capture work is on the `worktree-zxdlss-planeb` branch (`caea3ad4`, `fd08e784`).
  - Debug parts:
    - capture endpoints (`capture/planeb`, `dlss/classmap`, `dlss/sidebyside`, `dlss/stats`);
    - a per-pixel decision trace;
    - a shadow instance with a divergence monitor (screen digest + state hash);
    - a guest-lag probe ("next K frames if key X is pressed");
    - pack-owned read-only trigger sets shown in the debugger list;
    - `memhash`/`physhash` observables (`metadata-manager/trigger-integration.md` §2).
- **Media manager** (PLAN #58, M1-M5 on master).
  - One vocabulary on every surface.
  - `recording` error (HTTP 409) while TTD records; guest writes are replay barriers.
  - Disk sector, track and catalog inspection stay floppy-specific.
  - Planned H-phase debug verbs: `media versions`, `changes`, `read-block --at v|--at-frame f`, `diff`, `bookmark`, and an `NC_MEDIA_VERSION` notification (`docs/inprogress/2026-09-28-storage-manager/media-history-design.md` §6).

## 18. Capture and recipes

- **Screenshots.** `ScreenCapture` writes GIF or PNG, screen only or the full framebuffer, as base64 or to a file (`screencapture.h:19-58`).
  - WebAPI `/capture/screen` defaults to GIF (`capture_api.cpp:100-144`).
  - MCP `capture_media screenshot` and `inspect_state screen_image`.
- **OCR:** `/capture/ocr`, CLI `capture ocr|romtext`, Py `capture_ocr`, MCP `screen_ocr`. ZX 32×24 text only; the text-mode OCR for other modes is part of #42.
- **Recording.** `core/recording` supports single-track, multi-track, channel-split and audio-only modes.
  - Back ends: native (macOS VideoToolbox, Windows NVENC / MF-AAC / MP4 muxer) or an ffmpeg pipe; GIF and DSD encoders (`recordingmanager.h:25-77`).
  - The Qt recording window has presets (`videorecordingwidget.cpp:429`, `:901-956`).
  - Surfaces: `/video/record`, MCP `record_*`.
- **Recipes** (`.recipe/`, 31 files):
  - `_common`: machines, setup, transports.
  - `analysis`: memory counters, non-standard loader, port trace, TTD recording, TTD reverse debugging, TTD visual inspection.
  - `articles`: "Who corrupted this memory?" TTD bug hunt, demo boot verification, disk protection triage, physical protection forensics.
  - `machines`, `media`, `peripherals`, `run`.
  - None covers symbols, listings, the assembler or patching. PLAN #34 wants symbols/debugger and capture recipes, and PLAN #15 wants the tape-ordering rule.
- **MCP resources:** keyboard layout, BASIC reference, Z80 ISA, TR-DOS commands, memory map, `machine/profi`, and the dynamic `emulator-state`. Per-machine resources are PLAN #14.

## 19. Feature gating and performance approach

- **Debug mode.** One master switch (`debugmode`) picks the memory interface: fast, or debug/contended (`featuremanager.cpp:644-653`; M1 contention adds `FastContendedMemIf` / `DbgContendedMemIf`, PLAN #61). With debug mode off, breakpoint checks, trackers and TTD hooks are not reached at all (`memory.cpp:1926-1931`).
- **Breakpoint checks.** They cost one flag test and one byte test on a miss (§7).
- **Analyzers.** Hot subscriptions are raw function pointers, and `hasCPUStepSubscribers()` gates the per-instruction dispatch (`analyzermanager.h:233`).
- **TTD overhead.** Budgets and measured numbers are in `docs/emulator/design/debugger/time-travel-debug/overhead-and-gating.md` §1a. The write journal can be switched off ("gaming" vs "development" mode, `timetravelmanager.h:399-419`).
- **Planned.** The GS debugger's tight catch-up is costed in phase 1, with a fallback of flushing only at instruction start (`gs-debugger/design.md` §11). The debugger model promises nothing on the fast path (`protocol.md` §8).

## 20. Strengths vs other emulators

1. **Time travel on every surface.** Record, seek to a T-state with raster-accurate render, step back by instruction or T-state, reverse-continue, find-last with value, PC and physical-page filters, bookmarks, a coverage index, resume-from-past with journaled edits, and `.ttd` files. It is reachable from Qt, CLI, WebAPI, MCP, Lua, Python, GDB (`bs`/`bc`) and DeZog. Among the neighboring surveys, Mesen2 steps back by instruction, scanline or frame on top of its rewind buffer, but has no reverse search and Lua cannot drive it (`mesen2-debugger.md` §12). BizHawk's time travel is frame-granular (`bizhawk-debugger.md`). DeZog's reverse debugging rewinds registers only (`dezog-debugger.md`). None of them offers reverse-continue or find-last over a fully rewound machine through remote protocols.
2. **An AI-agent surface.** A native MCP server with 12 task-shaped tools, router tools over the whole OpenAPI, resources, progress events, and a 31-file recipe library of worked workflows (protection forensics, TTD bug hunts). None of the other surveyed emulators has one.
3. **Ten remote surfaces from one core.** Telnet CLI, REST + OpenAPI/Swagger, MCP, Lua, Python, DZRP (with a TTD history extension), ZRCP and GDB RSP (with a paging target feature and reverse execution). Several debuggers can attach at once.
4. **Bank-aware breakpoints with a measured hot path.** Physical-page execute/read/write breakpoints plus any-bank ones, owners, groups and silent analyzer breakpoints. The miss path is 1.2-1.4 ns and gated by a benchmark.
5. **Semantic analyzers.** TR-DOS call and FDC semantics, ROM editor control points with verified BASIC typing, ROM-font OCR, AY write log, audio capture.
6. **Port-level forensics.** A 1M-event port trace with device attribution for every port decoder (incl. ATM/Evo), filters and compressed export. There are also a GS-side port trace and `DeviceState` reports down to each MoonSound FM operator.
7. **Beam-precise stepping and reporting.** Run to scanline, run to pixel, run to interrupt, the mode-aware `DescribeBeam`, and a ULA beam widget.
8. **Design depth for the next step.** A worked multi-CPU model (main + GS/NeoGS card with one timeline), a video mapper for pixel↔memory, a trigger engine with retroactive TTD back ends, and a DAP/LSP toolchain. All are documented with phases and dependencies.

## 21. Gaps

**Missing or weak capabilities:**
- **No conditions, hit counts, ranges, actions, IRQ breakpoints or breakpoint persistence in core.** Only ZRCP evaluates conditions. `BreakpointRangeDescription` is declared (`breakpointmanager.h:85-106`) and never used. PLAN #6 has zero code.
- **The Qt debugger is read-only and thin:**
  - It cannot edit registers, memory or instructions.
  - No change highlighting, navigation history, watches or run-to-cursor (the menu entry is disabled).
  - Stack depth is 4. The FDC is the only device panel.
  - The TTD panel has frame stepping only.
  - Breakpoints are not saved (`debuggerwindow.cpp:648-652`).
- **Several stubs:**
  - Keyboard breakpoints cannot be added (`breakpointmanager.cpp:68-85`).
  - `RemoveBreakpoint(descriptor)` throws "Not implemented" (`:97-106`).
  - `DebugManager::AddBreakpoint/Remove/Enable/Disable` are empty (`debugmanager.cpp:137-166`).
  - `DocumentDisasm` returns an empty struct.
  - `BorderTimingWidget` measures nothing (its border color is hard-coded 0).
  - `dispatchMemoryRead/Write/VideoLine` have no callers, so those analyzer subscriptions are inert.
  - `ROMPrintDetector` is not registered.
  - The `DisassemblerListingView`, column view and text view are not used.
- **Single-CPU.** No card-CPU debugging (disassembly, registers beyond PC/SP/AF, banked RAM) until PLAN #45.
- **No event push.** The WebSocket accepts clients but never publishes (`emulator_websocket.cpp:97`, no callers), and there are no Lua/Python callbacks. Clients poll (PLAN #23).
- **Symbols:**
  - Lookup ignores paging, with one label per Z80 address in the map.
  - A real sjasmplus `.sym` is silently dropped.
  - z88dk `.map`, SLD, SkoolKit, MAME and Unreal `.l` files are not supported, and the ROM maps in `data/symbols/` are not auto-loaded.
  - The listing parser handles one flat file.
- **No instruction trace log to file**, no memory diff or cheat search, no watch expressions, and disassembly T-states are computed but not shown.
- **Snapshots:** `.sna` and `.z80` only. `SupportedSnapshotExtensions()` lists `szx` (`emulator.cpp:1912`), but loading it is rejected.
- **TTD limits:**
  - No memory budget (`ttdcodecpagestore.h:179`).
  - Device RAM (GS, MoonSound) is stored as whole-RAM blobs until V1.
  - Loading media or a snapshot ends the session.
  - `find-last` has no register-value condition.
  - `ttd.ksy` does not model the flag-gated trailing sections (`:140-141`).
  - Only one timeline exists: resume truncates the future.
- **Surface parity is manual.** Lua lacks key, profiler, call-trace and analyzer functions. Python is not built in CI (PLAN #19). There are no MCP prompts.

**Contradictions between docs and code:**
- **Breakpoint manager comments vs code.** `breakpointmanager.h:114` says filter writes happen "under mutex", but neither file has a lock. `:158-160` says "no breakpoint IDs reuse", but `RemoveBreakpointByID` resets the sequence (`cpp:140-149`).
- **SLD support is claimed but absent.** The MCP tool descriptions say labels come from ".sld/.lbl" (`core/automation/mcp/src/mcp-symbols.cpp:37`, `:59`). `command-interface.md` §4.4 calls SLD the primary format. `docs/features/automation.md` says ".sld/.lst". No SLD parser exists (`labelmanager.cpp` has no `sld`).
- **Other symbol-format claims.** `command-interface.md` claims Pasmo/TASM/Z80ASM `.map` and "save in SLD format". `SaveLabels` ignores its format argument.
- **`/assemble` with `write:true` bypasses the journal.** It uses `MemoryWriteFast` (`debug_api.cpp:3795-3797`), not `EditMemoryFromTool`, so the edit is not journaled in TTD. The memory-write routes, CLI and DeZog do journal their edits.
- **OpenAPI coverage.** `OPENAPI_MAINTENANCE.md:18` claims "239 of 239" and 26 fragments. The checker reports 241/243 today (the MoonSound state routes are missing from the spec), and there are 27 fragments.
- **MCP tool count.** `docs/features/mcp/README.md:3`, `:26` says 11 smart tools; 12 are registered. The server instructions omit `unreal://machine/profi` (`mcp-dispatcher.cpp:28-29`).
- **GDB docs.** `docs/emulator/design/control-interfaces/gdb-protocol.md:7` still says "Planned (Q2 2026)". `core/automation/gdb/README.md` advertises `monitor gdbport` and the `gdb_port`/`gdb_bind`/`gdb_autoattach` INI keys, which do not exist in code.
- **DeZog README.** `docs/inprogress/2026-08-27-dezog-integration/README.md` shows every phase unchecked, while its TODO says the integration is fully verified.
- **Stale parity matrix.** `docs/inprogress/2026-08-26-automation-gaps/feature-parity.md` still marks as missing: CLI register write, Lua/Py TTD dump/load, `run_until_condition`, GDB and DZRP. All of these exist.
- **Two precedence tables for one planned language.** The conditional-breakpoint grammar binds bitwise operators tighter than comparisons (`2026-08-17-conditional-breakpoints/design.md` §4.2). The expression evaluator uses C order, with `==` tighter than `&` (`2026-08-26-expression-evaluator/design.md` "Operator Precedence").
- **TTD timeline design.** `ttd-timeline-summary-design.md` depends on `writeJournalOffset` and a dirty count from `ramPages.size()`, and neither exists. PLAN #7 still calls it "design complete & reviewed".
- **Debugger model links.** `docs/inprogress/2026-09-28-debugger-model/README.md` and `TODO.md` point to `gui-requirements.md`; the folder has `gui-main-debugger.md` and `gui-card-debugger.md` instead.
- **HUD README.** It lists `hudlayout`, `hudpresenter`, `hudcompositor` and `blend/`, which are not in `unreal-qt/src/hud/`.
- **Hotkeys.** The Qt menu and the debugger window bind different keys to step in, step out and F9 (§9).

**Behavior worth checking (inferred from code, not tested):**
- `skipBreakpoints` suppresses only execution breakpoints. The memory and port checks do not consult it.
- The memory-read check does not exclude opcode fetches, so a read breakpoint may also fire on execution.
- The observer that step-over registers is never removed with `RemoveObserver`.
