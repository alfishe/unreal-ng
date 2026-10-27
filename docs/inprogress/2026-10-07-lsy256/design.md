# LSY256: technical design

**Date:** 2026-10-07 · part of [README.md](README.md) · requirements: [requirements.md](requirements.md) · tests: [tdd-plan.md](tdd-plan.md)

Rules followed: simple design, no pattern for its own sake, naive first and measure later, the odd chip in its own place.
Here the odd part is one 8-bit latch, so it needs no library of its own: it is a small state struct plus a decoder class.

## 1. Today

| Item | State on master |
|:--|:--|
| `MM_LSY256` | in `platform.h` (position 15 of the enum; do not renumber) |
| Model row | `config.h`: `"Orel' BK-08 (LSY)", "LSY256", MM_LSY256, 256, RAM_256` |
| ROM | `rom.cpp` maps the four roles; `config.cpp` reads `[ROM] LSY=`; `data/rom/lsy256.rom` tracked |
| Decoder | none: `PortDecoder::IsModelSupported` is false, the factory throws |
| Config folder | none: `data/configs/lsy256/unreal.ini` missing, so `IsModelCreatable` is false |
| Latch field | `pLSY256` was removed from `EmulatorState` as a dead field by the model-state refactor ([2026-10-07-model-state](../2026-10-07-model-state/goals-and-requirements.md)) |
| Tests | `emulatormanager_test.cpp` asserts "unsupported" for `MM_LSY256` |
| Text | `snapshotcapture.cpp` and `mcp-tools.cpp` name LSY256 as not creatable |

## 2. Hardware facts (single source: Unreal NedoPC, see [research-reference-consensus.md](research-reference-consensus.md))

### 2.1 Memory map

| Window | Content |
|:--|:--|
| `#0000-#3FFF` | by `#7B` bits EMUL (3) and BLKROM (1), table 2.2 |
| `#4000-#7FFF` | RAM 5 |
| `#8000-#BFFF` | RAM 2 |
| `#C000-#FFFF` | RAM `(#7FFD & 7) + 8 * PA3`, PA3 = `#7B` bit 4 (16 pages = 256K) |

### 2.2 Port `#7B` (write, low address byte `#7B`, any high byte)

| Bit | Name | Meaning |
|:--|:--|:--|
| 0 | DV0 | with BLKROM: selects RAM page 13 (1) or 12 (0) at `#0000` |
| 1 | BLKROM | `#0000` is RAM instead of ROM |
| 2 | - | unused in Unreal; **O** |
| 3 | EMUL | "page mode at `#0000`": ROM as the Spectrum 128 pages it |
| 4 | PA3 | page bit 3 at `#C000` |
| 5-7 | - | unused in Unreal; **O** |

| EMUL | BLKROM | `#0000-#3FFF` |
|:--|:--|:--|
| 0 | 0 | SYS ROM (ROM page 2, the "LSY-Setup") |
| 1 | 0 | the normal ROM choice: no DOS session: `#7FFD` bit 4 = 0 128 ROM, = 1 48 ROM; DOS session: bit 4 = 1 TR-DOS ROM, = 0 SYS ROM (Unreal's generic rule) |
| 0 | 1 | RAM page 13 (DV0 = 1) or 12 (DV0 = 0), read/write |
| 1 | 1 | RAM page 8 / 9 (no DOS: bit 4 = 0 / 1) or 10 / 11 (DOS), read-only |

So RAM pages 8..11 are a RAM mirror of the four ROM pages in the order 128, 48, SYS, TR-DOS, and 12/13 are free RAM. The
design assumes the software fills 8..11; the emulator does not (open Q2).

### 2.3 Other ports and devices

- `#7FFD`: generic 128K decode (A15 = 0, A1 = 0). Bits 0-2 page, 3 screen, 4 ROM, 5 lock.
- `#FE`: keyboard read, EAR, beeper, border. The BK-08 keyboard is read here with six keys on D0-D5 and a seventh per half-row on D7
  (R11); D6 is the EAR input.
- Unreal gives it the common sound setup (AY, Covox). Nothing LSY-specific is known (Q6).
- TR-DOS: the Beta 128 ports behave as on the Pentagon in the ROM-at-`#0000` modes. In RAM modes Unreal arms no `#3Dxx` entry (Q3).
- Reset forces the SYS ROM (`#7B` = 0 does that by itself).

### 2.4 Video and timing

Standard Spectrum 256x192 with screens 5 and 7. No model-specific timing is known; Unreal's INI default is 224 T x 320 lines =
71680 T. The design uses the Pentagon timing and keeps the choice in the config (Q5).

## 3. ROM

See [roms.md](roms.md). Nothing to add to `rom.cpp`. Phase 1 adds the tagged-ROM signatures for the debugger's page names.

## 4. Fit in unreal-ng

### 4.1 State

`core/src/emulator/platforms/lsy/lsystate.h`:

```cpp
struct LsyState
{
    uint8_t p7B = 0;   // the #7B latch; the emulator reset zeroes it with EmulatorState{}
};
```

Member `EmulatorState::lsy`. Plain values, as `ScorpionState`.

### 4.2 Port decoder

`core/src/emulator/ports/models/portdecoder_lsy256.{h,cpp}`: `class PortDecoder_LSY256 : public PortDecoder_Pentagon128`.
The Pentagon 128 decoder already does `#FE`, `#7FFD` with the lock, AY, joystick, Beta 128 and port tracing. LSY adds:

- `DecodePortOut`: if `(port & 0xFF) == 0x7B`, store the latch and call `ApplyLsyBanks()`; then continue to the base decode (Unreal does not stop there).
- `switchRAMPage(value)` override (the base has a `virtual`): window 3 = `(value & 7) + 8 * PA3`.
- `ApplyLsyBanks()`: window 0 by table 2.2: ROM pages through the existing `base_*_rom` pointers via
  `Memory::SetROMPageToBank`; RAM through `SetRAMPageToBank0`; mode (1,1) adds `Memory::SetBankWriteProtected(0)`.
- `UpdateModelMemoryBanks()` override so a rebuild after a restore (TTD, snapshot) lands in the right map; `reset()` zeroes the latch.
- `getPortTraceDecodeRules()` appends one row for `#7B`.

Factory: `portdecoder.cpp` gets `case MM_LSY256: result = new PortDecoder_LSY256(context);` in `GetPortDecoderForModel` and
`case MM_LSY256:` in `IsModelSupported`, together.

Keyboard: see section 6.

### 4.3 Config

`data/configs/lsy256/unreal.ini`: a copy of the Pentagon 128K INI with `HIMEM=LSY`, `RAMSize=256`, `LSY=rom\lsy256.rom`
and the Pentagon timing. The copy is a file, not code. `Config::GetConfigFolderForModel` already returns `lsy256`.

### 4.4 Screen

`Screen::DetectMode` falls to `DetectModeLegacy` for this model. It needs the `M_PENTAGON128K` timing set as the Pentagon-class
default: one `case MM_LSY256: return DetectModePentagon(state);` line, no new video mode.

## 5. Snapshots

### 5.1 MachineStateTransfer: `PagingFamily`

New family `Lsy256`, with name "LSY256" in `FamilyName`, and `FamilyOf(config)` maps `MM_LSY256` to it.

- As a **source**: if `#7B` is a 128-compatible state (EMUL = 1, BLKROM = 0, PA3 = 0) and no page above 7 is used, the machine is a Spectrum 128K
  (`Need::Mode128`). Otherwise `Need::SameModel`, reason "the source runs the LSY256 pager (window 0 mode, pages 8-15)".
- As a **target**: counted with the 128-class targets (like the ATM and Profi boards). Entering the 128 layout means writing `#7B` = `#08`
  (EMUL) then `#7FFD`, through an `EnterSpectrum128Paging` override.
- Loading a state that uses pages above 7 from another model (Pentagon 512K) is refused as "needs Pentagon".

### 5.2 Pipeline policy and capture

- Policy: no `ISnapshotCommitPolicy` of its own. The legacy commit works once the target is in the 128 layout. A named policy is added only if an
  LSY-native snapshot format is ever wanted (none exists).
- Capture (`snapshotcapture.cpp` `DefaultView`): `case MM_LSY256:` with machine hint `"128k"`, timing hint `"128k"` (or `"pentagon"` once Q5 is
  settled), 16 banks, and a refusal when window 0 is RAM or the SYS ROM is shown: `Unavailable("...", "mode:lsy")`. The error text that lists LSY256 as
  not creatable is changed.
- Z80 / SNA / SZX: no machine id exists for it. A save writes the 128K view; a load of an SZX file for another model is refused by the model check.

## 6. Keyboard

The BK-08 matrix has 8 half-rows, six keys on D0-D5 and a layer key on D7 (table in Unreal `vars.cpp`; rows: ALT Z X C V RUS / SHF; A S D F G BSL / SL;
Q W E R T CMA / PNT; 1-5 TIL / TAB; 0-6 MNS / PLS; P O I U Y LB / RB; ENT L K J H COL / QUO; SPC CTL M N B R/A / CPS). The matrix in
`EmulatorState::matrix` is a byte per half-row, so it already holds D5 and D7. Work:

1. A key-descriptor table for the BK-08 (`core/src/emulator/io/keyboard/`), chosen by the model, replacing the Spectrum 40-key table.
2. `#FE` read: bit 5 and bit 7 come from the matrix on this model (Pentagon 128 returns 1 there; D6 stays EAR).
3. Host keys: the host-keyboard route maps PC keys to BK-08 positions (Russian layout tables are the ROM's: page 1).
4. TTD already saves the matrix.

The mapping from ZX key names (used by `type_input`) to BK-08 keys is a table that is the one thing needing the ROM: phase 3 reads the key
tables in page 1 to fill it (Q4).

## 7. TTD

Blob `LsyPaging`: `PeripheralId` next free (62 on master; check `ttdserializable.h` on the day), also a `TTDDeviceType` entry with the same number and a name in
`ttdfileinfo.cpp` (`"lsy-paging"`). Layout: `struct LsyPagingState { uint8_t p7B; uint8_t reserved[3]; }` (4 bytes, explicit filler, as `Plus3PagingState`).
Files: `core/src/debugger/ttd/lsy/ttdlsypaging.{h,cpp}`. `PortDecoder_LSY256::GetTTDModelStateIds()` returns it and `CreateTTDSerializers()` builds it; `ttd.ksy` gets the
layout. `TTDLoadState` writes the latch then asks the decoder to rebuild the banks. `#7FFD` is in the chipset state already. No fixtures exist, so none change.

## 8. Media and slots

- Media: Beta 128 with four drives, TAP/TZX, as the Pentagon. Nothing LSY-specific; the disk controller comes from the Pentagon decoder.
- Slots: one row in `slots/refdata/machines.cpp`: `{ .model = MM_LSY256, .name = "LSY256", .variant = "Orel BK-08 (LSY)", ... }`; buses and built-ins as the
  Pentagon row until a source says the board has a ZX bus (Q7).

## 9. Automation and Qt (parity on every surface)

| Surface | Work |
|:--|:--|
| CLI | `list models` shows it creatable (automatic from `IsModelCreatable`); `paging` prints the latch and window-0 mode |
| WebAPI + OpenAPI | `POST /emulator/start {"model":"LSY256"}`; `/state/paging` gets `lsy_p7b` and `lsy_window0` (`sys-rom`, `rom`, `ram-rw`, `ram-ro`); the model enum in `openapi_lifecycle.inc`; the schema in `openapi_schemas.inc` |
| MCP | model list text in `mcp-tools.cpp` loses "cannot be created"; the paging state carries the same two fields |
| Lua, Python | `get_paging()` carries the same fields (the binding headers list models) |
| Qt | `MM_LSY256` added to the `supportedModels` set in `menumanager.cpp` |
| Recipe | `.recipe/machines/lsy256.md`, verified live (MCP first) |

## 10. Phased plan

Every phase: tests first ([tdd-plan.md](tdd-plan.md)), `tools/build/build.sh` with zero warnings, `tools/build/test.sh`, the gcc check
(`docker/linux/build.sh --test`), and a **TTD recording started before every live run** of the machine in that phase (so a surprise is replayable).
Each phase ends with its recipe step verified on the surfaces it touched. No commit without the owner's word.

| Phase | Work | Automation parity in the phase | Recipe / TTD |
|:--|:--|:--|:--|
| 1 | `LsyState`, `PortDecoder_LSY256`, factory + `IsModelSupported`, `data/configs/lsy256`, screen case, tagged-ROM signatures; the SYS ROM boots | CLI create/list, WebAPI create + OpenAPI model enum, MCP create, Lua and Python model names, Qt menu entry | `.recipe/machines/lsy256.md` v1 (create, ports, ROM roles); record TTD while the ROM boots, read the `#7B` values it writes (settles Q2, Q3) |
| 2 | Window-0 modes, window 3 page bit, read-only RAM, NMI; the boot tests | paging state fields on all five surfaces (CLI, WebAPI + OpenAPI, MCP, Lua, Python); Qt debugger window map | recipe v2 (the mode table exercised); TTD recording of a `#7B` sweep |
| 3 | BK-08 keyboard: descriptor table, `#FE` D5 / D7, host keys, ZX-name mapping | `type_input` / CLI `key` / WebAPI keyboard / MCP / Lua / Python key names; Qt host keyboard | recipe v3 (typing into the menu); TTD recording of typing, replayed |
| 4 | TTD blob `LsyPaging`, `PagingFamily::Lsy256`, capture view, snapshot refusals | snapshot load / save options and refusal reasons on all surfaces; TTD `inspect` shows the blob | recipe v4 (load a 128K snapshot, save one, record and replay) |
| 5 | Slots row; timing question Q5 and sound Q6 settled with whatever evidence turned up; docs | slots API on every surface; `list models` text | recipe final; a TTD fixture of the boot kept in `testdata/` |

Phases 1-2 need no answer to an open question. Phase 3 needs Q4 (the ROM tables). Phase 4 needs Q1 (page numbering) settled.

## 11. Cost (A/B)

The new decoder is a separate class with no code on other models' hot paths. Window 0 changes only on a `#7B` or `#7FFD` write. No benchmark is needed
beyond the existing suite; if `#7B` is written per frame by some program, the cost is one map rebuild, measured then (naive first).
