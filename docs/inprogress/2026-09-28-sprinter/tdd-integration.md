# TDD — model registration, TTD, snapshots, automation, GUI

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): full start is the user default, `ide0.slave` empty, configuration module in the TTD blob |
| **Index** | [technical-design.md](technical-design.md) |
| **Mapping** | [unreal-ng-mapping.md](unreal-ng-mapping.md) |

## 1. Model registration

| Step | Where | Change |
|---|---|---|
| Enum | `core/src/emulator/platform.h:305-325` (`MEM_MODEL`) | append `MM_SPRINTER` before `N_MM_MODELS` (no renumbering of existing values) |
| Model table | `core/src/emulator/config.h:47-68` (`Config::mem_model[]`) | `{ "Sprinter Sp2000", "SPRINTER", MM_SPRINTER, 4096, RAM_4096 }` |
| Creatable | `PortDecoder::IsModelSupported` + `GetPortDecoderForModel` (`core/src/emulator/ports/portdecoder.cpp:56-135`) | add `MM_SPRINTER` to both (they must stay in sync) |
| Config folder | `Config::GetConfigFolderForModel` (`core/src/emulator/config.cpp:749`) | default rule (lowercased short name) → `data/configs/sprinter/unreal.ini` |
| Memory subclass | `Core` factory (`core/src/emulator/cpu/core.cpp:105-108`, the `ScorpionMemory` precedent) | `new SprinterMemory(context)` for `MM_SPRINTER` |
| Screen | `VideoController::GetScreenForMode` (`core/src/emulator/video/videocontroller.cpp:9-31`) | `ScreenSprinter` for `MM_SPRINTER` |
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

### 2.1 New peripheral ids

`PeripheralId` is append-only (`core/src/debugger/ttd/ttdserializable.h`). Taken since this table was
written: 15 `EvoSdCard`, 16 reserved for `TsConfPaging`, 17 `AtaChannel` (the IDE boards), 18 `Ds12887` (the shared clock, already built -
the Sprinter reuses it and needs no id of its own). The Sprinter ids below therefore shift up by four
when they are appended (15 -> 19 and so on); the order stays:

| Id | Name | Blob contents |
|---|---|---|
| 15 | `SprinterPld` | `SprinterPldState` (cells, CNF, ROM_RG, ALL_MODE, PORT_Y, RGMOD, HOLD, flags, config state, sink count and hashes), the active configuration module's **name** and its opaque **state blob** ([tdd-ports-memory.md](tdd-ports-memory.md) §6.1), `SprinterAccelState`, IDE channel latch + data latch |
| 16 | `SprinterVideo` | video RAM (256 KB) as a TTD memory region (PLAN #40 V1 "device RAM in the page store"); until V1 lands, a whole-array blob with a CRC short-cut |
| 17 | `Z84C15` | SIO (both channels, FIFOs, registers), CTC, PIO, system registers |
| 18 | `SprinterCbl` | Covox-Blaster ring, indices, control, tick phase |
| ~~19~~ | `Ds12887` | shared id 18, already built (PLAN #60(c)): cells + address latch + time base |

`TimeTravelManager::RegisterModelPeripherals` (`core/src/debugger/ttd/timetravelmanager.cpp:1141`,
`:1192-1213`) picks them up through `PortDecoder_Sprinter::CreateTTDSerializers()` /
`GetTTDModelStateIds()`, the Profi pattern (`portdecoder_profi.cpp:378-388`).

### 2.2 Memory

- 256 RAM pages: exactly `MAX_RAM_PAGES`; page 255 is an ordinary page since `3a6eabc6`.
  `ResolveModelRamPages` (`timetravelmanager.cpp:1064-1086`) returns 256 for `MM_SPRINTER`.
- The port table (page `#40`) and the graphics area (`#50-#5F`) are ordinary pages: journaled like
  all RAM. A write to page `#40` changes decoding at the next port access; replay reproduces it
  because the page contents are restored before execution resumes.
- Fast RAM (4 cache pages) must be journaled too (today's cache pages are unused, so check that the
  dirty tracker covers them).
- Video RAM is device memory (above).

### 2.3 Inputs

Keyboard events are journaled once (the E2b event with ZX + PC key). Mouse deltas already go
through the TTD input gateway. The CMOS in host-time mode records its time offset at the start of a
recording; replay uses it (NFR-3).

### 2.4 Snapshots

- Spectrum formats (`.sna`, `.z80`, `.szx`) cannot hold the machine: loading one on a Sprinter
  is refused with "not supported on SPRINTER"; saving likewise.
- A native snapshot = the TTD key frame (all serializers above + RAM + fast RAM + VRAM) written as a
  file. It rides on whatever PLAN #40 V5 (the chunked TTD container) or the `uns-snapshots` branch
  (PLAN #52 audit) produces; no Sprinter-only format.

## 3. Automation

| Surface | Addition |
|---|---|
| Model lists | nothing to do: all surfaces read `Config::GetAvailableModels` (`core/automation/webapi/src/api/lifecycle_api.cpp:94-211`); only the MCP text above |
| Paging | `GET /state/paging` shows the 4 windows as "physical page + kind" (RAM / vROM cell / ROM / fast RAM / graphics / ISA), the cells `#C0-#FF`, CNF map, DOS, turbo — through the decoder's `GetPagingLatches` |
| New state block | `GET /api/v1/emulator/{id}/state/sprinter`: config state, map number, PORT_Y, RGMOD, HOLD, ALL_MODE, clock ratio, frame lines, accelerator mode, CBL state, IDE channel. CLI `sprinter`, Lua `emu:sprinter()`, Python `emulator.sprinter()`, MCP `inspect_state` aspect `sprinter` |
| Port table | `GET /state/sprinter/ports?map=0&dos=1&rw=r` → the decoded table (address pattern → code + name); `GET /state/sprinter/ports/lookup?port=21BC&rw=w` → one lookup with the index |
| Port trace | every traced access carries `code` and the code's name (D10) |
| Media | the `media` verbs of PLAN #58 (`fdd.*`, `ide*.*`); until M4, the existing `disk` verbs for `fdd.*` |
| Per-model switches | extend: `state_memory_api.cpp:175`, `:355-372` (ROM page counts: 16), `cli-processor-state.cpp:338`, `:365`, `:471-474`, `:512`, `state_screen_api.cpp:132` (mode name `sprinter`), `ports_api.cpp:106`, `lua_emulator.h:3037`, `:3197`, `python_emulator.h:3097`, `:3242` |
| Recipe | `.recipe/machines/sprinter.md`: create, boot DSS from a floppy image, from a folder; switch to Spectrum mode; read the port table |

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
