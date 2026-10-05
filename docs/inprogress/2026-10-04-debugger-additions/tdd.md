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

## 3. E4: write a disk sector

### 3.1 The problem in one example

A TR-DOS disk's catalog is track 0, side 0, sectors 1-8; sector 9 holds the disk title and the free-space counters.
A debugger can read any sector (`GET /disk/A/sector/0/0/9`), but cannot fix a byte in it: patching a game on the
disk, repairing a catalog entry, or putting a test pattern where a loader will read it all need a sector write.

After this step:

```text
PUT /api/v1/emulator/{id}/disk/A/sector/0/0/9   {"offset": 245, "hex": "4D594449534B"}
-> {"drive":"A","cylinder":0,"side":0,"sector":9,"offset":245,"bytes_written":6,"sector_size":256,"moment":"paused"}
```

and the disk title starts with "MYDISK"; the next `READ SECTOR` of the guest sees it.

### 3.2 Design

- **Addressing** as the read: drive A-D, cylinder, side, sector **ID** (the R byte of the sector's address mark,
  1-based for TR-DOS), whatever the physical interleave. `offset` (default 0) + the bytes must fit in the sector's
  data field (`sector_size`, 128 << N).
- **The write** is `Track::writeSectorData` on the data field (the WD1793 `WRITE SECTOR` path): the data CRC is
  recalculated, the sector and track are marked dirty, so the image counts as modified and the usual save on eject
  applies. The address mark is not touched.
- **Refused**: no disk, no such track / sector, a sector with no data field (ID-only), bytes past the data field,
  and a **write-protected** disk (as the drive would refuse a guest write; clear the protect switch first).
- **When**: a coherent moment, as the port write (`RunAtCoherentMoment`): never under a running WD1793 command
  half-way through a sector.
- **TTD**: a tool edit (`EditMemoryFromTool`): a debugger-edit marker on the timeline. Disk content is media, not
  machine state (TTD v2 decision 25), so the marker is what tells a seek that the disk changed there.

Core: `core/src/debugger/media/sectorwrite.h`, `SectorWrite::Write(emulator, drive, cylinder, side, sector,
offset, bytes, source)`. Every surface calls it:

| Surface | Form |
|---|---|
| WebAPI | `PUT /disk/{drive}/sector/{cyl}/{side}/{sec}` with `offset` and `hex` (spaces allowed), `data` (byte array) or `base64` |
| MCP | `invoke_api` (no dedicated disk tool exists; the disk recipes use it) |
| CLI | `disk write <drive> <cyl> <side> <sec> <hex> [--offset N]` |
| Lua | `disk_write_sector(drive, cyl, side, sec, data [, offset])`, drive 0-3, data a string of bytes or a table -> `true` or `nil, error` |
| Python | `emu.disk_write_sector(drive, cyl, side, sec, data, offset=0)`, data bytes -> `None`, `ValueError` / `RuntimeError` |

Lua and Python number the sector from 0 (ID - 1), because their existing `disk_read_sector` does: on every surface
a read and a write with the same arguments name the same sector.
| Qt | debugger toolbar "Disk sector": drive / cylinder / side / sector, hex view, a typed byte is written |

### 3.3 Tests

`SectorWrite_Test` on a formatted TR-DOS image: a write lands in the data field (read back, data CRC valid), marks
the image dirty, keeps the address mark; offset + length past the field, a missing sector, an empty drive and a
write-protected disk are refused with their reasons; a WD1793 `READ SECTOR` after the write returns the new bytes;
TTD gets a debugger-edit marker. Qt: `DiskSectorDialog_Test`.

## 4. E5: CMOS and NVRAM

### 4.1 What was there and what was missing

The CMOS clock's cells were already on most surfaces: WebAPI `GET/POST /rtc/cells`, CLI `rtc read / write`, Lua /
Python `rtc_read` / `rtc_write`, MCP `inspect_state` `rtc` (writes through `invoke_api`); the one path is
`RtcAccess`. Missing: the Qt debugger, and the ZX-Evo AVR's 4 KiB EEPROM (what Unreal's `ED_NVRAM` editor shows),
which no surface could reach except 16 bytes at a time through the clock's `#F0-#FF` window as the guest does.

### 4.2 Design

Two device memory regions, so every region surface and the Qt "Device memory" dialog (§2.2) cover both without new
routes:

| Region | On | Size | Read | Write |
|---|---|---|---|---|
| `cmos` | every machine with a CMOS clock (ATM3 / TS-Conf, Profi, Scorpion SMUC, ...) | the chip's cells (128 or 256) | `Ds12887::PeekRegister`: side-effect free, register C keeps its flags | `Ds12887::WriteRegister`, a guest write: the time registers set the clock, C and D ignore it - the same as `RtcAccess::Write` |
| `eeprom` | ZX-Evo (`EvoAvr`: ATM3, TS-Conf) | 4096 | the byte | the byte (plain storage, no side effect) |

The clock is not part of a port decoder's own regions: `DeviceMemory::Regions` adds the regions of the machine's
clock (`RtcAccess::Find`, whichever board carries it) after the decoder's. The chip owns its region objects
(`Ds12887::CollectMemoryRegions`, `EvoAvr` adds `eeprom`). Writes are tool edits (`DeviceMemory::Write`); the EEPROM
is a region the TTD engine already compares at each capture (`EvoAvr::TTDRegions`), the cells are in the chip's
TTD state.

`/rtc/cells` and the `rtc_*` functions stay: they answer in the clock's terms (time, registers) and take the same
path.

### 4.3 Tests

`RtcAccess_Test.CmosRegionIsTheSamePath` on every clock machine (a region write reads back through `RtcAccess` and
the region); `RtcEepromRegion_Test` (a region write is the EEPROM byte; past the end refused); the TS-Conf region list
is cram, sfile, cmos, eeprom.

## 5. F4 + F5: long run-control calls and the run-control claim

### 5.1 The problems in one example

The WebAPI server has two HTTP worker threads. `POST /skip_until {"pc":"0x8000"}` with a target that is never
reached runs up to 700 million T-states (minutes) on one of them; a second such call takes the other, and from
then on nothing answers, not even the TUI's own `GET /debug/snapshot` polling (F4). Separately, `/step`, `/steps`
and `/stepover` answer `409 Conflict` while another surface (GDB) holds run control, but `/stepout`, `/skip_until`
and the `run_*` calls step the machine under GDB's feet (F5). The CLI has the same split.

### 5.2 Design

**F4: long calls leave the HTTP worker.** `steps`, `stepover`, `stepout`, `skip_until`, `run_tstates`,
`run_to_scanline`, `run_scanlines`, `run_to_pixel`, `run_to_interrupt`, `run_frame` and `run_frames` run on a small
pool of their own (4 threads, `LongCallPool`); the handler returns at once, the pool thread does the work and calls
drogon's `callback` when it is done (drogon allows answering from any thread). The answer, its fields and its
status codes are exactly as before: a client sees no difference except that the server keeps answering everything
else meanwhile. A fifth concurrent long call waits for a pool thread. The single `step` stays on the worker (one
instruction).

Stop notifications already exist on the WebSocket (`paused` / `step_done` with `seq`, F2); nothing new is needed for
a client that prefers not to wait on the reply.

**F5: one claim rule.** Every call that advances the CPU checks the run-control claim first and answers `409
Conflict` "Run-control held by <surface>" while another surface holds it: WebAPI `/stepout`, `/skip_until` and every
`run_*` join `/step`, `/steps`, `/stepover` and `/resume`; the CLI's `stepout`, `skip_until` and `run_*` join its
`step`, `stepover` and `steps`. One helper per surface (`RunControlHeldReply` in the WebAPI, `RunControlHeld` in
the CLI) so the message is the same everywhere. Lua and Python run inside the emulator's own process and check no
claim on any call today; they stay as they are (a separate decision if wanted).

### 5.3 Tests

Live (core-tests cannot link drogon): three never-ending `skip_until` calls at once, then `GET /emulator/{id}` answers
within a second; each long call still returns its JSON. MCP / CLI unit level: `CLIProcessor` answers `stepout`,
`skip_until`, `run_frames` with the claim message while a fake claim is held (`RunControlClaim_Test` style).

## 6. E8: wait for a change (long-poll)

### 6.1 The problem in one example

A simple client (a shell script, a browser page without WebSocket code) runs the machine and wants to know when it
stops at a breakpoint. Today it polls `GET /debug/snapshot` every 50 ms. After this step it asks once and the
answer comes when something changed:

```text
GET /api/v1/emulator/{id}/debug/wait?since=42&timeout_ms=5000
-> {"seq":43,"changed":true,"state":"paused","pause":{"reason":"breakpoint","breakpoint_id":3,"address":32768}}
-> {"seq":42,"changed":false,"state":"running","pause":{"reason":"none"}}          (5 s, nothing happened)
```

### 6.2 Design

- **What "changed" means**: the debugger snapshot's `seq` (§ snapshot tdd) moved past `since`: a stop, a run start,
  a tool edit. `since` omitted = the current `seq` (wait for the next change). `seq` already different = an
  immediate answer.
- **Core**: `Emulator::WaitDebugChange(since, timeoutMs)` blocks on a condition variable that `NoteDebugChange`
  signals (all its callers are rare paths: stops, run starts, tool edits); `DebugSnapshot::WaitAnswer` builds the
  reply (`seq`, `changed`, `state`, `pause` as in the snapshot). CLI, Lua and Python call the blocking form.
- **WebAPI without holding a worker**: the handler answers at once when `seq` already moved; otherwise it puts a
  10 ms repeating timer on drogon's event loop that checks `seq` and answers on a change or at the deadline. No
  thread waits; many clients can wait at once. `timeout_ms` 0..60000 (default 10000).
- **MCP**: `control_execution` action `wait` (`since`, `timeout_ms`) -> one line "changed: seq 43, paused at
  breakpoint #3 (8000)" or "no change in 5000 ms (seq 42, running)".
- **Qt**: none needed - the GUI gets the core notifications directly.

| Surface | Form |
|---|---|
| WebAPI | `GET /debug/wait?since=&timeout_ms=` |
| MCP | `control_execution` `wait` |
| CLI | `debug-wait [since] [--timeout ms]` |
| Lua | `debug_wait([since [, timeout_ms]])` -> table |
| Python | `emu.debug_wait(since=None, timeout_ms=10000)` -> dict |

### 6.3 Tests

`DebugSnapshot_Test`: a wait on a paused machine with no change times out after its timeout (small, 20 ms); a wait
while another thread pauses a running machine returns `changed` with reason `pause`; `since` already behind
answers at once. MCP unit test for `wait`; WebAPI live: a wait, then a pause from another request ends it.

## 7. E2: PC history with pages

### 7.1 The problem in one example

A TS-Conf program jumps into code paged in at `#C000` and crashes. The TUI's "PC hist" panel (TDD-DBG-02 §4.11)
shows the last instructions before the stop, each with the page its window showed, so "C000 ram32" and "C000 ram33"
are told apart. No server data existed for it.

```text
GET /api/v1/emulator/{id}/debug/pchist?depth=4
-> {"armed":true,"started_now":false,"total":5123,"capacity":1024,
    "entries":[{"address":49155,"kind":"ram","page":32},{"address":49152,"kind":"ram","page":32}, ...]}
```

### 7.2 Design

- **What is recorded**: one entry per instruction the CPU starts - its address and the physical page (kind and
  number) of the 16K window it lies in, at that moment. An accepted interrupt is not an instruction (no entry); a
  prefix is part of its instruction (one entry; the original Unreal debugger counted every M1 fetch, prefixes
  included - noise in a history). Pages for every machine, not only TS-Conf.
- **The ring**: the newest 1024 entries (`PcHistory`, owned by the debug manager).
- **Cost**: nothing until a debugger asks. `PcHistory::Arm(true)` raises the step-work bit `kStepWorkPcHistory`
  (the combined gate every step already loads, performance guidelines "combined gate"); only then does
  `Z80::StepInstructionWithWork` call `Record`. A machine that already takes the work path (TS-Conf, Sprinter, ATM3,
  Profi ...) pays one more bit test per step while the history is off - measured by A/B (7.4).
- **Arming**: the first read arms it (`started_now` says the entries start now); `POST /debug/pchist
  {"enabled": false}` stops it again, `true` restarts it empty. Reads happen at a coherent moment.
- **In the snapshot**: `pchist=N` adds the same report, so the TUI still repaints with one request.

| Surface | Form |
|---|---|
| WebAPI | `GET /debug/pchist?depth=`, `POST /debug/pchist {"enabled"}`, `GET /debug/snapshot?pchist=` |
| MCP | `inspect_state` aspect `pchist` (`count`), `snapshot` with `pchist` |
| CLI | `pchist [depth]`, `pchist on / off`, `debug-snapshot --pchist N` |
| Lua | `pc_history([depth])`, `pc_history_arm(on)`, `debug_snapshot{pchist = N}` |
| Python | `emu.pc_history(depth=32)`, `emu.pc_history_arm(on)`, `emu.debug_snapshot(pchist=N)` |
| Qt | debugger toolbar "PC history" (opening it starts the recording; Stop ends it) |

### 7.3 Tests

`PcHistory_Test`: off until armed (no step-work bit, nothing recorded), newest first, disarm keeps the entries, the
ring keeps the newest 1024, the TS-Conf page of window 3, the first report arms it, the snapshot's `pchist` field
and its limit. MCP `InspectState_PcHistoryAspect`; Qt `PcHistoryDialog_Test`.

### 7.4 A/B benchmark

A = `b98aa2243` (before), B = `7df0910b4` (the PC history, off), Release with `-DBENCHMARKS=ON` in detached
worktrees; load average 7-11; rounds A B A B A B B A B A; `cpu_time` per host frame (µs):

| Benchmark | min A | min B | paired mean B vs A | B slower in |
|---|---|---|---|---|
| `BM_HostFrame_48K_Fast` (plain step path, unchanged) | 1200.1 | 1141.9 | -4.5 % | 0 / 5 |
| `BM_HostFrame_TSConf_Fast` (work path) | 1928.3 | 1863.0 | -3.3 % | 0 / 5 |
| `BM_HostFrame_Sprinter_Fast` (work path) | 3100.0 | 3040.9 | -2.0 % | 0 / 5 |
| `BM_HostFrame_ATM710_Fast` | 2249.6 | 2189.8 | -2.9 % | 0 / 5 |

No regression. B is faster in every pair, also on the 48K whose step path did not change at all: a code-layout
effect of the rebuilt binary, not a speed-up from this change; the bit test on the work path is below what the
procedure can see.

## 8. D9: which windows a CPU write reaches

Verified first: `/state/paging` reported window 0 as "read/write" whenever it showed RAM, also on TS-Conf with
`W0_WE` off (the mapper sends those writes to the trash page), and windows 1-3 carried no access field at all;
`/state/memory`, the CLI and the snapshot hard-coded "read/write" too.

The truth is the write pointer the CPU uses: `Memory::IsWindowWritable(window)` is false when the window's writes go
to the trash page (ROM, write-protected RAM). Every report that lists windows now asks it: WebAPI `/state/paging`
(`writable` and `read_write` on all four), `/state/memory` and `/state/memory/rom` (`read_write`, `bank0_access`),
the snapshot's `pages[].writable`, Lua / Python `paging_state` (`writable`), CLI `paging` ("(read-only)") and
`state memory`. MCP's `paging` aspect forwards the WebAPI report. Test: `TsConfMemory_Test.WindowWritableFollowsTheMapper`
(ROM, RAM without and with W0_WE, windows 1-3).

Noticed, not changed here: those same reports hard-code `type` RAM for windows 1-3, which is wrong on machines that
can map ROM there (ATM Turbo); the snapshot's `pages[].kind` is right.

## 9. The rest (outline; designed when reached)

| Item | What | Notes |
|---|---|---|
| E7 | Label import (XAS / ALASM) | low priority |
