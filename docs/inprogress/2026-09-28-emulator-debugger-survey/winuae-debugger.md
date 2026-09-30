# WinUAE debugger — capability survey

**Source:** https://github.com/tonioni/WinUAE.git · local checkout commit `d122d453` (2026-07-09, `git describe` 6030-353-gd122d453; version constants 6.1.0 public beta 9, `WinUAE/include/options.h:17`, `WinUAE/od-win32/win32.h:15`) · C++ / Win32 (console window plus an optional Win32 dialog debugger)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** A text console monitor (`WinUAE/debug.cpp`, 9527 lines) is the core. It has a cycle-exact per-color-clock DMA recorder with an on-screen overlay, a memory heat map, a copper tracer, validators and OS awareness (AmigaOS tasks, SegTracker, a "debugmem" sandbox with symbols and source lines). A Win32 multi-pane GUI front end (`WinUAE/od-win32/debug_win32.cpp`) sits on the same command parser. There is no GDB stub in this tree; debugger commands can be injected over a named-pipe IPC, through input events or from the Amiga side.

Terminology used below: **CCK** = color clock, the Amiga chipset's DMA slot unit (one bus slot, 2 CPU clocks on a 7 MHz 68000). **hpos/vpos** = beam position in CCKs and lines. **Agnus/Denise** = the DMA and video chips (the Spectrum ULA does both jobs). **Copper** = the display coprocessor that runs a list of beam-synchronized register writes. **Blitter** = the block-copy DMA engine.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | `r` | Dump chipset state and full 68k state | `WinUAE/debug.cpp:7304` |
| CPU & registers | `r <reg> <value>` | Modify Dx/Ax/USP/ISP/VBR/... | `WinUAE/debug.cpp:7315`, `WinUAE/debug.cpp:7088` |
| CPU & registers | `rc[d]` | Show CPU instruction/data cache contents | `WinUAE/debug.cpp:7308` |
| CPU & registers | `rs` / `rss` | List the tracked user / supervisor call-stack frames | `WinUAE/debug.cpp:7311` |
| CPU & registers | `i [addr]`, `c`, `e`, `ea`, `ex` | Vector table dump; CIA, disk and custom registers; AGA colors; last value written per custom register plus the PC (or copper address) that wrote it | `WinUAE/debug.cpp:7272`, `WinUAE/debug.cpp:7271`, `WinUAE/debug.cpp:1485` |
| Disassembly | `d [addr] [n]`, `dppc`, `do` | 68k disassembly (PPC mode switch) with symbols, segments, source lines, library LVO names | `WinUAE/debug.cpp:7382`, `WinUAE/disasm.cpp:1984`, `WinUAE/disasm.cpp:2396` |
| Disassembly | `a [addr] [insn]` | Line assembler, interactive mode | `WinUAE/debug.cpp:7359` |
| Memory views | `m addr [lines]` | Hex and ASCII dump; unmapped words shown as `****` with the bank name | `WinUAE/debug.cpp:1437` |
| Memory views | `W`, `Wf`, `Wc`, `L`, `S` | Write values or a string, fill, copy, load or save a block to a file | `WinUAE/debug.cpp:5270`, `WinUAE/debug.cpp:5221`, `WinUAE/debug.cpp:6373` |
| Memory views | `s "str"/bytes [start] [end]`, `fa <ea>` | Search memory; find instructions whose effective address equals `<ea>` | `WinUAE/debug.cpp:6438`, `WinUAE/debug.cpp:6908` |
| Memory views | `dm`, `mmu`, `mmud`, `U addr` | Address-space map; MMU function code, MMU table dump, logical-to-physical translation | `WinUAE/debug.cpp:7394`, `WinUAE/debug.cpp:7667`, `WinUAE/debug.cpp:7860` |
| Symbols & labels | debugmem symbols | Hunk `HUNK_SYMBOL`/`HUNK_DEBUG`, ELF, stabs and `.debug_line` lines; `seg`/`segs` list segments | `WinUAE/debugmem.cpp:1777`, `WinUAE/debugmem.cpp:2583`, `WinUAE/debug.cpp:7329` |
| Symbols & labels | Library LVO symbols | `.fd` files plus `amiga.lib`; `TL` binds library bases; `lib/Function` usable in expressions | `WinUAE/debugmem.cpp:2318`, `WinUAE/debugmem.cpp:3604` |
| Breakpoints | `f addr [Nx] [Hx]` | PC breakpoint (toggle), hit count, chained ("only after bp x") | `WinUAE/debug.cpp:6187` |
| Breakpoints | `fo n reg oper val [val2] [mask]` | Register condition breakpoint (Dx, Ax, PC, SR, FPU and MMU regs); signed/unsigned, range and not-range | `WinUAE/debug.cpp:6196`, `WinUAE/debug.cpp:7955` |
| Breakpoints | `f a1 a2`, `f` | Break when PC is in a range; break when PC first enters RAM ("boot block finder") | `WinUAE/debug.cpp:6318`, `WinUAE/debug.cpp:6347` |
| Breakpoints | `fi [opcode/asm]` | Break on RTS/RTD/RTE or on a given opcode (up to 3 words, or assembler text) | `WinUAE/debug.cpp:6237` |
| Breakpoints | `il [mask]`, `tx` | Exception breakpoint by vector bitmask; break on any exception | `WinUAE/debug.cpp:7274`, `WinUAE/debug.cpp:7936` |
| Breakpoints | `fp "name"/addr` | Break when a given AmigaOS task/process runs code in its own segments | `WinUAE/debug.cpp:6351`, `WinUAE/debug.cpp:8153` |
| Breakpoints | `fs lines`, `fs vpos hpos`, `fc cck` | **Beam-position / cycle breakpoints** | `WinUAE/debug.cpp:6128`, `WinUAE/debug.cpp:6109` |
| Breakpoints | `ob addr`, `ot` | Copper instruction breakpoint; copper single step | `WinUAE/debug.cpp:3463`, `WinUAE/debug.cpp:3459` |
| Breakpoints | `di R/W/RW/P [track]` | Break on floppy DMA read/write or PIO access, optional track filter | `WinUAE/debug.cpp:6869` |
| Breakpoints | `fen`, `dt`, blitter `log_blitter & 16` | Break on an Enforcer hit; "debugtest" soft asserts (blitter/keyboard/floppy) that log or break; blitter misuse breaks | `WinUAE/debug.cpp:7511`, `WinUAE/debug.cpp:6585`, `WinUAE/blitter.cpp:1065` |
| Watchpoints | `w n addr len [R/W/I] [F/C/L/N/M/B..] [V val] [PCaddr] [channels]` | 20 memwatch points; per-channel filter (CPU, copper, blitter A-D, each bitplane/sprite/audio channel, disk); freeze/force value, must-change, log-only, bus-error injection | `WinUAE/debug.cpp:5029`, `WinUAE/debug.cpp:4316` |
| Watchpoints | `wd`, `wl`, `smc` | Illegal-address logger; custom-register and DMA-pointer validator; self-modifying-code detector | `WinUAE/debug.cpp:5066`, `WinUAE/debug.cpp:5060`, `WinUAE/debug.cpp:4030` |
| Execution control | `g [addr]`, `t [n]`, `tt`, `z`, `tl`, `tr`, `ts`/`tsp`, `tse`/`tsd` | Go; step n instructions; step skipping exceptions; step over; step one source line; run until PC is in the debugmem program; run until the current function returns; enable/disable stack-frame tracking | `WinUAE/debug.cpp:7439` |
| Execution control | Cycle delta on break | "Cycles: N Chip, M CPU. (V= H= -> V= H=)" since the last resume | `WinUAE/debug.cpp:8259` |
| Execution control | `u`, `ua`, `uc` | Mute the break cause that just fired, or all, or clear | `WinUAE/debug.cpp:7841`, `WinUAE/debugmem.cpp:278` |
| History | `H[H][D] [n]` | 500-entry PC history with hpos/vpos per instruction; `HH` full register dump; `HD` shows DMA activity between instructions | `WinUAE/debug.cpp:7549`, `WinUAE/debug.cpp:7913` |
| Video, raster & beam | `v -2..-6` | **Visual DMA debugger**: per-CCK slot colors overlaid on the picture or drawn as a side diagram | `WinUAE/debug.cpp:7816`, `WinUAE/debug.cpp:1865`, `WinUAE/drawing.cpp:5571` |
| Video, raster & beam | `v vpos [hpos] [n]`, `V`, `vv`, `vl` | Text decode of recorded DMA slots: 8 rows per CCK (position, Denise/Agnus events, channel+register, value+event flags, address, CIA/ROM access, sync+RAS/CAS) | `WinUAE/debug.cpp:3127`, `WinUAE/debug.cpp:2562` |
| Video, raster & beam | `vm [type sub rgb]`, `vo` | List/recolor/toggle each DMA category; disable the DMA debugger | `WinUAE/debug.cpp:7754`, `WinUAE/debug.cpp:7747` |
| Video, raster & beam | `o`, `od`, `ot`, `ob` | Copper list disassembly with recorded execution beam positions, blitter-wait positions and jumps | `WinUAE/debug.cpp:3444`, `WinUAE/debug.cpp:3363` |
| Video, raster & beam | `sp addr`, `Ma`/`Mb`/`Ms` | Sprite decode; mute audio channels, bitplanes, sprites | `WinUAE/debug.cpp:6634`, `WinUAE/debug.cpp:7617` |
| Video, raster & beam | `gfx_overscanmode=ultra/ultra_hv/ultra_csync` | Show blanking, H/V sync or composite sync as colors in the full raster | `WinUAE/drawing.cpp:5532`, `WinUAE/cfgfile.cpp:218` |
| Profiling / heat maps | `vh`, `vh -1`, `vh <pct> <n>`, `vh <CHANNEL>` | Memory heat map (8-byte cells, per-channel), optional live 256x256 on-screen view, text stats of hot code ranges | `WinUAE/debug.cpp:7712`, `WinUAE/debug.cpp:2017`, `WinUAE/debug.cpp:1963` |
| Profiling | Trainer `C`, `Cl`, `D` | Cheat search by value and by change direction | `WinUAE/debug.cpp:3705`, `WinUAE/debug.cpp:3549` |
| OS awareness | `T`/`Tt`, `Td Tl Tr Tp Ts TR Ti TO TM Tf Te` | Tasks (running/ready/waiting with saved PC/SP), devices, libraries, resources, ports, semaphores, residents, interrupts, DOS list, memory list, filesystem.resource, expansion | `WinUAE/debug.cpp:5668`, `WinUAE/debug.cpp:5760` |
| OS awareness | SegTracker + debugmem ("uaedbg") | Tracks `LoadSeg`/`UnLoadSeg`; runs a program in a guarded memory pool with per-byte init/write/exec state | `WinUAE/filesys.asm:4651`, `WinUAE/debugmem.cpp:603` |
| Tracing & logging | `write_log` beam timestamps | Each log line is prefixed `[frame hpos -/= vpos/linear_vpos]` | `WinUAE/od-win32/writelog.cpp:578` |
| Tracing & logging | Per-subsystem log switches | `-blitterdebug`, `-inputlog`, `-vsynclog`, `-serlog`, `-scsilog`, ... and `did`/`dj` in the debugger | `WinUAE/od-win32/win32.cpp:6724`, `WinUAE/od-win32/win32.cpp:7088` |
| Tracing & logging | `debug_sprintf` port | The Amiga program writes to `$BFFF00` and the emulator prints formatted text, with a `%[CYCLES]` delta | `WinUAE/debug.cpp:9483`, `WinUAE/debug.cpp:9307` |
| Scripting & remote | `debug_parser` | Runs one debugger command line from IPC (`DBG ` prefix), an input event (`dbg `) or the uaelib config channel | `WinUAE/debug.cpp:8691`, `WinUAE/uaeipc.cpp:70`, `WinUAE/inputdevice.cpp:4320`, `WinUAE/cfgfile.cpp:8143` |
| Scripting | Lua | Byte/word read/write, logging, config read/write, custom register names as globals; `on_uae_vsync` hook | `WinUAE/luascript.cpp:190`, `WinUAE/custom.cpp:5279` |
| Scripting | Trainer `.ini` files | Pattern-matched code patches implemented on top of memwatch | `WinUAE/debug.cpp:8706`, `WinUAE/debug.cpp:9047` |
| Import / export | Memwatch in savestates | Memwatch points are saved and restored with savestates | `WinUAE/debug.cpp:4099` |
| UI conveniences | Win32 GUI | 9 pages (F1-F9), F11 step over / F12 step into, changed-line highlight, in-place editing of registers/memory/disassembly, breakpoint dots, branch-target resolution | `WinUAE/od-win32/debug_win32.cpp:75`, `WinUAE/od-win32/resources/winuae.rc:78` |
| UI conveniences | `?expr`, `?.` | Calculator: hex/bin/dec/float conversion and expression evaluation | `WinUAE/debug.cpp:1292`, `WinUAE/calc.cpp:346` |

## 2. Architecture and the cost of debugging

- **Entry point.** The CPU loop calls `debug()` when `SPCFLAG_BRK` is set (`WinUAE/newcpu.cpp:4422`). When any breakpoint is enabled, `debug()` re-arms that flag on every instruction by setting `trace_mode = TRACE_CHECKONLY` (`WinUAE/debug.cpp:8289`, `WinUAE/debug.cpp:160`). Only then is there a per-instruction cost: a linear scan of 20 `breakpoint_node`s (`WinUAE/debug.cpp:8102`). With no breakpoints the flag stays clear and the cost is zero.
- **Memory watchpoints do not cost anything per access globally.** For each watched range, `memwatch_setup()` swaps only the 64 KiB banks that contain the range for a copy of the bank whose get/put handlers are `debug_lget`/`debug_wput`/... (`WinUAE/debug.cpp:4759`, `WinUAE/debug.cpp:4823`). The copy also covers mirror aliases of the bank (`WinUAE/debug.cpp:4810`). Other banks keep their native handlers.
- **DMA-side watch** goes through explicit hooks in the chipset code: `debug_getpeekdma_chipram`/`debug_getpeekdma_value`/`debug_putpeekdma_*`. These are guarded by `memwatch_enabled` (`WinUAE/custom.cpp:11747`, `WinUAE/blitter.cpp:318`).
- **DMA recording** is guarded by `if (debug_dma)` at every recording site (`WinUAE/custom.cpp:11177`, `WinUAE/custom.cpp:5326`).
- **Stopped state.** When the debugger stops, sound is paused, input is released, and `debug_1()` runs a blocking read-eval loop on the console (`WinUAE/debug.cpp:8243`, `WinUAE/debug.cpp:7884`). If there is no JIT and no filesystem unit, a state capture is taken on resume (`WinUAE/debug.cpp:8284`).

## 3. Console command set (complete, from the dispatcher)

The help text is in `WinUAE/debug.cpp:182`. The dispatcher `debug_line()` is at `WinUAE/debug.cpp:7221` and has more commands than the help lists. Commands the help does not mention: `reset`/`reseth`/`resetk` (`WinUAE/debug.cpp:7198`), `rs`/`rss`, `seg`/`segs`, `sc` (screenshot, `WinUAE/debug.cpp:7342`), `dt` (debugtest), `tr`/`tl`/`ts*`, `fen`, `TL`, `Te`, `u`/`ua`/`uc`, `vo`/`vm`/`vv`/`vl`/`V`, `wl`, `w-`, `vh ?`.

| Cmd | Syntax | Effect |
|---|---|---|
| `g` | `g [addr]` | Resume (optionally set PC) |
| `t` | `t [n]` (1..10000), `tt` (no exceptions), `tx`, `tr`, `tl`, `ts`/`tsp`/`tse`/`tsd` | Step variants (§7) |
| `z` | | Step over (`TRACE_MATCH_PC` on the next PC) (`WinUAE/debug.cpp:7492`) |
| `f` | `f`, `f addr [Nn] [Hn]`, `f a1 a2`, `fi ...`, `fo ...`, `fl`, `fd`, `fr n`, `fp ...`, `fs ...`, `fc n`, `fa ea [s] [e]`, `fen` | Breakpoints and find (§5) |
| `il` | `il [hi32 [lo32]]` | 64-bit exception-vector break mask; with no argument it toggles all except interrupts (`WinUAE/debug.cpp:7274`) |
| `w` | see §6 | Memwatch |
| `H` | `H [n [skip]]`, `HH`, `HD`; `n > 1000` means "search the history for this PC" | History (§8) |
| `v`/`V` | see §9 | DMA debugger |
| `vh` | see §12 | Heat map |
| `o` | `o [0-3/addr] [lines]`, `od`, `ot`, `ob addr` | Copper (§10) |
| `m`, `d`, `a`, `W*`, `L`, `S`, `s`, `C`, `Cl`, `D` | | Memory and search |
| `T*` | | OS lists (§14) |
| `M` | `Ma mask`, `Mb mask [one]`, `Ms mask` | Mute audio channels, bitplanes, sprites (`WinUAE/debug.cpp:7617`); bitplane mask applied in `WinUAE/drawing.cpp:3794` |
| `sp` | `sp addr [addr2] [size]` | Sprite decode with OCS/ECS start/stop positions (`WinUAE/debug.cpp:6798`) |
| `di`/`did`/`dj` | | Disk break mode, disk log level, input log level |
| `I` | `I <event string>` | Send a custom input event (`WinUAE/debug.cpp:7260`) |
| `x`/`xx`/`mg`/`dg` | | Close; switch between console and GUI; show memory/disassembly in the GUI |
| `q` | | Quit the emulator |
| `b` | | Help says "step to previous state capture position"; the body is `#if 0` (does nothing) (`WinUAE/debug.cpp:6559`) |

## 4. Numbers and expressions

- **Literals** (`checkvaltype2`, `WinUAE/debug.cpp:941`): `$hex`, `0xhex`, `!dec`, `%bin` (the `` ` `` character is allowed as a digit-group separator, `WinUAE/debug.cpp:853`). Floats: `1.5`, `$hex.S`, `hi lo.D`, `$exp.hi lo.X` (single/double/extended). Register names with an optional `R` prefix (`Rd0`, `PC`, `VBR`, `FP0`...). A symbol name, with an optional leading `#`, resolves through `debugmem_get_symbol_value`, including `library/Function` (`WinUAE/debugmem.cpp:3604`). A bare number uses the command's default base: hex for addresses, decimal for counts.
- **Size suffix**: `.b .w .3 .l .s .d .x .p` (`WinUAE/debug.cpp:1060`). Without a suffix, the size is inferred from the magnitude (`WinUAE/debug.cpp:1142`).
- **Operators.** If an operator follows a value, the whole remaining token goes to the infix calculator (`WinUAE/debug.cpp:1152`). The calculator is shunting-yard with doubles (`WinUAE/calc.cpp:1`): `+ - * /`, `\` (mod), `| & ^ ~ !`, `<< >>`, `== !=` (numbers and strings), `< >`, ternary `?:`, `true`/`false`, and memory reads `rb( )`, `rw( )`, `rl( )` (`WinUAE/calc.cpp:641`, `WinUAE/calc.cpp:404`). Register tokens are allowed inside calc expressions (`WinUAE/calc.cpp:684`).
- **Converter** `?value` prints `$hex = %bin (byte-grouped) = unsigned = signed`. With floats or `?.` it adds `.S/.D/.X` encodings (`WinUAE/debug.cpp:1292`).
- **Limitation:** breakpoint conditions do **not** use this expression engine. `fo` compares only one register against constants (§5).

## 5. Breakpoints

**Data structure.** `struct breakpoint_node { value1, value2, mask, type(reg id), oper, opersigned, enabled, cnt, chain }` × `BREAKPOINT_TOTAL = 20` (`WinUAE/include/debug.h:124`, `WinUAE/include/debug.h:87`).

- **PC breakpoint** `f addr [N<count>] [H<bp>]`. Stored as `type = BREAKPOINT_REG_PC`, `oper == EQUAL`. Running the same `f addr` again removes it (toggle) (`WinUAE/debug.cpp:6326`). The PC is the logical 24/32-bit PC after `munge24` (`WinUAE/debug.cpp:8099`). There are no bank or physical keys; the MMU function code for debugger accesses is selected separately with `mmu <fc>`.
- **Register-condition breakpoint** `fo <num> <reg> <oper> <val> [<val2>] [<mask>] [N<count>] [H<chain>]` (`WinUAE/debug.cpp:6196`):
  - Registers: D0-D7, A0-A7, PC, USP, MSP, ISP, VBR, SR, CCR, CACR, CAAR, SFC, DFC, TC, ITT0/1, DTT0/1, BUSC, PCR, FPIAR, FPCR, FPSR, FP0-7 (`WinUAE/debug.cpp:670`).
  - Operators: `== != < > <= >=`, `-` (inclusive range) and `!-` (outside range) (`WinUAE/debug.cpp:571`).
  - A trailing `s` after the operator makes the compare signed. A mask of `0xff` or `0xffff` sign-extends at byte or word size (`WinUAE/debug.cpp:7967`).
- **Hit count** `N<n>`: the breakpoint only fires on the n-th hit and prints "Breakpoint n hit ... count=" on earlier hits (`WinUAE/debug.cpp:8053`).
- **Chaining** `H<m>`: a breakpoint that another breakpoint chains to is skipped on its own. When the head fires, the chain is walked and every link must also match (`WinUAE/debug.cpp:8106`). This gives cheap AND-combinations such as "PC == x AND D0 > 5".
- **Opcode breakpoint** `fi`: with no argument it matches RTS/RTE/RTR. With arguments it takes up to 3 opcode words, or assembler text that is assembled with `m68k_asm` (`WinUAE/debug.cpp:6237`). The extension words are compared against prefetch (`WinUAE/debug.cpp:8191`).
- **PC range / RAM**: `f a1 a2` runs until `a1 <= PC < a2`. `f` alone runs until the PC is in RAM, skipping `JMP abs` library stubs (`WinUAE/debug.cpp:8143`).
- **Exception**: `il` sets a 64-bit mask of vectors that break (`WinUAE/debug.cpp:7936`).
- **Process breakpoint**: `fp "name"` or `fp addr` (§14).
- **Beam-position and cycle breakpoints** (`cycle_breakpoint`, `WinUAE/debug.cpp:6128`):
  - `fc n` schedules an event n CCKs ahead.
  - `fs n` waits n full lines (n × maxhpos CCKs).
  - `fs vpos hpos` stores `debug_vpos`/`debug_hpos`. At each hsync, `debug_hsync()` checks for the target line and then schedules an event for the exact hpos (`WinUAE/debug.cpp:6109`, called from `WinUAE/custom.cpp:5510`). The break is `TRACE_IMMEDIATE`, so it stops mid-instruction stream at the exact CCK *(inferred from `internal_debug`, `WinUAE/debug.cpp:6093`)*.
- **Copper breakpoint** `ob addr` fires when the copper fetches an instruction at `addr..addr+3` (`WinUAE/debug.cpp:3344`).
- **Disk**: `di RW 40` breaks on DMA read/write or PIO for track 40 (`WinUAE/debug.cpp:6869`, `WinUAE/disk.cpp:4939`).
- **Soft-assert breaks**: `dt <n> [1]` toggles "debugtest" categories (Blitter, Keyboard, Floppy). Hardware-misuse sites call `debugtest()`, which logs with the PC and optionally breaks (`WinUAE/debug.cpp:6585`, `WinUAE/blitter.cpp:1132`). `fen` breaks on Enforcer hits (`WinUAE/debug.cpp:165`).
- **GUI**: breakpoints show as a filled circle beside disassembly lines, and the context menu toggles them (`WinUAE/od-win32/debug_win32.cpp:1955`, `WinUAE/od-win32/debug_win32.cpp:975`).

## 6. Memory watchpoints (memwatch)

**Node** (`WinUAE/include/debug.h:172`): `addr, size, rwi, val, val_mask, access_mask, val_size, val_enabled, mustchange, modval, modval_written, frozen, reg, pc, nobreak, reportonly, bus_error` × `MEMWATCH_TOTAL = 20`.

**Grammar** (`memwatch()`, `WinUAE/debug.cpp:5029`): `w <num> <addr> [<len>] [<channel names>...] [R][W][I] [F] [C] [M] [L] [N] [B[R][W][P]] [V<value>[.size]] [PC<addr>]`

- `w` alone lists the points. `w <num>` removes one. `w -` disables the whole system. `wl` toggles the register/DMA validator. `wd [x]` toggles the illegal-access logger (§13).
- **Access type** `rwi`: R = 1, W = 2, I = 4 (opcode fetch). The default is all three (`WinUAE/debug.cpp:5115`). CPU instruction fetches are reported as `rwi = 4` from `debug_lgeti`/`debug_wgeti` (`WinUAE/debug.cpp:4573`).
- **Size overlap**: a word or long access hits if any of its bytes falls in `[addr, addr+len)` (`WinUAE/debug.cpp:4356`).
- **Channel filter** `access_mask`: 31 bits, one per bus master (`WinUAE/include/debug.h:137`):
  - CPU instruction fetch, CPU data read, CPU data write.
  - Blitter A, B, C, and blitter D split into normal, line and fill writes.
  - Copper, disk, AUD0-3, BPL0-7, SPR0-7.
  - `NONE` = 0x80000000.
  - Named groups: `ALL`, `NONE`, `DMA`, `BLT`, `BLTD`, `AUD`, `BPL`, `SPR`, `CPU`, `CPUD`, `CPUI`, `CPUDR`, `CPUDW`, `COP`, `BLTA`..`BLTDF`, `DSK`, `AUD0`..`SPR7` (`WinUAE/debug.cpp:273`). `w ?`/`vh ?` prints them (`WinUAE/debug.cpp:334`).
  - If no channel is given, the mask is CPU only (`WinUAE/debug.cpp:5214`).
  - Copper MOVEs reach memwatch as writes to `$DFFxxx` with the COP mask, so a watch on a custom register can catch copper writes (`WinUAE/custom.cpp:4262`, `WinUAE/debug.cpp:4660`).
- **Value match** `V<val>[.b/.w/.l]`: triggers only if the accessed data equals the value. The compare is shifted across a wider access (`WinUAE/debug.cpp:4391`). `val_mask` exists and is saved in savestates, but the parser always sets it to `0xffffffff`. It cannot be set from the command line (`WinUAE/debug.cpp:5117`).
- **PC filter** `PC<addr>`: only accesses made by the instruction at that address count (`WinUAE/debug.cpp:4386`).
- **Modifiers**:
  - `C` (must change): a write that stores the same value is ignored (`WinUAE/debug.cpp:4419`).
  - `M`: remember the first written value and break only when a later write differs (`WinUAE/debug.cpp:4424`).
  - `F` (freeze): writes are dropped. With `V`, reads and writes are forced to the value, merged at byte granularity (`WinUAE/debug.cpp:4435`). The trainer uses this.
  - `L`: report only, print a message and keep running (`WinUAE/debug.cpp:4474`).
  - `N`: no break.
  - `B[R|W|P]`: inject a hardware bus error on read, write or program fetch instead of breaking (`WinUAE/debug.cpp:4364`).
- **Hit message** (`WinUAE/debug.cpp:4301`): `Memwatch 3: break at 00DFF180.W  W  00000F00 PC=00FC0F2A COP (180)`. It gives address, size, R/W/I, value, PC and channel name with the custom register number. For blitter hits it also dumps the blitter registers at blit start and now (`WinUAE/debug.cpp:4312`, `WinUAE/blitter.cpp:376`).
- **Persistence**: memwatch points are written to savestates (version 1 chunk) and re-armed on load (`WinUAE/debug.cpp:4099`, `WinUAE/debug.cpp:4176`).

## 7. Stepping and tracing

- `t [n]`: step n instructions (`TRACE_SKIP_INS`).
- `tt`: the same with `no_trace_exceptions`.
- `tx`: run until the next exception (`trace_param[0] = 0xffffffff`, `WinUAE/debug.cpp:7479`, checked in `WinUAE/debug.cpp:7947`).
- `z`: step over (run to the next PC).
- `fi`: run to the next return (`WinUAE/debug.cpp:7439`).
- Source-level stepping with debugmem:
  - `tl` steps until the source line changes (`TRACE_SKIP_LINE`, `WinUAE/debug.cpp:8210`).
  - `tr` runs until the PC is in the debugged program's range.
  - `ts`/`tsp` break when the current stack frame pops (`WinUAE/debugmem.cpp:552`).
  - `tse`/`tsd` enable or disable call-stack tracking. It is fed by `branch_stack_push`/`branch_stack_pop_rts`/`_rte` from the CPU core (`WinUAE/debugmem.cpp:450`). `rs`/`rss` list the tracked frames with registers.
- **Cycle counter**: every stop prints `Cycles: <chip CCKs> Chip, <cpu cycles> CPU. (V=a H=b -> V=c H=d)` for the time since the last resume, i.e. the beam position at resume and at stop (`WinUAE/debug.cpp:8259`, `WinUAE/debug.cpp:95`).
- There is no instruction trace-to-file log in the console debugger. The DMA record and the PC history are the trace facilities. `vl` dumps DMA records to the log file.
- The CPU core also has a `cpu_tracer` mode that records in-flight instruction memory accesses. It is used with input recording and prints `STARTCYCLES/ENDCYCLES` and a DMA log (`WinUAE/newcpu.cpp:1520`, `WinUAE/newcpu.cpp:5148`). Its purpose appears to be replaying mid-instruction state *(inferred)*.

## 8. History / backtrace

- `struct cpuhistory { regstruct regs; int fp(frame), vpos, hpos; } history[MAX_HIST = 500]` is a ring (`WinUAE/debug.cpp:176`, `WinUAE/include/debug.h:27`).
- `addhistory()` is called at the top of `debug()` and skips a PC equal to the previous entry (`WinUAE/debug.cpp:7913`). History is therefore only collected while the debugger is single-stepping per instruction, i.e. while any breakpoint or trace is armed *(inferred from the call site)*.
- `H [n [skip]]` prints each entry as `<intmask or -1 for supervisor> hhh/vvv <disassembly>` (`WinUAE/debug.cpp:7602`).
- `HH` prints a full register dump per entry.
- `n > 1000` searches the history for that PC.
- **`HD`** interleaves the DMA record between consecutive instructions with `dma_disasm()`. The CPU trace then shows exactly which DMA slots (bitplane, copper, blitter, refresh) happened between two instructions (`WinUAE/debug.cpp:7597`, `WinUAE/debug.cpp:7158`).

## 9. Visual DMA debugger (cycle-exact DMA recorder)

### 9.1 Recording data structure

- `struct dma_rec` (`WinUAE/include/debug.h:228`) is one record per CCK. Fields:
  - Position: `hpos`, `vpos[2]` (raw and linear/vsync-relative), `frame`, `tick` (absolute CCK counter), `dhpos[2]` (Denise horizontal counter for both half-cycles).
  - Bus: `reg` (custom register of the DMA fetch; CPU = `0x1000 | size | 0x100 for write`), `dat` (up to 64-bit for AGA wide fetch), `size`, `addr`, `type` (DMARECORD_*), `extra` (channel number or sub-kind).
  - Event bitmasks: `evt` (DMA_EVENT_*), `agnus_evt`/`agnus_evt_changed`, `denise_evt[2]`/`denise_evt_changed[2]`, plus `evtdata`.
  - CPU interrupt state: `intlev` (CPU interrupt mask), `ipl`, `ipl2`.
  - Conflict slot: `cf_reg/cf_dat/cf_addr`, the second requester in the same slot.
  - CIA access: `ciareg/ciamask/ciarw/ciaphase/ciavalue` (the E-clock phase).
  - ROM access: `miscaddr/miscval/miscsize`.
  - Sync: `cs/hs/vs`.
- **Storage.** A ring of `NR_DMA_REC_MAX = 1000 lines × 300 cols = 300,000` records (`WinUAE/debug.cpp:1648`), allocated lazily on the first record call (`WinUAE/debug.cpp:1714`). Two line-index tables, `dma_record_lines1/2`, point at each line's first record. They swap at every frame wrap, so `lines2` always indexes the previous complete frame for drawing (`WinUAE/debug.cpp:1657`).
- **Clocking.** `inc_cck()` in the chipset calls `record_dma_next_cycle(agnus_hpos, vpos, linear_vpos_vsync)` once per CCK. That closes the current record, stamps the sync signals, and clears the next record (`WinUAE/custom.cpp:11175`, `WinUAE/debug.cpp:1704`). Recording happens only when `debug_dma != 0`. The Agnus state carries over to the next record (`agnus_evt`), so the level is known at every slot and `_changed` marks edges.
- **Producers** (each writes into the current slot):
  - Refresh, sprite, bitplane and UHRES slots: `WinUAE/custom.cpp:11686`, `WinUAE/custom.cpp:11744`, `WinUAE/custom.cpp:11804`, `WinUAE/custom.cpp:11894`.
  - Disk and audio: `WinUAE/custom.cpp:5327`, `WinUAE/custom.cpp:5369`.
  - Copper: `WinUAE/custom.cpp:8995`, with `extra` = move/wait/skip/jump kind.
  - Blitter channels A-D with fill/line flags in `extra`: `WinUAE/blitter.cpp:306`.
  - CPU chip-bus accesses with I/D and size: `WinUAE/custom.cpp:12246`.
  - CIA E-clock phase and register access: `WinUAE/cia.cpp:2469`.
  - ROM access: `WinUAE/memory.cpp:1121`.
  - CPU instruction start with opcode (`DMA_EVENT_CPUINS`): `WinUAE/newcpu.cpp:5135`.
  - IRQ, STOP, IPL change and IPL sampling: `WinUAE/newcpu.cpp:3539`, `WinUAE/newcpu.cpp:4699`, `WinUAE/debug.cpp:2318`.
  - Denise blanking, burst, horizontal display window, BPL1DAT arm: `WinUAE/drawing.cpp:4742`, `WinUAE/drawing.cpp:4853`.
  - Agnus data-fetch start/stop and bitplane run: `WinUAE/custom.cpp:9426`, `WinUAE/custom.cpp:9459`.
- **Conflict detection.** A second read or write into an already used slot is kept in `cf_*`, and `write_log("DMA conflict R/W: v= h= OREG= NREG=")` is emitted (`WinUAE/debug.cpp:2416`, `WinUAE/debug.cpp:2433`). This works as an emulator self-check for bus arbitration.
- **Side use.** Every recorded address also feeds `debug_mark_refreshed()`, a DRAM row-refresh tracker (`WinUAE/debug.cpp:2302`). Its checker `check_refreshed()` is never called (dead code).

### 9.2 Categories and colors

`set_debug_colors()` (`WinUAE/debug.cpp:1793`). Each category has up to 8 sub-colors (`DMARECORD_SUBITEMS`) selected by `extra & 7`:

| Type | Name | Default RGB | Sub-variants |
|---|---|---|---|
| 0 | idle "-" | 222222 | |
| 1 | Refresh | 444444 | |
| 2 | CPU | A25342 code / AD98D6 data | |
| 3 | Copper | EEEE00 / AAAA22 wait / 666644 special | |
| 4 | Audio | FF0000 | |
| 5 | Blitter | 008888 A-C, 00AA88 D (write), 0088FF fill, 00FF00 line | |
| 6 | Bitplane | 0000FF | |
| 7 | Sprite | FF00FF | |
| 8 | Disk | FFFFFF | |
| 11 | Conflict | FFB840 | blinks against the normal color on alternate lines (`WinUAE/debug.cpp:1838`) |
| 12 | DIW | CFCFCF | OR-ed in on display-window edges |
| 13 | DDF | CFCF4F | OR-ed in on data-fetch (bitplane run) edges |

A 2-pixel strip at the left of each diagram line shows the highest CPU interrupt level seen on that line (`intlevc[]`: black, grey, green, yellow, blue, dark red, red, white; `WinUAE/debug.cpp:1768`, `WinUAE/debug.cpp:1942`).

**`vm`** lists every `type,sub: color * name`. `vm <type> <sub> <rrggbb>` recolors one sub-color (sub 0 = all), and `vm <type> <sub>` toggles the category's visibility (`WinUAE/debug.cpp:7754`).

### 9.3 Display modes (`v -N` sets `debug_dma = N`, `WinUAE/debug.cpp:7816`)

- **1**: record only (also what `v` with no argument does when the debugger is off).
- **2: in-picture overlay.** In the Denise pixel pipeline, each output pixel is linked to the DMA record of the CCK that produced it (`debug_dma_ptr = rd->dr`, `WinUAE/drawing.cpp:5029`). On alternating pixels of odd lines the pixel is replaced with that slot's DMA color. Window/fetch-edge markers (t > 1) always override (`WinUAE/drawing.cpp:5571`). The effect is a raster-true hatched overlay: you see which bus master owned the bus at the exact beam position of each pixel *(pattern inferred from the `dmadebugtoggle` logic)*.
- **3-6: side diagram.** `draw_frame_extras()` calls `debug_draw()` for every output line (`WinUAE/drawing.cpp:1961`), which calls `debug_draw_cycles()`. The previous frame's records are drawn right-aligned (`dx = width - xplus*maxhpos - 16`), one CCK per 1/2/3 pixels (modes ≥4 give 2 px, mode 6 gives 3 px) and one line per 1 or 2 rows (mode ≥5 doubles vertically) (`WinUAE/debug.cpp:1865`). A record from a different tick, or after `end`, is drawn black. Line optimizations are disabled while this is active (`WinUAE/drawing.cpp:1949`).
- `vo` turns the DMA debugger off and resets drawing (`WinUAE/debug.cpp:7747`).

### 9.4 Text decode (`v`, `V`, `vv`, `vl`)

- `v <vpos> [<hpos>] [<blocks>]` decodes the **previous** frame. `V` decodes the current frame (`toggle = cmd == 'v'`, `WinUAE/debug.cpp:7808`, `WinUAE/debug.cpp:3049`).
- `vv` indexes lines by linear (vsync-relative) vpos instead of the hardware vpos.
- `vl` writes the decode to the log with 16 columns and no 48-slot limit (`WinUAE/debug.cpp:3132`, `WinUAE/debug.cpp:3166`).
- `log_dma_record()` dumps automatically while input recording/playback is running (`WinUAE/debug.cpp:3294`).

Each CCK column is 15 characters wide and has 8 rows (`get_record_dma_info`, `WinUAE/debug.cpp:2562`):

1. `[hh ddd/DDD i]`: hpos (hex), Denise hcounter for both half-CCKs, IPL.
2. Denise flags for each half: `H`/`h` hblank edge/level, `V`/`v` vblank, `U`/`u` color burst, `W`/`w` HDIW, `B`/`b` BPL1DAT armed; `??????` = unknown. Uppercase means "changed this cycle" (`WinUAE/debug.cpp:2812`).
3. Agnus flags: `W` VDIW, `B`/`D` bitplane run (two stages), `E` vertical-end flags, then hardware H/V/C sync, programmable H/V/C sync, `B` hblank (`WinUAE/debug.cpp:2722`).
4. Channel and register:
   - `BPL1   110`, `SPR3 14C`, `AUD0 0AA`, `RFS0 1FE`, `DSK`.
   - `COP-M/-W/-S/-X/-1/-J/-D` = copper move/wait/skip/... (`WinUAE/debug.cpp:2614`).
   - `BLT-A..D`, `BLF-*` (fill), `BLL-*` (line).
   - `CPU-RWD` / `CPU-WLI` = CPU read/write, B/W/L size, I(nstruction)/D(ata) (`WinUAE/debug.cpp:2877`).
   - `!xxx` = the conflicting register.
5. Value (16/32/64-bit), followed by event letters (`WinUAE/debug.cpp:2929`):
   - Blitter: `D` final D, `B` start/finish, `b` IRQ.
   - CPU vs blitter: `s` CPU stole a cycle from the blitter, `S` stolen.
   - Bitplane/copper: `p` bitplane fetch update, `W` copper wake, `#` copper wake2, `c` copper wanted, `C` copper use.
   - CPU/interrupts: `I` CPU IRQ, `|` STOP, `+` STOP+IPL, `i` INTREQ, `^` IPL sample.
   - Fetch window: `0/1/2` DDFSTRT/DDFSTOP/DDFSTOP2, `M` modulo add.
   - Frame: `*L`/`*F` long line / long frame.
   - `#A/#B` CIA-A/B IRQ, `X` special.
6. Address (24-bit).
7. CIA access `RA3   00FF` (read/write, which CIA, register, value), or the E-clock phase number, or ROM access `ROMRW 00F80010`.
8. Sync `H/V/X/C`, then DRAM `RAS CAS` with `+` on a page change (`WinUAE/debug.cpp:3017`, `WinUAE/custom.cpp:1281`).

At a `DMA_EVENT_CPUINS` slot the **instruction mnemonic is written vertically** into the last character of the 8 rows, one letter per row. The CPU instruction boundaries can then be read inside the bus timeline (`WinUAE/debug.cpp:3217`).

## 10. Copper debugging

- `od` toggles copper recording. `record_copper()` stores `{addr, nextaddr, w1, w2, hpos, vpos, bhpos, bvpos}` for each executed instruction in two alternating frame sets of 100,000 records (`WinUAE/debug.cpp:3303`, `WinUAE/debug.cpp:3324`). The set is flipped at vsync (`WinUAE/custom.cpp:5284`).
- `o [0|1|2|3|addr] [lines]` disassembles from COP1LC/COP2LC/current. For every instruction found in the recorded set it prints:
  - `[vvv hhh]`, the beam position where the instruction actually executed.
  - `!` if memory now differs from what was executed.
  - `*` at the current copper PC.
  - Register names for MOVE (`BPLCON0 := 0x...`), and WAIT/SKIP decoded as `vpos & mask >= ..., hpos ...` with raw VP/VE/HP/HE/BFD fields.
  - `BLT [vvv hhh]` where a blitter-finished wait resolved.
  - `Copper jump` when the next address is not +4 (`WinUAE/debug.cpp:3363`, `WinUAE/debug.cpp:1595`).
- `ot` single-steps one copper instruction (`debug_copper |= 2`, breaks at the next record). `ob addr` is a copper PC breakpoint (`WinUAE/debug.cpp:3459`).
- In the DMA record, copper slots carry kind codes and wake/skip events (§9.4).
- `ex` shows for each custom register whether the CPU or the copper wrote it last, and from which address (`custom_storage[].pc`, low bit = copper; `WinUAE/custom.cpp:7581`, `WinUAE/debug.cpp:1545`).

## 11. Blitter debugging

- Blitter registers are snapshotted at blit start together with the PC and a CPU/copper flag. `blitter_debugdump()` prints "at start" and "now" (`WinUAE/blitter.cpp:355`, `WinUAE/blitter.cpp:376`). It is called automatically on blitter memwatch hits.
- `-blitterdebug <mask>` sets `log_blitter`: 1 = log each blit end with cycle and missed-cycle counts, 2 = warn when a blit starts while one is active, 16 = break into the debugger on misuse (`WinUAE/blitter.cpp:442`, `WinUAE/blitter.cpp:2029`, `WinUAE/od-win32/win32.cpp:7088`).
- Misuse warnings, with PC and beam position: BLTCON changes mid-blit, line/fill toggled while active, ECS DOFF bit (`WinUAE/blitter.cpp:1060`, `WinUAE/blitter.cpp:1168`).
- The DMA record shows each blitter channel slot (A/B/C/D, fill/line variants, final D) and CPU/blitter cycle stealing (`DMA_EVENT_CPUBLITTERSTOLEN`, `WinUAE/blitter.cpp:1754`).
- Memwatch channels BLTA/BLTB/BLTC/BLTDN/BLTDL/BLTDF split the blitter destination writes into normal, line and fill writes (`WinUAE/blitter.cpp:318`).

## 12. Memory heat map

- `vh [n]` allocates `heatmap[16 MiB / 8]` cells of `{mask, cpucnt, cnt, type, extra}` (`WinUAE/debug.cpp:1947`, `WinUAE/debug.cpp:2198`). It then installs three `NONE` memwatch ranges (custom registers, all chip RAM, slow RAM), so every access in those ranges reaches `memwatch_func` (`WinUAE/debug.cpp:7717`). This overwrites user memwatch slots 0-2.
- `memwatch_heatmap()`, per access (`WinUAE/debug.cpp:2204`):
  - OR-s the channel mask into the cell (the cell knows *who* ever touched it).
  - Counts CPU instruction fetches in `cpucnt`.
  - Sets `cnt = 31`, a decay counter.
  - Stores a display type (bitplane/audio/blitter/copper/disk/CPU code/CPU data).
- **On-screen** (`vh -1` → `debug_heatmap = 2`): a 256×256 grid is drawn at x = 16. One pixel is one 8-byte cell, so the grid covers the first 512 KiB of chip RAM. The color is the category color scaled by `cnt/32`, and `cnt` decrements each frame. Recently touched memory glows and fades out (`WinUAE/debug.cpp:1963`, `WinUAE/debug.cpp:1985`).
- **Stats** (`WinUAE/debug.cpp:2017`):
  - `vh [<pct> [<lines>]]` repeatedly finds the hottest CPU-fetch cell, grows the range while neighbors are ≥ pct% of its count, and prints `NNN: start - end len (len) xx.xxxxx%`, sorted by share. This is a cheap "where is the CPU spending time" profiler.
  - `vh <CHANNEL> [lines]` (for example `vh BPL`) lists contiguous address ranges that the channel has touched: "where are the bitplanes, sprites and copper lists".
  - `vh c` clears the data and `vhd` disables the heat map.

## 13. Validators and analysis tools

- **Illegal-access logger** `wd [1]`: builds a per-byte map of the 24-bit space (and 64 KiB granules above it) with flags readable/writable. Sources are RAM/ROM banks, custom registers (write-only flags from `custd`), CIA, RTC, IDE, CD32 and ROM mirrors. Any other access prints `W: addr=val PC=` / `R:` / `RO:` / `WO:` and optionally breaks (`WinUAE/debug.cpp:3830`, `WinUAE/debug.cpp:3904`). `wd addr len` whitelists a range.
- **Register/DMA validator** `wl` (`memwatch_access_validator`) logs:
  - Mirror custom-register accesses.
  - Accesses to registers that do not exist on the configured chipset.
  - Writes to read-only and reads from write-only registers.
  - Unused bits set on write.
  - Byte and unaligned accesses.
  - DMA pointers set outside chip RAM, with the PC or copper address that last set the pointer (`WinUAE/debug.cpp:4188`, `WinUAE/debug.cpp:4244`).
  - DMA fetches from invalid memory, naming the data and pointer registers (`WinUAE/debug.cpp:4267`).
- **SMC detector** `smc [1]`: a per-byte table of `{writer PC, version, hitcount}`. A fetch from a byte written earlier prints `SMC at a - b (n) from <writer PC>` and optionally breaks. Up to 8 hits per location; ROM writers and 68000 prefetch false positives (RTS/BRA.B) are filtered; a version counter gives O(1) clearing (`WinUAE/debug.cpp:4030`).
- **debugmem sandbox** (§14.2): the strongest checker. It tracks per byte "initialized / written / executed / written-after-executed / written-without-cache-flush" (`WinUAE/debugmem.cpp:603`).

## 14. OS and process awareness

### 14.1 Tasks, lists, process breakpoints

- `T`/`Tt` reads ExecBase (`$4`): the current task, then the Ready and Wait lists. For each it prints TASK/PROCESS, name, and for CLI processes the task number and command name. For non-running tasks it prints the waiting signal mask and the SP/PC taken from the saved context on the task stack (offset 70/74 depending on the Kickstart version) (`WinUAE/debug.cpp:5636`, `WinUAE/debug.cpp:5668`).
- `Tr/Td/Tl/Tp/Ts` walk ExecBase lists (resources +336, devices +350, libraries +378, ports +392, semaphores +532). Every node prints its address, type and priority. Libraries and devices also print version.revision, open count and the ID string (`WinUAE/debug.cpp:6068`).
- `TR` residents, `Ti` interrupt vectors and server chains, `TO` the DOS device list including FileSysStartupMsg/DosEnvec geometry, `TM` memory list, `Tf` filesystem.resource, `Te` expansion boards (`WinUAE/debug.cpp:5766`-`WinUAE/debug.cpp:6017`).
- `fp "name"` / `fp addr`: on every instruction outside ROM, the debugger reads `ThisTask` (`ExecBase+276`). If it is the named process (name or CLI command name), it walks that process's seglist. It breaks when the PC is inside one of that program's own segments, i.e. "stop when this program's code (not the OS) runs" (`WinUAE/debug.cpp:8153`).

### 14.2 SegTracker, debugmem ("uaedbg") and Enforcer/MungWall emulation

- `debugging_features = segtracker, fsdebug` (`WinUAE/cfgfile.cpp:261`). The boot ROM installs SegTracker-style patches of `LoadSeg`/`NewLoadSeg`/`UnLoadSeg` (`WinUAE/filesys.asm:4651`). These report every loaded seglist to the emulator (trap 202/203/212/213 → `debugmem_addsegs`/`debugmem_remsegs`, `WinUAE/filesys.cpp:9227`). The debugger can then show the segment name and number in disassembly (`WinUAE/disasm.cpp:1984`) and `seg`/`segs` list them (`WinUAE/debugmem.cpp:3833`).
- **debugmem** ("Combined Enforcer, MungWall and SegTracker emulation", `WinUAE/debugmem.cpp:3`):
  - A tool on the Amiga side hands an executable plus debug data to trap 200 (`debugmem_reloc`, `WinUAE/filesys.cpp:9205`). The program is relocated into a private high memory pool (auto-placed in 0x70000000-0xF0000000, `WinUAE/debugmem.cpp:3403`). AllocMem/AllocVec/FreeMem are redirected there (trap 204-207).
  - Each 256-byte page has per-byte state flags (`WinUAE/debugmem.cpp:212`). Accesses are checked for: invalid memory, instruction fetch from uninitialized memory, fetch from memory modified without a cache flush, fetch from memory modified after being executed, uninitialized reads, and partial-page overruns (`WinUAE/debugmem.cpp:603`). Each hit prints a page report with the DMA channel involved and breaks (`WinUAE/debugmem.cpp:343`).
  - Low-chip-RAM (NULL-pointer) accesses are reported Enforcer-style, with ExecBase and vector reads exempted (`WinUAE/debugmem.cpp:3884`).
  - An `ILLEGAL` instruction breaks (`WinUAE/debugmem.cpp:4012`), and trap handlers break with the faulting PC (`WinUAE/debugmem.cpp:1062`).
  - Break causes are numbered, and `u` can mute the cause that just fired (`WinUAE/debugmem.cpp:260`).

## 15. Symbols

- Symbol sources: hunk symbols (`HUNK_SYMBOL`), `HUNK_DEBUG` line info, and ELF with stabs (`N_SO`, `N_FUN`, `N_SLINE`, ...) or `.debug_line` (`WinUAE/debugmem.cpp:1260`, `WinUAE/debugmem.cpp:1777`, `WinUAE/debugmem.cpp:2583`). Symbols carry local/global and function flags, shown as `(L)`/`(F)` (`WinUAE/debugmem.cpp:3646`).
- Source-file lines are interleaved in disassembly (`WinUAE/disasm.cpp:2505`).
- Symbols annotate effective-address values in disassembly (`WinUAE/disasm.cpp:297`) and are accepted in every numeric argument (`WinUAE/debug.cpp:1045`).
- Library calls: `.fd` files from the plugin `debugger\fd` directory plus `amiga.lib` give LVO names. `TL` scans the ExecBase library/device/resource lists to bind each library base (`WinUAE/debugmem.cpp:2030`, `WinUAE/debugmem.cpp:1985`). `JSR/JMP x(A6)` is then shown as `exec/AllocMem`, and the target is resolved through the `JMP` stub (`WinUAE/disasm.cpp:2393`).

## 16. Logging

- `write_log()` goes to the console, the log file and/or stdout. After each newline it prefixes a wall-clock time **and the emulated beam position** `[vsync_counter hpos -|= vpos/linear_vpos]` (`-` = long frame; `WinUAE/od-win32/writelog.cpp:548`). `write_dlog()` writes without a timestamp (used by `vl`).
- Channels are process-wide integers set by command-line switches (`-log`, `-logfile`, `-blitterdebug n`, `-inputlog n`, `-vsynclog`, `-serlog[2|3]`, `-scsilog`, `-filesyslog[2]`, `-a2065log`, `-bsdlog`, `-rplog`, `-dsplog`, `-ethlog`, `-tabletlog`, ...; `WinUAE/od-win32/win32.cpp:6720`-`WinUAE/od-win32/win32.cpp:7104`). Debugger commands `did <level>` (disk) and `dj [mask]` (input) change the matching levels at runtime (`WinUAE/debug.cpp:6877`, `WinUAE/debug.cpp:7389`).
- `log_illegal_mem` is a config option for illegal memory access logging (`WinUAE/cfgfile.cpp:85`).
- **Guest printf port** `debug_sprintf` (requires `debug_mem`):
  - The Amiga program writes arguments to `$BFFF00`, then the address of a format string to `$BFFF04`; `$BFFF08` gives a va_list pointer instead (`WinUAE/debug.cpp:9483`, dispatched from `WinUAE/cia.cpp:2824`).
  - Supports `%d %x %s %b`(BCPL string) `%c %p %l`.
  - `%[CYCLES]d` prints the CCKs since the previous print, a **built-in cycle stopwatch** for code timing (`WinUAE/debug.cpp:9307`).

## 17. Scripting, automation, remote

- `debug_parser(cmd, out, outsize)` runs a debugger command line with output captured to a buffer (`WinUAE/debug.cpp:8691`). Callers:
  - The Windows named-pipe IPC with a `DBG ` prefix (`WinUAE/uaeipc.cpp:70`, `WinUAE/uaeipc.cpp:184`).
  - The `dbg ` custom input event, so a key can be bound to a debugger command (`WinUAE/inputdevice.cpp:4320`).
  - The uaelib config channel `dbg <cmd>` from Amiga-side tools (`WinUAE/cfgfile.cpp:8143`).
- Lua (`WinUAE/luascript.cpp:190`) offers `uae_log`, `uae_read_u8/u16`, `uae_peek_u16`, `uae_write_u8/u16`, `uae_read_config`/`uae_write_config`. Custom register names are exported as globals. The handlers `on_uae_vsync` and `on_uae_config_changed` are called (`WinUAE/custom.cpp:5279`, `WinUAE/cfgfile.cpp:10126`). There are no breakpoint or step APIs.
- Trainer files (`.ini`, documented in the comment at `WinUAE/debug.cpp:8706`):
  - A patch matches an instruction byte pattern with `xx` wildcards when the CPU executes it, so it works on relocated code.
  - It then computes the effective address and installs a memwatch that NOPs, freezes or sets the value, or writes once.
  - It can also replace code.
- The Amiga-side "debugger MMU" API `mmu_init` (uaelib call 84) remaps memory banks for a guest-supplied MMU table, with a logging option (`WinUAE/debug.cpp:8590`, `WinUAE/uaelib.cpp:386`).

## 18. GUI debugger (Win32)

- Pages: OUT1, OUT2, MEM1, MEM2, DASM1, DASM2, BRKPTS, MISC, CUSTOM on F1-F9. F11 steps over (`z`), F12 steps into (`t`). Alt+arrows scroll memory (`WinUAE/od-win32/debug_win32.cpp:75`, `WinUAE/od-win32/resources/winuae.rc:78`, `WinUAE/od-win32/debug_win32.cpp:1663`).
- The GUI generates the same console commands. `xx` switches between console and GUI (`WinUAE/debug.cpp:7539`).
- A changed line is redrawn in the highlight color (`UpdateListboxString` compares the old and new text, `WinUAE/od-win32/debug_win32.cpp:290`).
- Registers, memory and disassembly can be edited in place (`WinUAE/od-win32/debug_win32.cpp:1175`), and a double click toggles CCR/FPSR flags (`WinUAE/od-win32/debug_win32.cpp:1278`).
- The disassembly pane marks breakpoints and resolves branch targets for BSR/JMP/JSR and conditional branches (`WinUAE/od-win32/debug_win32.cpp:1967`).
- Context menus: set the memory view to A0-A7 or PC, enter an address, copy a line or all lines, toggle or delete breakpoints (`WinUAE/od-win32/debug_win32.cpp:1420`).
- Input history with Up/Down (`WinUAE/od-win32/debug_win32.cpp:150`).

## 19. Notable and unique ideas (ranked, ZX Spectrum angle)

1. **Per-bus-slot recorder with a raster overlay (`v -2`)** (§9.1, §9.3). One record per CCK in a frame-indexed ring, and each output pixel linked to the record of the cycle that produced it. The Spectrum equivalent: one record per T-state (or per ULA memory slot) holding ULA fetch (bitmap/attr), CPU contended access, I/O port access, floating-bus value, and INT/border state, drawn over the picture at the beam position. This makes contention and "which T-state painted this pixel" directly visible.
2. **Side-by-side cycle diagram (`v -3..-6`)** with configurable per-category colors (`vm`) and an interrupt-level strip. For the ULA this would be a 224/228-column-per-line bar chart of ULA-owned vs CPU-contended vs idle cycles, drawn from the previous frame while the current frame renders.
3. **8-row text decode of a line (`v vpos hpos`)** (§9.4). It shows level vs edge flags (lowercase vs uppercase) for sync, blanking and display window, bus register, value, address and DRAM row, plus **the CPU mnemonic written vertically at instruction-start slots**. For the Spectrum: rows for ULA phase, contention delay applied, port, value, and IM2 vector sampling. `vl` dumps the same to the log, and `V` vs `v` picks the current or previous frame.
4. **`HD` history interleaved with DMA** (§8). Every history entry keeps `(frame, vpos, hpos)`, and the bus activity between two instructions is printed inline. On the Spectrum: show the T-states lost to contention and the ULA fetches that happened during each instruction.
5. **Beam-position breakpoints `fs vpos hpos` / `fc n` and the cycle delta printed on every stop** (§5, §7). Cheap to implement as a scheduled event, and useful for multicolor and border effects. "Cycles since resume, (V,H) → (V,H)" should be on every stop line.
6. **Channel-filtered memwatch** (§6). Watch points filtered by bus master (CPU fetch/read/write vs each DMA channel), with value match, must-change, changed-from-first-write, PC filter, log-only and freeze/force. For the Spectrum, the masters are CPU vs ULA vs (Next/ZX-Evo DMA, Beta Disk), and "log-only" is a trace point.
7. **Heat map with channel masks and decay** (§12). A live glowing grid of memory touched in the last frames, colored by who touched it, plus text stats of the hottest code ranges and "which ranges were ever read by the video fetch". On the Spectrum, 48 KiB at 4-byte cells fits a 128×96 map; a ULA-only view shows the actual screen banks in use (shadow screen on 128K).
8. **Last-writer tracking per hardware register (`ex`)** (§10). Value, PC, and whether the CPU or the copper wrote it. On the Spectrum: last write to `#FE`/`#7FFD`/`#1FFD`/AY per port, with PC and T-state.
9. **Beam timestamp on every log line** `[frame hpos/vpos]` (§16). Trivial to add and makes all logs correlate with the raster.
10. **Guest printf port with a cycle stopwatch** (`$BFFF00`, `%[CYCLES]`) (§16). A magic I/O port that test ROMs and demos can use to print and time code sections without a debugger stop.
11. **Validators** (§13): writes to non-existent or read-only registers, unused bits set, DMA pointers out of range (with the last setter), and SMC with the writer's PC. Spectrum analogues: port writes with undecoded bits, paging-port misuse, and code executed after being overwritten (tape loaders, decrunchers).
12. **Breakpoint chaining (`H`) and hit counts (`N`)** (§5). A minimal way to build AND-conditions and "nth occurrence" without a full expression engine.
13. **OS-aware breaks (`fp`) and segment tracking** (§14.1, §14.2). A TR-DOS or +3DOS analog could break when code runs outside ROM for a named loaded file.
14. **Emulator self-check from the recorder**: double-booked bus slots are logged as "DMA conflict" (§9.1). Useful as an assertion for ULA/CPU arbitration correctness.

## 20. Gaps and caveats

- `b` (step back to a state capture) is documented but compiled out (`#if 0`, `WinUAE/debug.cpp:6561`). There is no reverse execution.
- Breakpoint conditions cannot use the expression engine. `fo` compares one register only, and there are no memory-content conditions except through memwatch value match. There are 20 breakpoints and 20 memwatch points.
- Memwatch `val_mask` is stored and compared but cannot be set from the parser (always `0xffffffff`, `WinUAE/debug.cpp:5117`).
- Likely bug: sprite and bitplane DMA memwatch masks are built as `MW_MASK_SPR_0 + num` and `MW_MASK_BPL_0 + num` (addition, not shift). For channel 1 and up this produces wrong bits, e.g. BPL_0|CPU_I. Audio uses `<< nr` correctly (`WinUAE/custom.cpp:11747`, `WinUAE/custom.cpp:11810`, `WinUAE/custom.cpp:5372`).
- `vh` hijacks memwatch slots 0-2 without warning (`WinUAE/debug.cpp:7727`). The on-screen heat map covers only the first 512 KiB.
- `check_refreshed()` (the DRAM refresh checker) is never called (`WinUAE/debug.cpp:2274`).
- Help text mismatches:
  - The help says `TS` for residents; the code uses `TR`, and `TS` is not handled (`WinUAE/debug.cpp:5948`).
  - `v` accepts `-6` although the help says -2 to -5.
  - Several commands are missing from the help (§3).
- The DMA record is valid only in cycle-exact mode (help text, `WinUAE/debug.cpp:249`). The ring holds about one PAL frame at 300,000 records, so older frames are lost.
- `N` (nobreak) memwatch sets `memwatch_triggered` but never clears it until the next stop. The next unrelated stop then prints a stale memwatch hit *(inferred from `WinUAE/debug.cpp:4473`, `WinUAE/debug.cpp:8234`)*.
- History is only recorded while the per-instruction debug hook is armed. Free-running code has no backtrace.
- Lua has no debugger hooks (no breakpoints, no per-instruction callbacks). There is no GDB/remote debug protocol in this tree; IPC gives text command in / text out only.
- The GUI is Win32-only. Other ports get the console monitor.
