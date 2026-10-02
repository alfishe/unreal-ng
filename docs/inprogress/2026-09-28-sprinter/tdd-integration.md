# TDD — model registration, TTD, snapshots, automation, GUI

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): full start is the user default, `ide0.slave` empty, configuration module in the TTD blob. §2 TTD as built in S7 (2026-10-02) |
| **Index** | [technical-design.md](technical-design.md) |
| **Mapping** | [unreal-ng-mapping.md](unreal-ng-mapping.md) |

## 1. Model registration

| Step | Where | Change |
|---|---|---|
| Enum | `core/src/emulator/platform.h:305-325` (`MEM_MODEL`) | append `MM_SPRINTER` before `N_MM_MODELS` (no renumbering of existing values) |
| Model table | `core/src/emulator/config.h:47-68` (`Config::mem_model[]`) | `{ "Sprinter 2000", "SPRINTER", MM_SPRINTER, 4096, RAM_4096 }` |
| Creatable | `PortDecoder::IsModelSupported` + `GetPortDecoderForModel` (`core/src/emulator/ports/portdecoder.cpp:56-135`) | add `MM_SPRINTER` to both (they must stay in sync) |
| Config folder | `Config::GetConfigFolderForModel` (`core/src/emulator/config.cpp:749`) | default rule (lowercased short name) → `data/configs/sprinter/unreal.ini` |
| Memory subclass | `Core` factory (`core/src/emulator/cpu/core.cpp:105-108`, the `ScorpionMemory` precedent) | `new SprinterMemory(context)` for `MM_SPRINTER` |
| Screen | `VideoController::CreateScreen(model)` (PLAN #60(e), built) | `case MM_SPRINTER: return new ScreenSprinter(context);` |
| ROM | `ROM::GetROMFilename` / `LoadROM` (`core/src/emulator/memory/rom.cpp:66`, `:137`); `[ROM]` keys (`config.cpp:236`, `:257` pattern) | `[ROM] SPRINTER=rom/sprinter/sp2k-3.04.rom`, loaded raw as 16 pages (the ATM3 "whole image, no ROMSET" pattern) |
| Frame timing | `ApplyModelTimingDefaults` (`config.cpp:905-1057`) | `frame = 71680`, `t_line = 224`; `intstart/intlen` unused (the interrupt source owns INT) |
| Cache pages | `MAX_CACHE_PAGES` (`platform.h:251`) | 2 → 4 (64 KB fast RAM) |
| Qt model list | `unreal-qt/src/menumanager.cpp:627-641` | add `SPRINTER` |
| MCP text | `core/automation/mcp/src/mcp-tools.cpp:121` | add `SPRINTER` |
| AGENTS.md model list | repository root | add `SPRINTER` to the creatable list when S1 lands |

### 1.1 `data/configs/sprinter/unreal.ini`

```ini
[MISC]
HIMEM=SPRINTER
RAMSize=4096

[SPRINTER]
FastStart=0                 ; 0 = full start through the ROM loader (user default); 1 = skip it (test default)
CmosFile=sprinter-cmos.bin  ; relative to the config folder
DssBootLoader=              ; optional 1 536-byte DSS loader for folder volumes
Turbo=1                     ; front-panel turbo allowed (MAME "turbo hard")

[ULA]
Frame=71680
Line=224

[BETA128]
; four drives, 3.5" HD

;[MEDIA]
;ide0.slave.device = cdrom  ; optional empty CD unit, once ATAPI exists (S7); the default is no device

[ROM]
SPRINTER=rom/sprinter/sp2k-3.04.rom
```

Files follow the repo's CRLF/LF convention of the neighboring configs (new file: LF).

## 2. TTD and snapshots

As built in phase S7 (2026-10-02, branch `sprinter-ttd`; outcome and tests:
[s7-ttd-outcome.md](s7-ttd-outcome.md)). Serializers: `core/src/debugger/ttd/sprinter/ttdsprinter.h`,
`core/src/debugger/ttd/ttdwd1793context.h`; `PortDecoder_Sprinter::GetTTDModelStateIds()` declares and
`CreateTTDSerializers()` supplies them, the Profi / TSConf pattern.

### 2.1 Peripheral ids

`PeripheralId` is append-only (`core/src/debugger/ttd/ttdserializable.h`). The ids planned here at
15-18 were taken by other devices before S7; the Sprinter's are:

| Id | Name | Blob (v1, a version byte first) | Size |
|---|---|---|---|
| 25 | `SprinterPld` | `SprinterPldState` (112 bytes, padding-free), the decoder's own fields (Covox-Blaster control, powered-on flag, DCP-opened frame and PC), the INT source (mode page, frame lines, the acknowledged pulse, the keyboard INT flip-flop), the frame height the raster runs with, the active configuration module **by name** with its state (room for the largest registered module), the block accelerator's state (u16 size + bytes) | 177 + module + accelerator |
| 18 | `Ds12887` | shared serializer (cells, address latch, time base) | |
| 28 | `SprinterVideoRam` | the 256 KB video RAM; on load the pen cache and the INT list are rebuilt | 1 + 262 144 |
| 29 | `Z84C15` | `Z84C15::SaveState`: system registers, the wait generator with its power-on M1 counter and the RETI rule's after-ED flag, the watchdog, CTC, SIO (receive FIFOs), PIO, IP / IUS of every daisy-chain source | 1 + 171 |
| 30 | `SprinterFastRam` | the 64 KB fast RAM (Memory's cache pages are not RAM pages) | 1 + 65 536 |
| 31 | `SprinterInput` | `Ps2KeyboardStream::State` (bytes on the wire, typematic, held keys) and `MsSerialMouse::State` (the packet in flight, the last sample), the overrun counter | 85 |
| 35 | `Wd1793Context` | the WD1793 command in flight beyond the 254-byte BetaDisk blob: queued steps as tags, transfer pointers as (drive, track, offset), byte cell, rotational delay, rate-retry search | 1 + 112 |
| 32 | `SprinterCovoxBlaster` | the Covox / Covox-Blaster (S6, 2026-10-02): `CovoxBlasterState`, v1, 545 bytes | |
| 33-34 | `SprinterIsa`, `SprinterPads` | **reserved** for S6b (ISA I/O window) and the extended pads: no serializer yet; the device that lands declares its id and adds a blob, the other blobs keep their layout | |

The CPU registers are `TTDCpuState`: the Z84C15 engine executes on the `Z80State` register file
(zero copy), and the library's boundary state is mirrored into `Z80State::boundary`; a restore makes
the engine push it back (`Z84C15Engine::InvalidateBoundary`). Blobs are restored in ascending id
order, so `Wd1793Context` (35) completes the BetaDisk blob (1) after it.

After a load the decoder re-derives what follows from the state (`OnTtdStateLoaded`): the windows,
the turbo and its wait overlay, the accelerator as the CPU's bus agent, the step hook.

### 2.2 Memory

- 256 RAM pages: `ResolveModelRamPages` returns 256 for `MM_SPRINTER`; the port table (page `#40`) and
  the graphics area (`#50-#5F`) are ordinary pages, restored before execution resumes.
- Video RAM and fast RAM: whole-array blobs (TTD v2 memory regions, migration-trajectory Phase 1, are
  not on master). Measured: the video RAM blob compresses to ~7 KB at the BIOS screen; a 4 MB machine's
  recording costs ~3.6 ms per frame over a plain 21 MHz frame (~10.8 ms with the full renderer), most of
  it the 4 MB key frame every 50 frames, not the Sprinter blobs (0.17 ms per capture of all of them).

### 2.3 Inputs

Keyboard: the host key is journaled once as a ZX key (`Key`, the matrix of code `#40`) and a PC key
(`PcKey`, the PS/2 stream on SIO A). Mouse: the Kempston counters and buttons (`MouseMove` /
`MouseButtons` ...); the serial mouse on SIO B samples them when SIO B is accessed. The streams'
state (bytes on the wire, the packet in flight) and the SIO FIFOs are machine state in the
checkpoint, so a restore in the middle of a PS/2 byte or a mouse packet resumes on the same bit.
The CMOS switches to emulated time when a recording starts.

### 2.4 Snapshots

- Spectrum formats (`.sna`, `.z80`, `.szx`) cannot hold the machine: loading one on a Sprinter
  is refused with "not supported on SPRINTER"; saving likewise.
- A native snapshot = the TTD key frame (all serializers above + RAM + fast RAM + VRAM) written as a
  file. It rides on whatever PLAN #40 Phase 5 (the chunked TTD container) or the `uns-snapshots` branch
  (PLAN #52 audit) produces; no Sprinter-only format.

## 3. Automation

| Surface | Addition |
|---|---|
| Model lists | nothing to do: all surfaces read `Config::GetAvailableModels` (`core/automation/webapi/src/api/lifecycle_api.cpp:94-211`); only the MCP text above |
| Paging | `GET /state/paging` shows the 4 windows as "physical page + kind" (RAM / vROM cell / ROM / fast RAM / graphics / ISA), the cells `#C0-#FF`, CNF map, DOS, turbo — through the decoder's `GetPagingLatches` |
| New state block | `GET /api/v1/emulator/{id}/state/sprinter`: config state, map number, PORT_Y, RGMOD, HOLD, ALL_MODE, clock ratio, frame lines, accelerator mode, CBL state, IDE channel. CLI `sprinter`, Lua `emu:sprinter()`, Python `emulator.sprinter()`, MCP `inspect_state` aspect `sprinter` |
| Port table | `GET /state/sprinter/ports?map=0&dos=1&rw=r` → the decoded table (address pattern → code + name); `GET /state/sprinter/ports/lookup?port=21BC&rw=w` → one lookup with the index |
| Port trace | every traced access carries `code` and the code's name (D10): the field, the code table in every export and the `code` filter are built (PLAN #60(g)); `PortDecoder_Sprinter` sets `PortDecodeDisposition::internalCode` from its port table and returns the code names from `GetPortTraceCodeTable()` |
| Media | the `media` verbs of PLAN #58 (`fdd.*`, `ide*.*`); until M4, the existing `disk` verbs for `fdd.*` |
| Per-model switches | extend: `state_memory_api.cpp:175`, `:355-372` (ROM page counts: 16), `cli-processor-state.cpp:338`, `:365`, `:471-474`, `:512`, `state_screen_api.cpp:132` (mode name `sprinter`), `ports_api.cpp:106`, `lua_emulator.h:3037`, `:3197`, `python_emulator.h:3097`, `:3242` |
| Recipe | `.recipe/machines/sprinter.md`: create, boot DSS from a floppy image, from a folder; switch to Spectrum mode; read the port table |

**Status (2026-10-02, branch `sprinter-automation`): implemented**, on all five surfaces from one
core source (`DeviceState::Sprinter` / `SprinterPaging` / `SprinterPortTable` / `SprinterPortLookup` /
`SprinterText` in `ports/models/sprinter/sprinterdevicestate.cpp`); as built and verified:
[automation-outcome.md](automation-outcome.md). Deviations from the table above:

- **Names.** Lua / Python use the existing style: `sprinter_state()`, `sprinter_ports{...}`,
  `sprinter_port(port, ...)`, `sprinter_text()` (not `emu:sprinter()` / `emulator.sprinter()`); CLI
  `state sprinter [ports | port <hex> | text]`; MCP aspects `sprinter`, `sprinter_ports`, `sprinter_text`.
- **Added:** `GET /state/sprinter/text` (the screen text of the mode table's text squares: the ZX OCR
  cannot read BIOS / DSS screens), the BIOS images in the state block (`bios`: which image runs,
  which are shipped, how to pick one), MCP resource `unreal://machine/sprinter`, `UNREAL_MCP_PORT`,
  disk uploads up to 4 MB (1.44 MB floppies).
- **Not in the state block:** the accelerator mode beyond the `SCALE` cell (phase S5); the IDE
  channel is a placeholder that S3b fills.
- **Paging:** `/state/paging` keeps its shape; on the Sprinter `banks[].type` is the window kind and
  `banks[].page` the physical page, with the whole view under `sprinter`; the generic latch list is
  empty (`#7FFD` / `#1FFD` live in the PLD). `GET /ports` lists BIOS 3.04's map-0 rows and the fixed
  decodes (no live latch binding) plus `live.sprinter_port_table`.
- **Per-model switches:** the file:line hints above are from 2026-09-28; the switches went into
  `state_memory_api.cpp` (`getStateMemory`, `getStateMemoryRAM`, ROM pages 16, `getStatePaging`),
  `state_screen_api.cpp` (`M_SPRINTER` legacy fields; the mode name `Sprinter` already came from
  `Screen`), `ports_api.cpp`, `cli-processor-state.cpp` (`state memory` / `ram` / `rom`), the Lua and
  Python `paging_state` / `ports_map`, `ROM::GetROMPageRole` and `PortDecoder::getPortMapEntries`.
- **Media:** the existing `disk` / `media` verbs serve the floppies; IDE slots come with S3b.

**Audit round (2026-10-02, branch `sprinter-automation`)**, as built
([automation-audit-2026-10-02.md](automation-audit-2026-10-02.md) §4 status column): the state block gained
`accelerator`, `clock.waits`, `z84c15.wait_generator` / `daisy_chain`, `sound.ay.stereo` from the AY config and a
BIOS block naming the loaded image by CRC-32; new reports `SprinterVideo` (`/state/sprinter/video`, the mode table
per square), `SprinterPalette` (`/palette`), `SprinterSoundRing` (`/sound/ring`), `SprinterBios` /
`SprinterBiosSelect` (`/state/sprinter/bios`, `POST /sprinter/bios`, the create option `"sprinter": {...}`;
`sprinterbios.{h,cpp}`); the video RAM is the device memory region `vram` (`emulator/memory/devicememory.h`,
`PortDecoder::CollectMemoryRegions`, `sprintervramregion.h`). Generic pieces it brought: the video change log
(`/video/changes`, `VideoLatches` family block, `Screen::CaptureFamilyLatches`, `NoteVideoTableWrite`), the
screen digest in the core with `Screen::DigestSurface`, `Screen::IndexedFrame` + `/capture/framebuffer`, the
per-device mixer (`audiomixer.h`) and the per-source audio capture, `DeviceState::AudioChannels`, the per-mode
screen notes in `DeviceState::ScreenMode`, the GUI status-bar mode label.

## 4. Debugger (Qt)

| View | Content |
|---|---|
| Memory | windows labeled with physical page and kind; "go to physical page" for all 256 pages + 4 fast RAM pages + 16 ROM pages |
| Port table (new dock) | a 512-row grid per map (A15 A14 A6 A5 A13 A7 A2 A1 A0) × {R, W} × {DOS, no DOS} × {PN5} showing codes and names; highlights the entry used by the last access; edits write page `#40` |
| PLD registers (new dock) | cells `#C0-#FF` with names, CNF bits, ALL_MODE bits, clock |
| Video | VRAM image (1 024 × 256), mode-table grid (56 × 40, per page), palettes (8 × 256), the square under the mouse in the screen view |
| Breakpoints | "port access by code" (e.g. break on any code `#27` write = IDE command) in addition to address breakpoints |

## 5. Documentation

When the machine lands: `docs/hardware/sprinter-sp2000.md` (the hardware reference, next to
`docs/hardware/profi-1024.md`), `.recipe/machines/sprinter.md` (next to `profi.md`, `atm.md`), the
model in `AGENTS.md`'s creatable list, a `data/rom/README-ROMS.md` entry, and this folder gets its
`DONE.md`.
