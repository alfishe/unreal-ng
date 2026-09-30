# FCEUX debugger — capability survey

**Source:** https://github.com/TASEmulators/fceux · local checkout commit `d339f1fa` (2025-11-11) · C++ core, two front ends: Qt 5/6 (Linux, macOS, Windows) and native Win32
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** The NES/Famicom 6502 debugger core (`src/debug.cpp`, `src/conddebug.cpp`, `src/debugsymboltable.cpp`) is shared. Each front end supplies its own GUI tool windows: debugger, trace logger, code/data logger, PPU (CHR) viewer, sprite viewer (Qt only), name table viewer, hex editor, RAM watch/search, TAS Editor. Lua 5.1 scripting has memory hooks and a small `debugger.*` library. There is no console monitor and no remote debug protocol (no GDB stub, no WebSocket). Rewind comes from a savestate ring (Qt state recorder), the TAS Editor greenzone, and a trace-based one-instruction "Step Back" (Qt).

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Register panel | PC, A, X, Y, P as editable fields; N V U B D I Z C checkboxes; stack page view `$0100`; CPU cycle and instruction counters with deltas since the last break | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:1123-1268`; `fceux/src/debug.cpp:612-648` |
| CPU & registers | Change PC | "Change PC" (Ctrl+Shift+G) sets PC to the selected line | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:441-442` |
| CPU & registers | PPU state panel | PPUCTRL, PPUMASK, PPUSTAT, PPUADDR (v), OAMADDR, scanline, pixel, X/Y scroll; a pop-up decodes CTRL/MASK/STATUS bits (BG/sprite enable, left-8px clip, 8x16, NMI, grayscale, emphasis, vblank, sprite 0 hit, overflow) | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:1304-1394`, `:4282-4330`, `:7466-7503` |
| Disassembly | Disassembly view | Live 6502 disassembly around PC; optional byte codes, ROM file offsets, "trace data" (`= #$xx` operand values), CDL `c`/`d`/`cd` prefix column; configurable PC line placement (top, mid, center, custom offset) | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:580-661`, `:3872-3914` |
| Disassembly | Symbolic disassembly | Operands replaced or annotated with `.nl`/ld65 names; `@ $addr` effective address and `= #$val` shown for indexed/indirect modes | `fceux/src/drivers/Qt/SymbolicDebug.cpp:43-76`, `:78-280`; `fceux/src/drivers/Qt/SymbolicDebug.h:83-89` |
| Disassembly | Inline assembler (Win32 only) | Left-click on the address gutter opens "Inline Assembler": one line at a time, patches applied to PRG in memory, undo, "Save" writes the `.nes` file | `fceux/src/drivers/win/debugger.cpp:1340-1425`, `:2403`; `fceux/src/asm.cpp:15-258` |
| Disassembly | ROM patcher (Win32 only) | Poke bytes at a `.nes` file offset | `fceux/src/drivers/win/debugger.cpp:1437-1470`, `:2669` |
| Memory views | Hex editor | Four address spaces: CPU bus, PPU bus, OAM, ROM file; edit, undo (ROM edits), copy/paste, find, TBL character maps, fonts, refresh 5-60 Hz | `fceux/src/drivers/Qt/HexEditor.cpp:1141-1399`; `fceux/src/drivers/Qt/HexEditor.h:147-151` |
| Memory views | Activity highlight | A changed byte gets a highlight that fades over 16 color steps, one step per refresh | `fceux/src/drivers/Qt/HexEditor.cpp:3590-3605`, `:4081-4087`; `fceux/src/drivers/Qt/HexEditor.h:153` |
| Memory views | CDL coloring in hex editor | ROM bytes colored by CDL state: code (yellow), data (blue), PCM data (cyan), both (green); CHR rendered/read colors | `fceux/src/drivers/Qt/HexEditor.cpp:3659-3700` |
| Memory views | Side-effect-free reads | Debugger reads go through `GetMem` with `fceuindbg=1`, so PPU register reads do not clear latches; `$2000-$401F` return cached register copies | `fceux/src/debug.cpp:337-372`; `fceux/src/ppu.cpp:327`, `:588` |
| Navigation & bookmarks | Go to address / PC, history | Ctrl+A go to address, Ctrl+G go to PC, Ctrl+Left/Right back and forward through navigation history | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:422-462`, `:3168-3176` |
| Navigation & bookmarks | Bookmarks | Named address bookmarks (Add/Delete/Name, hotkey M in the asm view); saved in `.fdb` | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:1511-1519`, `:6398-6405`, `:4787-4794` |
| Symbols & labels | `.nl` name files | Per-bank files `<rom>.ram.nl` (below `$8000`) and `<rom>.<bank-hex>.nl`; `$ADDR#Name#Comment`, `$ADDR/COUNT#Name#` arrays, `\` continuation lines | `fceux/src/debugsymboltable.cpp:401-462`, `:464-708` |
| Symbols & labels | ld65 `.dbg` import | "Import ld65 dbg file": cc65 labels with scopes, banked by segment ROM offset | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:403`; `fceux/src/debugsymboltable.cpp:935-1001`; `fceux/src/ld65dbg.cpp` |
| Symbols & labels | Hardware register names | Built-in page `-2`: `PPU_CTRL`..`PPU_DATA`, APU and joypad registers; on/off toggle | `fceux/src/debugsymboltable.cpp:710-750`; `fceux/src/drivers/Qt/ConsoleDebugger.cpp:909` |
| Symbols & labels | Symbol editor | Add or edit a name/comment at an address, including arrays with size, init byte, "overwrite names in array body", comment on head only | `fceux/src/drivers/Qt/SymbolicDebug.cpp:527-617`; `fceux/src/drivers/win/debuggersp.cpp:1123-1310` |
| Breakpoints | CPU R/W/X breakpoints | Single address or range; any combination of Read, Write, Execute; Enable; optional condition and name; up to 64 | `fceux/src/debug.h:6-19`, `:52-64`; `fceux/src/drivers/Qt/ConsoleDebugger.cpp:1874-1952` |
| Breakpoints | PPU memory breakpoints | R/W on a PPU address/range, matched when the CPU accesses `$2007` and the current VRAM address (v) falls in range | `fceux/src/debug.cpp:784-799` |
| Breakpoints | OAM (sprite) breakpoints | R/W on OAM address/range via `$2004` + OAMADDR; any `$4014` write (OAM DMA) triggers every OAM write breakpoint | `fceux/src/debug.cpp:800-818` |
| Breakpoints | ROM (file offset) breakpoints | Execute breakpoint keyed on the `.nes` file offset of PC, independent of which CPU address the bank is mapped at | `fceux/src/debug.cpp:832-842`, `:310-316` |
| Breakpoints | Forbid breakpoints | A range (with its own condition) where every breakpoint hit is suppressed | `fceux/src/debug.cpp:650-676` |
| Breakpoints | Stack-aware R/W | PHA/PHP/PLA/PLP/JSR/RTS/RTI stack traffic and unannounced S changes (interrupt pushes) are checked against R/W breakpoints on `$01xx` | `fceux/src/debug.cpp:762-776`, `:855-924` |
| Breakpoints | Symbolic / vector address entry | Address field accepts a symbol name, `NMI`/`VBL`, `RST`, `IRQ`/`BRK` (read from vectors); NSF `LOAD`/`INIT`/`PLAY`; FDS `NMI1-3` | `fceux/src/debug.cpp:30-112` |
| Breakpoints | Special break conditions | Bad opcode, unlogged code (first execution of a byte not yet in CDL), unlogged data, cycle count exceeded, instruction count exceeded, Lua request | `fceux/src/debug.h:21-27`; `fceux/src/debug.cpp:701-714`, `:592-599`; `fceux/src/drivers/Qt/ConsoleDebugger.cpp:763-819` |
| Breakpoints | Counter breaks, one-shot or continuous | Cycle/instruction threshold, absolute or relative; continuous mode re-arms at `now + delta` after each hit | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:7850-8230`, `:4585-4620` |
| Breakpoints | Context-menu creation | Asm view: B = add/edit breakpoint, S = symbol, M = bookmark, H = open hex editor, "Run To Cursor"; hex editor: add R/W/X breakpoint for the address | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:6352-6410`; `fceux/src/drivers/Qt/HexEditor.cpp:3148-3190` |
| Conditions & expressions | Condition language | Registers, flags, `#hex` numbers, `$addr` memory reads, `$[expr]` indirection, `K` (PC bank), `T` (data bank), `R` (value read), `W` (value about to be written); `+ - * /`, comparisons, `&& \|\|` | `fceux/src/conddebug.cpp:27-42`, `:216-512`; `fceux/src/debug.cpp:384-490` |
| Watchpoints & watches | RAM Watch / RAM Search | Separate TAS-style tools: watch list, value search with comparisons | `fceux/src/drivers/Qt/RamWatch.cpp`; `fceux/src/drivers/Qt/RamSearch.cpp`; `fceux/src/drivers/win/ramwatch.cpp` |
| Watchpoints & watches | Freeze | Freeze/unfreeze a RAM byte from the hex editor (cheat engine) | `fceux/src/drivers/Qt/HexEditor.cpp:3128-3144`; `fceux/src/drivers/win/memview.cpp:112-113` |
| Execution control | Run / Pause / Step Into / Over / Out | F5, F6, F11, F10 (JSR only: temporary breakpoint at PC+3 in slot 64), Shift+F11 (JSR/RTS nesting counter) | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:673-712`, `:3005-3092`; `fceux/src/debug.cpp:716-756` |
| Execution control | Run Line / Run 128 Lines | F7/F8 run 341/3 or 128*341/3 CPU cycles, i.e. one or 128 scanlines of time | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:739-749`, `:3113-3145`; `fceux/src/debug.cpp:737-748` |
| Execution control | Run to selected line | F1 / "Run To Cursor" | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:730-731`, `:3108-3111` |
| History / rewind / time travel | Step Back (Qt) | F9 pops the last trace-logger record, restores A/X/Y/S/P/PC and undoes simple stores | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:3094-3106`; `fceux/src/drivers/Qt/TraceLogger.cpp:2581-2632` |
| History / rewind / time travel | State recorder (Qt) | Ring of in-memory savestates (default 15 min history, one every 60 frames, zlib level configurable); load previous/next with optional pause | `fceux/src/state.cpp:1212-1470`; `fceux/src/state.h:84-114`; `fceux/src/drivers/Qt/StateRecorderConf.cpp` |
| History / rewind / time travel | Autosave ring, undo/redo load state | Load last autosave; backup state before load enables undo/redo of a load | `fceux/src/fceu.cpp:1356-1375`; `fceux/src/state.cpp:1078-1205` |
| History / rewind / time travel | Movies and TAS Editor | `.fm2` input movies, TAS Editor `.fm3` projects with greenzone (per-frame savestates, thinned with age), branches, markers, lag log | `fceux/src/movie.cpp:436-466`; `fceux/src/drivers/Qt/TasEditor/greenzone.cpp:130-164` |
| Tracing & logging | Trace logger | Per-instruction log to window and/or file; options: registers, flags, frame/cycle/instruction counts, bank, symbolic, stack-depth indentation, "only new code/data" (CDL-driven, skipped-lines counter), breakpoint hits and messages | `fceux/src/drivers/Qt/TraceLogger.cpp:74-110`, `:308-380`, `:946-1059`, `:1227-1408` |
| Tracing & logging | Auto-start trace for Step Back | Option to start the trace logger with the debugger so Step Back has history | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:214-226`, `:858` |
| Video, raster & beam | Scanline/pixel readout | Debugger shows current scanline (including vblank / pre-render -1) and pixel, estimated from CPU timestamp; exact from `newppu` when enabled | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:4291-4327`; `fceux/src/debug.cpp:950-958` |
| Video, raster & beam | Scanline-triggered viewer refresh | PPU viewer and name table viewer snapshot VRAM when the PPU reaches a user-chosen scanline ("Display on Scanline"), plus a refresh-skip divisor | `fceux/src/ppu.cpp:1390`, `:1874-1875`; `fceux/src/drivers/Qt/ppuViewer.cpp:1612-1650`, `:284-294`; `fceux/src/drivers/Qt/NameTableViewer.cpp:1755-1785`, `:549` |
| Video, raster & beam | PPU (CHR) viewer | Two pattern tables, 8x16 mode, tile hover/click info, palette rows (BG/sprite), "mask unused graphics" using CHR CDL (with invert), grid; tile editor | `fceux/src/drivers/Qt/ppuViewer.cpp:225-300`, `:945-976`, `:2054-2522` |
| Video, raster & beam | Sprite (OAM) viewer (Qt) | 64 sprites from OAM with flip/priority/palette/position, tile address, preview; "CPU page" data source present but disabled (TODO) | `fceux/src/drivers/Qt/ppuViewer.cpp:2976-3114`, `:2996-2997` |
| Video, raster & beam | Name table viewer | Four name tables with mirroring, scroll lines (current X/Y scroll), tile grid, attribute grid, attributes view, ignore palette, zoom, tile info (PPU addr, tile index/addr, attribute addr/value, palette addr) | `fceux/src/drivers/Qt/NameTableViewer.cpp:186-461`, `:535` |
| Video, raster & beam | Palette | Palette rows in PPU viewer; palette editor with ACT export | `fceux/src/drivers/Qt/ppuViewer.cpp:299`, `:1770-1777`; `fceux/src/drivers/Qt/PaletteEditor.cpp` |
| Sound & device views | APU/IO in memory reads | `$4000-$4017` debugger reads return internal latches (PSG, DMC, OAM DMA, `$4016`, frame IRQ mode) | `fceux/src/debug.cpp:350-362` |
| Profiling, heat maps, coverage, code/data logging | Code/Data Logger | PRG byte flags (code, data, bank bits, indirect code/data, PCM, run from `$6000`); CHR flags (rendered, read via `$2007`); counters and percentages; `.cdl` auto-load/save; "Save Stripped Data" / unused-data ROM export | `fceux/src/debug.cpp:495-600`; `fceux/src/ppu.cpp:492-525`, `:734-745`; `fceux/src/sound.cpp:182-189`; `fceux/src/drivers/Qt/CodeDataLogger.cpp:151-267`, `:543-724`, `:850-1000` |
| Profiling, heat maps, coverage, code/data logging | Host profiler | Optional build flag `__FCEU_PROFILER_ENABLE__` for timing emulator C++ functions (host side, not guest code) | `fceux/src/profiler.h:23-32`; `fceux/src/CMakeLists.txt:37-39` |
| Scripting, automation & remote debug protocols | Lua 5.1 | Libraries `emu`, `rom`, `memory`, `ppu`, `joypad`, `zapper`, `input`, `savestate`, `movie`, `gui`, `sound`, `debugger`, `cdlog`, `taseditor`, `bit` | `fceux/src/lua-engine.cpp:6218-6471`, `:6632-6647` |
| Scripting, automation & remote debug protocols | Lua memory hooks | `memory.registerread/write/exec(addr, [size], fn)`; callback `(address, size, value)` | `fceux/src/lua-engine.cpp:2407-2477`, `:2260-2326`; `fceux/src/x6502.cpp:160-223`, `:641-644` |
| Scripting, automation & remote debug protocols | Netplay desync check (Qt) | Per-frame CRC32 of every executed opcode stream plus a RAM checksum, compared across peers | `fceux/src/drivers/Qt/NetPlay.cpp:3235-3251`; `fceux/src/drivers/Qt/TraceLogger.cpp:1229-1232` |
| Import / export & persistence | Debugger file | Qt `.fdb` text (`BreakPoint: startAddr=... flags=ECRWXF condition="..." desc="..."`, `Bookmark: ...`); Win32 `.deb` binary with breakpoints and hex bookmarks; optional auto-load on ROM load | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:4683-4800`, `:4869`, `:846`; `fceux/src/drivers/win/pref.cpp:175-230` |
| Import / export & persistence | `.nl` save | Symbol edits written back to the per-bank `.nl` files (register page `-2` never saved) | `fceux/src/debugsymboltable.cpp:228-300`; `fceux/src/drivers/win/debuggersp.cpp:1341` |
| UI conveniences | Layouts and fonts | Compact / Compact Split / Wide / Wide Quad layouts with tabbed CPU/PPU/Breakpoints/Bookmarks panes; font and syntax colors per element | `fceux/src/drivers/Qt/ConsoleDebugger.cpp:480-531`, `:553`, `:3178-3200` |
| UI conveniences | Win32 gutter mouse actions | Left-click = inline assembler, middle-click = Game Genie code, right-click = hex editor | `fceux/src/drivers/win/debugger.cpp:2384-2420` |

## 2. Debugger core and breakpoint model

- **Storage.** `watchpointinfo watchpoint[65]`: slots 0..63 are user breakpoints and slot 64 is kept for step over (`fceux/src/debug.cpp:604`; `fceux/src/debug.h:52-64`). Each entry holds `address`, `endaddress` (0 = single address), `flags` (16 bit), a pre-parsed `Condition*` AST, the condition text and a description. `numWPs` is the number of used slots, which are packed from index 0 (`fceux/src/debug.cpp:609`).
- **Flag bits** (`fceux/src/debug.h:6-19`): `WP_E` 0x01 enable, `WP_W` 0x02, `WP_R` 0x04, `WP_X` 0x08, `WP_F` 0x10 forbid. Address space: `BT_C` 0x00 CPU, `BT_P` 0x20 PPU, `BT_S` 0x40 OAM, `BT_R` 0x80 ROM. `NewBreak` clears `WP_X` for PPU and OAM breakpoints (`fceux/src/debug.cpp:222-260`).
- **When the check runs.** `X6502_Run` calls `DebugCycle()` before each opcode fetch, so a breakpoint stops the CPU *before* the instruction runs (`fceux/src/x6502.cpp:626-631`). The call is wrapped in `DEBUG(...)`, which is enabled by `-DFCEUDEF_DEBUGGER` in both CMake and MSVC builds (`fceux/src/types.h:35-39`; `fceux/src/CMakeLists.txt:132`, `:175`).
- **Access prediction, not bus snooping.** `DebugCycle` reads the opcode bytes and computes the effective address `A` from an addressing-mode table (`optype`). The access kind comes from the `opbrktype[256]` table (R, W or RW per opcode) plus X for every opcode (`fceux/src/debug.cpp:944-1000`; `fceux/src/debug.h:29-48`). CPU R/W breakpoints therefore match the *predicted* operand address of the instruction. Dummy reads, RMW double writes, DMA and vector fetches are not seen. Stack traffic has its own heuristics (below).
- **Matching** (`fceux/src/debug.cpp:778-928`):
  - CPU: `(flags & brk_type)`, then range or equality against `A` for R/W and against `PC` for X.
  - PPU: only when the instruction touches `$2000-$3FFF` with `(A&7)==7` (the `$2007` port); compared against `FCEUPPU_PeekAddress()`.
  - OAM: `(A&7)==4` compared against `PPU[3]` (OAMADDR); any write to `$4014` fires every OAM breakpoint that has W set, whatever its range.
  - ROM: execute only; compared with `GetNesFileAddress(PC)`, the `.nes` file offset including the 16-byte header (`fceux/src/debug.cpp:310-316`, `:832-842`). ROM read breakpoints are commented out (`:838-841`).
  - Stack: for PHA/PHP/PLA/PLP/JSR/RTS/RTI the touched `$01xx` bytes are checked against R/W breakpoints. For other opcodes, a change in S since the previous instruction is treated as an unannounced push (write) or pull (read), which catches NMI/IRQ pushes (`:762-776`, `:855-924`).
- **Conditions and forbid.** When the address matches, `BREAKHIT(i)` calls `CondForbidTest`. That evaluates the breakpoint's condition and then scans every enabled forbid (`WP_F`) entry. If PC lies in a forbid range whose own condition is true, the hit is suppressed (`fceux/src/debug.cpp:650-676`, `:778`). The first accepted hit ends the scan (`goto STOPCHECKING`).
- **Special break types** (`fceux/src/debug.h:21-27`): step (-1), bad opcode (-2, `opsize==0`), cycles exceeded (-3), instructions exceeded (-4), Lua `debugger.hitbreakpoint()` (-5, sets `break_asap`), unlogged code (-6), unlogged data (-7). The last two are raised from the CDL logger on the first logging of a byte (`fceux/src/debug.cpp:592-599`).
- **Hit handling.** `BreakHit` pauses emulation and calls `FCEUD_DebugBreakpoint` (`fceux/src/debug.cpp:678-685`). In Qt, the emulation thread then *blocks inside the CPU loop*: it drops the emulator mutex and spins with `msleep(16)` until resumed, and frame advance also releases it (`fceux/src/drivers/Qt/ConsoleDebugger.cpp:4574-4654`). Win32 runs a nested message loop `win_debuggerLoop()` (`fceux/src/drivers/win/debugger.cpp:907-972`). Either way, the break is exact to the instruction, in the middle of a frame.
- **Step engine** (`fceux/src/debug.cpp:716-756`): `step` breaks on the next instruction. `stepout` counts JSR/RTS nesting (`jsrcount`) and turns into `step` on the matching RTS. `runline` breaks once `timestampbase+timestamp >= runline_end_time`. Step over puts a hidden X breakpoint at `PC+3` in slot 64, but only when the opcode is JSR, and otherwise just steps (`fceux/src/drivers/Qt/ConsoleDebugger.cpp:3060-3092`).

## 3. Condition expression grammar

Recursive-descent parser (`fceux/src/conddebug.cpp:27-42`, `:393-527`). The condition is compiled once into a `Condition` tree when the breakpoint is set (`fceux/src/debug.cpp:147-213`) and evaluated by `evaluate()` (`fceux/src/debug.cpp:411-485`).

```
P         -> Connect
Connect   -> Compare {('||' | '&&') Compare}          // same precedence, left-assoc
Compare   -> Sum {('==' | '!=' | '<=' | '>=' | '<' | '>') Sum}
Sum       -> Product {('+' | '-') Product}
Product   -> Primitive {('*' | '/') Primitive}
Primitive -> Number | Address | Register | Flag | 'K' | 'T' | 'R' | 'W' | '(' Connect ')'
Number    -> '#' hex            (max $FFFF)
Address   -> '$' hex | '$' '[' Connect ']'
```

| Operand | Meaning | Source |
|---|---|---|
| `A` `X` `Y` `S` | registers | `fceux/src/debug.cpp:116-136` |
| `P` | **program counter** (not the status register) | `fceux/src/debug.cpp:131` |
| `N V U B D I Z C` | single flag, 0/1 | `fceux/src/debug.cpp:123-130` |
| `#1F` | hex literal | `fceux/src/conddebug.cpp:320-340` |
| `$0300` | byte at CPU address (via side-effect-free `GetMem`) | `fceux/src/debug.cpp:433` |
| `$[expr]` | byte at computed address (pointer dereference) | `fceux/src/conddebug.cpp:364-379` |
| `K` | bank of PC (`getBank(PC)`) | `fceux/src/debug.cpp:434` |
| `T` | bank of the instruction's effective address | `fceux/src/debug.cpp:435` |
| `R` | byte at the effective address (value that will be read) | `fceux/src/debug.cpp:436` |
| `W` | value the instruction will write, predicted from the opcode class (STA/STX/STY/PHA/PHP, ASL/LSR/ROL/ROR, INC/DEC, SAX/AHX/SHX/SHY/TAS) | `fceux/src/debug.cpp:384-408`, `:437` |

Semantics: division by zero yields 0 (`fceux/src/debug.cpp:476`). `&&`/`||` evaluate both sides (no short-circuit). There are no unary operators, no bitwise AND/OR/shift and no 16-bit memory read. The constant-address branch only accepts `0-9A-F` right after `$`, so a lowercase `$c000` is rejected (`fceux/src/conddebug.cpp:343`). Example: `A==#80 && $[#10]==#0 && K==#3`.

## 4. Symbols, `.nl` files and bank handling

- **Bank definition.** The "bank" is a fixed 16 KiB chunk of the PRG image (`debuggerPageSize = 14`, never changed), not the mapper's real bank size. `getBank(addr) = (fileOffset - 16) >> 14`, and NSF uses 4 KiB (`fceux/src/debug.cpp:18`, `:297-308`). The same bank number keys `.nl` pages, the `K`/`T` condition operands, and the trace "bank" column.
- **Files.** `<rom>.ram.nl` holds addresses below `$8000` (page `-1`). `<rom>.<BANK>.nl` (bank in hex) holds ROM symbols. Archive `|` separators become `.` (`fceux/src/debugsymboltable.cpp:401-462`). All banks up to `romSize/16K` are loaded when the game loads (`:755-787`).
- **Line syntax** (`fceux/src/debugsymboltable.cpp:492-703`):
  - `$C000#Reset#Entry point` defines name and comment.
  - `$0300/10#Buffer#` creates an array: 0x10 symbols `Buffer[0]..Buffer[15]` (the count is hex).
  - A line starting with `\` continues the previous comment.
  - `\` escapes `\n \r \t` inside names.
  - Duplicate names are allowed by default (`dbgSymAllowDuplicateNames`, `:24`).
- **Lookup.** `debugSymbolTable_t` is a `std::map<page, debugSymbolPage_t>` with offset and name maps per page (`fceux/src/debugsymboltable.h:87-166`). The disassembler looks in the page of the current bank for addresses `>= $8000`, then in page `-1`, then in the register page `-2` (`fceux/src/drivers/Qt/SymbolicDebug.cpp:43-76`). Breakpoint address fields resolve names through `getSymbolAtAnyBank`, which matches the first bank found (`fceux/src/debug.cpp:70-79`).
- **Win32** keeps a second, older linked-list parser and cache (`pageNames[]`). It reloads a page's `.nl` whenever the bank mapped at that CPU window changes, and mirrors edits into the core table (`fceux/src/drivers/win/debuggersp.cpp:684-780`, `:1192`, `:1298`).
- **ld65**: cc65 `.dbg` labels are imported. Scope prefixes are joined into the name, and the bank comes from the segment's ROM offset (`fceux/src/debugsymboltable.cpp:935-1001`).

## 5. Trace logger

- **Format.** Qt builds the line in `traceRecord_t::convToText` (`fceux/src/drivers/Qt/TraceLogger.cpp:946-1059`). Field order: `(N lines skipped)`, `f<frame>`, `c<cycles>`, `i<instructions>`, registers `A:xx X:xx Y:xx S:xx`, flags as case-coded letters `nvUbdIzc`, stack-depth indentation (`(0xFF-S)&31` spaces), optional `$BB:` bank prefix, `$PPPP:`, raw bytes padded to 3, disassembly, and for RTS ` (from $XXXX)` (the subroutine being left). Registers and flags go either to the left of the disassembly or after it.
- **Sample line** (default options: registers, flags, to the left, stack tabbing; reconstructed from the code):
  `A:00 X:00 Y:00 S:FD nvUbdIzc   $C000: 78        SEI`
  With "Log Bank Number" and "Symbolic Trace" on:
  `A:05 X:00 Y:00 S:FB nvUbdIzc     $07:C123: 8D 00 03  STA $0300 PlayerX = #$05`
- **Options** (bitmask, `fceux/src/drivers/Qt/TraceLogger.cpp:74-86`): registers, processor status, only new instructions, only new data (both need CDL on; skipped lines are counted), to the left, frames count, messages, breakpoint hits, symbolic, code tabbing, cycles count, instructions count, bank number. Window buffer: 1,000 to 3,000,000 lines, default 1,000,000 (`:169`, `:239-246`).
- **Performance design (Qt).** The emulation thread stores a compact binary `traceRecord_t` (registers, opcode bytes, bank, counters, predicted write address and old value) in a ring. Text is produced later, on display or by a `QThread` disk writer (`TraceLogDiskThread_t`, highest priority) that drains a second 3M-entry ring. If the writer falls behind, the producer waits in 1 ms sleeps, then warns about overrun once (`fceux/src/drivers/Qt/TraceLogger.cpp:1106-1153`, `:2414-2540`; `fceux/src/drivers/Qt/TraceLogger.h:138`). On Windows the writer uses `TraceFileWriter`: unbuffered, write-through, overlapped I/O in 4 KiB blocks (`fceux/src/drivers/win/TraceFileWriter.h:7-46`). The instruction callback is a null-checked linked list, so it costs almost nothing when tracing is off (`fceux/src/debug.cpp:1008-1018`).
- **Win32 tracer** formats text synchronously per instruction. It appends a `-----` rule after RTS and has the same option set (`fceux/src/drivers/win/tracer.cpp:743-900`, `:872-875`).

## 6. Code/Data Logger (CDL)

- **File format.** Raw bytes with no header: one byte per PRG byte, followed by one byte per CHR-ROM byte if the cart has CHR ROM (`fceux/src/drivers/Qt/CodeDataLogger.cpp:978-1000`). Loading ORs the file into the current log (`:850-896`).
- **PRG byte bits** (`fceux/src/debug.cpp:525-600`; `fceux/src/sound.cpp:182-189`):

| Bit | Meaning |
|---|---|
| 0 (0x01) | executed as code (every opcode and operand byte) |
| 1 (0x02) | read as data (operand address of non-write, non-JMP opcodes; vectors) |
| 2-3 (0x0C) | CPU window the byte was seen in: `(addr>>11)&0x0C` = `$8000/$A000/$C000/$E000` |
| 4 (0x10) | code reached through an indirect `JMP ($xxxx)` |
| 5 (0x20) | data accessed through `(zp,X)` or `(zp),Y` |
| 6 (0x40) | DMC (PCM) sample data |
| 7 (0x80) | accessed while mapped below `$8000` (for example PRG in `$6000-$7FFF`) |

- **CHR byte bits** (`fceux/src/ppu.cpp:492-525`, `:734-745`): bit 0 = fetched by rendering, bit 1 = read by the CPU through `$2007`. With CHR RAM, a write through `$2007` clears the entry (`:960-966`).
- **Uses.** Disassembly `c`/`d`/`cd` column, hex editor coloring, PPU viewer "mask unused graphics", trace "only new code/data", unlogged code/data breakpoints, stripped-ROM export (unused bytes zeroed, or the inverse) (`fceux/src/drivers/Qt/CodeDataLogger.cpp:543-724`), and Lua `cdlog.*`. FDS clears the CDL entry of bytes that are written (`fceux/src/debug.cpp:572-589`).

## 7. PPU, name table, OAM and palette tools (raster aspects)

- **Scanline-triggered refresh.** The core calls `FCEUD_UpdatePPUView(scanline,1)` before each visible line and `FCEUD_UpdateNTView(scanline,0)` inside `DoLine` (`fceux/src/ppu.cpp:1390`, `:1874-1875`; the `newppu` path is at `:2197-2198`). The viewer copies CHR/palette/nametable data only when `scanline == PPUViewScanline` / `NTViewScanline`, and a refresh-skip divisor thins the updates further (`fceux/src/drivers/Qt/ppuViewer.cpp:1612-1650`; `fceux/src/drivers/Qt/NameTableViewer.cpp:1755-1785`; Win32 `fceux/src/drivers/win/ppuview.cpp:45-47`, `:87`; `fceux/src/drivers/win/ntview.cpp:50`, `:377`). This shows CHR banks and nametables as they are at a chosen raster line, which exposes mid-frame bank switches and split screens.
- **Name table viewer** overlays the current scroll window ("scroll lines"), tile and attribute grids. It reports per-tile PPU address, tile index/address, attribute byte and address, and palette address, and shows the mirroring type (`fceux/src/drivers/Qt/NameTableViewer.cpp:186-461`, `:535`).
- **PPU viewer** has CDL-based masking of unused tiles (`fceux/src/drivers/win/ppuview.cpp:35-36`, `:123`), a tile editor, palette rows and ACT export. The **sprite viewer** is Qt only and reads OAM (`SPRAM`) (`fceux/src/drivers/Qt/ppuViewer.cpp:1687`, `:2976-3114`).
- **Beam position.** The old PPU is scanline based. The debugger estimates the pixel as `(timestamp*48 - linestartts)/16` (NTSC) or `/15` (PAL), and the vblank line from CPU cycles; the code itself notes that this display is unreliable. With `newppu` it shows the exact scanline and dot (`fceux/src/drivers/Qt/ConsoleDebugger.cpp:4291-4327`; `fceux/src/debug.cpp:950-958`).

## 8. Hex editor

Views of the CPU bus, PPU bus, OAM and ROM file (`fceux/src/drivers/Qt/HexEditor.h:147-151`). Features:
- ROM edits are tracked with undo and shown in red. "Save ROM"/"Save ROM As" write the file (`fceux/src/drivers/Qt/HexEditor.cpp:1141-1194`, `:3653-3657`).
- TBL files for game text, and find.
- Freeze/unfreeze through the cheat engine.
- Context menu: add R/W/X breakpoint, add symbolic name, bookmark, "Go Here in ROM File" (`:3120-3208`).
- Activity fade: a byte that changes starts at level 15, which drops by one each refresh and picks from a 16-color table (`:3590-3605`).
- CDL coloring (`:3659-3700`).

The Win32 `memview.cpp` has the same concepts (freeze, activity fading, CDL colors) (`fceux/src/drivers/win/memview.cpp:112-136`, `:195-196`).

## 9. Inline assembler

`Assemble(output, addr, str)` is a one-line 6502 assembler. It accepts `[]`/`{}` as parentheses, `;` comments and `0x` as `$`, and returns 1 on a syntax error (`fceux/src/asm.cpp:15-60`). Only the Win32 debugger exposes it. The dialog assembles line by line at the clicked address, keeps a patch list with undo, "Apply" pokes PRG memory, and "Save" writes the `.nes` (`fceux/src/drivers/win/debugger.cpp:1340-1425`). The Qt front end has no inline assembler; its disassembly comes from `DisassembleWithDebug`.

## 10. Lua API (debug-relevant parts)

- **Memory hooks.** `memory.registerwrite|registerread|registerexec(addr [, size], fn)` (aliases `register`, `registerrun`, `registerexecute`). A negative size means the range ends at `addr`, and `nil` clears the hook. The callback receives `(address, size, value)`: the value read, the value written, or 0 for exec (`fceux/src/lua-engine.cpp:2407-2477`, `:2278-2289`, `:6280-6286`). Hooks are dispatched from `RdMem`/`WrMem`/`RdRAM`/`WrRAM`/DMA and before every opcode for exec (`fceux/src/x6502.cpp:160-223`, `:641-644`). Addresses are CPU addresses; there is no bank filter.
- **Hook filtering.** `TieredRegion` keeps three island lists: broad (a single island), mid (islands merged across gaps up to 0x1000) and narrow (exact). An address must hit all three before the per-address Lua table is consulted. A comment notes about 200 ms per 10^8 calls with no hook set (`fceux/src/lua-engine.cpp:2144-2224`, `:2312-2326`).
- **`debugger.*`**: `hitbreakpoint()` (break before the next instruction), `getcyclescount`, `getinstructionscount`, `resetcyclescount`, `resetinstructionscount`, `getsymboloffset(name [, bank])` (`fceux/src/lua-engine.cpp:5037-5096`, `:6418-6426`).
- **Frame callbacks**: `emu.registerbefore/registerafter/registerexit`, `gui.register`, `savestate.registersave/registerload` (`fceux/src/lua-engine.cpp:6218-6248`, `:6330-6339`, `:6395`).
- **Other**: `memory.getregister/setregister` (`pc`, `a`, ...) (`fceux/src/lua-engine.cpp:1993-2070`), `rom.readbyte/writebyte`, `ppu.readbyte/readbyterange`, `savestate.create/save/load/persist`, `movie.*`, `cdlog.*` (start/pause/reset/load/save), `taseditor.*`. `emu.debuggerloop` / `debuggerloopstep` exist only in the Win32 build (`fceux/src/lua-engine.cpp:482-496`).

## 11. Rewind, savestates and movies

- **Savestate** format: 16-byte header `FCSX`, optionally zlib-compressed `SFORMAT` chunk stream (`fceux/src/state.cpp:379-460`).
- **State recorder** (Qt): a ring of `EMUFILE_MEMORY` snapshots taken every N frames or every T minutes, with ring size = history / interval. After a load it can pause for a few seconds or fully. If "previous" is pressed again within 30 frames of a load, it goes one more snapshot back (`fceux/src/state.cpp:1214-1470`; update hook `fceux/src/fceu.cpp:831`).
- **Autosave ring, backup and undo of load state** (`fceux/src/fceu.cpp:1356-1375`; `fceux/src/state.cpp:1078-1205`).
- **Movies**: `.fm2` text header keys (`version`, `emuVersion`, `rerecordCount`, `palFlag`, `romChecksum`, `guid`, `fourscore`, `port0-2`, `comment`, `subtitle`, `savestate`) (`fceux/src/movie.cpp:436-466`). The TAS Editor (Win32 and Qt ports) keeps a greenzone of per-frame savestates. Older frames are thinned to every 2nd/4th/8th/16th state as they age (`fceux/src/drivers/Qt/TasEditor/greenzone.cpp:130-164`; `fceux/src/drivers/Qt/TasEditor/greenzone.h:13-16`).
- **Step Back** (Qt): an instruction-level undo using the trace ring. It restores registers and, for STA/STX/STY/INC/DEC with zp/abs/(zp,X)/(zp),Y/zp,X/zp,Y operands below `$8000`, writes back the pre-write value through the normal write handler (`fceux/src/drivers/Qt/TraceLogger.cpp:1355-1406`, `:2581-2632`).

## 12. Qt vs Win32 differences

| Aspect | Qt | Win32 |
|---|---|---|
| Breakpoint/bookmark persistence | `.fdb` text next to the ROM, optional auto-load (`fceux/src/drivers/Qt/ConsoleDebugger.cpp:4683-4800`) | `.deb` binary (breakpoints + hex bookmarks) (`fceux/src/drivers/win/pref.cpp:175-230`) |
| Symbols | Core `debugSymbolTable` only; ld65 import | Own linked-list `.nl` parser with per-window reload, mirrored into the core table (`fceux/src/drivers/win/debuggersp.cpp:123-780`) |
| Inline assembler / ROM patcher | none | yes (`fceux/src/drivers/win/debugger.cpp:1340-1470`) |
| Step Back | yes (trace ring) | none |
| Counter breaks | one-shot/continuous, absolute/relative dialog | checkbox + limit field (`IDC_DEBUGGER_BREAK_ON_CYCLES`) |
| Sprite (OAM) viewer, CHR tile editor | yes | none found |
| State recorder (rewind ring) | yes | not wired (only Qt files reference `StateRecorder`) |
| Trace path | callback list + binary records + disk thread | direct `FCEUD_TraceInstruction` call, synchronous text (`fceux/src/debug.cpp:1005-1007`) |
| Breakpoint wait | emulation thread sleeps in `FCEUD_DebugBreakpoint` with mutex released | nested `win_debuggerLoop()` message pump |
| Lua `emu.debuggerloop` | no-op | functional |
| Debugger UI | multiple layouts, navigation history, fonts/colors | classic fixed dialog, Game Genie middle-click |

## 13. Notable and unique ideas

1. **CDL as a debugger primitive.** One byte of flags per PRG/CHR byte drives coverage stats, disassembly annotation, hex coloring, CHR masking, trace filtering ("only new code") and "break on first execution of unlogged code/data". For a ZX debugger, a per-physical-byte (per RAM/ROM page) code/data/indirect map could drive all of these views from one source.
2. **Scanline-triggered viewer snapshots.** Tile/nametable viewers capture state at a user-chosen raster line rather than at end of frame, which makes mid-frame bank or attribute changes visible. This maps directly to a "sample at T-state/line N" option for ZX screen, attribute and port viewers.
3. **Predicted `W` and `R` values in conditions.** Breakpoint conditions can test the value an instruction is about to write, or the value it reads, as well as the bank of the data access (`T`) and of PC (`K`). A `W==#00` style filter on writes is cheap because the value is computed only after an address match.
4. **ROM-offset execute breakpoints.** Execute breakpoints can be keyed on the physical file offset instead of the CPU address, so they follow code whichever window it is paged into. This is the NES equivalent of page-aware Z80 breakpoints on 128K/Pentagon banks.
5. **Forbid ranges.** Negative breakpoints (with their own conditions) mute every other breakpoint inside a PC range, for example to ignore hits from an NMI handler or a memcpy routine.
6. **Trace pipeline design.** Compact binary records in the emulation thread, deferred text formatting, a high-priority writer thread and unbuffered overlapped writes on Windows. Tracing stays close to real time with millions of lines.
7. **Step Back from the trace ring.** A cheap single-instruction reverse step with no snapshots: registers plus the old value of simple stores are kept per record. It is limited, but useful for "what did that instruction just do".
8. **Stack-aware memory breakpoints.** R/W breakpoints on `$01xx` catch implicit stack traffic, including interrupt pushes detected from changes in S.
9. **Netplay desync fingerprint.** A per-frame CRC32 of the executed opcode stream plus a RAM checksum gives an instruction-level divergence detector, which could be reused for determinism tests (for example of TTD replays).
10. **Lua hook tiered region cache.** A three-level interval filter keeps per-access Lua hooks nearly free when the address is not hooked.

## 14. Gaps and caveats

- **Bug: zero-page indexed addresses are not masked.** `optype` cases 5 and 8 compute `A = opcode[1] + X` (or `+ Y`) without `& 0xFF`. Breakpoints and `R`/`W`/`T` for `zp,X`/`zp,Y` that wrap past `$FF` see `$01xx` instead of the real zero-page address (`fceux/src/debug.cpp:993`, `:996`).
- **Bug: callback unlinking.** `FCEUI_TraceInstructionUnregisterHandle` assigns `cb_prev = cb->next` instead of `cb_prev->next = cb->next`. Removing a non-head callback leaves the list pointing at freed memory (`fceux/src/debug.cpp:1067-1070`).
- **Latent bug: wrong right-hand operand lookup.** The RHS path calls `getValue(c->type2)` instead of `getValue(c->value2)`. It is unreachable with the current parser, which always builds binary nodes from `lhs`/`rhs` subtrees *(inferred)* (`fceux/src/debug.cpp:454`).
- **Range ROM breakpoints.** The `endaddress` branch does not check `BT_R`, so a ROM *range* breakpoint is compared with CPU addresses, not file offsets (`fceux/src/debug.cpp:824-829`).
- **Per-instruction cost when no breakpoint is set.** `DebugCycle` always fetches up to 3 opcode bytes plus pointer bytes through `GetMem`, before the gating `if` (`fceux/src/debug.cpp:965-999`). Breakpoint matching is a linear scan over up to 64 entries per instruction, and forbid entries are rescanned on every match.
- **R/W breakpoints are predictions.** Dummy reads, RMW double writes, DMC/OAM DMA reads and interrupt vector fetches do not trigger them. PPU breakpoints see only CPU `$2007` accesses, not rendering fetches. OAM write breakpoints fire on any `$4014` write whatever their range.
- **"Bank" is a fixed 16 KiB unit.** For mappers with 8 KiB (or 32 KiB) banking, `K`/`T`, `.nl` file names and the trace bank column do not match the real mapper bank numbering (`fceux/src/debug.cpp:18`, `:297-308`).
- **No hit counters, pass counts, log-only (non-stopping) breakpoints or temporary breakpoints** in the `watchpointinfo` model (`fceux/src/debug.h:52-64`).
- **Step Back is partial.** It does not undo RMW shifts (ASL/LSR/ROL/ROR), stack pushes, writes at `$8000+` (mapper), PPU/APU side effects or cycle counters. Undo writes go through live handlers, which can themselves cause side effects (`fceux/src/drivers/Qt/TraceLogger.cpp:2581-2607`).
- **Step Over** handles only JSR (and BRK with `BRK_3BYTE_HACK`); step-out nesting counts only JSR/RTS, not interrupts or RTI (`fceux/src/debug.cpp:716-728`).
- **The beam position shown on the old PPU is approximate**, as the code comments say (`fceux/src/drivers/Qt/ConsoleDebugger.cpp:4296-4297`).
- **The Qt sprite viewer's "CPU Page #" data source is disabled** (TODO) (`fceux/src/drivers/Qt/ppuViewer.cpp:2997`).
- **Condition language limits.** No bitwise operators, no word reads, no symbols inside expressions, uppercase-only hex after `$`, and `P` means PC.
