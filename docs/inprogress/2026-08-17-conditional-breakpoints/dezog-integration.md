# DeZog Integration: A DZRP Automation Module for unreal-ng

Date: 2026-08-17
Status: proposal (research-backed; see `research/dezog-dzrp.md` for the protocol deep dive and `research/automation-modules-survey.md` for the infrastructure survey)

## 1. Why

[DeZog](https://github.com/maziac/DeZog) is the de-facto standard VS Code debug adapter for Z80/ZX Spectrum development: source-level debugging for sjasmplus/z88dk projects, labels, WPMEM/ASSERTION/LOGPOINT annotations, unit tests, memory/sprite views, reverse-step "lite". Emulators plug into it as *remotes*. Today the remotes are ZEsarUX, CSpect, MAME (gdbstub), the internal simulator (zsim), and real ZX Next hardware. Supporting DeZog turns unreal-ng into a first-class target for the modern cross-assembler workflow — anyone building with sjasmplus gets breakpoints, stepping, and variable views in VS Code against our emulator.

The integration cost is deliberately small: DeZog's own protocol, **DZRP (DeZog Remote Protocol)**, was designed so that the remote implements a thin command set while DeZog itself does the heavy lifting (condition evaluation, disassembly, step-over planning, history). The CSpect plugin — a complete, shipping DZRP server — is roughly a dozen small C# files.

## 2. What DZRP is

DZRP is a binary, little-endian, strictly serialized request–response protocol over a byte stream (TCP in our case; the wire format is transport-agnostic):

- **Command** (DeZog → remote): `[len:4][seq:1][cmd:1][payload]`, seq 1–255 wrapping.
- **Response** (remote → DeZog): `[len:4][seq:1][payload]` — no command ID; correlation is by seq, one command outstanding at a time.
- **Notification** (remote → DeZog): `[len:4][0][ntf:1][payload]` — seq 0 is reserved; the only notification is `NTF_PAUSE`, reporting why execution stopped (manual, breakpoint, watchpoint read/write) with the address, bank, and a display string.

Current protocol version: **2.0.0** (2.1.0 adds only the optional `CMD_INTERRUPT_ON_OFF`). Version is negotiated in `CMD_INIT`; mismatched major version must be rejected. Full command table with payload layouts: `research/dezog-dzrp.md` §2.2.

Key semantics that shape the implementation:

- **No step commands exist.** Step-into/over/out are all `CMD_CONTINUE` carrying up to two temporary breakpoints, an "alternate command" byte, and a step-over PC range. The `CMD_CONTINUE` response is an immediate ack; the eventual stop arrives asynchronously as `NTF_PAUSE`. (Classic first-timer bug: blocking the response until the CPU stops.)
- **Long addresses.** Breakpoints, watchpoints, and pause notifications carry `address:2 + (bank+1):1`, where bank byte 0 means "plain 64K address, match any bank". A banked breakpoint must match *both* PC and the bank currently mapped into that slot — exactly the slot/page filtering we are designing for conditional breakpoints, so both features share one mechanism.
- **Conditions are DeZog's job, not ours.** The condition string arrives in `CMD_ADD_BREAKPOINT`, but for socket remotes DeZog evaluates it client-side: on every `NTF_PAUSE` it checks the expression (reading registers/memory over DZRP) and silently sends `CMD_CONTINUE` if false. A remote may ignore condition strings entirely and still be fully functional ("slow" mode). Once our native conditional-breakpoint engine lands, we can evaluate remote-side as the "fast" path — the protocol explicitly allows either.
- **Watchpoints are the one feature the remote must genuinely implement** (`CMD_ADD_WATCHPOINT`: range + read/write mask; `NTF_PAUSE` reasons 3/4). Notably, neither CSpect nor the ZX Next remote supports them — our memory-access hooks make this an easy differentiator.

## 3. How DeZog will connect

There is no generic "dzrp" remote type in DeZog's launch.json — the shipped socket type is `cspect`. Since the CSpect plugin is just a DZRP-over-TCP server, we implement the identical server and users configure:

```json
{
    "remoteType": "cspect",
    "cspect": { "hostname": "localhost", "port": 12000 }
}
```

DeZog cannot tell the difference; `CMD_INIT` even carries our server name string ("unreal-ng x.y"), which DeZog displays. The `cspect` remote may occasionally send ZX-Next-only commands (TBBlue register, sprites) — we answer with zeros/empty payloads, never errors or disconnects.

Longer term, the clean route is a small TypeScript PR to DeZog adding a `unreal` remote type. That is also the only way to get full bank-aware debugging on ATM Turbo 2+ / ZX Evolution: the DZRP wire format carries banks as one byte (`bank+1`, so max bank 254) and machine types only up to ZXNEXT — 256-page machines need a DeZog-side memory-model addition. Until then we report machine type `ZX128K` (or `ZX48K`) with the matching 4×16K slot layout, and banked breakpoints work for the 128K subset while plain 64K breakpoints work everywhere.

## 4. Module design: `core/automation/dezog/`

Follow the existing automation-module pattern (no plugin registry exists; each module is a static lib wired by hand — see `research/automation-modules-survey.md` §1). The GDB TDD (`docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md` §2–3) already prescribes the right architecture for a debug-protocol module; DZRP has the same shape as GDB RSP (framing → dispatch → target adaptation), so we reuse its decisions wholesale.

```
core/automation/dezog/
├── CMakeLists.txt              # modeled on cli/CMakeLists.txt (static lib, ws2_32 on WIN32)
├── include/
│   ├── automation-dezog.h      # AutomationDezog: start(port=12000)/stop(), owns server thread
│   ├── dzrp-server.h           # TCP accept loop (reuse cli/include/platform-sockets.h)
│   ├── dzrp-framing.h          # [len][seq][cmd] codec, notification builder
│   └── dzrp-dispatcher.h       # command → handler table
└── src/
    ├── automation-dezog.cpp
    ├── dzrp-server.cpp
    ├── dzrp-framing.cpp
    ├── dzrp-dispatcher.cpp
    └── dzrp-target.cpp         # adaptation to Emulator/Memory/BreakpointManager APIs
```

Wiring (all mechanical, mirrors existing modules): `ENABLE_DEZOG_AUTOMATION` option in root `CMakeLists.txt` and `core/automation/CMakeLists.txt`; compile-definition blocks in `unreal-qt/CMakeLists.txt` (must match — existing comment); `#if ENABLE_DEZOG_AUTOMATION` blocks in `automation.h/.cpp` (`startDezog()/stopDezog()`). Config in `unreal.ini`: `dezog_port` (default 12000 — 8765/8090/2000 are taken), bind 127.0.0.1 only, ModuleLogger submodule, shutdown ordering per the WebAPI precedent.

**Threading**: acceptor thread + one client connection at a time (DeZog is single-client; refuse a second connect). The client thread reads commands and calls emulator APIs; `NTF_PAUSE` is emitted from a MessageCenter observer on `NC_EXECUTION_BREAKPOINT` — i.e. from the emulator thread — so the socket writer needs a small mutex (frame-at-a-time atomic writes).

### Mapping DZRP onto existing APIs

| DZRP | unreal-ng API |
|---|---|
| CMD_INIT | machine type from current model config → 2 (48K) / 3 (128K); name "unreal-ng" |
| CMD_GET_REGISTERS | `Emulator::GetZ80State()` fields; slots via `Memory::GetRAMPageForBank/GetROMPageForBank/GetMemoryBankMode` per bank 0–3 |
| CMD_SET_REGISTER | direct `Z80State` field writes (paused-only) — **no setter API exists today; this module is its first client** |
| CMD_READ_MEM / CMD_WRITE_MEM | `Memory::DirectReadFromZ80Memory` / `DirectWriteToZ80Memory` (side-effect-free — critical, DeZog hammers reads after every stop) |
| CMD_ADD/REMOVE_BREAKPOINT | `BreakpointManager::AddExecutionBreakpoint(addr, owner="dezog")`; bank byte ≠ 0 → the `*InPage` bank-aware variant; DZRP's 16-bit IDs map 1:1 to our `breakpointID` |
| CMD_ADD/REMOVE_WATCHPOINT | `AddMemRead/MemWrite` over the range (range support is part of the conditional-breakpoints design; until then, per-address expansion) |
| CMD_CONTINUE | temp breakpoints in a `"dezog_temp"` group + `Resume()`; alternate cmds via naive single-step loop first (`RunSingleCPUCycle`), proper step-over later (`StepOver()` is async — completion via NC_EXECUTION_BREAKPOINT, do not assume synchronous) |
| CMD_PAUSE | `Emulator::Pause()` + NTF_PAUSE reason 1 |
| CMD_READ_PORT / CMD_WRITE_PORT | port decoder access (phase 2) |
| CMD_READ_STATE / CMD_WRITE_STATE | our snapshot save/load as the opaque blob (phase 2, cheap win) |
| 5, 10–19, 22, 23 | graceful stubs (zeros/empty); log unknown commands, never crash |

### Prerequisites surfaced by the survey (shared with the GDB plan)

1. **Instance-tagged events**: `NC_EXECUTION_BREAKPOINT` / `NC_EMULATOR_STATE_CHANGE` payloads carry no emulator-instance ID; with multiple instances an observer wakes on the wrong emulator's event (the Qt debugger already works around this). Tag the payloads.
2. **Run-control claim** (GDB TDD §3.3): an advisory owner token in `EmulatorContext` so CLI/WebAPI/Qt can't resume a target DeZog holds paused.
3. **Register writes**: first-ever writer of `Z80State` from automation; enforce paused-only.

## 5. Phasing

- **Phase 1 — MVP** (first VS Code session): TCP server + framing; CMD_INIT/CLOSE, GET_REGISTERS (+slots), SET_REGISTER, READ/WRITE_MEM, ADD/REMOVE_BREAKPOINT (64K + 128K banked; condition string ignored), CONTINUE (temp bps, naive stepping), PAUSE, NTF_PAUSE reasons 0/1/2, stubs for the rest. Tests under `core/tests/automation/dezog/` (framing round-trip, dispatcher against a mock target, golden byte sequences from DeZog's transport log).
- **Phase 2**: watchpoints (reasons 3/4) — ahead of CSpect; ports; CMD_SET_SLOT; state save/restore via snapshots; real step-over/step-out semantics; run-control claim.
- **Phase 3**: remote-side condition evaluation ("fast" conditions) reusing the conditional-breakpoints expression engine; DeZog-side PR for an ATM/Evolution memory model (the one hard wire-format limit: 1-byte bank+1 caps banks at 254).

## 6. Relation to the GDB plan

The gdb TDD and this module are siblings, not competitors: same placement, threading contract, event prerequisites, and target-adaptation layer. DZRP is the *simpler* protocol (binary framing vs. RSP escaping, ~15 commands vs. ~40 packets, banking built in vs. absent) and delivers a better-matched frontend (DeZog knows Z80 and Spectrum banking; GDB does not). Recommended order: DeZog first, GDB later reusing the target-adaptation layer (`dzrp-target.cpp` generalizes into the TDD's `gdbtarget_z80`).
