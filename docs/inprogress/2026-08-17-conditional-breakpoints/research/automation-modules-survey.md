# Automation Modules — Infrastructure Survey (input for DeZog module design)

Date: 2026-08-17. All paths relative to repo root. Companion to `dezog-integration.md`.

## 1. Automation module structure & build wiring

**Layout** (`core/automation/`): `automation.h/.cpp` (owner singleton), `automation-main.cpp` (standalone exe), plus `cli/`, `lua/`, `python/`, `webapi/`. Third-party deps are vendored per module (`lua/lib/{lua,sol2}`, `webapi/lib/{drogon,jsoncpp,zlib}`, `python/3rdparty/{pybind11,python-cmake-buildsystem}`, `cli/lib/cli11`).

**No base class, no registration mechanism.** `Automation` (`core/automation/automation.h:12-95`) holds raw pointers to each transport behind `#if ENABLE_*_AUTOMATION`, with hand-written `startX()/stopX()` helpers (`automation.cpp:118-238`). Each transport class independently defines `start()`/`stop()` — signatures differ (`AutomationCLI::start(uint16_t port=8765)` returns bool; `AutomationWebAPI::start()`/`AutomationLua::start()` return void). Adding "dezog" means editing 4 places in `automation.h` + `automation.cpp` plus the CMake files below; there is no plugin registry to hook.

**Options/features gating**: options are declared twice — root `CMakeLists.txt:20-24` and `core/automation/CMakeLists.txt:17-21` (`ENABLE_AUTOMATION` master switch forces all off, `automation/CMakeLists.txt:26-30`). Qt app force-sets them (`unreal-qt/CMakeLists.txt:10-14`) and must replicate the `target_compile_definitions` block exactly (`unreal-qt/CMakeLists.txt:284-287` vs the automation CMake block — explicit "must match" comment). Sub-target names go into `AUTOMATION_*_TARGET` vars, linked with `$<$<BOOL:...>>` genexes; sub-libs linked **PUBLIC** deliberately.

**Per-module CMake pattern** (best template: `core/automation/cli/CMakeLists.txt`): `project(automation-cli)`, `add_library(... STATIC)`, include dirs (`include/`, `lib/`, `${ROOT_DIR}/core/src`), the four `ENABLE_*_AUTOMATION` compile defs, install rules, output dirs, `ws2_32` on WIN32. "Core library is linked by the parent project" — static automation libs use headers only.

**Getting the Emulator**: nobody is handed a pointer. Every module resolves through the `EmulatorManager` singleton (`core/src/emulator/emulatormanager.h:54,65-176`): `GetInstance()`, `GetEmulatorIds()`, `GetEmulator(id)`, `GetMostRecentEmulator()`, `GetSelectedEmulatorId()` (globally shared selection, :119). CLI does this in its ctor (`cli/src/automation-cli.cpp:33-45`) and per client connection (:441-451), then `CLIProcessor::SetEmulator(shared_ptr<Emulator>)`. `Automation::getEmulatorIdOrFirst()` (`automation.cpp:88-104`) is the fallback helper.

**Threading**: every module runs its own thread(s). Lua: worker thread + task queue + `dispatchSync` promise/future marshalling (`lua/src/automation-lua.h:14-30,66-93`). WebAPI: one thread running drogon's event loop; `stop()` calls `drogon::app().quit()`, joins with 1 s timeout then detaches (`webapi/src/automation-webapi.cpp:133-178`). CLI: acceptor thread + one thread per client, tracked for clean shutdown (`cli/include/automation-cli.h:54-70`).

## 2. Existing TCP/socket servers

- **CLI = raw TCP server, hand-rolled, cross-platform — the template for a binary-protocol server.** `core/automation/cli/src/automation-cli.cpp`: `socket()` :224, `SO_REUSEADDR` :235, `bind` :251, `listen` :257, `poll()` accept loop (chosen over `select()` to dodge FD_SETSIZE) :267, `accept` :292, per-client `select`+`recv` loop :486-510. Portability shim `cli/include/platform-sockets.h` (winsock2 ordering, `SOCKET`/`INVALID_SOCKET`, errno mapping, `initializeSockets()`/`cleanupSockets()`). Protocol is line-based telnet (IAC negotiation :425-433), but the transport layer is byte-oriented and reusable verbatim.
- **WebAPI = drogon** (vendored, trantor event loop), requires OpenSSL — self-disables if absent. Port hardcoded 8090 (`automation-webapi.cpp:226`), `setThreadNum(2)`; pre-checks port availability with raw sockets because drogon `exit()`s on bind failure. A WebSocket controller exists (`webapi/src/emulator_websocket.h/.cpp`).

No cpp-httplib/boost/asio anywhere. Ports taken: CLI 8765, WebAPI 8090; the GDB TDD reserves 2000.

## 3. GDB stub / RSP status

**No implementation exists** — only design docs plus a C# reference in `other/ZXMAK2/src/ZXMAK2.Hardware.GdbServer/` (`GDBNetworkServer.cs`, `GDBSession.cs`, `GDBPacket.cs`).

- `docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md` (654 lines, Draft v1.0, 2026-07-19). §2 (:37-61) prescribes the module layout: `core/automation/gdb/{automation-gdb, gdbserver, gdbpacket, gdbdispatcher, gdbtarget_z80, gdbmonitor}` behind `ENABLE_GDB_AUTOMATION`, owned by `Automation`; "transports talk to Emulator/EmulatorContext public APIs only… thin protocol adaptation, not new debugger logic". §3.1 threading contract; §3.3 (:133-149) the **run-control claim** in `EmulatorContext` (advisory owner token so WebAPI/Lua/Qt can't resume a debugger-held target); §3.4 out-of-band events; §6.4 (:616-624) config conventions (`unreal.ini`, `gdb_port` 2000, bind 127.0.0.1, ModuleLogger, shutdown ordering); §7 tests under `core/tests/automation/gdb/`; §8 phase plan. It names DeZog: "the z80-gdb forks from the DeZog ecosystem" (:633).
- `docs/emulator/design/control-interfaces/gdb-protocol.md` (550 lines) — protocol spec, register mapping (:89), planned packets (:118), server architecture sketch (:370-482).

No `dezog`/`DZRP`/`zsim` references anywhere in the repo yet.

## 4. Event subscription and run control

**MessageCenter**: `core/src/3rdparty/message-center/messagecenter.h` + `eventqueue.h:167-170` — four `AddObserver` overloads. Pattern: `MessageCenter::DefaultMessageCenter().AddObserver(TOPIC, static_cast<Observer*>(this), static_cast<ObserverCallbackMethod>(&Class::onEvent))` — see `unreal-qt/src/emulator/emulatorbinding.cpp:272-296`; lambda-style in `core/src/emulator/emulator.cpp:2091`.

**Topics**: `constexpr char const*` in `core/src/emulator/platform.h:20-56`. Relevant: `NC_EXECUTION_BREAKPOINT` (:36, payload `SimpleNumberPayload` = breakpoint ID), `NC_EXECUTION_CPU_STEP` (:35), `NC_EMULATOR_STATE_CHANGE` (:33), `NC_SCANLINE_BOUNDARY`, `NC_BREAKPOINT_CHANGED`, `NC_SYSTEM_RESET`, instance lifecycle topics. Posted from `z80.cpp:240`, `memory.cpp:209,278`, `portdecoder.cpp:113-219`.

**Known trap** (GDB TDD §6.3, confirmed in code): `NC_EXECUTION_BREAKPOINT` / `NC_EMULATOR_STATE_CHANGE` payloads carry **no emulator instance ID** — with multiple instances an observer wakes on the wrong emulator's event. `debuggerwindow.cpp:213-222` documents abandoning global subscription for this reason (routes via `EmulatorBinding` signals). Tagging these payloads is a prerequisite for any correct multi-instance debug server.

**Run control** — `core/src/emulator/emulator.h`: `Pause(bool broadcast=true)` / `Resume(bool broadcast=true)` (:183-184), `WaitWhilePaused()` (:185), `Stop()`, `Reset()`. Stepping (:195-211): `RunSingleCPUCycle(skipBreakpoints=true)`, `RunNCPUCycles`, `RunFrame`/`RunNFrames`, `StepOver()` + `CancelPendingStepOver()`, `RunTStates`, `RunUntilScanline`, `RunNScanlines`, `RunUntilNextScreenPixel`, `RunUntilInterrupt`, `RunUntilCondition(std::function<bool(const Z80State&)>, maxTStates)`. State: `IsPaused()/IsRunning()/IsDebug()`, `DebugOn()/DebugOff()` (swap fast↔instrumented memory interface, `emulator.cpp:2114-2129`).

`StepOver()` is temp-breakpoint + `Resume()` and is **asynchronous** — completion detected via an `NC_EXECUTION_BREAKPOINT` observer (`emulator.cpp:2050-2097`). A debug server must not assume `StepOver()` returns with the CPU stopped. CLI's guard pattern (check `IsPaused()`, snapshot PC, act, re-fetch state): `cli/src/commands/cli-processor-debug.cpp:19-240`.

**Breakpoints** — real API is `breakpointmanager.h:153-255`: `AddExecutionBreakpoint(addr, owner=OWNER_INTERACTIVE)`, `AddMemRead/MemWrite/PortIn/PortOut`, `*InPage` bank-aware variants, `AddCombinedMemory/PortBreakpoint`, `RemoveBreakpointByID`, activate/deactivate, groups. **The `owner` string is the mechanism to keep protocol-owned breakpoints separate from user/Qt ones** — use `owner="dezog"`.

## 5. Register access

`core/src/emulator/cpu/z80.h`: `struct Z80Registers` :20 (packed) — unions giving 16-bit and 8-bit views (`pc/pcl/pch` :33, `sp` :43, `ir_/r_low/i` :54, af/bc/de/hl, `alt.` shadow set, ix/iy). `struct Z80State : Z80Registers, Z80DecodedOperation` :291 adds `prev_pc`, `m1_pc`, `nextpc`, `isDebugMode`, `int_pending`, `tpi`, memory interface pointers. `class Z80 : public Z80State` :330 — the live CPU object *is* the state struct.

Access: automation uses `Z80State* s = emulator->GetZ80State()` (`cli-processor-debug.cpp:56,64,613,619`), **must re-fetch after any execution step**. Qt uses `emu->GetContext()->pCore->GetZ80()` (`registerswidget.cpp:44-49,73-79`).

**Writes**: there is **no register setter API anywhere** (grep: zero hits). A DZRP `CMD_SET_REGISTER` equivalent would be the first writer — poke `Z80State` fields directly, safe only while paused (document/enforce). Debugger memory access: `Memory::DirectReadFromZ80Memory` / `DirectWriteToZ80Memory` (`memory.h:299-301`).

## Concrete implications for a dezog module

1. Copy `cli/` as the skeleton: raw TCP + `platform-sockets.h` (include, don't re-invent), acceptor thread + per-client thread, `ws2_32` on WIN32, static lib `automation-dezog`.
2. Wire: option `ENABLE_DEZOG_AUTOMATION` in root `CMakeLists.txt:20-24`, `core/automation/CMakeLists.txt` (option + add_subdirectory + target var + link + include + compile def), `unreal-qt/CMakeLists.txt:10-14` and `:284-287`, four `#if` blocks in `automation.h/.cpp`.
3. Reuse the GDB TDD's architecture decisions wholesale (module placement §2, threading §3.1, run-control claim §3.3, out-of-band §3.4, config/logging/shutdown §6.4) — DZRP and RSP have the same shape; `gdbpacket/gdbdispatcher/gdbtarget_z80` maps 1:1 to binary framing/dispatch/register-codec.
4. Prerequisites flagged by docs and confirmed in code: (a) instance-tagged `NC_EXECUTION_BREAKPOINT`/`NC_EMULATOR_STATE_CHANGE` payloads; (b) run-control claim so other frontends can't resume a debugger-held target; (c) register **write** support does not exist yet.
