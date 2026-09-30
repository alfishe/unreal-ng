# ZXSpin debugger — capability survey

**Source:** ZXSpin 0.7 (Windows, closed source; `ZXSpin.exe` md5 `b1086a60c692b98ace67dac2f5af136f`, dated 2009-12-01) · Delphi (VCL) Win32 · reverse-engineering workspace `ZXSpin/` (network share, IDA database plus Hex-Rays output)
**Surveyed:** 2026-09-28 (resource extraction, string reading, reading of the existing Hex-Rays output and disassembly; IDA itself was not used)
**Scope note:** A GUI debugger window (`TDebuggerForm`, tabs Disassembly / Memory / Breakpoints / Labels-Comments / Log-Messages / Hardware Info, side panel Registers / Ports / AY Registers / Stack) with a one-line command box, a separate code profiler window, a full multi-tab Z80 assembler IDE (`TAsmEditor` + `TZ80Assembler`) that can run and single-step the assembled program, a graphics ripper, POKE manager, RZX record/playback with embedded snapshots, and an "action script" input player. No remote protocol, no scripting language beyond the action script, no trace log, no rewind, no beam or contention visualization.

## Evidence sources and short names

All paths are relative to the workspace root `ZXSpin/`. Delphi form resources (`RT_RCDATA` "DFM" streams) were extracted from `ZXSpin.exe` with `pefile` and converted to text; published method names were recovered from the Delphi method tables inside the `CODE` section and matched to function addresses.

| Short name | What it is |
|---|---|
| `DFM:TDebuggerForm` etc. | Delphi form resource `RT_RCDATA/TDEBUGGERFORM` of `ZXSpin/ZXSpin.exe` (controls, captions, menus, `ShortCut` values decoded to key names) |
| `MT:Name@addr` | Published method `Name` of the form's class at `addr`, from the Delphi method table (e.g. `MT:NewBreakpoint1Click@0x569B2C`) |
| `DEC:<chunk>:<line>` | `ZXSpin/analysis/decompiled/<chunk>.c:<line>` (Hex-Rays output; the header line of each function) |
| `ASM:<chunk>` | `ZXSpin/analysis/disasm/<chunk>.asm` |
| `STR:"…"` | String literal inside `ZXSpin.exe` (`strings`, or a Delphi `AnsiString` constant read at the address the code references) |
| `INI:[Section]Key` | `ZXSpin/spin.ini`; `CFG:[Section]Key` = `ZXSpin/Default.spincfg` |
| `README` | `ZXSpin/analysis/README.md` (prior analysis notes and renames) |

Several names given by the prior analysis are misleading and are corrected here: `Dbg_RunSingleFrame` (0x55CDDC) is the published method `SingleStep` and executes one instruction; `FrameTiming_ptr` points at the Z80 register file (PC at +36, the registers at +24..+57); `Emu_RunFrame_Fast` (0x584360) is the debug-aware frame loop, selected only when breakpoints exist.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| CPU & registers | Registers panel | PC, SP, AF, BC, DE, HL, IX, IY, AF', BC', DE', HL', I, R, IM, flag header `S Z 5 H 3 V N C` with 8 flag check boxes, "TStates" counter, "Interrupts" (IFF) check box *(resource)* | `DFM:TDebuggerForm` `RegistersDisplay`; `MT:UpdateRegisters@0x55D42C` |
| CPU & registers | In-place register editing | Click a register label, overlay memo `RegEdit` accepts a value; flag and IFF check boxes toggle directly *(resource)* | `DFM:TDebuggerForm` `RegEdit`, `CheckBox1..8`, `IntsCheckBox`; `MT:SelectReg@0x56FDB0`, `MT:RegEditKeyPress@0x570B10` |
| CPU & registers | 8-bit register view | View > "8Bit Registers" splits pairs *(resource, config)* | `DFM:TDebuggerForm` `N8BitRegisters1`; `INI:[Debugger]Reg8Bit` |
| CPU & registers | Stack pane | Paint box of stack words, 1/2/4/8 words per line or Auto, own scroll bar *(resource)* | `DFM:TDebuggerForm` `StackDisplay`, `StackPopup`; `MT:PaintBox3Paint@0x55F5E0` |
| CPU & registers | Push / Pop / Change SP | Tools menu and disassembly pop-up: "Push Register or Value:", "Pop Register:", "New Stack Pointer:" *(resource, strings)* | `MT:PushValue2Click@0x56F80C`, `MT:PopValue2Click@0x56F9FC`; `STR:"Push Register or Value:"` |
| Disassembly | Disassembly view | Owner-drawn list with reorderable, resizable, hideable columns: Address, Label, Bytes, T-States, Instruction, Comment, Exec *(resource)* | `DFM:TDebuggerForm` `HeaderControl1`, `PopupMenu3`; `MT:PaintBox1Paint@0x558DE8`, `MT:BuildDisassembly@0x552E18` |
| Disassembly | Bank / base selector | "Bank:" and "Base:" combo boxes and an address combo defaulting to `main:PC`; follow Program Counter / Stack Pointer / HL Register *(resource)* | `DFM:TDebuggerForm` `cbBank`, `cbBase`, `AddressBar`; `MT:cbBankChange@0x55F3D8` |
| Disassembly | Data typing of ranges | Mark selection as Code, Byte data, Word data, Text; "Advanced…" dialog with line breaks by fixed count, absolute delimiter value, or OR'ed delimiter value (bit-7 terminated strings) *(resource)* | `DFM:TDebuggerSelectionForm`; `MT:SetAsType@0x57177C`, `MT:Advanced1Click@0x5726D4` |
| Disassembly | Inline edit mode | Ctrl+E: overlay editor for "Enter instruction", "Enter bytes to write to memory", "Enter comment", "Enter Label [, Length]"; ROM blocked unless "Allow ROM Editing" *(resource, strings)* | `MT:EditModeToggle@0x5652AC`; `DEC:chunk_07200_07400:12886`; `INI:[Debugger]AllowRomEdit` |
| Disassembly | Restore ROM | Tools > "Restore ROM" reloads the ROM image after edits *(resource)* | `MT:RestoreROM1Click@0x56F320` |
| Disassembly | Save disassembly | Ctrl+D; output options address, instruction bytes, lower case, source format, DEFBs, tabs *(resource, config)* | `MT:SaveDisassembly1Click@0x562A60`; `INI:[Debugger]OutAddress…OutTabs` |
| Disassembly | Send to assembler / Underlay source | "Send to assembler" opens the range in an assembler window; "Underlay Source…" and "Assemble File…" run the assembler file dialog in modes 4 and 5 *(resource, decompiled)* | `MT:UnderlaySource1Click@0x56C8B0`, `MT:AssembleFile1Click@0x56CA4C`; `DEC:chunk_07400_07600:1351`, `:1434` |
| Memory views | Memory tab | Hex dump with Address / Items / Characters columns, items per line 1..32 or Auto, bytes or words, character set Windows / Spectrum / Mixed, bank combo *(resource)* | `DFM:TDebuggerForm` `HexDump`, `MemDisplayPopup`, `MemDisplayHeaderPopup` |
| Memory views | Copy / Fill / Load / Save block | Alt+C copy block, Alt+F fill (8-bit, 16-bit, hex bytes, text), Ctrl+L load binary, Ctrl+S save binary, each with a RAM page selector (Main Memory, Bank 0..7) *(resource)* | `DFM:TDebuggerInputForm`; `MT:CopyMemoryBlock1Click@0x563EC8`, `MT:FillMemoryBlock2Click@0x563D30` |
| Memory views | Find | Ctrl+F / F3: 8-bit, 16-bit/address, hex bytes, text; include relative jumps, match case, forward/backward, snap to instruction; "Find Selected Address" and "Find Instruction Address" *(resource)* | `DFM:TDebuggerFindForm`; `MT:FindValueReference2Click@0x5636C0`, `MT:FindAddress1Click@0x56377C` |
| Memory views | Label hover tooltip | Hovering a label shows name, "(n bytes)[..]" or "(n words)[..]" preview of its data, and its comment *(decompiled)* | `MT:Timer2Timer@0x56AFD0` `DEC:chunk_07400_07600:298` |
| Navigation & bookmarks | Go To / history | Ctrl+G go to, Ctrl+Shift+G go to instruction's address operand, Ctrl+P current PC, Ctrl+R previous PC, Back/Forward drop-down buttons *(resource)* | `DFM:TDebuggerForm` `GoTO3`, `BackButton`, `ForwardButton`; `MT:PreviousPC1Click@0x572580` |
| Symbols & labels | Labels/Comments tab | Label table with value, type (byte, word, text, …), length, name, comment; 533-byte records *(decompiled)* | `DFM:TDebuggerForm` `SymbolTab`; `DEC:chunk_07400_07600:1470` |
| Symbols & labels | Import / export labels | "Open symbols file" parser: `NAME value [type[,len]] ;comment`, type letters `b`,`w`,`d`,`e`,`t` (text with delimiter); Export menu item present *(decompiled, resource)* | `MT:ImportSymbols1Click@0x5724DC`; `DEC:chunk_07400_07600:1470` |
| Symbols & labels | Assembler label transfer | Assemble dialog "Transfer Labels to Debugger" *(resource)* | `DFM:TAsmFileForm` `CheckBox2` |
| Breakpoints | Execution breakpoints | Ctrl+B toggle / double-click, Ctrl+Shift+B set/edit dialog; key = address + memory page (Any, Main Memory, Bank 0..7, ROM 0..3, IF1 ROM, Multiface ROM/RAM) *(resource, decompiled)* | `DFM:TBreakPointInputForm`; `MT:SetBreakPoint@0x55C75C` `DEC:chunk_07200_07400:8456` |
| Breakpoints | Conditional breakpoints | Any breakpoint can carry a condition expression; a breakpoint with no address (key `0x80000000`) is a pure condition checked every instruction *(decompiled)* | `DEC:chunk_07200_07400:8456`; `DEC:chunk_07400_07600:1196` |
| Breakpoints | Memory read/write and port breakpoints | Via condition variables `RADDR`, `WADDR`, `RPORT`, `WPORT`, `VALUE`, `RWWORD` set by the memory and port access paths *(decompiled)* | `DEC:chunk_07200_07400:14456`; `ASM:chunk_07200_07400` handlers at 0x5691B8..0x5692A2 |
| Breakpoints | Breakpoints tab | List of Address / Memory / Condition / Comment; pop-up Edit, Disable, Unset, Go To Breakpoint Address; status bar "No Breakpoints / 1 Breakpoint / n Breakpoints" *(resource, strings)* | `DFM:TDebuggerForm` `BreakpointsTab`, `PopupMenu2`; `DEC:chunk_07200_07400:16092` |
| Breakpoints | Ignore page | Option "Ignore page in breakpoints" *(resource, config)* | `DFM:TOptionsFormNew` `cbIgnoreBpPage`; `INI:[Debugger]IgnoreBPPage` |
| Breakpoints | Assembler-editor breakpoints | "Set/Unset Breakpoint" in the source editor pop-up *(resource)* | `DFM:TAsmEditor` `ToggleBreakpoint1`; `MT:ToggleBreakpoint1Click@0x53DBF0` |
| Conditions & expressions | Condition language | Registers (8/16-bit, shadow, IXH..IYL, I, R), `ROM`, `RAM`, `SCREEN`, `PAGING`, access variables, +3 FDC `MSR`, `ST0..ST3`, `CTRK0/1`, `CHEAD0/1`, `CSR0/1`, `IF1`, `TRUE/ENABLED`, `FALSE/DISABLED`, labels, `[addr]` memory read, operators `+ - * / AND & OR XOR SHL << SHR >> < > <= >= = <> !=`, `.B/.W/.L` *(decompiled)* | `DEC:chunk_07200_07400:14456` |
| Conditions & expressions | Compiled conditions | Conditions compile to a threaded bytecode run by a register-machine interpreter (jump table at 0x568F52) *(decompiled)* | `DEC:chunk_07200_07400:15498`; `ASM:chunk_07200_07400` |
| Conditions & expressions | Calculator | Typing an expression into the command box prints its value ($hex or decimal) *(decompiled)* | `MT:ExecuteString@0x560874` `DEC:chunk_07200_07400:10150` |
| Execution control | Run / Step / Step Over / Exit Function / Run to Here / Run to Selected | Alt+R, Alt+S, Shift+Alt+S, Alt+X, Shift+Alt+R; toolbar buttons *(resource, decompiled)* | `DFM:TDebuggerForm` `Run1`, `ToolBar1`; `DEC:chunk_07400_07600:2676`, `:2727`; `DEC:chunk_07200_07400:16822` |
| Execution control | Run Until | Alt+U: "Run Until…" address, address `IF` condition, or pure condition *(resource, decompiled)* | `MT:Runto1Click@0x569A58`; `DEC:chunk_07200_07400:10150` |
| Execution control | Stop / Stop and Restore | Stop the program started from the assembler, optionally restoring the registers or a full snapshot taken before it ran *(resource, decompiled)* | `DFM:TAsmEditor` `StopButton`, `StopRestoreButton`; `DEC:chunk_07000_07200:6269` |
| Execution control | Command line | One-line box: `LD reg,value`, `LD (addr),value`, `POKE addr,value`, `BP addr`, `RUN`, `RUN TO …`, `UNTIL addr [IF cond]`, `UNTIL cond`, `ASM`/`ASMR` *(decompiled)* | `DEC:chunk_07200_07400:10150` |
| Execution control | Lock paging, OUT, NMI | "Lock Paging" (Alt+L), "Output Byte to Port…" (Alt+O); main window NMI, frame advance *(resource, strings)* | `MT:OutputBytetoPort1Click@0x56411C`; `STR:"Frame advance"` |
| Tracing & logging | Log/Messages tab | Each stop appends " - Debugger Opened" or " - Breakpoint at $xxxx" with a "Registers: BC=$.. DE=$.. HL=$.. AF=$.. …" line *(strings, decompiled)* | `DEC:chunk_07200_07400:2317` |
| Profiling, heat maps, coverage, code/data logging | Executed-code bitmap | Run > "Log executed": one bit per address per 8 KB page (32 pages), shown as the Exec column; "Clear logs" zeroes it *(decompiled)* | `DEC:chunk_07400_07600:4836`, `:4855`; `DEC:chunk_07600_07800:7025`, `:7354` |
| Profiling, heat maps, coverage, code/data logging | Code profiler | "Add profile block" on a selection; window lists Name, Address range, Count, % CPU, Last / Min / Max / Avg T-states *(resource, decompiled)* | `DFM:TProfilerForm`; `DEC:chunk_07400_07600:4894`; `DEC:chunk_07600_07800:7523` |
| Video, raster & beam | Hardware Info tab | Text page: model, T-states per frame and per scanline, contention model (none / 48K / 128K / +3 / Pentagon), ROM paged, paging mode and 16 KB slot map, shadow screen *(strings, decompiled)* | `MT:BuildHardwarePage@0x55F7E4` `DEC:chunk_07200_07400:9829` |
| Video, raster & beam | Graphics ripper | Line / Char / Screen layouts, mask None / Byte / Line, width, height, line modulo, zoom x1..x16, inverse video / mask, swap order, grid, strips; import/export, send to assembler *(resource)* | `DFM:TGfxRipper`; `MT:PaintBox1Paint@0x5308BC` |
| Sound & device views | Ports tab | `$FE` (bits `xExSMBdr`), `$7FFD` (`xxCRSRam`), `$1FFD` (`xxxSMRxP`), `$FFFD`, `$BFFD`, `$3FFD` *(resource)* | `DFM:TDebuggerForm` `PortsDisplay` |
| Sound & device views | AY Registers tab | R0..R15 with names (fine/coarse pitch, noise, mixer, volumes, envelope, I/O ports) *(resource)* | `DFM:TDebuggerForm` `AYRegsDisplay` |
| Scripting, automation & remote debug | Action script | Recording > "Load Action Script": text file of `WAIT n`, `KEYPRESS k`, `KEYDOWN k`, `KEYUP k`, `SNAPSHOT file`, `DIR`, `;` comments *(decompiled)* | `DEC:chunk_06200_06400:7971` |
| History / rewind / time travel | RZX recording with snapshots | Record/playback RZX, embedded snapshots, resume an open recording from a snapshot point, "RZX Studio" snapshot browser, IN-count mismatch detection *(strings, decompiled)* | `README` RZX section; `DEC:chunk_07400_07600:6343`, `:6657`, `:6734`, `:7254`; `DFM:TRZXSnapWindow` |
| Import / export & persistence | POKE manager | Direct POKE with bank, `.pok` import, local POKE database, TipShop web search *(resource, strings)* | `DFM:TPokesForm`, `DFM:TPOKEEditorForm`; `DEC:chunk_06600_06800:4096` |
| Import / export & persistence | Window layout persistence | Every debugger-related form stores placement and visibility *(config)* | `INI:[WindowPlacements]`, `INI:[WindowVisible]` |
| UI conveniences | Customizable debugger chrome | Toggle toolbar, registers, ports, stack, command line, status bar; mouse wheel; font picker (fixed-width check); syntax and data-type highlighting; hex/decimal (Ctrl+H/Ctrl+N) *(resource)* | `DFM:TDebuggerForm` `PopupMenu5`, `View1`; `MT:Font1Click@0x56D32C` |
| Assembler | Z80 assembler IDE | Multi-tab, multi-window editor with undo/redo, regex find/replace, labels browser, messages pane, recent files, Run / Debug / Step / Step Over / Run to Selected / Exit Function / Stop / Stop and Restore *(resource)* | `DFM:TAsmEditor`, `DFM:TAsmFindForm` |
| Assembler | Assembler language | Macros, REPEAT/REPT, IF/IFDEF/IFNDEF/ELIF, STRUCT, INCLUDE/INCBIN, ALIGN, PAGE, relocatable mode, local labels *(strings)* | `STR:` directive table near `TZ80Assembler`; error list `STR:"Phasing error"` etc. |
| Assembler | Assemble dialog | From current file or external source; to memory (start address, start page incl. DivIDE ROM) or binary file; listing file; ROM writes; force RAM0 at $C000; case-sensitive labels; enable interrupts before running *(resource)* | `DFM:TAsmFileForm` |

## 2. CPU & registers

The side panel is a `TPageControl` with four tabs: Registers, Ports, AY Registers, Stack (`DFM:TDebuggerForm` `PageControl2`). The register labels are clickable (`OnClick = SelectReg`); an invisible `TMemo RegEdit` is placed over the clicked register and its `OnKeyPress`/`OnExit` write the value back (`MT:RegEditKeyPress@0x570B10`, `MT:RegEditExit@0x571020`) *(resource)*. Eight flag check boxes share `CheckBoxClick` (`MT:CheckBoxClick@0x56E6F0`), and a separate `IntsCheckBox` toggles the interrupt enable (`MT:IntsCheckBoxClick@0x56F7AC`) *(resource)*. The panel also shows the frame T-state counter ("TStates: ") and, when a tape is running, " Tape: " (`STR:"TStates: "`, `STR:" Tape: "`) *(strings)*.

The same register file is readable by name from the command line (`GetValue`, `DEC:chunk_07200_07400:10489`): `PC SP A F B C D E H L IX IY I R IXH IXL IYH IYL A' F' B' C' D' E' H' L' AF BC DE HL AF' BC' DE' HL'`; `(expr)` reads a byte from memory; anything else goes to Delphi `StrToInt` (so `$` hex works) *(decompiled)*.

## 3. Disassembly

- **Columns.** `THeaderControl` sections Address, Label, Bytes, T-States, Instruction, Comment, Exec; `DragReorder = True`, per-column show/hide from `PopupMenu3` (defaults: Address, Instruction, Executed checked). Column state handled by `MT:ColumnClick@0x56B7CC`, `MT:FixColumn@0x56BA4C`, `MT:SetCol@0x56BCC4` *(resource)*.
- **Disassembler tables.** Mnemonic templates are strings with a leading type character: `@LD BC, $NNNN` (16-bit immediate), `!LD B, $NN` (8-bit), `#DJNZ $$$+e` (relative), `%LD ($NNNN), HL` (absolute address), `?LD B, RES 0 (IY+$DD)` (undocumented DDCB/FDCB forms) (`STR:` around the `TZ80Assembler` class name) *(strings)*. Undecodable prefixes render as `DEFB $DD` / `DEFB 221` etc. (`GetDisassembly` `DEC:chunk_07200_07400:3077`) *(decompiled)*.
- **Scrolling.** A 0..65535 track bar (`CoolTrackBar1`) plus a left/right `TUpDown` (`udDis`) for byte-wise nudging *(resource)*.
- **Data types.** "Edit selection" dialog (`DFM:TDebuggerSelectionForm`): start address, data length, RAM page (Main Memory, Bank 0..7, DivIDE EEPROM, DivIDE RAM Bank 0..3), type Code / Text / Byte data / Word data, line breaks "Fixed values per line" (default 4), "Absolute delimiter value", "OR'ed delimiter value"; Set range / Clear range *(resource)*. Data typing is page-aware. Highlighting of data types is a View toggle (`Datatypehighlighting1`) *(resource)*.
- **Editing.** Ctrl+E (`EditModeToggle`) turns the list into an editor: prompts "Enter instruction", "Enter bytes to write to memory", "Enter comment", "Enter Label [, Length]"; ROM lines show " (Disabled - Can't edit ROM)" unless `AllowRomEdit` (`DEC:chunk_07200_07400:12886`) *(decompiled)*. The overlay memo has "Set Displacement from…" in its pop-up (`PopupMenu4`) *(resource)*.
- **Save disassembly.** `[Debugger]` keys `OutAddress`, `OutInstructionBytes`, `OutLowerCase`, `OutSourceFormat`, `OutDEFBs`, `OutTabs` *(config)*.

## 4. Memory views

The Memory tab (`HexDump`) is a paint box with a header (Address / Items / Characters), items per line 1, 2, 4, 8, 16, 32 or Auto, bytes or words, and the same data-typing sub-menu as the disassembly (`MemDisplayPopup`) *(resource)*. Its track bar spans 0..32767 with steps of 16 and 176 *(resource)*. Character display Windows / Spectrum / Mixed is shared with the assembler (`CFG:[AsmEditor]TextDisplayMode`, `INI:[Visual]UseZXChars`) *(config)*.

Block operations use one generic input form (`DFM:TDebuggerInputForm`): Start Address, Length, Copy To, RAM Page (Main Memory, Bank 0..7), Fill Type 8-Bit Data / 16-Bit Data / Hex Bytes / Text *(resource)*.

Find (`DFM:TDebuggerFindForm`): search type 8-Bit, 16-Bit / Address (default), Hex Bytes, Text; "Include Relative Jumps" (a 16-bit address search also matches `JR`/`DJNZ` targets), "Match Case", "Snap to Instruction", direction *(resource)*.

## 5. Navigation, symbols and labels

- History: Back / Forward tool buttons are `tbsDropDown` style, so they offer a drop-down list of earlier locations *(resource)*. "Previous PC" (Ctrl+R) jumps to the PC before the last step *(resource; the exact semantics are an open question)*.
- The address combo box defaults to `main:PC`, which suggests a `<bank>:<expression>` syntax *(inferred)*.
- Label records are 533 bytes each (`dword_8DC6D8`, count `dword_8DC6DC`): value (+0), flags/type (+4..+12), name (+21, 256 bytes), comment (+277, 256 bytes) (`DEC:chunk_07400_07600:1470`, `DEC:chunk_07400_07600:298`) *(decompiled)*.
- Symbol-file parser (`DEC:chunk_07400_07600:1470`): splits each line on space/tab; `;` starts a comment; fields are name, value (full expression, evaluated by the UI evaluator), optional type and length separated by `,`. Type letters: `b` byte data, `w` word data, `d` and `e` two further data kinds (width 4 and 3 in the parser), `t` text; a four-character `t` type such as `td`/`tt` plus a two-digit hex value sets the text delimiter as absolute (`0x100`) or OR'ed (`0x200`) *(decompiled; the meaning of `d`/`e` is open)*. `Symbols.txt` is referenced by the form constructor (`DEC:chunk_07200_07400:2036`), probably a default label file *(inferred)*.
- Labels can be used in breakpoint conditions; the compiler resolves them to their value at compile time (`v57 = *(dword_8DC6D8 + 533*idx)`, `DEC:chunk_07200_07400:14456`) *(decompiled)*.

## 6. Breakpoints

### 6.1 Data structure

The debugger form holds a dynamic array of 32-byte breakpoint records at form offset +3572, count at +3456 (`MT:SetBreakPoint@0x55C75C`, `DEC:chunk_07200_07400:8456`) *(decompiled)*:

| Offset | Content |
|---|---|
| +0 | key: bits 0..15 address, bits 16..23 page id, bits 28..30 kind flags; `0x80000000` = address-less (condition-only) |
| +4 | condition source text (AnsiString) |
| +8 | cleared string (purpose unknown) |
| +12 | list-view item of the Breakpoints tab |
| +16 | compiled condition bytecode (AnsiString used as a buffer) |
| +20 | caller parameter (used by the assembler's run-to-end breakpoint) |
| +24 | dynamic array: profile blocks that start here |
| +28 | dynamic array: profile blocks that end here |

Page ids (display strings from `SetBreakPoint`): `0x10..0x17` ROM 0..3 (two 8 KB halves each), `0x18/0x19` (no label), `0x1A` MFROM, `0x1B` MF3ROM, `0x1C` MFRAM, `0x1D` MF3RAM, `0x1E` IF1ROM, `0xFF` Any, anything else "RAM n" *(decompiled)*. The page is the value of `PageBankMap_Read[addr >> 13]`, that is an **8 KB** page, not a 16 KB bank. The dialog offers Any, Main Memory, Bank 0..7 (`DFM:TBreakPointInputForm` `ComboBox2`) *(resource)*.

Kind flags (from `Emu_PerOpcodeHook_Fast`, `DEC:chunk_07600_07800:7523`) *(decompiled)*:

- `0x20000000` profile-block entry: stamps the current T-state into each linked profile record;
- `0x10000000` profile-block exit: updates count, last, min, max and a `double` sum;
- `0x40000000` assembler "program end" breakpoint: calls `sub_53B564` (`DEC:chunk_07000_07200:6269`), which removes it and restores state (see 8.3);
- no flag: a user breakpoint that stops the machine.

A flagged record does not stop the machine, so profile blocks are breakpoints that only measure.

### 6.2 Compiled check list and check cost

Before running, `Dbg_BuildBreakpointList` (`DEC:chunk_07400_07600:1196`) compiles every record into a flat 16-byte-per-entry array (`dword_8D8688`, count `dword_8D868C`, pointer `dword_8D8690`): `{?, mask, key, condition ptr}`. The mask is `0xFFFF` for page Any and `0xFF1FFF` (page + offset within the 8 KB page) otherwise. Address-less breakpoints are appended after the address ones *(decompiled)*.

The per-instruction check `sub_56C83C` (`DEC:chunk_07400_07600:1312`) builds `PC | PageBankMap_Read[PC>>13] << 16`, scans the list linearly with `(entry.mask & key) == entry.key`, and runs the condition only on an address match. The whole check runs only when the debugger form has at least one breakpoint and no stop is pending *(decompiled)*.

The cost is kept low at the loop level. `Emu_SelectRunMode` (`DEC:chunk_08000_08128:1585`) installs the debug-aware frame loop (0x584360) only when a breakpoint exists or a run-until target is set. Otherwise the plain loop, which has no breakpoint code, runs *(decompiled)*. The debug loop calls the hook **before** each instruction and sets `byte_B9D8B2 = 1`, which turns on access capture in the memory and port paths (`DEC:chunk_07600_07800:7316`) *(decompiled)*.

### 6.3 Memory and port breakpoints

There are no separate read/write/port breakpoint types. Access breakpoints are conditions over variables that the memory and port helpers fill in while `byte_B9D8B2` is set *(decompiled)*:

| Variable | VM opcode | Source |
|---|---|---|
| `RADDR` | 88 | `word_B9D8AC` if access flag bit 2 (memory read) |
| `WADDR` | 89 | `word_B9D8AE` if bit 3 (memory write) |
| `RPORT` | 90 | `word_B9D8AC` if bit 0 |
| `WPORT` | 91 | `word_B9D8AE` if bit 1 (`Z80_OutPort` stores the port and ORs 2) |
| `VALUE` | 92 | data of the access (byte, or word when bit 4 marks a 16-bit access) |
| `RWWORD` | 93 | 1 for a 16-bit access, 0 otherwise |

Each variable returns `-1` when no access of that kind happened. Writers: `sub_57730C` (memory write, ORs 8), `sub_577608` (read, ORs 4), `sub_577920` (read-modify-write, ORs `0x0C`), `sub_578740` (16-bit read, ORs `0x14`), `Z80_OutPort` (ORs 2) (`ASM:chunk_07400_07600`, `ASM:chunk_07600_07800`) *(decompiled)*. The flags are cleared after each hook call, so an access condition becomes true at the **next** instruction boundary, after the accessing instruction has finished *(decompiled)*. Only one address slot exists per direction, so for an instruction with several accesses the later one probably overwrites the earlier *(inferred)*.

## 7. Conditions and expressions

There are two evaluators.

1. **Breakpoint and run-until compiler** (`DEC:chunk_07200_07400:14456`, 0x56797C) produces bytecode for the interpreter at 0x568F2C (`DEC:chunk_07200_07400:15498`; handlers listed in `ASM:chunk_07200_07400` from 0x5691B8) *(decompiled)*:
   - Operators (opcode): `+` 1, `-` 2, `*` 3, `/` 4 (signed), `AND`/`&` 5, `OR`/`|` 6, `XOR`/`^` 7, `SHL`/`<<` 8, `SHR`/`>>` 9, `<` 11, `>` 12, `<=` 13, `>=` 14, `=` 15, `<>`/`!=` 16. Comparisons return `-1` (true) or `0`, so bitwise `AND`/`OR` also act as logical operators.
   - Operands: 35 = literal (next dword); character constants in `'…'` or `"…"` of 1..4 characters packed big-endian; unary minus 34; `(…)` sub-expression 32; `[…]` memory byte 33; opcodes 40/41/42 read a byte, word or long at a constant address (probably the `.B`/`.W`/`.L` suffixes) *(inferred)*.
   - Register operands 48..81: A, BC, DE, HL, B, C, D, E, H, L, IX, IY, SP, PC, AF, shadow registers, IXH/IXL/IYH/IYL, I, R. Paging operands 82..85: `ROM` (from `$7FFD` bit 4), `RAM` (`$7FFD & 7`), `SCREEN` (`$7FFD` bit 3), `PAGING` (lock state). Access operands 88..93 (see 6.3). +3 FDC operands 96..106: `MSR`, `ST0..ST3`, `CTRK0`, `CHEAD0`, `CSR0`, `CTRK1`, `CHEAD1`, `CSR1` (bytes at `0x8D866C+0..10`). Constants: `TRUE`/`ENABLED` = -1, `FALSE`/`DISABLED` = 0, `IF1` = 1. Labels are folded to their values.
   - Evaluation is a two-register accumulator machine. Each operand handler moves the old result to `ebx`, each operator combines `ebx op eax`, and operators are emitted after their right operand. So evaluation is **strictly left to right with no precedence**; grouping needs `(…)` *(decompiled)*.
2. **UI evaluator** (`DEC:chunk_07200_07400:13115`, 0x565B7C; 49 callers: go to, dialogs, command line, symbol files) accepts `+ - * / & | ^ %`, `MOD`, the same comparisons and shifts, `.B/.W/.L`, register names, `ROM`, `SCREEN`, `PAGING`, `RADDR`, `WADDR`, `RPORT`, `WPORT`, `VALUE`, `RWWORD`, `RAM`, the boolean words and labels *(strings, decompiled)*.

## 8. Execution control

### 8.1 Stepping and run-until encoding

Run targets go into one 32-bit variable (`dword_8D86A4`, copied to `*off_621AC8`) plus an optional condition string (`dword_8DC6D4`) *(decompiled)*:

| Encoding | Meaning | Set by |
|---|---|---|
| `0xFF000000 + addr` (`addr - 0x1000000`) | run until PC = addr, optionally only if the condition holds | Step Over (address after the current instruction, length from the disassembler), Run to Here, Run to Selected, `UNTIL addr [IF cond]` |
| `0xFE000000 + x` (`- 0x2000000`) | Exit Function: arms a flag at `off_6221DC` (= 1); the stop test in the hook only checks `dword_B9D924` *(inferred: a call-depth or RET tracker)* | `DEC:chunk_07400_07600:2727` |
| `0xFD000000 + x` (`- 0x3000000`) | run until the condition alone is true | `UNTIL cond` |

References: `DEC:chunk_07400_07600:2676` (Step Over), `DEC:chunk_07200_07400:16822` (Run to Here), `DEC:chunk_07400_07600:2707` (Run to Selected), `DEC:chunk_07600_07800:7523` (checks). Single Step (`MT:SingleStep@0x55CDDC`, `DEC:chunk_07200_07400:8753`) rebuilds the breakpoint list, executes one opcode with sound, tape and peripheral ticks, calls the hook, and handles the frame wrap *(decompiled)*.

### 8.2 Command line

`MT:ExecuteString@0x560874` (`DEC:chunk_07200_07400:10150`) *(decompiled)*:

- If the whole line evaluates as a number, it prints the value (hex with `$` when hex display is on).
- `LD <reg>,<expr>` writes any register named above; `LD (<addr>),<expr>` writes memory.
- `POKE <addr>,<byte>` writes memory.
- `BP <addr>` toggles a breakpoint.
- `RUN` resumes; `RUN TO …` is parsed and turned into `UNTIL`.
- `UNTIL <cond>` (condition-only), `UNTIL <addr>` or `UNTIL <addr> IF <cond>`.
- `ASM` and `ASMR` are recognized, but their branches are empty in this build.

### 8.3 Running code from the assembler

The assembler's Run / Debug buttons plant a hidden `0x40000000` breakpoint at the program's end. When it fires, `sub_53B564` (`DEC:chunk_07000_07200:6269`) removes it and then either (Stop and Restore) reloads a snapshot captured before the run through `sub_602870`, or (Stop, or natural return) copies back the saved 64-byte register block and PC. Assembled code can therefore be run like a subroutine and the machine returned to its earlier state *(decompiled; the exact trigger mapping is inferred)*.

## 9. Tracing and logging

There is no instruction trace log. The Log/Messages tab (`LogWnd`, read-only `TMemo`) receives a line per stop, " - Debugger Opened" or " - Breakpoint at …" followed by a "Registers: BC=$…, DE=$…, HL=$…, AF=$…, BC'=$… …" dump (`DEC:chunk_07200_07400:2317`), plus assembler errors (" errors - see log") *(strings, decompiled)*. The `\SpeccyGFX.log` string belongs to the renderer plug-in, not the debugger (`README`) *(strings)*.

## 10. Profiling, coverage and code/data logging

- **Executed-code bitmap** ("Log executed", `DEC:chunk_07400_07600:4836`): toggles bit 0 of `Z80_DebugFlags`. Both frame loops then set one bit per executed PC in `ExecCoverageBitmap[256*page + …]`: 32 pages x 1 KB = one bit per byte of each 8 KB page, covering ROM, RAM and interface pages (`DEC:chunk_07600_07800:7025`, `:7354`). "Clear logs" zeroes 32 x 1024 bytes (`DEC:chunk_07400_07600:4855`). The disassembler reads the bitmap for the Exec column (`DEC:chunk_07200_07400:5193`) *(decompiled)*. Only instruction starts are marked; reads and writes are not logged.
- **Code profiler** (`DFM:TProfilerForm`, caption "Code profiler"): "Add profile block" (`DEC:chunk_07400_07600:4894`) asks for a name (default `profileN`), takes the selection start as the entry address and the selection end as the exit address, both page-qualified (`addr & 0x1FFF | page << 16`), creates or reuses breakpoints flagged `0x20000000` / `0x10000000`, and appends a 304-byte record: +0 entry key, +4 exit key, +8 last, +12 entry T-state stamp, +16 count, +20 min, +24 max, +28 sum (double), +48 name (256). At exit the hook computes `Emu_TotalTstates + frame_tstate - entry_stamp` (`DEC:chunk_07600_07800:7523`) *(decompiled)*. Columns: Name, Address range, Count, % CPU, Last / Min / Max / Avg Tstates; pop-up Clear stats, Clear all, Remove *(resource)*. Nested or re-entrant blocks keep only one entry stamp, so recursion is not handled *(inferred)*.

## 11. Video, raster and beam

There is **no** beam position display, no raster or event viewer and no contention map in the debugger. Contention appears only as text on the Hardware Info tab (`BuildHardwarePage`, `DEC:chunk_07200_07400:9829`): "Contention: No Contention applied / 48k / 128k / +3 Contention Timing / Pentagon 128 Display Timing", T-States per frame and per scanline, ROM paged (incl. DivIDE and IF1), "+3 Paging is in Special Mode", slot map `$0000..$3FFF: ROM n` / `Page n`, "Screen is mapped to Page 7 (Shadow Screen)", "Paging is Disabled/Enabled" *(strings)*. Contention itself lives in the core (`ContDelayTable` 0x9DA890, `IOContDelayTable` 0x621CB4, `README`), with no UI.

The graphics ripper (`DFM:TGfxRipper`) is a memory-as-graphics viewer and exporter (see registry). It is not a sprite *finder*: no search or auto-detection strings or controls exist *(resource)*.

## 12. Scripting, automation and remote debugging

- No remote debugging protocol, no scripting language. `SpinNet.cfg` is a 9-byte network setting for the TipShop / game finder, not a debug server *(config, inferred)*.
- **Action script** (`DEC:chunk_06200_06400:7971`): Recording > "Load Action Script" (`*.Script`). Lines are `COMMAND argument`, `;` comment. Commands `WAIT n`, `KEYPRESS key`, `KEYDOWN key`, `KEYUP key`, `SNAPSHOT file`, `DIR` are encoded into an input queue (`dword_6304A8`) *(decompiled)*. This is an input-injection player, not a debugger script.

## 13. History, RZX and snapshots

No rewind. RZX (`README`; `DEC:chunk_07400_07600:6343` `RZX_Start`, `:6657` `RZX_StartRecording`, `:6734` `RZX_PlaybackNextFrame`) *(decompiled, strings)*:

- Playback feeds `IN` results from the recording. A mismatch reports "n INs expected / n INs executed in Frame n" and offers to restart recording.
- Recording embeds snapshots. When closing, the user can finalize or "leave it open for a later session", and a later session can "overwrite, or resume recording from a snapshot point". Stepping back past the start gives "This is the first snapshot block in the recording" (`DEC:chunk_07400_07600:7254`), which implies rollback to earlier snapshot blocks *(inferred)*.
- "RZX Studio…" opens `TRZXSnapWindow` ("RZX Snapshots"): two image panes and nine buttons, probably a thumbnail browser of the embedded snapshots *(resource, inferred)*.
- Status bar: " RZX Replaying - ", " RZX Recording Active", " RZX Recording Suspended" (`DEC:chunk_08000_08128:3335`) *(strings)*.

## 14. Assembler

- **Editor** (`DFM:TAsmEditor`): `TSPINEdit` text panes with gutter, syntax highlighting, highlight bar, Spectrum/Windows/Mixed charset; tabs (Add, Duplicate, Close tab); "New Window"; docked "Labels" (Alt+B) and "Messages" (Alt+M) panes; Undo/Redo, Cut/Copy/Paste, Find (Ctrl+F), Replace (Ctrl+H) with regular expressions and scope/direction; go to line or label (Ctrl+G); Revert (F5); Assemble (Alt+A); Run (Alt+X), Debug (Alt+D), Stop, Stop and Restore; "Convert text…" *(resource)*. Options: backups, tab conversion, tab size, "Generate debug info", "Allow assembly to ROM", DEFS default byte (`CFG:[AsmEditor]`, `INI:[Assembler]`) *(config)*.
- **Language** (directive table beside `TZ80Assembler`, `TAsmMacro`, `TAsmSrcFile`): `ALIGN ASMREAD ASMWRITE BASE CODE DATA DEFB DEFINE DEFM DEFS DEFSW DEFW DISABLE ENABLE ELIF ELSE ENDIF ENDM ENDR ENDREP ENDREPEAT ENDSTRUC ENDSTRUCT IFDEF IFEQ IFNDEF IFNE INCBIN INCLUDE LIST LOAD MACRO NDEF NOLIST NOOPT OFFSET PAGE REPEAT REPT RESB RESW RETURN ROTATE STRUCT TITLE` *(strings)*. The error list covers macros with named parameters, `ROTATE`, nested structures, single-line `REPEAT`, local labels ("Local label before global label"), "Relocatable mode must be set before any code is generated", "Phasing error", "Instruction violates page bounds" *(strings)*.
- **Assemble dialog** (`DFM:TAsmFileForm`): see registry. The GFX ripper and the debugger can "Send to" a new assembler window *(resource)*.
- A Sinclair BASIC parser with keyword tokens `BEEP-1001` … `VAL$-1089` and syntax help strings is also in the binary *(strings)*. It is not wired to the debugger as far as the strings show.

## 15. POKE tools

`DFM:TPokesForm` tabs: "Poke Memory" (address 0..65535, or 0..16383 with a bank 0..7; value; "Add From File" `.pok`; current POKE list), "POKE Database" (search, Game Name / Publisher list, Get POKEs, Delete Game, Add), "TipShop" (web search `http://www.the-tipshop.co.uk/cgi-bin/search.pl?name=`) *(resource, strings)*. There is **no POKE finder** (no lives/value-change search strings or controls) *(strings, resource)*.

## 16. Notable and unique ideas

1. **Profile blocks built from non-stopping breakpoints.** Entry/exit breakpoints flagged as "measure only" give count, last, min, max and average T-states per named code range. The same engine as breakpoints, zero cost when unused. A cheap, very useful timing tool for Spectrum code (multicolor loops, interrupt handlers).
2. **Page-qualified breakpoints on 8 KB granularity**, with "Any" as the default and a global "Ignore page in breakpoints" switch. Breakpoints survive paging correctly, including ROM halves, IF1 ROM and Multiface RAM/ROM.
3. **Compiled conditions.** Conditions are compiled once to bytecode and run by a tight threaded interpreter only on address match. Address-less conditions act as "stop when …" watch expressions.
4. **Access breakpoints as condition variables** (`RADDR`, `WADDR`, `RPORT`, `WPORT`, `VALUE`, `RWWORD`) instead of separate types. One grammar covers "write of 0 to 23672 from bank 7", "OUT to $FE with bit 4 set", and so on. Capture is switched on only in the debug loop.
5. **+3 FDC state in conditions** (`MSR`, `ST0..ST3`, `CTRKn`, `CHEADn`, `CSRn`). Rare in any emulator, handy for loader and copy-protection work.
6. **Assembler-to-machine round trip:** assemble to memory, transfer labels, run or debug from the editor with editor breakpoints, then "Stop and Restore" the pre-run machine state. Source-level debug info is optional.
7. **Data typing with delimiter-driven line breaks** (absolute or OR'ed terminator, bit-7 strings), page-aware and importable from symbol files with type letters.
8. **Per-page executed bitmap** feeding an Exec column in the disassembly: a simple code/data separation aid.
9. **Run-mode swap**: the debug loop replaces the plain loop only while breakpoints or run targets exist, so debugging costs nothing when idle.
10. **Label hover tooltip** showing the label's typed data inline.

## 17. Gaps and caveats

- No beam/raster position, no per-T-state view, no contention visualization, no scanline events: nothing comparable to Unreal Speccy's beam screen or Xpeccy's `RAY`.
- No trace log, no history or rewind (only RZX snapshot blocks), no call stack view, no watch window (only address-less condition breakpoints).
- Condition evaluation has no operator precedence (strict left to right). Access variables hold one address per direction and are tested one instruction late.
- The breakpoint check is a linear scan per instruction. It is fine for tens of breakpoints; there is no per-address lookup table.
- Breakpoint dialog pages offer only Any / Main Memory / Bank 0..7, while the engine keys on 8 KB pages; ROM, IF1 and Multiface pages come only from setting the breakpoint where they are currently paged in *(inferred)*.
- "Disable Breakpoint", "Remove profile block", "Export Labels…", "Lock Paging" and "Stop Executing" have no `OnClick` in the form resource. They may be wired in code or unfinished *(resource)*.
- Only Pentagon among the non-Sinclair clones; the debugger has no TR-DOS / Beta Disk state variables (the +3 uPD765 does).
- The command-line `ASM`/`ASMR` verbs are recognized but do nothing in this build *(decompiled)*.
- Evidence limits: all claims come from resources, strings and Hex-Rays output. The IDA database was not opened, so the items in the open-questions list need confirmation.
