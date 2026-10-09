# Quorum: technical design

**Date:** 2026-10-07 · part of [README.md](README.md) · hardware evidence in [research-quorum-reference-consensus.md](research-quorum-reference-consensus.md)

Rules followed: simple code, no pattern for its own sake; an odd chip lives in its own small class; naive first, measure
later; real variants kept as real variants.

## 1. Today

`MM_QUORUM` is in the enum and the model table, `quorum_rom_path` is read from `[ROM] QUORUM=`, `rom.cpp` maps the four
ROM pages (SYS, DOS, 128, 48) and `Ports` has `_p00` and `_p80FD`. Nothing else: `PortDecoder::IsModelSupported` returns
false, so `GetPortDecoderForModel` throws; the snapshot capture says "Kay, Quorum, LSY256, Phoenix, GMX and ZX Next cannot
be created yet"; `emulatormanager_test.cpp` asserts the creation is refused.

## 2. Hardware facts

### 2.1 Ports

| Port | Decode (`mask/value`) | Dir | Function |
|:--|:--|:--|:--|
| `#00` | `0x0099/0x0000` | W | control register CMR1 (2.2) |
| `#7FFD` | `0x801A/0x0018` | W | paging CMR0 (2.3) |
| `#80FD` | `0xA01A/0x8018` | W | latched, no effect known (Q4) |
| `#FE` | `0x0099/0x0098` | R/W | border, beeper, MIC / EAR, keyboard rows |
| `#7E` | `0x0099/0x0018` | R | extra keyboard rows |
| `#80..#83` | exact low byte | R/W | WD1793 command-status / track / sector / data |
| `#85` | exact low byte | W (R) | FDC system port, translated to Beta128 `#FF` |
| `#FFFD / #BFFD` | AY standard (BC: `0xE012/0xE010` and `0xE012/0xA010`) | R/W | AY-3-8912 |
| `#1F` | Kempston joystick (BC: `0x0099/0x0019`) | R | joystick |

The decodes are partial, so several ports can match one address (consensus P14). The decoder keeps one rule list and
runs every matching rule, in the order of the table (naive: Q8).

### 2.2 `#00` (CMR1), reset 0

| Bit | Mask | Meaning |
|:--|:--|:--|
| 0 | `#01` | `F_RAM`: RAM page 0 (or 8) replaces the ROM at `#0000` |
| 2 | `#04` | `PENTAGON` timing (1024K, one source: Q3) |
| 3 | `#08` | `RAM_8`: the `#0000` RAM page is 8 (folds to 0 on 128K) |
| 4 | `#10` | `BLK_128` (1024K, one source: Q2), not built until Q2 closes |
| 5 | `#20` | `B_ROM`: 1 = normal ROM (128 / 48 / DOS), 0 = SYS ROM |
| 6 | `#40` | `BLK_WR`: 7FFD writes ignored; writes to `#0000-#3FFF` go nowhere |
| 7 | `#80` | `TR_DOS`: what the trap shows (Q1) |

### 2.3 `#7FFD` (CMR0), reset 0

| Bit | Meaning |
|:--|:--|
| 0-2 | RAM page at `#C000` (low bits) |
| 3 | screen: 0 = page 5, 1 = page 7 |
| 4 | ROM: 0 = 128 page, 1 = 48 page |
| 5-7 | 128K: ignored. 256K: bit 6 = page bit 3. 1024K: Q2 (U: bits 6, 7, 5 -> 3, 4, 5; K: bits 5, 6, 7 -> 3, 4, 5) |

### 2.4 Memory map (the whole algorithm)

```
rom_visible = !F_RAM and !trapOn           // trapOn: the TR-DOS trap, see Q1
page0000 = RAM_8 ? 8 : 0                   // masked by RAM size
if trapOn:   show DOS ROM (recommended: if TR_DOS) else RAM page0000     // Q1; U shows the DOS ROM always
elif F_RAM:  RAM page0000
elif !B_ROM: ROM SYS
else:        ROM (CMR0.4 ? 48 : 128)
read  #0000 = the above           write #0000 = BLK_WR ? nowhere : RAM page0000        // Q10
#4000 = RAM 5      #8000 = RAM 2      #C000 = RAM page(CMR0) & ramMask
```

Trap: an M1 fetch at `#3D00-#3DFF` while the 48 page is mapped sets `trapOn`; an M1 fetch at `>= #4000` clears it. NMI
acknowledge sets `#00` = 0 and clears the trap. Reset clears everything. `ramMask` = pages - 1 (7 or 63).

### 2.5 Video and timing

Standard Spectrum screen from page 5 or 7. Z's raster: 224 T line, 312 lines, 69888 T, INT 32 T, first paper line 80
plus 65 T, borders 32 lines top and bottom, 16 T left and right; no contention (V1, V3). With `PENTAGON` set (Q3) the
Pentagon 128 timing. The 69888 T is a 128K-sized frame with a Pentagon-like paper start; nobody measured it ("proof???" in Z).

### 2.6 Devices

WD1793 (shared model) with two drives; AY-3-8912 and beeper; Kempston joystick; two keyboard matrices
([research-quorum-keyboard.md](research-quorum-keyboard.md)). No IDE, RTC, DMA, covox or mouse is known.

## 3. Firmware and configuration

| Item | Value |
|:--|:--|
| Config folder | `data/configs/quorum/unreal.ini` (new; from `profi3/`, minus the Profi keys): `[ROM] QUORUM=rom\qu7v42.rom`, `RAMSize=1024`, `RESET=SYS`, `TurboSound=Single`, `[HDD] Scheme=NONE` |
| ROM roles (`rom.cpp`) | SYS / ROM-MENU, TR-DOS, 128K, 48K BASIC (a Quorum entry like `PROFI_ROLES`; names are the page roles, not image names) |
| Model name | "Quorum" / `QUORUM`; RAM 128 and 1024 (256: Q9) |

## 4. How it fits unreal-ng

### 4.1 Files

| File | Change |
|:--|:--|
| `core/src/emulator/ports/models/portdecoder_quorum.{h,cpp}` | new `PortDecoder_Quorum`: the port table above, `UpdateModelMemoryBanks`, reset, the trap hook, the FDC translation |
| `core/src/emulator/ports/portdecoder.cpp` | `IsModelSupported` and `GetPortDecoderForModel`: `case MM_QUORUM`; `GetPortMapInfo` rows and `modelName`; the other `switch (model)` sites listed in 4.9 |
| `core/src/emulator/ports/models/quorumkeyboard.{h,cpp}` | new: the `#7E` matrix (4.5) |
| `core/src/emulator/memory/rom.cpp` | role names for the Quorum (the page mapping exists) |
| `core/src/emulator/video/screen.cpp` | `DetectVideoMode`: `case MM_QUORUM`; mode `M_QUORUM` with the raster of 2.5 (or `M_ZX128` geometry with Quorum parameters if the mode table allows a parameter set) |
| `core/src/emulator/ports/ports.h` | `_p00`, `_p80FD` already declared; the decoder owns its latches, these two are kept in sync for the debugger display |
| `data/configs/quorum/`, `core/src/emulator/config.cpp` | the shipped config; the default-config lookup for `MM_QUORUM` |

### 4.2 Decoder

`PortDecoder_Quorum` derives from `PortDecoder` like `PortDecoder_Spectrum128` and registers its rules through the same
claim list. Latches `_p00`, `_p7ffd`, `_p80fd`, `_trapOn` are private fields; the memory is rebuilt by one function
(`UpdateModelMemoryBanks`) that implements the pseudo-code in 2.4. `#7FFD` is the Spectrum 128 handler with the Quorum
mask and the `BLK_WR` test. `#FE` reuses `Default_Port_FE_In/Out` behind the Quorum mask. The TR-DOS trap uses the
existing CF_TRDOS machinery; the Quorum-specific part is what `UpdateModelMemoryBanks` shows while it is on (Q1).

### 4.3 FDC

The existing WD1793 is addressed in Beta128 form (`#1F/#3F/#5F/#7F` registers, `#FF` system). The decoder maps `#80+n` to
register n, and `#85` to `#FF` with `((v & ~3) ^ 0x10) | drv`, `drv = {3,0,1,3}[v & 3]` (consensus P8). The FDC rules do
not depend on `CF_TRDOS` (Q6). FDC clock policy is the default; media goes through the normal drive slots (`TRD`, `SCL`,
`FDI`, `TD0`...). Trap-based fast loading stays on its usual switch.

### 4.4 Memory

`Memory` already holds 64 pages and `base_sys_rom / base_dos_rom / base_128_rom / base_sos_rom` for `MM_QUORUM`. The
decoder sets bank 0 to a ROM page or RAM page (page 0 or 8) and, for `BLK_WR`, a write target that discards. Whether the
core has a "write-discard" bank for a RAM-backed read is checked in phase 1 (the ROM case already writes to a trash page).

### 4.5 Keyboard

Class `QuorumKeyboard` (8 rows x 6 bits, pressed = 0), fed by the host-key path and the automation key API, read by the
`#7E` rule. NMI and reset keys call the existing NMI request and soft reset. Details: [research-quorum-keyboard.md](research-quorum-keyboard.md).

### 4.6 TTD serialization and blob ids

- `ttd::PeripheralId::QuorumPaging` takes the next free id at implementation time (61 `EvoFlash` was the last when this
  was written; two other designs of 2026-10-07 may also claim 62, so the number is decided when the code lands) and the
  same number in `TTDDeviceType`, with the `static_assert` pair that every id has.
- Blob `QuorumPagingState` (packed, 4 bytes): `p00`, `p80FD`, `trapOn`, `version`. `#7FFD`, `#FE` and the counters travel in the
  common chipset state; the comment in `ttd.ksy` already says "Quorum p00 / p80FD move to per-model serializers".
- `PortDecoder_Quorum::GetTTDModelStateIds()` returns `{QuorumPaging}` (plus the ids of any shared chip the decoder owns, as the Profi decoder does), `CreateTTDSerializers()` returns `TTDQuorumPaging` (files `core/src/debugger/ttd/quorum/ttdquorumpaging.{h,cpp}`).
  The keyboard matrix rides on the existing `KeyboardMatrix` blob if it can carry 16 rows, otherwise a second small blob
  (decided in phase 3; the matrix is rebuilt from journaled key events either way).
- Restore must end in the same banks as live execution: `TTDLoadState` calls `UpdateModelMemoryBanks`.
- No Quorum TTD fixture exists. The first one (a boot to the menu) is recorded in phase 5 and committed under `testdata/ttd/`.

### 4.7 Snapshots

- **MachineStateTransfer** (`machinestatetransfer.cpp`): a new `PagingFamily::Quorum` ("Quorum") in `FamilyOf`, with
  `FamilyName`. It needs: a Quorum target accepts a `Mode48` or `Mode128` source (pages 0-7, plain `#7FFD`) by writing `#00` = `#20`
  (normal ROM, nothing mapped, no block) and the 7FFD latch; the ROM role maps Editor -> page 2 and Basic48 -> page 3. As a source,
  a Quorum in the plain state (`#00` = `#20`, no trap) is a `Mode128` state; any other `#00` is `SameModel` only.
  A Quorum never accepts the Profi, Scorpion or ATM extended states (`SameFamily`).
- **Snapshot pipeline policy and capture** (`snapshotpolicy.*`, `snapshotcapture.cpp`): the capture `switch` gets `case MM_QUORUM`
  with machine hint `"quorum"` (timing hint `"128k"` until Q3), banks 0-7 (`#00` plain) and the 7FFD value; when `#00` is not
  plain the capture returns `Unavailable("... #00 holds ...", "capture_unsupported")`. No named commit policy is needed:
  the legacy commit plus the transfer covers it. 1024K capture waits for Q2.
- The file formats: SNA and Z80 have no Quorum machine id; whether SZX has one is not verified (Q12): until checked a Quorum
  saves as a 128K snapshot and loads as one.

### 4.8 Media and slots

Disks: the two built-in drives through the normal media API. `core/src/emulator/slots/refdata/machines.cpp` gets a
`MachineDef` for `MM_QUORUM` with built-ins (AY, WD1793, Kempston) and no bus (no source for an expansion connector: Q11).
The Quorum has no tape-deck-specific part; the tape follows `#FE` bit 6 and the standard loaders.

### 4.9 Automation and UI (every `switch (model)` that must learn the model)

CLI model table (`cli-processor-state.cpp`), WebAPI (`state_memory_api.cpp`, OpenAPI enum, `/state/paging` fields
`quorum_p00`, `quorum_p7ffd`, `quorum_rom`, `quorum_trap`), MCP `unreal://machine/quorum` resource and model enum, Lua and
Python `paging_state()`, port map and port trace, debugger ROM names, Qt `menumanager.cpp`
`supportedModels` (add `MM_QUORUM`, design link to this folder). Host-key mapping in the Qt `KeyboardManager` for the extra keys.

## 5. Plan

Each phase is test-first ([tdd-plan.md](tdd-plan.md)), ends with the full build (zero warnings), `core-tests` green and
`docker/linux/build.sh --test` (gcc), and carries the same automation checklist:

> **Automation parity gate (every phase):** the new behavior is reachable and tested through CLI, WebAPI (+ OpenAPI),
> MCP, Lua and Python where the surface applies, and visible in Qt. `.recipe/machines/quorum.md` is updated and every
> command in it was run live. **A TTD recording is started before each live verification run** (recorded session saved
> to `scratch/`), so a failing run can be stepped back.

| Phase | Work | Needs | Settles |
|:--|:--|:--|:--|
| 0 | Evidence: done in this folder; remaining open items need only a ROM disassembly (Q1, Q2, Q10) or a Quorum document | - | - |
| 1 | `PortDecoder_Quorum` for **128K**: `#00`, `#7FFD` (128K bits, no 1024 bits), `#FE`, memory map 2.4 (Q1/Q10 recommended defaults), reset, NMI; `IsModelSupported`; config folder; ROM roles; the refusal test replaced; boots to the ROM-MENU, 128 and 48 BASIC. Automation: model name on every surface, `/state/paging`, recipe skeleton, Qt menu | - | R1-R4, R10-R12, R16, R19 |
| 2 | FDC: `#80..#85`, the `#FF` translation, TR-DOS trap, boot a TRD from the menu; media API; recipe disk section | 1 | R14, R17, R18, R40 |
| 3 | Keyboard: `QuorumKeyboard`, `#7E`, host and automation key names, F11 / F12; AY and joystick decode (BC masks per Q7) | 1 | R13, R15, R41, R42 |
| 4 | Video: `DetectVideoMode`, raster 2.5, screen page; floating bus decision | 1 | R30, R31 |
| 5 | TTD blob and fixture; `MachineStateTransfer` family; snapshot capture; port map rows | 1-4 | R50, R51, R54 |
| 6 | 1024K (and 256K if Q9 says yes): the page bits, `BLK_128`, the Pentagon timing bit - only after Q2 / Q3 close; capture for 1024K | Q2, Q3 | R11, R32 |
| 7 | Docs to the permanent tree, `AGENTS.md` model list, `data/rom/README-ROMS.md` source line, slots `MachineDef`, final recipe pass, move this folder to DONE | 1-6 | - |

Phase 1 can start now: its only open items (Q1, Q10) have a recommended default that the ROM cannot contradict (the ROM
sets bit 7 never and never writes under a mapped ROM that matters).
