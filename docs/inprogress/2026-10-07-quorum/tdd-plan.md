# Quorum: test plan

**Date:** 2026-10-07 · part of [README.md](README.md) · follows [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase of [design.md](design.md) section 5. Every test runs in under 50 ms unless it boots the
real ROM (those carry a comment saying why and use `EnableTurboMode()`). No `sleep_for`: waits use `TestWait::For`.
Scratch files use `TestPathHelper::GetUniqueTestScratchPath()`. Test files are named `<sourcefile>_test.cpp`; test
classes `ClassName_Test`. Port tests use a synthetic 64K ROM (page k filled with `#C0 | k`) and RAM page tags
(`#40 | n`) in a `quorumfixture.h`, derived from `profifixture.h`, so that a read answers "which page is here".
Real-ROM tests use the tracked `data/rom/qu7v42.rom`.

## 1. Phase 1: model, `#00`, `#7FFD`, `#FE`

| Test file | Test | Checks |
|:--|:--|:--|
| `core/tests/emulator/emulatormanager_test.cpp` | `QuorumIsCreatable` | the existing "refused" loop drops `MM_QUORUM`; creation gives the Quorum decoder; RAM 128 and 1024 accepted, 512 refused |
| same | `ModelListHasQuorum` | `list_models` contains `QUORUM` once |
| `core/tests/emulator/ports/models/portdecoder_quorum_test.cpp` | `ResetState` | after reset: `#00` = 0, `#7FFD` = 0; `#0000` reads the SYS page tag (`#C0`) |
| same | `Port00SelectsRomPage` | `OUT #00,#20` -> 128 page tag (`#C2`); with `#7FFD` bit 4 -> 48 page (`#C3`); `#00` = 0 -> SYS again |
| same | `Port00DecodeIsPartial` | writes to `#0000`, `#0006`, `#0060` (A7, A4, A3, A0 = 0) all reach `#00`; `#0080`, `#0010`, `#0008`, `#0001` do not |
| same | `Port00RamOverRom` | `#00` = `#21` -> `#0000` reads RAM tag `#40`, a write there sticks; `#29` -> RAM page 8 on 1024K (tag `#48`), page 0 on 128K |
| same | `Port00BlockWriteIgnores7ffd` | `#00` = `#60`; `OUT #7FFD` leaves the page; clearing bit 6 allows it again |
| same | `Port00BlockWriteDiscardsLowWrites` | with bit 6, a write to a RAM-mapped `#0000` does not change the page |
| same | `Port7ffdDecode` | `0x801A/0x0018`: `#7FFD`, `#7FFC` (A0 free) page; `#FFFD` (A15 = 1), `#7FF5` (A3 = 0) do not |
| same | `Port7ffdPages128k` | `#C000` tag = `#40 | (v & 7)` for all 8; `#4000` is page 5, `#8000` page 2 always |
| same | `Port7ffdScreenBit` | bit 3 flips the screen source between page 5 and 7 (checked through the screen base) |
| same | `PortFeDecode` | `0x99/0x98`: `#FE`, `#7FFE` change the border; `#7E` (A7 = 0) and `#FF` (A0 = 1) do not |
| same | `PortFeBeeperAndEar` | bit 4 reaches the beeper, EAR returns on bit 6 |
| same | `ReservedLatch80fd` | a write to `#80FD` is stored, changes no bank |
| same | `NmiClearsPort00` | `#00` = `#60`; NMI acknowledge -> `#00` = 0, SYS page visible |
| same | `Overlapping7ffcFiresBoth` | `OUT #7FFC` changes the page and the border (open question Q8: this test pins the recommended default) |
| `core/tests/emulator/memory/rom_test.cpp` (existing) | `QuorumRomRoles` | the four role names, SYS first |
| `core/tests/emulator/machines/quorum/quorum_boot_test.cpp` (new, real ROM; boot-bound, comment says so) | `BootsToRomMenu` | VRAM contains the "ROM-MENU QUORUM" text, PC in the SYS page |
| same | `MenuStarts128Basic` / `MenuStarts48Basic` | after the menu key, PC executes in the 128 / 48 page and the `(c)` screen shows (key names fixed once seen) |

## 2. Phase 2: FDC and TR-DOS

| Test file | Test | Checks |
|:--|:--|:--|
| `portdecoder_quorum_test.cpp` | `FdcRegistersMap` | `OUT #80..#83` reach the WD1793 command / track / sector / data; `IN #80` returns status |
| same | `FdcSystemTranslation` | for each `v` in 0..3 and bit 4 on/off, the system latch equals `((v & ~3) ^ 0x10) \| {3,0,1,3}[v & 3]` |
| same | `FdcDriveSelectBAndInvalid` | `#85` = `01` selects drive A, `10` B, `00` and `11` select nothing |
| same | `FdcAlwaysOn` | `#00` bit 7 set does not hide `#80` (pins Q6 default) |
| same | `TrdosTrapEntersAt3dxx` | M1 fetch at `#3D00` with the 48 page -> trap on; fetch at `#4000` -> off; with the 128 page mapped -> no trap |
| same | `TrapShowsDosRomWhenBit7` | `#00` = `#A0` and trap: `#0000` reads the DOS page tag (`#C1`) |
| same | `TrapShowsRamWhenBit7Clear` | `#00` = `#20` and trap: `#0000` reads RAM page 0 (pins Q1 recommended default; one line to change if Q1 closes the other way) |
| `quorum_trdos_test.cpp` (new, real ROM) | `BootsTrdFromMenu` | the TRD fixture from `testdata` boots; a file listing appears |
| same | `WritesToDriveB` | a sector written on drive B is in the mounted image (scratch path) |
| `emulator/io/fdc/wd1793_test.cpp` (existing) | `BetaSystemBit4Inverted` only if the WD1793 model needs a hook for the Quorum system byte | otherwise none |

## 3. Phase 3: keyboard, AY, joystick

| Test file | Test | Checks |
|:--|:--|:--|
| `core/tests/emulator/io/keyboard/quorumkeyboard_test.cpp` | `Matrix7eEveryKey` | each of the 39 positions: press, read `#7E` with its row low, exact bit 0 |
| same | `Matrix7eRowsAnd` | two rows low read the AND of both |
| same | `Matrix7eIdleIs3f` | no key: 6 bits set, bits 6-7 from the bus (1) |
| same | `Port7eDecode` | `0x99/0x18`: `#7E` and `#007E`-style aliases with A6, A5, A2, A1 changed (`#1E`) read the matrix; `#FE` (A7 = 1) does not |
| same | `CursorKeysOnDigitPositions` | host Left/Down/Up/Right appear at `#F7FE` b4, `#EFFE` b4, b3, b2 without Caps Shift |
| same | `F11IsOneNmiPerPress` / `F12Resets` | a held F11 requests one NMI; F12 resets |
| `portdecoder_quorum_test.cpp` | `AyPortsWork` | register select / data round trip through `#FFFD / #BFFD` |
| same | `JoystickAt1f` | `IN #1F` returns the Kempston bits |
| `quorum_boot_test.cpp` | `RomReadsExtraKeyboard` | pressing the menu key through the `#7E` matrix makes the menu act (the ROM needs `#7E`) |

## 4. Phase 4: video

| Test file | Test | Checks |
|:--|:--|:--|
| `core/tests/emulator/video/int_timing_test.cpp` | `QuorumFrameLength` | frame 69888 T, line 224 T, INT length 32 T, first paper at 80 x 224 + 65 (pinned numbers with the Z source in a comment; marked unproven) |
| `core/tests/emulator/video/screen_test.cpp` | `QuorumDetectsItsMode` | `DetectVideoMode(MM_QUORUM)` is not the legacy mode |
| same | `QuorumNoContention` | a NOP stream in the screen page takes un-contended time |
| `quorum_video_test.cpp` | `PaperAndBorderPixels` | a pixel written to page 5 / 7 appears where expected |

## 5. Phase 5: TTD, transfer, capture

| Test file | Test | Checks |
|:--|:--|:--|
| `core/tests/debugger/ttd/ttdquorumpaging_test.cpp` | `StateRoundTrip` | save, change latches, load: `#00`, `#80FD`, trap restored and the banks rebuilt |
| same | `BlobSizeIsPinned` | `sizeof` and the `version` byte |
| `core/tests/debugger/ttd/quorumttd_test.cpp` | `ReplayIsBitExact` | record a boot to the menu with keys, seek back and forward, hashes equal; a checkpoint from `QUORUM` refuses to load into another model |
| `core/tests/loaders/snapshot/machinestatetransfer_test.cpp` | `QuorumTakesA128Source` / `QuorumPlainIsA128Source` / `QuorumUnusualP00IsSameModelOnly` | the three rules of design 4.7 |
| `core/tests/loaders/snapshot/snapshotcapture_test.cpp` | `QuorumCaptures128View` / `QuorumRefusesWhenP00IsNotPlain` | the view has 8 banks and the 7FFD; the refusal carries `capture_unsupported` |
| `portdecoder_portmap_test.cpp` | `QuorumPortRows` | the table in design 2.1 appears, `modelName` is "Quorum" |
| model fingerprint test (existing) | unchanged | other models' fingerprints are the same |

## 6. Automation (every phase, as the phase adds surface)

| Area | Cases |
|:--|:--|
| WebAPI | `POST /emulator/start {"model":"QUORUM","ram_size":1024}` -> 200; `/state/paging` has `quorum_p00`; OpenAPI enum lists `QUORUM` |
| CLI, Lua, Python | model-name round trip; `paging_state()` fields; extra key names press and read back |
| MCP | `emulator_manage create QUORUM`; `inspect_state` paging; `type_input` with an extra key name |
| Qt | menu entry present (a menu-builder unit test, as for the other models) |
| Live | the recipe `.recipe/machines/quorum.md`: every command run against a live build, with a TTD recording started first, the transcript kept in `scratch/` |

## 7. Phase 6 additions (after Q2 / Q3)

`Paging1024`: the page-bit table of the chosen reading for all 64 pages; `Blk128`; `PentagonTimingBit`; `Ram256`
(if kept): bit 6 only. Each is a named test whose comment cites the question that closed.
