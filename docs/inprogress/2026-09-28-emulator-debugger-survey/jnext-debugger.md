# jnext debugger — capability survey

**Source:** https://github.com/jorgegv/jnext · local checkout commit `810cfdff` (2026-08-13), version 0.99.155 (`jnext/version.yaml`) · C++17, Qt 6 Widgets debugger (optional, `ENABLE_DEBUGGER` CMake option), SDL/headless frontends
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** one Qt GUI debugger window (`jnext/src/debugger/`) over a Qt-free backend (`jnext/src/debug/`); command-line debug aids (trace, profiler, compositor/DAC/esxdos traces, magic breakpoint/port). No console monitor, no expression language, no remote-debug protocol inside the emulator: DeZog is supported only indirectly through emulated hardware (ESP-01 server mode and joystick-port UART) that a guest-side `dezogif_ng` stub uses. A scriptable debugger DSL exists as a design document only.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | CPU Registers panel | AF BC DE HL, alternates, IX IY SP PC, I R, IFF1/IFF2, IM; flags S Z H PV N C colored; `HALTED`; displayed screen bank (`Bank 5`/`Bank 7`) + Timex mode (`Alt`, `HiCol`, `HiRes`); read-only | `jnext/src/debugger/cpu_panel.cpp:240`, `:265` |
| CPU & registers | MMU panel | Eight 8K slots with effective physical page and type (`ROM` or `Bn` bank); 128K port 0x7FFD bank, ROM bit, paging Lock (red) | `jnext/src/debugger/mmu_panel.cpp:96`, `:122` |
| Disassembly | Disassembly panel | Z80 + all Z80N opcodes, gutter / address / bytes / mnemonic; PC row yellow+bold; full 0000-FFFF scroll; symbol substitution for 16-bit immediates | `jnext/src/debugger/disasm_panel.cpp:355`, `jnext/src/debug/disasm.cpp` |
| Disassembly | Context menu | Toggle Breakpoint, Run to Here, Go to Address, Watch symbol / `$nnnn` / `(HL)` `(DE)` `(BC)` `(IX` `(IY` `(SP)`, Break on Read/Write on immediate or on the register's current value | `jnext/src/debugger/disasm_panel.cpp:560-674` |
| Memory views | Memory hex editor | 16 bytes/row + ASCII; `CPU View` (64K logical) or `Slot 0..7` (names the mapped page, updates on paging); row coloring: SP row orange, pixels 4000-57FF cyan, attributes 5800-5AFF yellow; two-hex-digit in-place edit written through the MMU (ROM stays read-only) | `jnext/src/debugger/memory_panel.cpp:60`, `:200`, `:322-330` |
| Memory views | Stack panel | 24 words from SP: address, word hex + decimal, high/low bytes; TOS green | `jnext/src/debugger/stack_panel.cpp` |
| Memory views | Call Stack panel | Shadow call stack: depth, type `CALL`/`RST`/`INT`/`NMI`, caller, target (symbol when known); max depth 256 | `jnext/src/debug/call_stack.h:8-47`, `jnext/src/debugger/callstack_panel.cpp` |
| Navigation & bookmarks | Address box / Go to PC | Hex address entry (`5800`, `$5800`, `0x5800`) in disassembly and memory; Enter = run to selected line | `jnext/src/debugger/disasm_panel.cpp`, `jnext/src/debugger/memory_panel.cpp` |
| Symbols & labels | MAP file loader | z88dk `.map` (only `; addr` entries, `; const` skipped) and simple `NAME = $ADDR` format; bidirectional std::map lookup; first symbol at an address wins | `jnext/src/debug/symbol_table.h:9-47`, `jnext/src/debug/symbol_table.cpp:7` |
| Breakpoints | Execute (PC) | `unordered_set<uint16_t>` of logical addresses, checked before each instruction | `jnext/src/debug/breakpoints.h:59-63`, `jnext/src/debug/debug_state.cpp:44-52` |
| Breakpoints | Memory read / write / read-write | Logical 16-bit address; flagged in the MMU during the access, machine stops after the instruction | `jnext/src/memory/mmu.h:245-265`, `jnext/src/core/emulator.cpp:7908` |
| Breakpoints | I/O read / write | Port watch with partial decode: 00xx matches any port with that low byte, >= 0100 matches exact 16-bit port | `jnext/src/debug/breakpoints.h:69-90`, `jnext/src/debug/breakpoints.cpp:75-83`, `jnext/src/port/port_dispatch.cpp:28-32` |
| Breakpoints | One-shot | Internal breakpoint for Step Over and Run to Here | `jnext/src/debug/breakpoints.h:96-100` |
| Breakpoints | Magic breakpoint | `ED FF` (ZEsarUX) and `DD 01` (CSpect) opcodes pause and auto-open the debugger (`--magic-breakpoint`) | `jnext/src/core/emulator.cpp:1056-1063`, `jnext/src/cpu/z80_cpu.cpp:862`, `:1022` |
| Breakpoints | Persistent breakpoints | `--persistent-breakpoints` keeps breakpoints armed with the window closed; a hit reopens the window | `jnext/src/debug/debug_state.h:27-40`, `jnext/src/debugger/debugger_manager.cpp:665-676` |
| Breakpoints | Breakpoints panel | All kinds in one list sorted by address, type, address, symbol; Add/Edit/Remove; synced with disassembly gutter via observers | `jnext/src/debugger/breakpoint_panel.cpp:104-144`, `jnext/src/debug/breakpoints.h:23-57` |
| Conditions & expressions | — | None: no conditions, hit counts, or actions | — |
| Watchpoints & watches | Watches panel | Address, label, size Byte/Word/Long (little-endian), live value through the current CPU mapping | `jnext/src/debugger/watch_panel.cpp:134-188` |
| Execution control | Run / Break / Step Into / Over / Out | F5 / F9 / F6 / F7 / F8; step-off-breakpoint on resume; Step Over only for CALL/CALL cc/RST/DJNZ | `jnext/src/debugger/debugger_window.cpp:400-422`, `jnext/src/debug/debug_state.h:61-113` |
| Execution control | Run to EOSL / EOF | Run to end of current scanline / to mid-point of last visible line (raster-effect stepping) | `jnext/src/debugger/debugger_manager.cpp:462-533` |
| Execution control | Run to cycle | Internal `RUN_TO_CYCLE` step mode on master clock (used by EOSL/EOF and rewind replay) | `jnext/src/debug/debug_state.cpp:38-42`, `jnext/src/core/emulator.cpp:7848` |
| Tracing & logging | Instruction trace log | 10 000-entry ring (resizable, optional no-wrap): cycle, PC, all registers, opcode bytes; F2 toggle, F3 export to text | `jnext/src/debug/trace.h:8-66`, `jnext/src/debug/trace.cpp:241-285`, `jnext/src/core/emulator.cpp:8308-8330` |
| Tracing & logging | Per-subsystem logging | `--log-level warn,cpu=trace,...`, `--log-file`; `esxdos` logger traces every RST $08 call with name, args, result | `jnext/src/core/cli_options.h:542-553`, `jnext/src/core/log.h:102-106`, `jnext/src/core/esxdos_trace.h` |
| Tracing & logging | Magic port | `--magic-port PORT` prints guest writes to stderr in `hex`/`dec`/`ascii`/`line` mode | `jnext/src/core/emulator.cpp:5949-5985` |
| Tracing & logging | Compositor trace | `--compositor-trace FILE` per-pixel compositor CSV for one frame (`--compositor-trace-frame`, default 250) | `jnext/src/core/cli_options.h:502-507`, `jnext/src/video/renderer.h:505` |
| Tracing & logging | DAC trace | `--dac-trace FILE` timestamped DAC writes to CSV | `jnext/src/audio/dac_trace_recorder.h`, `jnext/src/core/cli_options.h:397` |
| History / rewind | Rewind buffer | Full-state snapshot per frame in an mmap'd ring; Step Back (Shift+F7), Frame Back (Shift+F6), frame slider; replay to exact cycle | `jnext/src/debug/rewind_buffer.h:11-136`, `jnext/src/core/emulator.cpp:10725-10843` |
| History / rewind | RZX record / play | `--rzx-record`, `--rzx-play` input recordings | `jnext/src/core/cli_options.h`, `jnext/src/core/rzx_recorder.h` |
| Video, raster & beam | Video panel | Raw HC/VC while paused; layer enables; priority order (SLU..ULS); 32 ULA palette swatches; tabs All / ULA primary-shadow / Layer2 active-shadow / Sprites / TileMap / Background; unreached rows darkened; transparency checkerboard; per-scanline register replay | `jnext/src/debugger/video_panel.cpp:78-199`, `:334-341`, `:604-609`, `:804-826` |
| Video, raster & beam | Sprites panel | All 128 sprites: X, Y, pattern, palette offset, visible, mirror X/Y, rotate, scale 1/2/4/8 | `jnext/src/debugger/sprite_panel.cpp:76-81` |
| Video, raster & beam | Copper panel | Running state, Copper PC and mode, 64 decoded instructions around PC (`WAIT v,h`, `MOVE NR xx=yy`, `NOP`, `HALT`) | `jnext/src/debugger/copper_panel.cpp:75-90` |
| Sound & device views | NextREG panel | All 256 registers, name, hex + binary; hex editable (writes with side effects); shows composed live values | `jnext/src/debugger/nextreg_panel.cpp:143-174` |
| Sound & device views | Audio panel | 16 registers of each of three AYs named; per-source mute (AY0-2, DAC, beeper); TurboSound, AY/YM, ABC/ACB info | `jnext/src/debugger/audio_panel.cpp:87-100` |
| Profiling & coverage | T-state profiler | `--profile`: T-states per physical address (page<<13 or offset), last logical PC; text output + Perl heat-map joiner against .map | `jnext/src/profiler/profiler.h:1-40`, `jnext/src/core/emulator.cpp:8453` |
| Scripting, automation & remote | Headless automation | `--headless`, `--delayed-screenshot[-frames/-layers]`, `--delayed-keypress[-frames]`, `--delayed-nmi`, `--delayed-snapshot`, `--delayed-automatic-exit` | `jnext/src/core/cli_options.h:433-501` |
| Scripting, automation & remote | DeZog via emulated hardware | ESP-01 `AT+CIPSERVER` listen mode (127.0.0.1 by default) and joy-port UART to host so guest `dezogif_ng` can talk to DeZog | `jnext/src/esp01/include/esp01/esp_at.h:40`, `jnext/src/peripheral/joy_uart_source.h:12`, `jnext/src/core/emulator_config.h:311` |
| Scripting, automation & remote | Scriptable debugger DSL | `on execute/io_write/frame ... when ... do ... end` design; not implemented | `jnext/doc/design/SCRIPTABLE-DEBUGGER.md:923` |
| Import / export | Trace export, MAP import | Text trace file; MAP symbols; no breakpoint/watch persistence | `jnext/src/debug/trace.cpp:241` |
| UI conveniences | Attach to emulator window | Debugger window docks/follows the emulator window | `jnext/src/debugger/window_attach.h`, `jnext/src/debugger/debugger_window.cpp:593` |
| UI conveniences | Corrupt-state guard | After a failed rewind restore, resuming asks for confirmation | `jnext/src/debugger/debugger_manager.cpp:136`, `:183` |

## 2. CPU, registers and memory map

- The CPU panel is read-only; edits go through Memory or NextREG panels (`jnext/src/debugger/cpu_panel.cpp`). It shows which screen bank the ULA displays and the Timex mode (`jnext/src/debugger/cpu_panel.cpp:265-269`).
- The MMU panel shows the *effective* page per slot, whichever of NextREG 0x50-0x57 or legacy 128K paging produced it (`jnext/src/debugger/mmu_panel.cpp:122`).

## 3. Disassembly

- `disasm_one(addr, read_fn)` returns address, up to 4 bytes, mnemonic (`jnext/src/debug/disasm.h:6-18`); `is_call_like` covers CALL, CALL cc, RST, DJNZ — so Step Over on DJNZ runs the whole loop (`jnext/src/debug/disasm.h:23-25`).
- The context menu builds register-indirect entries only when the instruction really uses that register, capturing the value at menu-open time (`jnext/src/debugger/disasm_panel.cpp:595-674`).
- The panel is refreshed only on stop, not while running (user guide `jnext/src/doc/user-guide/06-debugger/panels/03-disassembly.md`).

## 4. Memory views

- The memory panel is a hex editor whose `CPU View` covers the logical 64K and whose slot views are bound to the current slot mapping, not a fixed physical page (`jnext/src/debugger/memory_panel.cpp:60-62`, `:200`). There is no physical-page browser independent of the mapping *(inferred from the selector entries)*.
- Writes go through the MMU exactly like CPU writes (user guide `04-memory.md`).
- Call stack tracking hooks `on_instruction_pre` / `on_instruction_post` around each instruction and detects taken CALL/RET by comparing SP (`jnext/src/debug/call_stack.h:23-37`, `jnext/src/core/emulator.cpp:8361`, `:8480`). It is enabled only while the debugger is open, so the stack is not retroactive.

## 5. Breakpoints

**Data structures.** `BreakpointSet` holds `unordered_set<uint16_t> pc_bps_`, `vector<Watchpoint> watchpoints_` (addr + `WatchType {READ, WRITE, READ_WRITE, IO_READ, IO_WRITE}`), and one one-shot address (`jnext/src/debug/breakpoints.h:8-13`, `:113-116`). Duplicates are rejected on insert (`jnext/src/debug/breakpoints.cpp:42-49`).

**Keying.** All addresses are 16-bit logical CPU addresses; there is no bank/page qualifier, so an execute breakpoint fires in whatever page is mapped at that address. Memory watchpoints are checked in each MMU read branch (boot ROM overlay, Multiface, DivMMC, Layer 2 read-over, normal slots) (`jnext/src/memory/mmu.h:245-340`).

**I/O keying.** The partial-decode rule (00xx = low-byte match, else exact) is in `has_io_watchpoint` (`jnext/src/debug/breakpoints.cpp:75-83`); READ_WRITE does not exist for ports (`jnext/src/debug/breakpoints.h:88-90`).

**Hit semantics.** PC breakpoints fire before the instruction (`jnext/src/core/emulator.cpp:7836`). Data and I/O hits set a latch `data_bp_hit_` + address; the run loop pauses *after* the instruction finishes (`jnext/src/core/emulator.cpp:7908-7912`). No hit counts, no conditions, no enable/disable toggle, no actions (log-and-continue); the magic port is the documented substitute for logging.

**Performance.** One cached bool `armed_ = active_ || persistent_` gates everything (`jnext/src/debug/debug_state.h:33-40`, `:139-146`); with the debugger closed no breakpoint check runs at all. Memory checks additionally short-circuit on `has_any_watchpoints()` before the linear scan of the watchpoint vector (`jnext/src/memory/mmu.h:246-248`). Observer notification sits only on mutators; the hot-path readers notify nobody (`jnext/src/debug/breakpoints.h:36-40`).

**Step-off.** Every paused-to-running transition arms a one-instruction "step-off" that suppresses the breakpoint under PC exactly once (`jnext/src/debug/debug_state.h:61-87`, `:148-168`); a resume while already running arms nothing, so it cannot swallow a later hit.

**Magic breakpoint.** CPU callback `on_magic_breakpoint` on `ED FF` / `DD 01`, which sets the debugger active and pauses; the debugger manager then opens the window (`jnext/src/core/emulator.cpp:1056-1063`, `jnext/src/debugger/debugger_manager.cpp:665-676`).

## 6. Watches

Byte/Word/Long, little-endian, read through the current CPU mapping (`jnext/src/debugger/watch_panel.cpp:134-188`). No format choices (hex only), no expressions, no change highlighting found.

## 7. Execution control

- `StepMode {NONE, INTO, OVER, OUT, RUN_TO_CYCLE, STEP_BACK, RUN_BACK_TO_CYCLE}` (`jnext/src/debug/debug_state.h:6`).
- **Step Out** ends only when a return instruction (C9, RET cc, ED 45/4D and undocumented ED RETN aliases) pops SP by exactly 2 *past* the SP captured at F8; nested calls and interrupt RETI are ignored (`jnext/src/debug/debug_state.cpp:74-131`).
- **Run to EOSL** rounds the elapsed master cycles to the next line boundary; beyond the last visible row it jumps to the next frame start (`jnext/src/debugger/debugger_manager.cpp:507-533`). **Run to EOF** targets the middle of the last visible line (raw VC = FB_HEIGHT-1 + vblank_top), or the same point in the next frame if already past (`jnext/src/debugger/debugger_manager.cpp:462-491`).
- A paused frame is *resumed*, not restarted: frame-start actions (Copper restart, per-line change-log reset, interrupt scheduling, rewind snapshot) run only at a true frame start (`jnext/src/core/emulator.cpp:7781-7806`).
- Hotkeys: F5 Continue, F9 Break, F6 Step, F7 Over, F8 Out, Shift+F6 Frame Back, Shift+F7 Step Back, F2 trace toggle, F3 export (`jnext/src/debugger/debugger_window.cpp:400-474`).

## 8. Tracing and logging

- **Trace entry**: master cycle, PC, AF BC DE HL, alternates, IX IY SP, up to 4 opcode bytes, length (`jnext/src/debug/trace.h:8-16`). Recorded before execution; length computed through a raw function pointer to avoid per-instruction `std::function` construction (`jnext/src/debug/trace.h:18-25`, `jnext/src/core/emulator.cpp:8308-8330`).
- **Export format** (`jnext/src/debug/trace.cpp:273-280`), one line per instruction:
  `000000123456  $8000  AF=0044 BC=0000 DE=0000 HL=5C00  AF'=0000 BC'=0000 DE'=0000 HL'=0000  IX=0000 IY=5C3A SP=FF4A  [-Z---P--]  3E 01`
- Ring vs freeze-when-full (`set_no_wrap`) and resize for long captures (`jnext/src/debug/trace.h:41-47`).
- The repo ships a CSpect plugin that writes the *same* format so jnext and CSpect traces can be diffed line by line to find the first divergent instruction (`jnext/tools/cspect_plugin/CSpectFullTrace.cs:1-20`), plus Python DZRP scripts that drive CSpect for differential captures (`jnext/tools/cspect_dzrp/`).
- Side traces: esxdos RST $08 syscall log (`jnext/src/core/log.h:102-106`), compositor per-pixel CSV, DAC CSV, magic port (see registry).

## 9. History / rewind

- `RewindBuffer`: `max_frames` fixed-size slots in one lazily-faulted `mmap(MAP_ANONYMOUS)` block; snapshot at the top of `run_frame()` when the scheduler queue is empty (`jnext/src/debug/rewind_buffer.h:11-37`, `jnext/src/core/emulator.cpp:7160`). Snapshot size must stay constant; the header lists every variable-length state field that had to be made fixed-width (`jnext/src/debug/rewind_buffer.h:80-110`).
- `rewind_to_cycle` restores the nearest snapshot at or before the target and replays with `RUN_TO_CYCLE` in `replay_mode_` (no audio, no rendering), then re-renders (`jnext/src/core/emulator.cpp:10725-10784`).
- `step_back(n)` needs the trace log: it takes the cycle of trace entry `size-n`, clears the trace, and rewinds to that cycle (`jnext/src/core/emulator.cpp:10786-10843`). Enabling rewind force-enables the trace.
- Failed restore (sentinel/bounds check) pauses in a flagged corrupt state; resuming asks for confirmation (`jnext/src/core/emulator.cpp:10739-10746`, `jnext/src/debugger/debugger_manager.cpp:136`).
- UI: status bar "Rewind: N frames / M MB", slider + Jump Here, buffer-size dialog with MB estimate (`jnext/src/debugger/debugger_window.cpp:300-330`, `:710-782`).

## 10. Video, raster and beam

- **Raster position**: HC/VC shown only while paused, from `paused_hc()` / `paused_vc()` (raw counters) (`jnext/src/debugger/video_panel.cpp:604-609`, `:804-826`). The per-instruction `VideoTiming::advance()` walk is enabled only while the debugger is active, because a human reading HC/VC is its only consumer (`jnext/src/debug/debug_state.h:16-23`).
- **Per-scanline replay**: the panel rewinds each layer's change log to its frame baseline, then replays changes line by line (palette, ULA palette select, NR 0x15 priority, Layer 2/tilemap scroll) so raster splits look as on screen (`jnext/src/debugger/video_panel.cpp:105-199`, `:334-341`).
- **Progress view**: rows not yet reached by the raster are drawn dark; transparent pixels use a checkerboard (`jnext/src/debugger/video_panel.cpp:78-100`). Combined with Run to EOSL this is a line-by-line raster stepper.
- Layers individually: ULA primary/shadow, Layer 2 active/shadow bank, sprites, tilemap, background (NR 0x4A) (`jnext/src/debugger/video_panel.cpp:280-300`).
- Copper and sprite attribute tables described in the registry. There is no per-T-state event map, no contention view, and no DMA (zxnDMA) panel.

## 11. Sound and device views

- NextREG panel writes go through `nextreg().write()` with full side effects (`jnext/src/debugger/nextreg_panel.cpp:174`); values shown are composed hardware state rather than last-written bytes.
- Audio panel mute is output-stage only and does not change guest-visible behavior (`jnext/src/debugger/audio_panel.cpp:87-100`).

## 12. Profiling

`--profile` allocates a 2M-entry mmap'd table keyed by `(effective 8K page << 13) | (PC & 0x1FFF)`, storing T-states and last logical PC; output is `<phys_hex6> <log_hex4> <tstates>` lines; `tools/get-function-heatmap.pl` joins it with z88dk .map files for a per-function heat map (`jnext/src/profiler/profiler.h:1-40`). Cost when off: one predicted-false branch (`jnext/src/core/emulator.cpp:8453`).

## 13. Scripting, automation and remote debug

- No Lua/Python, no GDB stub, no DZRP/CSpect server in the emulator. Automation is command-line driven (`--headless` with delayed screenshot/keypress/NMI/snapshot/exit) (`jnext/src/core/cli_options.h:433-501`).
- DeZog works the hardware way: the emulated ESP-01 implements `AT+CIPSERVER` (listen, bound to 127.0.0.1 unless `--esp-listen-address`) so a guest `dezogif_ng` stub can accept DeZog's connection; a joy-port UART can be bridged to a host serial end (`jnext/src/esp01/include/esp01/esp_at.h:40-41`, `jnext/src/peripheral/joy_uart_source.h:12`, `jnext/src/core/cli_options.h:252`).
- The scriptable-debugger DSL design (event handlers `on execute sym[...]`, `on io_write PORT when VALUE >= ...`, `on frame N`, `assert`, `dump_regs`, `dump_mem`, variables `CYCLE FRAME VC`) is "Design — awaiting implementation approval" (`jnext/doc/design/SCRIPTABLE-DEBUGGER.md:1-60`, `:923`).

## 14. Notable and unique ideas

1. **Run to End of Scan Line + per-scanline video replay with darkened unreached rows** — a true raster stepper: step one line, see exactly which part of each layer has been drawn with the register values of each line.
2. **One cached `armed` bool as the only hot-path gate** plus an opt-in `--persistent-breakpoints`; zero cost when the debugger is closed, and the trade-off is explicit.
3. **Step-off arm on the paused-to-running edge only** — a clean, one-instruction-wide fix for "F5 at a breakpoint re-hits it", applied to every resume path through one `unpause_()`.
4. **Step Out by SP depth + return-opcode set** (including undocumented RETN aliases), robust to nested calls and interrupts.
5. **Port breakpoints with ZX partial-decode semantics** (low-byte match for 00xx) — matches how Spectrum ports are actually decoded.
6. **Trace-format-compatible CSpect plugin** for differential debugging against another emulator.
7. **Rewind = frame snapshots + deterministic replay to a cycle**, with Step Back driven by the trace log's cycle stamps; fixed-width state as an enforced invariant.
8. **Magic breakpoint (ED FF / DD 01) and magic port printf** — cheap guest-side hooks compatible with ZEsarUX/CSpect conventions.
9. **Profiler keyed by physical page**, so banked code gets separate buckets.
10. **Composed-value NextREG editor** with real side effects.

## 15. Gaps and caveats

- No conditions, hit counts, enable/disable, or log-only breakpoints; no expression evaluator anywhere.
- All breakpoints are logical-address only; no bank/page-qualified breakpoints despite the MMU-heavy Next.
- Breakpoints, watches and symbols are not persisted to disk.
- Breakpoints are inert while the debugger window is closed unless `--persistent-breakpoints` is given.
- Data breakpoints stop after the instruction; the faulting PC is not shown directly (only `data_bp_addr`).
- The trace recorder reads opcode bytes with `mmu_.read()` (`jnext/src/core/emulator.cpp:8322-8323`), the same hot path that raises READ watchpoints; a read watchpoint on an address within an instruction's 4-byte window could be tripped by tracing *(inferred, not tested)*.
- Step Back after a previous Step Back only reaches back to the snapshot the replay started from, since the trace is cleared and repopulated during replay *(inferred from `jnext/src/core/emulator.cpp:10831` and `:8307`)*.
- No DMA (zxnDMA), contention, or per-T-state event visualization; HC/VC shown only while paused.
- No emulator-side remote protocol; the scriptable debugger is unimplemented.
- Call stack is built only from when the debugger opens.
