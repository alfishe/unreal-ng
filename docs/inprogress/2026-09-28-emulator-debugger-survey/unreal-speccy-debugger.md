# Unreal Speccy (classic 0.39.0) debugger — capability survey

**Source:** https://github.com/alfishe/unreal-speccy (mirror of SMT / Alone Coder / Deathsoft Unreal Speccy) · local checkout commit `a685b6f` (2020-04-02), `VERS_STRING "0.39.0"` (`unreal-speccy/mods.h:4`) · C++ (MSVC/GCC, Win32 only), DirectX/GDI text-mode monitor drawn into the emulator window, Win32 dialogs for managers. Compared variants: `pentevo/tools/unreal_fix/0.39.0/original` (debugger sources byte-identical to the checkout above) and `pentevo/tools/unreal_fix/0.39.0/nedopc` (NedoPC patch set, differences in §13).
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** One built-in full-screen "monitor" (80x30 character grid rendered with a 8x16 font) that replaces the emulated picture while stopped; a handful of Win32 modal dialogs (breakpoint manager, on-screen watches, labels, GS, cheat search). No remote protocol, no scripting, no history/rewind, no trace log file. Everything is keyboard-driven and every key is rebindable in `unreal.ini` `[SYSTEM.KEYS]`.

Paths below are relative to the surveyed trees: `unreal-speccy/...` for the classic checkout, `pentevo/tools/unreal_fix/0.39.0/nedopc/...` for the NedoPC patch.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Registers window | `af af' sp ir / bc bc' pc t / de de' ix im,iff1,iff2 / hl hl' iy flags`; T-state counter in frame (or `DiHALT` when HALT with DI); values changed since last stop drawn bright | `unreal-speccy/dbgreg.cpp:41-96` |
| CPU & registers | In-place register edit | Cursor over a 26-cell layout graph (`TRegLayout` with left/right/up/down links); type hex to edit 8/16-bit, toggle IFF/flags, cycle IM 0-1-2 | `unreal-speccy/dbgreg.cpp:9-37`, `:102-139` |
| CPU & registers | Direct-edit hotkeys | One key per register/flag (`reg.a`...`reg.CF`) | `unreal-speccy/keydefs.cpp:141-175` |
| CPU & registers | Jump from register | Register value -> disasm cursor (`'`) or memory cursor (`;`) | `unreal-speccy/dbgreg.cpp:163-181` |
| CPU & registers | Multi-CPU | Main Z80 + General Sound Z80 (`MOD_GSZ80`), each with own membits, cursors, breakpoints; `Ctrl+~` cycles | `unreal-speccy/vars.cpp:140-151`, `unreal-speccy/dbgcmd.cpp:254-271` |
| Disassembly | CPU (trace) window | 21 lines `ADDR BYTES MNEMONIC`; PC line highlighted, BPX lines red; title shows `Z80(n)` and last branch source address | `unreal-speccy/dbgtrace.cpp:172-249` |
| Disassembly | Branch preview | For the instruction at PC: computes taken/not-taken for JP/JR/CALL/RET cc/DJNZ/RST/JP (HL); shows target and up/down arrow, marks the target line with a left arrow | `unreal-speccy/dbgtrace.cpp:59-165`, `:208-228` |
| Disassembly | Built-in assembler | Type a mnemonic over the cursor line (`ENTER` or any letter A-Y) -> assembled into memory; column 2 accepts raw hex bytes; column 1 re-targets the view | `unreal-speccy/dbgtrace.cpp:299-354`, `unreal-speccy/z80asm.cpp` |
| Disassembly | Labels in disassembly | `Ctrl+L` toggles label names in operand and address column (max 10 chars) | `unreal-speccy/dbgtrace.cpp:15-49`, `:494-497` |
| Memory views | Hex/ASCII dump | 12 lines, 8 bytes hex + ASCII, or 32-byte text-only (`Alt+D`); nibble-level edit, ASCII edit | `unreal-speccy/dbgmem.cpp:71-178`, `:250-285` |
| Memory views | Alternate address spaces | Same window edits Z80 memory, raw FDD track, logical sectors (CRC auto-fixed), CMOS, NVRAM, ATM/"comp" palette | `unreal-speccy/dbgmem.cpp:24-56`, `unreal-speccy/dbgcmd.cpp:182-189` |
| Memory views | Register-pointer watches | Fixed rows for `PC SP BC DE HL IX IY BC' DE' HL'` + 3 user addresses, 8 bytes each | `unreal-speccy/dbgoth.cpp:33-97` |
| Memory views | Stack window | `-2`, `SP`, `+2`..`+10` words | `unreal-speccy/dbgoth.cpp:99-115` |
| Memory views | Find text / find masked code | Wrap-around search; code search is 4 bytes with 4-byte AND mask | `unreal-speccy/dbgcmd.cpp:18-66` |
| Memory views | Fill / load / save block | Pattern fill (up to 4 bytes), load from host file or TR-DOS sectors, save to host file, TR-DOS file, TR-DOS sectors, or disassembly text file | `unreal-speccy/dbgcmd.cpp:68-109`, `unreal-speccy/dbgrwdlg.cpp:276-386` |
| Memory views | Cheat (memory search) | Snapshot-diff search over all RAM: new / equal value / increased / decreased, byte or word | `unreal-speccy/cheat.cpp:47-172` |
| Navigation & bookmarks | 8 position slots + jump stack | `Ctrl+1..8` save, `1..8` restore (pushes current), 32-deep back stack (`Backspace`) | `unreal-speccy/dbgtrace.cpp:257-269`, `:458-528` |
| Navigation & bookmarks | Follow operand | `'` push+jump to 4-hex operand of cursor instruction; `;` show it in memory window | `unreal-speccy/dbgtrace.cpp:470-492` |
| Navigation & bookmarks | Mouse | Click selects window/row/column; right click in disasm offers "breakpoint" | `unreal-speccy/debug.cpp:83-156` |
| Symbols & labels | Label store | Sorted array of (host pointer into emulated RAM/ROM, name); binary search; bank-aware | `unreal-speccy/dbglabls.cpp:79-97` |
| Symbols & labels | `sos.l`, `user.l` files | `AAAA name` or `PP:AAAA name`; `user.l` auto-reloaded on change | `unreal-speccy/dbglabls.cpp:16-40`, `:99-163`, `unreal-speccy/config.cpp:1003` |
| Symbols & labels | XAS / ALASM import | Scans RAM for XAS7 and ALASM label tables and imports them | `unreal-speccy/dbglabls.cpp:165-329` |
| Symbols & labels | Go-to-label dialog | Type-ahead substring filter over labels visible in the current memory map | `unreal-speccy/dbglabls.cpp:350-462` |
| Breakpoints | Execution (BPX) | Per-address bit in 64K `membits`; `SPACE` toggles; ranges in manager | `unreal-speccy/dbgtrace.cpp:385-389`, `unreal-speccy/debug.cpp:257-261` |
| Breakpoints | Memory read / write | `MEMBITS_BPR` / `MEMBITS_BPW` per address, ranges | `unreal-speccy/z80_main.inl:33-54` |
| Breakpoints | Conditional | Up to 16 expressions, evaluated before every instruction | `unreal-speccy/debug.cpp:283-294`, `unreal-speccy/z80/defs.h:203-205` |
| Breakpoints | Port IN/OUT | Via condition operands `IN`, `OUT`, `VAL` | `unreal-speccy/dbgbpx.cpp:83-87`, `unreal-speccy/io.cpp:22`, `:693`, `:1079` |
| Conditions & expressions | C-like RPN compiler | Operators, register/port operands, `M(x)` / `x->y` memory reads | `unreal-speccy/dbgbpx.cpp:26-268` |
| Watchpoints & watches | 4 expression watches (OSW) | Up to 4 expressions shown as hex in the on-screen LED overlay while running | `unreal-speccy/dbgbpx.cpp:658-703`, `unreal-speccy/leds.cpp:422-426` |
| Watchpoints & watches | Bank-usage watch | ROM (B48/DOS/128/SYS) and RAM pages touched in the last frame | `unreal-speccy/leds.cpp:395-420`, `unreal-speccy/memory.cpp:304-310` |
| Execution control | Step, step over, run to cursor, return | `F7`, `F8`, `F4`, `F11`; step over handles CALL/RST, block ops, HALT | `unreal-speccy/dbgtrace.cpp:538-609`, `:424-432`, `unreal-speccy/dbgcmd.cpp:144-151` |
| Execution control | Set PC, run, break-in | `Z` sets PC=cursor; `ESC` toggles monitor/emulation | `unreal-speccy/dbgtrace.cpp:293-297`, `unreal-speccy/emulkeys.cpp:43-50`, `unreal-speccy/dbgcmd.cpp:112-121` |
| Execution control | Time delta | T-states since the monitor was last left / last step | `unreal-speccy/dbgoth.cpp:13-31`, `unreal-speccy/dbgtrace.cpp:530-536` |
| Tracing & logging | Ripper's tool | Records read/written bytes (`MEMBITS_R/W`), then saves 64K dump with unreferenced bytes replaced by a filler | `unreal-speccy/dbgcmd.cpp:194-241` |
| Tracing & logging | Last branch | `last_branch` = address of the last taken jump/call/ret, shown in disasm title | `unreal-speccy/z80/defs.h:192`, `unreal-speccy/dbgtrace.cpp:245-247` |
| Profiling, heat maps, coverage | Memory band LED | 64K map of R/W/X per frame (bytes-per-pixel 64..512) in the LED overlay | `unreal-speccy/leds.cpp:431-493` |
| Video, raster & beam | Screen / alt screen / ray-painted screen | `F9` shows video memory, `Shift+F9` the other 128K screen, `Alt+F9` the frame painted up to the current beam position (rest cleared) | `unreal-speccy/dbgcmd.cpp:123-142`, `unreal-speccy/draw.cpp:864-895` |
| Video, raster & beam | Screen inset in monitor | `Alt+S` replaces the watch pane by a 37x13-cell crop of screen memory or of the ray-painted frame | `unreal-speccy/dbgoth.cpp:54-86`, `unreal-speccy/dbgpaint.cpp:30-45` |
| Sound & device views | AY registers | 16 AY registers of the active chip, current register highlighted; `Alt+Y` switches chip in TurboSound | `unreal-speccy/dbgoth.cpp:117-141` |
| Sound & device views | Beta 128 (WD1793) | cmd/data, status, sector, head/track, system/DRQ-INTRQ | `unreal-speccy/dbgoth.cpp:242-290` |
| Sound & device views | Ports / pages | `FE`, `7FFD` (highlight when 48K-locked), model extended port (`1FFD`/`DFFD`/`FDFD`/`FF77`/`00`) or CMOS index, `EFF7`; bank names per 16K window, read-only banks colored | `unreal-speccy/dbgoth.cpp:143-240` |
| Sound & device views | General Sound dialog | HLE GS module/sample list, play sample, save sample as PCM, list of unsupported GS commands | `unreal-speccy/dbgoth.cpp:292-517` |
| Import / export & persistence | `bpx.ini` | r/w/x ranges per CPU saved at exit and restored at start | `unreal-speccy/dbgbpx.cpp:705-799`, `unreal-speccy/init.cpp:138`, `:217` |
| UI conveniences | Rebindable hotkeys | Action tables keyed by name, bound from `[SYSTEM.KEYS]`, up to 4-key chords | `unreal-speccy/keydefs.cpp:86-248`, `unreal-speccy/config.cpp:1098-1115` |
| UI conveniences | Key help | `F1` in monitor opens HTML help at anchor `monitor_keys` | `unreal-speccy/emulkeys.cpp:424` |
| Scripting, automation & remote debug | None | No scripting, no GDB stub, no socket API | *(inferred: no such code in the tree)* |
| History / rewind / time travel | None | Only the previous-register snapshot used for change highlighting; quick save slots 1-3 (`qsave1..3.sna`) | `unreal-speccy/debug.cpp:158-162`, `unreal-speccy/keydefs.cpp:112-117` |

## 2. Architecture and performance approach

- **Two CPU cores.** Each frame `spectrum_frame()` picks `z80dbg::z80loop()` (debug core with `DbgMemIf`) when `cpu.dbgchk` is set, else the fast core (`unreal-speccy/mainloop.cpp:29-38`). `dbgchk` is recomputed by `isbrk()` whenever breakpoints change: it is true if any conditional breakpoint exists, or any `membits` byte has `BPR|BPW|BPX` (a linear 64K OR scan), or the memory-band LED is on, or the model is Profi/Scorpion-ProfROM (ROM-read side effects need the debug core) (`unreal-speccy/debug.cpp:304-328`). With no breakpoints the emulator runs with zero debugger overhead.
- **Per-instruction hook.** In the debug core `debug_events()` runs before every `step()` (`unreal-speccy/z80_main.inl:201`, `:218`). It sets `MEMBITS_X` for PC, ORs `MEMBITS_BPX` into `dbgbreak`, checks the stop-here / stop-SP conditions of step-over and exit-sub, evaluates all conditional breakpoints, resets `brk_port_in/out` to `0xFFFFFFFF`, and enters `debug()` if anything fired (`unreal-speccy/debug.cpp:255-300`).
- **Memory access hook.** Debug-core `rm()`/`wm()` set `MEMBITS_R`/`MEMBITS_W` and OR the `BPR`/`BPW` bit into `dbgbreak` (branch-free) (`unreal-speccy/z80_main.inl:28-54`). The break is taken at the next instruction boundary (the access completes first).
- **`membits`** is one byte per 64K logical address per CPU: `MEMBITS_R=0x01, W=0x02, X=0x04, BPR=0x10, BPW=0x20, BPX=0x40` (`unreal-speccy/vars.h:69-70`, `unreal-speccy/vars.cpp:186`; GS CPU has its own, `unreal-speccy/vars.cpp:104`). The same array serves breakpoints, the ripper, and the memory-band LED.
- **Monitor loop.** `debug()` switches the renderer to `RF_MONITOR`, then loops: repaint all panes, `dispatch()` global keys, then the active window's action table (`ac_regs`/`ac_trace`/`ac_mem`) via `dispatch_more()`, then the window's "typing" handler (hex digits start register/memory edit, letters start the assembler) (`unreal-speccy/debug.cpp:165-253`).
- **Previous-state highlighting.** On leaving the monitor (and before each step) the full `TZ80State` is copied to `PrevCpus[]`; `showregs()` brightens any register/flag that differs (`unreal-speccy/debug.cpp:247`, `unreal-speccy/dbgreg.cpp:71-72`, `:91`).

## 3. Screen layout (80x30 text cells)

Coordinates from `unreal-speccy/debug.h:4-38`:

| Pane | Position (x,y) / size | Content |
|---|---|---|
| regs | 1,1 / 32x4 | registers, flags `SZ5H3PNC` (upper = set) |
| Z80(n) trace | 1,6 / 32x21 | disassembly; title shows CPU index and `last_branch` |
| watches | 34,1 / 37x13 | PC..HL' rows + 3 user rows, or screen inset (`Alt+S`) |
| memory | 34,15 / 37x12 | dump; title `memory: CURS gsdma: XXXXXX` or disk position |
| ports | 72,1 / 7x4 | `FE`, `7FFD`, ext port, `EFF7` |
| beta128 | 72,6 / 7x5 | WD1793 state |
| stack | 72,12 / 7x10 | stack words |
| pages | 72,22 / 7x4 | bank name per window (`RAMnn`, `ROMnn`, `BASIC`, `TRDOS`, `B128K`, `SVM`, `CACHE`) |
| time delta | 1,28 / 26x1 | T-states since last run/step |
| AY | 31,28 / 48x1 | 16 AY registers |

## 4. CPU & registers

- Layout table `regs_layout[]` gives offset in `TZ80State`, width (8, 16, 1 = IFF bit, 2 = IM, 30..37 = flag bit index +30), screen x/y and the four neighbor indices, so cursor movement is data-driven (`unreal-speccy/dbgreg.cpp:9-37`, `unreal-speccy/dbgreg.h:3-9`).
- `renter()` edits the register under the cursor: 8/16-bit via `input2/input4` hex fields (a hex key pressed while the regs window is active is re-posted so it becomes the first digit), width 1 toggles, width 2 cycles IM 0->1->2, flags toggle one bit (`unreal-speccy/dbgreg.cpp:102-139`, `:183-191`).
- `R` is shown as `(r_low & 0x7F) + r_hi` and written back so bit 7 is preserved (`unreal-speccy/dbgreg.cpp:63`, `:138`).
- `DiHALT` replaces the T counter when `halted && !iff1` (deadlock indicator) (`unreal-speccy/dbgreg.cpp:53-61`). The main loop also prints `CPU HALTED` / `CPU STOPPED` (for `JR $` or `JP $` with DI) in the status line (`unreal-speccy/mainloop.cpp:44-60`).

## 5. Disassembly window

- `disasm_line()` prints `AAAA` + up to 4 opcode bytes (longer instructions shown as `..` + last 4) or, with labels on, up to 10 chars of the label at that address, then the mnemonic (`unreal-speccy/dbgtrace.cpp:15-49`).
- Cursor has three columns (`cs[3][2] = {0,4},{5,10},{16,16}`): address, bytes, mnemonic; `LEFT/RIGHT` change column (`unreal-speccy/dbgtrace.cpp:170`, `:422-423`). `ENTER` or typing opens an input field in that column: address (re-center; keeps cursor row by walking back with `cpu_up()`), hex bytes (written with `DirectWm`), or assembler source via `assemble_cmd()` (`unreal-speccy/dbgtrace.cpp:299-354`). Typing `A`..`Y` starts assembler input directly (`Z` is set-PC) (`unreal-speccy/dbgtrace.cpp:356-364`).
- Scrolling up uses a heuristic re-sync: disassemble from `ip-16` forward and take the instruction that ends at `ip` (`unreal-speccy/dbgtrace.cpp:271-283`).
- `tracewndflags()` decodes the instruction at PC (skipping DD/FD/ED prefixes) and returns target + flags `TWF_BRANCH`, `TWF_BRADDR` (target from stack/register), `TWF_LOOPCMD` (conditional, taken), `TWF_CALLCMD`, `TWF_BLKCMD` (LDIR/CPIR/INIR/OTIR family), `TWF_HALTCMD` (target = `0x38` or IM2 vector read from `(I<<8)|IntVec()`) (`unreal-speccy/dbgtrace.cpp:51-165`). The PC line shows the resolved target `AAAA` + arrow when it comes from stack/register, and a left arrow is drawn on the target line if it is visible (`unreal-speccy/dbgtrace.cpp:208-228`).

## 6. Memory views

- `editor` selects the address space: `ED_MEM` (Z80 view through current paging, `DirectMem`), `ED_PHYS` (raw MFM track bytes of drive A-D), `ED_LOG` (concatenated sector data of a track; writes recompute the sector CRC via `wd93_crc`), `ED_CMOS`, `ED_NVRAM`, `ED_COMP_PAL` (writes set `temp.comp_pal_changed`) (`unreal-speccy/debug.h:85`, `unreal-speccy/dbgmem.cpp:24-69`). `Ctrl+D` cycles them; `Ctrl+M/V/O` select mem/phys/log directly; `Ctrl+T` prompts drive, track, sector (`unreal-speccy/dbgmem.cpp:355-380`).
- Two layouts: `0000 11 22 33 44 55 66 77 88 abcdefgh` (8 bytes, nibble cursor `mem_second`) or 32-char text dump (`unreal-speccy/dbgmem.cpp:124-143`).
- `find1dlg` (text, up to 8 chars, case sensitive) and `find2dlg` (4-byte code + 4-byte mask, match when `(mem & mask) == (code & mask)`) search forward with wrap in whichever address space is active (`unreal-speccy/dbgcmd.cpp:18-66`).
- Block I/O menus: load from binary file / TR-DOS sectors (TR-DOS file and raw sectors listed but not implemented); save to binary file / TR-DOS file (adds a catalog entry) / TR-DOS sectors (256-byte sectors, track+sector auto-increment) / "as Z80 disassembly" (text file of `disasm_line` output) (`unreal-speccy/dbgrwdlg.cpp:139-386`).
- Cheat search (`Alt+F6`, both in emulation and monitor) keeps a RAM snapshot and a 1-bit-per-byte candidate mask; results (<=100) list `Address` (mapped as C000/8000/4000 by page), `Page`, `Offset`, `Value` (`unreal-speccy/cheat.cpp:13-45`, `:174-222`).

## 7. Navigation & bookmarks

- 8 slots `save_pos/save_cur` (top line + cursor) and a 32-entry jump stack; `crest(n)` pushes before jumping; `pop_pos()` pops (`unreal-speccy/dbgtrace.cpp:257-269`, `:458-528`).
- `cjump()` / `cdjump()` pick the last 4-hex-digit group in the cursor instruction's mnemonic text as the operand address (text scraping, so `LD HL,(1234)` and `JP 1234` both work; 2-digit operands do not) (`unreal-speccy/dbgtrace.cpp:470-492`).
- `cgoto` (`G`; the manual says `Ctrl+G`, the ini binds `cpu.goto=G`), `cfindpc` (`HOME`), `mgoto` (`Ctrl+G`), memory jumps to PC/SP/BC/DE/HL/IX/IY (`unreal-speccy/dbgmem.cpp:301-353`).

## 8. Symbols & labels

- `MON_LABELS` stores `(unsigned char *address, name_offs)` pairs, where `address` is a **host pointer into emulated RAM/ROM** (`RAM_BASE_M + page*PAGE + offset`), sorted by pointer; lookup is binary search (`unreal-speccy/dbglabls.h:2-52`, `unreal-speccy/dbglabls.cpp:88-97`). Display resolves the current mapping (`am_r(addr)`), so labels are automatically bank-correct (`unreal-speccy/dbgtrace.cpp:26`).
- File grammar, one label per line, max 63 chars per line (`unreal-speccy/dbglabls.cpp:99-163`):
  - `AAAA name` -> offset `AAAA` from the base the file is loaded against (ROM for `sos.l`, RAM page 0 for `user.l`, so effectively a linear RAM offset);
  - `PP:AAAA name` -> RAM page `PP`, offset `AAAA & 0x3FFF`.
  Errors are printed to the console with line number.
- `sos.l` is loaded for the BASIC48 ROM at every config apply (`unreal-speccy/config.cpp:1003`). `user.l` is loaded lazily and re-imported when a directory change notification fires (checked in the monitor loop only while labels are shown) (`unreal-speccy/dbglabls.cpp:16-40`, `unreal-speccy/debug.cpp:185-186`).
- `Ctrl+A` (in CPU window) scans memory: XAS7 tables in bank 6 (or `#46` on Pentagon 512+), ALASM 4.42-5.0x tables anywhere in RAM (validated chain of `len|flags, value, reversed name` records, up to 16 tables), offered in a menu (`unreal-speccy/dbglabls.cpp:165-329`).
- `Ctrl+J` dialog lists labels in the 4 currently mapped windows as `AAAA name`; typing `0-9 A-Z _` filters by case-insensitive substring (beeps on no match), `Backspace` deletes; `Enter`/double click pushes position and jumps (`unreal-speccy/dbglabls.cpp:350-462`).

## 9. Breakpoints, conditions and watches

### 9.1 Breakpoint kinds and keying

| Kind | Storage | Keying | Fires |
|---|---|---|---|
| Execution | `membits[addr] & BPX` | 16-bit **logical** address, not bank-aware | before executing the instruction at PC (`unreal-speccy/debug.cpp:257-261`) |
| Memory read / write | `membits[addr] & BPR/BPW` | 16-bit logical address, ranges | after the access, at the next instruction boundary (`unreal-speccy/z80_main.inl:33-54`) |
| Conditional | `cpu.cbp[16][128]` compiled scripts, `cbpn` count | expression | before each instruction (`unreal-speccy/debug.cpp:283-294`) |
| Port | `IN`/`OUT`/`VAL` operands in a condition | 16-bit port | the condition is evaluated at the boundary after the IN/OUT instruction (values reset after each check) (`unreal-speccy/debug.cpp:296`, `unreal-speccy/io.cpp:22`, `:693`) |
| Step helpers | `dbg_stophere`, `dbg_stopsp`, `dbg_loop_r1/r2` | PC / SP | run-to-cursor, step-over, exit-sub (`unreal-speccy/debug.cpp:263-281`) |

There are no hit counts, no enable/disable flags per breakpoint, no actions (log-and-continue); a hit always stops. Bank awareness must be expressed in a condition (`FD` operand = `#7FFD`).

### 9.2 Breakpoint manager (`Alt+C`)

Win32 dialog with three lists (`unreal-speccy/dbgbpx.cpp:505-656`): conditional expressions (max 16, `MAX_CBP`), execution ranges (`XXXX` or `XXXX-YYYY`, hex), memory ranges with `R`/`W` check boxes (list shows `XXXX-YYYY RW`). Double-click moves an entry back into the edit box and deletes it (edit-in-place). Conditions are round-tripped to text by `script2text()` (fully parenthesized) (`unreal-speccy/dbgbpx.cpp:270-328`).

### 9.3 Expression language

Compiled by `toscript()` (shunting-yard) into a token array; evaluated by `calc()` on a 64-entry `unsigned` stack (`unreal-speccy/dbgbpx.cpp:26-268`). All arithmetic is 32-bit unsigned. Input is upper-cased except the char after a quote.

Operators, from `prio[]` (lower number binds tighter) (`unreal-speccy/dbgbpx.cpp:131-157`):

| Priority | Operators |
|---|---|
| 1 (unary) | `!` `~` `M` (memory byte: `M(x)`) and binary `->` (`a->b` = byte at `a+b`) |
| 2 | `*` `%` `/` (division/modulo by zero leave the left operand unchanged) |
| 3 | `+` `-` |
| 4 | `>>` `<<` |
| 5 | `>` `<` `=` `>=` `<=` `==` `!=` (`=` is an alias of `==`) |
| 6 | `&` |
| 7 | `^` |
| 8 | `\|` |
| 9 | `&&` |
| 10 | `\|\|` |

Operands (`DECL_REGS`, `unreal-speccy/dbgbpx.cpp:74-122`):
- 8-bit: `A F B C D E H L I R`, `A' F' B' C' D' E' H' L'`, `VAL` (last port byte read/written), `FD` (`comp.p7FFD`);
- 16-bit: `AF BC DE HL PC SP IX IY AF' BC' DE' HL'`;
- 32-bit: `IN`, `OUT` (last port accessed by the instruction, `0xFFFFFFFF` if none);
- function: `DOS` (1 when TR-DOS ports are active, `CF_DOSPORTS`);
- numbers: **hex only, must start with a digit** (`0DFFD`, not `DFFD`); character literals `'A'`.
Operands are stored as pointers to live variables (`DB_PCHAR/PSHORT/PINT/PFUNC`), so evaluation is a pointer dereference, no lookup.

Examples from the manual (`unreal-speccy/doc/unreal_e.txt:444-481`): `(out+1) | (in+1)` any port access; `(in & 8001) == 0` keyboard half-row B..SPACE; `!(out & 1)` any OUT to #FE; `(out & 0FF)==0FD && (val&7)==3` page 3 selected; `M(pc)==0CB && pc->1 >= 10 && pc->1 <= 17` break on `RL r`. Precedence trap called out by the manual: `out & 0FF == 0FE` parses as `out & (0FF == 0FE)`.

### 9.4 Watches

- **On-screen watches (`Alt+O`)**: 4 expressions (same language) with enable boxes, plus "trace RAM/ROM" checkboxes; evaluated once per frame and drawn in the OSW LED overlay while the emulator runs, together with per-frame ROM/RAM bank usage (`*`/`-` per page) (`unreal-speccy/dbgbpx.cpp:658-703`, `unreal-speccy/leds.cpp:395-427`).
- **Monitor watch pane**: fixed register-pointer rows plus 3 user addresses set with `Ctrl+U` (`unreal-speccy/dbgoth.cpp:54-97`).

## 10. Execution control

| Action | Key (default ini) | Mechanism |
|---|---|---|
| Enter monitor | `ESC` (emulation) | `main_debug()` sets `dbgchk=dbgbreak=1` (`unreal-speccy/emulkeys.cpp:43-50`) |
| Continue | `ESC` (`mon.emul`) | recompute `dbgchk` per CPU, clear breaks (`unreal-speccy/dbgcmd.cpp:112-121`) |
| Step | `F7` | `cpu.Step()` then IRQ acceptance check, `CheckNextFrame()` (`unreal-speccy/dbgtrace.cpp:538-557`) |
| Step over | `F8` | CALL/RST: stop at next PC with same SP; block op/HALT: stop at next PC / ISR entry; otherwise single step (loop skipping code is commented out) (`unreal-speccy/dbgtrace.cpp:559-609`) |
| Run to cursor | `F4` | `dbg_stophere = trace_curs` (`unreal-speccy/dbgtrace.cpp:424-432`) |
| Run to return | `F11` | `dbg_stophere = word at (SP)` (`unreal-speccy/dbgcmd.cpp:144-151`) |
| Set PC | `Z` | `pc = trace_curs` (`unreal-speccy/dbgtrace.cpp:293-297`) |
| Switch CPU | `Ctrl+~` | `CpuMgr.SwitchCpu()` (`unreal-speccy/dbgcmd.cpp:254-271`) |
| Write #7FFD / ext port | `Alt+B` / `Alt+M` | `editbank()`, `editextbank()` via `out()` (`unreal-speccy/dbgcmd.cpp:153-170`) |

Step-over SP check also stops when PC lands within 256 bytes after the call site with the same SP (catches returns to a slightly different address) (`unreal-speccy/debug.cpp:269-275`).

## 11. Tracing, profiling and coverage

- **Ripper (`Alt+T`)**: first press asks "trace reads", "trace writes", filler byte (default `#CF`) and clears R/W bits; while running, the debug core marks accessed bytes; second press saves a 64K `.bin` with unreferenced bytes replaced by the filler (`unreal-speccy/dbgcmd.cpp:194-241`). Ripped code+data from a game in one step.
- **Memory band LED** (`[LEDS] MemBand`, `BandBpp=64/128/256/512`): per frame draws the 64K space as rows of 128 pixels, each pixel OR-ing R/W/X bits of `BandBpp` bytes into three colored stripes; bits are cleared after drawing unless the ripper holds them (`unreal-speccy/leds.cpp:431-493`, `unreal-speccy/config.cpp:524-525`). A live coverage/heat strip.
- **`last_branch`**: set by every taken jump/call/ret/rst opcode handler (`unreal-speccy/z80/op_noprefix.cpp:104` ff., `unreal-speccy/z80/op_ed.cpp:57`), shown next to `Z80(n)`. One-deep branch trace.
- No instruction trace log, no port log file, no cycle profiler.

## 12. Video, raster and beam tools

- `mon.screen` (`F9`): draw the screen from video memory, full window, wait for a key; `mon.altscreen` (`Shift+F9`): same with `#7FFD` bit 3 flipped temporarily; `mon.rayscreen` (`Alt+F9`): `update_screen()` up to the current T-state then `clear_until_ray()` fills the rest of the frame buffer, i.e. shows exactly what the beam has painted so far including border and multicolor effects (`unreal-speccy/dbgcmd.cpp:123-142`, `unreal-speccy/draw.cpp:864-895`).
- `Alt+S` cycles the watch pane: watches -> "screen memory" inset -> "ray-painted" inset (37x13 cells of the frame centered on the paper) (`unreal-speccy/dbgoth.cpp:54-86`, `unreal-speccy/dbgpaint.cpp:30-45`).
- No explicit beam-position readout; the T counter in the regs pane is the only raster coordinate.

## 13. Differences in the pentevo 0.39.0 variants

- `original/`: all debugger files (`dbg*.cpp`, `debug.*`, `keydefs.cpp`) identical to the classic checkout.
- `nedopc/` adds:
  - **Global breakpoint switch**: `bkpts_ena` (ini `[DEBUG] EnaBkpts=1`), toggled by `main.breakpoints=ALT Q` "toggle breakpoints outside monitor". When off, `debug_events()` swallows any break unless the user explicitly requested the monitor (`user_mon_req` set by `main_debug`), and `isbrk()` returns 0 so the fast core runs (`pentevo/tools/unreal_fix/0.39.0/nedopc/debug.cpp:302-321`, `pentevo/tools/unreal_fix/0.39.0/nedopc/emulkeys.cpp:49`, `:526-529`, `pentevo/tools/unreal_fix/0.39.0/nedopc/config.cpp:570`, `pentevo/tools/unreal_fix/0.39.0/nedopc/x32/emul.ini:628-629`, `:760`).
  - **Virtual (Z80-address) labels**: label file lines `:AAAA name` are stored with a fake pointer `NULL+AAAA` and looked up by logical address before the physical one, i.e. labels that follow the address regardless of paging (`pentevo/tools/unreal_fix/0.39.0/nedopc/dbglabls.cpp:122-175`, `pentevo/tools/unreal_fix/0.39.0/nedopc/dbgtrace.cpp:26-29`).
  - Portability fixes only elsewhere (`%lld` time delta on non-MSVC, byteswap casts) (`pentevo/tools/unreal_fix/0.39.0/nedopc/dbgoth.cpp:27-31`, `pentevo/tools/unreal_fix/0.39.0/nedopc/dbgcmd.cpp:45-51`).

## 14. Hotkey reference (default `unreal-speccy/x32/unreal.ini` `[SYSTEM.KEYS]`, lines 684-889)

Format `action=key1 [key2 [key3 [key4]]]`, all listed keys must be down (chord). Tables are searched in order and the **first** matching entry wins (`unreal-speccy/util.cpp:208-251`).

**Emulation:** `main.monitor=ESC`, `main.memsearch=ALT F6`, `main.pokedialog=F6`, `main.help=F1`.

**All monitor windows (`mon.*`):**

| Key | Action | Key | Action |
|---|---|---|---|
| `ESC` | emul (run) | `F7` | step |
| `F8` | step over | `F11` | exit sub (run until word at SP) |
| `F9` | show screen | `Shift+F9` | show alt screen |
| `Alt+F9` | ray-painted screen | `Alt+S` | watches / screen / ray inset |
| `Alt+C` | breakpoint manager | `Alt+O` | on-screen watches |
| `Ctrl+U` | set 3 user watch addresses | `Ctrl+J` | go to label |
| `Alt+R` | load block | `Alt+W` | save block (incl. disasm to file) |
| `Alt+F` | fill block | `Alt+T` | ripper start/save |
| `Alt+B` | write #7FFD | `Alt+M` | write model ext port |
| `Alt+D` | 8-byte hex / 32-byte text dump | `Ctrl+D` | cycle mem/phys/log/cmos/nvram/comppal |
| `TAB` / `Shift+TAB` | next / prev window | `Ctrl+~` | next CPU |
| `Alt+Y` | switch AY | `Alt+G` | GS dialog |
| `Alt+P` | pokes | `Alt+F6` | cheat search |
| `F1` | help | `F2/F3/F5`, `Alt/Ctrl/Shift+F2/F3` | save/load/sound, quick save/load |
| `F12` + modifiers, `F11` + modifiers | resets / NMIs | `Alt+F1` | settings |

**Registers window (`reg.*`):** arrows move, `ENTER` edit; `Ctrl+A` A, `Ctrl+F` F, `Ctrl+B` BC, `Ctrl+D` DE, `H` HL, `P` PC, `S` SP, `X` IX, `Y` IY, `I`, `R`, `M` IM, `Ctrl+1` IFF1, `Ctrl+2` IFF2; `Alt+S/Z/5/H/3/P/N/C` toggle SF/ZF/F5/HF/F3/PV/NF/CF; `'` code jump, `;` data jump; typing `0-9A-F` edits.

**CPU window (`cpu.*`):** `HOME` find PC, `F4` run to cursor, `Alt+F7` find text, `Ctrl+F7` find code with mask, `G` goto, `SPACE` toggle BPX, `ENTER` edit, `Z` set PC, arrows/`PGUP`/`PGDN` move, `Ctrl+1..8` save slot, `1..8` restore slot, `Backspace` back, `'` follow operand, `;` operand in dump, `Ctrl+L` labels on/off, `Ctrl+A` import XAS/ALASM labels; typing `A..Y` starts assembling.

**Memory window (`mem.*`):** arrows/`PGUP`/`PGDN`, `HOME`/`END` line start/end, `Ctrl+TAB` hex/ASCII, `Alt+F7` find text, `Ctrl+F7` find code, `Ctrl+G` goto, `Ctrl+M` memory, `Ctrl+V` physical track, `Ctrl+O` logical sectors, `Ctrl+T` choose drive/track/sector, `Ctrl+P/S/B/D/H/X/Y` go to PC/SP/BC/DE/HL/IX/IY; other keys edit.

## 15. Persistence formats

- `bpx.ini` (emulator directory), written at exit, read at start; one line per contiguous range: `<type><cpu>=0xSTART[-0xEND]`, type `r`/`w`/`x`, cpu `0` (main) or `1` (GS), e.g. `x0=0x8000`, `w0=0x5B00-0x5BFF` (`unreal-speccy/dbgbpx.cpp:705-789`). Conditional breakpoints, watches, bookmarks and label toggles are **not** persisted.
- `sos.l` / `user.l` label files (§8). No symbol export.

## 16. Notable and unique ideas

1. **Pointer-compiled condition language with port and memory operands** — `IN/OUT/VAL/FD/DOS` plus `M()`/`->` make port-level and paging-aware breakpoints one-liners; compile-to-RPN with live pointers keeps per-instruction evaluation cheap.
2. **Two-core switch driven by `isbrk()`** — zero debugger cost when no breakpoints exist, full per-access checks otherwise. A good model for unreal-ng's fast/debug paths.
3. **`membits` byte map reused for three tools** (breakpoints, ripper, memory-band coverage LED) — one cheap per-access OR feeds breakpoints, code/data ripping and a live R/W/X heat strip.
4. **Ray-painted screen (`Alt+F9`) and screen inset** — shows exactly what the beam has drawn at the stop point, including border/multicolor; a beam debugger in one keystroke.
5. **Ripper's tool** — code+data extraction by access tracing with a filler for untouched bytes.
6. **Bank-correct labels by host pointer** plus XAS/ALASM in-memory label import and live `user.l` reload; NedoPC's `:AAAA` virtual labels complement it for logical addresses.
7. **Branch preview with resolved RET/JP (HL) targets and HALT -> ISR target** in the disasm pane; step-over over HALT lands in the interrupt handler.
8. **Disk editor inside the memory pane** (physical track / logical sectors with CRC fix-up) and TR-DOS sector/file block I/O.
9. **Changed-register highlighting and `DiHALT`/`CPU STOPPED` deadlock hints.**
10. **Fully rebindable chord hotkeys per window**, named actions (`cpu.bpx`, `mem.goto`...) — a naming scheme unreal-ng can reuse for its own key map.

## 17. Gaps and caveats

- Execution/memory breakpoints are keyed by 16-bit logical address only; no page/bank qualifier outside conditional expressions. No hit counts, no temporary/one-shot breakpoints other than run-to-cursor, no per-breakpoint enable.
- Conditional breakpoints are evaluated before every instruction with a 64-entry stack and no overflow check; `calcerr` only detects an unbalanced stack (`unreal-speccy/dbgbpx.cpp:28-66`). Hex numbers that start with a letter are rejected, not reported clearly ("Error in expression / Please do RTFM").
- `isbrk()` scans all 64K `membits` every time a breakpoint changes (`unreal-speccy/debug.cpp:322-324`).
- Watch dialog initializes the ROM checkbox from `trace_ram` instead of `trace_rom` (`unreal-speccy/dbgbpx.cpp:671`).
- `mon_load()` menu declares 3 items while 4 are defined ("from raw sectors" never shown); "from TR-DOS file" says "not implemented"; save "to raw sectors" is disabled (`unreal-speccy/dbgrwdlg.cpp:278-325`, `:334`).
- `unreal.ini` binds `mon.setrange=F6` / `mon.resetrange=SHIFT F6`, but no such actions exist in `ac_mon`; they are silently ignored (`unreal-speccy/x32/unreal.ini:767-768`, `unreal-speccy/keydefs.cpp:88-138`). The manual's `Alt+U` "disasm to file" has no binding either; the feature lives in the `Alt+W` save menu.
- First-match chord dispatch plus table order can shadow bindings *(inferred)*: in the registers window `reg.hl=H` precedes `reg.HF=ALT H`, `reg.sp=S` precedes `reg.SF=ALT S`, `reg.pc=P` precedes `reg.PF=ALT P`, so the `Alt` flag toggles are reachable only by cursor; `reg.CF=ALT C` shadows `mon.bpdialog=ALT C` there (`unreal-speccy/keydefs.cpp:141-175`, `unreal-speccy/util.cpp:217-247`).
- Right-click menu in non-disasm panes contains placeholder text ("I don't know what to place to menu") (`unreal-speccy/debug.cpp:141-146`).
- Operand following scrapes the mnemonic text for the last 4 hex digits; no real operand model (`unreal-speccy/dbgtrace.cpp:470-492`).
- Step-over "loop skipping" (JR/JP cc backwards) is commented out (`unreal-speccy/dbgtrace.cpp:577-597`).
- Win32/DirectX only; all dialogs modal; monitor replaces the game picture (no side-by-side view).
