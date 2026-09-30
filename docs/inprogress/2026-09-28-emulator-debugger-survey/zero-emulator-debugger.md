# Zero-Emulator debugger — capability survey

**Source:** https://github.com/ArjunNair/Zero-Emulator · local checkout commit `6a752be3629e8770a0bb0c35424aa276bc81a856` (2025-06-19, "Merge pull request #59 ... better_iff2_fix") · C# / .NET WinForms (DataGridView-based tool windows, ScintillaNET console)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** A GUI debugger ("Monitor") made of a disassembly window plus satellite WinForms tool windows (breakpoints, registers, memory, watch, execution log, machine state, memory heat map, a stubbed call stack). There is also a tiny "Commander" console (peek/poke/tape) and a command-line playback queue that can write an instruction trace to a file. There is no remote protocol, no expression language and no scripting.

Paths below are relative to the emulator checkout root, written as `Zero-Emulator/<path>:<line>`. The core lives in `Ziggy/` and the Windows UI in `ZiggyWin/ZiggyWin/`.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Monitor register mirror | Copies PC, SP, HL, DE, BC, AF, IR, MEMPTR, the alternate set (HL', DE', BC', AF'), IX, IY, IM and the T-state counter when it pauses | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:1417` |
| CPU & registers | Registers window | Shows 16-bit values, or high/low bytes with an "8-bit" checkbox, in hex or decimal. Flags S Z 5 H 3 P/V N C appear as checkboxes, plus IFF1 | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Registers.cs:31` |
| CPU & registers | Register values act as links | Clicking a register value scrolls the disassembly to the address that register holds | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Registers.cs:155` |
| Disassembly | Full 64K linear disassembly | A hand-written decoder (one large `switch`) sweeps 0..65535 and fills a data-bound list with address, bytes and instruction | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:1536` |
| Disassembly | Incremental re-disassembly | Re-decodes a range and stops early once decoded rows match the existing list | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7164` |
| Disassembly | Address → row lookup | A `Dictionary<int,int>` maps an address to its row, with a linear `Find` fallback | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:1000`, `:968` |
| Disassembly | Hex / decimal / ASCII display | The bytes column can show ASCII characters, and a menu toggle switches hex/decimal | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:881`, `:7741`, `:7749` |
| Disassembly | In-place byte editing | Double-clicking the Bytes cell edits it. It accepts up to 5 values (0-255, `$`/`#` for hex), pokes them, zero-fills leftover bytes and re-disassembles | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7242`, `:1336`, `:1391` |
| Disassembly | Second (core) disassembler | `zx_spectrum.Disassemble(addr)` returns one line of text. Only the CLI trace uses it | `Zero-Emulator/Ziggy/Speccy/zxSpectrum.cs:436`, `Zero-Emulator/ZiggyWin/ZiggyWin/Form1.cs:881` |
| Memory views | Memory viewer | Grid of 10 bytes per row with address, bytes and a characters column. Rebuilt on every pause | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/MemoryViewer.cs:9`, `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:439` |
| Memory views | Poke memory dialog | Writes one byte to an address, then refreshes the disassembly, memory and watch views | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/PokeMemory.cs:18`, `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:1474` |
| Memory views | Machine state window | Model, T-state, frame length, paging enabled, shadow screen, the bank in each of the four 16K pages, a "contended" marker and the ROM name | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Machine State.cs:15` |
| Navigation & bookmarks | Jump to address | A text box plus "Go" button (`$`/`#` hex or decimal) scrolls to and selects the row | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7428`, `:7444` |
| Navigation & bookmarks | Follow on pause | When paused, the view scrolls to PC and selects that row | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:1443` |
| Symbols & labels | Built-in system variables | 72 ROM system-variable names (KSTATE ... P RAMT), keyed by address | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:44` |
| Symbols & labels | Operand substitution | A single-operand instruction whose operand matches a known address shows the name. The tooltip shows the number | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:902` |
| Symbols & labels | Load symbols | `.txt`/`.csv` file with `address,name` lines, merged into the same dictionary | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7787` |
| Breakpoints (every kind) | Execution (PC) breakpoint | Toggle it by double-clicking a row or with the toolbar/menu. A glyph is drawn in the row header | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7242`, `:7674`, `:7711` |
| Breakpoints (every kind) | Register-equals breakpoints | Breaks when A, HL, BC, DE, IX, IY or SP equals a value, checked before each instruction | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:556` |
| Breakpoints (every kind) | Memory read / write / execute | Exact address, with an optional exact byte value | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:457`, `:489`, `:513` |
| Breakpoints (every kind) | Port write / port read | Write: exact port plus optional value. Read: mask match `(port & addr) == addr`, no value filter | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:737`, `:753` |
| Breakpoints (every kind) | ULA write / read | Any even port. The write can filter on a value | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:767`, `:781` |
| Breakpoints (every kind) | Retriggered interrupt | Breaks on the core's `RE_INTERRUPT` state-change event | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:698`, `Zero-Emulator/Ziggy/Speccy/zxSpectrum.cs:8002` |
| Breakpoints (every kind) | RZX frame end | Breaks at the end of any RZX playback frame, or of frame N. The status line shows expected and actual fetch and IN counts | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:809` |
| Breakpoints (every kind) | Breakpoint editor | Lists every `SPECCY_EVENT` in a combo box, with address and value fields. Can remove the selected entries or all of them | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Breakpoints.cs:77`, `:122` |
| Conditions & expressions | Fixed equality only | Each breakpoint is `(event, address, data)`, where -1 means "any". There is no expression grammar | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:283` |
| Watchpoints & watches | Watch window | Address, label (defaults to the system-variable name) and current byte. Updated on every memory write while it is visible | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/WatchWindow.cs:94`, `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:480`, `:7397` |
| Execution control | Resume / Step In / Step Over / Step Out / Run to Cursor / Stop | F5 / F11 / F10 / F12 plus toolbar buttons | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7558`–`:7672`, `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.Designer.cs:435`–`:483` |
| Tracing & logging | Execution log ("Profiler" window) | While active, logs every executed instruction as address, T-state and instruction. Can be saved as `trace.log` | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:668`, `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Profiler.cs:62`, `:89` |
| Tracing & logging | CLI trace to file | `/trace file` … `/stoptrace` writes `$PC  T  disasm` for each instruction, a register dump at start and stop, and per-frame RZX fetch/IN counts | `Zero-Emulator/ZiggyWin/ZiggyWin/Form1.cs:925`, `:876`, `:852` |
| History / rewind | RZX bookmark and rollback | While recording an RZX, Insert stores an SZX bookmark and Delete rolls back to it. These are recording tools, not debugger stepping | `Zero-Emulator/ZiggyWin/ZiggyWin/Form1.cs:1655`, `:4595`, `:4607`, `Zero-Emulator/Ziggy/Speccy/zxSpectrum.cs:6090` |
| Video, raster & beam | (none beyond ULA port breakpoints and machine state) | No beam position, raster, or screen/attribute viewers | — |
| Profiling / heat maps | Memory heat map ("Memory Profiler") | 256×256 map of the 64K address space at 2×2 px per byte. Read = cyan, write = red, redrawn and cleared once per second | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/MemoryProfiler.cs:39`, `:118` |
| Scripting, automation & remote | Command-line playback queue | `-q` queues `/loadfile`, `/waitframes N`, `/trace`, `/stoptrace`, `/savesnap`, `/debug`, `/exit` | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/CLIOptions.cs:45`, `Zero-Emulator/ZiggyWin/ZiggyWin/Form1.cs:900`, `Zero-Emulator/README.md:111` |
| Scripting, automation & remote | Commander console | `tape stop|start|play`, `poke a v [v…]`, `poke a1 to a2 with v`, `peek a [a…]`, `peek a1 to a2` | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Command.cs:16`, `:53`, `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Commander.cs:24` |
| Import / export & persistence | Save disassembly | Writes the whole disassembly list to `disassembly.log` | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7861` |
| Import / export & persistence | Load / save binary | Loads a file at an address (16384+) or into a RAM bank. Saves an address range or a bank | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/LoadBinary.cs:51` |
| Import / export & persistence | Trainer (POKE) files | Applies or reverts named groups of `bank address new old` pokes | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Trainer Wizard.cs:26`, `:59` |
| Import / export & persistence | Breakpoint and watch persistence | None. Both are cleared when the monitor closes | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7323` |
| UI conveniences | Hotkeys | Alt+B breakpoints, Alt+P poke, Alt+W watch, Alt+L execution log, Alt+S machine state, Alt+V memory, Alt+R registers | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.Designer.cs:523`–`:592` |
| UI conveniences | Tool windows remembered | Satellite windows hide with the monitor and reopen when it is shown again | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:7611`, `:7902` |
| UI conveniences | Stack window | "Stack" button opens a call-stack viewer. It is a stub with no data source | `Zero-Emulator/ZiggyWin/ZiggyWin/Tools/CallStackViewer.cs:19`, `:37` |

## 2. CPU, registers and machine state

- The monitor keeps a private copy of the registers (`pc, bc, de, hl, ir, mp, sp, _bc, _de, _hl, ix, iy, im, af, _af`) and `tstates` (`Zero-Emulator/ZiggyWin/ZiggyWin/Tools/Monitor.cs:24`). `UpdateZXState` fills this copy from `cpu.regs` when the monitor enters PAUSE (`Monitor.cs:1195`, `:1417`). The comment says this avoids a frame-rate hit from updating the UI on every opcode (`Monitor.cs:546`).
- The Registers window has two modes. With "8-bit" checked (`checkBox1`) it shows `HH  LL` pairs; otherwise it shows whole 16-bit words. Each mode can be hex or decimal (`Registers.cs:31`–`:100`). The F register is decoded into eight checkboxes, including the undocumented bits 3 and 5 (`Registers.cs:102`–`:143`). IFF1 is shown (`Registers.cs:145`); IFF2 is not.
- The registers are read-only. Every value is a `LinkLabel` that jumps the disassembly to that value (`Registers.cs:155`–`:209`). There is no in-place editing of registers.
- The Machine State window (`Machine State.cs:15`) shows the model name, `cpu.t_states`, `FrameLength`, `!pagingDisabled`, `showShadowScreen`, `BankInPage0..3` as strings, `contendedBankPagedIn` and the ROM name for each model. For 48K, pages 2 and 3 show `-----` (`:28`–`:32`).

## 3. Disassembly

- **Decoder.** The protected `Disassemble(startAddr, endAddr, rebuild, traceOn)` (`Monitor.cs:1536`) is a large `switch` that runs to about line 7200. It covers the unprefixed, CB, ED, DD/FD and DDCB/FDCB tables. It handles undocumented DD/FD fall-through with a `goto jmp4Undoc` (`Monitor.cs:1573`, `:7151`). An unknown DDCB opcode opens a MessageBox (`Monitor.cs:7108`–`:7111`).
- **Format.** Each row stores a format string (for example `"LD BC, {0,0:D}"`) plus `Param1`/`Param2` (`Monitor.cs:1587`, `:942`). The text is only built when displayed. Switching hex/decimal rewrites `:D` ↔ `:x` in the format string (`Monitor.cs:918`–`:922`), so the list does not have to be rebuilt.
- **Rebuild strategy.** `rebuild == true` recreates the whole list. `rebuild == false` walks from the row found for `startAddr` and overwrites rows in place. When a decoded row matches the stored one, it skips to the next (`Monitor.cs:7164`–`:7179`). The address → row dictionary is rebuilt at the end (`Monitor.cs:7202`).
- **Linear sweep.** The listing always starts at address 0 and runs to 65535 (`Monitor.cs:435`, `:1126`, `:7217`). It is not anchored on PC, so a PC inside a misaligned instruction resolves to the closest earlier row (`FindCore` returns `i - 1`, `Monitor.cs:975`) *(inferred: code reached by a different alignment is shown wrongly)*.
- **Byte editing.** Double-clicking column 1 (Bytes) puts the cell into edit mode (`Monitor.cs:7247`–`:7256`). `CellValidating` splits the input on spaces, converts each value with `Utilities.ConvertToInt` (`$`/`#` prefix means hex, `Zero-Emulator/ZiggyWin/ZiggyWin/Utilities.cs:61`) and pokes it with `PokeByteNoContend`. If the new instruction is shorter, the remaining old bytes are set to 0 (`Monitor.cs:1369`–`:1385`). `CellEndEdit` then re-disassembles `addr..addr+10` (`Monitor.cs:1396`).
- **Glyphs.** `RowPostPaint` draws a breakpoint icon for rows in `breakpointRowList` and an arrow for the current row (`Monitor.cs:7711`–`:7727`).

## 4. Memory views and editing

- **Memory viewer.** `memoryViewList` is a `BindingList<MemoryUnit>` of 10-byte rows. It is fully rebuilt whenever the monitor pauses (`Monitor.cs:439`–`:450`) or resyncs (`:1130`). It has three columns: Address, Bytes (hex or decimal), and Characters (32..127, otherwise `.`) (`Monitor.cs:1054`–`:1082`, `MemoryViewer.cs:20`–`:39`). The grid is read-only and has no jump box. There is no bank selector: all reads go through `PeekByteNoContend`, which follows the currently paged 64K.
- **Poke.** The Poke dialog calls `Monitor.PokeByte`. That pokes without contention, re-disassembles at the address, patches the memory-view row, and updates matching watch entries (`Monitor.cs:1474`–`:1490`).
- **Commander.** `peek`/`poke` parse numbers with `Convert.ToInt32`, so only decimal works (`Command.cs:83`, `:93`, `:114`). Both range forms are exclusive of `addr2` (`Command.cs:87`, `:118`).

## 5. Symbols and labels

- One dictionary, `systemVariables` (`Monitor.cs:44`), serves as the symbol table. It is pre-filled with 72 48K ROM system variables.
- **Load Symbols** reads each line as `addr,name` (`addr` decimal or `$`/`#` hex). It skips addresses above 65535 and overwrites or adds entries (`Monitor.cs:7787`–`:7815`). A malformed line pops a MessageBox from `ConvertToInt` (`Utilities.cs:70`).
- **Substitution** happens only when an instruction has exactly one parameter (`param2 == int.MaxValue`) and that parameter is in the dictionary. The whole format is then filled with the name, and the numeric value goes into the tooltip (`Monitor.cs:906`–`:914`, `:1403`). Jump and call targets are substituted the same way, since they are also `Param1`. Labels are never shown as row prefixes, and two-operand instructions such as `LD (IX+d), n` are never labelled.
- The "System Variables" menu check turns substitution on and off (`Monitor.cs:7762`).

## 6. Breakpoints

**Data model.** `breakPointList` is a `BindingList<KeyValuePair<SPECCY_EVENT, BreakPointCondition>>` (`Monitor.cs:1027`). A parallel `breakPointConditions` list feeds the editor grid (`:1032`). `BreakPointCondition` holds `(condition, address, data)`, where -1 means "don't care". Equality compares all three fields (`Monitor.cs:283`–`:342`).

**Event kinds** (`Zero-Emulator/Ziggy/Speccy/speccy_common.cs:86`–`:131`) and how each is handled:

| Kind | Key | Match rule | Where checked |
|---|---|---|---|
| A / PC / HL / BC / DE / IX / IY / SP | register value in `Address` | equality; `Data` ignored | `Monitor.cs:556`–`:612` (before each opcode) |
| Memory Write | address, optional value | exact address; if `Data > -1`, value must match | `Monitor.cs:457` (raised in `PokeByte` **before** the store, `zxSpectrum.cs:6806`) |
| Memory Read | address, optional value | same | `Monitor.cs:489` (raised by contended `PeekByte`, `zxSpectrum.cs:6797`) |
| Memory Execute | address, optional value | same | `Monitor.cs:513` (raised by the opcode-fetch path, `zxSpectrum.cs:6778`) |
| Port Write | port, optional value | exact port; optional value | `Monitor.cs:737` |
| Port Read | port | `(e.Port & bp.Address) == bp.Address` (mask); no value; during RZX playback **any** IN breaks | `Monitor.cs:753`–`:765` |
| ULA Write | fixed 254 | any port with A0 = 0; optional value | `Monitor.cs:767`, `Breakpoints.cs:138` |
| ULA Read | fixed 254 | any port with A0 = 0 | `Monitor.cs:781` |
| Retriggered Interrupt | — | state-change event | `Monitor.cs:698`; raised only at `zxSpectrum.cs:8002` |
| RZX Frame end | frame number in `Data` | -1 = every frame | `Monitor.cs:809` |
| Interrupt, Frame Start, Frame End, RZX Playback start, RZX port read | — | listed in the editor but never raised: `StateChangeEvent` is invoked only for `RE_INTERRUPT`, and `OnRZXPlaybackStartEvent` has no caller | `zxSpectrum.cs:46`, `:8002` (grep of `Ziggy/` and `ZiggyWin/`) |

- **Addressing** is always the logical 16-bit CPU address or port. There is no bank or page qualifier anywhere in `BreakPointCondition`.
- **Registration.** The monitor subscribes to all core events when it loads or resyncs (`Monitor.cs:1111`, `:7215`) and unsubscribes on close (`:1100`, `:7324`). The core raises memory and port events only when there is a subscriber (`zxSpectrum.cs:6777`, `:6797`, `:6806`, `:6871`), so with the monitor closed the check costs one null test. With the monitor open, every opcode, memory access and port access builds an `EventArgs` object and scans the whole breakpoint list linearly, whether or not breakpoints exist. A commented-out variant once deregistered when the list was empty (`Monitor.cs:1180`). The opcode hook runs at the top of the run loop, before the instruction (`zxSpectrum.cs:6674`), so a PC breakpoint stops before the instruction executes.
- **Re-hit guard.** After resuming, the first `OpcodeExecuted` call with the same PC is skipped (`Monitor.cs:540`–`:543`).
- **Break action.** Always "pause and show the monitor". `DoPauseEmulation` redisassembles all of 0..65535, rebuilds the memory view, updates the status line `"Breakpoint hit: …"` and refreshes every tool window (`Monitor.cs:428`–`:454`). There are no hit counts, enable/disable flags, log-only actions or temporary breakpoints (only the implicit run-to-cursor address).
- **Editor.** The combo box lists every enum description. For the first 14 entries an address is required. For ULA read and write the address is fixed to `$fe`. For interrupts both fields are disabled. The value field is optional (`Breakpoints.cs:122`–`:180`).

## 7. Conditions, expressions and watches

- There is no expression evaluator. A condition is a single equality test built into each event kind (section 6).
- **Watches** (`Monitor.WatchVariable`, `Monitor.cs:228`) are single bytes: address, label and value. Adding a duplicate address is ignored (`WatchWindow.cs:108`). Values refresh on every memory-write event while the window is visible (`Monitor.cs:480`–`:485`), on resync (`:1143`) and after a poke (`:1482`). Watches cannot trigger a break. They display bytes only, with no word or type formats.

## 8. Execution control

State machine `MonitorState { RUN, PAUSE, STEPIN, STEPOVER, STEPOUT, RUNTOCURSOR }` (`Monitor.cs:218`). Emulation runs on the UI thread's idle loop (`Form1.cs:1003` `zx.Run();` under `if (zx.doRun)`). `zx.Pause()` and `zx.Resume()` return immediately; the old thread code is left after a `return; //THREAD` (`zxSpectrum.cs:408`–`:422`). Pausing works only through `doRun = false`.

- **Step In (F11):** sets STEPIN. After the next opcode hook the state goes back to PAUSE and the views refresh (`Monitor.cs:7600`, `:659`–`:664`).
- **Step Over (F10):** checks the **text** of the current row. If it contains `CALL`, `RST`, `LDIR`, `LDDR`, `INIR`, `INDR`, `OTIR`, `OTDR`, `CPIR` or `CPDR`, it sets `runToCursorAddress` to the next row's address and hides the monitor. Otherwise it behaves like Step In (`Monitor.cs:7558`–`:7598`).
- **Step Out (F12):** runs until PC sits on an opcode from the RET family (`C9`, `C0/C8/D0/D8/E0/E8/F0/F8`, `ED 45`, `ED 4D`), then pauses at the next instruction (`Monitor.cs:621`–`:650`, `:7734`). It does not track stack depth or whether a conditional RET is taken *(inferred: a nested RET or an untaken `RET cc` stops early)*.
- **Run to Cursor:** a one-shot `runToCursorAddress` compared on every opcode (`Monitor.cs:615`–`:619`, `:7651`).
- **Resume (F5)** hides the monitor and its tool windows and sets RUN (`Monitor.cs:7665`). **Stop Debugging** closes the monitor, which unsubscribes events and clears breakpoints, watches and log (`Monitor.cs:7607`, `:7323`).
- Every step or run resets the emulated keyboard, so a key held when the monitor took focus is not left stuck (`Monitor.cs:7602`, `:7656`).

## 9. Tracing and logging

- **In-monitor execution log** (window titled "Profiler", menu "Execution Log", Alt+L). "Start" clears `logList` and sets `isTraceOn` (`Profiler.cs:89`–`:105`). On each opcode, when PC has changed and the state is not STEPOVER, the previous instruction is decoded in trace mode and a `LogMessage { Address, Tstates, Opcodes }` is appended (`Monitor.cs:668`–`:683`). The T-state is taken **before** the logged instruction runs (`previousTState`, `:682`). The log is an unbounded `BindingList` kept in memory. Sample line written by Save (`Profiler.cs:79`, format `{0,-5}   {1,-5}   {2,-20}`):
  ```
  All numbers in hex.
  -------------------
  4d4     13920   LD HL, 5c3b
  ```
  *(sample reconstructed from the format strings)*
- **CLI trace** (`-q /trace file … /stoptrace`). It subscribes `OnSpeccyExecutedOpcode` to the core opcode event and writes `$%04x\t%5d\t<disasm>` with the core disassembler (`Form1.cs:877`–`:883`). It dumps PC/SP/IX/IY/HL/HL'/DE/DE'/BC/BC'/AF/AF' at start and stop (`Form1.cs:885`–`:899`). During RZX playback it adds an "RZX Frame N: Expected fetches / Actual fetches / Expected INs / Actual INs" block at the end of each frame (`Form1.cs:856`–`:865`). This is the most useful tool here for checking RZX sync.
- **Save disassembly** writes every row as `{0,-5}   {1,-15}   {2,-20}` to `disassembly.log` (`Monitor.cs:7861`–`:7893`).

## 10. History, rewind, time travel

There is no debugger rewind or reverse step. The only related feature is RZX recording: Insert adds a bookmark, which is an SZX snapshot in the recording (`Form1.cs:1655`, `zxSpectrum.cs:6090`), and Delete rolls back to the last bookmark (`Form1.cs:1661`, `:4607`, `zxSpectrum.cs:6098`). This is aimed at players making recordings, not at debugging.

## 11. Video, raster, beam and device views

- None. There is no beam position, no per-T-state event view, no screen, attribute, sprite or font viewer, and no contention view. What exists nearby: ULA read/write port breakpoints (section 6), the `contendedBankPagedIn` flag and shadow-screen flag in Machine State (`Machine State.cs:20`, `:25`), and the T-state counter.
- No sound or AY register view was found in the debugger tools (`ZiggyWin/ZiggyWin/Tools/` listing).

## 12. Profiling, heat maps, coverage

- **Memory Profiler** (menu Tools → Memory Profiler, `Monitor.cs:7967`). It subscribes to the memory read and write events. A write sets `heatMap[addr] = 6` and a read sets `1` (`MemoryProfiler.cs:118`–`:126`). A background thread busy-waits (no sleep) until one second has passed, then paints each address as a 2×2 block in a 512×512 bitmap (256 addresses per row) with an 8-color palette, and clears the map (`MemoryProfiler.cs:39`–`:84`). So it shows "touched in the last second", split into read (cyan) and write (red). Execute is not tracked.
- There is no instruction or cycle profiler, no call graph and no code/data logger. The "Profiler" button opens the execution log (section 9).

## 13. Scripting, automation and remote

- **Command-line playback queue** (`-q`): `/loadfile`, `/waitframes N` (counted by `FrameEndEvent`), `/trace`, `/stoptrace`, `/savesnap` (SZX), `/debug` (opens the monitor), `/exit` (`Form1.cs:900`–`:977`, `Form1.cs:2191`, `README.md:111`–`:120`). Commands run one after another from the idle loop (`Form1.cs:986`). This makes unattended "load → wait → trace N frames → snapshot → exit" runs possible.
- **Commander** console (ScintillaNET): each registered `Command` handler gets the lower-cased, space-split input (`Commander.cs:24`–`:25`, `:84`–`:93`). Only tape and memory handlers exist (`Command.cs`). There is no command for breakpoints, registers or stepping.
- There is no GDB stub, no DeZog, no socket or HTTP API and no scripting language.

## 14. Import, export and persistence

- **Import:** symbol files (section 5), binary load to an address or a RAM bank (`LoadBinary.cs:51`–`:110`; address mode requires 16384..65535, `:64`), trainer POKE files (`Trainer Wizard.cs:26`), plus the usual snapshot and tape formats.
- **Export:** disassembly log, execution log, binary save (address range or bank, `LoadBinary.cs:111`–`:178`), SZX snapshot.
- **Persistence:** none for the debugger. Breakpoints, watches, the log and loaded symbols live only in memory. Breakpoints, watches and the log are cleared when the monitor closes (`Monitor.cs:7323`–`:7334`). Symbols stay in the `Monitor` instance, which is only hidden (`e.Cancel = true`, `:7357`).

## 15. UI conveniences

- Toolbar plus menu, with hotkeys F5/F10/F11/F12 and Alt+B/P/W/L/S/V/R (`Monitor.Designer.cs:435`–`:592`).
- Double-click toggles a PC breakpoint. The row header shows a breakpoint icon and a current-row arrow (`Monitor.cs:7242`, `:7711`).
- Tooltip on the Instruction column: "Double-click to toggle a breakpoint at this address.", or the numeric value when a label was substituted (`Monitor.cs:1403`–`:1413`).
- Tool windows are hidden and restored together with the monitor (`Monitor.cs:7611`, `:7902`).
- One global hex/decimal toggle applies to every tool window (`Monitor.cs:7749`–`:7760`, `UpdateToolsWindows` `:381`).
- The monitor leaves fullscreen and forces a screen repaint when it breaks (`Monitor.cs:429`–`:431`).
- Number input everywhere accepts `$` or `#` as the hex prefix (`Utilities.cs:65`).

## 16. Notable and unique ideas

1. **RZX-aware debugging.** A per-frame RZX breakpoint with expected/actual fetch and IN counts (`Monitor.cs:809`–`:816`), and the same numbers in the CLI trace (`Form1.cs:856`). It gives a direct view of desync in input recordings. This is rare among ZX emulators and useful for a TTD/replay design.
2. **Headless trace via command queue.** `-q "/loadfile x" "/waitframes 100" "/trace t.log" "/waitframes 1" "/stoptrace" "/exit"` is a simple, reproducible batch trace with register dumps at both ends (`Form1.cs:900`–`:977`).
3. **Breakpoint on a register value** (A, HL, BC, DE, IX, IY, SP equality), not only on PC (`Monitor.cs:556`). Cheap, and handy for "when does HL reach the screen address".
4. **ULA-port and port-mask breakpoints.** "Any even port" read/write, and a mask-style port read (`Monitor.cs:760`, `:767`). Useful for Spectrum port decoding, where the ULA answers on A0 alone.
5. **Lazily formatted disassembly.** Rows store a format string plus parameters, so hex/decimal and label substitution switch without re-decoding (`Monitor.cs:902`–`:932`).
6. **Operand symbolization from the ROM system-variable table by default** (`Monitor.cs:44`). Out of the box, BASIC/ROM code shows `LD HL, (CH ADD)`-style operands.
7. **Read/write memory heat map with a one-second decay** (`MemoryProfiler.cs`). A crude but instant "what is this program touching" overview.
8. **Byte editing directly in the disassembly grid** with immediate re-decode (`Monitor.cs:1336`–`:1401`).

## 17. Gaps and caveats

- **Listed but dead breakpoint kinds:** Interrupt, Frame Start, Frame End, RZX Playback start and RZX port read appear in the editor but are never raised (section 6). The RZX-playback-start handler exists (`Monitor.cs:800`), but `OnRZXPlaybackStartEvent` (`zxSpectrum.cs:46`) has no caller.
- **Port breakpoints don't fire on +3.** `zx_plus3.cs` overrides `In` and `Out` (`:238`, `:628`) without calling `base.In` or `base.Out`, the only places that raise `PortEvent` (`zxSpectrum.cs:6868`–`:6894`) *(inferred from grep: no `base.In`, `base.Out` or `PortEvent` in that file)*. The 48K/128K/128KE/Pentagon models do call the base methods.
- **Port read raises twice per IN:** once from `In(port)` with value 0 and once from `In(port, result)` (for example `zx_48k.cs:231`, `:260`). A port-read breakpoint can therefore fire before the value is known.
- **Port-write status bug:** the no-value branch formats the `BreakPointCondition` object instead of the port (`Monitor.cs:747`), and the port-read branches do the same (`:756`, `:761`), so the status line shows a type name.
- **Step Out** is not stack-aware and counts an untaken `RET cc` as a return (`Monitor.cs:626`–`:648`). **Step Over** relies on string-matching the mnemonic (`Monitor.cs:7574`).
- **Call stack viewer is a stub.** Its data source is commented out (`CallStackViewer.cs:19`, `:37`), the push/pop handlers are empty (`Monitor.cs:720`–`:724`), and `PushStackEvent`/`PopStackEvent` are never invoked.
- **Heat map bugs:** the color index is `heatMap[f] % 7`, so the palette's 8th entry is unreachable (`MemoryProfiler.cs:63`). `FormClosing` unsubscribes the write event but not the read event (`:131`), so the read handler stays attached after the window closes. The paint thread busy-spins and assigns `pictureBox1.Image` from outside the UI thread (`:45`, `:81`).
- **Cost of a break:** every pause re-disassembles all 64K and rebuilds 6,554 memory rows (`Monitor.cs:435`, `:440`). While the monitor is open, every memory access allocates an `EventArgs` and scans all breakpoints (section 6).
- **No bank awareness:** breakpoints, the memory view and the disassembly all work on the currently paged 64K only.
- **Memory breakpoints only see contended accessors** (`PeekByte`, `PokeByte`, the fetch path). Accesses through the `NoContend` helpers, such as some block operations *(inferred)*, and all debugger pokes bypass them.
- **Trace log is unbounded in memory** (`logList`) and is refreshed by rebinding the entire DataGridView (`Profiler.cs:56`).
- **IFF2, the halt state and the interrupt/NMI line are not shown** in the Registers window (`Registers.cs:145`).
- **`Form1_BAK.cs`** (3.9k lines) is a stale copy left in the tree and is not part of the survey.
