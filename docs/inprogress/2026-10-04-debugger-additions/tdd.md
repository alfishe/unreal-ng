# Debugger additions after the snapshot (E1, E6, E4, E5, F4, F5, E8, E2, D9, E7)

Status: design, 2026-10-04. Plan and progress: [TODO.md](TODO.md).

Source of the list: [webapi-gap-analysis.md](../2026-09-24-tui-debugger/webapi-gap-analysis.md) §3 and its
"Additions, in recommended order" table. Done before this folder: F1 / D3 (a step that hits a breakpoint no longer
parks the HTTP thread), F2 (WebSocket debugger events), D1-D8, the breakpoint work (pages, ranges, port masks,
hits) and D7 + E3 ([debugger-snapshot](../2026-10-04-debugger-snapshot/tdd.md)). What these additions are for:
the TS-Conf TUI debugger ([TDD-DBG-02](../2026-09-24-tui-debugger/TDD-DBG-02_unreal-tsconf-debugger-tui.md)) and
any other debugger front end ("skin") of the [debugger model](../2026-09-28-debugger-model/).

Every item is on every automation surface (WebAPI + OpenAPI, MCP, CLI, Lua, Python) and, where a person would use
it, in the Qt debugger; each surface's docs and the recipes are updated in the same step. Each step adds; nothing
existing changes its answer.

Each item gets its full design here when its turn comes; the ones below E1 are the outline from the analysis.

## 1. E1: write a port from a debugger

### 1.1 The problem in one example

On TS-Conf a debugger user wants RAM page #20 in window 3 (#C000-#FFFF) to look at it. The CPU does that with
`OUT (#13AF),#20`. Today no automation surface can do it: memory writes reach RAM, register writes reach the CPU,
but nothing goes through the machine's port decoder. The only port writes from outside the CPU are three GDB
pseudo-registers (#7FFD, #1FFD, #FE) that call the decoder bare - breakpoints may fire on the calling thread and
TTD does not see the edit.

After this step:

```text
POST /api/v1/emulator/{id}/ports/out   {"port":"0x13AF","value":"0x20"}
-> {"port":"0x13AF","value":"0x20","moment":"paused"}
```

and window 3 shows page #20, exactly as after the CPU's OUT.

### 1.2 What a port write does

The machine's own decoder handles it (`PortDecoder::DecodePortOut`, the model's override), so every side effect a
CPU OUT has, it has: paging, the TS-Conf registers (a DMA start included), AY register select and data, the
border, a floppy controller command, a card's port. Three things differ, because the write comes from a tool and
not from the CPU:

| What | CPU OUT | Tool port write | Why |
|---|---|---|---|
| Port breakpoints | fire | do not fire | the write is the user's own action; firing would stop the machine on its own edit (and, on an HTTP thread, park it - finding F1) |
| Device wait states (`Z80::AddWaitStates`, `AddWaitTicks`: TS-Conf external I/O / IDE stall, Scorpion turbo I/O, ATM3, Sprinter, COM port...) | stretch the instruction | dropped | a tool write takes no time of the machine: the CPU's clock and the wait observer (TS-Conf frame INT counter) are left as they were |
| TTD | an ordinary input | a tool edit (`Emulator::EditMemoryFromTool`) | replay cannot reproduce it; the edit is recorded with every device state it changed, as a memory write from a tool is |

The port trace and the memory-access tracker still see the write (they log what the decoder did); its "caller"
is the CPU's PC at that moment.

`seq` of the debugger snapshot grows (EditMemoryFromTool calls NoteDebugChange), so a front end redraws.

### 1.3 When the write happens

The same moments as the snapshot (one shared helper, `Emulator::RunAtCoherentMoment`, which the snapshot's Build
switches to):

| Emulator | Where the write runs | `moment` |
|---|---|---|
| paused (confirmed park) | on the caller's thread while the emulation stays parked | `paused` |
| created, never started | on the caller's thread | `stopped` |
| running | on the emulation thread between two frames (no pause, no audio gap) | `frame` |
| being stepped by another client, no frame boundary within 500 ms | not done | 503 busy |

### 1.4 Core

```cpp
// core/src/debugger/ports/portwrite.h
namespace PortWrite
{
struct Result { bool ok; bool busy; std::string moment; std::string error; };
/// One port write through the machine's decoder, as a tool edit (see tdd §1.2)
Result Write(Emulator* emulator, uint16_t port, uint8_t value, const char* source);
}
```

- `Z80::OutOfTimeScope` (RAII): saves `tt` and the wait observer, detaches the observer, restores both at the end.
  Nothing on the CPU's path changes.
- Breakpoints: `EmulatorContext::toolPortAccess` is set for the write; `BreakpointManager::ResolvePort` checks it
  after its "no port breakpoints" early return, so a machine without port breakpoints pays nothing and one with
  them pays one load of a bool on a port access - no hot-path change (performance guideline: the check sits behind
  an existing gate).

### 1.5 Surfaces

| Surface | Form |
|---|---|
| WebAPI | `POST /ports/out` `{"port": "0x13AF" or 5039, "value": "0x20" or 32}`; 200 `{port, value, moment}`, 400 bad port / value, 503 busy, 404 no emulator |
| MCP | `control_execution` action `port_out` with `port`, `value` |
| CLI | `out <port> <value>` (hex `#13AF`, `0x13AF`, `13AFh` or decimal) |
| Lua | `port_out(port, value)` -> `true` or `nil, error` |
| Python | `emu.port_out(port, value)` -> `None`, raises on error |
| Qt | debugger toolbar "Port OUT..." (port and value, hex) |

The parameter parsing (hex forms, range checks) is one core function, so every surface accepts the same text.

### 1.6 Tests

- `PortWrite_Test`: 128K #7FFD write switches the page in window 3 (paused and stopped), a port breakpoint on
  #7FFD does not fire, the CPU clock is unchanged on a TS-Conf with the external I/O stall on, `seq` grows, TTD
  recording gets a tool-edit marker, a running machine takes the write at a frame boundary (`moment` = `frame`).
- The TS-Conf example of §1.1 (#13AF = #20 -> window 3 shows page #20).
- MCP unit test for `port_out`; WebAPI live check with curl (core-tests cannot link drogon).

## 2. E6: TS-Conf CRAM and SFILE as device memory regions

### 2.1 The problem in one example

The TS-Conf palette (CRAM, 256 colors of 15 bits) and the sprite table (SFILE, 85 sprites of 3 words) live inside
the chip, not in RAM. A program writes them through the FM window (a 4 KB window it maps over RAM with `FMADDR`)
or by DMA. A debugger can read them (`/state/tsconf` shows the palette), but cannot change a color or move a
sprite.

After this step:

```text
GET  /api/v1/emulator/{id}/memory/region/cram?offset=2&length=2        -> color 1, low byte first
POST /api/v1/emulator/{id}/memory/region/cram {"offset":2,"hex":"1F00"}  -> color 1 becomes pure blue
```

### 2.2 Design

The device memory regions (`devicememory.h`, Sprinter `vram`) already reach every surface: WebAPI
`/memory/regions` and `/memory/region/{name}`, CLI `memory region`, Lua / Python `region_read` / `region_write`,
MCP `inspect_state` `memory_region` (writes through `invoke_api`). So this step only gives the TS-Conf decoder two
regions. The Qt debugger has no view of device memory regions at all yet (the Sprinter `vram` neither); that view
is its own item (TODO A2q), for every machine at once, not a TS-Conf special case: the debugger toolbar's "Device
memory" dialog lists the machine's regions and shows one as hex; a typed byte goes through `DeviceMemory::Write`
(overwrite only, the region size is fixed).

| Region | Size | Layout | A write |
|---|---|---|---|
| `cram` | 512 bytes | word n (color n) at offset 2n, low byte first: bits 14-10 R, 9-5 G, 4-0 B, bit 15 the VDAC flag | the screen catches up first (the change shows from the current dot), the palette is rebuilt, the video change log notes it |
| `sfile` | 512 bytes | word n at offset 2n, low byte first; sprite d is words 3d, 3d+1, 3d+2 | the screen catches up first (SFILE is read per line) |

The layout is the FM window's (offsets #000-#1FF and #200-#3FF of the window), so an address seen in a program's
FM write is the region offset. A region write changes one byte of a word directly; it does not use the FM
window's "even byte waits for the odd one" latch, so it never disturbs a program's half-done FM write. The
decoder's `CommitTableWord` is the one place a CRAM / SFILE word changes outside DMA; the FM window calls it too.

Writes are tool edits (`DeviceMemory::Write` already wraps them in `EditMemoryFromTool`); the TS-Conf device
state that TTD keeps carries CRAM and SFILE.

### 2.3 Tests

`TsConfRegions_Test`: both regions listed with their sizes; a `cram` write changes `GetState().cram`, the palette
version and the rendered color; an `sfile` write changes the sprite word; reading is side-effect free; an FM-window
write and a region write give the same word; a TTD restore after the edit keeps it.

## 3. The rest (outline; designed when reached)

| Item | What | Notes |
|---|---|---|
| E4 | Disk sector write `PUT /disk/{drive}/sector/{cyl}/{side}/{sec}` | marks the image modified; a tool edit for TTD |
| E5 | NVRAM (CMOS) read and write | check what `/rtc/cells` already covers first |
| F4 | `/stepout`, `/skip_until` (and the long `/steps`) start the run and report through events, not by holding an HTTP worker | the same pattern as Continue |
| F5 | `/stepout`, `/skip_until` and the run_* calls check the run-control claim (409 when another surface holds it) like `/step` does | |
| E8 | Long-poll `GET /debug/wait?since=<seq>&timeout_ms=` | answers when the snapshot's `seq` moves; async (no worker held) |
| E2 | PC history with page, `GET /debug/pchist?depth=` | per-instruction ring armed only while a debugger asks for it; A/B benchmark |
| D9 | `read_write` per window in `/state/paging` for TS-Conf | verify first |
| E7 | Label import (XAS / ALASM) | low priority |
