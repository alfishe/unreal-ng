# zxsp debugger — capability survey

**Source:** https://github.com/Megatokio/zxsp · local checkout commit `27d2c30` (2025-12-13) · C++ / Qt 5 (macOS and Linux)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** GUI-only debugger built from "Inspector" tool windows (one per emulated item: Z80, ULA, AY, keyboard, tape, FDC, interfaces) plus four memory inspectors (hex, disassembly, 1-bit graphics, access heat map). Run control lives in the Machine menu. No console monitor, no scripting, no remote protocol, no trace log, no rewind.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Z80 inspector | Clock (MHz), current CPU cycle in frame, A/F, A'/F', flags as `SZ1H1VNC` string, PC, SP, BC, DE, HL, IX, IY, BC', DE', HL', IM, I, R; checkboxes INT pending, NMI pending, IE (IFF1); refresh 10 Hz | `zxsp/Source/Qt/Inspector/Z80Insp.cpp:17-125`, `:127-196` |
| CPU & registers | Register editing | Every field is an editable line edit; Return writes the register; flags accept letters `SZHVNC`; IM accepts 0..2 | `zxsp/Source/Qt/Inspector/Z80Insp.cpp:198-290` |
| CPU & registers | Interrupt / NMI injection | Ticking INT raises /INT, ticking NMI triggers NMI, IE sets IFF1/IFF2 | `zxsp/Source/Qt/Inspector/Z80Insp.cpp:325-349` |
| Execution control | Run to CPU cycle | Typing a cycle number into "Cpu cycle" while halted runs the machine forward (modulo frame length) to that T-state | `zxsp/Source/Qt/Inspector/Z80Insp.cpp:292-306` |
| Execution control | CPU clock override | Typing a MHz value sets machine speed from CPU clock | `zxsp/Source/Qt/Inspector/Z80Insp.cpp:308-316` |
| Execution control | Halt / Step in / Step over / Step out | Menu actions with Cmd/Ctrl+Shift+H/I/S/O | `zxsp/Source/Qt/MachineController.cpp:854-863`; `zxsp/Source/Uni/Machine/Machine.cpp:1186-1298` |
| Execution control | NMI / reset buttons | Push NMI (Ctrl+Shift+N), reset, power-on reset | `zxsp/Source/Qt/MachineController.cpp:851-853` |
| Breakpoints | Read / write / execute breakpoints per byte | Flag bits stored in each memory cell's 32-bit `CoreByte`; set by clicking bytes in the hex or disassembly inspector with R/W/X mode buttons | `zxsp/Source/Uni/Items/Z80/Z80options.h:28-30`; `zxsp/Source/Qt/Inspector/MemoryHexInspector.cpp:960-1032`; `zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:1049-1076` |
| Breakpoints | Global enable | "Enable breakpoints" (Ctrl+Shift+B) sets `cpu_break_rwx` in `cpu_options` | `zxsp/Source/Qt/MachineController.cpp:864-865`, `:1977-1984` |
| Breakpoints | Stack breakpoint (internal) | `cpu_break_sp` + `stack_breakpoint`: stops when SP equals a target after RET/POP/EX (SP),HL; used by step over/out | `zxsp/Source/Uni/Items/Z80/Z80macros.h:451-458`; `zxsp/Source/Uni/Items/Z80/Z80.h:215`, `:263` |
| Breakpoints | Hit reporting | Status message "CPU stopped at 'read'/'write'/'exec' breakpoint at $XXXX" | `zxsp/Source/Uni/Machine/Machine.cpp:1126-1150` |
| Memory views | Data source selector (all memory inspectors) | "As seen by CPU" (64K), "All Rom", "Rom Pages", "All Ram", "Ram Pages" + page combobox listing each page's base..end | `zxsp/Source/Qt/Inspector/MemoryInspector.cpp:104-135`, `:402-435`, `:486+` |
| Memory views | Hex/ASCII view and editor | 8..64 bytes/row (16/32 presets), byte or word mode, resizable; Edit mode for hex nibbles and ASCII (Meta sets bit 7); bit-7 chars shown inverted | `zxsp/Source/Qt/Inspector/MemoryHexInspector.cpp:52-191`, `:258-352`, `:1034-1112` |
| Memory views | Register highlights | Bytes at BC/DE/IX/IY (yellow), SP (green), HL (cyan), PC (red) painted as paper colors | `zxsp/Source/Qt/Inspector/MemoryHexInspector.cpp:779-816`; colors `zxsp/Source/Qt/Inspector/MemoryInspector.cpp:36-39` |
| Memory views | 1-bit graphics view | Memory as a monochrome bitmap, 1..128 bytes per row, hover highlights the byte and shows `$addr: $value` tooltip | `zxsp/Source/Qt/Inspector/MemoryGraphInspector.cpp:28-82`, `:395` |
| Navigation & bookmarks | Go to address / register | Address line edit; combobox PC/SP/BC/DE/HL/IX/IY jumps there, switching ROM/RAM page source if needed | `zxsp/Source/Qt/Inspector/MemoryInspector.cpp:130-135`, `:168-185`, `:306-363` |
| Disassembly | Disassembly view | Address, hex, mnemonic columns; PC row painted red; resizable 16..40 mnemonic columns, 2..100 rows | `zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:35-38`, `:491-630` |
| Disassembly | Follow PC | Selecting PC in the register combobox turns on follow mode; auto-off when running fast or while editing | `zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:799-820`, `:941-953` |
| Disassembly | Inline assembler | Edit a mnemonic in place; line is re-assembled on every key with zasm, pen green if valid, red if not; Return stores bytes and pads leftover bytes of overwritten opcodes with NOP | `zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:446-490`, `:1135-1200` |
| Disassembly | Backward scrolling | Heuristic "step back n opcodes" by disassembling forward from `addr - 4n - 12` | `zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:326-374` |
| Profiling, heat maps | Memory access inspector | Per-byte R/W/X access heat map: read blue, write green, execute red, mixed colors combine; decay modes Flash / Decay fast / Decay slow / Accumulate; 32..512 bytes per row, 2x2..4x4 pixels | `zxsp/Source/Qt/Inspector/MemoryAccessInspector.cpp:39-44`, `:106-194`, `:254-336`, `:471-490` |
| Video, raster & beam | ULA inspector | ULA clock, CPU clock + overdrive % + predivider, top/screen/bottom rows, columns, bytes/row, CPU cycles per row and per frame, frames/s | `zxsp/Source/Qt/Inspector/UlaInsp.cpp:67-131`, `:372-463` |
| Video, raster & beam | Contention readout | "cpu waitcycles (contended video ram)" checkbox, waitmap offset from screen start, 8-cycle waitmap pattern as `1-1-1-1-1-1-0-0` | `zxsp/Source/Qt/Inspector/UlaInsp.cpp:141-159`, `:465-492` |
| Video, raster & beam | Border / MIC / EAR | Border color index with matching background color, MIC and EAR output bits | `zxsp/Source/Qt/Inspector/UlaInsp.cpp:165-184`, `:494-518` |
| Video, raster & beam | Paging readout | Port `$7FFD`, `$1FFD`, pages at `$0000/$4000/$8000/$C000`, video page, "mmu port locked", "ram only", disc motor, printer strobe | `zxsp/Source/Qt/Inspector/UlaInsp.cpp:203-269`, `:304-370` |
| Video, raster & beam | Frames hit | Screen refresh health (%) as exponential average | `zxsp/Source/Qt/Inspector/UlaInsp.cpp:274-275`; `zxsp/Source/Qt/Screen/ScreenZxsp.cpp:39-42` |
| Sound & device views | AY inspector | Clock, pitch/volume per channel A/B/C, noise pitch, mixer, envelope pitch and shape (letter graphics), ports A/B; editable | `zxsp/Source/Qt/Inspector/AyInsp.cpp:190`, `:194-198`, `:242+` |
| Sound & device views | Keyboard inspector | Image of the machine keyboard; pressed keys shown; clicking a key presses it on the emulated matrix | `zxsp/Source/Qt/Inspector/KeyboardInspector.cpp:751-776` |
| Sound & device views | Tape recorder inspector | Animated deck, current major/minor block info, tape position; context menu: insert/delete empty block, eject, save as, auto start/stop, instant load | `zxsp/Source/Qt/Inspector/TapeRecorderInsp.cpp:504-520`, `:702-712` |
| Sound & device views | Other device inspectors | ~50 inspectors: FDCs (+3, Beta 128, +D, D80, JLO), DivIDE, Multiface 1/128/3, joysticks, printers, SpectraVideo, Currah µSpeech, IF1/IF2, RAM packs, TC2068 dock | factory `zxsp/Source/Qt/Inspector/Inspector.cpp:214-340` |
| Import / export | RZX record | Record RZX, auto-start on any key, append snapshots | `zxsp/Source/Qt/MachineController.cpp:951-955`; `zxsp/Source/Uni/Machine/Machine.cpp:1115-1119` |
| Import / export | Snapshots, screenshots, GIF | Save as, screenshot, record GIF movie | `zxsp/Source/Qt/MachineController.cpp:846-849` |
| Import / export | Inspector settings persistence | Data source, pages, rows, bytes/row, scroll position, window size, word mode, decay mode, pixel size stored per memory inspector kind | `zxsp/Source/Qt/Inspector/MemoryInspector.cpp:152-161`; `zxsp/Source/Qt/Inspector/MemoryAccessInspector.cpp:205-209` |
| UI conveniences | Tool windows | Any number of inspector windows; right-click switches a window to another item (machine image, memory views, any attached item) | `zxsp/Source/Qt/ToolWindow.cpp:266-310` |
| UI conveniences | Memory-view shortcuts | Hex Ctrl+M, Disassembly Ctrl+Shift+M, Graphical Ctrl+Alt+M, Access Ctrl+Meta+M | `zxsp/Source/Qt/MachineController.cpp:880-890` |

## 2. CPU and registers

- The Z80 inspector is a fixed 200x350 grid of `MyLineEdit` fields refreshed by a 10 Hz `QTimer`, writing only changed values (`zxsp/Source/Qt/Inspector/Z80Insp.cpp:125`, `:139-196`).
- Editing: `slotReturnPressedInLineEdit` parses the text with `intValue` and writes straight into `getRegisters()` (`:198-290`). The flags field is parsed letter by letter (`V S Z H N C`), undocumented bits 3/5 are shown as fixed `1` in the mask string `SZ1H1VNC` (`:187`, `:262-281`).
- The "Cpu cycle" field is a **run-to-T-state** control: when the machine is suspended, entering `n` runs `(n - cc + cc_ffb) % cc_ffb` cycles, i.e. forward to cycle `n` of the current or next frame (`:292-306`).

## 3. Breakpoints

**Storage.** Every emulated memory byte is a 32-bit `CoreByte`: data in the low byte, option flags in the upper bits (`zxsp/Source/Uni/Memory.h:16`; `zxsp/Source/Uni/Items/Z80/Z80.h:48-64`). The same bit positions are used as `cpu_options` switches and as per-byte flags (`zxsp/Source/Uni/Items/Z80/Z80options.h:23-47`):

| Bit | Name | Meaning |
|---|---|---|
| 8 | `cpu_waitmap` | add contention wait cycles |
| 10/11/12 | `cpu_break_x/w/r` | execute / write / read breakpoint |
| 13 | `cpu_patch` | ROM patch trap (tape traps, DivIDE, Multiface paging) |
| 15 | `cpu_crtc` | update screen before this write |
| 16/17/18 | `cpu_r/w/x_access` | access-tracking bits for the access inspector |
| 19/20 | `cpu_memmapped_r/w` | memory-mapped I/O callback |
| 21 | `cpu_floating_bus` | unmapped read returns floating-bus byte |
| 30 | `cpu_break_sp` | option only: check stack breakpoint |

**Keying.** Flags live on the physical byte, so breakpoints are bank-aware by construction: a breakpoint set in "Ram Pages" on page 3 fires only when that physical page is accessed, wherever it is mapped (inferred from `PgInfo::core_r/core_w` pointing into the backing arrays, `zxsp/Source/Uni/Items/Z80/Z80.h:48-64`, and `dataReadPtrForOffset`, `zxsp/Source/Qt/Inspector/MemoryInspector.cpp:187-199`). In "As seen by CPU" mode the click goes to whatever page is mapped at that moment.

**Cheap checks.** Each memory access does one load of the `CoreByte` and one AND with `options`; only when a set bit survives is any branch taken (`PEEK` `zxsp/Source/Uni/Items/Z80/Z80macros.h:80-100`, `POKE` `:102-145`, `GET_INSTR` `:164-200`). With breakpoints disabled the cost is the AND. `Machine::runForSound` also strips `cpu_waitmap | cpu_crtc` from `options` outside the contended screen window (`zxsp/Source/Uni/Machine/Machine.cpp:1084-1103`).

**Hit handling.** `PEEK`/`POKE` record `break_addr`, set `result = cpu_exit_r/w` and `ic_max = 0` (the instruction finishes, then `run()` returns). `GET_INSTR` exits before executing (`cpu_exit_x`). `Machine::runForSound` suspends and shows the status message (`zxsp/Source/Uni/Machine/Machine.cpp:1126-1150`). To resume past an execute breakpoint at PC, `break_ptr` remembers the `CoreByte` that stopped the CPU and `GET_INSTR` skips it once (`zxsp/Source/Uni/Items/Z80/Z80macros.h:171-179`, `zxsp/Source/Uni/Machine/Machine.cpp:1180-1184`).

**UI.** Hex and disassembly inspectors have an "Edit" button and three toggle buttons R (blue), W (green), X (red). Any combination of R/W/X forms `breakpoint_mask`; clicking a byte toggles those bits (`zxsp/Source/Qt/Inspector/MemoryHexInspector.cpp:137-165`, `:939-952`, `:1008-1020`). Bytes with breakpoints are drawn bold with pen color R/G/B mixed from the x/w/r bits, light gray when all three are set (`:250-256`). In the disassembly view, clicking a hex digit toggles that byte, clicking the mnemonic toggles the whole opcode (all bytes) (`zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:1049-1076`).

**Not present:** conditions, hit counts, actions/log points, port (I/O) breakpoints, a breakpoint list window, persistence (flags live only in RAM arrays).

## 4. Execution control

- `stepIn()` runs one CPU cycle budget, which executes exactly one instruction or starts interrupt handling (`zxsp/Source/Uni/Machine/Machine.cpp:1265-1287`).
- `stepOver()` decodes the opcode at PC: for `CALL`, taken conditional `CALL cc` (flags evaluated), and `RST`, it sets `stack_breakpoint = SP` and resumes; the stop happens after the `RET` that restores SP (`:1186-1236`). For `HALT` it runs to the next interrupt; for `ED` block instructions it repeats until PC or the opcode changes or a breakpoint fires (`:1239-1257`).
- `stepOut()` sets `stack_breakpoint = SP + 2` and resumes, so it stops after the next `RET`/`POP` that brings SP to that value (`:1289-1298`; hook `zxsp/Source/Uni/Items/Z80/Z80macros.h:451-458`).
- Z80 inspector: run-to-cycle, clock override, INT/NMI/IE injection (section 2).

## 5. Memory views

- **Common base** `MemoryInspector`: data source and page combos, base-address line edit, register combobox, scrollbar, 20 Hz refresh, settings persisted per inspector type (`zxsp/Source/Qt/Inspector/MemoryInspector.cpp:48-161`). Jumping to a register in ROM/RAM page mode resolves the CPU address to the physical page and switches the combobox; unmapped targets report "Register %s points to unmapped memory" (`:306-363`). Page tables per machine (e.g. Jupiter 1 KiB pages) are in `ramPage()` / `romPage()` (`:486+`).
- **Hex** (`MemoryHexInspector`): three `SimpleTerminal` panes; incremental redraw compares new vs displayed `CoreByte` including breakpoint bits (`zxsp/Source/Qt/Inspector/MemoryHexInspector.cpp:692-776`); scroll reuses rows via `scrollScreen` + `memmove` (`:652-690`). A hover tooltip ("pc -> $xxxx: breakpoints: rwx") exists but is disabled with an early `return` (`:839`).
- **Graphical** (`MemoryGraphInspector`): `QImage::Format_Mono` canvas, 8 pixels per byte, row width 1..128 bytes (for finding sprites and fonts), hovered byte outlined in red/cyan (`zxsp/Source/Qt/Inspector/MemoryGraphInspector.cpp:39-82`).
- **Access** (`MemoryAccessInspector`): see section 7.

## 6. Disassembly

- Two disassembler back-ends over `z80::DisAss`: `AsSeenByCpuDisass` (reads through `cpu->rdPtr`) and `CoreDisass` (a ROM/RAM page array with wrap-around) (`zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:46-76`).
- Backward stepping is heuristic and documented as ambiguous (`:326-374`).
- Inline assembly uses `Z80Assembler::assembleSingleLine` from the bundled zasm; validity is re-checked on every keystroke and shown as green/red pen (`:446-461`, `:1135-1200`). Overwriting a longer instruction pads with `NOP` (`:463-490`).

## 7. Profiling: memory access heat map

- Opening the inspector sets all three `cpu_*_access` bits on every ROM and RAM byte and enables `cpu_access` in `cpu_options` (`zxsp/Source/Qt/Inspector/MemoryAccessInspector.cpp:126-134`).
- The CPU **clears** the bit on access (`pg.both_r(A) -= cpu_r_access`), and only when it is still set, so each byte costs at most one extra write per refresh period (`zxsp/Source/Uni/Items/Z80/Z80macros.h:91`, `:122`, `:206`).
- The GUI (20 Hz) scans memory for cleared bits, ORs a color into a per-byte `QRgb` buffer (read → blue, write → green, execute → red) and re-arms the bits (`zxsp/Source/Qt/Inspector/MemoryAccessInspector.cpp:254-276`). Then it fades: Flash (clear), Decay fast (-5 per channel per tick), Decay slow (-1), Accumulate (`:471-490`, `:297-319`). Red and green are dimmed for balanced brightness (`:327-336`).
- In "As seen by CPU" mode each row is mapped through `cpu->rdPtr` to the ROM or RAM pixel buffer, so the view follows paging (`:503-517`).

## 8. Video, ULA and contention

- The ULA inspector is a read-only dashboard of timing geometry and contention parameters (section 1 rows). It shows the model's waitmap as a bit pattern and the waitmap offset relative to screen start, not per-cycle contention events (`zxsp/Source/Qt/Inspector/UlaInsp.cpp:465-492`).
- There is no beam-position marker, no per-T-state event view and no raster overlay on the screen. The only beam-related interaction is the Z80 inspector's run-to-cycle field.

## 9. Notable and unique ideas

1. **Breakpoints as flag bits in each memory cell** (`zxsp/Source/Uni/Items/Z80/Z80options.h:23-47`): physical-address, bank-aware breakpoints with O(1) per-access cost and no lookup table; the same mechanism carries contention, ROM traps, memory-mapped I/O and access tracking.
2. **Self-clearing access bits for the heat map** (`zxsp/Source/Qt/Inspector/MemoryAccessInspector.cpp:254-276`): the CPU pays one decrement per byte per refresh; the GUI re-arms. R/W/X color mixing plus decay modes make code, data and stack areas visible live.
3. **Run to a given T-state** from the register window (`zxsp/Source/Qt/Inspector/Z80Insp.cpp:292-306`): simple and useful for raster work.
4. **Step over / out via stack-pointer breakpoint** (`zxsp/Source/Uni/Machine/Machine.cpp:1186-1298`): robust against code that manipulates the return address or never returns to PC+3.
5. **Inline assembler with live validity color** in the disassembly view (`zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:1135-1200`).
6. **Physical-page data sources** ("Rom Pages" / "Ram Pages") shared by all memory views, with register jumps resolved through the current mapping (`zxsp/Source/Qt/Inspector/MemoryInspector.cpp:306-363`).
7. **One inspector per hardware item**, switchable in any tool window (`zxsp/Source/Qt/ToolWindow.cpp:266-310`), with editable AY registers and a clickable keyboard.
8. **Contention parameters on screen** (waitmap pattern + offset) in the ULA inspector (`zxsp/Source/Qt/Inspector/UlaInsp.cpp:141-159`).

## 10. Gaps and caveats

- No conditional breakpoints, hit counts, I/O port breakpoints, breakpoint list, symbols/labels, call stack, watches, trace log, history/rewind, scripting or remote debug API.
- Breakpoints are not persisted and are stored in memory arrays; RAM re-allocation (memory config change) would drop them *(inferred)*.
- Clicking an opcode in disassembly sets the mask on all its bytes, but `cpu_break_x` is only checked at the first byte (`GET_INSTR`), so the extra bytes add read breakpoints only if R was selected (`zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:1065-1073`; `zxsp/Source/Uni/Items/Z80/Z80macros.h:164-180`).
- The access inspector never removes `cpu_access` from `cpu_options` after it closes (only `|=` exists, `zxsp/Source/Qt/Inspector/MemoryAccessInspector.cpp:134`), so tracking cost persists for the session.
- The hex tooltip is disabled (`zxsp/Source/Qt/Inspector/MemoryHexInspector.cpp:839`). The ULA inspector's "Restore defaults" button is created but never connected (`zxsp/Source/Qt/Inspector/UlaInsp.cpp:276-277`), and all ULA fields are read-only.
- The disassembly code notes that `disass->pointer()` may be invalid during breakpoint clicks (`zxsp/Source/Qt/Inspector/MemoryDisassInspector.cpp:1058`, `:1069`).
- Contention stripping in `runForSound` applies only to `UlaZxsp`; other models run the whole frame with full options (TODO at `zxsp/Source/Uni/Machine/Machine.cpp:1100`).
