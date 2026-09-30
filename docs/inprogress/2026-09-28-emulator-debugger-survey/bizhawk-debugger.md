# BizHawk debugger — capability survey

**Source:** https://github.com/TASEmulators/BizHawk · local checkout commit `23fd0ba0ca` (2026-01-10, `2.11-308`) · C# / .NET, WinForms client (EmuHawk)
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** multi-system frontend whose debugging tools (Debugger, Trace Logger, Hex Editor, RAM Watch/Search, Cheats, CDL, Lua console + API, TAStudio, rewind) are generic and sit on optional core *services* (`IDebuggable`, `IMemoryDomains`, `ITraceable`, `IDisassemblable`, `ICodeDataLogger`, `IStatable`). No GDB stub; remote control goes through Lua `comm.*` (TCP/UDP socket, HTTP, WebSocket, memory-mapped file) and C# external tools. The ZX Spectrum core (ZXHawk) implements only part of the services, and memory callbacks never fire in it.

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| Architecture | Core service model | Tools declare `[RequiredService]`/`[OptionalService]` and degrade when a core lacks a service or throws `NotImplementedException` | `BizHawk/src/BizHawk.Emulation.Common/Interfaces/Services/IDebuggable.cs:4-44`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.IToolForm.cs:8-99` |
| CPU & registers | Register box | Every entry of `GetCpuFlagsAndRegisters()` (name -> value + bit size); 1-bit entries as flag checkboxes; editable when `SetCpuRegister` works | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/RegisterBoxControl.cs:29-115` |
| CPU & registers | ZXHawk registers | A B C D E F H L, AF BC DE HL, I R IX IY SP PC, `Shadow AF/BC/DE/HL`, flags C N P/V 3rd H 5th Z S (no IFF/IM) | `BizHawk/src/BizHawk.Emulation.Cores/CPUs/Z80A/Z80A.cs:126-175` |
| Disassembly | Debugger disassembly view | Address + mnemonic columns, current PC row light cyan; disassembles forward from PC over the System Bus domain; CPU picker when a core has several CPUs | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.Disassembler.cs:38-139`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.cs:41-80` |
| Disassembly | ZXHawk disassembler | Z80 table disassembler (CB/DD/ED/FD/DDCB/FDCB), `PCRegisterName = "PC"`, CPU list `["Z80"]` | `BizHawk/src/BizHawk.Emulation.Cores/CPUs/Z80A/NewDisassembler.cs:383-448` |
| Memory views | Memory domains | Named byte-addressable regions with size, endianness, word size, writable flag, Peek/Poke/Bulk peek; `System Bus` + `MainMemory` conventions | `BizHawk/src/BizHawk.Emulation.Common/Base Implementations/MemoryDomain.cs:14-155`, `BizHawk/src/BizHawk.Emulation.Common/Base Implementations/MemoryDomainList.cs:30-47` |
| Memory views | ZXHawk domains | `System Bus` (64K, paged view) + one domain per ROM and per RAM bank (`RAM - BANK 5 (Screen)`, `RAM - BANK 7 (Shadow Screen)`, `ROM - +3DOS`...) per model | `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.IMemoryDomains.cs:17-87` |
| Memory views | Hex Editor | Any domain; 1/2/4-byte grouping, big-endian toggle, find next/prev (hex or text), go to address, poke, freeze (color-coded), add to RAM Watch, table (.tbl) files, copy/paste, import/export binary, save ROM domain back to file, increment/decrement | `BizHawk/src/BizHawk.Client.EmuHawk/tools/HexEditor/HexEditor.cs:128-155`, `:312-416`, `:1247-1927` |
| Watchpoints & watches | RAM Watch | Per-watch domain, size (byte/word/dword), display type (signed, unsigned, hex, binary, fixed 12.4 / 20.12 / 16.16, float), previous value (last frame / last change / original), change counter, notes, separators; on-screen display; context "Read/Write breakpoint" to the Debugger; `.wch` files | `BizHawk/src/BizHawk.Client.Common/tools/Watch/WatchDisplayType.cs:11-60`, `BizHawk/src/BizHawk.Client.Common/tools/Watch/PreviousType.cs:5-8`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/Watch/RamWatch.cs:717-1201` |
| Watchpoints & watches | RAM Search | Iterative narrowing: compare to previous / specific value / specific address / change count / difference, operators `= > >= < <= != DifferentBy`; Fast vs Detailed mode; undo/redo, preview, auto-search (lag-aware), misaligned search, exclude watched | `BizHawk/src/BizHawk.Client.Common/tools/RamSearchEngine/Enums.cs:3-26`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/Watch/RamSearch.cs:1123-1323` |
| Breakpoints | Read / Write / Execute | Address + mask on the System Bus scope; type Read/Write/Execute; toggle, duplicate, edit, remove; hit pauses the client and posts "Breakpoint hit" | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/BreakpointControl.cs:56-274`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/AddBreakpointDialog.cs` |
| Breakpoints | Read-only foreign callbacks | Callbacks registered by Lua/other tools show in the list as read-only | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/BreakpointControl.cs:92-121`, `BizHawk/src/BizHawk.Client.Common/BreakpointList.cs:122-134` |
| Conditions & expressions | Address mask | Only "condition": `(addr & mask) == address`; no value/register conditions in the UI (Lua callbacks fill the gap) | `BizHawk/src/BizHawk.Emulation.Common/Base Implementations/MemoryCallbackSystem.cs:69-80` |
| Conditions & expressions | Value override | A callback may return a value that replaces the value read/written | `BizHawk/src/BizHawk.Emulation.Common/Interfaces/IMemoryCallbackSystem.cs:5-11`, `BizHawk/src/BizHawk.Emulation.Common/Base Implementations/MemoryCallbackSystem.cs:76` |
| Execution control | Run / Step Into / Over / Out | Buttons enabled per `CanStep(StepType)` | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.cs:127-202` |
| Execution control | Seek to PC | Temporary read-only execute breakpoint named `Seek to PC 0x...`, removed on hit; Cancel Seek | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/BreakpointControl.cs:64-76`, `:160-176`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.cs:260-275` |
| Execution control | Frame advance / pause / seek to frame | Hotkeys F / Pause; `PauseOnFrame` seek (optionally turbo) | `BizHawk/src/BizHawk.Client.Common/config/Binding.cs:29-31`, `BizHawk/src/BizHawk.Client.EmuHawk/MainForm.cs:1129-1148`, `:3059-3115` |
| Tracing & logging | Trace Logger | Core pushes `TraceInfo(disassembly, registerInfo)` to a sink; to window (ring of MaxLines, default 10 000) or to file with segmenting (default 150 MB per file); header per core | `BizHawk/src/BizHawk.Emulation.Common/Interfaces/Services/ITraceable.cs:3-36`, `BizHawk/src/BizHawk.Client.EmuHawk/tools/TraceLogger.cs:30-93`, `:128-191`, `:379-404` |
| Tracing & logging | ZXHawk trace | One line per opcode fetch + `====NMI====` / `====IRQ====` markers; registers, total cycles, flags | `BizHawk/src/BizHawk.Emulation.Cores/CPUs/Z80A/Z80A.cs:266-269`, `:740`, `:761`, `:814-852`, `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.IEmulator.cs:30-37` |
| Tracing & logging | ZXHawk OSD device messages | Tape status (playing/stopped, image, block n of m + description, block %, tape %), block change/auto start-stop, disk status (image, detected protection, drive light); verbosity setting | `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.Messaging.cs:90-425`, `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.ISettable.cs:60-61` |
| Tracing & logging | ZXHawk FDC debug log | uPD765 CSV log (STATUS, WRITE, READ, CODE, MT, MF, SK, counters) behind a hard-coded `writeDebug = false` | `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/Hardware/Disk/NECUPD765.IPortIODevice.cs:19-66` |
| History / rewind | Rewinder | Ring buffer of savestates (Zwinder / ZeldaWinder), default 512 MB, optional delta/compression, fixed or adaptive interval, memory or disk backing; Shift+R | `BizHawk/src/BizHawk.Client.Common/config/RewindConfig.cs:8-63`, `BizHawk/src/BizHawk.Client.Common/rewind/Zwinder.cs:9`, `BizHawk/src/BizHawk.Client.EmuHawk/MainForm.cs:1283-1296`, `:2178` |
| History / rewind | Movies (bk2) | Input movie record/playback, rerecord counter, lag frames | `BizHawk/src/BizHawk.Client.Common/movie/`, `BizHawk/src/BizHawk.Client.EmuHawk/MainForm.cs:3073-3074` |
| History / rewind | TAStudio | Piano roll input editor with greenzone (tiered state cache), branches (state + input log + screenshots + markers), markers, lag log, undo history, seek to marker, Lua hooks | `BizHawk/src/BizHawk.Client.Common/movie/tasproj/ZwinderStateManager.cs:17-223`, `BizHawk/src/BizHawk.Client.Common/movie/tasproj/TasBranch.cs:12-43`, `BizHawk/src/BizHawk.Client.Common/config/Binding.cs:136-147` |
| Video, raster & beam | — (generic) | Nothing generic; system-specific viewers exist only for NES/SNES/GB/GBA/Genesis/PCE/SMS/TI-83; none for ZX | `BizHawk/src/BizHawk.Client.EmuHawk/tools/` (subfolders) |
| Profiling & coverage | Code/Data Logger | Per-domain byte-flag arrays, 8 flag columns with % coverage per domain; new/open/save/append (OR-merge); auto-save/start/resume; `BIZHAWK-CDL-2` file | `BizHawk/src/BizHawk.Client.EmuHawk/tools/CDL.cs:37-102`, `:165-188`, `BizHawk/src/BizHawk.Emulation.Common/Base Implementations/CodeDataLog.cs:73-185` |
| Profiling & coverage | ZXHawk CDL | Domains per ROM/RAM bank + System Bus, routed via `ReadCDL(addr)` to physical bank | `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.ICodeDataLog.cs:20-172` |
| Scripting | Lua console + API | Libraries `emu` (registers, disassemble, totalexecutedcycles, frameadvance, yield, lag), `memory`/`mainmemory` (domain reads/writes s8..u32 le/be, float, ranges, hash_region), `event` (on_bus_read/write/exec[_any], onframestart/end, oninputpoll, onsavestate/onloadstate, onexit), `gui`, `joypad`, `input`, `movie`, `savestate`, `memorysavestate`, `client`, `comm`, `SQL`, `userdata`, `bit`, `bizstring`, `forms`, `console`, `tastudio` | `BizHawk/src/BizHawk.Client.Common/lua/CommonLibs/EmulationLuaLibrary.cs:21-123`, `BizHawk/src/BizHawk.Client.Common/lua/CommonLibs/MemoryLuaLibrary.cs:18-363`, `BizHawk/src/BizHawk.Client.Common/lua/LuaHelperLibs/EventsLuaLibrary.cs:110-263` |
| Remote protocols | `comm` IPC | TCP/UDP socket client (`socketServerSend/Response/ScreenShot`), HTTP GET/POST (+ screenshot), WebSocket client, memory-mapped file read/write/copy-from/to-memory; configured by `--socket-ip/--socket-port/--socket-udp`, `--url-get/--url-post`, `--mmf` | `BizHawk/src/BizHawk.Client.Common/lua/CommonLibs/CommLuaLibrary.cs:21-296`, `BizHawk/src/BizHawk.Client.Common/ArgParser.cs:54-96` |
| Remote protocols | External tools (ApiHawk) | C# DLL tools loaded from the External Tools path, using the same API classes as Lua (Emulation, Memory, MemoryEvents, Gui, Joypad, Movie, SaveState, Comm...) | `BizHawk/src/BizHawk.Client.EmuHawk/tools/ExternalToolManager.cs:16-91`, `BizHawk/src/BizHawk.Client.Common/Api/Classes/` |
| Import / export | Cheats | Address/value with optional compare (`= > >= < <= !=`), re-applied every frame; `.cht` lists | `BizHawk/src/BizHawk.Client.Common/tools/Cheat.cs:7-211`, `BizHawk/src/BizHawk.Client.EmuHawk/MainForm.cs:2994`, `:3077` |
| Import / export | Persistence | `.wch` watch lists (also other emulators' format), `.cdl`, `.cht`, `.bk2`/`.tasproj`, trace text files; breakpoints are not saved | `BizHawk/src/BizHawk.Client.Common/tools/Watch/WatchList/WatchList.cs:384-411` |
| UI conveniences | Cross-tool links | RAM Search/Watch -> Hex Editor, Hex Editor -> RAM Watch, RAM Watch -> Debugger breakpoints; ToolBox Shift+T | `BizHawk/src/BizHawk.Client.EmuHawk/tools/Watch/RamWatch.cs:1171-1201`, `BizHawk/src/BizHawk.Client.Common/config/Binding.cs:119-127` |

## 2. Architecture: how cores expose debugging

- **`IDebuggable`**: `GetCpuFlagsAndRegisters()`, `SetCpuRegister(name, value)`, `MemoryCallbacks`, `CanStep(StepType)`, `Step(StepType)`, `TotalExecutedCycles` (`BizHawk/src/BizHawk.Emulation.Common/Interfaces/Services/IDebuggable.cs:17-44`). `StepType` is `Into`, `Out`, `Over` (`BizHawk/src/BizHawk.Emulation.Common/Enums.cs:17-21`). `RegisterValue` carries a value and a bit size 1..64, so the client can render registers of any CPU generically (`IDebuggable.cs:48-80`).
- **`IMemoryCallbackSystem`**: scopes (domain or CPU names), callbacks typed Read/Write/Execute with optional address + mask; `CallMemoryCallbacks(addr, value, flags, scope)`; `HasReads/HasWrites/HasExecutes` and per-scope variants (`BizHawk/src/BizHawk.Emulation.Common/Interfaces/IMemoryCallbackSystem.cs:11-120`). Flags encode size, access and CPU (`:109-120`).
- **`ITraceable`**: a `Header` and a settable `Sink`; the core pushes one `TraceInfo(Disassembly, RegisterInfo)` per instruction (`BizHawk/src/BizHawk.Emulation.Common/Interfaces/Services/ITraceable.cs:3-36`). `CallbackBasedTraceBuffer` implements tracing on top of an execute memory callback for cores that do not trace natively (`BizHawk/src/BizHawk.Emulation.Common/Base Implementations/CallbackBasedTraceBuffer.cs:12-86`).
- **`IDisassemblable`**: `Cpu`, `AvailableCpus`, `PCRegisterName`, `Disassemble(domain, addr, out length)` (`BizHawk/src/BizHawk.Emulation.Common/Interfaces/Services/IDisassemblable.cs:11-57`).
- **`ICodeDataLogger`**: `SetCDL`, `NewCDL`, `DisassembleCDL`; the log is a `Dictionary<domain name, byte[]>` of per-byte flags (`BizHawk/src/BizHawk.Emulation.Common/Interfaces/Services/ICodeDataLogger.cs:10-74`).
- **Capability probing**: the Debugger touches each feature inside `try/catch NotImplementedException` to decide what to enable (`BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.IToolForm.cs:36-99`).

## 3. Debugger window

- Layout: register box, disassembly, breakpoint list, Seek-to box, Run/Step buttons (`BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.cs:41-146`).
- Disassembly is rebuilt from the current PC each update: `RowCount = VisibleRows * 6 + 2` lines forward; scrolling up re-disassembles back with a heuristic that tries to land on an instruction ending at the current address, falling back to address-1 after 5 bytes (`BizHawk/src/BizHawk.Client.EmuHawk/tools/Debugger/GenericDebugger.Disassembler.cs:54-139`). No symbols, no labels, no gutter breakpoints, no bytes column.
- Refresh model: full update on each frame (`GeneralUpdate`) and on breakpoint hit (`UpdateForBreakpointHit`) (`GenericDebugger.IToolForm.cs:101-134`).

## 4. Breakpoints

- `Breakpoint` = scope, type, address, mask, name (`"Pause"` by default), active, read-only; activating registers a `MemoryCallback` with the core, deactivating removes it (`BizHawk/src/BizHawk.Client.Common/BreakpointList.cs:66-178`).
- Keying: always the `System Bus` scope of the core (`BreakpointControl.cs:139`, `:152`), i.e. CPU-visible logical addresses; banked/physical breakpoints are possible only through Lua with another scope, if the core offers one.
- Matching: linear scan over the callback list; `(addr & mask) == address` (`MemoryCallbackSystem.cs:69-80`). Cheapness relies on the core checking `HasReads/HasWrites/HasExecutes` (one bool each) before calling, and on `CallMemoryCallbacks` returning at once when `_hasAny` is false (`MemoryCallbackSystem.cs:82-87`); the interface comment asks cores to keep this "very very quickly" (`IMemoryCallbackSystem.cs:21-25`). The collection is copy-on-write so callbacks can add/remove callbacks during iteration (`MemoryCallbackSystem.cs:258-360`).
- Hit action: `MainForm.PauseEmulator()` + OSD "Breakpoint hit" (`BreakpointControl.cs:56-62`). `PauseEmulator` only sets a flag (`BizHawk/src/BizHawk.Client.EmuHawk/MainForm.cs:1341-1345`), and the client loop runs whole `FrameAdvance()` calls (`MainForm.cs:3061`), so a hit stops the *client* at the end of the current frame, not the CPU at the instruction *(inferred from the code path; cores that implement mid-frame stepping are the exception)*.
- No hit counts, no conditions beyond the mask, no enable-on-hit actions, no persistence of the breakpoint list.

## 5. Watches, RAM Search, Hex Editor, Cheats

- Watches have domain + address + size + display type + big-endian + notes; "previous" reference is Original / LastSearch / LastFrame / LastChange (`BizHawk/src/BizHawk.Client.Common/tools/Watch/PreviousType.cs:5-8`); change counts per watch; on-screen watches.
- RAM Search keeps a candidate list and filters it per step; `SearchMode.Fast` is auto-chosen when main memory exceeds 1 MB (`BizHawk/src/BizHawk.Client.Common/tools/RamSearchEngine/SearchEngineSettings.cs:11-13`); undo history of candidate lists (`BizHawk/src/BizHawk.Client.Common/tools/RamSearchEngine/RamSearchEngine.cs:19`, `:141`).
- Hex Editor freeze uses the cheat system; colors Freeze/Highlight/HighlightFreeze are configurable (`HexEditor.cs:141-143`).
- Cheats are re-poked every frame by `CheatList.Pulse()` (`MainForm.cs:2994`, `:3077`); compare-type cheats are either sent to cores that support them natively (`SendCheatToCore`) or evaluated client-side (`BizHawk/src/BizHawk.Client.Common/tools/Cheat.cs:188-211`).

## 6. Tracing

- Trace Logger sink either appends to an in-window list trimmed to `MaxLines` or streams to a file, starting with the core's `Header`, splitting into segments when `FileSizeCap` MB is reached (`TraceLogger.cs:128-191`, `:379-404`).
- **ZXHawk trace format**: header `Z80A: PC, machine code, mnemonic, operands, registers (AF, BC, DE, HL, IX, IY, SP, Cy), flags (CNP3H5ZS)`; each line is built at the opcode fetch (`Z80A.cs:266-269`, `:814-852`), e.g.
  `8000: 3E 01        LD A, 01h                  AF:0144 BC:0000 DE:0000 HL:5C00 IX:0000 IY:5C3A SP:FF4A Cy:123456 cNP-hZ-se` (illustrative values; lower case = flag clear, last letter is interrupt enable).
  Interrupt entry is marked by separate `====NMI====` / `====IRQ====` lines (`Z80A.cs:740`, `:761`). Alternate registers, I and R are not in the trace.
- ZXHawk attaches the trace callback per frame only when the sink is enabled (`ZXSpectrum.IEmulator.cs:30-37`), so tracing off costs one null check per fetch.

## 7. History, rewind and TAS tools

- Rewind stores full core savestates in a ring (`ZwinderBuffer`), sized in MB, with optional delta and compression and a fixed or adaptive capture interval (`RewindConfig.cs:53-63`). Enabled only for cores with `IStatable` (`MainForm.cs:1290`). ZXHawk registers `IStatable` (`ZXSpectrum.cs:151`).
- TAStudio greenzone keeps four tiers: `_current`, `_recent`, `_gapFiller` ring buffers and `_reserved` "ancient" states every `AncientStateInterval` (default 5000) frames (`ZwinderStateManager.cs:17-55`, `ZwinderStateManagerSettings.cs:41-113`). Branches store frame, core state, input log, core + OSD frame buffers, change log, markers, user text (`TasBranch.cs:12-43`). Lua can hook greenzone invalidation, branch load, and row painting (`BizHawk/src/BizHawk.Client.EmuHawk/tools/Lua/Libraries/TAStudioLuaLibrary.cs`).
- Time travel is frame-granular (state + deterministic re-emulation); there is no per-instruction reverse step.

## 8. Code/Data Logger

- Generic tool shows, for every CDL domain, total size, % touched, and counts per flag bit 0x01..0x80 (`CDL.cs:92-102`, `:165-188`). Append ORs another log in (`CodeDataLog.cs:103`). File header `BIZHAWK-CDL-2` + subtype (`CodeDataLog.cs:139-185`).
- ZXHawk: `NewCDL` creates one array per ROM/RAM bank plus `System Bus` (`ZXSpectrum.ICodeDataLog.cs:20-56`); `ReadMemory_CDL` maps the logical address to the physical bank via `_machine.ReadCDL(addr)` (`:76-172`). It is hooked only on `CpuLink.ReadMemory` (data reads), not on `FetchMemory` (opcode fetches) (`BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.CpuLink.cs:9-15`). `DisassembleCDL` is not implemented (`ZXSpectrum.ICodeDataLog.cs:58-60`).

## 9. ZX Spectrum core (ZXHawk) — debugger-visible surface

- Models: 16K, 48K, 128K, +2, +2A, +3, Pentagon 128 (work in progress) (`BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/readme.md`).
- Services: `IDebuggable` (registers only), `ITraceable`, `IDisassemblable`, `ICodeDataLogger`, `IMemoryDomains`, `IStatable`, `IInputPollable` (`ZXSpectrum.cs:119-151`, `ZXSpectrum.IDebuggable.cs:10-24`).
- `CanStep` returns false for every step type and `Step` throws (`ZXSpectrum.IDebuggable.cs:18-21`): no stepping.
- `MemoryCallbacks` is a `MemoryCallbackSystem` with scope `System Bus` (`ZXSpectrum.IDebuggable.cs:16`), but neither ZXHawk nor the shared Z80A core ever calls `CallMemoryCallbacks` (other Z80A-based cores such as SMS call it from their own bus handlers). Result: the Debugger enables its breakpoint panel (probing `HasReads` succeeds, `GenericDebugger.IToolForm.cs:38-46`), yet read/write/execute breakpoints, Seek to PC, and Lua `event.on_bus_*` never fire on ZXHawk *(inferred from the absence of any call site)*.
- Memory domains expose each ROM and RAM bank separately, named by role (`RAM - BANK 5 (Screen)`, `RAM - BANK 7 (Shadow Screen)`), so the hex editor and RAM watch can inspect unpaged banks (`ZXSpectrum.IMemoryDomains.cs:42-87`).
- ULA, contention and floating bus are modeled (`BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/Machine/CPUMonitor.cs:56-83` runs the ULA clock before each CPU cycle and adds contention T-states), but none of it is exposed: no beam position, no contention counters, no ULA/port view. Only `TotalExecutedCycles` (includes contention) reaches the Debugger and the trace `Cy:` column.
- Tape/disk debugging = OSD messages and controller buttons: `Play/Stop/RTZ/Record Tape`, `Insert Next/Previous Tape`, `Next/Prev Tape Block`, `Get Tape Status`, `Insert Next/Previous Disk`, `Get Disk Status` are *controller inputs*, hence movie-recordable (`BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/ZXSpectrum.Controllers.cs:100-120`). Status output shown in section 1 (`ZXSpectrum.Messaging.cs:90-425`).
- Lag frames = frames in which the keyboard/joystick was not read (`BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/Machine/SpectrumBase.cs:183`).

## 10. Scripting and remote control

- Lua memory events take an optional `scope` (memory domain / CPU); unsupported cores log "not implemented" instead of failing (`EventsLuaLibrary.cs:126-160`). `on_bus_exec_any` fires before every instruction (`EventsLuaLibrary.cs:174`). Callbacks receive `(addr, val, flags)` and may return a replacement value.
- `emu.yield()` lets a script run across frames; `emu.frameadvance()` steps one frame (`EmulationLuaLibrary.cs:26`, `:111`).
- IPC is client-side only: BizHawk *connects out* to a socket/HTTP/WebSocket server, or shares a memory-mapped file; there is no listening debug server or GDB/DeZog protocol (`CommLuaLibrary.cs:32-296`, `ArgParser.cs:54-96`).

## 11. Notable and unique ideas

1. **Capability-based service interfaces** — one debugger UI for dozens of CPUs, each feature probed and greyed out individually; a model for keeping unreal-ng's automation surfaces (CLI/WebAPI/MCP/Lua) consistent.
2. **Memory domains as the universal addressing unit** — every tool (hex, watch, search, cheats, CDL, Lua) takes a domain name; ZXHawk's per-bank domains make banked memory inspectable without paging.
3. **Callbacks that can override the value** read/written — enables patch-on-read and injection experiments from scripts.
4. **RAM Search with previous-value modes, change counters and undo** — the reverse-engineering workflow ("find the lives counter") that pure debuggers lack.
5. **Tiered greenzone (current/recent/gap/ancient) + branches with screenshots** — memory-bounded time travel over long sessions.
6. **Tape and disk transport as movie-recordable inputs** with OSD status (block n of m, % through block/tape, detected disk protection).
7. **Trace to window or to size-segmented files**, with the core defining its own header and line layout.
8. **Code/Data Logger keyed by physical bank** with per-flag coverage percentages and OR-merge of sessions.
9. **Lua + ApiHawk share one API layer**, and `comm.*` offers socket/HTTP/WebSocket/MMF IPC for external controllers.

## 12. Gaps and caveats

- **ZXHawk breakpoints are dead**: memory callbacks are never invoked, but the UI does not grey them out (see section 9).
- ZXHawk cannot step (`CanStep` false); debugging is frame advance + registers + disassembly + trace.
- **ZXHawk CDL stores the data byte, not flags**: `_cdl[domain][address] = data` (`ZXSpectrum.ICodeDataLog.cs:95-169`), so the CDL tool's per-flag columns show value bit statistics rather than code/data coverage; opcode fetches are not logged at all.
- Z80A trace disassembly and operand bytes are read through `_link.ReadMemory` (`Z80A.cs:820-825`), which with CDL active is `ReadMemory_CDL`, so tracing marks bytes as read in the CDL *(inferred)*.
- `DD ED` / `FD ED` prefixes disassemble via `mnemonicsED[A]` with `A` still 0xED instead of the following byte (`NewDisassembler.cs:400`, `:413`) *(inferred from reading)*.
- Generic breakpoints stop at frame end, not at the faulting instruction *(inferred)*; no conditions, hit counts, or persistence; System Bus scope only in the UI.
- Callback matching skips the scope check when `Address` is null (`MemoryCallbackSystem.cs:74`), so address-less callbacks fire for every scope.
- Z80 register set omits IFF1/IFF2 and IM (`Z80A.cs:126-160`); no symbol/label support anywhere in the debugger.
- FDC debug log is hard-disabled and its file path is a developer's local Windows path (`NECUPD765.IPortIODevice.cs:19-21`).
- No ZX-specific viewers (screen/attribute, ULA beam, contention, AY registers, tape pulse view).
