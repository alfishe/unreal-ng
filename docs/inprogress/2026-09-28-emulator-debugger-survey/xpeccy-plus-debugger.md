# xpeccy-plus debugger — capability survey

**Source:** https://github.com/dotkoval/xpeccy-plus · local checkout commit `7a96d8da` · C (core `libxpeccy`) + C++ / Qt (GUI)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** GUI-only debugger: one `QMainWindow` (`DebugWin`) of dock widgets (CPU, Disasm, DUMP, REG-DUMP, FDD disk dump, CMOS, Screen, Sound Chip, Tape, FDC, Breakpoints, Heat map, Palette, MEMMAP, STACK) plus satellite windows (Watcher, Memory finder/filler, Sprite scanner, Labels list). Breakpoints with a C-like condition language, event-log output of hits, a pipe-separated trace log file, a heat map, and a rewind feature that is not connected to the debugger. No console monitor, no scripting, no remote debug protocol. Only a Z80 core is built in.

Conventions: every path is relative to the emulator root and prefixed `xpeccy-plus/`. *(inferred)* marks a conclusion drawn from reading the code, not something the code states.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | CPU panel (table-driven) | Built from the core's `xRegDsc` table; rebuilt when `cpu->core` changes; flag names come from the `REG_EOT` terminator | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1890-1899`; `xpeccy-plus/src/libxpeccy/cpu/cpu.c:483-507`, `:505` |
| CPU & registers | Z80 registers | PC, SP, AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, IR (combined), IM, IFF1, IFF2; WZ/MEMPTR only in CMake Debug builds | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:494-542`, `:535-537`; `xpeccy-plus/CMakeLists.txt:63,68` |
| CPU & registers | Register editing | Every register is an `xHexSpin`, committed per keystroke; IFF1/IFF2 are checkboxes; Up/Down ±1, PgUp/PgDn ±0x100, X toggles hex/decimal | `xpeccy-plus/src/xgui/debuga/debuger.cpp:525-540`, `:1985-2008`; `xpeccy-plus/src/xgui/classes.cpp:346-385` |
| CPU & registers | Flag checkboxes | 8 checkboxes `SZ5H3PNC` (undocumented 5 and 3 included), editable | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:541`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:1335-1352`, `:1974-1983` |
| CPU & registers | Changed-value highlight | Background highlight of changed registers; "Split pairs" lights only the changed byte | `xpeccy-plus/src/xgui/classes.cpp:264-271`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:1412`, `:585-587` |
| CPU & registers | Register click navigation | Left click on a register name moves the disassembly, right click moves the dump | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1279-1300` |
| CPU & registers | CPU panel layouts | Auto / 1 column / 2 columns / Wide (4 columns grouped by `REG_GRP_*`) | `xpeccy-plus/src/xgui/debuga/debuger.cpp:566-592`; `xpeccy-plus/src/xcore/xcore.h:792-795` |
| CPU & registers | REG-DUMP dock | Bytes at every `REG_RDMP` register (PC, BC, DE, HL, SP, BC', DE', HL', IX, IY, WZ in debug builds), read-only | `xpeccy-plus/src/xgui/debuga/dbg_rdump.cpp:30-125`; `xpeccy-plus/ui/dbgwidgets/form_regdump.ui:37-39` |
| CPU & registers | T-state counter | `"<ticks since last stop> / <T since last INT>"`, "Accumulate T" checkbox | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1197`, `:328-329`, `:350-356`, `:938-939`; `xpeccy-plus/ui/dbgwidgets/form_disasm.ui:179-201` |
| CPU & registers | Plugin CPU cores | Other cores can be `dlopen`ed from a library exporting `getCore`; the panel is generic over register tables | `xpeccy-plus/src/libxpeccy/cpu/cpu.c:98-160`; `xpeccy-plus/src/xcore/machines.cpp:935-938` |
| Disassembly | 4-column listing | Address/label/comment, bytes, mnemonic with label substitution, info (operand value or branch arrow) | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:84-86`, `:201-204` |
| Disassembly | Row kinds | Comment rows, label rows, instruction rows, `LABEL: EQU $-n` rows, block separators after RET/JP/JR/DJNZ | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:427-537`, `:34-74` |
| Disassembly | Show segment | Address column as `RAM:pp:oooo` / `ROM:` / `SLT:` / `EXT:` | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:496-507` |
| Disassembly | PC-row operand value | Memory operand at PC (`(HL)`, `(IX+d)`, `(nn)`...; tested bit for BIT/RES/SET) shown in the info column | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:379-405`; `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:253-283` |
| Disassembly | Taken-branch arrows | Conditional branch at PC evaluated against live flags; arrow down/up/left on the PC row, left arrow on the target row | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:393-402`, `:563-578`; `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:285-321` |
| Disassembly | Label substitution | `#nnnn` replaced by a label, `LABEL + n` up to 7 bytes back for memory operands, drawn bold | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:276-309`, `:922-925` |
| Disassembly | Per-byte "view as" types | Code, Opcode (EXEC), Byte (`DB`), Word (`DW`), Address (`DW label`), ASCII (`DB "..."`) in the high nibble of the flag map | `xpeccy-plus/src/libxpeccy/spectrum.h:58-63`; `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:311-377` |
| Disassembly | In-place editing / assembler | Column 0: labels, comments, go-to; column 1: hex bytes; column 2: `db`/`dw` directives or Z80 assembly | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:733-873` |
| Disassembly | Copy / disasm to file | Ctrl+C copies block as source with `ORG`; "Disasm to file" writes block or whole bus | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1078-1110`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:2123-2163` |
| Disassembly | Dim address / opcodes, syntax coloring | Dim color = average of text and background; constants colored, labels bold | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:92-105`, `:154-164`, `:884-946` |
| Disassembly | Page-aware PC highlight | PC row matched by physical cell, follows paging | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:446-469` |
| Disassembly | RAM/ROM page mode (dead) | `XVIEW_RAM`/`XVIEW_ROM` exists but `setMode` is never called | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:18-19`, `:997-1001` |
| Memory views | DUMP dock | CPU / RAM page / ROM page, 4..16 bytes per row, text column in WIN1251/CP866/KOI8R, hex byte editing | `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:16-37`, `:354-411`, `:604-632` |
| Memory views | CMOS dump | 256 bytes of `cmos.data[]`, editable | `xpeccy-plus/src/xgui/debuga/dbg_cmos_dump.cpp:8-63` |
| Memory views | Stack view | Words around SP with signed offsets, ±16-byte window shift, read-only | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2212-2245`, `:474-489`; `xpeccy-plus/src/xgui/debuga/dbg_stack.cpp:17-22` |
| Memory views | Memory finder | Up to 8 bytes with per-byte mask, text mirror; CPU address space only | `xpeccy-plus/src/xgui/debuga/dbg_finder.cpp:17-91`; `xpeccy-plus/ui/dbgfinder.ui:36-65` |
| Memory views | Memory filler | Range + 8-byte pattern + mask; Mask / Put / Or / And / Xor | `xpeccy-plus/src/xgui/debuga/dbg_memfill.cpp:12-91`; `xpeccy-plus/ui/filler.ui:23-146` |
| Memory views | Save RAM dump | Binary, Hobeta `.$C`, or file on a TR-DOS disk in drive A-D | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2496-2593` |
| Memory views | Open RAM dump | Raw binary 1..0xFF00 bytes at a start address through the CPU map | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2644-2702` |
| Navigation & bookmarks | History back (F5) | List of (view anchor, cursor) pairs, back only, unbounded | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1116-1120`, `:1262-1271` |
| Navigation & bookmarks | 5 marked addresses | Ctrl+1..5 store, Alt+1..5 jump (hard-coded) | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1139-1147`, `:1287-1305` |
| Navigation & bookmarks | Go to PC / Set PC / Go to address | Home / End / G; address syntax `.REG`, `seg:off`, `#hex`, `0xhex`, label, plain hex | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1195-1211`, `:1272-1281`, `:623-666` |
| Navigation & bookmarks | Follow operand (F4) | Jumps to the operand/branch target; on RET follows the return address peeked from (SP) | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1253-1261`; `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:303-311` |
| Navigation & bookmarks | Dump register jumps | Ctrl+P/S/B/D/H/X/Y put the dump at PC/SP/BC/DE/HL/IX/IY | `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:426-432`, `:470-476` |
| Navigation & bookmarks | Block selection | Ctrl+click start, Shift+click end, drag, middle click clears; shared between disasm and dump | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:15-16`, `:1313-1350`; `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:9-10` |
| Symbols & labels | Label sets | Named sets of `name -> xAdr`; one current set; reverse map keyed by (memory type, physical address) | `xpeccy-plus/src/xcore/labels.cpp:10-83`, `:31-42`, `:127-138` |
| Symbols & labels | Comments | Global `commap[type][abs]`, shown as `; text` rows | `xpeccy-plus/src/xcore/labels.cpp:254-278`; `xpeccy-plus/src/xcore/xcore.h:609` |
| Symbols & labels | Label file load/save | `BB:AAAA NAME` text (sjasmplus/Unreal style *(inferred)*); CLI `-l/--labels` | `xpeccy-plus/src/xcore/labels.cpp:152-252`; `xpeccy-plus/src/main.cpp:481-483` |
| Symbols & labels | `.xmap` project file | Binary chunks: RAM/ROM/slot flag maps, all label sets, comments; CLI `--xmap` | `xpeccy-plus/src/xcore/xmap.cpp:36-240`; `xpeccy-plus/src/main.cpp:518-520` |
| Symbols & labels | Labels list window | Substring filter, set management, CPU or physical address display, double-click jump | `xpeccy-plus/src/xgui/labelist.cpp:19-156` |
| Breakpoints | CPU-address breakpoint | 16-bit bus address range, F/R/W | `xpeccy-plus/src/xcore/breakpoints.cpp:104-117`; `xpeccy-plus/src/libxpeccy/defines.h:82-94` |
| Breakpoints | Physical RAM / ROM / slot cell breakpoint | Absolute physical offset range, F/R/W (Space in disasm by default) | `xpeccy-plus/src/xcore/breakpoints.cpp:85-91`, `:305-316`; `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1225-1235` |
| Breakpoints | I/O port breakpoint | Port + mask, R = IN, W = OUT | `xpeccy-plus/src/xcore/breakpoints.cpp:286-292` |
| Breakpoints | IRQ breakpoint | Fires when an interrupt request is pending (one per list) | `xpeccy-plus/src/libxpeccy/spectrum.c:918-923` |
| Breakpoints | Global condition breakpoint | Address-less condition evaluated after every instruction; level or edge ("On change") | `xpeccy-plus/src/xcore/breakpoints.cpp:170-189` |
| Breakpoints | Temporary breakpoints | One-shot fetch bit in the physical page map for Step over and Run to cursor | `xpeccy-plus/src/libxpeccy/spectrum.c:982-998`, `:912-915` |
| Breakpoints | Breakpoint list dock | On/F/R/W checkboxes, Addr, Cond, Cnt; sortable; Add/Edit/Open/Save/Delete; Reset counter | `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:66-144`, `:633-692` |
| Breakpoints | Breakpoint editor dialog | Type, flags, bank/offset/absolute range, IO mask, action, log, condition with live validation and help | `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:288-617`; `xpeccy-plus/ui/brkmanager.ui` |
| Breakpoints | Context-menu breakpoints | Fetch/Read/Write on a disasm or dump cell or block | `xpeccy-plus/src/xgui/debuga/debuger.cpp:821-827`, `:2406-2445`, `:2369-2387`; `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:670-701` |
| Conditions & expressions | Expression language | C-like, compiled to RPN; registers, labels, pseudo-variables (RD, WR, MDT, IN, OUT, VAL, DOS, SLOT0..3, FRAME, RAYX, RAYY, HITS), `M(x)`, `[x]`, `a->b`, `RAY(x,y)` | `xpeccy-plus/src/xcore/xexpr.cpp:15-30`, `:94-122`, `:197-333`, `:513-648`; `xpeccy-plus/src/xcore/xexpr.h:11-49` |
| Conditions & expressions | Live validation | `ok: <bracketed hex form>` or `error at pos` as you type; OK refused on error | `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:465-489`, `:574-577` |
| Conditions & expressions | Syntax help | `res/help/cond-syntax.html` in a dialog | `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:491-512`; `xpeccy-plus/res/help/cond-syntax.html` |
| Conditions & expressions | Hit / pass counters | `hits` (before condition) and `count` (condition passed); `HITS` usable in conditions | `xpeccy-plus/src/ethread.cpp:510-514`; `xpeccy-plus/src/xcore/xcore.h:232-233` |
| Conditions & expressions | Actions | Debugger, Counter, Screen dump (.scr); orthogonal Log flag | `xpeccy-plus/src/xcore/xcore.h:212-216`; `xpeccy-plus/src/ethread.cpp:302-332` |
| Watchpoints & watches | Watcher window | Registers, mapped banks, expression watches with 11-byte dumps; refreshes every frame while running | `xpeccy-plus/src/watcher.cpp:34-279`; `xpeccy-plus/src/emulwin.cpp:1199-1200` |
| Watchpoints & watches | Watched ports | Up to 16 port/mask entries, last value on the bus or machine register copy; persisted in config | `xpeccy-plus/src/libxpeccy/spectrum.h:100`; `xpeccy-plus/src/libxpeccy/spectrum.c:280-356`; `xpeccy-plus/src/xcore/config.cpp:278-280` |
| Watchpoints & watches | Data watchpoints | R/W breakpoints plus conditions on RD/WR/MDT/IN/OUT/VAL (no separate watchpoint type) | `xpeccy-plus/src/libxpeccy/spectrum.c:121-191` |
| Execution control | Step into / over / out | F7 / F8 (temp bp after CALL/RST/DJNZ/HALT/block ops) / F6 (call-depth counter, Z80 only) | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1024-1046`; `xpeccy-plus/src/libxpeccy/cpu/Z80/z80nop.c:40-64` |
| Execution control | Fast step | Alt+F7 = 10 steps | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1047-1050` |
| Execution control | Run to cursor | F9, temp fetch bit on the cursor's physical cell | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1051-1059` |
| Execution control | Set PC | End sets PC to the cursor row | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1200-1211` |
| Execution control | Trace modes | Trace, stop on INT, stop here, trace log; holding F7 auto-traces | `xpeccy-plus/src/xgui/debuga/debuger.cpp:54-61`, `:953-998`, `:1109-1170` |
| Execution control | NMI / reset / pause | F10 / F12 / Pause | `xpeccy-plus/src/xcore/keymap.cpp:442`, `:470` |
| Tracing & logging | Trace log file | `|`-separated: PC, mnemonic, registers after the step, with a header row | `xpeccy-plus/src/xgui/debuga/debuger.cpp:958-975`, `:1126-1151` |
| Tracing & logging | Breakpoint log | One `XLG_BRK` INFO line per passing hit with place, access, counts, condition, all registers, paging signals | `xpeccy-plus/src/xcore/breakpoints.cpp:390-523` |
| History / rewind / time travel | Rewind | Page-deduplicated snapshots every N frames, played backwards with reversed audio while Delete is held in the main window; not connected to the debugger | `xpeccy-plus/src/xcore/rewind.cpp:17-38`, `:298-360`; `xpeccy-plus/src/emw_keys.cpp:226-227` |
| Video, raster & beam | RAY / FRAME | Beam X/Y in dots (bold in HBLANK/VBLANK), frame counter with reset | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1214-1219`, `:811-818` |
| Video, raster & beam | `RAY(x,y)` / `RAYX` / `RAYY` in conditions | Break when the beam crosses a dot during an instruction, or on beam position | `xpeccy-plus/src/xcore/xexpr.cpp:603-623` |
| Video, raster & beam | Screen viewer | Auto / Main / Shadow / Both / Custom page+offset, zoom, attributes-only, mono, no flash, grid, pixel/attribute address readout | `xpeccy-plus/src/xgui/debuga/dbg_zxscr.cpp:77-161`, `:340-481` |
| Video, raster & beam | Sprite scanner | Memory as 1-bpp bitmap, width/height/page, ZX-screen interleave, columns, save raw bytes | `xpeccy-plus/src/xgui/debuga/dbg_sprscan.cpp:19-152` |
| Video, raster & beam | Palette | 16x16 grid of the renderer's 256-entry RGB table, read-only | `xpeccy-plus/src/xgui/debuga/dbg_palette.cpp:12-65` |
| Sound & device views | Sound chip (PSG) | R0-R15, periods, volumes, mixer, levels, generator bits, envelope plot, per-channel mute (editable) | `xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:94-469` |
| Sound & device views | Sound chip (FM, YM2203) | Timers, Ch3 mode, per-channel Alg/Fb/Bk/Fq/Out, per-operator editable table, channel off | `xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:497-706` |
| Sound & device views | Wave scope + beeper bar | Mixer oscilloscope (40 ms running / 2 ms held), Fit / Log scale; beeper level bar | `xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:714-980`, `:1084-1088` |
| Sound & device views | FDC view | WD1793/uPD765 registers and drive state (BUSY, COM, IRQ, DRQ, TRK, SEC, DATA, SR, SR0-2, POS, IDX, MOT...) | `xpeccy-plus/src/xgui/debuga/dbg_fdd.cpp:12-35` |
| Sound & device views | Disk track dump | Raw track bytes per drive/track, ID/data/CRC coloring, head byte bold, jump to head | `xpeccy-plus/src/xgui/debuga/dbg_diskdump.cpp:5-283` |
| Sound & device views | Tape view | In/out levels, current signal length, state, signal index, 20 us/px signal diagram | `xpeccy-plus/src/xgui/debuga/dbg_tape.cpp:17-101` |
| Sound & device views | MEMMAP | Four 16K windows as ROM/RAM + page, editable (forces the map), Restore mapping | `xpeccy-plus/src/xgui/debuga/debuger.cpp:776-806`, `:2023-2057` |
| Sound & device views | PORTS / SIGNALS | Watched-port values; DOS, ROM, CPM, INT signals | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1209-1212`, `:2322-2345` |
| Profiling, heat maps, coverage | Heat map | Per-physical-byte read/write/exec counters for RAM and ROM; CPU/RAM/ROM views; categorical coloring; CSV export | `xpeccy-plus/src/libxpeccy/heatmap.c:47-123`; `xpeccy-plus/src/xgui/debuga/dbg_heat.cpp:59-631` |
| Profiling, heat maps, coverage | Runtime mapping | Marks executed bytes EXEC and other read bytes BYTE in the view-type nibble | `xpeccy-plus/src/libxpeccy/spectrum.c:98-114`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:335`, `:2195-2206` |
| Scripting, automation & remote debug | CLI | `--bp ADR|LABEL`, `--brk FILE`, `-l/--labels FILE`, `--xmap FILE` | `xpeccy-plus/src/main.cpp:58-60`, `:466-483`, `:518-520` |
| Scripting, automation & remote debug | Scripting / remote protocol | None found | — |
| Import / export & persistence | `.xbrk` breakpoint files | Text `TYPE:arg1:arg2:FLAGS:ACTION:condition` | `xpeccy-plus/src/xcore/breakpoints.cpp:611-807` |
| Import / export & persistence | Unreal `bpx.ini` import | `x0=`/`r0=`/`w0=` lines merged into CPU breakpoints | `xpeccy-plus/src/xcore/breakpoints.cpp:555-609` |
| Import / export & persistence | Heat map CSV, sprite raw bytes, trace log, disasm source | See the rows above | — |
| UI conveniences | Dock layout persistence | `<confdir>/debuga.layout`, fallback to default | `xpeccy-plus/src/xgui/debuga/debuger.cpp:853-885` |
| UI conveniences | Detachable Screen / Sound windows | The only views refreshed every emulated frame while running | `xpeccy-plus/src/main.cpp:351-365`; `xpeccy-plus/src/xgui/sndwin.cpp:58-63` |
| UI conveniences | Rebindable hotkeys | All `XCUT_` actions saved under `[KEYS]` | `xpeccy-plus/src/xcore/keymap.cpp:436-518`; `xpeccy-plus/src/xcore/config.cpp:295-301`, `:713-714` |

## 2. Architecture and refresh model

- **Window.** `DebugWin` (`xpeccy-plus/src/xgui/debuga/debuger.h:53`) builds its docks at `xpeccy-plus/src/xgui/debuga/debuger.cpp:414-501`. Default layout: REG-DUMP, FDD (disk dump) and CMOS are tabs behind DUMP; Screen, Sound Chip, Tape, FDC, Heat map and Palette are tabs behind Breakpoints; MEMMAP and STACK are stacked on the right (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1697-1726`). No panel has a close button (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1719-1723`). The layout is saved to `<confdir>/debuga.layout` on destruction and restored at startup, falling back to the default layout if the saved state is refused (`xpeccy-plus/src/xgui/debuga/debuger.cpp:853-885`).
- **No per-hardware hiding.** There are no `hw->id` checks in `xpeccy-plus/src/xgui/debuga/`; CMOS, FDC and FM tabs appear on a 48K. The comment at `xpeccy-plus/src/xgui/debuga/debuger.cpp:503-505` says anchors are "deliberately not in dockWidgets"; no code hides any dock per hardware.
- **The machine is held while the debugger is open.** `MainWin::doDebug()` pauses emulation with `PR_DEBUG` and sets `flgDBG = 1` (`xpeccy-plus/src/emulwin.cpp:1488-1492`); `DebugWin::start()` sets `comp->flgDBG = 1`, `comp->vid->debug = 1` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:296-298`) and stops fast-load (`xpeccy-plus/src/xgui/debuga/debuger.cpp:291`). The emulation thread does not advance frames while `flgDBG` is set (`xpeccy-plus/src/ethread.cpp:373`) and `renderFrame()` refuses to step the machine (`xpeccy-plus/src/emulwin.cpp:1522`).
- **Closing** (`DebugWin::stop()`) executes one instruction with `flgDBG = 1` (so a breakpoint at the current PC does not re-fire), then clears `flgDBG`, feeds the scope and hides floating docks (`xpeccy-plus/src/xgui/debuga/debuger.cpp:326-348`).
- **Refresh.** `fillAll()` = `fillCPU()` + `fillNotCPU()` + `fillDisasm()` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1227-1231`); it runs on open (`:311`), after each step (`doStep`, `:936-947`), after each trace step (`customEvent`, `:1121-1172`), on profile change (`:358-379`) and after memory edits (`:680`). `fillNotCPU()` (`:1195-1225`) updates the T counter, MEMMAP, redraws only visible docks (`:1200-1203`), emits `s_scr_upd` / `s_snd_upd` for detached windows (`:1206-1207`) and updates SIGNALS / RAY / FRAME / stack / ports (`:1209-1224`). Every `xDockWidget` redraws on `visibilityChanged` (`xpeccy-plus/src/xgui/classes.cpp:759`).
- **Consequence:** every docked view is a snapshot of a held machine. Only the detached Screen and Sound windows (`xpeccy-plus/src/main.cpp:351-365`) and the Watcher window refresh while running.
- **Per-byte metadata.** Physical-memory flag maps `brkRamMap[4M]`, `brkRomMap[512K]`, `slot->brkMap`, plus CPU-address `brkAdrMap[64K]` and `brkIOMap[64K]` (`xpeccy-plus/src/libxpeccy/spectrum.h:231-234`; `xpeccy-plus/src/libxpeccy/cartridge.h:29`). Low nibble = breakpoint flags, high nibble = disassembler view type (`xpeccy-plus/src/libxpeccy/cartridge.h:11-18`). On 16-bit buses `brkAdrMap` is ORed in when reading a cell's flags (`xpeccy-plus/src/libxpeccy/spectrum.c:1010-1012`).

## 3. CPU & registers

### 3.1 Cores

| Core | Where |
|---|---|
| Z80 (only built-in core) | `cpuTab[] = { {CPU_Z80, "Z80", z80RegTab, 16, ...}, {CPU_NONE, "none", ...} }` — `xpeccy-plus/src/libxpeccy/cpu/cpu.c:66-69`; enum `xpeccy-plus/src/libxpeccy/cpu/cpu.h:126-129` |
| "none" dummy | `dumCore` — `xpeccy-plus/src/libxpeccy/cpu/cpu.c:62` |
| External plugin | `dlopen` of a library exporting `getCore()` (`xpeccy-plus/src/libxpeccy/cpu/cpu.c:98-160`, selected in `xpeccy-plus/src/xcore/machines.cpp:935-938`); the Z80 core also exports itself as "Z80ext" (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:589-593`). A plugin's registers would appear automatically *(inferred)*. |

Only `src/libxpeccy/cpu/Z80/` exists (no LR35902/6502/i8080/K1801 cores as in upstream Xpeccy). Disassembly and assembly go through the core pointers `mnem` and `asmbl` (`xpeccy-plus/src/libxpeccy/cpu/cpu.c:245`, `:395`).

### 3.2 Z80 register table (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:494-542`)

| Name | Size | Flags | Shown beside | Group | Notes |
|---|---|---|---|---|---|
| PC | word | `REG_RDMP \| REG_PC` | SP | PTR | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:497` |
| AF | word | — | AF' | MAIN | getter recomputes F from flag bits (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:450`) |
| BC, DE, HL | word | `REG_RDMP` | BC', DE', HL' | MAIN | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:499-501` |
| SP | word | `REG_RDMP \| REG_SP` | — | PTR | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:503` |
| AF' | word | — | — | SHADOW | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:504` |
| BC', DE', HL' | word | `REG_RDMP` | — | SHADOW | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:505-507` |
| IX, IY | word | `REG_RDMP` | IX->IY | PTR | `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:509-510` |
| IR | word | — | IM | CTRL | `(I<<8) \| (R&0x7F) \| R7` (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:482`); I and R only combined |
| WZ (MEMPTR) | word | `REG_RDMP` | — | PTR | only under `#ifdef ISDEBUG` (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:535-537`), defined only for CMake Debug builds (`xpeccy-plus/CMakeLists.txt:63,68`) |
| IM | `REG_2` (0..2) | — | — | CTRL | `z80_set_im` maps 3 -> 2 (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:443`) |
| IFF1 | bit | — | IFF2 | CTRL | checkbox (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:539`) |
| IFF2 | bit | — | — | CTRL | checkbox (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:540`) |

Hidden (`REG_EMPTY`) but usable in expressions: I, R, A, A', F, F', B..L, B'..L', IXH/IXL/IYH/IYL (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:512-534`).

### 3.3 Flags

Flag string `"SZ5H3PNC"` (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:541`), 8 checkboxes including bits 5 and 3. `fillFlags()` shows up to 16 label+checkbox pairs, right-justified, reading `cpu_get_flag()` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1335-1352`; `xpeccy-plus/src/libxpeccy/cpu/cpu.c:586`, flag = the hidden `REG_FLG` "F" entry `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:516`). Clicking a checkbox calls `setFlags()` -> `cpu_set_flag()` and refills the panel (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1974-1983`). Layout: 8 per row under two register columns, 4 per row in 1-column and Wide layouts (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1376-1405`). F' flags have no checkboxes (only via AF').

### 3.4 Editing

- Every non-bit register is an `xHexSpin` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:525-531`) with `XHS_BGR | XHS_DEC | XHS_FILL | XHS_AUTOW` (changed-value background, hex/dec toggle, leading zeros, auto width).
- Commit is per keystroke: `textChanged -> DebugWin::setCPU` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:539`); `setCPU()` rebuilds a register bunch from every visible widget and calls `cpuSetRegs()` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1985-2008`; `xpeccy-plus/src/libxpeccy/cpu/cpu.c:557-570`), then refreshes flags, stack and disasm.
- Keys in a field: Up/Down ±1, PgUp/PgDn ±0x100, Insert toggles input mask/overwrite, X toggles hex/decimal (`xpeccy-plus/src/xgui/classes.cpp:346-385`).
- IFF1/IFF2 are `QCheckBox`, `toggled -> setCPU` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:540`, `placeReg` `:1407-1428`). `REG_RO` would make a field read-only (`:1416`, `:1422`); no Z80 register has it.
- Change highlight: per-byte change mask when `XHS_BGR` (`xpeccy-plus/src/xgui/classes.cpp:264-271`); "Split pairs" (`conf.dbg.regsplit`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1412`, menu `:585-587`) lights only the changed byte, except for typed registers (PC/SP/flags).
- Register-name clicks: left -> disassembly at the value, right -> dump at the value (`regClick`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1279-1300`); the left click pushes disasm history (`:1293-1295`).
- Layout menu on the CPU dock title: Auto / 1 column / 2 columns / Wide + "Split pairs" (`xpeccy-plus/src/xgui/debuga/debuger.cpp:566-592`; `xpeccy-plus/src/xcore/xcore.h:792-795`).

### 3.5 What is missing from the CPU panel

HALT state (`flgHALT`, `xpeccy-plus/src/libxpeccy/cpu/cpu.h:132`), the EI "no INT" latch (`flgNOINT`, `xpeccy-plus/src/libxpeccy/cpu/cpu.h:134`), `intrq`/`inten` (only a bold INT label in SIGNALS), Q latch, interrupt vector, current prefix/opcode, per-instruction T-states; MEMPTR in release builds; separate I and R fields; F' checkboxes.

T-states live in the Disasm dock: `labTcount = "<tickCount - tCount> / <frmtCount>"` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1197`); `frmtCount` = T since the last INT, reset at `IRQ_VID_INT` (`xpeccy-plus/src/libxpeccy/spectrum.c:478-480`). "Accumulate T" (`xpeccy-plus/ui/dbgwidgets/form_disasm.ui:179-181`) keeps the left counter running across steps (`xpeccy-plus/src/xgui/debuga/debuger.cpp:328-329`, `:938-939`, `:350-356`).

### 3.6 REG-DUMP dock

Rows = registers with `REG_RDMP` (`xpeccy-plus/src/xgui/debuga/dbg_rdump.cpp:72-88`), text like `"HL (1234):"` / `BC' (0000):` then N bytes, N following the panel width in whole groups (`xpeccy-plus/src/xgui/debuga/dbg_rdump.cpp:30-62`, `:107-125`). Bytes come from `romData`/`ramData` via the page map, `memRd()` for `MEM_SLOT`, `"--"` for EXT/IO (`xpeccy-plus/src/xgui/debuga/dbg_rdump.cpp:115-124`). Read-only (`xpeccy-plus/ui/dbgwidgets/form_regdump.ui:37-39`); no text column, no bytes before the pointer.

## 4. Disassembly

### 4.1 Columns and row kinds

Model `xDisasmModel` in view `xDisasmTable` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.h:42-120`), 4 columns (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:84-86`):

| Col | Content | Source |
|---|---|---|
| 0 | Address, label line, or `; comment` | `dasm[row].aname` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:201`) |
| 1 | Instruction bytes in hex | `dasm[row].bytes` (`:202`, filled `:511-516`) |
| 2 | Mnemonic/operands with label substitution | `dasm[row].command` (`:203`) |
| 3 | Info: memory-operand value at PC or branch-direction icon | `dasm[row].info` / `icon` (`:204`, `:185-195`) |

Row kinds from `getDisasm()` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:427-537`): comment row spanning all columns, gray (`:475-485`, `:143-144`, span `:1033-1034`); label row, bold, `dbg.disk.id.*` colors, only with "Show labels" (`:486-494`, font `:132-138`, background `:173-178`); instruction row (`:495-517`); `LABEL: EQU $-n` row when a label falls inside a multi-byte instruction (`:518-534`); empty block-separator row after RET/JP/JR/DJNZ etc. when "Block separator" is on (`:34-74`, `:557-558`; the mnemonic list still contains 6502/x86/1801VM1 names from upstream, `:31-41`).

Address column (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:496-507`): default 4-hex CPU address; with "Show segment" (`conf.dbg.segment`) `RAM:pp:oooo` / `ROM:` / `SLT:` / `EXT:` with `pp = (bank>>6)&0xFF` (assumes 16K pages) and `oooo = adr & 0x3FFF` (page offset, not CPU address). Widths are fixed on resize: address sized for `"000:00:0000"`, bytes for 10 hex digits, info 50 px; visible rows = viewport height / row height (`:963-979`). There is no flags column.

### 4.2 PC-row extras

`dasmCode()` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:379-405`) on the PC row: if the instruction reads memory (`mnm.mem`), column 3 shows a word (`OF_MWORD`) or byte (`:387-392`), computed by `z80_mnem` from `(HL)`, `(DE)`, `(BC)`, `(IX+d)`, `(IY+d)`, `(nn)`; for BIT/RES/SET the tested bit (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:253-283`, `:280-282`). A conditional branch that is taken gets a down/up/left(self-jump) arrow (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:393-402`), and `update_lst()` puts a left arrow on the target row (`:563-578`). `z80_mnem` evaluates DJNZ, JP cc, CALL cc, RET cc, JR cc, JP, CALL, RET, JR against live flags (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:285-321`); for RET/RET cc `oadr` = word at (SP) (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:303-311`, `xpeccy-plus/src/libxpeccy/cpu/cpu.h:25`).

### 4.3 Operand formatting and label substitution

Shared formatting (`xpeccy-plus/src/libxpeccy/cpu/cpu.c:230-317`): `:1` byte, `:2` word, `:3` relative target, `:4`/`:5` signed displacement, `:7` current address; operand address arithmetic masked to 16 bits (`xpeccy-plus/src/libxpeccy/cpu/cpu.c:248`, `:277`). `placeLabel()` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:276-309`) replaces `#nnnn` with a label; for `OF_MEMADR` operands it searches up to 7 bytes back and writes `LABEL + n` (`:283-307`), stored in `drow.label` so the delegate draws it bold (`:304`, `:922-925`).

### 4.4 Address space and paging

- The listing always runs in the CPU bus address space. `XVIEW_CPU`/`XVIEW_RAM`/`XVIEW_ROM` + page exist (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:18-19`, `:221-274`, `:449-472`) and `xDisasmTable::setMode()` exists (`:997-1001`), but nothing calls it: the only `setMode(` calls in `src/xgui` go to the dump (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:289`, `:746`, and `xpeccy-plus/src/xgui/debuga/debuger.cpp:928` inside a commented-out block `:909-932`).
- To list another page you force it into a 16K window via MEMMAP (section 13.1).
- Metadata is page-aware: labels, comments, breakpoints, view types keyed by (type, physical absolute address) (`xpeccy-plus/src/xcore/labels.cpp:39`, `:257`; `xpeccy-plus/src/libxpeccy/spectrum.c:982-992`).
- SLOT pages are read via `memRd` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:233`); writes to SLOT are ignored (`:256`); ROM writes need "Allow ROM wr" (`conf.dbg.romwr`, `:257-259`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:905-907`).

### 4.5 View-as types (high nibble, `xpeccy-plus/src/libxpeccy/spectrum.h:58-63`)

| Flag | UI name (`xpeccy-plus/ui/dbgwidgets/form_disasm.ui`) | Rendering |
|---|---|---|
| `DBG_VIEW_CODE` 0x00 | (default) | disassembled (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:419-420`) |
| `DBG_VIEW_EXEC` 0x50 | "Opcode" (`xpeccy-plus/ui/dbgwidgets/form_disasm.ui:310-312`) | disassembled; also set by runtime mapping |
| `DBG_VIEW_BYTE` 0x10 | "Byte" | `DB #xx,...` up to `conf.dbg.dbsize` (default 8), stops at a label (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:311-325`; `xpeccy-plus/src/xcore/config.cpp:628`) |
| `DBG_VIEW_WORD` 0x20 | "Word" | `DW #xxxx,...` up to `dwsize` (default 4) (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:327-345`; `xpeccy-plus/src/xcore/config.cpp:629`) |
| `DBG_VIEW_ADDR` 0x30 | "Address" | single `DW label`, `oadr` set so F4 follows it (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:347-355`) |
| `DBG_VIEW_TEXT` 0x40 | "ASCII" | `DB "..."` for printable 32..127, up to `dmsize` (default 127) (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:357-377`; `xpeccy-plus/src/xcore/config.cpp:630`) |

Marking: View submenu in the context menu and a toolbar "View" button (`xpeccy-plus/src/xgui/debuga/debuger.cpp:630-634`, `:828-835`); `chaCellProperty()` applies to the cell or the whole selected block (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2406-2488`); for ASCII, non-printable bytes become Byte (`:2469-2475`). Typing `db`/`dw`/`db "..."` also sets the type (4.6). Runtime mapping sets EXEC/BYTE automatically (section 16.2). Persisted in `.xmap` (section 6.4).

### 4.6 In-place editing (`xDisasmModel::setData`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:733-873`)

Columns 0-2 are editable except the command column of EQU and separator rows (`:580-590`); Enter opens the editor (`:1282-1285`).

- **Column 0** (`:752-782`): empty text deletes the comment or label on the row (`:754-764`); `; text` adds/replaces a comment, `;` alone deletes it (`:765-772`); `.REG` or `seg:off` is parsed as an address (`:773-774`); anything else goes through `asmAddr()` (`:669-688`): `@name` jumps only; `#hex`, `0xhex`, a label name or plain hex navigates; an unparseable identifier (letter or `_` first, no `$`, no space) creates or renames the label at that row's address (using `mem_get_xadr`, so the type RAM/ROM/SLOT is correct, `:750`). After a parse the listing is re-anchored so the target lands on the cursor's row (`adr_for_row(..., exact=1)`, `:699-731`, `:778-780`).
- **Column 1** (`:783-793`): hex pairs written one per byte; editor limited to 12 hex digits = 6 bytes (`xpeccy-plus/src/xgui/classes.cpp:729`).
- **Column 2** (`:794-869`): `db "text"` writes text and marks TEXT (`:799-812`); `db n` writes one byte, marks BYTE (`:813-824`); `dw label|n` writes two bytes, marks ADDR or WORD (`:825-852`); otherwise `cpuAsm()` assembles, with `#` rewritten to `0x` (`:797-798`, `:853-862`). Operands must be numeric: the scanner accepts only `+ - 0-9` to start an argument (`xpeccy-plus/src/libxpeccy/cpu/cpu.c:321`, `:342`), so labels are not accepted (`TODO: replace label name`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:854`). The Z80 assembler accepts `hx/lx/hy/ly` (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:328-360`). After success the cursor moves down one row (`s_comenter` -> `rowDown`, `:868`, `:956`, `:1063-1076`).

### 4.7 Colors and highlights

Palette keys `dbg.pc.bg/txt`, `dbg.sel.bg/txt`, `dbg.brk.txt`, settable from style sheets via Q_PROPERTYs (`xpeccy-plus/src/xgui/debuga/dbg_disasm.h:67-71`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:981-987`). Foreground priority: breakpoint text, comment gray, PC, selection, label (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:140-167`). Breakpoint rows change only text color, no background (`:141-142`, `:168-184`). PC row gets a background, matched by physical cell (`:446-469`). "Dim address"/"Dim opcodes" use the average of text and background (`:92-105`, `:154-164`). Constants use `DBG_PAL_CONST`, labels bold; on hover/selection constants become bold instead (`:884-946`). Block highlighting only in CPU mode (`:470`).

### 4.8 Other listing features

- Ctrl+C copies the block (or current instruction) as source with `ORG` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1078-1110`, `:1289`).
- "Disasm to file" writes the block or all of 0..busmask with `; Created by Xpeccy+ Debugger` and `ORG` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2123-2163`).
- Context menu: Breakpoints (Fetch/Read/Write), View, Labels list, Trace here, Show labels (`xpeccy-plus/src/xgui/debuga/debuger.cpp:820-839`, `:2389-2403`).

## 5. Navigation & bookmarks

### 5.1 History (disasm only)

`QList<QPair<int,int>> history`, each entry = (view anchor `asmadr`, cursor row address) (`xpeccy-plus/src/xgui/debuga/dbg_disasm.h:96-99`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1116-1120`). Back only: F5 (`XCUT_RETFROM`) `takeLast()` restores scroll and cursor (`:1262-1271`); no forward, no redo; unbounded, never trimmed (`:1119`); not persisted.

| Pushes history | Does not push (`setAdr(x)` defaults `hist = 0`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.h:88`) |
|---|---|
| Typed address in column 0 and F4 follow, both via `s_adrch(oadr, nadr)` -> `t_update` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:781`, `:957`, `:1054-1061`, `:1253-1261`) | Go to PC (Home); re-centering on PC after step/stop (`xpeccy-plus/src/xgui/debuga/debuger.cpp:314`, `:944`) |
| Left click on a CPU register (`setAdr(adr, 1)`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1293-1295`) | Search hits (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2605-2609`) |
| Double-click in the Labels list (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2087-2095`) | Breakpoint list, heat map, scrollbar (`xpeccy-plus/src/xgui/debuga/debuger.cpp:680`, `:686`, `:721`); mark jumps (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1143`) |

### 5.2 Marked addresses

Five slots `int storedAddress[5]` initialized to -1 (`xpeccy-plus/src/xgui/debuga/dbg_disasm.h:101`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:951-953`). Ctrl+1..5 store, Alt+1..5 jump (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1139-1147`, `:1287-1305`), hard-coded, not `XCUT_`. The stored value is `getAdr()` = `model->asmadr`, the top row of the view, not the cursor (`:1003-1005`, `:1145`). Jumps are not recorded in history (`:1143`). Not persisted. On macOS the handler reads raw `ev->modifiers()` without `xNativeMods()` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1156`) while the debugger window swaps Ctrl and Cmd (`xpeccy-plus/src/xcore/keymap.cpp:590-605`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1001`), so "Ctrl+1" is physically Cmd+1 on a Mac *(inferred)*. `xpeccy-plus/src/xcore/bookmarks.cpp` is file favorites, unrelated (`xpeccy-plus/src/xcore/bookmarks.cpp:6-50`).

### 5.3 Go to / follow

- Go to PC, Home (`XCUT_TOPC`): `setAdr(pc)`, CPU mode only (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1195-1199`).
- Set PC, End (`XCUT_SETPC`): PC = cursor row address, refresh all (`:1200-1210`).
- Go to address, G (`XCUT_GOTOADR`): opens the column 0 editor on the first non-separator row (`:1272-1281`). `str_to_adr` (`:623-666`) accepts `.REG` (register by name from the core table), `seg:off` = `(seg<<4)+off` with register names allowed for each part, `#hex`, `0xhex`, label name, plain hex. No arithmetic (`label+5`, `hl+2`).
- Follow operand, F4 (`XCUT_JUMPTO`): jumps to `dasm[row].oadr`, keeping the cursor row (`:1253-1261`); on RET follows the return address (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:303-311`); `jp (hl)` has no `oadr` *(inferred from the mnem table: no `:2` operand)* (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:253`).
- Labels list double-click/Enter: `memFindAdr`, nothing if the page is unmapped (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2087-2095`; `xpeccy-plus/src/xgui/labelist.cpp:105-120`).

### 5.4 Scrolling (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1149-1436`)

| Input | Effect |
|---|---|
| Up / Down | move cursor; at the edge scroll one instruction (`:1165-1178`) |
| Ctrl+Up / Ctrl+Down | scroll exactly one byte (`:1166`, `:1400-1401`, `:1420-1421`) |
| PageUp | top row becomes bottom row via a DP back-search over up to `(rows+2)*8` bytes (`adr_for_row`, `:699-731`, `:1179-1184`) |
| PageDown | last non-EQU row becomes top (`:1185-1194`) |
| Wheel | one instruction; Ctrl = one byte (`:1430-1436`) |
| Scrollbar | direct address 0..0xFFFF (`xpeccy-plus/src/xgui/debuga/debuger.cpp:364-366`, `:679-680`) |

Single-step scroll up (`getPrevAdr()`): tries 16..1 bytes back, takes the first start whose instruction length equals the distance, else one byte back (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:592-604`).

### 5.5 Blocks

Ctrl+click = block start, Shift+click = end, left drag selects, middle click clears (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1313-1350`, `:1376-1393`). `blockStart`/`blockEnd` are globals shared with the dump (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:15-16`, `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:9-10`). Block operations: fill (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2613-2616`), view type (`:2413-2419`), disasm to file/clipboard, breakpoints (`:2369-2387`).

## 6. Symbols & labels

### 6.1 Data model

`conf.labsets` = list of `xLabelSet{name, list: QMap<name, xAdr>}`, `conf.curlabset` = active set; only the current set is looked up (`xpeccy-plus/src/xcore/labels.cpp:10-83`). `map_labels()` builds `conf.labmap[type][abs] = name` (`xpeccy-plus/src/xcore/labels.cpp:31-42`). Lookup keyed by memory type (RAM/ROM/SLOT/IO/EXT) and absolute physical address (`:127-138`); one name per cell, re-adding drops the old entry (`:111-125`). With no set, adding a label creates `noname` (`:115-118`). Comments: global `conf.commap[type][abs]`, independent of sets (`:254-278`; `xpeccy-plus/src/xcore/xcore.h:609`).

### 6.2 Label text file

`loadLabels()` (`xpeccy-plus/src/xcore/labels.cpp:152-219`), dialog "Load SJASM labels" (`:161`): line `BB:AAAA NAME` split on `[: \r\n]` (`:170-176`); a line starting with `:` gets `FF` prepended (`:170-171`). *(inferred)* sjasmplus/Unreal `LABELSLIST` style. Bank and address hex; every label created as `MEM_RAM` (`:178`) with `abs = (bank<<14) | (adr & 0x3FFF)` (`:189-190`); bank `FF` remapped by CPU address 0000->0, 4000->5, 8000->2, C000->0 (`:181-188`). A set named after the file is created/reused and cleared first (`:163-165`). Path kept in `conf.labpath` for "Reload snapshot and labels" (`:213`; `xpeccy-plus/src/filer.cpp:558-569`). `saveLabels()` (`:221-252`) writes `BB:AAAA NAME` with `BB = abs>>14` for RAM and `FF` for any other type (`:241`). CLI `-l/--labels <file>` (`xpeccy-plus/src/main.cpp:481-483`).

### 6.3 Editing and Labels list

Create/rename by typing in column 0, delete by clearing it (4.6). Labels list window (`xpeccy-plus/src/xgui/labelist.cpp`): case-sensitive substring filter (`str.contains(f)`, `:19-27`); choose/create/rename/delete sets (`:122-156`); addresses as `RAM:xxxxxx` or, with "CPU", `CPU:xxxx` when mapped else `pp:oooo` (`:51-92`). No per-label add/delete/rename ("TODO: extend labels list to labels sets manager, add address column", `:1`); `actLabManager` exists in the .ui but is commented out (`xpeccy-plus/ui/dbgwidgets/form_disasm.ui:566-572`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:628`).

### 6.4 `.xmap` container (`xpeccy-plus/src/xcore/xmap.cpp`)

Signature `XMEMMAP ` then chunks of 8-byte tag + LE32 length + data (`xpeccy-plus/src/xcore/xmap.cpp:36-148`, `:171-240`):

| Chunk | Content |
|---|---|
| `ramflags` | per-byte breakpoint + view-type map for RAM, capped at 4M |
| `romflags` | same for ROM, capped at 512K |
| `sltflags` | same for the cartridge slot |
| `labels  ` | one chunk per label set: lines `TYPE:abs8hex:name` plus `NAME:_:setname` |
| `comments` | lines `TYPE:abs8hex:text`; `:` inside text re-joined on load (`xpeccy-plus/src/xcore/xmap.cpp:120-123`) |

Only format that keeps ROM/SLOT/IO labels and comments (`xpeccy-plus/src/xcore/xmap.cpp:209-233`); EXT labels dropped on save (`:214`). Raw map bytes include the breakpoint nibble (`xpeccy-plus/src/xcore/xmap.cpp:64-81`, `:187-196`). Loading calls `brkInstallAll()` (`xpeccy-plus/src/xcore/xmap.cpp:141`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:2059-2066`); the note on code/memory reads this as making stored breakpoints live, while the breakpoint note observes that `brkInstallAll()` clears the flag nibble and rebuilds from the list, so breakpoints do not round-trip *(inferred)*. CLI `--xmap <file>` (`xpeccy-plus/src/main.cpp:518-520`).

### 6.5 Where labels are used

| Where | How |
|---|---|
| Label row above instruction | `find_label(xadr)` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:487-494`) |
| Operand `LABEL + n` | `placeLabel` (`:276-309`) |
| EQU rows | `:518-534` |
| `DW label` in ADDR view | `:347-355` |
| Disasm / dump address input | `str_to_adr` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:655-659`; `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:235`, `:249-256`) |
| `dw label` in the command editor | `:825-836` |
| Conditions and watches | label lookup by name at each evaluation (`xpeccy-plus/src/xcore/xexpr.cpp:559-568`) |
| `--bp LABEL` | `xpeccy-plus/src/main.cpp:466-472` |
| Not used | assembler operands; finder, filler, save/open dump dialogs use hex spinboxes (`xpeccy-plus/ui/filler.ui:23-38`, `xpeccy-plus/ui/dumpdial.ui`, `xpeccy-plus/ui/openDump.ui`) |

## 7. Memory views

### 7.1 DUMP dock (`xpeccy-plus/src/xgui/debuga/dbg_dump.{h,cpp}`, `xpeccy-plus/ui/dbgwidgets/form_dump.ui`)

- Layout: column 0 address, columns 1..16 bytes (only `dmpsize` shown), column 17 text (`xpeccy-plus/src/xgui/debuga/dbg_dump.h:17-21`). Bytes per row Auto (whole groups of 4: 4/8/12/16), 8, 12, 16 (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:354-411`, `:595-598`). Text code pages WIN1251 / CP866 / KOI8R; bytes < 32 as `.` (`:16-37`, `:600-602`). Dock title `DUMP | xxxxxx` = cursor address (`:708-711`).
- Modes (`cbDumpView`, `:604-606`): **CPU** = 64K through the current map, SLOT via `memRd` (`:45-55`, `:740-742`); **RAM / ROM** = page `sbDumpPage` + displayed base (default C000 RAM, 0000 ROM) + "page size" 256B..64K (`:608-632`, `:723-749`), addresses shown `pp:aaaa` (`:207-214`). Physical index always `(adr & 0x3FFF) | (page << 14)` (`:56-67`, `:92-100`): the page is always a 16K unit; page size changes only wrap and display.
- Editing: byte cells 2 hex digits (`:227-233`, `:257-263`; delegate `xpeccy-plus/src/xgui/classes.cpp:730`); in CPU view ROM writes need "Allow ROM wr", SLOT writes ignored (`:77-91`); text column not editable (`:200-201`, `:631`); column 0 edit = go to address.
- Highlighting: any breakpoint bit -> `dbg.brk.txt` text (`:168-170`); block -> `dbg.sel.*`, CPU mode only (`:155-158`, `:171-172`). No changed-byte highlighting.
- Context menu: Fetch/Read/Write breakpoints on cell or block (`:620-623`, `:671-701`).
- Navigation: G edits the row-0 address with the disasm syntax (`:237-256`, `:477-483`); Ctrl+P/S/B/D/H/X/Y register jumps (`:470-476`, `:426-432`); right-click on a CPU register name (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1289-1291`); search hits move dump and disasm (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2605-2609`); wheel one row, PgUp/PgDn one page, scrollbar (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:464-469`, `:574-580`, `:637-638`). No history, no marks.
- Hard-coded keys: Up/Down, PgUp/PgDn, Enter, F2 ignored so it reaches the debugger (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:443-491`).
- Other memories are separate docks: CMOS (section 14.3), disk track dump (14.5), REG-DUMP (3.6). No video-RAM, GS/sound RAM or cartridge dump mode (grep for `gs->`, `vram`, `MEM_EXT` in `src/xgui/debuga/`). The Sprite scanner shows RAM graphically (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2620-2626`).

### 7.2 Memory finder (`xpeccy-plus/src/xgui/debuga/dbg_finder.cpp`, `xpeccy-plus/ui/dbgfinder.ui`)

Bytes `HH:hh:...` up to 8 (input mask `xpeccy-plus/ui/dbgfinder.ui:36-37`); Mask 8 bytes default FF (`:61-65`); Text mirrors bytes as Latin-1, 8 characters (`xpeccy-plus/src/xgui/debuga/dbg_finder.cpp:17-52`). Match `(mem & mask) == (pat & mask)`, mask 00 = byte wildcard (`:81`); no nibble wildcards, regex, case folding or Unicode. Scope: CPU address space via `memRd` (`:80`), from `adr` for `busmask+1` bytes with wrap (`:79`); unmapped pages unsearchable. Start = disasm top + 1 on first open (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2597-2603`); after a hit `adr` = hit (`xpeccy-plus/src/xgui/debuga/dbg_finder.cpp:91`); `patFound(adr)` moves disasm and dump (`xpeccy-plus/src/xgui/debuga/debuger.cpp:763`, `:2605-2609`).

### 7.3 Memory filler (`xpeccy-plus/src/xgui/debuga/dbg_memfill.cpp`, `xpeccy-plus/ui/filler.ui`)

Start/end 4-hex spinboxes, pattern up to 8 bytes, mask up to 8 bytes, method Mask / Put / Or / And / Xor (`xpeccy-plus/ui/filler.ui:23-146`, `xpeccy-plus/src/xgui/debuga/dbg_memfill.cpp:69-87`); pattern repeats cyclically (`:89-91`); block pre-fills start/end (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2613-2616`, `xpeccy-plus/src/xgui/debuga/dbg_memfill.cpp:12-19`); CPU space masked to 16 bits (`:70`, `:88`); ROM only with "Allow ROM wr", SLOT skipped (`:29-43`).

### 7.4 Save / load memory

- **Save RAM dump** (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2496-2593`, `xpeccy-plus/ui/dumpdial.ui`): linked Start/End/Length spinboxes (`:737-752`, `:2496-2519`); data via `rdbyte()` through the current CPU map (`:2521-2536`, `:2099-2112`). Formats: Binary (`:2538-2546`); Hobeta `.$C`, name = start address text, start/length in header (`:2548-2569`); into a TR-DOS file on drive A-D, disk created and formatted if absent, limit 0xFF00 (`:2576-2593`). No page selection (old bank field commented out, `:2522`, `:2530`, `:2555`).
- **Open RAM dump** (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2644-2702`, `xpeccy-plus/ui/openDump.ui`): raw binary 1..0xFF00 bytes (`:2664-2676`) at a hex start (default C000) (`:2657-2662`), written with `dbg_mem_wr` honoring "Allow ROM wr" (`:2630-2655`), stops at 0x10000 (`:2648`). No Hobeta/TRD/SCL import, no page target.

### 7.5 Stack view (`xpeccy-plus/src/xgui/debuga/dbg_stack.{h,cpp}`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:464-501`, `:2212-2245`)

Rows fill the panel height (`xpeccy-plus/src/xgui/debuga/dbg_stack.cpp:17-22`). The SP row is labeled with SP's address `XXXX:` on a tinted band (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2220-2223`, `xpeccy-plus/src/xgui/debuga/dbg_stack.cpp:72-73`); others with signed offsets `+02:`, `-04:` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2214-2218`); words little-endian via `rdbyte` through the CPU map (`:2239-2240`). Two arrow buttons shift the first offset by ±2 within ±16 bytes (`DBG_STACK_OFS`), saved as `stack.offset` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:474-489`, `:2230-2233`; `xpeccy-plus/src/xcore/xcore.h:788`; `xpeccy-plus/src/xcore/config.cpp:248`, `:725`). Paint-only: event filter handles only Resize (`xpeccy-plus/src/xgui/debuga/dbg_stack.h:18-30`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1840-1842`); no editing, no symbol lookup, no return-address hints, no click-through. Related: F4 on a RET follows (SP) (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:303-311`).

## 8. Breakpoints

### 8.1 Architecture

An `xBrkPoint` lives in `conf.brk.list` (`std::vector`, unbounded) (`xpeccy-plus/src/xcore/xcore.h:217-237`, `:604-608`). The list is compiled by `brkInstallAll()` (`xpeccy-plus/src/xcore/breakpoints.cpp:344-379`) into the flag maps (section 2) with bits `FETCH=1, RD=2, WR=4, TFETCH=8`, plus `conf.brk.map[type][addr] -> xBrkPoint*` for hit lookup (`xpeccy-plus/src/xcore/xcore.h:607`). The C core only sets `flgBRK`, `brkt` (type) and `brka` (address); the emulation thread (`xpeccy-plus/src/ethread.cpp:499-537`) looks up the entry, counts, evaluates the condition and runs the action.

### 8.2 Kinds (`xpeccy-plus/src/libxpeccy/defines.h:82-94`)

| Enum | Meaning | Keyed by | Flags | Created from |
|---|---|---|---|---|
| `BRK_CPUADR` | CPU bus address (any page mapped there) | 16-bit address range `adr..eadr` (`xpeccy-plus/src/xcore/breakpoints.cpp:104-117`) | F/R/W | Dialog "ADR bus (MEM)" (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:293`); Shift+Space in disasm (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1216-1224`); `--bp` (`xpeccy-plus/src/main.cpp:466-472`); `bpx.ini` import (`xpeccy-plus/src/xcore/breakpoints.cpp:570-609`) |
| `BRK_IOPORT` | I/O port | port + mask, hit when `(port & mask) == (adr & mask)` (`xpeccy-plus/src/xcore/breakpoints.cpp:286-289`, `xpeccy-plus/src/xcore/xcore.h:231`) | R = IN, W = OUT (`xpeccy-plus/src/xcore/breakpoints.cpp:291-292`) | Dialog "ADR bus (IO)" (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:294`) |
| `BRK_MEMRAM` | Physical RAM cell | `page<<14 \| offset` (16K pages), range (`xpeccy-plus/src/xcore/breakpoints.cpp:87`, `:305-307`) | F/R/W | Dialog "RAM cell"; Space in disasm (default) (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1225-1235`); disasm context menu (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2434-2445`); dump context menu (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:680-700`) |
| `BRK_MEMROM` | Physical ROM cell | absolute ROM offset, range (`xpeccy-plus/src/xcore/breakpoints.cpp:88`, `:309-311`) | F/R/W | as above |
| `BRK_MEMSLT` | Cartridge slot cell | absolute slot offset, range (`xpeccy-plus/src/xcore/breakpoints.cpp:89`, `:313-316`) | F/R/W | Dialog "SLT cell", Space on a slot page |
| `BRK_MEMEXT` | "extended" memory | absolute address | — | only from dump CPU mode on EXT (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:695`); never installed (no case in `brkInstall()`, `xpeccy-plus/src/xcore/breakpoints.cpp:285-322`), not saved (`:784-786`) *(inferred: inert)* |
| `BRK_IRQ` | interrupt request pending | none; one per list | condition only | Dialog "IRQ" (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:298`) |
| `BRK_COND` | global condition | condition text (entries differ only by text, `xpeccy-plus/src/xcore/breakpoints.cpp:27-31`) | "On change" edge flag | Dialog "Condition (global)" (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:299`) |
| `BRK_MEMCELL` | creation-request pseudo-type, resolved to RAM/ROM/SLT/EXT by `flag & MEM_BRK_TMASK` (`xpeccy-plus/src/xcore/breakpoints.cpp:85-91`) | — | — | — |
| `BRK_HBLANK` | declared only (`xpeccy-plus/src/libxpeccy/defines.h:92`) | — | — | — |

Fields (`xpeccy-plus/src/xcore/xcore.h:217-237`): `off`, `fetch`, `read`, `write`, `temp`, `last`/`fired`/`onchg` (COND edge logic), `log`, `type`, `adr`, `eadr`, `mask`, `hits`, `count`, `action`, `cond` (text), `script` (compiled).

### 8.3 Where each kind is checked

| Check | Where | Timing |
|---|---|---|
| Fetch / temp fetch | `compExec()` at PC: `comp_check_bp(comp, pc, MEM_BRK_FETCH \| MEM_BRK_TFETCH)` (`xpeccy-plus/src/libxpeccy/spectrum.c:900-917`) | before the instruction; sets `flgBRKPRE` (`:911`), returns without executing |
| IRQ | `if (comp->cpu->intrq && comp->flgIBRK)` (`xpeccy-plus/src/libxpeccy/spectrum.c:918-923`) | before instruction/interrupt acceptance; tests `intrq` only, not `inten`/IFF1, so *(inferred)* fires on an INT held under DI and on NMI (both bits of `intrq`, `xpeccy-plus/src/libxpeccy/cpu/cpu.h:147`; `xpeccy-plus/src/libxpeccy/hardware/common.c:65,131`) |
| Memory read | `memrd_watched()` (`xpeccy-plus/src/libxpeccy/spectrum.c:121-130`) | during; action after the instruction. *(inferred)* also fires on opcode/operand fetch of that byte |
| Memory write | `memwr_watched()` (`xpeccy-plus/src/libxpeccy/spectrum.c:174-191`) | during; only when the cell resolves to RAM/ROM/SLOT (`fptr` non-null, `:174-176`); *(inferred)* CPU-address W breakpoints do not fire on EXT/unmapped memory |
| I/O in | `iord`: `brkIOMap[port] & MEM_BRK_RD` (`xpeccy-plus/src/libxpeccy/spectrum.c:395-401`) | before the device read; action after instruction |
| I/O out | `iowr`: `brkIOMap[port] & MEM_BRK_WR` (`xpeccy-plus/src/libxpeccy/spectrum.c:433-439`) | after the device write |
| Global conditions | emulation thread after every instruction, only if no address breakpoint fired on it (`xpeccy-plus/src/ethread.cpp:521-534`) | after the instruction |

`comp_check_bp()` (`xpeccy-plus/src/libxpeccy/spectrum.c:26-57`) looks at `brkAdrMap` first; only if that misses does it resolve the physical cell (`mem_get_xadr`) and check the RAM/ROM/SLOT map, so a CPU-address hit wins over a page hit at the same place.

### 8.4 Flag-map implementation and cost

- **Fast path:** memory hooks are entered only when `comp_mem_watched()` = `flgMAP | flgHEAT | flgCOND | flgBRKMEM` is non-zero (`xpeccy-plus/src/libxpeccy/spectrum.h:260-262`, `xpeccy-plus/src/libxpeccy/spectrum.c:67-84`, `:156-166`). `flgBRKMEM` is reset in `brkInstallAll()` and set only if a map byte gets a flag (`xpeccy-plus/src/xcore/breakpoints.cpp:324`, `:365`). The check itself is a byte lookup per access.
- **Slow path:** any memory breakpoint, condition or log flag sends every memory access through `memrd_watched`/`memwr_watched` (`xpeccy-plus/src/libxpeccy/spectrum.h:260-262`, `xpeccy-plus/src/libxpeccy/spectrum.c:67-84`).
- **Global conditions** are evaluated after every instruction (`xpeccy-plus/src/ethread.cpp:521`), with registers and labels looked up by name string each time (`xpeccy-plus/src/xcore/xexpr.cpp:559-568`).
- **Rebuild cost:** `brkInstallAll()` clears ~4.7 MB of maps and recompiles all conditions on every edit (`xpeccy-plus/src/xcore/breakpoints.cpp:344-379`); each IO breakpoint loops over all 65536 ports (`:287-299`); `brk_clear_tmp` walks 4.6 MB on every debugger open (`:246-257`).
- The core snapshot (`xstate`) skips the 4.6 MB maps (`xpeccy-plus/src/libxpeccy/spectrum.h:226-230`; `xpeccy-plus/src/libxpeccy/xstate.c:66-70`). Run-ahead frames skip breakpoint checks (`xpeccy-plus/src/libxpeccy/spectrum.c:898-900`), and run-ahead is disabled while `flgDBG` (`xpeccy-plus/src/ethread.cpp:373`).

### 8.5 Temporary, system and hardware breakpoints

- **Temporary:** not list entries. `MEM_BRK_TFETCH` (0x08) is ORed into the physical page map via `getBrkPtr()` (`xpeccy-plus/src/libxpeccy/spectrum.c:982-998`) by Step over (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1031-1041`) and Run to cursor (`:1051-1059`). On hit the bit is cleared and `brkt = -1` (`xpeccy-plus/src/libxpeccy/spectrum.c:912-915`), which the thread treats as "open debugger" (`xpeccy-plus/src/ethread.cpp:501-503`). All temp bits are wiped on debugger open (`xpeccy-plus/src/xgui/debuga/debuger.cpp:302` -> `xpeccy-plus/src/xcore/breakpoints.cpp:246-257`). `brkInstallAll()` keeps only the high nibble (`clearMap` `&= 0xf0`, `xpeccy-plus/src/xcore/breakpoints.cpp:259-265`) and memsets `brkAdrMap`, so *(inferred)* any breakpoint edit erases an armed temp bit; moot because the debugger is closed while one is armed. The `brk.temp` field is honored by `brkInstall` (`xpeccy-plus/src/xcore/breakpoints.cpp:278`) but nothing sets it (`:127`, `:712`, `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:528`, `:579`).
- **System breakpoints:** `conf.brk.list_sys` / `BRKF_SYSTEM` (`xpeccy-plus/src/xcore/xcore.h:210`, `:606`; `xpeccy-plus/src/xcore/breakpoints.cpp:16`, `:204-205`, `:370`) are unused infrastructure; the tape trap is hard-coded PC checks with a TODO to make it a system breakpoint (`xpeccy-plus/src/ethread.cpp:430-440`).
- **Hardware BRKA (machine emulation, not the debugger):** ZX-Evo raises NMI when `regBF & 0x10` and the fetch address matches (`xpeccy-plus/src/libxpeccy/hardware/pentevo.c:157-160`, register `:30`, `:233-260`).

### 8.6 Hit and count semantics

Address/IO/IRQ breakpoints are handled in the thread after the instruction (or before, for fetch/IRQ) (`xpeccy-plus/src/ethread.cpp:507-519`). `hits++` happens before the condition, so `HITS > 30` skips 30 hits; `count++` only when the condition passes (`xpeccy-plus/src/ethread.cpp:510-514`; `xpeccy-plus/src/xcore/xcore.h:232-233`). Global `BRK_COND`: `brk_check_cond()` (`xpeccy-plus/src/xcore/breakpoints.cpp:170-189`), only if `brk_cond_n > 0` (`xpeccy-plus/src/ethread.cpp:521`); level-triggered by default, `onchg` makes it edge-triggered false->true (`xpeccy-plus/src/xcore/breakpoints.cpp:181`); for COND `hits` counts firings, so HITS can limit but not skip, and the dialog warns about this (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:473-481`). Several conditions can fire on one instruction, each with its action (`xpeccy-plus/src/ethread.cpp:524-533`). Global conditions are skipped on an instruction where an address breakpoint fired (`else if`, `xpeccy-plus/src/ethread.cpp:521`) *(inferred consequence: `last` edge state not updated then)*. The list's Cnt column shows `count`; `hits` is not shown. Neither is saved.

### 8.7 Actions (`xpeccy-plus/src/xcore/xcore.h:212-216`, `xpeccy-plus/src/ethread.cpp:302-332`, dialog `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:316-318`)

| Action | Effect |
|---|---|
| `BRK_ACT_DBG` "Debugger" | pause (`PR_DEBUG`) + open the debugger (`xpeccy-plus/src/ethread.cpp:323-326`) |
| `BRK_ACT_COUNT` "Counter" | increments `count`, emulation continues (`xpeccy-plus/src/ethread.cpp:309-310`) |
| `BRK_ACT_SCR` "Screen dump (ZX only)" | writes 0x1B00 bytes of RAM page 5 to `xpeccy_HHmmss_zzz_N.scr` in the screenshot dir, continues (`xpeccy-plus/src/ethread.cpp:311-322`) |
| `log` flag (orthogonal) | one `XLG_BRK` INFO line on every passing hit, whatever the action (`xpeccy-plus/src/ethread.cpp:306`; `xpeccy-plus/src/xcore/breakpoints.cpp:509-523`); see 11.2 |

After a non-debugger action on a pre-instruction break (fetch/IRQ), `brkskip` runs one instruction with `flgDBG = 1` so the breakpoint does not re-fire forever (`xpeccy-plus/src/ethread.cpp:327-331`, `:414-420`).

### 8.8 Management UI

**List dock** (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp`, `xpeccy-plus/ui/dbgwidgets/form_brkpoints.ui`), columns (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:97-121`, data `:66-95`):

| # | Header | Content |
|---|---|---|
| 0 | On | checkbox = `!off` |
| 1 | F | fetch (hidden for IRQ/IO/COND) |
| 2 | R | read (hidden for IRQ/COND) |
| 3 | W | write (hidden for IRQ/COND) |
| 4 | Addr | `brkGetString()`: `CPU:8000-8FFF`, `IO:00FE mask 00FF`, `RAM:01C000 [07:0000]`, `ROM:...`, `SLT:...`, `EXT:...`, `IRQ`, `COND[, on change]` (`:24-64`) |
| 5 | Cond | condition text |
| 6 | Cnt | `count` |

Click on a checkbox cell toggles and reinstalls (`:253-270`); double-click jumps the disassembler to a CPU/RAM/ROM breakpoint (`:272-284`; SLT/IO not handled). Sortable by any column (`xpeccy-plus/ui/dbgwidgets/form_brkpoints.ui:43-45`, `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:123-144`). Toolbar Add, Edit, Open, Save, Delete (`xpeccy-plus/ui/dbgwidgets/form_brkpoints.ui:66-127`, `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:633-640`), multi-select delete (`:670-681`). Context menu "Reset counter" zeroes `hits` and `count` (`:683-692`; `xpeccy-plus/ui/dbgwidgets/form_brkpoints.ui:141-147`); `brkActEdit` is defined in the .ui (`:136-138`) but not in any menu *(inferred: unused)*. `keyPressEvent` ignores all keys (`:249-251`): no Del/Enter/Space. No groups, names, tags or comments.

**Editor dialog** "Breakpoint editor" (`xpeccy-plus/ui/brkmanager.ui`, `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:288-617`): Type combo (7 types, `:293-299`); flags Fetch/Read/Write/On change (`xpeccy-plus/ui/brkmanager.ui:108-134`); bank (16K page) + start.off + start.abs + end.off + end.abs kept in sync (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:333-375`); IO mask; Action; Log; Condition + "?" help + live error label. Visible fields per type: `chaType()` (`:440-461`); limits follow bus/RAM/ROM/slot size (`:423-438`). A Value/Mask pair (`leValue`, `leValMask`, `xpeccy-plus/ui/brkmanager.ui:47-64`; ranges `xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:310-314`) is never enabled (`EL_VAL` not passed to `setElements()`) nor read by `confirm()`: an unimplemented data-value filter. Edit = delete old + add new (`confirmBrk`, `:659-664`); editing always re-enables (`:517`, `:578`); counters carried over (`:590-591`).

**Quick toggles:** disasm `XCUT_SETBRK` (Space) toggles a physical-cell fetch breakpoint, Shift+Space a CPU-address one, Alt = read, Ctrl = write (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1214-1252`; default `xpeccy-plus/src/xcore/keymap.cpp:504`); the code notes "modifiers doesn't work with new hotkeys" (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1241`); the Windows build has its own Shift test (`:1216-1222`); uses `brkXor(..., del=1)`, deleting the entry when all flags clear (`xpeccy-plus/src/xcore/breakpoints.cpp:218-235`). Disasm context menu Breakpoints > Fetch/Read/Write (`xpeccy-plus/src/xgui/debuga/debuger.cpp:821-827`, handler `chaCellProperty` `:2406-2445`) applies to the selected block as a physical range via `brkSet` (never deletes; *(inferred)* unticking all three leaves an empty entry). Dump context menu Fetch/Read/Write (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:670-701`) -> `DebugWin::brkRequest` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2369-2387`).

### 8.9 `.xbrk`, `bpx.ini`, CLI, persistence

- **`.xbrk`**: text, one line per breakpoint `TYPE:arg1:arg2:FLAGS:ACTION:condition` (`xpeccy-plus/src/xcore/breakpoints.cpp:611-613`, writer `:724-807`, reader `:615-722`).

| Field | Values |
|---|---|
| TYPE | `IO`, `CPU`, `ROM`, `RAM`, `SLT`, `IRQ`, `COND` |
| arg1 | CPU: `AAAA[-BBBB]`; page types: 16K page (hex byte); IO: port |
| arg2 | page types: `off[-off]`; IO: mask |
| FLAGS | letters `F`, `R`, `W`, `0` (off), `E` (on change), `L` (log) |
| ACTION | `DBG`, `SCR`, `CNT` |
| condition | last field, may contain colons |

  `;` lines are comments; hit counts not saved; loading replaces the list (`:624`). UI Open/Save with `*.xbrk` filter (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:694-713`).
- **Unreal `bpx.ini` import**: lines `x0=`, `r0=`, `w0=` with optional `-` range, `0x`/`#`/`$` hex or decimal; lines for CPU index ≠ 0 (GS Z80) skipped; x/r/w lines for the same range merged into one CPU breakpoint (`xpeccy-plus/src/xcore/breakpoints.cpp:555-609`). No export in that format.
- **CLI**: `--brk FILE` (`xpeccy-plus/src/main.cpp:60`, `:474-480`); `--bp ADR|LABEL` = CPU fetch breakpoint (`xpeccy-plus/src/main.cpp:58-59`, `:466-472`).
- **No automatic persistence**: `xpeccy-plus/src/xcore/config.cpp` saves nothing from `conf.brk.list` (only the `dbg.brk.txt` color, `xpeccy-plus/src/xcore/config.cpp:316`). Not in snapshots (8.4). `.xmap` stores the flag nibble but see 6.4.

## 9. Conditions & expressions (`xpeccy-plus/src/xcore/xexpr.{h,cpp}`)

C-like, compiled to RPN (`xpeccy-plus/src/xcore/xexpr.h:11-30`) by a precedence-climbing parser (`xpeccy-plus/src/xcore/xexpr.cpp:366-384`), evaluated on a 64-entry `unsigned` stack (`xpeccy-plus/src/xcore/xexpr.cpp:535-648`). Shared by breakpoint conditions and the Watcher (`xpeccy-plus/src/xcore/xexpr.h:8-9`, `xpeccy-plus/src/xcore/xexpr.cpp:652-665`).

**Operators** (`xpeccy-plus/src/xcore/xexpr.cpp:15-30`, tokens `:94-122`, unary `:337-362`):

| Priority | Operators |
|---|---|
| primary | `( )`; `[x]` little-endian word from memory; `M(x)` byte from memory; `RAY(x,y)` (`:197-218`, `:283-305`) |
| unary | `!` `~` `-` `+` |
| 11 | `a->b` = byte at a+b (`:377-379`) |
| 10 | `*` `/` `%` (division/modulo by 0 yields 0, `:626-627`) |
| 9 | `+` `-` |
| 8 | `<<` `>>` (shift ≥ 32 gives 0, `:630-631`) |
| 7 | `<` `>` `<=` `>=` (unsigned) |
| 6 | `==` `!=` `=` (`=` aliases `==`, `:115`) |
| 5 / 4 / 3 | `&` / `^` / `\|` |
| 2 / 1 | `&&` / `\|\|` (both operands always evaluated: RPN, no short-circuit *(inferred)*) |

**Literals** (`xpeccy-plus/src/xcore/xexpr.cpp:220-267`): decimal by default; `0x` or `#` hex; leading `0` octal; `'c'` character. A bare hex word like `FF` is never a number (`:261-263`). The header comment claiming machine-base numbers (`xpeccy-plus/src/xcore/xexpr.cpp:3`) is stale (`:244-265`).

**Name resolution** (`xpeccy-plus/src/xcore/xexpr.cpp:268-333`): `M(` / `RAY(` built-ins -> CPU register via `cpu_get_reg` by name (anything the core exposes, including hidden A, F, I, R, BC', IXL...) -> pseudo-variable -> label -> error. `.NAME` forces a register.

**Pseudo-variables** (`xpeccy-plus/src/xcore/xexpr.h:32-49`, values `xpeccy-plus/src/xcore/xexpr.cpp:513-533`):

| Name | Value |
|---|---|
| `RD`, `WR` | address of the last memory read / write in this instruction (instruction bytes excluded for RD, `xpeccy-plus/src/libxpeccy/spectrum.c:145-149`) |
| `MDT` | data of that read/write |
| `IN`, `OUT` | full 16-bit port of the last IN / OUT |
| `VAL` | data of that IN/OUT |
| `DOS` | TR-DOS ROM active |
| `SLOT0`..`SLOT3` | 16K page mapped at 0000/4000/8000/C000 |
| `FRAME` | frames since reset (`comp->frmCount`) |
| `RAYX`, `RAYY` | beam position between instructions |
| `HITS` | this breakpoint's hit counter |
| `RAY(x,y)` | 1 if the beam crossed dot (x,y) during this instruction (`xpeccy-plus/src/xcore/xexpr.cpp:603-623`) |

"Last event" variables are `-1` (FFFFFFFF) when no such access happened; reset per instruction by `comp_brk_newstep()` (`xpeccy-plus/src/libxpeccy/spectrum.c:959-972`, called `xpeccy-plus/src/ethread.cpp:535-536`); latched only while `flgCOND` is set, which `brkInstallAll()` sets when any condition or logging breakpoint exists (`xpeccy-plus/src/xcore/breakpoints.cpp:349-357`).

**Not available:** flag bits by name (only via `F`, e.g. `F & 0x40` *(inferred)*), T-state or instruction counters, port values other than the last IN/OUT, stack depth, disk/tape state; no assignment, side effects or strings.

**Semantics:** `brk_cond_true()` returns true on an empty condition, a compile error or an evaluation error ("a typo doesn't silently disable the breakpoint", `xpeccy-plus/src/xcore/breakpoints.cpp:147-158`). Labels and registers are looked up by name on every evaluation (`xpeccy-plus/src/xcore/xexpr.cpp:559-568`); conditions are recompiled on every `brkInstallAll()` (`xpeccy-plus/src/xcore/breakpoints.cpp:350`). UX: live `ok: <fully bracketed hex form>` / `error at pos` (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:465-489`); OK refuses a broken condition (`:574-577`); help dialog (`:491-512`).

## 10. Watches

### 10.1 Watcher window (`xpeccy-plus/src/watcher.{h,cpp}`)

Opened from the disasm toolbar "Watcher" (`actWutcha`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:653`, `:694`, `:700`; `xpeccy-plus/src/main.cpp:319`). Refreshed every frame via `s_watch_upd` from `MainWin::updateSatellites()` (`xpeccy-plus/src/emulwin.cpp:534`, `:1199-1200`; `xpeccy-plus/src/main.cpp:348`), only while visible (`xpeccy-plus/src/watcher.cpp:105`). Shows up to 32 register name/value pairs from the same `cpuGetRegs` table (`xpeccy-plus/src/watcher.cpp:34-42`, `:66-84`), the four mapped banks (`RAM:05`...) (`:116-119`) and a list of watches. Each watch = type CPU/RAM/ROM (`xpeccy-plus/src/watcher.h:12-16`, `xpeccy-plus/src/watcher.cpp:49-51`) + expression (same language via `xEval`, `xpeccy-plus/src/xcore/xexpr.cpp:652-665`), displayed as two rows: `TYPE: expr` + hex word, then 11 bytes from that address (`xpeccy-plus/src/watcher.cpp:244-279`). Watches live in a `QList` only (`xpeccy-plus/src/watcher.h:36`), no save/load; expressions recompiled on every cell repaint (`xpeccy-plus/src/watcher.cpp:256`); no change highlighting; register spin boxes have no write-back.

### 10.2 Watched ports (PORTS panel)

Up to `PWATCH_MAX = 16` port/mask entries (`xpeccy-plus/src/libxpeccy/spectrum.h:100`). Hit = `(bus & mask) == port`; last value latched on IN and OUT (`xpeccy-plus/src/libxpeccy/spectrum.c:280-287`, called `:408`, `:444`). If the machine table declares a register copy for the port, the value is read from the `Computer` field by offset (`REG_BYTE`/`REG_WORD`/`REG_32`) (`xpeccy-plus/src/libxpeccy/spectrum.c:316-361`). Machine-declared `xPortDsc` ports merged by `comp_pwatch_sync()` (`xpeccy-plus/src/libxpeccy/spectrum.c:316-346`): Pentagon `7FFD` (`xpeccy-plus/src/libxpeccy/hardware/pentagon.c:46-49`); ZX Evo BaseConf `7FFD, EFF7` (`xpeccy-plus/src/libxpeccy/hardware/pentevo.c:707-711`); TSConf `7FFD, EFF7, 01AF, 21AF` (`xpeccy-plus/src/libxpeccy/hardware/tslab.c:680-690`); also 128, +2, +3, Scorpion, Profi, ATM2, P1024, Phoenix. Panel rows: `getPortString(port, mask)` + value or `"--"` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2322-2345`), tooltip "Last value on the port" (`:2305-2306`). Edited via right-click "Watched ports..." -> `xPortWatchDialog`, hex port list with on/off ticks, `-7FFD` = off (`xpeccy-plus/src/xgui/debuga/debuger.cpp:773-775`, `:2270-2278`, `:2307-2308`; `xpeccy-plus/src/xgui/portwatch.cpp:12-18`, `:57`) and in Options (`xpeccy-plus/src/xgui/options/setupwin.cpp:1027`, `:1281`). Persisted as `ports = 7FFD,-1FFD,...` (`xpeccy-plus/src/xcore/config.cpp:278-280`; parser `xpeccy-plus/src/xcore/machines.cpp:1362`, `:1597`; format `:1404-1435`). Display only, no break action; values read-only.

### 10.3 Data watchpoints

No separate type: R/W memory breakpoints (8.2) plus conditions on `RD/WR/MDT/IN/OUT/VAL` (9).

## 11. Execution control, hotkeys, tracing

### 11.1 Execution control

| Action | Implementation |
|---|---|
| Step into (F7) | one `compExec()` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1024-1030`, `doStep` `:936-947`); holding F7 (auto-repeat) starts Trace, release stops it (`:1027-1029`, `:1109-1114`) |
| Step over (F8) | if the opcode has `OF_SKIPABLE`, temp fetch bit at `pc+len` in the physical page, resume; else a plain step (`:1031-1041`). Skipable: CALL/CALL cc, RST, DJNZ, HALT, LDIR/LDDR/CPIR/CPDR/INIR/INDR/OTIR/OTDR (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80nop.c:1092,1206,1294-1360`; `xpeccy-plus/src/libxpeccy/cpu/Z80/z80ed.c:823-835`; flag `xpeccy-plus/src/libxpeccy/cpu/cpu.h:100`) |
| Step out (F6) | sets `flgRetBRK`, `regCallCnt = 0`, resumes (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1042-1046`). Core counts CALL/RST (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80nop.c:40-51`, cap 65000) and accepted INT/NMI (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:119-121`, `:141-143`), decrements on RET/RETI/RETN; RET at depth 0 raises `IRQ_BRK` -> `brkt = -1` -> debugger (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80nop.c:53-64`, `xpeccy-plus/src/libxpeccy/spectrum.c:465-467`). Z80 only |
| Fast step (Alt+F7) | 10 single steps (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1047-1050`) |
| Run to cursor (F9) | only with disasm focus: temp fetch bit on the cursor's physical cell, full-speed resume (`:1051-1059`) |
| Set PC (End) | `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1200-1211` |
| Resume (Esc) | `stop()`: one instruction with `flgDBG = 1`, then clear (`xpeccy-plus/src/xgui/debuga/debuger.cpp:326-333`, `:1102-1105`) |

**Trace modes** (toolbar `tbTrace` and disasm cell menu, `xpeccy-plus/src/xgui/debuga/debuger.cpp:54-61`, `:607-610`, `:644-647`, `:838`; texts `xpeccy-plus/ui/dbgwidgets/form_disasm.ui:356-377,506`):

| Mode | Stops when |
|---|---|
| `DBG_TRACE_ALL` "Trace" | any key (`xpeccy-plus/src/xgui/debuga/debuger.cpp:995-998`) |
| `DBG_TRACE_INT` "Trace, stop on INT" | `intrq & inten` after a step (`:1156-1159`) |
| `DBG_TRACE_HERE` "Trace, stop here" | `PC == cursor address` (`:1160-1163`, `traceAdr = getAdr()` `:977`) |
| `DBG_TRACE_LOG` "Trace log" | any key; writes the log file (11.3) |

Trace runs one step per posted Qt event (`:979`, `:1165-1167`): GUI-bound and slow. *(inferred)* Trace, fast step and step into do not honor user breakpoints: `flgDBG = 1` while the debugger is open (`xpeccy-plus/src/xgui/debuga/debuger.cpp:298`), `compExec()` skips fetch/IRQ checks when `flgDBG` is set (`xpeccy-plus/src/libxpeccy/spectrum.c:900`), and `doStep` never looks at `flgBRK`.

**Not present:** run N instructions / frames / T-states, run to next frame or scanline, step back / reverse step, a run-until-return robust to stack tricks, skip instruction, stop on HALT, stop on unknown opcode (an `IRQ_PANIC` path exists, `xpeccy-plus/src/libxpeccy/spectrum.c:471-473`, nothing raises it).

### 11.2 Complete hotkey table

Defaults from `xpeccy-plus/src/xcore/keymap.cpp:436-518`, enum `xpeccy-plus/src/xcore/xcore.h:320-397`, groups `SCG_MAIN`, `SCG_DEBUGA`, `SCG_DISASM`, `SCG_DUMP` (`xpeccy-plus/src/xcore/xcore.h:399-405`). All `XCUT_` entries are rebindable and saved under `[KEYS]` (`xpeccy-plus/src/xcore/config.cpp:295-301`, `:713-714`). Debugger window keys are handled in `DebugWin::keyPressEvent` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:995-1107`), disasm keys in `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1149-1311`, dump keys in `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:434-496`.

| Group | XCUT_ | Config name | Default | Action |
|---|---|---|---|---|
| MAIN | `XCUT_PAUSE` | key.pause | Pause | pause (`xpeccy-plus/src/xcore/keymap.cpp:442`) |
| MAIN | `XCUT_REWIND` | key.rewind | Delete | "Rewind (hold)" (`xpeccy-plus/src/xcore/keymap.cpp:444`) |
| MAIN | `XCUT_NMI` | key.nmi | F10 | NMI (`xpeccy-plus/src/xcore/keymap.cpp:470`) |
| DEBUGA | `XCUT_OPTIONS` | key.options | F1 (Cmd+, on macOS) | Options (`xpeccy-plus/src/xcore/keymap.cpp:436-440`) |
| DEBUGA | `XCUT_DEBUG` | key.debuger | Esc | open / close debugger (`xpeccy-plus/src/xcore/keymap.cpp:441`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:1102-1104`) |
| DEBUGA | `XCUT_SAVE` | key.save | F2 | save file (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1020-1022`); disasm forwards it (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1211-1213`) |
| DEBUGA | `XCUT_LOAD` | key.load | F3 | open file, then go to PC |
| DEBUGA | `XCUT_KEYBOARD` | key.keywin | Alt+K | virtual keyboard |
| DEBUGA | `XCUT_SCRWIN` | key.scrwin | Alt+S | screen window (no case in the debugger switch *(inferred: handled elsewhere or unused there)*) |
| DEBUGA | `XCUT_SNDWIN` | key.sndwin | Alt+A | sound chip window |
| DEBUGA | `XCUT_RESET` | key.reset | F12 | reset |
| DEBUGA | `XCUT_STEPIN` | key.dbg.stepin | F7 | step; auto-repeat traces |
| DEBUGA | `XCUT_STEPOVER` | key.dbg.stepover | F8 | step over |
| DEBUGA | `XCUT_STEPOUT` | key.dbg.stepout | F6 | step out |
| DEBUGA | `XCUT_FASTSTEP` | key.dbg.faststep | Alt+F7 | 10 steps |
| DEBUGA | `XCUT_TMPBRK` | key.dbg.runtohere | F9 | run to cursor |
| DEBUGA | `XCUT_TRACE` | key.dbg.trace | Ctrl+T | trace (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1068-1070`) |
| DEBUGA | `XCUT_OPEN_DUMP` | key.dbg.opendump | Ctrl+O | load binary into memory |
| DEBUGA | `XCUT_SAVE_DUMP` | key.dbg.savedump | Ctrl+S | Save RAM dump dialog (`xpeccy-plus/src/xcore/keymap.cpp:494`) |
| DEBUGA | `XCUT_OPEN_XMAP` | key.dbg.openxmap | (none) | load .xmap |
| DEBUGA | `XCUT_SAVE_XMAP` | key.dbg.savexmap | (none) | save .xmap |
| DEBUGA | `XCUT_FINDER` | key.dbg.finder | Ctrl+F | memory finder |
| DEBUGA | `XCUT_LABELS` | key.dbg.labels | Ctrl+L | toggle label display |
| DEBUGA | `XCUT_LABLIST` | key.dbg.lablist | (none) | labels list window |
| DEBUGA | `XCUT_DBG_RELOAD` | key.dbg.reload | (none) | reload snapshot and labels |
| DISASM | `XCUT_TOPC` | key.disasm.topc | Home | go to PC |
| DISASM | `XCUT_SETPC` | key.disasm.setpc | End | set PC to cursor |
| DISASM | `XCUT_SETBRK` | key.disasm.setbrk | Space | toggle breakpoint (Shift = CPU address, Alt = read, Ctrl = write) (`xpeccy-plus/src/xcore/keymap.cpp:504`) |
| DISASM | `XCUT_JUMPTO` | key.disasm.jump | F4 | follow operand |
| DISASM | `XCUT_RETFROM` | key.disasm.ret | F5 | history back (navigation only, not an execution return) |
| DISASM | `XCUT_GOTOADR` | key.disasm.goto | G | edit address |
| DUMP | `XCUT_DUMP_GOTOADR` | key.dump.goto | G | edit row-0 address |
| DUMP | `XCUT_DUMP_REG_PC` | key.dump.goto.pc | Ctrl+P | dump at PC |
| DUMP | `XCUT_DUMP_REG_SP` | key.dump.goto.sp | Ctrl+S | dump at SP (`xpeccy-plus/src/xcore/keymap.cpp:511`) |
| DUMP | `XCUT_DUMP_REG_BC` | key.dump.goto.bc | Ctrl+B | dump at BC |
| DUMP | `XCUT_DUMP_REG_DE` | key.dump.goto.de | Ctrl+D | dump at DE |
| DUMP | `XCUT_DUMP_REG_HL` | key.dump.goto.hl | Ctrl+H | dump at HL |
| DUMP | `XCUT_DUMP_REG_IX` | key.dump.goto.ix | Ctrl+X | dump at IX |
| DUMP | `XCUT_DUMP_REG_IY` | key.dump.goto.iy | Ctrl+Y | dump at IY |

Hard-coded (not remappable): disasm Ctrl+C copy, Ctrl+1..5 / Alt+1..5 marks, Up/Down/PgUp/PgDn with Ctrl variants for Up/Down, Enter edit; dump Up/Down, PgUp/PgDn, Enter, F2 pass-through (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:443-491`); CPU field Up/Down/PgUp/PgDn/Insert/X (`xpeccy-plus/src/xgui/classes.cpp:346-385`).

### 11.3 Trace log and breakpoint log formats

- **Trace log file** (`DBG_TRACE_LOG`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:958-975`, `:1126-1151`): `|`-separated, header row of register names, then per step `"PC"|mnemonic|"reg"|...`. PC and mnemonic are taken before the step, registers after it.
- **Breakpoint log line** (`XLG_BRK`, INFO; `xpeccy-plus/src/xcore/breakpoints.cpp:390-523`): place (`CPU:addr RAM:pg:off`, `IO:port`, `IRQ`, `COND`), access (`F`, `RD a=v`, `WR a=v`, `IN=v`, `OUT=v`), `n=count/hits`, condition text, every register (PC = the instruction's own PC via `brkpc`), flag letters, `ROMPG RAMPG DOS ROM CPM INT`. Requires the log enabled (`xlog_on(XLG_BRK, XLL_INFO)`, `:511`); `xpeccy-plus/src/xcore/log.cpp:361-363` reinstalls breakpoints when log settings change.
- No ring buffer of recent PCs, no "last N instructions" view, no branch history.

## 12. History / rewind / time travel

`xpeccy-plus/src/xcore/rewind.{h,cpp}`: a snapshot every `conf.emu.rewind.step` frames, page-deduplicated (4 KB pages, refcounted, cap 512 MB) (`xpeccy-plus/src/xcore/rewind.cpp:17-38`, `:22-23`, `:150`, `:320-324`); played backwards with reversed audio while Delete (`XCUT_REWIND`) is held in the main window (`xpeccy-plus/src/emw_keys.cpp:226-227`, `:466-467`; `xpeccy-plus/src/xcore/rewind.cpp:298-360`); releasing resumes from the snapshot shown. History is dropped when media is written (`xpeccy-plus/src/xcore/rewind.cpp:97-103`) or the machine changes (`:299-304`).

**Relation to the debugger: none.** No file under `src/xgui/debuga/` references rewind or xstate. `rewind_frame()` runs only at frame end in `emuCycle` (`xpeccy-plus/src/ethread.cpp:477`), which does not run while the debugger holds the machine. `rewind_load`/`rewind_restore` are used only by the `--bench-rewind` self-check (`xpeccy-plus/src/ethread.cpp:808-829`). No reverse step, no reverse-continue, no "rewind to frame N" UI. Breakpoints do not fire during rewind playback (the loop runs `rewind_play` instead of `compExec`, `xpeccy-plus/src/ethread.cpp:403-415`).

## 13. Machine state: MEMMAP, PORTS, SIGNALS, FRAME, RAY

### 13.1 MEMMAP (`xpeccy-plus/ui/dbgwidgets/form_misc.ui`)

| Item | Detail | Source |
|---|---|---|
| Four 16K windows | combo ROM/RAM + hex page spin, tooltips `0000..3FFF` etc. | `xpeccy-plus/src/xgui/debuga/debuger.cpp:776-806`; `xpeccy-plus/ui/dbgwidgets/form_misc.ui:30-72` |
| Value | `pg = mem->map[i<<6]` (first 256-byte page of the window), page = `pg.num >> 6` | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2023-2045` |
| Edit | `memSetBank(mem, bank<<6, type, page, MEM_16K, ...)`: writes the live map, holds until the machine repages | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2047-2057` |
| Forced marker | window lit / combo bold while the forced value is still mapped | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2015-2021`, `:2031-2041` |
| Restore mapping | right-click on the block or title restores the 256-entry map saved on debugger open | `xpeccy-plus/src/xgui/debuga/debuger.cpp:262-281`, `:800-806` |
| Visibility | Options `show.memmap` | `xpeccy-plus/src/xgui/debuga/debuger.cpp:2282-2283`; `xpeccy-plus/src/xcore/config.cpp:271` |

### 13.2 PORTS

See 10.2.

### 13.3 SIGNALS, FRAME, RAY

| Label | Meaning | Source |
|---|---|---|
| DOS | bold when `flgDOS` (TR-DOS ROM paged) | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1209`; `xpeccy-plus/src/libxpeccy/hardware/common.c:66`, `xpeccy-plus/src/libxpeccy/spectrum.c:683` |
| ROM | bold when `flgROM` (7FFD bit 4 on 128/Pentagon) | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1210`; `xpeccy-plus/src/libxpeccy/hardware/pentagon.c:13`, `xpeccy-plus/src/libxpeccy/hardware/zx128.c:19` |
| CPM | bold when `flgCPM` | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1211` |
| INT | bold when `intrq & inten` | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1212` |
| FRAME | `comp->frmCount` (same counter as the `FRAME` condition variable); right-click "Reset counter" | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1219`, `:811-818`; `xpeccy-plus/src/libxpeccy/spectrum.h:160` |
| RAY X / Y | `vid->ray.x/y` decimal; X bold in HBLANK, Y bold in VBLANK; measured from the top-left of the visible image | `xpeccy-plus/src/xgui/debuga/debuger.cpp:1214-1217`; `xpeccy-plus/src/libxpeccy/video/vidcommon.h:23-24` |

Blocks can be hidden via `show.ports/signals/ray/frame` (`xpeccy-plus/src/xcore/config.cpp:271-275`, defaults on `:656-660`; `setMiscBlocks`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:2282-2297`). Not shown: video mode, ULA/border register, ULA+ state, TSConf/Evo video registers, contention, INT length/position, NMI, beam position in T-states, `hCount`/`fCount` (`xpeccy-plus/src/libxpeccy/spectrum.h:163-164`), turbo/CPU frequency.

## 14. Device views

### 14.1 Sound chip (`xpeccy-plus/src/xgui/debuga/dbg_sndchip.{h,cpp}`, `xpeccy-plus/ui/dbgwidgets/form_psg.ui`, `xpeccy-plus/ui/dbgwidgets/form_fm.ui`)

Tabs from `ts_chip(conf.zx->ts, 0..3)`: `PSGn` per non-`SND_NONE` chip, plus `FMn` for `SND_YM2203` (`xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:1013-1029`; `xpeccy-plus/src/libxpeccy/sound/ayym.h:11-15`); rebuilt when the chip signature changes (`:1074-1078`). SAA1099, General Sound, Soundrive/Covox not shown (only `aymChip` via `ts_chip`, `xpeccy-plus/src/libxpeccy/sound/ayym.h:252`).

**PSG page:**

| Field | Content | Editable | Source |
|---|---|---|---|
| Regs | R0..R15 as 2x8 hex fields | yes, `ay_poke_reg` under `emu_lock` (acts like a port write) | `xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:~285-300`, `:365-369`, `:254-258` |
| Period A/B/C | `(R(2n+1)<<8 \| R2n) & 0xFFF` | yes | `:303`, `:374-391`, `:436-437` |
| Period N | `R6 & 0x1F` | yes | `:380-382`, `:440` |
| Period E | `R12:R11` | yes | `:383-386`, `:441` |
| Vol A/B/C | `R8..R10 & 0x0F`, envelope bit preserved | yes | `:394-398`, `:438` |
| Mixer A/B/C | `"TNE"` string | no | `getAYmix` `:185-190`, `:452` |
| Lev A/B/C | held: instantaneous `ay_chan_lev` 0..31 bar+figure; running: peak `ay_chan_peak` with fall-off | no | `:456-463`; `xLevelCell` `:94-160` |
| State A/B/C, N | tone / noise generator bit, only while held | no | `:453`, `:465` |
| E vol | `chanE.vol & 0x1F` | no | `:468` |
| Envelope shape | plot of `eForm` via `ay_env_shape` | no | `xAYEnvView` `:192-231`, `:469` |
| Mute A/B/C | `chan->mute`, debugger-only silence | yes | `:402-408`; `xpeccy-plus/src/libxpeccy/sound/ayym.h:92` |

**FM page** (YM2203 only; blanks otherwise, `:659-663`): Timer A `(R24<<2)|(R25&3)` + R27 bit 0 (`:668-673`); Timer B R26 + R27 bit 1 (`:674-676`); Ch3 mode R27 b7:6 off/special/CSM/? (`:678-679`); channel tabs 1/2/3 (`:533-541`, `:587-596`); "off" = `fm_off[ch]` (`:619-624`; `xpeccy-plus/src/libxpeccy/sound/ayym.h:184`); Alg, Fb `(RB0+c >> 3) & 7` (`:681-682`); Bk/Fq from RA4/RA0 (`:683-685`); Out (`:686`). Operator table Op1..Op4 (`:497-517`, carriers `:521`, draw `:690-706`, edit `:628-636`): editable DT (R30 b6:4), MUL (R30 b3:0), TL (R40 &7F), RS (R50 b7:6), AR (R50 &1F), DR (R60 &1F), SL (R80 b7:4), SR (R70 &1F), RR (R80 &0F), EG/SSG-EG (R90 &0F); readouts St (OFF/ATK/DEC/SUS/REL), Bk/Fq, Lev (`0x3FF - att`), carrier `*`. Writes via `ym2203_poke_reg` under `emu_lock` (`:260-264`); state copied from `ym2203_fm_view` per refresh (`:666`).

**Footer:** "Wave" oscilloscope of the mixer, 40 ms window running / 2 ms held (`:714-715`), min/max outline + smoothed average, fixed or Fit (octave steps) scale, optional Log, peak + timebase label (`:717-980`, `:940-965`); "Beeper" bar of `beep->val` (`:1084-1088`, `drawHBar` `:19-31`). Refresh: docked on fills only; detached every emulated frame, every 10th in fast mode (`XSCR_FAST_EVERY`) (`xpeccy-plus/src/xgui/sndwin.cpp:58-63`; `xpeccy-plus/src/xcore/xcore.h:812`). Focused fields with unsaved edits are not overwritten (`putValue`, `:240-250`); meaningless readouts are dashed when running (`machineHeld()` = `flgDBG || conf.emu.pause`, `:173-175`). Not shown: AY address latch, chip type/clock/stereo, port A/B semantics, YM2203 SSG prescaler, key-on state (0x28), no recording.

### 14.2 Detached windows

Dock shows "The sound chips are in a window of their own" / "The screen is in a window of its own" with "Bring it back" (`xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp` ctor; `xpeccy-plus/src/xgui/debuga/dbg_zxscr.cpp:~543-566`).

### 14.3 CMOS / RTC (`xpeccy-plus/src/xgui/debuga/dbg_cmos_dump.{h,cpp}`)

32 rows x (address + 8 bytes) = 256 bytes of `cmos.data[]` (`xpeccy-plus/src/xgui/debuga/dbg_cmos_dump.cpp:8-35`; `xpeccy-plus/src/libxpeccy/cmos.h:13`), every byte editable, writes `data[adr]` directly (`:39-63`). Refresh via `QTableView::update()` without `dataChanged` (`:87-89`) *(inferred: relies on a repaint re-querying the model)*. Shown for every machine.

### 14.4 FDC (`xpeccy-plus/src/xgui/debuga/dbg_fdd.cpp`)

One `FDC` struct serves WD1793 (`DIF_BDI`) and uPD765 (`DIF_P3DOS`) (`xpeccy-plus/src/libxpeccy/fdc.h:8-13`, `:37-103`); identical view, all read-only `QLabel`s:

| Label | Field | Line |
|---|---|---|
| BUSY | `!fdc->idle` | `:12` |
| COM | `fdc->com` or `--` when idle | `:13` |
| IRQ | `fdc->irq` (WD INTRQ / uPD765 exec) | `:14`; `xpeccy-plus/src/libxpeccy/fdc.h:41` |
| DRQ | `fdc->drq` | `:15` |
| TRK / SEC / DATA | WD registers | `:16-19` |
| HEAD | `fdc->side` | `:18` |
| SR | `fdc->state` | `:20` |
| SR0 / SR1 / SR2 | uPD765 status | `:21-23` |
| CRC | `fdc->crc` | `:24` |
| INT | `fdc->intr` | `:25` |
| DMA | `fdc->dma` ("not implemented yet", `xpeccy-plus/src/libxpeccy/fdc.h:87`) | `:26` |
| IE | `dif->inten` | `:27` |
| FLP | drive letter A-D | `:29` |
| RDY | disk in, door closed | `:30` |
| TRK (drive) | `flp->trk` | `:31` |
| POS | `flp->pos` | `:32` |
| IDX | index pulse | `:33` |
| DATA (drive) | `flpRd(flp, side)` or `--` | `:34` |
| MOT | motor | `:35` |

Not shown: SR3, uPD765 command/result buffers (`xpeccy-plus/src/libxpeccy/fdc.h:93-98`), `fcrc`, HD mode, step direction, Beta Disk #FF system register, non-selected drives, write-protect.

### 14.5 Disk track dump "FDD" (`xpeccy-plus/src/xgui/debuga/dbg_diskdump.{h,cpp}`, `xpeccy-plus/ui/dbgwidgets/form_fdddump.ui`)

Raw track, not sectors. Controls: drive A-D, track spin (slot `(cyl<<1)|side`, max 86 or 43 cylinders by `trk80`, step 2 single-sided), bytes per row Auto/8/12/16, target button (`xpeccy-plus/src/xgui/debuga/dbg_diskdump.cpp:5-12`, `:243-270`; `xpeccy-plus/ui/dbgwidgets/form_fdddump.ui:39-121`). Rows `"TT:OOOO"` + bytes + ASCII (`:209-236`); row count = track length / row bytes (`:126-133`). ID / data / CRC bytes colored `dbg.disk.id/.data/.crc` (`:168-190`); byte under the head bold (`:191-201`); Target jumps to the head (`:277-283`, `:37-39`); empty drive shows `FF` (`:227-231`). Read-only (`xpeccy-plus/src/xgui/debuga/dbg_diskdump.h:10-21`); no sector decoding, TR-DOS catalog, export or search.

### 14.6 Tape (`xpeccy-plus/src/xgui/debuga/dbg_tape.cpp`)

| Item | Content | Line |
|---|---|---|
| Tape in | bar of `volPlay` (0..256) | `:17` |
| Tape out | bar of `levRec` (0/1) | `:18` |
| Current | remaining µs of the current signal (`tape_sig_len`) | `:19`; `xpeccy-plus/src/libxpeccy/tape.c:694-696` |
| State | stop / play / rec | `:20` |
| Pos | `tape->pos - 1` = signal index inside the current block | `:21`; `xpeccy-plus/src/libxpeccy/tape.h:163` |
| Diagram | 330x100 px, 20 µs/px, amplitude across block boundaries, red cross-hair at the current position | `:22-101` |

Read-only; no block list, block number, block type/length/checksum or transport (those are in the main-window tape window, `xpeccy-plus/src/emulwin.cpp:1188-1197` *(inferred as the place)*).

### 14.7 Palette (`xpeccy-plus/src/xgui/debuga/dbg_palette.{h,cpp}`)

16x16 grid of `vid->pal[]` (final host RGB, ABGR `uint32_t`, `xpeccy-plus/src/libxpeccy/video/video.h:137`) (`xpeccy-plus/src/xgui/debuga/dbg_palette.cpp:12-44`); click shows Index and decimal R/G/B + preview (`:46-65`); read-only (`xpeccy-plus/ui/dbgwidgets/form_palette.ui:57-93`). Not the hardware palette (ULA+ 64 GRB332, TSConf CRAM, ATM/Evo port FF, Profi); no `bpal`/`gpal` view (`xpeccy-plus/src/libxpeccy/video/video.h:138-139`); no export.

## 15. Video: screen viewer and sprite scanner

### 15.1 Screen viewer (`xpeccy-plus/src/xgui/debuga/dbg_zxscr.{h,cpp}`)

| Mode (`XSCR_*`, `xpeccy-plus/src/xcore/xcore.h:800-805`; buttons `xpeccy-plus/src/xgui/debuga/dbg_zxscr.cpp:347-353`) | Page |
|---|---|
| Auto | `vid->vidPage` (on air) (`pageFor`, `:77-86`) |
| Main | 5 |
| Shadow | 7; disabled below 128K or with a bus ≤ 64K (`scr_has_shadow`, `:25-28`, `:507-513`) |
| Both | 5 and 7 side by side or stacked, whichever is larger (`geom()`, `:128-161`) |
| Custom | any 16K page 0..FF at offset 0..3FFF (`:355-360`, `:432-437`) |

Only the standard 256x192 bitmap + 32x24 attributes are decoded (`vid_get_screen`, `xpeccy-plus/src/libxpeccy/video/video.c`); ULA+ colors via `vid_zxcol`, flash checkbox disabled under ULA+ (`xpeccy-plus/src/xgui/debuga/dbg_zxscr.cpp:520`); no hi-res/hi-color/Timex/ATM/TSConf/Profi/Evo modes. Border strip in the current border color (`vid_brd_col`, `:168-175`); caption names the page and highlights the on-air page (`:184-188`). Options persisted in `conf.dbg.*`: zoom Fit / x1..x4 (`:340-342`, `XSCR_ZOOMMAX 4`), no pixels (attributes only), no colors, no flash, show grid (`:417-426`, `:522-530`). Hover readout: XY pixels, char cell, pixel-byte address, bit number, attribute address (`show_dot`, `:456-481`); click holds the readout and marks the cell (`:254-261`); typing into Screen:/Attr: marks the cell (`:428-430`, `markAdr` `:265-282`); right-click copies screen/attribute address (`:317-327`); clicking the main window in debug mode feeds a dot (`xpeccy-plus/src/emw_mouse.cpp:13-16` -> `DebugWin::showScrDot`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1257-1259`). Addresses assume page 5 at #4000 and other pages at #C000 (`vid_scr_base`, `xpeccy-plus/src/libxpeccy/video/video.c:605-607`). Data: running = copy at the last frame boundary (`vid_scr_snap_get`), held = live memory (`:88-110`). No image export.

### 15.2 Sprite scanner (`xpeccy-plus/src/xgui/debuga/dbg_sprscan.{h,cpp}`, `xpeccy-plus/ui/memviewer.ui`)

Separate dialog "Memory sprite scanner", from the disasm toolbar (`actSprScan -> doMemView`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:698`, `:2620-2626`), initial page = page at #C000. Renders memory as 1 bpp (set bit = light) in a 256x256 image scaled to 512x512 (`xpeccy-plus/src/xgui/debuga/dbg_sprscan.cpp:75-152`). Parameters: Addr 0..FFFF, Width 1..256 bytes (32 columns fit, horizontal scrollbar beyond), Height 1..32 char rows (x8 lines), Page 0..255 used for addresses ≥ #C000 (`ramData[(page<<14)|(adr&0x3FFF)]`), lower addresses via the live map with `memRd` (`:63-73`; `xpeccy-plus/ui/memviewer.ui:45-115`). Options: Inversion, Grid, "ZX screen" (Spectrum line interleave, 32x192), "Columns" (strip repeated side by side, alternating color) (`:19-22`, `:98-138`). Wheel = one char row (`width*8` bytes) (`:31-39`). "Save" writes raw bytes (w x h x 8, or 0x1800 in ZX-screen mode) (`:41-61`). Redrawn in `fillNotCPU` when visible (`xpeccy-plus/src/xgui/debuga/debuger.cpp:1221-1222`) and on parameter change.

## 16. Profiling: heat map and runtime mapping

### 16.1 Heat map (`xpeccy-plus/src/libxpeccy/heatmap.{h,c}`, `xpeccy-plus/src/xgui/debuga/dbg_heat.{h,cpp}`)

- **Collection:** three 32-bit saturating counters per byte, rd/wr/ex (`xpeccy-plus/src/libxpeccy/heatmap.h:10-15`, `xpeccy-plus/src/libxpeccy/heatmap.c:47-48`), per physical byte of RAM and ROM sized to `ramMask+1`/`romMask+1` (`xpeccy-plus/src/libxpeccy/heatmap.c:51-54`); SLOT/EXT/IO not counted (`:72-78`). Read hook: exec if the address is `PC-1` (opcode, prefix, displacement or immediate just fetched), else read (`xpeccy-plus/src/libxpeccy/spectrum.c:97-120`); write hook -> write (`xpeccy-plus/src/libxpeccy/spectrum.c:169-173`). Not counted in run-ahead frames; I/O not tracked. Off by default; "Collect" sets `flgHEAT` (`xpeccy-plus/src/xgui/debuga/dbg_heat.cpp:611-616`), not saved in config *(inferred from grep)*. "Reset all counters" with confirmation (`:618-622`); also cleared on every `comp_snap_reset` (`xpeccy-plus/src/libxpeccy/spectrum.c:749-760`).
- **View:** source CPU (64K through paging, four 64x64 blocks, 4 bytes per cell on 16-bit buses), RAM page n, ROM page n (16K raster, 256 bytes/row, cells 1..8 px, scrollable) (`xpeccy-plus/src/xgui/debuga/dbg_heat.h:9-19`; `xpeccy-plus/src/xgui/debuga/dbg_heat.cpp:89-91`, `:141-161`, `:563-565`, `:590-601`). Counter All / Read / Write / Exec (`:566-569`). Coloring categorical: in All, blue if `ex > wr+rd`, else red if `wr > rd`, else green; grey untouched; single-channel modes show that color if non-zero (`:59-72`, colors `:19-25`). Hover: address (or 4-byte range), type RAM/ROM/SLT/IO, page:offset, three counts (`:401-431`, legend `:481-535`); "collecting is off" hint (`:328-332`). Double-click jumps the disassembler (RAM/ROM via `memFindAdr` only if mapped) (`:439-449`; `xpeccy-plus/src/xgui/debuga/debuger.cpp:716`).
- **Export:** CSV `type,page,offset,read,write,exec`, every RAM then ROM byte, decimal (`xpeccy-plus/src/xgui/debuga/dbg_heat.cpp:624-631`; `xpeccy-plus/src/libxpeccy/heatmap.c:105-123`).
- Refresh only when the debugger fills.

### 16.2 Runtime mapping

"Runtime mapping" toolbar option sets `flgMAP` when the debugger closes (`xpeccy-plus/src/xgui/debuga/debuger.cpp:335`): executed bytes get `DBG_VIEW_EXEC`, other read bytes without a type get `DBG_VIEW_BYTE` (`xpeccy-plus/src/libxpeccy/spectrum.c:98-114`). "Clear mapping" wipes the type nibble (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2195-2206`); `mapAuto()` is an empty stub (`:2208-2210`). Persisted via `.xmap`.

## 17. Import / export and persistence summary

| Item | Persisted? | Format / place |
|---|---|---|
| Breakpoints | only manually | `.xbrk` (8.9); `bpx.ini` import; `--brk`, `--bp`; not in config or snapshots |
| Labels | manually | `BB:AAAA NAME` text; `.xmap`; `-l`, `--xmap` |
| Comments, view types | manually | `.xmap` only |
| Watched ports | yes | config `ports = ...` |
| Watcher expressions | no | — |
| History, marks | no | — |
| Show labels / Show segment | no | forced in constructor (`xpeccy-plus/src/xgui/debuga/debuger.cpp:582-583`) |
| Stack offset | yes | `stack.offset` |
| Screen viewer options | yes | `conf.dbg.*` |
| Dock layout | yes | `<confdir>/debuga.layout` |
| Hotkeys | yes | `[KEYS]` |
| Heat map | CSV export only | — |
| Memory | Binary / Hobeta / TR-DOS file; raw binary load | 7.4 |
| Disassembly | source text with `ORG` | 4.8 |
| Trace | pipe-separated file | 11.3 |
| Sprite data | raw bytes | 15.2 |

Limits: breakpoints unbounded (`xpeccy-plus/src/xcore/xcore.h:605`); one breakpoint per address (maps hold one flag set and one pointer per cell; `xpeccy-plus/src/xcore/breakpoints.cpp:326-331`; `res/help/cond-syntax.html` "An address holds one breakpoint"); one IRQ breakpoint (`xpeccy-plus/src/xcore/breakpoints.cpp:21`, `:37-40`); expression stack 64 (`xpeccy-plus/src/xcore/xexpr.cpp:535`); 16 watched ports; step-out depth 65000, then silently disarms (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80nop.c:45-49`); maps 4 MB RAM / 512 KB ROM (`xpeccy-plus/src/libxpeccy/spectrum.h:231-232`).

## 18. Notable and unique ideas

1. **`RAY(x,y)` in conditions** — break when the beam crosses a given dot during an instruction (`xpeccy-plus/src/xcore/xexpr.cpp:603-623`), plus `RAYX`/`RAYY`/`FRAME`: raster-aware breakpoints without a dedicated beam UI; directly relevant to multicolor/border effects.
2. **Per-instruction access variables** `RD`/`WR`/`MDT`/`IN`/`OUT`/`VAL` — any breakpoint or global condition can filter on the data actually moved, e.g. "write of 0 to 5800-5AFF", "OUT to FE with bit 4 set" (`xpeccy-plus/src/xcore/xexpr.cpp:513-533`).
3. **Physical-cell breakpoints as the default** — Space toggles a RAM/ROM/slot-page breakpoint, CPU-address is the Shift variant; temp breakpoints for F8/F9 also go into the physical page (`xpeccy-plus/src/libxpeccy/spectrum.c:982-998`). Bank-aware by construction.
4. **Flat per-byte flag maps with a global fast-path gate** — one byte lookup per access, and the watched path is entered only when a flag, heat or condition is active (`xpeccy-plus/src/libxpeccy/spectrum.h:260-262`). The same byte also carries the disassembler view type.
5. **Breakpoint log lines with full machine state** — `XLG_BRK` records place, access, counters, all registers and paging signals, turning any breakpoint into a non-stopping tracepoint (`xpeccy-plus/src/xcore/breakpoints.cpp:390-523`).
6. **Screen-dump action** — a breakpoint can write the screen to `.scr` and continue (`xpeccy-plus/src/ethread.cpp:311-322`): frame capture at a code point.
7. **`HITS` inside the condition plus separate `hits`/`count`** — skip-N and limit-N without extra fields (`xpeccy-plus/src/ethread.cpp:510-514`).
8. **Edge-triggered global conditions ("On change")** (`xpeccy-plus/src/xcore/breakpoints.cpp:181`).
9. **Fail-open conditions** — a broken condition still breaks (`xpeccy-plus/src/xcore/breakpoints.cpp:147-158`); with live `ok: <bracketed form>` validation.
10. **`.xmap` project file** — flag maps, all label sets and comments, page-aware, in one chunked container (`xpeccy-plus/src/xcore/xmap.cpp`).
11. **Runtime mapping** — execution marks code vs. data view types automatically (`xpeccy-plus/src/libxpeccy/spectrum.c:98-114`).
12. **Heat map with categorical R/W/X coloring, per physical byte, CSV export** (`xpeccy-plus/src/xgui/debuga/dbg_heat.cpp:59-72`, `:624-631`).
13. **Machine-declared watched ports** read from the machine's latch copies (`xpeccy-plus/src/libxpeccy/spectrum.c:316-361`).
14. **Taken-branch arrows and PC-row operand values; F4 on RET follows (SP)** (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:379-405`; `xpeccy-plus/src/libxpeccy/cpu/Z80/z80.c:303-311`).
15. **Detachable Screen and Sound windows that stay live at frame rate**; Sound panel with per-channel mute and FM operator editing (`xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:402-408`, `:628-636`).
16. **Disk track dump with ID/data/CRC coloring and head-position byte** (`xpeccy-plus/src/xgui/debuga/dbg_diskdump.cpp:168-201`).
17. **Unreal `bpx.ini` import** (`xpeccy-plus/src/xcore/breakpoints.cpp:555-609`).

## 19. Gaps and caveats

All items are from source reading, not runs; *(inferred)* as marked.

### 19.1 Breakpoints

1. **IO breakpoints clobber each other.** `brkIOMap[adr] = 0` is written for every matching port, and the `if (comp->brkIOMap[adr]) map[BRK_IOPORT][adr] = brk` check sits outside the mask test, so each IO breakpoint re-points all flagged ports to itself; with two IO breakpoints, hits on the first are attributed to the second (condition, action, counter) (`xpeccy-plus/src/xcore/breakpoints.cpp:286-299`) *(inferred)*.
2. **Sorting the list corrupts hit attribution.** `conf.brk.map` holds raw pointers into the vector; `xBreakListModel::sort()` reorders without `brkInstallAll()` (`xpeccy-plus/src/xgui/debuga/dbg_brkpoints.cpp:133-144`) *(inferred)*.
3. **Overlapping memory breakpoints:** the later entry (list sorted by start, `xpeccy-plus/src/xcore/breakpoints.cpp:191-192`) overwrites the earlier one's nibble and pointer; a disabled one clears an enabled one's flags (`xpeccy-plus/src/xcore/breakpoints.cpp:277-282`, `:326-331`) *(inferred)*.
4. **`brkFind` ignores range end for page types** (only CPUADR compares `eadr`, `xpeccy-plus/src/xcore/breakpoints.cpp:21-40`): Space on a cell that starts an existing RAM range toggles the whole range *(inferred)*.
5. **Dump-view breakpoints lose their range and misuse `mask`**: `brkRequest` -> `brkSet(..., bgn, end)` sets `eadr = adr` and stores `end` in `mask` (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2369-2387`, `xpeccy-plus/src/xcore/breakpoints.cpp:118-122`, `:128`); it also compares the dump's physical address with the disasm block's CPU-address bounds (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2371`) *(inferred)*.
6. **`BRK_MEMEXT`** can be created from the dump but is never installed or saved (8.2).
7. **One hit per instruction**: `brkt/brka/brkev` are overwritten by each matching access, so only the last of two watched accesses is evaluated (`xpeccy-plus/src/libxpeccy/spectrum.c:121-130`, `:183-190`) *(inferred)*.
8. **R breakpoints also fire on opcode/operand fetches** of the byte (8.3) *(inferred)*.
9. **IRQ breakpoint vs. trace-on-INT inconsistency**: the breakpoint tests `intrq` without `inten`/IFF1 (fires repeatedly under DI), "Trace, stop on INT" uses `intrq & inten` (`xpeccy-plus/src/libxpeccy/spectrum.c:918`, `xpeccy-plus/src/xgui/debuga/debuger.cpp:1157`) *(inferred)*.
10. **Step over / run to cursor use the physical page mapped now** (`getBrkPtr`, `xpeccy-plus/src/libxpeccy/spectrum.c:982-998`): if the callee remaps the window the return is missed and execution runs free; F8 on `RST 08`/`RST 28` (inline data) sets the bit at `pc+1`, never executed; `getBrkPtr` returns a dummy byte for EXT, so F8/F9 there silently run free *(inferred)*.
11. **Stepping, trace and fast step ignore user breakpoints** (11.1) *(inferred)*.
12. Editing a breakpoint re-enables it; the Value/Mask data filter is dead UI; `brkActEdit` unused; `list_sys` and `temp` unused; `BRK_HBLANK` unimplemented; context-menu unticking leaves empty entries *(inferred)*; the list swallows no keys; SLT/IO double-click does nothing.
13. Breakpoints are not saved with config or snapshot; do not round-trip through `.xmap` *(inferred)*; hit counts never saved.
14. Screen-dump action always dumps RAM page 5, ignoring the 128K shadow screen (page 7) *(inferred gap)*.
15. Global conditions are skipped on instructions where an address breakpoint fired, so the edge state is not updated *(inferred)*.
16. Space modifiers (Alt/Ctrl) reportedly do not work with the new hotkey system (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:1241`).
17. Step-out silently disarms after call depth 65000 (`xpeccy-plus/src/libxpeccy/cpu/Z80/z80nop.c:45-49`); Z80 only.

### 19.2 Expressions and watches

18. Stale doc comment: `xpeccy-plus/src/xcore/xexpr.cpp:3` claims machine-base numbers, code is decimal (`xpeccy-plus/src/xcore/xexpr.cpp:244-265`).
19. No short-circuit `&&`/`||` *(inferred)*; no flag names, T-state/instruction counters, stack depth or device state; per-evaluation name lookups.
20. **Watcher ignores RAM/ROM types**: always reads `memRd(comp->mem, (value+col) & busmask)` through the CPU map (`xpeccy-plus/src/watcher.cpp:261`); also calls `memRd` from the GUI thread while running, which on SLOT/IO pages goes through device callbacks *(inferred risk)*; the same `memRd` on non-ROM/RAM pages is used by REG-DUMP (`xpeccy-plus/src/xgui/debuga/dbg_rdump.cpp:121`) and the sprite scanner (`xpeccy-plus/src/xgui/debuga/dbg_sprscan.cpp:68`). Watches not saved, recompiled on every repaint, no change highlight, no register write-back.

### 19.3 Disassembly, navigation, labels

21. **Disasm RAM/ROM page mode unreachable** (`setMode` never called); paging only by forcing banks in MEMMAP, which writes the live map (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2047-2057`).
22. **Label file `FF:` bank mishandled**: `FF` is remapped to bank 0/5/2/0 before the `switch (xadr.bank)` with `case 0xff`, so that case is dead (`xpeccy-plus/src/xcore/labels.cpp:181-196`); labels in 0000-3FFF become RAM page 0 labels (not ROM) and show at C000+offset when RAM 0 is paged there; `saveLabels` writes non-RAM labels as `FF:` (`xpeccy-plus/src/xcore/labels.cpp:241`), so ROM labels come back as RAM 0. ROM/SLOT/IO labels survive only via `.xmap`.
23. **Go-to-label uses the stored CPU address, not the current mapping** (`str_to_adr`, `xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:656-659`); for `.xmap` labels that is `abs & 0xFFFF` (`xpeccy-plus/src/xcore/xmap.cpp:99-100`), wrong for any page outside its "natural" slot. The Labels list double-click uses `memFindAdr` correctly (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2087-2095`).
24. **Spurious history entries**: every column-0 edit (comment/label) emits `s_adrch` and `t_update` pushes whenever `oadr >= 0` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:781`, `:1054-1057`) *(inferred)*.
25. **Assembled code marks only its first byte EXEC**: the loop never advances `ptr` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:856-860`).
26. **Non-printable single byte in TEXT view printed as decimal**: `QString("DB #%0").arg(dasmrd(...))` without `gethexbyte` gives `DB #200` for 0xC8 (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:372`).
27. **`db "..."` editor reads past the string end**: `str.at(idx)` after `idx++` without bounds check, including `idx == size` (`xpeccy-plus/src/xgui/debuga/dbg_disasm.cpp:803-811`); `str.at(3)` on the `db ` prefix without length check (`:800`) *(inferred: Qt asserts or reads out of range)*.
28. **`clear_all_labels` uses `free()` on `new` objects** and does not reset `conf.curlabset` (`xpeccy-plus/src/xcore/labels.cpp:93-98`); called by `load_xmap` (`xpeccy-plus/src/xcore/xmap.cpp:56`); *(inferred)* `curlabset` dangles if the file has no labels chunk.
29. **"Show labels" / "Show segment" not persisted**: forced on/off in the constructor (`xpeccy-plus/src/xgui/debuga/debuger.cpp:582-583`), no config key.
30. Marks store the view top, not the cursor; Ctrl/Alt+digit hard-coded; on macOS "Ctrl+1" is Cmd+1 *(inferred)*.
31. Gaps: no forward history or limit; no persisted history/marks; no arithmetic in address inputs; assembler takes no label operands; no flags column; Labels list has no per-label edit/delete, no address sort, case-sensitive filter; only `BB:AAAA NAME` and `.xmap` (no `.sym`, no$zmg/Fuse, sjasmplus `--sym` EQU import, no ROM bank prefix); only the Z80 disassembler/assembler built in.

### 19.4 Memory views

32. **Finder repeats the same hit**: after a match `adr` = match address and the next search starts there with `shift = 0` (`xpeccy-plus/src/xgui/debuga/dbg_finder.cpp:75-91`) *(inferred)*; start address persists across openings (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2600-2601`).
33. **Finder misses overlapping matches**: on mismatch `idx = 0` without re-testing the current byte (`xpeccy-plus/src/xgui/debuga/dbg_finder.cpp:80-88`), e.g. `01 02` not found in `01 01 02` *(inferred)*.
34. **Clicks on bytes 9-16 in the dump do not select**: `mousePressEvent` returns if `col > 8` (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:505`).
35. **Dump breakpoint menu shows wrong state in RAM/ROM view**: checkmarks from `getBrk(conf.zx, adr)` (CPU view, `xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:671-676`) while the action uses page addresses (`:698-699`); in CPU view a SLOT cell yields no breakpoint (`:690-696`).
36. **Dump page size is decorative**: physical index is always the 16K page (`xpeccy-plus/src/xgui/debuga/dbg_dump.cpp:56-67`), so 32K/64K "pages" wrap every 16K.
37. **`loadDUMP` never closes its `FILE*`** (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2644-2655`).
38. **Ctrl+S conflict**: `XCUT_SAVE_DUMP` and `XCUT_DUMP_REG_SP` share Ctrl+S (`xpeccy-plus/src/xcore/keymap.cpp:494`, `:511`); *(inferred)* the dump consumes it while focused.
39. Gaps: search limited to CPU space, 8-byte patterns, byte masks, no find next/prev, no case folding, no page/all-RAM scope; no changed-byte highlight; no ASCII editing; no video/GS/cartridge memory views; save/load only through the CPU map, no page target, no Hobeta/TRD/SCL import; stack view display-only (no symbols, return-address detection, click-through, editing).

### 19.5 CPU and state views

40. **MEMMAP mislabels non-ROM/RAM windows**: `setRFIndex(mmapType[i], pg.type)` with default index 0 (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2037`; `xpeccy-plus/src/xgui/xgui.h:36`; `xpeccy-plus/src/xgui/options/setupwin.cpp:50-54`) and a combo with only ROM (0) and RAM (`xpeccy-plus/src/xgui/debuga/debuger.cpp:788-789`), so `MEM_SLOT`/`MEM_EXT`/`MEM_IO` (`xpeccy-plus/src/libxpeccy/memory.h:11-17`) show as "ROM". The header comment "editable on ZX and read-only labels elsewhere" (`xpeccy-plus/src/xgui/debuga/debuger.h:130`) does not match the always-editable code. Only the first 256-byte page per 16K window is inspected (`xpeccy-plus/src/xgui/debuga/debuger.cpp:2033`), misrepresenting finer-grained mappers *(inferred)*.
41. **CPU register edits commit per keystroke** (`xpeccy-plus/src/xgui/debuga/debuger.cpp:539`): partial values are written mid-typing *(inferred consequence)*. No HALT, EI latch, interrupt vector, MEMPTR in release, separate I/R, F' checkboxes.
42. **CMOS dump does not show the RTC as the CPU sees it**: 0x00-0x09 come from the host clock, 0x0C/0x0D are synthesized (`xpeccy-plus/src/libxpeccy/cmos.c:45-78`); the dump shows stale `data[]` (`xpeccy-plus/src/xgui/debuga/dbg_cmos_dump.cpp:28`), so edits to 0x00-0x09 are ineffective; address latch and Evo `mode` (`xpeccy-plus/src/libxpeccy/cmos.h:11-12`) not shown; `cmos_wr` masks the address to 7 bits (`xpeccy-plus/src/libxpeccy/cmos.c:95`) although 256 bytes are shown; `update()` without `dataChanged` (`xpeccy-plus/src/xgui/debuga/dbg_cmos_dump.cpp:87-89`) *(inferred)*.
43. **FDC view has a side effect**: `draw()` calls `flpRd()`, which sets `flp->rd = 1`, then clears it (`xpeccy-plus/src/xgui/debuga/dbg_fdd.cpp:34`; `xpeccy-plus/src/libxpeccy/floppy.c:68-69`); `flp->rd` drives the main-window disk LED (`xpeccy-plus/src/emulwin.cpp:916-920`), so refreshing fakes and clears a read-activity event (harmless to emulation).
44. **Palette** is read-only and shows the renderer's table, not hardware palette registers.
45. **Tape view lacks the block index**: `labTapePos` shows the signal index within the block (`xpeccy-plus/src/xgui/debuga/dbg_tape.cpp:21`); `tape->block` is used only by the diagram (`:41-48`).
46. **Sprite scanner "Page" has no effect below #C000** (`xpeccy-plus/src/xgui/debuga/dbg_sprscan.cpp:63-73`).
47. **Screen Custom offset** is clamped only for page 0xFF in `vid_get_screen`; other pages read into the next page when offset > #2500 *(inferred from arithmetic)*.
48. **Heat map exec classification** by `PC-1 == adr` also counts a data read of the byte before PC as exec (`xpeccy-plus/src/libxpeccy/spectrum.c:99-100`) *(inferred edge case)*.
49. Docks for absent hardware (CMOS, FDC, FM placeholder) always present; FM page blanks (`xpeccy-plus/src/xgui/debuga/dbg_sndchip.cpp:659-663`).
50. Missing views: video/ULA state (mode, border, ULA+, TSConf/Evo registers, contention); interrupt panel beyond the INT label (position/length, NMI); SAA1099, General Sound, Soundrive/Covox, Kempston/mouse, keyboard matrix (separate window), IDE/SD/HDD, Evo/TSConf DMA, DivIDE/DivMMC; tape block list; history/time-travel views.

### 19.6 Execution, tracing, rewind

51. Trace is one step per Qt event (slow); trace log records registers after and PC/mnemonic before each step.
52. No run N instructions/frames/T-states, run to scanline/frame, reverse step, skip instruction, stop on HALT, stop on unknown opcode (`IRQ_PANIC` never raised, `xpeccy-plus/src/libxpeccy/spectrum.c:471-473`).
53. No instruction history ring buffer; rewind is main-window only and unreachable from the debugger; breakpoints do not fire during rewind playback; run-ahead frames skip breakpoint checks.
54. Performance: any memory breakpoint/condition/log forces the slow path for every access; global conditions run after every instruction with name lookups; every breakpoint edit rebuilds ~4.7 MB of maps and loops 64K per IO breakpoint; each debugger open walks 4.6 MB.
