# Spectaculator debugger — capability survey

**Source:** closed-source commercial Windows emulator by Jonathan Needle (https://www.spectaculator.com) · version 9.1.0.5068+d0f40f53 (May 2026 release, per `Debugger.dll` VERSIONINFO) · C++ / Win32 with the in-house "Graphite" GUI library (`graphite.dll`), GDI+ for the graphics views, TinyXML for project files
**Surveyed:** 2026-09-28 (reverse-engineering material: help file, PE resources, binary strings, earlier IDA notes; no disassembly was read for this survey)
**Scope note:** GUI-only debugger shipped as a plugin (`drivers/Debugger.dll`, one export `CreateInstanceEx`) that drives a core-side breakpoint and expression engine inside `Spectaculator.exe`. Tool windows: disassembly, registers, breakpoints, watches, memory inspector, call stack, screen inspector, graphics inspector, patch maker. No console monitor, no scripting, no remote protocol, no rewind in the debugger, no ULA or beam views. The debugger is disabled while a tournament RZX is being recorded.

### Evidence notation

Nothing here comes from source code. The "Where" column points at the evidence, and every claim carries a strength tag:

| Tag | Meaning |
|---|---|
| *(help)* | Official help file `Spectaculator91/spectaculator.chm`, cited as `chm:<page>` (for example `chm:debugger/breakpoints/about.htm`) |
| *(resource)* | PE resources. `dbg.dll:DIALOG 108` means a resource of `Spectaculator91/Spectaculator/drivers/Debugger.dll`, parsed directly with 7-Zip plus a small template parser. String IDs are the real `LoadStringW` IDs, not the block\*16 IDs that `tools/resource_extract.py` produces. `Spectaculator91/resources/...` means the main-exe resources that were already extracted. |
| *(strings)* | Literal strings or RTTI class names in `dbg.dll` or in `exe` (= `Spectaculator91/unpacked/Spectaculator.exe`, the unpacked main binary). `@0x...` is the virtual address. |
| *(prior RE doc)* | Earlier IDA session notes in `Spectaculator91/docs/*.md`. Function addresses are the IDA names from that session. Some of these notes contradict themselves; section 12 lists where. |
| *(inferred)* | My reading of the combined evidence, not confirmed by any single artifact |

Also consulted: the earlier project note `docs/inprogress/2026-08-17-conditional-breakpoints/research/spectaculator.md` (based on the online 9.0 manual and blog screenshots).

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| Architecture | Plugin debugger | `Debugger.dll` is a driver plugin (`CZxDebugger` implements `IZXDebugger`/`IZXUserInterface`) and adds "&Debugger... Ctrl+Enter" to the Tools menu through a plugin menu slot. If Ctrl is mapped as Symbol Shift, the shortcut is F10. *(resource, strings, prior RE doc)* | `dbg.dll:STRINGTABLE 1000,1006`; RTTI `.?AVCZxDebugger@@`; `Spectaculator91/docs/RE_Analysis.md` "Dynamic Menu Generation" |
| Architecture | Core-side engine | `CBreakpoint`, `CRpnExpression`, `CRpnNode`, `CRpnFunctionDef`, `IZXDebugContainer` and `CZ80Disassembler` all live in the main exe. The plugin owns the UI, the assembler, projects and patches. *(strings)* | `exe` RTTI strings; `dbg.dll` RTTI strings |
| CPU & registers | Registers window (Ctrl+R) | Four groups. "Program counter, stack pointer" shows PC and SP. "General registers" shows AF BC DE HL IX, AF' BC' DE' HL' IY. "Flags (F)" shows 8 checkboxes `S Z 5 H 3 V N C`. "Interrupts, memory refresh" shows IR and `IM: 2 (DI)`. *(help, resource, strings)* | `chm:debugger/registers/about.htm` (screenshot `registers_window.gif`); `dbg.dll:STRINGTABLE 1500-1503`; string `SZ5H3VNC` |
| CPU & registers | Memory map panel | Registers window lists the page mapped in each 16K slot, e.g. `$0000-$3fff 48K ROM`, `$c000-$ffff RAM 0`. Added in 9.0. *(help, strings)* | `chm:getting_started/revision_history/9.0.htm`; `dbg.dll:STRINGTABLE 1509`; exe page names `48K ROM`, `128K ROM 0`, `+3 ROM 3`, `TRDOS`, `Service ROM`, `RAM %d` |
| CPU & registers | Register editing | Click a value and type the pair value, or `reg:value` (e.g. `a:35`) for an 8-bit register. Flags are toggled via checkboxes. Five number formats accepted. *(help)* | `chm:debugger/registers/change.htm` |
| CPU & registers | Change highlight | Registers changed since the last break or step are drawn red *(help)* | `chm:debugger/registers/about.htm` |
| CPU & registers | Frame cycle readout | Status bar `T: 239 / 69888` shows the current T-state in the frame and the T-states per frame *(help screenshot, strings)* | `chm:debugger/about.htm` (`debug_window.gif`); `dbg.dll` string `T: %d / %d` `@0x10039684` |
| Disassembly | Browser-like disassembly | Columns: address, bytes, mnemonic, comment. Color-coded. Jump/call targets and `(IX+d)`/`(IY+d)` operands are hyperlinks. Ctrl-click opens a link in the Memory Inspector. *(help)* | `chm:debugger/about.htm`; `chm:getting_started/revision_history/9.0.htm` |
| Disassembly | Back / Forward history | Alt+Left and Alt+Right, with drop-down history (`CLinkHistory`) *(resource, strings)* | `dbg.dll:ACCELERATOR 101` (ids 101/102); `dbg.dll:STRINGTABLE 101,102,1011-1013` |
| Disassembly | Hex/decimal toggle and ASCII mode | Ctrl+H switches every view between hex and decimal. Ctrl+A shows the disassembly area as Spectrum ASCII. *(help, resource)* | `chm:debugger/hexdec.htm`; `chm:debugger/ascii.htm`; `dbg.dll:MENU 101` View |
| Disassembly | Undocumented opcodes | SLL mnemonic, redundant DD/FD prefixes, and the DDCB/FDCB "LD r, RLC (IX+d)" forms *(help, strings)* | `chm:getting_started/revision_history/7.0.htm`; `dbg.dll` strings `LD B, RLC`...; `PREFIX` |
| Disassembly | Inline assembler | Click the mnemonic area and type an instruction; Enter assembles it. No labels or macros. Supports undo and redo. Also used by Find. (`CZ80Assembler`, `CAsmEdit`) *(help, strings)* | `chm:debugger/assembler.htm`; RTTI `.?AVCZ80Assembler@@`, `.?AVCZ80AsmMemoryOutputStream@@` |
| Disassembly | Comments | Right-click the area right of a line, type text, Enter. Del removes. Comments are stored per address and memory page. *(help, strings)* | `chm:debugger/comments.htm`; dzx keys `comment`, `mempageid`, `address` |
| Disassembly | Set next instruction | Ctrl+I sets PC to the caret *(help, resource)* | `chm:debugger/setnext.htm`; `dbg.dll:MENU 101` |
| Disassembly | Show next instruction | Alt+Num* scrolls back to PC *(help)* | `chm:debugger/shownext.htm` |
| Disassembly | Export disassembly as source | `CZ80SrcExporter` class, `DEFB/DEFW/DEFM %s` and `; %s` formats, plus a tooltip "Saves the disassembly to a file." (40016). No menu item references id 40016. *(strings; reachability inferred)* | `dbg.dll:STRINGTABLE 40016`; RTTI `.?AVCZ80SrcExporter@@` |
| Memory views | Memory Inspector (Ctrl+M) | Address field (accepts expressions), columns `(Auto)` or fixed, data mode None / Bytes / 16-bit Words plus Spectrum ASCII. Values edited in place, caret auto-advances, Tab switches between hex and ASCII. Values changed during a step turn red. Undo added in 9.1. The last address is remembered. *(help, resource, strings)* | `chm:debugger/memory/about.htm`, `edit.htm`, `view.htm`; `dbg.dll:MENU 118`; `dbg.dll:STRINGTABLE 1504-1507`; setting `MemoryInspector1` |
| Memory views | Edit in disassembly | Click the byte column and type data in the Find syntax. ROM cannot be edited (breakpoints can still be set in ROM). *(help)* | `chm:debugger/memory.htm` |
| Memory views | Selection, cut/copy/paste | Mouse drag, Shift to extend, or Make Selection dialog (start plus end or length). Cut zeroes RAM (ROM is left unchanged). Copy as DEFBs (Ctrl+Shift+C). *(help, resource)* | `chm:debugger/selecting.htm`, `copypaste.htm`, `copy_defbs.htm`; `dbg.dll:DIALOG 145` |
| Memory views | Fill memory (9.1) | Start, end/length, byte value *(help, resource)* | `chm:debugger/fill.htm`; `dbg.dll:DIALOG 150` |
| Memory views | Paste file contents (9.1) | Loads a .bin/.raw file at the caret *(help)* | `chm:debugger/pastefile.htm` |
| Memory views | Export memory | "Export from:" combo offers `Main Memory` or `RAM page %d`, then start address and length *(help, resource)* | `chm:debugger/export.htm`; `dbg.dll:DIALOG 149`; `dbg.dll:STRINGTABLE 1007,1008` |
| Memory views | Find (Ctrl+F) | Searches main memory for bytes, little-endian words (5-digit decimal or 4-digit hex means a word), quoted strings, byte/word lists, or an unquoted Z80 instruction, which is assembled and its opcodes searched. The tooltip also says "or specific RAM pages", but the dialog has no page selector. *(help, resource)* | `chm:debugger/find.htm`; `dbg.dll:DIALOG 110`; `dbg.dll:STRINGTABLE 57636` |
| Memory views | Call stack (Ctrl+T) | Word-per-line view of memory at SP with an ASCII column. Click opens the address in the disassembly, Ctrl+click in the Memory Inspector. Buttons: Show Top of Stack, Push Value, Pop Value (changes SP only). *(help, resource)* | `chm:debugger/callstack/*.htm`; `dbg.dll:MENU 120`; `dbg.dll:DIALOG 121` |
| Memory views | Quick Peek | Hovering over any address in the disassembly, memory, call stack or registers windows shows a tooltip with a short disassembly, raw bytes and ASCII (`CSnippetDisassembler`) *(help, strings)* | `chm:debugger/quickpeek/about.htm`; RTTI `.?AVCSnippetDisassembler@@` |
| Navigation & bookmarks | Bookmarks | Toggle Ctrl+K, next/previous Ctrl+Alt+Down/Up, Clear. Shown in the gutter. Stored in the project. *(help, resource)* | `chm:debugger/bookmarks.htm`; `dbg.dll:MENU 101`; dzx key `bookmark` |
| Navigation & bookmarks | Go to address | Ctrl+G, with the five number formats *(help, resource)* | `chm:debugger/goto.htm`; `dbg.dll:DIALOG 107` |
| Symbols & labels | Labels | Only as breakpoint or watch "friendly names". There is no symbol table, no map/sym import and no label display in the disassembly. *(help, resource; absence inferred)* | `dbg.dll:DIALOG 108` (`&Label:`); `dbg.dll:DIALOG 138` |
| Breakpoints | 7 breakpoint types | On Execute Instruction, On Memory Read, On Memory Write, On Memory Read or Write, On I/O Port Read, On I/O Port Write, On I/O Port Read or Write. List abbreviations include `M/X`, `IO/R`, `IO/RW`. *(resource, strings)* | `dbg.dll:STRINGTABLE 2000-2006`; `chm:debugger/breakpoints/about.htm` |
| Breakpoints | Port mask | Editable combo "Port &Mask:" is enabled for I/O types, e.g. `$fbfe` with a mask for a keyboard half-row *(resource, help)* | `dbg.dll:DIALOG 108` ctl 1003/1020 |
| Breakpoints | Memory-page restriction | Checkbox "Break only when executing from %s", where %s is the page mapped at the address (e.g. "RAM 0"). No separate page combo exists in the template. Also available for memory read/write breakpoints. *(resource, help)* | `dbg.dll:DIALOG 108` ctl 1031; `dbg.dll:STRINGTABLE 1032,1033` |
| Breakpoints | Enable / disable / error state | Three icons: enabled, disabled, error (the condition failed to evaluate). Checkbox in the list, Ctrl+Shift+Space at the caret, or gutter context menu. *(help)* | `chm:debugger/breakpoints/about.htm`, `enable.htm` |
| Breakpoints | Hit counts | Break Always / hit count equal to N / is a multiple of N / is equal to or greater than N. Live "Current hit count" with Reset. *(resource, help)* | `dbg.dll:DIALOG 108` group "Hit Count"; `dbg.dll:STRINGTABLE 2020-2023` |
| Breakpoints | Breakpoint helpers | Drop-down on New Breakpoint: ULA read/write `$fe`, 128K/+2 paging write `$7ffd`, +2A/+3 paging write `$1ffd`, Any Key, 8 keyboard half-rows, Kempston `$31`, Kempston clone `$df`, Other... *(resource, help)* | `dbg.dll:MENU 122`; `chm:debugger/breakpoints/helpers.htm` |
| Breakpoints | Screen breakpoints | Right-click in the Screen Inspector: Pixel Byte or Attribute Byte, then Read, Write or Read-or-Write memory breakpoint on that byte *(help, resource)* | `chm:debugger/screen/screen_breakpoints.htm`; `dbg.dll:MENU 129` |
| Breakpoints | Adding breakpoints | Gutter click, Ctrl+Space at the caret, Ctrl+B dialog, toolbar, or Ctrl+right-click gutter (dialog pre-filled). Unlimited count. Not overwritten by self-modifying code. *(help)* | `chm:debugger/breakpoints/addbreakpoint.htm` |
| Breakpoints | Breakpoints window (Ctrl+Shift+K) | Columns Address, Type, Label, Hit Count, Condition. Toolbar: New (with helper arrow), Delete, Show in Disassembly, Delete All. Del key deletes (9.1). *(help, strings)* | `chm:debugger/breakpoints/show.htm`; `dbg.dll:STRINGTABLE 1015-1019`; `dbg.dll:MENU 117` |
| Breakpoints | Break on unhandled opcode | Options, Advanced, "Break into debugger on unhandled instruction". Only fires while tracing. *(help, strings)* | `chm:debugger/unhandled.htm`; `exe` string `Break On Unhandled` `@0x4d9594` |
| Conditions & expressions | C-like expression language | Operators `* / + - % == != < > <= >= ! && || & \| ^ ~ << >>` and unary minus. Functions `rb() rw() rwb()`. Z80 registers plus IM, IFF1, IFF2, NMIREQ, INTREQ, HALTED. Flag constants. Case-insensitive. Compiled to RPN in the core. *(help, strings)* | `chm:debugger/expressions/reference.htm`; `exe` RTTI `.?AVCRpnExpression@@` |
| Conditions & expressions | Live validation | Condition and Run Until edits show an icon plus a message line (ctl 1022/1023) with parser errors such as "Variable not found, '%s'" *(resource, strings)* | `dbg.dll:DIALOG 108,134,138`; `exe` strings `@0x4d7810..0x4d79fc` |
| Watchpoints & watches | Watch window (Ctrl+Shift+W) | Columns Expression, Label, Value, then ASCII and bytes at the address the value points to. Updated while running. Changed values highlighted. *(help)* | `chm:debugger/watches/about.htm` (`watch_window.gif`) |
| Watchpoints & watches | Watch presets | New Watch drop-down: System Variables... (checklist of 69 48K sysvars from an embedded XML), IM2 Vector (`rw(i*256+255)`), Custom (Ctrl+W) *(resource, strings)* | `dbg.dll:MENU 137`; `dbg.dll:DIALOG 140`; `dbg.dll:XML 141`; string `@0x1003b540` |
| Execution control | Trace / Break / Stop | F5 runs with breakpoints active. Ctrl+Break breaks. Shift+F5 runs with breakpoints ignored. Esc (Auto Run / Trace) runs as F5 if any breakpoints exist, otherwise as Shift+F5. *(help, resource)* | `chm:debugger/trace/about.htm`, `autoruntrace.htm`; `dbg.dll:ACCELERATOR 101` |
| Execution control | Step Into / Over / Out | F11 / F10 / Shift+F11. Step Over treats LDIR, CPDR and HALT loops as a single step and uses a temporary breakpoint after CALL. Step Out runs to RET. *(help)* | `chm:debugger/trace/stepinto.htm`, `stepover.htm`, `stepout.htm` |
| Execution control | Run to Cursor | Ctrl+F10, temporary breakpoint at the caret *(help)* | `chm:debugger/trace/runtocursor.htm` |
| Execution control | Run Until Interrupt | Ctrl+F11. Breaks when an INT or NMI is accepted. Reports "Break on interrupt request at %s". *(help, strings)* | `chm:debugger/trace/rununtilinterrupt.htm`; `dbg.dll:STRINGTABLE 2105` |
| Execution control | Run to Start / End of Frame | F9 stops about 1000 T into the next frame. Ctrl+F9 stops about 1000 T before the end of the current frame. Reports "Break on cycle %s, at %s". *(help, strings)* | `chm:debugger/trace/runtostartofframe.htm`; `dbg.dll:STRINGTABLE 40019,40020,2106` |
| Execution control | Run Until Condition | F6 opens an expression dialog, Ctrl+F6 repeats. The last expression is persisted. *(help, resource, strings)* | `chm:debugger/trace/runutilcondition.htm`; `dbg.dll:DIALOG 134`; setting `RunUntilExpression` |
| Execution control | Run Until Event | Alt+T runs until the tape stops ("Break on event at %s - %s", event `tape stopped`). This is the only event in 9.1. *(resource, strings; "only" inferred)* | `dbg.dll:MENU 101`; `dbg.dll:STRINGTABLE 2108,2120,40147` |
| Tracing & logging | Log CPU Instruction Execution | Debug menu toggle "Logs CPU instruction execution to a file". The file path is probably the hard-coded `C:\temp\z80cpu.log`. The exe writes the header `Sequence   Addr  Cycle Instruction` and one line per instruction in the format `%010u $%04x %05d %s`. Not in the help. *(resource, strings; path binding inferred)* | `dbg.dll:MENU 101` id 40031; `dbg.dll` string `@0x1003953c`; `exe` strings `@0x4dab1c`, `@0x4dab64`; `Spectaculator91/docs/Advanced_Features.md` "Instruction Tracing" |
| History / rewind | None in debugger | Only RZX recording rollback: Insert adds a bookmark, Delete rolls back to it (not in tournament mode). This is a recording feature, not a debugger feature. *(help)* | `chm:rzx/rollback.htm`, `rzx/insert_rollback.htm` |
| Video, raster & beam | Screen Inspector (Ctrl+E) | Screen: (Active) / Normal (RAM 5) / Shadow (RAM 7). Full Colour / White on Black / Black on White. Show Grid. Show Flash Attributes. Status bar shows X, Y, X8, Y8, Pixel `$addr`, Attr `$addr`, and which screen is active. Live while stepping. *(help, resource, strings)* | `chm:debugger/screen/*.htm`; `dbg.dll:MENU 123`; `dbg.dll:STRINGTABLE 1510-1518,2030-2033` |
| Video, raster & beam | Graphics Inspector (Ctrl+Shift+G) | Tile width (multiples of 8) and height (1 px steps) up to 256x256. Formats Character / Rows / Rows (Reverse) / Columns / Screen. Pad Bytes. Tile Address is an expression (e.g. `IX`). Grid, invert, zoom. Copy to clipboard or export PNG with a chosen cell size. *(help, resource, strings)* | `chm:debugger/graphics/*.htm`; `dbg.dll:STRINGTABLE 2040-2049`; `dbg.dll:DIALOG 148` |
| Video, raster & beam | Beam / contention views | None. No beam position marker, no per-line or per-T-state event view, no contention display. The only timing value shown is the `T: n / frame` status field. *(absence inferred from the complete resource and string set)* | `dbg.dll` resources (all enumerated) |
| Sound & device views | None in the debugger | No AY, tape, FDC, General Sound or port-state panels. Devices are visible only through I/O breakpoints and the helpers. *(absence inferred)* | `dbg.dll` resources |
| Profiling / coverage | None | No profiler, heat map or code/data logger. The core does have an instruction-byte "mark" hook in opcode fetch, but no UI uses it (see SPC-Q9). *(prior RE doc; absence inferred)* | `Spectaculator91/docs/ULA_Emulation.md` "Contention Check" |
| Scripting, automation | None | No script engine, CLI or network protocol. The debugger plugin exports only `CreateInstanceEx`. *(strings, imports)* | `dbg.dll` export table |
| Import / export & persistence | Debugging projects (.dzx) | XML (TinyXML), root `zxdebuginfo`. Holds breakpoints (`bptype`, `bpstate`, `address`, `addressmask`, `mempageid`, `hitcounttype`, `hitcountarg`, `condition`, `label`, `errormsg`), watches (`expression`, `label`), bookmarks, comments and patches. Auto-loads a `.dzx` next to a snapshot. Recent-files list (9.1). Unsaved-changes prompt. *(help, strings)* | `chm:debugger/projects.htm`; `dbg.dll` ASCII strings from `@0x100396a8`; `dbg.dll:STRINGTABLE 1022,1041` |
| Import / export & persistence | Patch Maker (undocumented) | Window listing named patches (Patch List) and an Undo Stack. Actions: Create Patch (from the selection or from undo history), Apply, Delete, Delete All, Clear Undo Stack, Export Patch as an Apple `.plist`. *(resource, strings)* | `dbg.dll:MENU 142`; `dbg.dll:STRINGTABLE 1028-1031,40123-40131`; RTTI `.?AVCPatchManager@@` |
| Import / export & persistence | Snapshots from the debugger | File, Open/Save Snapshot (.szx) inside the debugger frame *(resource)* | `dbg.dll:MENU 101` ids 40099/40100 |
| UI conveniences | Change notifications | Internal bus of named events (`ZxDbg.Breakpoint.Hit`, `.MemoryChanged`, `.RegisterChanged`, `.Watch.Changed`, `.UndoStack.Changed`, ...). Core notification `ZXNC_MEMORY_CHANGED` keeps all inspectors in sync (fixed in 9.1). *(strings, help)* | `dbg.dll` wide strings `ZxDbg.*`; `chm:getting_started/new.htm` |
| UI conveniences | Five number formats | `32678`, `0x8000`, `8000h`, `$8000`, `#8000` in every numeric field *(help)* | `chm:debugger/goto.htm` |

## 2. Architecture: core vs plugin

- `Spectaculator.exe` holds `CBreakpoint` (breakpoints and, per the prior RE doc, cheats), the RPN expression engine (`CRpnExpression`, `CRpnNode`, `CRpnNodeList`, `CRpnFunctionDef`, `CRpnExpressionStaticData`, `IRpnExpressionDelegate`), the Z80 disassembler (`ZxUtils::CZ80Disassembler`) and the `IZXDebugContainer` interface *(strings)*. It also holds the parser error strings ("Function not found, '%s'", "Variable not found, '%s'", "Mismatched parenthesis or spurious comma", "Not enough parameters for '%s'. Expected %d but got %d", "Division by zero", "*** Not Initialised***") *(strings, `exe@0x4d7810..0x4d79fc`)*.
- `Debugger.dll` holds the frame (`CDbgFrame`), every window class, a second tokenizer (`CZ80Token`, `CZ80TokeniserStaticData`) used by the assembler and Find, `CZ80Assembler`, the undo system (`CDbgUndoManager`, `CDbgUndoGroup`, `CUndoMemoryChange`), `CPatchManager`, `CWatchExpression`, and TinyXML for `.dzx` *(strings)*.
- It reads other plugins' state through string keys: `rzx.dll\RZX_TOURNAMENT_RECORDING` disables debugging during a tournament recording (7.0 changelog: "Disabled the debugger when in tournament mode") *(strings, help)*. The exe also answers `@LIMITINSFETCH`, `@GETINSFETCHES` and `@RESETREPOCH` *(strings)*; what they do is open (SPC-Q9).
- When a breakpoint fires, the prior RE doc describes `debugger_handle_break` (0x457320). It stores a break type and address, removes any temporary breakpoint, pauses sound, locks a mutex, signals the UI through a vtable callback, and blocks the emulation thread on a condition variable until the UI releases it. So the emulator runs on its own thread and the debugger UI parks it *(prior RE doc)*.

## 3. Breakpoints in detail

**Dialog** `dbg.dll:DIALOG 108` "Breakpoints" (273x200 DLU) *(resource)*:

- Location group:
  - `&Enabled` (1017)
  - `Break &at:` edit (1002)
  - `Port &Mask:` combo (1003, editable drop-down)
  - Type combo (1013)
  - `Break only when executing from specific memory page` checkbox (1031). At runtime the text becomes "Break only when executing from %s", with the page name filled in (string 1032).
  - `&Label:` (1018)
  - `&Condition:` (1014)
  - Validation icon and text (1022/1023)
- Hit Count group:
  - `When the breakpoint is &hit:` combo (1015) with argument edit (1011)
  - `Current hit count:` value (1016) and `&Reset` (1012)

**Break reasons** shown after a stop (`dbg.dll:STRINGTABLE 2100-2108`) *(strings)*:

| Id | Text |
|---|---|
| 2100 | Breakpoint hit at %s. |
| 2101 | Break on memory read from %s (%s). |
| 2102 | Break on memory write to %s (%s). |
| 2103 | Break on I/O read from port %s. |
| 2104 | Break on I/O write to port %s. |
| 2105 | Break on interrupt request at %s. |
| 2106 | Break on cycle %s, at %s. |
| 2107 | Break on run until expression, at %s. |
| 2108 | Break on event at %s - %s. |

The second `%s` in 2101/2102 is most likely the memory page name *(inferred)*. String 2106 shows the core has a break-at-T-state mechanism, but it is exposed only through F9/Ctrl+F9 *(inferred)*.

**Core record** (prior RE doc, `CBreakpoint`, about 672 bytes):

- id, 16-bit address, `enabled` (0 = off, 1 = on, 2 = error), type, subtype
- description `CString`
- `hit_mode` 0..3, `hit_target`, `hit_current`
- `bank_filter` (0 = any)
- an embedded `CRpnExpression`
- `CZxError last_error`
- slot, extra flags

The field names match the `.dzx` keys one-to-one: `bptype`, `bpstate`, `address`, `addressmask`, `mempageid`, `hitcounttype`, `hitcountarg`, `condition`, `label`, `errormsg` *(strings)*.

**Keying.** Execution and memory breakpoints are keyed by the 16-bit CPU address, optionally narrowed by a page id. They are not keyed by physical address (the help says "can be set anywhere ... including a specific RAM page and ROM") *(help)*. There are no address ranges: one address per breakpoint *(resource: single `Break at` field)*. For I/O breakpoints the check is `(port & mask) == (addr & mask)` *(inferred from the $fbfe/mask example and the helper list)*.

**Check path** (prior RE doc):

1. Storage is a hash map keyed by address (`hashtable_equal_range` 0x41f860), plus a small fast-reject bitmap rebuilt by `breakpoint_rebuild_bitmap` (0x41eb20). `z80_memory_watch_check` (0x4569d0) tests the bitmap first.
2. For each breakpoint on the address it runs `cheat_check_trigger` (0x40bf60):
   - skip unless enabled
   - check the page filter
   - evaluate the condition. On an evaluation error it sets `enabled = 2` and breaks.
   - increment the hit count and apply the hit mode
3. Hit counts therefore count only hits where the condition was true *(prior RE doc pseudo-code; unverified)*.

The bitmap layout in that doc is inconsistent (it says both "64-bit per 1KB" and "128 bits, 1 bit per 512 bytes"); see SPC-Q1.

**Self-modifying-code safety**: breakpoints are not written into memory as opcodes (no RST-38 patching), so SMC cannot erase them *(help)*.

**Helpers** (`dbg.dll:MENU 122`) *(resource)*:

- ULA Read / ULA Write (port $fe)
- 128K/+2 Paging Register Write ($7ffd)
- +2A/+3 Paging Register Write ($1ffd)
- Keyboard: Any Key, `1 to 5`, `6 to 0`, `Q to T`, `Y to P`, `A to G`, `H to Enter`, `Caps Shift to V`, `B to Space`
- Kempston ($31), Kempston Clone ($df)

The screenshot of the keyboard helper shows the result `$f7fe IO/R "&1 to 5"`: the helper sets address plus mask so that only that half-row read matches *(help screenshot `bp_keyboard_atog.png`, `breakpoints_window.gif`)*.

## 4. Expression language

This summarizes `chm:debugger/expressions/reference.htm` *(help)*; the parser lives in the exe *(strings)*.

- **Literals:** decimal, `0x`, `h` suffix, `$`, `#`. A character constant exists (error "Unterminated character constant") *(strings)*.
- **Operators:** `* / + - %`, unary `-`, `== != < > <= >=`, `! && ||`, `& | ^ ~ << >>`. The help calls `^` "Logical XOR" and `~` "Negate"; per the prior RE doc's operator table these are bitwise XOR and bitwise complement.
  - The prior RE doc lists the internal RPN opcode letters: `!` NOT, `~` complement, `_` negate, `? @` for `<= >=`, `A B` for `== !=`, `C D` for `&& ||`, `F G` for `<< >>`.
  - Tokens: number 1001, char 1003, registers 1005-1044 *(prior RE doc)*. That is exactly 40 variable names: 14 pairs/16-bit, 20 single 8-bit including primed, IXH/IXL/IYH/IYL, and IM, IFF1, IFF2, NMIREQ, INTREQ, HALTED *(inferred count match)*.
- **Functions:** `rb(expr)`, `rw(expr)` (little-endian), `rwb(expr)` (big-endian). There is no `*` dereference. The function table is data-driven (`CRpnFunctionDef`, error "Not enough parameters for '%s'") *(help, strings)*.
- **Constants:** `S_FLAG $80`, `Z_FLAG $40`, `F5_FLAG $20`, `H_FLAG $10`, `F3_FLAG $08`, `P_FLAG`/`V_FLAG $04`, `N_FLAG $02`, `C_FLAG $01` *(help, strings)*.
- **Not available:** T-state, scanline or frame counters; paging state (the current 7FFD value, the page at a slot); port values; symbols. The Run Until and watch contexts accept registers. The assembler and Find contexts reject them ("Register '%s' is not allowed in this context") *(strings; context split inferred)*.
- **Where expressions are accepted:** breakpoint condition, Run Until, watch, Memory Inspector address, Graphics Inspector tile address *(help)*.
- **Result width:** the prior RE doc shows the evaluator returning a `short`, which would make values 16-bit *(prior RE doc; see SPC-Q3)*.

## 5. Watches

- `dbg.dll:DIALOG 138`: Expression plus Label, with live validation *(resource)*.
- The list shows Expression, Label, Value, then the ASCII and hex bytes found at the address given by the value. Example rows in the help screenshot: `rw(i*256+255)` labelled "IM2 Vector" = `$e6e6`, and `rb($afc4)` *(help screenshot)*.
- The System Variables dialog (`DIALOG 140`: ListView with checkboxes, All/None) is fed by `XML 141`. That file lists 69 `<var addr name size>` entries from `KSTATE` (23552) to `P RAMT` (23732); there are no 128K sysvars. The watch templates `rb(%d)` / `rw(%d)` suggest size 1 maps to `rb` and size 2 to `rw`. What happens for larger sizes (e.g. `KSTATE` size 8, `MEMBOT` size 30) is unknown *(resource, strings; mapping inferred)*.
- Watches are expressions only. A "watchpoint" (break on access) is a memory breakpoint instead.

## 6. Execution control

Accelerators from `dbg.dll:ACCELERATOR 101` *(resource)*; semantics from `chm:debugger/trace/*` *(help)*:

| Key | Command | Notes |
|---|---|---|
| Esc | Auto Run / Trace | F5 if any breakpoint exists, otherwise Shift+F5 |
| F5 | Trace | Run with breakpoints active; closes the debugger window |
| Shift+F5 | Stop Debugging | Run with breakpoints ignored |
| Ctrl+Break | Break | |
| F11 | Step Into | Repeating block instructions and HALT take one step per iteration |
| F10 | Step Over | Block instructions complete in one step. For CALL/CALL cc, sets a temporary breakpoint after the call. |
| Shift+F11 | Step Out | Until RET or RET cc (the help warns about stack games) |
| Ctrl+F10 | Run to Cursor | Temporary breakpoint |
| F6 / Ctrl+F6 | Run Until Condition / repeat | Expression is evaluated per instruction *(inferred)* |
| Ctrl+F11 | Run Until Interrupt | INT or NMI accepted |
| F9 | Run to Start of Frame | About 1000 T into the next frame |
| Ctrl+F9 | Run to End of Frame | About 1000 T before the frame end |
| Alt+T | Run Until Tape Stops | |
| Ctrl+I | Set Next Instruction | |
| Alt+Num* | Show Next Instruction | |

The main-window NMI key was moved to Alt+F5 in 9.0 so that the debugger's F5 would not trigger it *(help, 9.0 changelog)*. The 9.0 changelog also fixed "Run to start/end of frame not always breaking at the correct time" *(help)*.

## 7. Tracing and logging

- **UI:** Debug, "Lo&g CPU Instruction Execution" (menu id 40031, a toggle). The tooltip string is id 40030, "Logs CPU instruction execution to a file." The feature is not in the help *(resource, strings)*.
- **File:** the only path-like string in `Debugger.dll` is `C:\temp\z80cpu.log` (`@0x1003953c`), so the log most likely goes there, with no file picker *(strings; inferred)*.
- **Format** (exe `@0x4dab1c`/`@0x4dab64`):

  ```
  Sequence   Addr  Cycle Instruction
  ----------------------------------
  %010u $%04x %05d %s
  ```

  That is: a 10-digit sequence number, a hex PC, a 5-digit decimal cycle (probably the T-state within the frame), and the disassembled instruction *(strings)*. The sample in `Spectaculator91/docs/Advanced_Features.md` does not match this format string and should be treated as illustrative.
- **Core functions** (prior RE doc): `debug_trace_set_state` 0x4a6c00, `debug_trace_open_file` 0x4a6c30, `debug_trace_close_file` 0x4a6cf0; state at core offsets +8336 (enabled), +8340 (FILE\*), +8344 (sequence) *(prior RE doc)*.
- **Not found:** no register columns, no filters, no ring-buffer history, no "previous instruction" view. The earlier note cites a community request "Show previous instruction for breakpoint".

## 8. Screen and graphics inspectors (the only video tooling)

- **Screen Inspector** renders the screen memory (not the TV output) of the active, normal (RAM 5) or shadow (RAM 7) screen. Modes: attributes on/off (two monochrome modes), grid, flash on/off. It updates live while stepping. Its main debugging feature is the right-click pixel/attribute byte breakpoints (section 3). The status bar decodes the hovered position into pixel X/Y, character X8/Y8 and the bitmap and attribute addresses *(help, strings)*.
- **Graphics Inspector** is a sprite and font ripper with tile formats Character, Rows, Rows (Reverse), Columns and Screen, plus pad bytes. Its settings are persisted (`GraphicsInspector.TileWidth`, `.TileHeight`, `.TileFormat`, `.TilePadBytes`, `.Zoom`, `.InvertColours`, `.ShowGrid`, `.Address`) *(strings)*.
- **No beam, raster, contention, border-timeline or floating-bus visualization exists.** The core does have per-T-state data that such views could use:
  - a border-change log of (color, T-state) pairs, up to 6999 per frame
  - a 224-entry per-line ULA fetch table used for the floating bus
  - lazy scanline rendering driven by T-state deltas

  *(prior RE doc: `ULA_Emulation.md`, `Advanced_Features.md`)* None of it is surfaced in the debugger.

## 9. Persistence formats

- **`.dzx` debugging project** (TinyXML, UTF-8). The element and attribute names come from the `dbg.dll` ASCII string pool *(strings)*:

  ```
  zxdebuginfo > comments > comment
  zxdebuginfo > bookmarks > bookmark
  zxdebuginfo > breakpoints > breakpoint
  zxdebuginfo > watches > watch
  zxdebuginfo > patches > patchgroup > patch
  ```

  Keys in the pool: `address`, `mempageid`/`memPageId`, `bptype`, `bpstate`, `addressmask`, `hitcounttype`, `hitcountarg`, `condition`, `errormsg`, `label`, `expression`, `start`, `bytes`. Which of these are attributes and which are child elements is open (SPC-Q7).
  - `errormsg` means the error state is persisted with the breakpoint.
  - `mempageid` on comments and bookmarks means annotations are page-aware *(inferred)*.
  - Loading: a `.dzx` with the snapshot's name is auto-loaded, and a project can be saved independently of machine state *(help)*.
- **Patch export:** Apple property list (`<!DOCTYPE plist ...>`, `integer`, `array`, and a base64 alphabet for `<data>`) *(strings)*. The purpose is not documented. It is plausibly the patch format of Spectaculator's iOS sibling *(inferred)*.
- **Settings** live under the registry-style path `Plugins\Debugger`: `Recent Files\File%d`, `Last Folder`, `MemoryInspector1`, `ScreenInspector`, `StackInspector`, `RunUntilExpression`, `GraphicsInspector.*` *(strings)*.

## 9a. Device plugins and General Sound

*Merged from a second, independent pass over the device plugins (`drivers/gs128.dll`, `drivers/ay8912.dll`, `drivers/zxtape.dll`, `betadisk.dll`). Evidence short names:*

| Short name | Source |
|---|---|
| `GS:`, `AY:`, `TAPE:`, `BETA:` | Resources/RTTI of `drivers/gs128.dll`, `drivers/ay8912.dll`, `drivers/zxtape.dll`, `betadisk.dll` |

### Device views

| Device | What the debugger or its plugin shows | Where |
|---|---|---|
| ULA / screen | Screen Inspector (section 1); port-$FE breakpoint helpers | `DBG:MENU_122`, `_123` |
| 128K paging | $7FFD / $1FFD write breakpoint helpers; page-qualified breakpoints; export/find per RAM page | `DBG:MENU_122`; `DBG:DIALOG_149` |
| Keyboard, joystick | Half-row, any-key and Kempston read breakpoint helpers | `DBG:MENU_122` |
| Tape | "Run Until Tape Stops"; the tape plugin has its own Cassette Recorder window with a table of contents (block number, type such as "Turbo Loading Data Block (%d bytes)", "Pure Tone (%d pulses)", "[ Loop %d Times ]"), and Stopped/Paused/Playing/Recording state | `DBG:MENU_101`; `TAPE:STRING_1017`-`1146`, `_2016`-`2029` |
| AY-3-8912 | No register view. The plugin only has an options page (mono, ABC, ACB stereo; Fuller Box, Melodik for 48K) | `AY:DIALOG_107`; `AY:STRING_1016`-`1019`; `AY:RTTI` `CAY8912`, `CSoundOptionsPage` |
| Beta 128 (WD1793) | No controller register view. The plugin has a "Beta 128 Disk Drives" window (drive letters, image names), format / eject, and options (number of drives 1-4, autoboot, fast disk access) | `BETA:STRING_9016`-`9030`; `BETA:DIALOG_107` |
| General Sound | See "General Sound" below | |

### General Sound

- **Emulated: yes.** The plugin `drivers/gs128.dll` (description "General Sound emulation driver for Spectaculator", registry key `Plugins\General Sound`, flag `GS_ENABLED`; UTF-16 strings in the DLL). It is enabled with the check box "General Sound (GS512)" in the "Pentagon 128 / Scorpion ZS 256 Enhanced Sound" group of the Sound options page, next to Covox (`AY:DIALOG_107`, control 1014; `CHM:gs/enable.htm`).
- The help describes it as a 12 MHz Z80 with up to 512 KB, 4 channels of 8-bit stereo with 6-bit volume, able to play MOD files, and warns it costs a lot of host CPU (`CHM:gs/about.htm`).
- **Debugger exposure: none found.** `gs128.dll` implements only the generic device interface (`GS:RTTI` shows `CGS128` and `IZXDevice`, no debugger or UI interface, no dialogs or menus). `Debugger.dll` has no string, menu or dialog that mentions General Sound, a second CPU or a CPU selector, and its expression variables are the main Z80 registers only (`CHM:debugger/expressions/reference.htm`). So the GS Z80, its memory and its channels cannot be inspected or stepped *(conclusion from absence of evidence)*.


## 10. Notable and unique ideas

Ranked by value for a ZX Spectrum debugger:

1. **Spectrum-aware breakpoint helpers.** One click creates a correctly masked I/O breakpoint for a keyboard half-row, any key, Kempston, ULA or the paging registers. This puts port-decoding knowledge into the UI instead of the user's head.
2. **Screen-to-breakpoint.** Right-click a pixel or attribute cell in the Screen Inspector to get a read/write breakpoint on exactly that byte. It is the fastest route to finding "who draws this sprite".
3. **Structured hit counts with a live counter, plus an explicit error state** for conditions that fail to evaluate, which are persisted (`errormsg`). A bad condition is visible, never silently false.
4. **Page-restricted breakpoints and page-aware annotations** (`mempageid` on breakpoints, comments and bookmarks). This is the right keying for banked code, and it stays cheap: one checkbox naming the page mapped at that moment.
5. **Debugging projects auto-loaded next to the snapshot.** Session state (breakpoints, watches, bookmarks, comments, patches) travels with the program being analyzed.
6. **Quick Peek tooltips everywhere and a hyperlinked disassembly**, including `(IX+d)` operands, with Back/Forward history. A browser metaphor for code navigation.
7. **Run-until family keyed on emulator events:** frame start/end offsets, interrupt acceptance, tape stop, arbitrary expression. "Break on event" is generic in the break-reason strings, so it is designed to grow.
8. **Watch presets:** a system-variable picker from an XML table and an IM2-vector watch (`rw(i*256+255)`). These are cheap, domain-specific conveniences.
9. **Undo/redo for all memory edits and assembly, and a patch maker built on the undo stack.** Edits become named, exportable patches.
10. **Find accepts an unquoted Z80 instruction** and searches for its opcodes.

## 11. Gaps and caveats

- No rewind or reverse step, and no execution history buffer. RZX rollback is a recording feature only.
- No beam, contention, raster or per-T-state views despite a timing-accurate core. The single timing readout is `T: n / frame`, and the expression language cannot reference T-states or scanlines.
- No symbols: no map, sym or sjasmplus import, and labels exist only as breakpoint and watch names.
- No range breakpoints, no data-value breakpoints except through conditions, and no multi-page selection per breakpoint.
- Find and the disassembly view work on the current 64K CPU view. Only Export (and possibly Find, per its tooltip) can address a specific RAM page. There is no browsing of unmapped banks.
- The trace log is undocumented and apparently has a fixed path (`C:\temp\z80cpu.log`), with no register state and no filtering.
- The sysvar table is 48K-only.
- No scripting or remote debugging. Windows-only and closed source.
- The debugger is disabled during tournament RZX recording.
- The "unhandled instruction" break works only while tracing, by design.
- Patch Maker, Log CPU Instruction Execution and Run Until Tape Stops are shipped but absent from the help.

## 12. Caveats about the evidence

- The `Spectaculator91/resources/` folder contains only main-exe resources; the debugger resources come from `Debugger.dll`, which was extracted for this survey. No IDA database exists yet for `Debugger.dll`.
- The prior RE docs were written quickly and contain inconsistencies:
  - The breakpoint bitmap description contradicts itself.
  - The trace sample line does not match the real format string.
  - The "snow" routine as described (random writes into screen RAM) is implausible for a snow model.
  - Function names such as `cheat_check_trigger` for the breakpoint evaluator may be mislabelled.

  Claims tagged *(prior RE doc)* need confirmation; the open questions are tracked separately for an IDA session.
