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

## 5. The rest (outline; designed when reached)

| Item | What | Notes |
|---|---|---|
| F4 | `/stepout`, `/skip_until` (and the long `/steps`) start the run and report through events, not by holding an HTTP worker | the same pattern as Continue |
| F5 | `/stepout`, `/skip_until` and the run_* calls check the run-control claim (409 when another surface holds it) like `/step` does | |
| E8 | Long-poll `GET /debug/wait?since=<seq>&timeout_ms=` | answers when the snapshot's `seq` moves; async (no worker held) |
| E2 | PC history with page, `GET /debug/pchist?depth=` | per-instruction ring armed only while a debugger asks for it; A/B benchmark |
| D9 | `read_write` per window in `/state/paging` for TS-Conf | verify first |
| E7 | Label import (XAS / ALASM) | low priority |
