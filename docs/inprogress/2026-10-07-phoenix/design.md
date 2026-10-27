# ZXM-Phoenix: technical design

**Date:** 2026-10-07 · part of [README.md](README.md) · requirements in [requirements.md](requirements.md) · evidence in
[research-reference-consensus.md](research-reference-consensus.md)

## 1. Today

| Piece | State on master |
|:--|:--|
| `MM_PHOENIX` | in `platform.h`; model table row `{"ZXM-Phoenix v1.0", "PHOENIX", MM_PHOENIX, 1024, RAM_1024 \| RAM_2048}` in `config.h` |
| ROM | `config.phoenix_rom_path` read from `[ROM] PHOENIX=`; `rom.cpp` maps the four pages (SYS 0, DOS 1, 128K 2, 48K 3) and names the file; `ROM::GetROMPageRole` has **no** Phoenix roles |
| Firmware | `data/rom/ZXM-Phoenix_bios.bin` tracked ([roms.md](roms.md)) |
| Config | every shipped `data/configs/*/unreal.ini` carries `PHOENIX=`; there is **no** `data/configs/phoenix/` folder, so `Config::IsModelCreatable` is false even before the decoder question |
| Port decoder | none: `PortDecoder::IsModelSupported` false, the factory throws `std::logic_error` |
| Screen | `Screen::DetectVideoMode` falls to `DetectModeLegacy` (the Pentagon has `DetectModePentagon`, which returns `M_PENTAGON128K` and honors the `#EFF7` video bits) |
| Timing defaults | `Config::ApplyModelTimingDefaults` has no Phoenix case for `intstart` / `intlen` / `frame` / `t_line` |
| TTD | no Phoenix blob; `#1FFD` is not in the standard chipset state |
| Snapshots | `snapshotcapture.cpp` returns `capture_unsupported`; `machinestatetransfer.cpp` `FamilyOf` returns `Other` |
| Automation / Qt | the name `PHOENIX` appears in the MCP description and `list_models` (`creatable: false`); no Qt entry |

## 2. Hardware facts

Every row below is from [research-reference-consensus.md](research-reference-consensus.md); the confidence column repeats
[requirements.md](requirements.md). No schematic was available.

### 2.1 Ports

| Port | Decode (A15..A0) | Dir | Effect |
|:--|:--|:--|:--|
| `#FE` | as the Pentagon decoder | R/W | ULA: keyboard, border, beeper, tape |
| `#7FFD` | `01xxxxxxxxxxxx01` | W | bits 2-0 RAM page bits 2-0; bit 3 screen (5 / 7); bit 4 ROM A14 (128K / 48K, or TR-DOS pair); bit 5 lock; bit 7 RAM page bit 4 |
| `#1FFD` | `00xxxxxxxxxxxx01` | W | bit 0 RAM page 0 at `#0000`; bit 1 ROM page 0 (SYS); bit 3 alternate ROM pair; bit 4 RAM page bit 3; bit 6 RAM page bit 6; bit 7 RAM page bit 5. Bits 2, 5 unused (Q12) |
| `#EFF7` | exact `#EFF7` | W (R back) | latch; bit 7 forces Beta Disk ports on. Other bits: stored only |
| `#BFFD` / `#FFFD` | as the Pentagon decoder | W / R,W | AY / YM data / register |
| `#1F` | as the Pentagon decoder | R | Kempston joystick |
| `#FADF` `#FBDF` `#FFDF` | exact | R | Kempston mouse buttons, X, Y |
| Beta Disk `#1F #3F #5F #7F #FF` | as the Pentagon decoder, only while the DOS ports are on | R/W | WD1793; `#FF` system port |
| `#00F7` | exact | R | 0 (Q6) |
| everything else | | R | floating bus |

### 2.2 RAM

| Window | Content |
|:--|:--|
| `#0000-#3FFF` | ROM (section 2.3) or RAM page 0 |
| `#4000-#7FFF` | RAM page 5 (fixed) |
| `#8000-#BFFF` | RAM page 2 (fixed) |
| `#C000-#FFFF` | RAM page `P` below |

`P = (7FFD & 7) | ((1FFD & 0x10) >> 1) | ((7FFD & 0x80) >> 3) | ((1FFD & 0x80) >> 2) | (1FFD & 0x40)`, then `P &= pages - 1`
where `pages` = 64 (1024K) or 128 (2048K). The screen is page 5, or page 7 when `7FFD` bit 3 is set, whatever `P` is.

Pages 0-7 are a Spectrum 128K, so `#1FFD` = 0 and `#7FFD` bit 7 = 0 give plain 128K behavior (the 128K compatibility mode that
software relies on). The page-number formula is one function in one place: if a schematic changes bits 3 and 4 (Q2) the fix is
one line plus the test table.

### 2.3 ROM at `#0000`

Four 16K pages: 0 SYS, 1 TR-DOS, 2 128K editor, 3 48 BASIC. `dosActive` is the TR-DOS session flag (`CF_TRDOS`) that the
emulator already raises when 48 BASIC reaches `#3Dxx` and clears when the session ends.

| `#1FFD` bit 0 | bit 1 | bit 3 | `dosActive` | `#7FFD` bit 4 | `#0000` |
|:--|:--|:--|:--|:--|:--|
| 1 | x | x | x | x | RAM page 0 (writable) |
| 0 | 1 | x | x | x | ROM 0 (SYS) |
| 0 | 0 | 1 | yes | x | ROM 3 |
| 0 | 0 | 1 | no | 1 | ROM 1 |
| 0 | 0 | 1 | no | 0 | ROM 0 |
| 0 | 0 | 0 | yes | x | ROM 1 |
| 0 | 0 | 0 | no | 1 | ROM 3 |
| 0 | 0 | 0 | no | 0 | ROM 2 |

The `dosActive` / bit 4 = 0 / bit 3 = 0 row is Xpeccy's and Xpeccy+'s. The shared `Memory::UpdateZ80Banks` (and Unreal) pick ROM 1
only with bit 4 = 1 and ROM 0 with bit 4 = 0 (Q9). The table follows Xpeccy because the only image has an erased ROM 0.

### 2.4 Video and timing

3.5 MHz, 224 T per line, 320 lines, 71680 T per frame, no contention, no wait states; INT position and length as the unreal-ng
Pentagon (`intstart` 71635, `intlen` 32). The renderer is the standard 256x192 Spectrum screen from page 5 / 7
(`M_PENTAGON128K`, no `#EFF7` modes, see Q4). Xpeccy+ is the only source with a number for the frame; it also has the
Pentagon's. The Pentagon `#EFF7` video modes are **not** enabled (no source implements them for the Phoenix). [Q7]

### 2.5 Devices

Beta Disk and the floppy drives, the AY / YM (and the TurboSound / TSFM option), Covox `#FB` as on the Pentagon, General
Sound, the Kempston joystick and mouse, the tape, the IDE through the global `[HDD] Scheme` (the storage survey lists no IDE of
its own for the Phoenix). All of it is the Pentagon-128 decoder's code. The Gluk RTC is optional (Q5).

### 2.6 Bus

NemoBus v1.1m (the board has the connector, some signals missing, per Black_Cat's guide quoted in
[the bus-slots research](../2026-10-03-zx-bus-slots/research-machines.md)). The slot planner has an entry list of machines;
see section 8.

## 3. How it fits unreal-ng

| Area | Change | File |
|:--|:--|:--|
| Factory | `case MM_PHOENIX:` -> `new PortDecoderPhoenix(context)`; `IsModelSupported` adds it | `core/src/emulator/ports/portdecoder.cpp` |
| Decoder | new class `PortDecoderPhoenix` | `core/src/emulator/ports/models/portdecoderphoenix.{h,cpp}` |
| Port map / trace | model name "Phoenix"; rows `#7FFD`, `#1FFD`, `#EFF7` added to the Pentagon-128 rows | `portdecoder.cpp` `GetPortMapInfo` |
| ROM roles | `PHOENIX_ROLES` | `core/src/emulator/memory/rom.cpp` |
| Memory | `MM_PHOENIX` joins the decoder-owned list in `Memory::UpdateZ80Banks`; the decoder maps all four windows | `core/src/emulator/memory/memory.cpp` |
| Config | folder `phoenix` (derived from the short name, no code); timing defaults in `Config::ApplyModelTimingDefaults` for `MM_PHOENIX` (the Pentagon's: `intstart` 71635, `intlen` 32, `frame` 71680, `t_line` 224); `[ROM] PHOENIX` already read | `core/src/emulator/config.cpp` |
| Config data | new `data/configs/phoenix/unreal.ini`, a copy of `pentagon512k` with `HIMEM=PHOENIX`, `RAMSize=1024`, `ROM` -> the Phoenix image, `RESET=128` | `data/configs/phoenix/` |
| Screen | `MM_PHOENIX` -> `DetectModePentagon` with `#EFF7` video bits ignored (a Phoenix variant of the function that keeps the overscan toggle and returns `M_PENTAGON128K`, the mode whose raster has the Pentagon timing) | `core/src/emulator/video/screen.cpp` |
| TTD | `PeripheralId::PhoenixPaging = 62`; blob `TTDPhoenixPaging`; `GetTTDModelStateIds` / `CreateTTDSerializers` in the decoder | `core/src/debugger/ttd/` |
| Snapshot capture | decoder installs `snapshot::WindowMapCapture::Instance()`; the `snapshotcapture.cpp` switch gets a Phoenix `capture_unsupported` reason that says "extended paging in use" | `core/src/loaders/snapshot/snapshotcapture.cpp` |
| State transfer | `PagingFamily::Phoenix` (new) | `core/src/loaders/snapshot/machinestatetransfer.cpp` |
| Slots | machine entry in the reference data | `core/src/emulator/slots/refdata/machines.cpp` |
| Automation | model lists in the CLI (`cli-processor-state.cpp`, `cli-processor-instance.cpp`), WebAPI (`state_memory_api.cpp`, `openapi_lifecycle.inc`, `openapi_schemas.inc`), MCP description and resource, Lua / Python headers | `core/automation/...` |
| Qt | `supportedModels`: "ZXM-Phoenix (1024K)" | `unreal-qt/src/menumanager.cpp` |
| Docs | `.recipe/machines/phoenix.md`, `.recipe/README.md`, `.recipe/_common/machines.md`, `AGENTS.md` models list, `data/rom/README-ROMS.md` already lists the ROM | |

No new third-party library: the Phoenix has no odd chip. (If an RTC is chosen the `Ds12887` device already exists.)

## 4. The decoder

`PortDecoderPhoenix` derives from `PortDecoder_Pentagon128` (not from the 512 / 1024 classes): it inherits the Beta Disk,
AY, Covox, Kempston joystick and mouse, `#FE`, tape and floating bus unchanged, and replaces only the paging. The Pentagon-1024
class is the wrong base because its `#7FFD` bit 5 is a page bit under `#EFF7` and its `#EFF7` carries video modes.

Existing decoders keep their `PortDecoder_X` names; the new class follows the repository rule (no underscore) and sits next
to them, like `ProfiBoard`.

```text
DecodePortOut(port, value, pc):
    claimed-port override (cards)            // as Pentagon1024: a full-decode card owns the cycle first
    if IsPort1FFD(port):  Out1FFD(value)     // A15=0, A14=0, A1=0, A0=1
    elif IsPort7FFD(port): Out7FFD(value)    // A15=0, A14=1, A1=0, A0=1
    elif port == 0xEFF7:  OutEFF7(value)
    else: Pentagon128::DecodePortOut(...)
```

- The Pentagon-128 `IsPort_7FFD` (A15, A2, A1) is looser than Phoenix's; the Phoenix checks run first, so a write to `#3FFD`-like
  addresses reaches `#1FFD` as on the board.
- `Out7FFD`: ignored when the lock bit is set (the screen bit included, as Pentagon-128 and all sources); otherwise
  `state.p7FFD = value`, remap, screen switch when bit 3 changed.
- `Out1FFD`: `state.p1FFD = value`, remap. Never locked.
- `OutEFF7`: `state.pEFF7 = value`; bit 7 -> the DOS ports gate (the same `CF_DOSPORTS` flag the Pentagon uses); no remap, no
  `InitRaster`.
- `UpdateModelMemoryBanks()` (called after a TTD restore, a snapshot restore and every TR-DOS flag change) recomputes the four
  windows from `p7FFD`, `p1FFD`, `CF_TRDOS`. This is the single mapping function; the port handlers call it.
- `reset()`: `p7FFD`, `p1FFD`, `pEFF7` = 0, ROM page 2.
- `EnterSpectrum128Paging()` (state transfer): `p1FFD` = 0, then the 128K `#7FFD` write.
- `GetTTDModelStateIds()` returns `{PhoenixPaging}`.

The decode functions are `static constexpr` bit masks next to each other, so the port-map rows and the tests read the same constants.

## 5. Memory and ROM

`Memory::UpdateZ80Banks` already has the branch for decoders that own their memory manager (ATM, +3). `MM_PHOENIX` is added to that
list. The decoder then sets: window 1 = RAM 5, window 2 = RAM 2, window 3 = RAM `P`, window 0 by the section 2.3 table with
`Memory::SetROMPage(n, false)` or `SetRAMPageToBank0`. The tail of `UpdateZ80Banks` (the TR-DOS session flags) is shared.

RAM size 1024 / 2048: `Memory` allocates it from `config.ramsize`; `P` is masked as in section 2.2. The pages above 7 of a
1024K machine are reachable only through the five bits, so the snapshot capture has nothing to read until they are used.

DOS session flags: `CF_LEAVEDOSRAM` (Unreal's choice for the Phoenix; the Pentagon uses `CF_LEAVEDOSADR`). `CF_LEAVEDOSRAM` is the flag that lets the session end when code runs from RAM, which matters once `#1FFD` bit 0 puts RAM under `#0000`. No source says what the real TR-DOS trap does there. [Q9]

## 6. TTD

TTD's chipset state holds `p7FFD` and `pEFF7`; it does not hold `p1FFD`. A latch nobody captures is lost on restore, so
Phoenix gets a one-byte blob, as `Plus3Paging` has for `p1FFD`:

| Item | Value |
|:--|:--|
| `PeripheralId` | `PhoenixPaging = 62` (next free after `EvoFlash = 61`; ids are never renumbered) |
| Blob | `struct PhoenixPagingState { uint8_t version; uint8_t p1FFD; uint8_t reserved[2]; }`, 4 bytes, like `Plus3PagingState` |
| Class | `TTDPhoenixPaging` in `core/src/debugger/ttd/phoenix/ttdphoenixpaging.{h,cpp}` |
| Restore | writes `state.p1FFD`, then `UpdateModelMemoryBanks()` |
| Name | `ttdfileinfo.cpp`: "phoenix-paging"; `ttd.ksy`: new entry in the id list and the blob layout |
| Fingerprint | `RAMSize` is part of the model fingerprint; a 1024K checkpoint refuses to load into a 2048K machine |
| Input events | none new (the paging ports are CPU writes) |
| Port-read journal | allowed: no interrupt source with a vector on the bus, no stepped engine |

Every phase's verification starts with a TTD recording ([section 9](#9-verification-protocol-for-every-phase)).

## 7. Snapshots and state transfer

| Pipeline piece | Behavior |
|:--|:--|
| Capture policy | `WindowMapCapture`: the view exists while the live window map is a Spectrum 128K (RAM page n = bank n). A Phoenix with `#1FFD` != 0 or a page above 7 in `#C000` gives `capture_unsupported` with "extended Phoenix paging in use" |
| Formats | `.sna` / `.z80` / `.szx` have no Phoenix machine id (no common emulator has one); a Phoenix saves a plain-128K state in them, and loading any of them runs on a 128K layout (`EnterSpectrum128Paging`) |
| `MachineStateTransfer` | new `PagingFamily::Phoenix` ("Phoenix"): a source analysis `Mode128` when `p1FFD` = 0 and no page above 7 is used and `p7FFD` bit 7 = 0, else `SameFamily`; it joins the "128-class" targets; extended state only to a Phoenix of at least the source's RAM size |
| ROM roles | Phoenix 128K editor / 48 BASIC map to `RomRole::Editor` / `Basic48`; ROM 0 (SYS) is `Specific` |
| `.zxp`, `.spg` | unchanged refusals |

## 8. Media and slots

- Floppy: Beta Disk with four drives as on the Pentagon; the media slots (`media` tool, `load_software`) work through the existing
  Beta Disk wiring, no new media kind.
- IDE: `IdeController::SchemeFits` already allows NEMO, NEMO-A8, NEMO-DIVIDE and DIVIDE on any non-Profi model, so the global
  `[HDD] Scheme` works with no Phoenix code; the Phoenix-specific test is the 65 536-port collision sweep for the new decoder.
- Slots (NemoBus v1.1m): a `machines.cpp` refdata row so the planner knows the model has a bus connector. Cards (MoonSound,
  MultiSound, GS) are not populated until the planner lists them for the model; the bus is not emulated beyond that. [Q13]
- TR-DOS 6.11 for Phoenix: not available locally (Q10).

## 9. Verification protocol for every phase

1. Build with `tools/build/build.sh` (zero warnings), tests with `tools/build/test.sh --gtest_filter='*Phoenix*'` then the full suite.
2. Create the machine through the surface under test (CLI, WebAPI, MCP, Lua, Python, Qt) with `model: PHOENIX`.
3. **Start a TTD recording before running anything** (`time_travel {"action":"start"}` / `POST /emulator/{id}/ttd/start` / CLI `ttd start`), run the recipe steps, stop.
   Then `seek` to the start and step forward: the state hash at the end equals the recorded one. A failure here is a TTD
   blob bug and blocks the phase.
4. Run the phase's recipe section and tick it in `.recipe/machines/phoenix.md` ("Verified: date, build").

## 10. Phases

Each phase ends with the full build, `core-tests` green, the Linux gcc check for C++ changes, and the parity table below
filled in for what the phase adds. Parity columns: CLI / WebAPI + OpenAPI / MCP / Lua / Python / Qt / recipe.

| Phase | Work | Parity delivered | Needs |
|:--|:--|:--|:--|
| 1 | **Creatable machine.** Factory + `IsModelSupported`; `PortDecoderPhoenix` with `#7FFD` / `#1FFD` / `#EFF7`, ROM table, RAM 1024 / 2048; `data/configs/phoenix/`; roles; timing defaults; screen mode; port map name. Boot to the 128K menu, 48 BASIC, TR-DOS | CLI `create`/`list_models`; WebAPI `start` + `models` + OpenAPI enum text; MCP `emulator_manage` description; Lua / Python model-name list; Qt Machine menu; **recipe `phoenix.md` (create, paging, ROM)** | none |
| 2 | **TTD and state.** `PhoenixPaging` blob (id 62), ksy, file info, fixture; `PagingFamily::Phoenix`; `WindowMapCapture`; capture reason text | `state/paging` fields (`p1FFD`, 7-bit page, ROM page + role) on WebAPI / CLI / Lua / Python; MCP `inspect_state`; TTD checkpoints from every surface; recipe adds TTD and transfer sections | 1 |
| 3 | **Devices and details.** `#EFF7` bit 7 forcing the DOS ports; `#00F7`; optional RTC (`CMOS=`); `IN` floating bus; mouse and joystick tests; TR-DOS flow (`CAT`, boot a disk) | the same surfaces; port map shows the extra rows; recipe adds the device sections (TR-DOS, mouse, joystick) | 1; Q5, Q6 |
| 4 | **Slots and bus.** Refdata row; planner test; MoonSound / MultiSound examples if the planner lists them | WebAPI `slots` for the model; MCP; recipe `slots.md` row | 1; Q13 |
| 5 | **Evidence-driven corrections.** Anything a schematic, a manual or a real board settles: page bit order (Q2), A2 decode (Q3), SYS page (Q1), `#EFF7` bits (Q4), turbo (Q8). Each is a one-line table change plus its test | none new | sources |

Phases 1-3 have no blocking open question and can start now. Each phase maps its tests to [tdd-plan.md](tdd-plan.md) and writes
tests first.

## 11. Cost

Nothing on a shared hot path: the decoder is a new class, `Memory::UpdateZ80Banks` gets one more model in an existing `||`
chain evaluated once per paging write, and the only shared `switch` additions are cold (creation, port map, capture). No A/B
benchmark is needed; the phase 1 review confirms it with a diff of `memory.cpp`.
