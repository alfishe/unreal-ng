# Model-specific state out of `EmulatorState`: technical design and test plan

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Requirements** | [goals-and-requirements.md](goals-and-requirements.md) |
| **Branch** | `refactor/model-state` |
| **Status** | Implemented |

## 1. Shape after the change

```
core/src/emulator/platforms/
├── atm/atmstate.h        AtmState       (ATM 4.50, ATM 7.10, ATM3 / ZX-Evo, Alco video)
├── atm/atmstate.cpp      AtmState::InitFont (needs ATM_FONT)
├── zxevo/evostate.h      EvoState       (PortDecoder_ATM3: ATM3 and ZX-Evo BaseConf)
├── scorpion/scorpionstate.h  ScorpionState  (Scorpion ZS-256 + its SMUC)
├── profi/profistate.h    ProfiState     (Profi v3 / v5)
└── tsconf/...            unchanged
```

`platform.h`:

```cpp
#include "emulator/platforms/atm/atmstate.h"
#include "emulator/platforms/profi/profistate.h"
#include "emulator/platforms/scorpion/scorpionstate.h"
#include "emulator/platforms/zxevo/evostate.h"

struct EmulatorState
{
    // counters, frame cost, digest, clock ... unchanged

    /// region <Port state>
    uint8_t p7FFD, pFE, pEFF7;          // Common ports
    uint8_t pBFFD, pFFFD;               // AY sound-specific
    uint8_t pDFFD, pFDFD, p1FFD, pFF77; // Models with extended memory-specific
    /// endregion </Port state>

    bool video_memory_changed;
    // A board NMI waits for the frame INT (read by Z80::ProcessInterrupts on every model;
    // only ZX-Evo sets it)
    bool nmiAtIntStartPending = false;

    uint8_t wd_shadow[4];
    unsigned active_ay;
    uint8_t flags = 0x00;
    uint8_t border_attr;
    struct { ... } tape;
    uint8_t comp_pal[0x10];
    uint8_t ulaplus_cram[64];
    uint8_t ulaplus_mode;
    uint8_t ulaplus_reg;

    /// region <Hardware-family state>
    AtmState atm;             // ATM Turbo 1 / 2+ / 3, ZX-Evo BaseConf
    EvoState evo;             // ATM3 / ZX-Evo BaseConf decoder only
    ScorpionState scorpion;   // Scorpion ZS-256
    ProfiState profi;         // Profi
    /// endregion
};
static_assert(std::is_trivially_copyable_v<EmulatorState>, "...reset and TTD copy it by value");
```

The family structs are plain aggregates with default member initializers of zero, so
`EmulatorState{}` zeroes them exactly as it zeroed the flat fields.

### 1.1 `AtmState` (`platforms/atm/atmstate.h`)

```cpp
#pragma once
#include <cstdint>

/// @file atmstate.h
/// @brief ATM Turbo family state shared by PortDecoder_ATM450, PortDecoder_ATM710 and
/// PortDecoder_ATM3 (ATM3 and ZX-Evo BaseConf), read by the ATM / Alco video and memory.
/// Embedded by value in EmulatorState::atm: reset and the TTD paging blob copy it as is.
struct AtmState
{
    uint8_t aFE = 0;            // ATM 4.50 system port #FE
    uint8_t aFB = 0;            // ATM 4.50 system port #FB
    unsigned pFFF7[8] = {};     // ATM 7.10 / ATM3 (4 MB) memory map: |7ffd|rom|b7b6|b5..b0|
    unsigned aFF77 = 0;         // last #xx77 address (mode, CPM, pen2 = A14)
    bool memSwapped = false;    // A5-A7 <-> A8-A10 swap (vestigial, kept for the TTD paging blob)

    // 16-cell palette RAM behind port #FF: ... (comment moved from platform.h)
    uint32_t palette[16] = {};
    uint8_t paletteRegs[16] = {};
    uint8_t borderBright = 0;

    // Text-mode font RAM: ... (comment moved from platform.h)
    uint8_t fontRam[2048] = {};
    uint8_t fontByte = 0;

    void InitPalette();         // standard 16 ZX colors (body moved unchanged, inline)
    void InitFont();            // atmstate.cpp: ATM_FONT transposed, fontByte = #FF
};
```

### 1.2 `EvoState` (`platforms/zxevo/evostate.h`)

```cpp
/// ATM3 / ZX-Evo BaseConf (PortDecoder_ATM3) latches. EmulatorState::evo.
struct EvoState
{
    // ATM3: pBD word, pBDb.l / pBDb.h bytes (named to satisfy ISO C++)
    union
    {
        uint16_t pBD;
        struct { uint8_t l; uint8_t h; } pBDb;
    };
    uint8_t pBE = 0, pBF = 0;
    uint8_t fddMask = 0;        // #13BD: bit n = drive n emulated in software
    uint8_t trdemu = 0;         // zdos.v: bit 0 page #FE in #0000, bit 1 swap due
    uint8_t vgSys = 0;          // D5..D0 of the last OUT (#FF)
    uint8_t wrProt = 0;         // #xBF7 write protect
    uint8_t turboPending = 0;   // zclock.v int_turbo
    bool inNmi = false;         // znmi.v: page #FF forced into #0000-#3FFF
    bool nmiEntry = false;      // the next accepted NMI is the board's own
};
```

The anonymous union cannot carry a default member initializer on `pBD` together with value-init
of the struct around it under every compiler; `EmulatorState{}` value-initializes `evo` and
therefore zeroes the union as before. `pBD` has no initializer (as today).

### 1.3 `ScorpionState`, `ProfiState`

```cpp
struct ScorpionState
{
    uint8_t turbo = 0;          // Turbo+ flip-flop (hardware-reference 13)
    uint8_t p7EFD = 0;          // ProfROM window latch
    uint8_t profromBank = 0;    // ProfROM plane
    uint8_t dosTrigger = 0;     // magic-button DOS trigger (hardware-reference §9)
    uint8_t pFFBA = 0, p7FBA = 0;   // SMUC
};

struct ProfiState
{
    uint8_t turboSwitch = 0;    // front-panel TURBO
    uint8_t cpmSwitch = 0;      // v5 front-panel CP/M
    uint16_t palette[0x10] = {};    // hi-res palette, 9-bit GGGRRRBBB
};
```

The comments on each field move with it verbatim from `platform.h`.

## 2. Field map

| Old `EmulatorState` field | New path |
|---|---|
| `aFE`, `aFB`, `pFFF7[8]`, `aFF77` | `atm.aFE`, `atm.aFB`, `atm.pFFF7`, `atm.aFF77` |
| `atmMemSwapped` | `atm.memSwapped` |
| `atmPalette`, `atmPaletteRegs`, `atmBorderBright` | `atm.palette`, `atm.paletteRegs`, `atm.borderBright` |
| `atmFontRam`, `atmFontByte` | `atm.fontRam`, `atm.fontByte` |
| `InitAtmPalette()`, `InitAtmFont()` | `atm.InitPalette()`, `atm.InitFont()` |
| `pBD`, `pBDb`, `pBE`, `pBF` | `evo.pBD`, `evo.pBDb`, `evo.pBE`, `evo.pBF` |
| `evoFddMask`, `evoTrdemu`, `evoVgSys`, `evoWrProt`, `evoTurboPending` | `evo.fddMask`, `evo.trdemu`, `evo.vgSys`, `evo.wrProt`, `evo.turboPending` |
| `evoInNmi`, `evoNmiEntry` (bitfields) | `evo.inNmi`, `evo.nmiEntry` (`bool`) |
| `nmiAtIntStartPending` (bitfield) | stays, plain `bool` |
| `scorpion_turbo`, `p7EFD`, `profrom_bank`, `scorpionDosTrigger` | `scorpion.turbo`, `scorpion.p7EFD`, `scorpion.profromBank`, `scorpion.dosTrigger` |
| `pFFBA`, `p7FBA` | `scorpion.pFFBA`, `scorpion.p7FBA` |
| `profi_turbo_switch`, `profi_cpm_switch`, `profiPalette` | `profi.turboSwitch`, `profi.cpmSwitch`, `profi.palette` |
| `pXXXX`, `p78FD`, `p7AFD`, `p7CFD`, `gmx_config`, `gmx_magic_shift`, `p00`, `p80FD`, `pLSY256` (+ `PF_*` defines), `pVD`, `vdbase`, `res1`, `res2`, `p0F`, `p1F`, `p4F`, `p5F`, `nvram` (+ `struct NVRAM`) | removed (dead) |

Older design documents under `docs/` (38 files) keep the old names: they record what the code was
when they were written, and many of the names are TTD blob fields, which do not change. This table
is the lookup.

Only `EmulatorState` members are renamed. Same-named fields of other structs keep their names:
`TTDAtmPagingBlob::aFF77`, `TTDScorpionProfRomBlob::p7EFD`, `VideoWriteLog::aFE`, the video-map
snapshots, `ProfiPagingBlob::profiPalette`, `EvoAvr`'s local `nvram`, `CONFIG::profrom_bank`
(comment only).

## 3. Files and edits

The compiler is the inventory: the old fields are deleted first, then every
`no member named 'X' in 'EmulatorState'` error is fixed at its line and column. A reference to
an old name that compiles after the change can only be a member of another struct (§2 last
paragraph), a comment or a string.

| Area | Files | Edit |
|---|---|---|
| State | `emulator/platform.h` | fields out, four members + includes + `static_assert` in; comments moved |
| State | `platforms/{atm,zxevo,scorpion,profi}/*state.h`, `platforms/atm/atmstate.cpp` | new |
| State | `emulator/emulatorcontext.cpp` | `EmulatorState::InitAtmFont` body moves to `atmstate.cpp`; ctor calls `emulatorState.atm.InitPalette()` / `.atm.InitFont()` |
| CPU | `cpu/core.cpp` (`scorpion.turbo`, `scorpion.dosTrigger`, `atm.pFFF7`), `cpu/z80.h` (comment) | path |
| Ports | `portdecoder.cpp/.h` (`PagingLatch::P7EFD`, `pFFF7` latch table), `portdecoder_atm450/.h`, `portdecoder_atm710/.h`, `portdecoder_atm3/.h`, `portdecoder_scorpion256.cpp`, `portdecoder_profi.cpp` | path |
| Memory | `memory.cpp` (`atm.aFF77`), `scorpion/scorpionmemory.cpp`, `scorpion/scorpionromwindow.cpp/.h`, `atm/evofontoverlay.h` | path |
| Video | `screen.cpp/.h`, `atm/screenatm.cpp`, `atm/atmvideomapper.cpp`, `alco/screenalco.cpp`, `alco/alcovideomapper.cpp`, `profi/screenprofi.cpp`, `profi/profivideomapper.cpp`, `map/videomapservice.cpp`, `map/videomap.h`, `map/videomapper.h`, `map/videowritelog.h` | path where the source is `EmulatorState` |
| State dump | `state/devicestate.cpp`, `state/devicestatevideo.cpp` | path |
| Emulator | `emulator.cpp` (`scorpion.dosTrigger`) | path |
| TTD | `atm/ttdatmpaging.cpp/.h`, `atm/ttdevofontram.cpp`, `scorpion/ttdscorpionprofrom.cpp/.h`, `scorpion/ttdsmuc.cpp/.h`, `profi/ttdprofipaging.cpp/.h`, `bench/ttdbench.cpp`, `ttdcheckpoint.cpp/.h` (comments) | only the `state.X` side of each copy; blob structs untouched |
| Tests | ~55 files under `core/tests` (list: `git diff --stat`), `_helpers/snapshotdigest.cpp` | path; `pXXXX` (always 0) dropped from the digest list; `machinestatehash_test.cpp` drops the assignments to the removed GMX / Quorum fields |
| Docs | this folder | |

Example, `portdecoder_atm3.cpp`:

```cpp
// before
_state->evoTrdemu |= 0x02;
_state->pFFF7[bank] = value;
_state->atmPalette[cell] = rgba;
// after
_state->evo.trdemu |= 0x02;
_state->atm.pFFF7[bank] = value;
_state->atm.palette[cell] = rgba;
```

Example, `ttdatmpaging.cpp` (blob field names stay, format unchanged):

```cpp
blob.aFF77 = state.atm.aFF77;
blob.atmMemSwapped = state.atm.memSwapped ? 1 : 0;
blob.evoTrdemu = state.evo.trdemu;
```

## 4. Test plan: how the behavior is held

### 4.1 Red first: new test `core/tests/emulator/state/emulatorstate_familystate_test.cpp`

Written before the structs exist (does not compile = red), green after step 3:

| Test | Asserts |
|---|---|
| `FamilyState_IsTriviallyCopyable` | `static_assert` on `EmulatorState`, `AtmState`, `EvoState`, `ScorpionState`, `ProfiState` (no owning pointers can sneak in) |
| `FamilyState_ValueInitZeroes` | `EmulatorState s{}` after scribbling a copy: every family byte is 0 |
| `FamilyState_ContextInitSeedsAtmPaletteAndFont` | a fresh `EmulatorContext`: `atm.palette` = the 16 ZX colors (same constants as the moved `InitPalette`), `atm.fontRam[c * 8 + r] == ATM_FONT[r * 256 + c]`, `atm.fontByte == 0xFF` |
| `FamilyState_ResetByAssignmentClearsFamilies` | set fields in all four, `state = EmulatorState{}`, all zero: the `EmulatorContext` reset path |
| `FamilyState_CopyCarriesFamilies` | copy-assign a scribbled state, `memcmp` of each family equal |

### 4.2 Characterization: the existing suite, unchanged assertions

About 1500 references in tests are moved to the new paths with **no change to any expected
value**. They pin the behavior that must not move:

- port decoders: `portdecoder_atm3_test`, `portdecoder_atm450_test`, `portdecoder_atm710_test`,
  `portdecoder_profi_test`, `scorpionports_test`, `scorpionmni_test`, `scorpionsmuc_test`,
  `scorpionturbo_test`, `kempston_mouse_decode_test`, `portdecoder_porttag_test`,
  `portdecoder_portmap_test`, `fulldecodeclaim_test`;
- memory / overlays: `scorpionromwindow_test`, `profrom_plane_notification_test`,
  `atm710turbooverlay_test`, `evo*_test`, `turbotest_test`;
- video: `atm_video_modes_suite_test`, `atm_videomode_test`, `profi_video_test`,
  `videomapservice_test`, `devicestatevideo_test`;
- TTD round trips (format fixed by unchanged blobs and their `static_assert`s):
  `ttdatmpaging_test`, `ttdscorpionprofrom_test`, `ttdprofipaging_test`, `ttdpage255_test`,
  `ttdcheckpoint_test`, `timetravelmanager_engineseek_test`,
  `timetravelmanager_servicestate_test`, `machinestatehash_test`;
- whole machines booting real ROMs with screen / state checks: `atm450_boot_test`,
  `atm710_trdos_boot_test`, `atm710_game2048_repro_test`, `zxevo_boot_test`, `zxevo_ers_test`,
  `scorpionmachine_test`, `snapshotpipeline_test`.

### 4.3 Gates before commit

1. **Baseline.** `tools/build/test.sh` on the untouched branch (= master `dd64db70d`); keep the
   list of failed tests (expected: none).
2. **After.** `tools/build/test.sh`: same pass set plus the new tests. Any difference blocks.
3. **Full build.** `tools/build/build.sh` (core, tests, Qt apps): zero warnings.
4. **gcc:16 `-O3`** with the CI flags on every changed / new `.cpp` (docker, half the cores).
5. **Leftover scan.** `git grep -nwE '<old names>'` over `core/ unreal-*`: every hit must be a
   member of another struct, a comment that names the old field on purpose, or the TTD blob.
6. **Format scan.** `git diff master -- core/src/debugger/ttd/ttd.ksy` is empty; no line of a
   `struct TTD*` / `*Blob` definition changed.

## 5. Performance

The change moves fields; it adds no instruction. `_state->atm.aFF77` compiles to one load at a
fixed offset, like `_state->aFF77`. The moved fields are read in port handlers, at paging
updates, in video mode / palette lookups and at frame boundaries, never per Z80 instruction or
memory access (checked with `grep` over `cpu/` and `memory/memory*.{h,cpp}`: only `core.cpp`
reset and `memory.cpp`'s ATM paging update touch them). The struct layout of the hot fields
(`t_states`, clock, `p7FFD`, `pFE`, `flags`, `border_attr`) changes offset slightly because dead
fields above them are removed; they stay in the first cache lines. No A/B benchmark.

## 6. Results (2026-10-07)

| Gate | Result |
|---|---|
| Inventory | clang `-fsyntax-only` over all 1492 project TUs after deleting the fields: 890 accesses rewritten at the reported line:column in 66 files; the only leftovers were the dead GMX / Quorum fields in `machinestatehash_test.cpp` (assignments removed) and `pXXXX` in the snapshot digest (kept as a literal `0`, so `testdata/loaders/golden/commit-digests.txt` is unchanged and still checks every SNA / Z80 / SZX fixture) |
| Baseline (master `dd64db70d`) | 8427 OK, 86 skipped, 2 failed: `BlockFormats_Test.SparseRawExport` (fails on master every time on this host), `KeyboardInjection_Integration_test.AbortSequence_StopsImmediately` (flaky) |
| After | 8432 OK (the baseline set + 4 new `EmulatorStateFamily_Test`), 86 skipped, 1 failed: `SparseRawExport`, as on master. Per-test JSON from 16 shards, compared line by line with the baseline |
| Full build (clang, core + tests + Qt apps) | zero compiler warnings (the `ld` notes about Homebrew dylibs built for macOS 15 are on master too) |
| gcc:16 `-O3` | every changed / new `.cpp` (69): `core/src` with the CMake gcc flags (`-Wall -Wextra -Werror ...`), tests with theirs: 0 errors, 0 warnings |
| `unreal-qt-tests`, `unreal-asm-tests` | 54 / 163 passed |
| TTD format | `ttd.ksy` unchanged; blob headers: comments only |

## 7. Rollback

One branch, one squashed commit on master: `git revert` restores the flat struct. No data
format changed, so recordings and snapshots made in between stay readable either way.
