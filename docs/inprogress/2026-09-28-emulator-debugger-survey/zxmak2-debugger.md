# ZXMAK2 debugger — capability survey

**Source:** https://github.com/zxmak/ZXMAK2 · local checkout commit `4964327` (2023-03-08, "reuse save/open dialog to store dialog state"; last tagged release 2.9.3.8) · C# / .NET Framework 4, WinForms + Managed DirectX (Windows only)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** ZXMAK2 has no single debugger. Debuggers are pluggable *bus devices* (`BusDeviceCategory.Debugger`, interface `IJtagDevice`) chosen per machine in `machines.config`. Five ship: the **default debugger** (`ZXMAK2.Hardware.General.Debugger`, WinForms `FormCpu`), **DebuggerEx** (docking MVVM variant of the same features), **Adlers debugger** (`ZXMAK2.Hardware.Adlers.Debugger`: command line, conditional/memory breakpoints, trace filters, comments, integrated Pasmo assembler, graphics viewer), the **Sprinter debugger** (default + Sprinter MMU ports) and a **GDB remote stub** (`ZXMAK2.Hardware.GdbServer`, TCP, gdb-z80). No scripting language, no rewind, no beam/raster visualizer. Paths below are relative to the repository root (`ZXMAK2/`).

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| Architecture | Debugger = bus device | Each debugger is a `BusDeviceBase` + `IJtagDevice`; `Attach(IDebuggable)` hands it the VM; registers an "open" command in the main UI | `ZXMAK2/src/ZXMAK2.Hardware/General/Debugger.cs:11-60` |
| Architecture | `IDebuggable` contract | Reset / StepInto / StepOver / Run / Stop, Read/WriteMemory (byte + block), Add/Remove/Get/Clear breakpoints, `UpdateState` + `Breakpoint` events, `CPU`, `Bus`, `GetFrameTact()`, `FrameTactCount`, `RzxState` | `ZXMAK2/src/ZXMAK2.Engine/Interfaces/IDebuggable.cs:8-35` |
| Architecture | Bus event hooks | Per-address 64K delegate tables for memory read (M1 and non-M1 separately), memory write, port read/write, no-MREQ cycles, plus pre-cycle, reset, INT/NMI ack, begin/end frame | `ZXMAK2/src/ZXMAK2.Engine/EventManager.cs:105-187` |
| CPU & registers | Register list (default) | `PC IR SP AF HL DE BC IX IY AF' HL' DE' BC' MW` (MEMPTR) as hex | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:151-167` |
| CPU & registers | Flag list | `S Z F5 H F3 P/V N C` as 0/1 | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:168-176` |
| CPU & registers | CPU state list | `IFF1/IFF2`, `HALT`, `BINT`, `IM`, `FX` (prefix state), `XFX` (ED/CB state), `LPC` (last PC), absolute `Tact` | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:178-186` |
| CPU & registers | RZX playback counters | `rzxm` fetch n/N, `rzxi` input n/N, `rzff` frame n/N shown while an RZX plays | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:187-192` |
| CPU & registers | Edit registers | Double-click a register: hex input dialog; double-click a flag: toggles bit; double-click IFF/HALT/IM: toggles/cycles | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:349-424`, `:490-510` |
| CPU & registers | Stack view (Adlers) | 10 words from SP (`addr: value`), toggled with the breakpoint list by F12 | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:144-177` |
| CPU & registers | Window title T-states (Adlers) | `Z80 CPU(Tact=<abs> frmT=<frame tact>)` | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:286-288` |
| CPU & registers | MVVM register panel (DebuggerEx) | Bindable properties incl. `Wz`, `Lpc`, `Im`, flags, RZX strings | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/ViewModels/RegistersViewModel.cs:30-328` |
| Disassembly | Disassembly panel | Owner-drawn: address, bytes, mnemonic, `; <n>T` timing column; PC arrow + breakpoint dot in the gutter | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/DebugPanels.cs:8-128`, `FormCPU.cs:221-227` |
| Disassembly | Instruction timing column | `TimingTool.GetTimingString()` = uncontended T-states of the instruction (`"4T"`, `"N/A"`) | `ZXMAK2/src/ZXMAK2.Engine.Cpu/Tools/TimingTool.cs:38-44` |
| Disassembly | Syntax highlighting (Adlers) | Mnemonics salmon, numbers green, jump/call/ret/rst violet, timing gray, notes inline italic | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/CustomControls/DebugPanels.cs:270-383` |
| Disassembly | Follow operand (Adlers) | Right-click: the first `#XXXX` in the line offered as "dump memory at" / "follow in disassembly" | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:1400-1436`, `DebugPanels.cs:384-397` |
| Disassembly | Save disassembly (Adlers) | Range prompt, writes `dis.asm` in the app folder | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:604-633` |
| Memory views | Hex/ASCII data panel | Configurable 1..32 columns, ZX charset (`zxencode`) column, click to POKE | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/DebugPanels.cs:527-690`, `FormCPU.cs:434-476` |
| Memory views | Find bytes (Adlers) | Comma-separated byte/word list, wraps around 64K, Ctrl+F / Ctrl+N (next) | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:681-792` |
| Memory views | Memory map window | Live CMR0/CMR1 (`7FFD`/`1FFD` style) values, page name mapped in each 16K window, DOSEN/SYSEN, PropertyGrid of the memory device; CMR editable | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/FormMemoryMap.cs:20-123` |
| Memory views | Sprinter MMU dump | Ports `89 RGADR`, `C9 RGMOD`, `82/A2/C2/E2 PAGE0..3`, `7FFD`, `1FFD` | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/Sprinter/DebugForm.cs:636-645` |
| Navigation & bookmarks | Goto address | Dasm and data panels each have "Goto" dialog; Adlers remembers last value; Ctrl+G / Ctrl+D (Adlers) | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:315-335`, `Adlers/Views/FormCPU.cs:1439-1450`, `:2144-2155` |
| Navigation & bookmarks | Back/forward history (Adlers) | Mouse XButton1/XButton2 walk a disassembly address history | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/CustomControls/DebugPanels.cs:399-427`, `:706-715` |
| Symbols & labels | Code comments + notes (Adlers) | Per-address comment (own line above) and note (inline); Ctrl+1 / Ctrl+2; XML load/save | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:1463-1663` |
| Symbols & labels | Assembler symbols (Adlers) | Symbol table from Pasmo compile; right-click inserts symbol as comment/note at its address | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/Assembler/Assembler.cs:289-305`, `:648-666` |
| Breakpoints | Execution breakpoint | `Breakpoint(ushort addr)` = predicate `PC == addr`, label `PC==#XXXX`; toggle by clicking gutter | `ZXMAK2/src/ZXMAK2.Engine/Entities/Breakpoint.cs:20-24`, `FormCPU.cs:248-264` |
| Breakpoints | Arbitrary predicate breakpoint | `Breakpoint(label, Predicate<IMachineState>)` API (used by Adlers) | `ZXMAK2/src/ZXMAK2.Engine/Entities/Breakpoint.cs:13-18` |
| Breakpoints | Conditional breakpoint (Adlers) | `br <lhs> <op> <rhs> [&& <lhs> <op> <rhs>]`, JIT-compiled to IL | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:1810-1874`, `Core/ILProcessor.cs:11-187` |
| Breakpoints | Memory read / write breakpoint (Adlers) | `br memread A [B]`, `br memwrite A [B]` (single address or inclusive range) | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:1819-1842`, `:1952-1999` |
| Breakpoints | Memory-value breakpoint (Adlers) | `br (#4000) == #FF` — evaluated only after a memory write happened | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/BreakpointAdlers.cs:26-40`, `:73-101` |
| Breakpoints | Opcode breakpoint (Adlers) | `br (pc) == #F3` (byte or word at a register) | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Core/ILProcessor.cs:142-184` |
| Breakpoints | Enable/disable/delete (Adlers) | `on N`, `off N`, `del N`, `del all`; double-click in list toggles | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:1111-1126`, `:1875-1895`, `:829-846` |
| Breakpoints | GDB Z0-Z4 breakpoints | Exec via engine breakpoint; read/write/access watch via bus hooks | `ZXMAK2/src/ZXMAK2.Hardware.GdbServer/Gdb/GDBSession.cs:342-367`, `GDBJtagDevice.cs:105-117` |
| Conditions & expressions | Operators | `==`, `!=`, `<`, `>` only; optional second clause with `&&` | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Core/ILProcessor.cs:189-211`, `DebuggerManager.cs:145` |
| Conditions & expressions | Operands | 16-bit regs `AF BC DE HL IX IY SP IR PC`, 8-bit `A B C D E F H L`, flags `fZ fC fPV fH fS fN`, `(addr)`, `(reg)`, numbers | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/DebuggerManager.cs:113-114`, `:330-338` |
| Conditions & expressions | Number syntax | bare = hex, `#`/`x`/`0x` hex, `%` binary, `$` **decimal** | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Core/ConvertRadix.cs:60-97` |
| Watchpoints & watches | None beyond breakpoints | No watch list / expression view in any debugger | *(inferred from all debugger views)* |
| Execution control | Step into (F7) | Executes one instruction including all prefixes (loops while `FX`/`XFX` not None) | `ZXMAK2/src/ZXMAK2.Engine/Spectrum.cs:270-276` |
| Execution control | Step over (F8) | Run to PC+len unless mnemonic contains `J` or `RET`; asks the user after 71680*50*5 T and doubles the limit | `ZXMAK2/src/ZXMAK2.Engine/Spectrum.cs:278-324`, `:80` |
| Execution control | Run / Break / Reset / NMI | F9 (default) or F10 (Adlers) run, F5 stop, F3 reset; `DebugNmi()` exists | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:266-313`, `Spectrum.cs:136-148` |
| Execution control | Step out | Toolbar button present, always "Not implemented" | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:645-648`, `ViewModels/DebuggerViewModel.cs:35-38` |
| Execution control | Set frame T-state | Double-click status `T: n / N`: advances `CPU.Tact` so the frame tact equals the entered value | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:586-599` |
| Tracing & logging | Instruction trace (Adlers) | Pre-cycle hook; unfiltered: `#PC   mnemonic` per instruction to the logger | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:947-1052` |
| Tracing & logging | Filtered trace = execution counters (Adlers) | Filters: all jumps/calls, conditional jumps, conditional calls, one opcode, "jump/call landing on address X", address ranges; per-address hit counts + total T-states written to a log file | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Core/DebuggerTrace.cs:16-49`, `:98-165` |
| Tracing & logging | Device I/O logs | `logIo` attribute on FDC (WD1793), IDE (Profi, ATM, PentEvo, SMUC, Sprinter), CovoxBlaster: register, value, PC, T | `ZXMAK2/src/ZXMAK2.Hardware/General/FddController.cs:238-303` |
| Tracing & logging | log4net sink | Async appender → rolling file `C:\Logs\ZXMAK2.log`, level ALL | `ZXMAK2/src/ZXMAK2/log4net.config:1-58` |
| History / rewind | None | No history, no reverse step; RZX playback only | — |
| Video, raster & beam | Frame T-state readout | Status bar `T: <frameTact> / <FrameTactCount>`, orange "Running" / blue "Ready" | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:129-134` |
| Video, raster & beam | OSD "Debug Info" | FPS (render/update/device), back/frame buffer size, sound kHz, `FrameStart: nT`, min/avg/max frame and latency times | `ZXMAK2/src/ZXMAK2.Host.WinForms/Mdx/Renderers/OsdRenderer.cs:171-226` |
| Video, raster & beam | Graphics viewer/editor (Adlers) | Views memory as ZX screen, sprite (w/h selectable, mirror), tile, "Jetpac" layout; zoom; export PNG/BMP/JPG/bytes | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/GraphicsEditor/GraphicsEditor.cs:72-242`, `:519-575` |
| Video, raster & beam | Beam position / raster overlay | Not present | — |
| Sound & device views | WD1793 debug window | Live `Wd1793.DumpState()` text refreshed by timer | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/dbgWD1793.cs:20-25` |
| Profiling, heat maps, coverage | Trace counters (Adlers) | Per-address execution counts for filtered instructions + T-state sum (see Tracing) | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Core/DebuggerTrace.cs:120-165` |
| Scripting, automation & remote | GDB remote stub | TCP port 2000 (configurable), `g G m M p P s c D z Z ? q`, gdb-z80 register order | `ZXMAK2/src/ZXMAK2.Hardware.GdbServer/Gdb/GDBSession.cs:139-219` |
| Scripting, automation & remote | Integrated assembler (Adlers) | Pasmo2.dll: compile to memory (refuses ROM) or TAP+BASIC loader; macros/defines | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/Assembler/Assembler.cs:102-330`, `Compiler.cs:68`, `Help/Assembler.html` |
| Scripting, automation & remote | Lua / other scripting | None found | — |
| Scripting, automation & remote | Console test harness | `src/Test`: ULA contention sanity patterns, ZEXALL, benchmark | `ZXMAK2/src/Test/Program.cs:19-166`, `:361` |
| Import / export & persistence | Block load/save | Load binary at addr/len, save addr/len to `.bin` (default `#4000`, 6912) | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:515-584` |
| Import / export & persistence | Save as DEFB (Adlers) | Range to `membytes.asm`, 8 bytes per `DEFB` line | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:636-678` |
| Import / export & persistence | Breakpoint files (Adlers) | `savebrs f` / `loadbrs f`: one condition per line, `;` comments, relative to app folder | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:2014-2063` |
| Import / export & persistence | Debugger config (Adlers) | `debugger_config.xml`: trace options, ranges, file name, assembler settings (breakpoints: literal "ToDo") | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:2272-2455` |
| UI conveniences | Docking layout (DebuggerEx) | WeifenLuo DockPanel: Registers + Breakpoints right, Disassembly document, Memory bottom | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/Views/FormDebuggerEx.cs:48-75` |
| UI conveniences | Auto-show on breakpoint | Breakpoint event: `Show()`, refresh, focus dasm | `ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:111-123` |
| UI conveniences | Command line (Adlers) | Enter executes, red error text in place, autocomplete from history, `lo`/`sa`/`a` expand to `loadbrs `/`savebrs `/`asm`; double-click dasm inserts `#addr` | `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:1063-1396`, `:813-821` |

## 2. Architecture: debuggers as pluggable devices

- The VM (`VirtualMachine`) implements `IDebuggable`; the bus calls `SetDebuggable(this)` so any device implementing `IJtagDevice` receives `Attach(dbg)` (`ZXMAK2/src/ZXMAK2.Engine/VirtualMachine.cs:325-329`). A debugger is added to a machine by listing it in `machines.config`, e.g. `<Device type="ZXMAK2.Hardware.General.Debugger" />` (`ZXMAK2/src/ZXMAK2/machines.config:12`). Switching to the Adlers debugger is documented as editing that line to `ZXMAK2.Hardware.Adlers.Debugger` (`ZXMAK2/src/Help/DebuggerWindowParts.html`).
- Because each debugger is just a device, several can coexist (e.g. default UI + GDB stub) and each gets the same `IDebuggable` plus the `IBusManager` event API (Adlers and GDB receive `bmgr`: `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Debugger.cs:53-61`, `ZXMAK2/src/ZXMAK2.Hardware.GdbServer/GDBJtagDevice.cs:69-80`).
- UI binding is indirect: the device creates `ViewHolder<IDebuggerGeneralView>` / `<IDebuggerExView>` / `<IDebuggerAdlersView>` / `<IDebuggerSprinterView>` and the host resolves the concrete WinForms form by interface (`ZXMAK2/src/ZXMAK2.Host.Presentation/Interfaces/IAboutView.cs:25-40`). This is the seam Kozynax later reused for its non-WinForms front-ends.
- Debug memory access goes through `IMemoryDevice.RDMEM_DBG/WRMEM_DBG`, which index the currently mapped read/write page tables (`ZXMAK2/src/ZXMAK2.Hardware/MemoryBase.cs:101-126`) — no contention, no bus events, but also **logical (current mapping) only**.

## 3. CPU, registers, disassembly and memory views

- **Default debugger** (`FormCpu`): three owner-drawn/list panels — disassembly (`DasmPanel`), hex dump (`DataPanel`), and three list boxes (registers, flags, CPU state) — plus toolbar/menu (Continue, Break, Step Into, Step Over, Step Out, Show Next Statement, Breakpoints) and a status strip (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:46-63`, `:125-149`). The status strip is recolored `#cc6600` while running and `#0077cc` when stopped; the disassembly text is grayed while running (`:129-145`). UI refresh is skipped while the form is hidden (`:104-109`).
- **Disassembly line** = `mnemonic` padded to 24 + `; <T>T` from `TimingTool` (`:221-227`). `TimingTool` is constructed with the CPU instance so timings follow the selected Z80 type (`ZXMAK2/src/ZXMAK2.Engine.Cpu/Tools/TimingTool.cs:32-44`); they are uncontended static timings, not measured.
- **DasmPanel** keyboard: Up/Down/PgUp/PgDn move, Enter fires `DasmClick`; clicking the gutter fires `BreakpointClick` (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/DebugPanels.cs:96-101`, `:333-360`). **DataPanel** uses arrow keys for byte cursor, Enter/click to POKE (`:928-970`), and renders the character column through a ZX character-set table (`:612-683`).
- **Adlers** (`ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs`) keeps the same panel concept but moves IFF/HALT/BINT/IM/FX/XFX under the flags (`:266-273`), frees the third list for **stack (10 words) or breakpoint list** toggled by F12 (`:144-228`, `:2115-2118`), and puts T-states in the window title (`:286-288`). Its DasmPanel adds syntax coloring (`CustomControls/DebugPanels.cs:276-383`), comment lines (address drawn centered with a yellow underline, `:236-246`), inline notes (`:371-377`) and back/forward history on mouse buttons 4/5 (`:399-427`, `:706-715`).
- **DebuggerEx** is a docking MVVM rewrite (`FormDebuggerEx` + `FormDisassembly`/`FormMemory`/`FormRegisters`/`FormBreakpoints` + view models). Functionally a subset of the default debugger: same commands, Step Out permanently disabled (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/ViewModels/DebuggerViewModel.cs:35-38`), breakpoint list synchronized from `GetBreakpointList()` when stopped (`BreakpointsViewModel.cs:41-66`).
- **Memory map window** shows, every timer tick, CMR0/CMR1, the page name (`ROM #nn (name)` / `RAM #nn`) mapped at `#0000/#4000/#8000/#C000`, DOSEN/SYSEN and a PropertyGrid over the memory device; CMR0/CMR1 are editable by double-click (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/FormMemoryMap.cs:20-123`). This is the only bank-aware view; the debugger itself addresses 64 KiB logical memory.

## 4. Breakpoints

### 4.1 Engine model and cost

- A breakpoint is `{ ushort? Address; string Label; Predicate<IMachineState> Check }` (`ZXMAK2/src/ZXMAK2.Engine/Entities/Breakpoint.cs:7-29`). The address form builds the predicate `state.CPU.regs.PC == addr`.
- `Spectrum` keeps a `List<Breakpoint>` (`ZXMAK2/src/ZXMAK2.Engine/Spectrum.cs:24`). The frame loop is marked *"performance critical block, do not modify!"*: after each `ExecCycle()` it `continue`s when the list is empty **or the CPU is HALTED**, otherwise evaluates `_breakpoints.Any(bp => bp.Check(this))` (`:103-123`, `:326-330`). One `ExecCycle` is one opcode or prefix (step-into loops until `FX`/`XFX` are None, `:270-276`), so checks run per M1 group, after the instruction — i.e. an exec breakpoint stops *before* the instruction at the address executes.
- Cost model: zero overhead with no breakpoints; with any breakpoint every predicate is a delegate call per instruction (LINQ `Any` over the list). No address bitmap. Breakpoints are keyed by **logical PC only** — no bank/page qualification anywhere.
- Known engine bug noted by the author: "Set BP on ADDR, load snapshot with state PC=ADDR, make sure BP didn't trigger" (`ZXMAK2/src/debug.txt:9-12`) — the first instruction after load/run is executed before any check.

### 4.2 Default / DebuggerEx / Sprinter

- Only execution breakpoints: toggle in the disassembly gutter (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:238-264`; `ViewModels/DisassemblyViewModel.cs:51-66`), "Clear breakpoints" in the context menu (`FormCPU.cs:337-341`). No conditions, counts or actions. The toolbar "Breakpoints" button is always disabled (`:142`).

### 4.3 Adlers conditional breakpoints

Grammar (tokenized by regex split on `\s+ , == != < >`, after normalizing `( x )` and `&&` spacing — `ZXMAK2/src/ZXMAK2.Hardware.Adlers/DebuggerManager.cs:141-177`):

```
br <cond> [&& <cond>]            ; exactly 8 tokens when multiconditional
br memread  <addr> [<addrTo>]
br memwrite <addr> [<addrTo>]
<cond>    := <lhs> (== | != | < | >) <rhs>
<lhs>     := <reg16> | <reg8> | <flag> | (<number>) | (<reg16>)
<rhs>     := <number>                      ; registers/memory on rhs parse but see caveats
<reg16>   := AF BC DE HL IX IY SP IR PC     ; AF' etc. listed but not compilable (see §9)
<reg8>    := A B C D E F H L
<flag>    := fZ fC fPV fH fS fN            ; case-sensitive 'f' prefix; F3/F5 excluded
<number>  := hex (default) | #hex | xhex | 0xhex | %bin | $dec
```

(`DebuggerManager.cs:113-114`, `:330-338`; `FormCPU.cs:1705-1808`; `Core/ConvertRadix.cs:60-97`.) Documented examples include `br pc == 9C40 && DE==#FFFF`, `br (pc) == #F3`, `br memwrite 9C40 EA60` (`ZXMAK2/src/Help/Commands.htm`).

Classification (`BreakPointConditionType`, `DebuggerManager.cs:14-27`): `registryVsValue`, `flagVsValue`, `memoryVsValue` (`(#addr)`), `registryMemoryReferenceVsValue` (`(reg)`), `memoryRead[InRange]`, `memoryWrite[InRange]`.

**JIT compilation.** Each condition is compiled with `System.Reflection.Emit.DynamicMethod` into a `Func<bool>` bound to the live `CpuRegs` object: `ldfld <RegName>; ldc.i4 <value>; ceq|cgt|clt` (`ZXMAK2/src/ZXMAK2.Hardware.Adlers/Core/ILProcessor.cs:15-40`, `:189-211`). Flags compile to `AF & mask` compared against 0 (`== 1` is rewritten to `!= 0`, `:41-98`). Memory operands call a wrapper around `IDebuggable.ReadMemory`, reading **one byte if the compare value is <= #FF, else a little-endian word** (`:99-184`, `:235-260`). The second clause is compiled separately and only evaluated if the first is true (`BreakpointAdlers.cs:59-65`).

**Deferred memory checks.** The debugger subscribes to *all* memory reads and writes (`FormCPU.cs:53-54`). On a write, every enabled `memoryVsValue`/`(reg)` breakpoint gets `IsNeedWriteMemoryCheck = true`; `memwrite A` sets `IsForceStop` if the address matches; ranges set the check flag (`:1952-1974`). The per-instruction predicate then (a) returns true on `IsForceStop`, (b) evaluates memory-value conditions only when a write happened since the last instruction (`BreakpointAdlers.cs:26-49`). Reads only affect `memread` breakpoints (`FormCPU.cs:1975-1999`). Opcode fetches use the separate M1 table (`ZXMAK2/src/ZXMAK2.Engine/EventManager.cs:201-226`), so `memread` does not fire on instruction fetch *(operand fetches presumably do — inferred)*. The stop lands after the accessing instruction completes.

**Management.** Stored in `ConcurrentDictionary<byte, BreakpointAdlers>`, lowest free index, max 255 (`FormCPU.cs:1905-1950`); re-entering an identical condition just re-enables it (`:1909-1925`). `on/off/del N`, `del all`, double-click in the list toggles; list shows `N:(off) mem write #XXXX-#YYYY` or the raw condition (`:189-226`). "Insert breakpoint here" in the dasm context menu issues `br pc == #addr` (`:1453-1460`). Breakpoint files: plain text, one condition per line without `br`, `;` comment lines, path relative to the app folder (`:2014-2063`).

### 4.4 GDB breakpoints

- `Z0/Z1` (and `z`) map to engine exec breakpoints; `Z2` write, `Z3` read, `Z4` access map to bus-hook lists in `GDBJtagDevice` (`ZXMAK2/src/ZXMAK2.Hardware.GdbServer/Breakpoint.cs:29-45`, `Gdb/GDBSession.cs:342-367`, `GDBJtagDevice.cs:105-131`). See §9 for defects.

## 5. Execution control

- F7 step into, F8 step over, F9 (default/Sprinter) or F10 (Adlers) run, F5 break, F3 reset (only when stopped) (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/General/FormCPU.cs:266-313`; `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:2068-2114`; `ZXMAK2/src/Help/Debugger.htm`). Escape hides the Adlers window (`:2131-2143`).
- **Step over** is textual: it disassembles the current instruction and single-steps if the mnemonic contains `J` or `RET` (so `DJNZ`, `JP`, `JR`, `RET*` step in; `CALL`, `RST`, `LDIR`, `HALT` step over) (`ZXMAK2/src/ZXMAK2.Engine/Spectrum.cs:278-293`). It runs until `PC == next address`, stops on a breakpoint, and after `71680*50*5` T-states asks "tacts executed, but operation not complete! Are you sure to continue?" doubling the limit each time (`:80`, `:296-323`, `:234-249`).
- **Step out**: button exists everywhere, never implemented (`FormCPU.cs:645-648`).
- **Frame-tact edit**: the status bar `T:` field (default) is editable; entering a value moves `CPU.Tact` forward so the ULA frame position becomes that value (`FormCPU.cs:586-599`). A crude "run to raster position" without executing code. The Adlers code has the same feature commented out (`ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:863-872`).
- `DebugNmi()` requests an NMI (`Spectrum.cs:144-148`); not exposed in the default debugger UI *(inferred: no caller in FormCpu)*.

## 6. Tracing, logging and profiling

- **Adlers trace hook** subscribes to `PreCycle` permanently (`ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/FormCPU.cs:55`) and returns early unless tracing (`:947-952`). Order of filters: address-area bitmap (`bool[65536]`), jump/call opcode set, single opcode, "detect jump to address X" (remembers whether the previous instruction was a jump/call and counts the source when PC lands on X), else trace everything (`:954-1043`). With "tact count" checked it sums static `TimingTool` timings (`:1045-1050`).
- **Unfiltered trace** writes `Logger.Debug("#{0:X4}   {1}", PC, mnemonic)` for every instruction — i.e. to log4net's `C:\Logs\ZXMAK2.log` or console appender, not to a dedicated file; the dialog warns "emulation will be very slow" before starting (`Core/DebuggerTrace.cs:207-225`; `ZXMAK2/src/ZXMAK2/log4net.config:1-58`). The `trace on|off` command merely toggles the same flag (`FormCPU.cs:1159-1174`). The help text says "ROM is not traced" but the check is commented out (`:951`).
- **Filtered trace = profile.** Filters increment `int[65536]` counters; on stop the file (name from the dialog, in the app folder) receives, sorted by address (`Core/DebuggerTrace.cs:120-165`):
  ```
  Addr: #8012   Trace occurences: 3120
  ...
  ===========================================================
  Total addresses: 14   Total occurences: 40211   Tact count: 512334
  Trace filter:
  - conditional jumps
  - trace area in: #8000->#8FFF;
  ```
  Opcode sets: conditional `CALL cc` (C4..FC), conditional `JR cc`/`JP cc`, common `JR JP CALL RET JP(HL)` (`:16-49`). Address ranges are persisted as `FROM;TO;Yes|No` tags (`:264-288`).
- **Device I/O logs**: `logIo="true"` on WD1793 controllers logs `WD93 <reg> <== #vv [PC=#pppp, T=t]` / `==>` for reads (`ZXMAK2/src/ZXMAK2.Hardware/General/FddController.cs:238-303`); IDE and Sprinter devices have the same switch (grep `LogIo`: `Profi/IdeProfi.cs:108-230`, `Atm/IdeAtm.cs`, `Evo/IdePentEvo.cs`, `General/IdeSmuc.cs`, `Sprinter/*`). Commented-out per-M1 logging exists in the bus (`ZXMAK2/src/ZXMAK2.Engine/EventManager.cs:208-210`).
- No code/data coverage map, no heat map, no call stack reconstruction.

## 7. Video, raster, beam and device views

- The only beam-related readouts are frame T-state (`T: n / N` status, title in Adlers) and the OSD "Debug Info" layer: `Render FPS`, `Update FPS`, `Device FPS`, `Back: [w,h]`, `Frame: [w,h]`, `Sound: kHz`, `FrameStart: nT`, plus min/avg/max frame time and latency graphs (`ZXMAK2/src/ZXMAK2.Host.WinForms/Mdx/Renderers/OsdRenderer.cs:171-226`, toggled by View → Debug Info, `ZXMAK2/src/ZXMAK2.Host.Presentation/MainViewModel.cs:633-641`). No beam crosshair, partial-frame display or per-T-state event list.
- **Graphics editor (Adlers `ge`)** views memory from an address in four modes — ZX screen, sprite (width/height combos, mirror), tile, and a "ZX Jetpac" sprite layout — with zoom, pixel shift left/right, selection area and export as PNG/BMP/JPG or bytes (`ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/GraphicsEditor/GraphicsEditor.cs:72-242`, `:519-660`). A ripper/viewer, not a live VRAM view.
- **Contention** is not visualized; it is verified offline by `src/Test` "Sanity ULA" runs that step single opcodes from a given frame tact and compare the resulting frame tacts with hard-coded patterns for 48K early/late and 128K ULAs (`ZXMAK2/src/Test/Program.cs:91-166`, patterns `:193-360`).
- Device windows: WD1793 state dump (`ZXMAK2/src/ZXMAK2.Hardware.WinForms/dbgWD1793.cs:20-25`), memory map (§3), Sprinter MMU ports (`Sprinter/DebugForm.cs:636-645`). No AY/sound register view in the debugger.

## 8. Remote debugging: GDB stub

- Device `GDB-Z80 SERVER` (originally "z80gdbserver", © 2011 Alexander Tsidaev, GPLv3), port 2000 by default, `port`/`log` persisted in the machine config (`ZXMAK2/src/ZXMAK2.Hardware.GdbServer/GDBJtagDevice.cs:1-146`). Listens on `IPAddress.Any` in a background thread, one thread per client; a new client stops the emulator (`Gdb/GDBNetworkServer.cs:40-50`, `:95-107`). With `log=true` every packet is logged `--> …` / `<-- …` (`:149-152`, `:168-177`).
- Packets: `q/Q` (`Supported` → `PacketSize=4000`, `Attached` → 1), `?` → `T05thread:00;`, `g/G` registers, `p/P` single register, `m/M` memory (hex), `X` ignored, `s` step, `c` continue, `D` detach (runs), `H` OK, `k` ignored, `v` (`vCont?` → empty), `z/Z`, Ctrl-C (0x03) → stop + `T02` (`Gdb/GDBSession.cs:139-219`, `:226-340`).
- Register order (gdb-z80): `a f bc de hl ix iy sp i r a' f' bc' de' hl' pc`, bytes/words little-endian hex (`:50-101`).
- On any stop the server **clears all breakpoints** (comment: "GDB will set them again") and broadcasts `T05` (`Gdb/GDBNetworkServer.cs:52-61`).

## 9. Notable and unique ideas

1. **Debugger as a hot-pluggable machine device** (`IJtagDevice`): several debuggers, including a network stub, can be attached per machine config without touching the core. Clean seam that let Kozynax add new front-ends later.
2. **JIT-compiled breakpoint conditions** (Adlers): conditions become `DynamicMethod` IL bound to the register object, so evaluation is a direct field load + compare, no interpreter.
3. **Write-triggered evaluation of memory-value breakpoints**: `(addr) == value` conditions are only re-evaluated after a bus write, not every instruction — cheap memory-state breakpoints.
4. **Per-address 64K delegate tables for bus events** (read/M1/write/IO/no-MREQ) — watchpoint hooks cost nothing for addresses nobody subscribed to.
5. **Filtered trace as a profiler**: counting hits of conditional jumps/calls or a single opcode per address plus a T-state sum, with a "who jumps to X" detector — a lightweight branch/opcode heat list useful for finding game loops and loaders.
6. **Frame-tact editor** in the status bar: set the ULA frame position directly while stopped.
7. **Integrated assembler + comments/notes + symbol → comment** round trip (Adlers): compile Pasmo source into RAM and annotate the disassembly from the symbol table.
8. **Offline ULA contention regression patterns** (`src/Test`): per-opcode frame-tact sequences for 48K early/late and 128K ULAs.
9. **Timing column** in every disassembly line (static T-states for the current CPU type).

## 10. Gaps and caveats

- **Windows only** (WinForms, Managed DirectX, `kernel32` `LoadLibrary` for Pasmo2.dll: `ZXMAK2/src/ZXMAK2.Hardware.Adlers/Views/Assembler/Compiler.cs:210-220`).
- **No bank-aware addressing**: breakpoints, dumps, disassembly and GDB memory are 64 KiB logical; only the memory-map window shows paging.
- Breakpoints are **not checked while the CPU is HALTED** (`ZXMAK2/src/ZXMAK2.Engine/Spectrum.cs:107`) and not before the first instruction after run/load (`ZXMAK2/src/debug.txt:9-12`).
- Default debugger: exec breakpoints only; Step Out never implemented; no watches, no call stack (stack view is Adlers only).
- Adlers expression language is minimal: 4 operators, at most two ANDed clauses, no arithmetic, no OR. Shadow registers are accepted by the tokenizer but `ILProcessor` does `GetField("AF'")` which does not exist (fields are `_AF` etc.), so such conditions fail *(inferred from `ILProcessor.cs:29` vs `ZXMAK2/src/ZXMAK2.Engine.Cpu/CpuRegs.cs:42-48`)*. Register-vs-register (`br a == b`) leaves `AccessType` at its default `memoryVsValue` and silently compares memory at address 0 *(inferred, `FormCPU.cs:1771-1807`)*. Memory compare width is inferred from the constant (`#0005` compares one byte). `getRegistryArrayIndex` maps e.g. `A` and `BC` to the same index (`DebuggerManager.cs:438-471`, unused by the compiler).
- `memwrite A..B` range breakpoints go through the memory-value path whose default branch compares a zero `leftValue` against the range end, so they stop only because `0 != RightValue` *(inferred, `BreakpointAdlers.cs:87-110`)*.
- `$` means **decimal** in Adlers numbers — the opposite of common Z80 convention.
- Unfiltered trace goes to the general log (hard-coded `C:\Logs\ZXMAK2.log`), not the named trace file; "ROM is not traced" is false (check commented out, `FormCPU.cs:951`). The Adlers PreCycle/read/write hooks stay subscribed for the lifetime of the form even when idle (`:53-55`).
- Breakpoint persistence in `debugger_config.xml` is a literal `"ToDo"` (`FormCPU.cs:2285`); comments/notes need manual XML save.
- **GDB stub defects**: removing an exec breakpoint constructs a *new* `Breakpoint(addr)` and calls `List.Remove`, which uses reference equality, so the breakpoint is never removed (`Gdb/GDBSession.cs:362-363`, `Breakpoint.cs` has no `Equals`); read/write watch hits send `T05` but never stop the emulator (`IsRunning=false` commented out, `Gdb/GDBNetworkServer.cs:52-61`); `g` returns only 15 of 16 registers (`Enumerable.Range(0, RegistersCount - 1)`, omits PC, `GDBSession.cs:247-252`); `s` calls `CPU.ExecCycle()` directly (one prefix, no breakpoint/frame bookkeeping, `:184-187`); `m/M` use `CPU.RDMEM/WRMEM` (bus accesses with side effects and hooks, `:299`, `:325`); listens on all interfaces although the description says localhost (`GDBNetworkServer.cs:45`, `GDBJtagDevice.cs:59`).
- DebuggerEx duplicates the default debugger with fewer features (no block I/O, no frame-tact edit) *(inferred from its view models)*.
