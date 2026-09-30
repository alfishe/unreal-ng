# MAME debugger — capability survey

**Source:** https://github.com/mamedev/mame · local checkout commit `f43983b6` (2026-09-23, build version 0.289 per `mame/makefile:1543`) · C++17/20; front ends in Qt 6, Win32, Cocoa (macOS), Dear ImGui; Lua 5.4 via sol2
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** One emulator-agnostic debugger core (`src/emu/debug/*`: console, expression engine, points, views) shared by every driver; several OSD front ends (Qt, Windows, Cocoa, ImGui, "none") render the same core views; a built-in GDB remote stub (OSD module) plus a Lua GDB-stub plugin; Lua API exposes debugger, symbols, expressions, breakpoints and memory taps. No driver-specific debugger UI for the ZX Spectrum family.

Paths below are relative to the MAME checkout root, written as `mame/src/...:line`.

---

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | State view | Every `device_state_interface` entry of the selected CPU, plus synthetic rows `cycles`, `beamx`/`beamy`/`frame` (per screen), changed values drawn in red (`DCA_CHANGED`) | `mame/src/emu/debug/dvstate.cpp:116`, `:125-140`, `:268` |
| CPU & registers | Z80 register set | PC, CURPC (hidden), SP, A..L (hidden), AF, BC, DE, HL, IX, IY, AF2..HL2, WZ (MEMPTR), R, I, IR, IM, IFF1, IFF2, HALT | `mame/src/devices/cpu/z80/z80.cpp:724-753` |
| CPU & registers | Registers as symbols | Each state entry becomes a lower-case, read/write expression symbol (`pc`, `hl`, `iff1`...) | `mame/src/emu/debug/debugcpu.cpp:584-599` |
| CPU & registers | Cycle symbols | `cycles` (remaining in slice), `totalcycles`, `lastinstructioncycles`, `curpc` | `mame/src/emu/debug/debugcpu.cpp:550-552`, `:606-607` |
| CPU & registers | Multi-CPU control | `focus`, `ignore`, `observe`, `suspend`, `resume`, `cpulist`, `next` (run until another CPU is scheduled) | `mame/src/emu/debug/debugcmd.cpp:216-224` |
| Disassembly | Disassembly view | Any device with a disassembler and a program space; follows `curpc` or any expression; PC row yellow, breakpoint rows red, visited rows light blue; right column = raw opcodes / decrypted (`AS_OPCODES`) opcodes / comments | `mame/src/emu/debug/dvdisasm.cpp:91-110`, `:455-489`; `mame/src/emu/debug/dvdisasm.h:24-29` |
| Disassembly | `dasm` to file | Disassemble a range to a text file, optional raw opcodes, any CPU | `mame/src/emu/debug/debugcmd.cpp:330`, `:4121` |
| Disassembly | Coprocessor disassembly | Non-CPU devices with `device_disasm_interface` (e.g. ZX Next Copper) get their own disassembly source | `mame/src/mame/sinclair/next/specnext_copper.h:8`, `mame/src/mame/sinclair/next/specnext_copper_dasm.cpp` |
| Memory views | Memory view sources | Every logical address space of every device, every ROM/memory region, and every save-state item (i.e. every device's registered internal arrays, including the whole RAM buffer) | `mame/src/emu/debug/dvmemory.cpp:163-212` |
| Memory views | Formats | Hex 8/16/32/64, octal 8/16/32/64, float 32/64/80; ASCII column; reverse (right-to-left) view; bytes-per-row; address radix hex/dec/oct; logical vs physical addressing | `mame/src/emu/debug/dvmemory.h:61-94` |
| Memory views | In-place editing | Typing hex/octal digits writes memory at the cursor | `mame/src/emu/debug/dvmemory.cpp:505-535` |
| Memory views | "Written by" lookup | Right-click shows the PC that last wrote the byte (needs `trackmem`) | `mame/src/osd/modules/debugger/qt/memorywindow.cpp:545-567` |
| Memory views | Commands | `dump`, `strdump`, `save`/`load` (binary), `saver`/`loadr` (regions), `fill`, `find` (with wildcards, strings, mixed sizes), `map` (logical to physical + handler name), `memdump` (full address-map dump) | `mame/src/emu/debug/debugcmd.cpp:277-353` |
| Navigation & bookmarks | Expression-driven views | Memory and disassembly views take an expression (re-evaluated, so `hl` or `w@sp` track live values); per-window input history | `mame/src/emu/debug/dvdisasm.cpp:327-336`; `mame/src/osd/modules/debugger/qt/memorywindow.cpp:414-424` |
| Navigation & bookmarks | Run to cursor / toggle BP at cursor | F4 / F9 / Shift+F9 in disassembly | `mame/src/osd/modules/debugger/qt/dasmwindow.cpp:87-95` |
| Symbols & labels | Symbol tables | Hierarchical tables: global (temp0..temp9, wpaddr/wpdata/wpsize, beam*, frame, functions, `.`-prefixed save-state globals) and per-CPU (registers, cycles, logunmap). No user label/symbol file import | `mame/src/emu/debug/debugcpu.cpp:63-101`; `mame/src/emu/debug/debugcmd.cpp:134-183` |
| Symbols & labels | `symlist` | Lists symbols of the global or a CPU table, read-only marked with `*` | `mame/src/emu/debug/debugcmd.cpp:355`, `:4649` |
| Symbols & labels | Comments | Per-address disassembly comments keyed by (address, CRC32 of opcode bytes); saved as XML `comments/<system>.cmt` | `mame/src/emu/debug/debugcpu.cpp:1707-1803`, `:131-196` |
| Breakpoints | Execution breakpoints | `bp addr[:cpu][,cond[,action]]`, enable/disable/clear/list; multimap keyed by PC | `mame/src/emu/debug/debugcmd.cpp:235-240`; `mame/src/emu/debug/debugcpu.h:217` |
| Breakpoints | Temporary breakpoints | `go addr`, `gni n` (n instructions ahead, max 512), run-to-cursor | `mame/src/emu/debug/debugcmd.cpp:844`, `:950-953` |
| Breakpoints | Registerpoints | `rp {cond}[,action]` — condition evaluated before every instruction | `mame/src/emu/debug/debugcmd.cpp:255-260`; `mame/src/emu/debug/debugcpu.cpp:2016` |
| Breakpoints | Exceptionpoints | `ep type[,cond[,action]]`, `gex` — CPU-specific exception numbers (not raised by the Z80 core) | `mame/src/emu/debug/debugcmd.cpp:262-267`, `:209-210` |
| Breakpoints | Event stops | `gint [irq]` (interrupt taken), `gvblank`, `gtime ms`, `gp` (privilege change), `gbt`/`gbf` (next true/false conditional branch) | `mame/src/emu/debug/debugcmd.cpp:205-216` |
| Watchpoints & watches | Watchpoints | `wp`/`wpd`/`wpi`/`wpo` addr[:space],len,r/w/rw[,cond[,action]] on program/data/I/O/opcode spaces; `wpaddr`, `wpdata`, `wpsize` available in conditions | `mame/src/emu/debug/debugcmd.cpp:242-253`, `:1530-1596`; `mame/src/emu/debug/points.cpp:314-358` |
| Watchpoints & watches | Opcode-fetch watchpoints | `wpo` on `AS_OPCODES` = range execution trap (Spectrum drivers map M1 fetches to a separate opcodes map) | `mame/src/emu/debug/debugcmd.cpp:248-249`; `mame/src/mame/sinclair/spectrum.cpp:542-545`, `:733` |
| Watchpoints & watches | Watch expressions | None as a panel; closest are `print`/`printf` in breakpoint actions | *(inferred from view list)* `mame/src/emu/debug/debugvw.h:28-40` |
| Conditions & expressions | Expression engine | C-like operators, 64-bit unsigned, memory accessors `b@ w@ d@ q@` / `b! ...` with space, logical/physical, region, share prefixes; assignment; functions | `mame/src/emu/debug/express.cpp:10-29`, `:1646-1723` |
| Execution control | Step/over/out | F11/F10/Shift+F11; step-over/out driven by disassembler `STEP_OVER`/`STEP_OUT`/`STEP_COND` flags | `mame/src/emu/debug/debugcpu.cpp:1091-1140`, `:1880-1940`; `mame/src/devices/cpu/z80/z80dasm.cpp:35-39` |
| Execution control | Reset | `softreset`, `hardreset` (F3 / Shift+F3) | `mame/src/emu/debug/debugcmd.cpp:357-358` |
| Execution control | Blank command | Enter on an empty console line = single step | `mame/src/osd/modules/debugger/qt/mainwindow.cpp:337-341` |
| Tracing & logging | `trace` / `traceover` | Per-CPU instruction trace to file, loop condensation, optional error-log interleave, per-line action, append mode, `{game}` macro | `mame/src/emu/debug/debugcmd.cpp:4222-4297`; `mame/src/emu/debug/debugcpu.cpp:2054-2175` |
| Tracing & logging | `tracelog` / `tracesym` / `traceflush` | printf-style or symbol dump lines into the trace file | `mame/src/emu/debug/debugcmd.cpp:718-770` |
| Tracing & logging | `printf`, `logerror`, `print` | Console / error.log output with a mini printf (`%c %d %o %x %X %s`) | `mame/src/emu/debug/debughlp.cpp:361-390` |
| Tracing & logging | Error log view | All driver `logerror()` output in a debugger window (1 MiB ring) | `mame/src/emu/debug/debugcon.cpp:38-39`, `:1202` |
| Tracing & logging | `logunmap` | Per-space toggle to log unmapped accesses | `mame/src/emu/debug/debugcpu.cpp:555-577` |
| Tracing & logging | `condump` | Dump console buffer to a file | `mame/src/emu/debug/debugcon.cpp:79`, `:166` |
| History / rewind | PC history | Last 256 PCs per CPU, `history [cpu[,n]]` with disassembly | `mame/src/emu/debug/debugcpu.h:163`; `mame/src/emu/debug/debugcmd.cpp:4309-4351` |
| History / rewind | Rewind | With `-rewind`, a RAM save state is captured before each step/over/out; `rewind`/`rw` (Windows: Ctrl+F11) loads the previous one | `mame/src/emu/debug/debugcpu.cpp:1095`, `:1113`, `:1131`; `mame/src/emu/save.cpp:721-812`; `mame/src/osd/modules/debugger/win/debugwininfo.cpp:737` |
| History / rewind | Save states | `statesave`/`stateload` from the console | `mame/src/emu/debug/debugcmd.cpp:269-272` |
| Video, raster & beam | Beam symbols | `beamx`, `beamy`, `frame` (or `beamxN`...) usable in conditions and shown in the state view | `mame/src/emu/debug/debugcpu.cpp:71-92` |
| Video, raster & beam | `gvblank` | Run to next VBLANK (F8) | `mame/src/emu/debug/debugcpu.cpp:267`, `:337-345` |
| Video, raster & beam | Screen refresh on break | Debugger forces a frame update, so partial (mid-frame) rendering is visible while stopped | `mame/src/emu/debugger.cpp:95-98`; `mame/src/emu/video.cpp:272-281` |
| Video, raster & beam | `snap` | Screenshot from the console | `mame/src/emu/debug/debugcmd.cpp:345`, `:4493` |
| Video, raster & beam | GFX viewer (UI, not debugger) | Palette / gfx-decode / tilemap viewer; Spectrum drivers only decode the ROM character set | `mame/src/frontend/mame/ui/viewgfx.cpp:1622`; `mame/src/mame/sinclair/spectrum.cpp:697-710` |
| Sound & device views | Devices window | Device tree and per-device information (Qt) | `mame/src/osd/modules/debugger/qt/deviceswindow.cpp`, `deviceinformationwindow.cpp` |
| Sound & device views | Save-state items as memory | Any device's internal state arrays (AY registers, ULA fields, etc.) viewable/editable in a memory view | `mame/src/emu/debug/dvmemory.cpp:194-209` |
| Profiling, coverage, CDL | `trackpc` | Marks visited instructions (address + opcode CRC) in the disassembly (light-blue background) | `mame/src/emu/debug/debugcpu.cpp:885-889`, `:1663-1670` |
| Profiling, coverage, CDL | `trackmem` / `pcatmem` | Records, for each (space, address, value), the PC that last wrote it | `mame/src/emu/debug/debugcpu.cpp:629-676`, `:1689-1700` |
| Profiling, coverage, CDL | Cheat search | `cheatinit`/`cheatrange`/`cheatnext`/`cheatnextf`/`cheatlist`/`cheatundo` RAM-search engine | `mame/src/emu/debug/debugcmd.cpp:299-314`, `:3107-3607` |
| Scripting & remote | Console scripting | `;` command chaining, `{}` grouping, actions on points, `source file`, `-debugscript` at startup | `mame/src/emu/debug/debugcon.cpp:346-366`, `:475-541`; `mame/src/emu/debug/debugcmd.cpp:367-370` |
| Scripting & remote | GDB stub (C++) | `-debugger gdbstub`, default port 23946; `g/G/m/M/p/P/s/c/z/Z/qXfer features/qRcmd`; Z80, Z80N maps included | `mame/src/osd/modules/debugger/debuggdbstub.cpp:409-433`, `:706-740`, `:1694-1721`; `mame/src/osd/modules/lib/osdobj_common.cpp:62-63` |
| Scripting & remote | GDB stub (Lua plugin) | Alternate stub in Lua (i386 only register map) | `mame/plugins/gdbstub/init.lua:12-26` |
| Scripting & remote | Lua debugger API | `manager.machine.debugger`: `command()`, `consolelog`, `errorlog`, `visible_cpu`, `execution_state`; `device.debug`: step/go/bpset/wpset/...; `symbol_table`, `parsed_expression` | `mame/src/frontend/mame/luaengine_debug.cpp:240-531` |
| Scripting & remote | Lua memory taps | `install_read_tap` / `install_write_tap` on any address space; bank entry get/set | `mame/src/frontend/mame/luaengine_mem.cpp:270-290`, `:673-690`, `:735-737` |
| Scripting & remote | Lua frame hooks | `emu.add_machine_frame_notifier`, `register_periodic`, reset/stop/pause notifiers | `mame/src/frontend/mame/luaengine.cpp:988-1037` |
| Scripting & remote | Custom driver commands | Drivers can register console commands (`helpcustom` lists them); none in Sinclair drivers | `mame/src/emu/debug/debugcon.cpp:78`; `mame/src/devices/machine/acorn_memc.cpp:96-99` |
| Import / export & persistence | Window layout | XML config: window type/position/size, memory format/radix/region, disasm right column, points type, splits, cursor, scroll, per-window expression history, color theme | `mame/src/osd/modules/debugger/xmlconfig.h:24-68` |
| Import / export & persistence | Media | `images`, `mount`, `unmount` from the console | `mame/src/emu/debug/debugcmd.cpp:360-362` |
| Import / export & persistence | Input injection | `input "text"` posts keystrokes via natural keyboard; `dumpkbd` dumps the key mapping | `mame/src/emu/debug/debugcmd.cpp:4823-4860` |
| UI conveniences | Help | `help [topic|command]`, prefix matching; `helpcustom` | `mame/src/emu/debug/debughlp.cpp:34-55`, `:2069-2120` |
| UI conveniences | Command abbreviation | Any unambiguous prefix of a command works | `mame/src/emu/debug/debugcon.cpp:288-299` |
| UI conveniences | Points windows | Breakpoints/watchpoints/registerpoints/exceptionpoints lists, sortable by column, click to toggle enable | `mame/src/emu/debug/dvwpoints.cpp:167-197`; `mame/src/osd/modules/debugger/qt/breakpointswindow.cpp:51-73` |
| UI conveniences | Themes | Light/dark background (Windows), color theme saved in config | `mame/src/osd/modules/debugger/win/consolewininfo.cpp:248-253`; `mame/src/osd/modules/debugger/xmlconfig.h:58` |

---

## 2. CPU & registers, execution model

- **Per-device debug object.** Every device gets a `device_debug`; CPUs (devices with `device_execute_interface` + state) get a private symbol table chained to the global one, with `cycles`, `totalcycles`, `lastinstructioncycles`, and one symbol per state entry (floats skipped: "TODO: floating point registers") (`mame/src/emu/debug/debugcpu.cpp:548-599`).
- **State view** rows: `cycles`, then beam rows (`beamx`, `beamy`, `frame`, or indexed per screen up to 8 screens), divider, then the CPU's visible state entries. A row is highlighted when its value changed since the last update, keyed on `total_cycles` changing (`mame/src/emu/debug/dvstate.cpp:116-156`, `:203-312`). The state view has no `view_char`, so registers are edited through `do reg=value` or the console, not in place (*inferred*: no `view_char` override in `dvstate.h`).
- **Z80 specifics.** `WZ` (MEMPTR), `IR`, `IM`, `IFF1/2`, `HALT` are exposed; `CURPC` (`STATE_GENPCBASE`) is the PC of the current instruction and is what breakpoints and history use (`mame/src/devices/cpu/z80/z80.cpp:724-753`, `mame/src/devices/cpu/z80/z80.lst:1096-1097`). While HALTed, the core calls `debugger_wait_hook()` instead of the instruction hook (`mame/src/devices/cpu/z80/z80.lst:1089-1093`), so breakpoints do not fire at the HALT address repeatedly; registerpoints and `gtime` still work there (`mame/src/emu/debug/debugcpu.cpp:1001-1030`).
- **Multi-CPU.** `focus`/`ignore`/`observe` toggle `DEBUG_FLAG_OBSERVING`; `suspend`/`resume` actually stop a CPU from executing; `next` (F6) runs until a different observed device is scheduled (`mame/src/emu/debug/debugcpu.cpp:293-315`, `:1046-1080`).

## 3. Disassembly

- Sources = every device with a disassembler and an `AS_PROGRAM` logical space (`mame/src/emu/debug/dvdisasm.cpp:91-110`). The ZX Next Copper (`specnext_copper_device` has memory + disasm interfaces but no execute interface) therefore shows up as its own disassembly source with `WAIT h,v` / `MOVE reg,val` / `NOOP` / `HALT` (`mame/src/mame/sinclair/next/specnext_copper_dasm.cpp`).
- The view follows `curpc` by default; any other expression makes it a fixed/tracking listing (`mame/src/emu/debug/dvdisasm.cpp:327-336`). Row attributes: current PC = `DCA_CURRENT` (yellow), breakpoint = `DCA_CHANGED` (red), cursor = `DCA_SELECTED`, visited = `DCA_VISITED`, comments green (`mame/src/emu/debug/dvdisasm.cpp:455-489`, `mame/src/emu/debug/debugvw.h:54-62`).
- Right column: raw opcodes, "encrypted" opcodes (bytes from `AS_OPCODES` vs `AS_PROGRAM` — useful for M1-decrypted systems), or comments; toggled with Ctrl+R / Ctrl+E / Ctrl+N (`mame/src/osd/modules/debugger/qt/dasmwindow.cpp:98-116`).
- Disassembly reads memory through `debug_disasm_buffer`, which reads with side effects disabled (*inferred* from the driver side: Spectrum clone paging on `3Dxx` fetches is guarded by `side_effects_disabled()`, `mame/src/mame/sinclair/pentagon.cpp:99-123`).
- Trace line format is `"%s: %s\n"` = PC, disassembly (`mame/src/emu/debug/debugcpu.cpp:2127`), with Z80 mnemonics padded `%-4s` and `$`-prefixed hex, e.g. `8000: ld   a,$01` (`mame/src/devices/cpu/z80/z80dasm.cpp:477-511`).

## 4. Memory views and address spaces

- **Sources** (`mame/src/emu/debug/dvmemory.cpp:163-212`), in order:
  1. every logical space of every device (`"Z80 ':maincpu' program space memory"`, `... io space`, `... opcodes space`);
  2. every memory region (`Region ':maincpu'`, ROMs);
  3. every save-state registration except timers, sorted by name. On Spectrum drivers this includes the RAM device's `m_pointer` array (whole 48K/128K/... buffer, all pages flat) because `ram_device` registers it with `save_pointer` (`mame/src/devices/machine/ram.cpp:136-142`). This is the only way to look at a non-paged-in 128K bank in a memory window; the CPU space view always shows whatever is currently paged.
- **Logical vs physical** toggle (Ctrl+G / Ctrl+Y in Qt) uses `device_memory_interface::translate` — meaningful for MMU CPUs, identity for Z80 (`mame/src/emu/debug/dvmemory.h:83`, `mame/src/osd/modules/debugger/qt/memorywindow.cpp:146-156`).
- **Unmapped** bytes render as `*` (`mame/src/emu/debug/dvmemory.cpp:306`).
- **Banks.** There is no "bank" selector in the view; banking is visible through `map addr` which prints, for read/write/fetch, the physical address and the handler name (bank names for `bankr`/`bankrw` maps, delegate names otherwise) (`mame/src/emu/debug/debugcmd.cpp:4551-4585`). Pentagon maps banks directly (`mame/src/mame/sinclair/pentagon.cpp:127-130`); Spectrum 128 uses per-bank handler functions (`mame/src/mame/sinclair/spec128.cpp:199-218`). `memdump` writes the complete live address-map tree to a file (`mame/src/emu/debug/debugcmd.cpp:4588`).
- **Commands with space suffixes**: most memory commands exist as `x`, `xd`, `xi`, `xo` for program/data/I/O/opcodes spaces, and accept `addr:cpu` or `addr:cpu:space` or `addr:region.m` / `addr:share.s` (`mame/src/emu/debug/debugcon.cpp:784-935`, `mame/src/emu/debug/debughlp.cpp:95-131`).
- `find` searches for sequences mixing strings, `?` wildcards and sized values (`b.`, `w.`, `d.`, `q.` prefixes, sticky) and prints all hits (`mame/src/emu/debug/debughlp.cpp:639-672`).

## 5. Symbols, labels, comments

- **No label support.** The symbol tables contain only registers, built-ins and save-state globals; there is no command or file format for user labels or symbol import (*inferred*: no label/sym code under `mame/src/emu/debug`). Disassembly never shows symbolic operands.
- Built-in global symbols: `temp0..temp9` (read/write scratch), `wpaddr`, `wpdata`, `wpsize`, `beamx/beamy/frame`, `cpunum`, and `.name` for every single-value `/globals/` save-state item (`mame/src/emu/debug/debugcpu.cpp:63-101`, `mame/src/emu/debug/debugcmd.cpp:156-183`).
- **Comments** are stored per CPU in a `std::set<dasm_comment>` ordered by (address, CRC32 of the instruction's opcode bytes) (`mame/src/emu/debug/debugcpu.h:259-289`). Lookup recomputes the CRC of the bytes currently at the address (`mame/src/emu/debug/debugcpu.cpp:1743-1750`), so a comment only shows when the same code is paged in: a cheap form of **bank-aware annotation** without knowing the banking scheme. Persistence: `comsave` writes `<comment_directory>/<system>.cmt` as XML `<mamecommentfile><system><cpu><comment address= color= crc=>text</comment>` (`mame/src/emu/debug/debugcpu.cpp:131-196`, `:1757-1768`); loaded lazily on first stop (`mame/src/emu/debug/debugcpu.cpp:366`, `:428`). `commit` = add + save.

## 6. Breakpoints (every kind)

| Kind | Keying | Check site | Notes |
|---|---|---|---|
| Breakpoint (`bp`) | Logical PC (`offs_t`), `std::multimap<offs_t, ...>` per CPU | `instruction_hook` → `breakpoint_check(curpc)` → `equal_range(pc)` | Condition + action; first hit stops, prints `Stopped at breakpoint N` (`mame/src/emu/debug/debugcpu.cpp:1981-2009`) |
| Temporary (`go addr`, `gni`, run-to-cursor) | Single `m_stopaddr` | `DEBUG_FLAG_STOP_PC` compare | `gni` walks the disassembler N instructions, max 512 (`mame/src/emu/debug/debugcmd.cpp:950-989`) |
| Registerpoint (`rp`) | None — condition only | Every instruction (`registerpoint_check`) and in wait hook | Generic "stop when expression true", e.g. `rp {hl==4000 && b@4000==0}` (`mame/src/emu/debug/debugcpu.cpp:2016-2045`) |
| Exceptionpoint (`ep`) | Exception number | `exception_hook` from CPU cores | Z80 core never calls `debugger_exception_hook` (no hits in `mame/src/devices/cpu/z80/`), so useless on Spectrum |
| Interrupt stop (`gint [line]`) | IRQ line or any | `interrupt_hook` from `standard_irq_callback` / Z80 daisy ack | NMI entry on Z80 does not call the hook (`mame/src/devices/cpu/z80/z80.lst:890-903` vs `:924-935`) |
| Branch stop (`gbt`/`gbf [cond]`) | Next conditional branch taken / not taken | Disassembler `STEP_COND` flag | (`mame/src/emu/debug/debugcpu.cpp:1244-1256`, `:1880-1940`) |
| Privilege (`gp`) | Privilege level change | `privilege_hook` | CPU-specific |
| Time (`gtime ms`) | Emulated time | `machine.time() >= m_stoptime` | (`mame/src/emu/debug/debugcpu.cpp:957-961`) |
| VBLANK (`gv`) | Screen VBLANK callback | `start_hook` on next slice | Stops at the next scheduler slice after VBLANK, not at a precise instruction (`mame/src/emu/debug/debugcpu.cpp:333-345`) |

- **Bank awareness:** none for breakpoints. The breakpoint compares only the logical PC (`mame/src/emu/debug/points.cpp:48-57`); `bpset` validates the address in `AS_PROGRAM` but never translates it (`mame/src/emu/debug/debugcmd.cpp:1380-1410`). Bank-specific breaks must be written as conditions on a bank symbol or port-latch save-state global (e.g. a `.m_port_7ffd_data`-style symbol only if the driver saved it under `/globals/`, *inferred*), or via Lua.
- **Hit counts:** there is no hit counter. The documented idiom is `temp0` in an action: `rp {PC==150},{temp0++; g}` (`mame/src/emu/debug/debughlp.cpp:1540-1543`).
- **Actions** are arbitrary console command strings executed at hit time; if the action ends in `g`, the stop message is suppressed (`mame/src/emu/debug/debugcpu.cpp:1994-2004`). Braces protect commas/semicolons.
- **Condition errors** are swallowed: an `expression_error` makes the point silently not hit (`mame/src/emu/debug/points.cpp:60-67`).

## 7. Conditions & expressions

Engine: `parsed_expression` tokenizes once, converts infix to postfix (`mame/src/emu/debug/express.cpp:1132-1146`, `:1794`), and interprets the token list on each `execute()` (`mame/src/emu/debug/express.cpp:1970`).

- **Operators** (high to low): `()`, postfix `++ --`, prefix `++ -- ~ ! - +` and memory accessors, `* / %`, `+ -`, `<< >>`, `< <= > >=`, `== !=`, `&`, `^`, `|`, `&&`, `||`, assignments `= *= /= %= += -= <<= >>= &= |= ^=`, `,` (`mame/src/emu/debug/express.cpp:10-29`). Word aliases: `bnot plus minus times mul div mod lt le gt ge eq ne not and band or bor bxor lshift rshift` (`mame/src/emu/debug/express.cpp:1434-1473`). No `?:`; use `if(c,a,b)`.
- **Semantics:** all arithmetic is unsigned 64-bit, and `&&`/`||` do **not** short-circuit (`mame/src/emu/debug/debughlp.cpp:238-243`).
- **Numbers:** default base 16; `#` decimal, `$` hex, `0x`, `0o`, `0b`; `'c'` character literals; quoted strings for commands that accept them (`mame/src/emu/debug/express.cpp:1476-1520`, `:1582-1640`).
- **Memory accessors** `[name.][l|p][space]size{@|!}address` (`mame/src/emu/debug/express.cpp:1646-1723`):
  - size: `b` 1, `w` 2, `d` 4, `q` 8 bytes;
  - space: `p` program (default), `d` data, `i` I/O, `3` opcodes; `l`/`p` prefix selects logical (translated) or physical;
  - `r` = direct RAM pointer read of program space, `o` = direct opcode pointer, `m` = memory region by tag, `s` = memory share by tag;
  - optional device tag before a dot, e.g. `maincpu.ib@fe`, `:maincpu.m`;
  - `@` suppresses side effects, `!` performs the access with side effects (`mame/src/emu/debug/express.cpp:1413-1419`, `:637-672`). Accessors are lvalues: `b@4000 = ff`.
  - Unmapped logical reads return all-ones (`mame/src/emu/debug/express.cpp:487-500`).
- **Functions:** `min`, `max`, `if`, `abs`, `bit(v,n[,len])`, `s8`, `s16`, `s32` (`mame/src/emu/debug/debugcmd.cpp:135-155`); up to 16 parameters (`mame/src/emu/debug/express.h:384`). Drivers and Lua can add functions and symbols (`mame/src/frontend/mame/luaengine_debug.cpp:270-314`).
- Writes through expressions mark memory modified, which refreshes disassembly/state views while stopped (`mame/src/emu/debug/express.cpp:623-629`, `mame/src/emu/debug/debugcpu.cpp:455-463`).

## 8. Watchpoints

- `wp[d|i|o] addr[:cpu[:space]],len,r|w|rw[,cond[,action]]` (`mame/src/emu/debug/debugcmd.cpp:1530-1596`). The address is translated once at set time; the watchpoint is installed on the translated space (`mame/src/emu/debug/debugcmd.cpp:1575-1592`).
- **Mechanism:** memory-system *taps* (`install_read_tap`/`install_write_tap`) over the exact range, split into up to three ranges with lane masks for partial first/last words (`mame/src/emu/debug/points.cpp:96-165`, `:211-312`). A change notifier re-installs taps when the address map changes (e.g. bank remap) (`mame/src/emu/debug/points.cpp:177-184`). Cost is zero outside the watched range; no per-access global check.
- **Trigger:** skipped when inside the debugger hook or when side effects are disabled (`mame/src/emu/debug/points.cpp:320`). Sets `wpaddr`, `wpdata`, `wpsize` (for reads, `wpdata` is the value read), evaluates the condition, runs the action, prints `Stopped at watchpoint N writing XX to YYYY (PC=...)` (`mame/src/emu/debug/points.cpp:358-420`). The stop happens mid-instruction; the PC shown is the current instruction.
- **Keying:** watchpoints sit on the CPU address space, so a watch on `C000` fires for whichever 128K page is mapped there. Watching a specific physical page requires a condition on the paging state.
- `wpo` (opcodes space) turns into an execution-range trap on Spectrum drivers because M1 reads go through the separate opcodes map (`mame/src/mame/sinclair/spectrum.cpp:542-545`).

## 9. Execution control

- Commands: `s[tep] [n]`, `o[ver] [n]`, `out`, `g[o] [addr]`, `gv`, `gi [irq]`, `ge [exc[,cond]]`, `gt ms`, `gp [cond]`, `gbt`/`gbf [cond]`, `gni [n]`, `n[ext]` (`mame/src/emu/debug/debugcmd.cpp:198-218`).
- **Step over/out** use disassembler flags: `STEP_OVER` (with `OVERINSTMASK` for delay slots), `STEP_OUT` (returns), `STEP_COND` (conditional); the debugger sets a hidden step address and a `CALL_IN_PROGRESS`/`TEST_IN_PROGRESS` state (`mame/src/emu/debug/debugcpu.cpp:1880-1940`, `:896-950`). Z80 marks CALL, RST, DJNZ, LDIR-class etc. per opcode table (`mame/src/devices/cpu/z80/z80dasm.cpp:35-39`). An interrupt during step-over is handled by `interrupt_hook` (`mame/src/emu/debug/debugcpu.cpp:736-757`).
- Multi-step `s 1000` refreshes views every 100 steps until 200 remain (`mame/src/emu/debug/debugcpu.cpp:944-950`).
- **Hotkeys** (Qt/Win/ImGui identical): F5 run, F12 run and hide, F6 next CPU, F7 next interrupt, F8 next VBLANK, F11 step into, F10 step over, Shift+F11 step out, F3 soft reset, Shift+F3 hard reset, F9 toggle BP, Shift+F9 enable/disable BP, F4 run to cursor; Ctrl+M/D/L/B new memory/disasm/log/points window (`mame/src/osd/modules/debugger/qt/windowqt.cpp:43-108`, `mame/src/osd/modules/debugger/debugimgui.cpp:366-415`). Windows also has Ctrl+F11 rewind step (`mame/src/osd/modules/debugger/win/debugwininfo.cpp:737`). Break into the debugger from the running machine: `~` (`mame/src/emu/inpttype.ipp:911`).

## 10. Tracing & logging

- `trace {file|off}[,cpu[,noloop|logerror[,action]]]`; `traceover` skips subroutine bodies (sets a return target from `STEP_OVER` info) (`mame/src/emu/debug/debugcmd.cpp:4222-4297`, `mame/src/emu/debug/debugcpu.cpp:2128-2140`).
- **Loop detection:** a 64-entry ring of recent PCs; a PC seen more than once in the ring is not logged and a counter increments; when the loop ends: `   (loops for N instructions)` (`mame/src/emu/debug/debugcpu.h:239-244`, `mame/src/emu/debug/debugcpu.cpp:2092-2110`).
- **Interrupts** are logged as a separate line `   (interrupted at PC, IRQ n)` (`mame/src/emu/debug/debugcpu.cpp:2150-2167`).
- **Action per line**, run before the line is written; the usual pattern adds registers: `trace t.log,,,{tracelog "A=%02X HL=%04X ",a,hl}` (`mame/src/emu/debug/debughlp.cpp:1188-1230`). `tracesym a,hl` emits `a=.. hl=..` using each symbol's format (`mame/src/emu/debug/debugcmd.cpp:732-770`).
- The file is flushed after every line (`mame/src/emu/debug/debugcpu.cpp:2145`) — safe but slow.
- Sample (Z80, with a `tracelog` action):
  ```
  A=3F HL=5C3A 0038: push af
  A=3F HL=5C3A 0039: push hl
     (loops for 212 instructions)
  ```
  *(format assembled from the code paths above; not captured from a run)*
- `logerror`-style driver logging (`LOG_*` masks, e.g. `mame/src/mame/sinclair/next/specnext.cpp:50-56`) goes to `error.log` and the debugger's error-log window; `trace ...,logerror` interleaves it into the trace (`mame/src/emu/debug/debugcpu.cpp:2239-2243`).

## 11. History / rewind / time travel

- **PC history:** ring of 256 PCs per CPU, always recorded while the debugger is enabled (`DEBUG_FLAG_HISTORY` set by default) (`mame/src/emu/debug/debugcpu.h:163`, `mame/src/emu/debug/debugcpu.cpp:603`, `:869-872`). No register or memory history.
- **Rewind:** option `-rewind` (default off) and `-rewind_capacity` MB (default 100, range 1–2048) (`mame/src/emu/emuopts.cpp:72-73`). A full in-RAM save state (`ram_state`) is captured *before* every `step`/`over`/`out` (`mame/src/emu/debug/debugcpu.cpp:1095`, `:1113`, `:1131`) and before a UI single-frame step (`mame/src/frontend/mame/ui/ui.cpp:1846`). `rewind` loads the previous state; newer states are invalidated on the next capture; oldest are dropped at capacity (`mame/src/emu/save.cpp:721-830`). No capture during `go`, so you can only rewind across manual steps. Rewind clears `trackpc`/`trackmem` data (`mame/src/emu/debug/debugcmd.cpp:2032-2044`).
- The help text states this is "not actual reverse execution" (`mame/src/emu/debug/debughlp.cpp:524-535`).

## 12. Video, raster & beam

- `beamx`/`beamy`/`frame` come from `screen_device::hpos()/vpos()/frame_number()` (`mame/src/emu/debug/debugcpu.cpp:76-92`). On the 48K driver the screen is `set_raw(X1/2, SPEC_CYCLES_PER_LINE*2, ...)` with the CPU at `X1/4` (`mame/src/mame/sinclair/spectrum.cpp:732`, `:754`), so `beamx` advances two pixels per T-state. These can be used in conditions, e.g. `bp 8000,beamy==40`, which gives raster-position breakpoints with no special feature.
- No beam crosshair, no per-T-state event view, no contention view, no raster-effect visualization in any front end (*inferred*: no `beam` references under `mame/src/osd/modules/debugger`).
- When stopped, `refresh_display()` calls `video().frame_update(true)`, which finishes the screen and then resets partial updates, so the host window shows the frame as rendered so far (above the beam: this frame; below: current VRAM content) (`mame/src/emu/debugger.cpp:95-98`, `mame/src/emu/video.cpp:216-281`) *(the above/below split is inferred from `finish_screen_updates`)*. Spectrum drivers call `m_screen->update_now()` on every VRAM or border write (`mame/src/mame/sinclair/spectrum.cpp:322`, `:352`; `mame/src/mame/sinclair/spec128.cpp:204`), so the partially drawn frame is accurate to the write.

## 13. ZX Spectrum drivers — what reaches the debugger

- **Address spaces:** Z80 program, I/O, and a separate opcodes (M1) map on the 48K/128K families (`mame/src/mame/sinclair/spectrum.cpp:542-560`, `:733`). Hence `wpo` (fetch watch), `3b@` (opcode-space reads), and "encrypted opcodes" column read the M1 path.
- **Contention** (`spectrum_ula_contended_device`, optional via a config switch "Contention" with early/late timing) is done by `adjust_icount` inside the memory/I/O handlers (`mame/src/mame/sinclair/spectrum_ula.cpp:37-140`, `mame/src/mame/sinclair/spectrum.cpp:665`). It is not exposed as a debugger view or symbol; its effect is visible only via `totalcycles` / `lastinstructioncycles` deltas.
- **Side-effect discipline:** read-side contention (`m1`, `data_r`, `io_r`, `ula_r`) is skipped when `side_effects_disabled()` (debugger peeks), but write-side (`data_w`, `io_w`, `ula_w`) is not guarded (`mame/src/mame/sinclair/spectrum_ula.cpp:48-96`). Beta-disk ROM paging on `3Dxx` fetches is guarded (`mame/src/mame/sinclair/pentagon.cpp:99-123`).
- **Floating bus** is computed from beam position and `total_cycles` in `floating_bus_r` (`mame/src/mame/sinclair/spectrum.cpp:505-527`); a debugger `ib@ff` read returns the value for the current beam position.
- **Paging:** 128K-class drivers use `memory_bank` objects (`bank_rom0`, `bank_ram0..3`) (`mame/src/mame/sinclair/spec128.h:22-37`); `map c000` shows the handler; `memory_bank.entry` is readable/writable from Lua (`mame/src/frontend/mame/luaengine_mem.cpp:735-737`). ZX Next uses eight `memory_view`s (`mame/src/mame/sinclair/next/specnext.cpp:117-124`) and installs its own read/write taps for shadowing and wait states (`mame/src/mame/sinclair/next/specnext.cpp:4107-4120`).
- **Next extras:** Z80N has a GDB register map (`mame/src/osd/modules/debugger/debuggdbstub.cpp:725`); the Copper has a disassembly source; LOG masks for IO/MEM/COPPER/INT (`mame/src/mame/sinclair/next/specnext.cpp:50-56`). No custom debugger commands in any Sinclair driver (no `register_command` under `mame/src/mame/sinclair`).
- Driver notes still list "No contended memory" in the header comments of `spectrum.cpp`/`spec128.cpp` (`mame/src/mame/sinclair/spectrum.cpp:132-147`, `mame/src/mame/sinclair/spec128.cpp:126-127`), which is out of date relative to the ULA device.

## 14. Code/data logging, coverage, RAM search

- **No CDL** (no code/data byte classification or export).
- `trackpc` inserts (PC, CRC32 of opcode bytes) into a `std::set` on every instruction while enabled; the disassembly paints matching rows (`mame/src/emu/debug/debugcpu.cpp:885-889`, `:1663-1680`). The CRC key makes coverage **bank-aware by content** (same address, different code = different entry).
- `trackmem` installs a full-range write tap per space and records `(space, address, value) -> last writer PC` (`mame/src/emu/debug/debugcpu.cpp:629-676`, `mame/src/emu/debug/debugcpu.h:291-317`). Query with `pcatmem addr` or right-click in a memory view; lookup uses the *current* value at the address, so it answers "who wrote the value that is there now".
- **Cheat search**: `cheatinit [sign][width][swap],addr,len[,space]`, `cheatnext equal|notequal|decrease|increase|decreaseorequal|increaseorequal|smallerof|greaterof|changedby ...`, `cheatnextf` (compare with first snapshot), `cheatlist [file]` (writes MAME cheat XML), `cheatundo` (`mame/src/emu/debug/debughlp.cpp:1870-1930`, `mame/src/emu/debug/debugcmd.cpp:3107-3607`).
- **Profiling:** none for guest code.

## 15. Scripting, automation & remote protocols

- **Console language:** commands separated by `;`; `{}` groups; quotes; `name=expr` lines are treated as `do` expressions; `//` comments in `source` files (`mame/src/emu/debug/debugcon.cpp:346-366`, `:505-529`). Up to 128 parameters (`mame/src/emu/debug/debugcon.h:26`).
- **Startup script:** `-debugscript file`; `-debuglog` mirrors the console to `debug.log` (`mame/src/emu/emuopts.cpp:184-187`).
- **GDB stub** (`-debugger gdbstub -debugger_host -debugger_port 23946`): packets `! ? c D g G H k m M p P q s T z Z`; `qSupported` → `PacketSize=4000;qXfer:features:read+;qOffsets+`; target XML generated from per-CPU register maps; `qRcmd` ("monitor") forwards to the MAME console; `Z0/Z1` → breakpoint, `Z2/3/4` → write/read/access watchpoint with GDB-address remap; stop reply includes `watch/rwatch/awatch:addr` (`mame/src/osd/modules/debugger/debuggdbstub.cpp:32`, `:1411-1530`, `:1619-1692`). Memory reads disable side effects (`mame/src/osd/modules/debugger/debuggdbstub.cpp:1322`). Z80 register map: AF BC DE HL AF' BC' DE' HL' IX IY SP PC IR (`mame/src/osd/modules/debugger/debuggdbstub.cpp:409-433`).
- **Lua:** `manager.machine.debugger:command(str)`, `.consolelog`/`.errorlog` (indexable text buffers), `.visible_cpu`, `.execution_state` ("run"/"stop"); `cpu.debug:step(n)`, `:go(addr)`, `:bpset(addr,cond,act)`, `:bplist()`, `:wpset(space,type,addr,len,cond,act)`, ...; `emu.symbol_table`, `emu.parsed_expression` (`mame/src/frontend/mame/luaengine_debug.cpp:240-531`). Memory: `space:read_u8`/`readv_*` (virtual/translated)/`read_range`, `install_read_tap`/`install_write_tap` returning passthrough handlers, `space.map` introspection (`mame/src/frontend/mame/luaengine_mem.cpp:544-690`). Frame/reset/pause notifiers (`mame/src/frontend/mame/luaengine.cpp:988-994`). The `console` plugin provides a terminal Lua REPL that also tails the debugger console (`mame/plugins/console/init.lua:226-227`).

## 16. Front ends & persistence

- **Qt** (`-debugger qt`): main window with console + registers + disassembly docks; separate memory, disassembly, error-log, points, devices, device-info windows; per-window command/expression history (Up/Down); image mount/unmount menu (`mame/src/osd/modules/debugger/qt/mainwindow.cpp:62-135`, `:220-246`, `:481-494`).
- **Windows** (`-debugger windows`): same windows, 100-entry edit history, cassette transport controls in the Media menu, light/dark background, "group debugger windows", save window arrangement (`mame/src/osd/modules/debugger/win/editwininfo.cpp:30`, `mame/src/osd/modules/debugger/win/consolewininfo.cpp:243-253`, `:402-427`).
- **Cocoa** (`-debugger osx`) and **ImGui** (`-debugger imgui`, rendered inside the MAME window with `-debugger_font`/`-debugger_font_size`) wrap the same core views (`mame/src/osd/modules/debugger/debugosx.mm:79`, `mame/src/osd/modules/debugger/debugimgui.cpp:1545-1546`).
- **Persisted:** window layout and view options (XML in the system cfg file) (`mame/src/osd/modules/debugger/xmlconfig.h:24-68`), comments (`.cmt`). **Not persisted:** breakpoints, watchpoints, registerpoints, trackpc/trackmem data (*inferred*: no points in `xmlconfig`/`debugqt.cpp` config handlers).

## 17. Performance approach

- **Single global gate.** CPU cores call `debugger_instruction_hook(pc)`, which is an inline test of `machine.debug_flags & DEBUG_FLAG_CALL_HOOK` (`mame/src/emu/diexec.h:224-229`). Without `-debug`, `DEBUG_FLAG_ENABLED` is off and the cost is one predictable branch per instruction.
- `compute_debug_flags()` sets `CALL_HOOK` only when something needs per-instruction attention: history, stepping, temp BP, live BP/RP, tracing, time stop within the slice (`mame/src/emu/debug/debugcpu.cpp:1839-1872`). Because history is on by default, **with `-debug` the hook runs for every instruction of every observed CPU**; `ignore` removes it for a CPU.
- **Breakpoints** are a `std::multimap` lookup per instruction (`equal_range`) guarded by a `LIVE_BP` flag that is recomputed on add/enable (`mame/src/emu/debug/debugcpu.cpp:1949-1975`, `:1981`). Registerpoints evaluate every condition every instruction (interpreted token list) — the expensive path.
- **Watchpoints and trackmem** cost nothing on the instruction path: they are memory-system taps on exact ranges, re-installed on map changes (`mame/src/emu/debug/points.cpp:177-184`).
- **trackpc** computes a CRC32 of the opcode bytes and a `std::set` insert per instruction (`mame/src/emu/debug/debugcpu.cpp:885-889`); display lookups recompute CRCs per visible row.
- **Tracing** disassembles and flushes the stream every instruction (`mame/src/emu/debug/debugcpu.cpp:2119-2145`).
- **UI refresh** while running: views update at most 4 times per second of host time (`mame/src/emu/debug/debugcpu.cpp:318-323`).

---

## 18. Notable and unique ideas

1. **Opcode-CRC keyed annotations and coverage** (`comments`, `trackpc`): keys of (address, CRC32 of instruction bytes) make comments and visited-marks follow the *code*, not the address, so they survive and stay correct across 128K/TR-DOS/+3 paging without modelling the banking scheme. Very cheap way to get bank-aware annotation.
2. **Watchpoints as memory-system taps** with a map-change notifier: zero cost outside watched ranges, correct across rebanking, sub-word lane masking. A direct model for bank-aware watchpoints on physical pages if taps are placed on the page, not the CPU address.
3. **One expression language everywhere** (conditions, actions, view addresses, `do`, `printf`, Lua): with side-effect-free (`@`) vs side-effecting (`!`) memory access, address-space and region/share prefixes, assignable lvalues, `temp0..9` scratch registers. Enables hit counters, conditional logging, and "break when `beamy==N`" raster breakpoints with no dedicated feature.
4. **Registerpoints** (break on arbitrary expression, no address) — a general "stop when state predicate is true" that covers bank-conditional breaks, SP-underflow checks, and memory-value triggers.
5. **Actions + `g`** pattern: any point can log and continue; together with `tracelog`/`tracesym` it turns breakpoints into non-stopping probes.
6. **`trackmem`/`pcatmem`**: "which PC wrote the byte that is here now", available from a right-click in the memory view. Useful for finding self-modifying code and screen writers on the Spectrum.
7. **Trace loop condensation and `traceover`**: keeps traces readable (e.g. LDIR/DJNZ delay loops, ROM key-scan loops) and interrupt entries annotated inline.
8. **Separate M1/opcodes address space**: lets the debugger distinguish fetches from data reads (`wpo` fetch-range traps, "encrypted opcodes" column). For a ZX emulator this maps naturally to M1-triggered paging (TR-DOS `3Dxx`, Interface 1 `0008`).
9. **Save-state items as memory sources**: every device's registered state (whole RAM, AY registers, ULA latches) becomes a browsable, editable memory view for free, including pages not currently mapped.
10. **Rewind snapshots per manual step**: cheap reverse-step for interactive stepping (full RAM state ring, capacity in MB).
11. **Custom driver commands** (`register_command` + `helpcustom`): drivers can add hardware-specific console commands (e.g. a paging or contention dump) without touching the core.
12. **GDB stub with `monitor` passthrough** and Z80/Z80N register maps; plus a Lua API able to set points, read consoles and install memory taps — enough for external tooling.

## 19. Gaps and caveats

- **No user labels/symbols**, no symbolic disassembly, no symbol file import (`.sym`, `.map`, SjASMPlus labels).
- **Breakpoints are not bank-aware**: logical PC only (`mame/src/emu/debug/points.cpp:52-53`); no physical/page qualifier; no hit counts, no ignore counts, no temporary one-shot flag on `bp`.
- **Breakpoints and watchpoints are not persisted** across sessions (only window layout and comments are).
- **Exceptionpoints do nothing on Z80**; `gint` does not see NMI entries on Z80 (NMI path lacks `interrupt_hook`) (`mame/src/devices/cpu/z80/z80.lst:890-903`).
- **`logunmap` symbol bug:** the per-space `logunmap` symbols are all added under the same name, and `symbol_table::add` erases the previous entry, so only the last space present (I/O on Z80) is controllable (`mame/src/emu/debug/debugcpu.cpp:555-577`, `mame/src/emu/debug/express.cpp:395-432`).
- **Expressions:** unsigned-only math (`a < 0` never true), no short-circuit `&&`/`||` (so `hl!=0 && b@hl==0` always reads memory), expression errors in conditions silently mean "no hit" (`mame/src/emu/debug/points.cpp:60-67`).
- **History** is PCs only (256); no register/memory history, no cycle stamps. **Rewind** only captures at manual steps, not during `go`, and discards tracking data; not true reverse execution.
- **Watchpoints stop mid-instruction**, and on read watchpoints `wpdata` is the value read; there is no "value changed" watch type (must be expressed as `wpdata != b@wpaddr` in a write-watch condition).
- **Contention is invisible** to the debugger (no view, no per-instruction contention figure); write-side contention handlers are not guarded by `side_effects_disabled()`, so debugger writes into contended RAM or `OUT` via expressions can adjust the CPU icount (*inferred* from `mame/src/mame/sinclair/spectrum_ula.cpp:60-96`).
- **No raster/beam visualization**, no event viewer, no screen/attribute viewer, no sound/AY register panel (AY state only via save-state memory sources).
- **Registerpoints and conditions are interpreted** every instruction (token-list interpreter); many `rp` entries slow execution noticeably *(inferred from `mame/src/emu/debug/express.cpp:1970`)*.
- **Trace flushes every line** (`mame/src/emu/debug/debugcpu.cpp:2145`); no binary trace format, no register columns without an action.
- `gvblank` stops at the next scheduler slice after VBLANK, not exactly at the VBLANK instruction (`mame/src/emu/debug/debugcpu.cpp:333-345`).
- Help text glitch: the general-help line for `rewind` lacks a trailing newline and runs into the `statesave` line (`mame/src/emu/debug/debughlp.cpp:80-81`).
- Driver header comments in `spectrum.cpp`/`spec128.cpp` still claim no contention although `spectrum_ula_contended_device` implements it.
