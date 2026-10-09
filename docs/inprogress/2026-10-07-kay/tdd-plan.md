# Kay-1024: test plan

**Date:** 2026-10-07 · part of [README.md](README.md) · follows [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase of [design.md](design.md) section 8. Every test runs in under 50 ms unless it boots a
real ROM (the boot tests say so in a comment). No test sleeps: waits use `TestWait::For`. Boot-bound tests call
`EnableTurboMode()`. Scratch files use `TestPathHelper::GetUniqueTestScratchPath()`. Test files are named
`<sourcefile>_test.cpp`; fixtures are `ClassName_Test`. Real-ROM tests use `data/rom/kay1024.rom` (tracked).

## 1. Phase 1: model, paging, ROM

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/emulatormanager_test.cpp` | `MM_KAY` leaves the "no decoder" list; `PortDecoder::IsModelSupported(MM_KAY)`; `FindModelByShortName("KAY")`, `"KAY1024"`, `"KAY256"` (RAM 256); `IsModelCreatable`; `GetConfigFolderForModel(MM_KAY) == "kay"`; the other four models (GMX, Quorum, LSY256, Phoenix) stay unsupported (negative) |
| `core/tests/emulator/ports/models/portdecoderkay_test.cpp` | `PortDecoderKay_Test.Port7FFDDecode`: `OUT #7FFD` takes effect, `#FFFD`-style ports and `#7FFC` (A0 = 0) do not page; `Port1FFDDecode`: `#1FFD`, `#3FFD` latch, `#1FFC` does not; `RamPageBitsAssemble`: 7FFD D0-D2 + 1FFD D4 + 1FFD D7 + 7FFD D7 give pages 0..63 at #C000 on 1024 KB (a table of bit patterns); `RamPageMaskedOn256`: pages wrap at 16; `RamAtZero`: 1FFD D0 = 1 maps page 0 at #0000, ROM off; `FixedWindows`: #4000 page 5, #8000 page 2 for every latch value (Q3); `LockBit`: 7FFD D5 = 1 refuses the next #7FFD, still accepts #1FFD; `ResetClearsLatches`; `RomRoleIndex`: the 4 x 2 table of (ROMS, TR-DOS, 7FFD.4) to role; `RomRoleToPageStandard` and `RomRoleToPageLayoutB` (`{0,1,3,2}`, `{2,3,0,1}`); `ScreenPagesAreLow`: shadow screen is page 7 whatever the top bits |
| `core/tests/emulator/memory/modelsregression_test.cpp` | `MM_KAY` with the synthetic tagged ROM (each page filled with its own tag): the role at #0000 for each state; the existing models unchanged |
| `core/tests/debugger/ttd/kay/ttdkaypaging_test.cpp` | `TTDKayPaging_Test.RoundTrip`: save / load / hash equal; `ChangesWithP1FFD`; `SizeIsFour` (`static_assert` mirror); `RestoreRebuildsBanks`: load a blob with 1FFD D0 = 1 and RAM is at #0000 |
| `core/tests/debugger/ttd/ttdmodelstatecontract_test.cpp` | every id from `PortDecoderKay::GetTTDModelStateIds()` has a serializer (add Kay to the model list) |
| `core/tests/emulator/kay_boot_test.cpp` (real ROM, boot-bound: it runs the ROM) | `KayBoot_Test.Rom128Editor`: the 128 menu text in VRAM; `Rom48Basic`: `(C) 1982` screen; `TrDosBanner`: from 48 BASIC `RANDOMIZE USR 15616` shows the TR-DOS banner (this settles Q1; if it fails the role table is swapped, see roms.md section 3); `KramisMenu`: ROMS = 1 with 7FFD.4 = 0 shows the "KRAMIS" page; `RamDisk1024`: a write and read of page 63 and page 0 differ |

## 2. Phase 2: ports and devices

| Test file | Cases |
|:--|:--|
| `portdecoderkay_test.cpp` | `AyDecode`: #BFFD / #FFFD with A0 = 1, A1 = 0 reach the AY; #BFFC does not; `FfFdRead`: register data; `KempstonOddPorts`: `IN #1F`, `#FF`, `#01` return the joystick byte, D5-D7 = 0; `KempstonBlockedOnFFFD`: `IN #FFFD` is the AY; `KempstonOffInDos`: with the DOS ports on, #1F is the FDC; `FeWrite`: border, tape, speaker bits; `FeReadBits`: D5 = 0, D7 = 1, D6 = EAR; `NoPortsOnOtherAddresses`: `#DFFD`, `#EFF7` change nothing (negative); `SinclairJoystickRows`: keys 6-0 on #EFFE; `Mouse`: standard decode answers |
| `core/tests/emulator/ports/portdecoder_portmap_test.cpp`, `portdecoder_porttag_test.cpp` | Kay's port map rows; model name "Kay-1024" |
| `core/tests/emulator/io/ide/ideadapter_test.cpp` | `SchemeFits(IDE_NEMO, MM_KAY)` true; `IDE_PROFI` on Kay false (negative) |
| `core/tests/emulator/ports/models/kempston_mouse_decode_test.cpp` | Kay row |

## 3. Phase 3: video and timing

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/video/int_timing_test.cpp` | Kay: frame 69888, line 224, INT 32 T, INT-to-paper distance (the pinned number, with its source in a comment); the INI path and the programmatic path give the same numbers |
| `core/tests/emulator/video/contention_test.cpp` | `ContentionNegative_Test.ClonesNeverWait` includes Kay |
| `core/tests/emulator/video/screen_test.cpp` | Kay mode is normal 256x192; 4T border: a border change inside a 4 T group shows at the group edge |

## 4. Phase 4: turbo

| Test file | Cases |
|:--|:--|
| `portdecoderkay_test.cpp` | `TurboNeedsSwitchAndBit`: on only with the switch on and 1FFD D2 = 0; reset with the switch on gives turbo; the switch is a recorded input |
| `core/tests/emulator/turbomode_test.cpp` | Kay runs 2x T-states per frame at turbo; `A/B` numbers recorded in the design before any overlay is added |
| `core/tests/debugger/ttd/kay/ttdkaypaging_test.cpp` | the turbo switch survives a seek |

## 5. Phase 5: snapshots, transfer

| Test file | Cases |
|:--|:--|
| `core/tests/loaders/snapshot/snapshotsave_test.cpp` | Kay view: 16 / 64 banks, `p1FFD` carried; a plain 128K state saves as `.sna` / `.z80`; a state with extension bits or ROMS = 1 gives a reason, not a wrong file; `.szx` is refused with the reason "no Kay machine id" (negative) |
| `core/tests/loaders/snapshot/machinestatetransfer_test.cpp` | `FamilyOf(MM_KAY) == Kay`; Kay to 128K keeps pages 0-7; 128K to Kay sets the roles |
| `core/tests/loaders/snapshot/snapshotgolden_test.cpp` | a golden Kay `.z80` round trip |
| `core/tests/debugger/ttd/` fixture | `testdata/ttd/` Kay boot recording replays equal |

Every phase adds its surface checks to the existing automation tests (CLI model tables, WebAPI model list and OpenAPI
enum, MCP resource text, Lua / Python model names) and runs the recipe by hand.
