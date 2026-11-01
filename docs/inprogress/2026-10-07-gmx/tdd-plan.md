# GMX: test plan

**Date:** 2026-10-07 - part of [README.md](README.md) - follows [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase of [design.md](design.md) section 7. Every test runs in under 50 ms unless it boots a real
ROM (those carry a comment that says why). No test sleeps (`TestWait::For` where waiting is needed). Scratch files use
`TestPathHelper::GetUniqueTestScratchPath()`. Boot-bound tests call `EnableTurboMode()`, except those asserting on pixels.
File names follow `<sourcefile>_test.cpp`; test suites and names use no underscores except the `_Test` suffix. Real-ROM
tests use the tracked `data/rom/gmx644.rom`; software tests use `testdata/machines/gmx/` and skip when it is absent.

Fixture: `core/tests/emulator/ports/models/gmxfixture.h`, built from `scorpionfixture.h` with `mem_model = MM_GMX`,
`ramsize = 2048`, and a synthetic tagged ROM (each 16 KB page filled with its page number, like `modelsregression_test.cpp`)
so mapping tests do not depend on the flash content.

## 1. Plumbing (phase 1)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/emulatormanager_test.cpp` | `GMX` creatable: `IsModelSupported(MM_GMX)` true, `Config::IsModelCreatable` true, `GetConfigFolderForModel` is `gmx`; the loop of "models without decoders" drops `MM_GMX`; `SCORPION` and `PROFSCORP` unchanged (negative) |
| `core/tests/emulator/ports/models/portdecoder_gmx_test.cpp` | `GmxPortDecoder_Test`: factory returns the Scorpion decoder for `MM_GMX`; reset clears every latch; port-map rows list the five GMX ports and `#00` (model name "ScorpionGMX"); the same keys on `MM_SCORP` are not decoded (negative) |
| `core/tests/emulator/memory/rom_test.cpp` | `GmxRomLoads512K`: a 512K file loads, 32 pages, 8 planes; a 256K file is refused with the existing message; `MM_GMX` returns `gmx_rom_path`; role names per plane |

## 2. Ports and memory (phase 1)

| Test file | Cases |
|:--|:--|
| `portdecoder_gmx_test.cpp` | `Window3FormulaTable`: table over `(DFFD, 1FFD bit 4, 7FFD low 3)` against the tagged RAM pages (all 128 pages once); `Window2IsXor2`: `78FD = 0` maps page 2, `= 2` maps page 0, `= #7F` page `#7D`; values above the installed size wrap; `78FD` bit 7 not stored |
| `portdecoder_gmx_test.cpp` | `ReadBack78FD`: bit 7 = `#FE` bit 1, bits 0-6 stored; `ReadBack7AFD`: the status formula over a table of latches (bit 7 = `#FE` bit 0); `ReadBack7EFD`: each bit from its source latch (table of eight rows), bit 2 follows the turbo write |
| `portdecoder_gmx_test.cpp` | `MagicShiftReadout`: write `#00` = `#0B` (bit 3, low bits 3) with bit 4 = 1: eight reads of `#78FD` bit 0 give `1 1 0 1 0 0 0 1` (`#8B`, low bit first); the ninth is 0; bit 4 = 0 additionally resets the CPU (PC = 0), RAM and `#7FFD` keep |
| `portdecoder_gmx_test.cpp` | `BlockExtDisablesTheFile`: after `#00` bit 5, writes to `#78FD #7AFD #7CFD #7EFD #DFFD` change nothing and reads fall to the Scorpion decode; `#00` itself still works; clearing bit 5 re-enables |
| `portdecoder_gmx_test.cpp` | `ExactHighByteDecode`: `#78FD` works at `#78FD`, `#78F9`... only where `A5 = 1, A1 = 0, A0 = 1` and the high byte matches; `#79FD`, `#78DD` are not GMX ports (negative); `#xx00` works with every high byte (sweep of 256) |
| `scorpionromwindow_test.cpp` (existing, extended) / `gmxplane_test.cpp` | `PlaneFromBits654`: `7EFD` bits 6-4 select plane 0..7; with `#00` bit 4 set the plane does not change and bits 3 and 7 still do; the `#0100-#010F` read strobe changes nothing on GMX (negative, with a read sweep) while it still works on `PROFSCORP` |
| `gmxrommapping_test.cpp` | `RomPageTable`: the ROM rule of design 2.4 over `1FFD` bits 0-2, `7FFD` bit 4, DOS flag, plane (page index into the tagged ROM = `plane*4 + page`); `HardDosPageBeatsRam0`: `1FFD = 5` maps plane page 3 and the Beta ports are on; `1FFD = 1` alone maps RAM 0; clearing bit 2 from RAM code returns to the normal rule on the next fetch above `#4000` (from the firmware trace, Q2/Q16 note in the test comment) |
| `portdecoder_gmx_test.cpp` | `LockPolicy` (Q3 closed): the `7FFD.5` lock blocks later `#7FFD` writes while `7EFD.2 = 0`; with `7EFD.2 = 1` they go through |
| `portdecoder_gmx_test.cpp` | `ResetKeepsPort00` (Q18): after `#00 = #80` and a RESET, `#7EFD` read bit 4 is still 1 and every other GMX latch is 0; `Write00WithoutReconfOnlyStores`: `#00 = #02` changes no mapping and does not reset the CPU |
| `portdecoder_gmx_test.cpp` | `GmxPortsBeatScorpionMasks` (Q15): `OUT #78FD/#7AFD/#7CFD/#7EFD` do not change `#7FFD`; `OUT #DFFD` does not select an AY register |
| `gmxrampower_test.cpp` | `TwoMegabyteRamIsAllReachable`: write a page-number byte to each of the 128 pages through windows 2 and 3 and read them back |

## 3. TTD and state (phase 2)

| Test file | Cases |
|:--|:--|
| `core/tests/debugger/ttd/gmx/ttdgmxpaging_test.cpp` | `BlobRoundTrip`: serialize/deserialize a state with every latch non-zero, byte-for-byte; `RestoreRebuildsWindows`: restore on a fresh machine reproduces the RAM windows and the ROM plane; `MagicLockSurvives`; `VersionMismatchIsRefused` |
| `ttdgmxpaging_test.cpp` | `ReplayIsBitExact`: a short program that writes the five ports (run by `Z80` steps from a code buffer, no ROM) recorded and replayed gives the same state hash; checkpoint in the middle |
| `core/tests/loaders/snapshot/snapshotcapture_test.cpp` | `GmxPlainStateCapturesAsScorpion256`; `Gmx78FDInUseIsUnsupported` (reason mentions extended GMX paging); `GmxPageAbove15Unsupported`; `1FFD bit 2 set is unsupported` |
| `core/tests/loaders/snapshot/machinestatetransfer_test.cpp` | Scorpion-256 source -> GMX target is `Mode128` and loads; GMX source with `78FD != 0` -> `SCORPION` target refused with the reason; GMX -> GMX keeps the extension |
| `core/tests/emulator/state/emulatorstate_test.cpp` | `GmxStateIsTriviallyCopyable` (static assert guarded test), reset zeroes it |

## 4. Video (phase 3)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/video/screen_test.cpp` | `DetectModeGmx`: `7EFD` bit 3 -> `M_GMX` and `R_640_200`; clear -> the Scorpion mode; the video mode name is "GMX" (existing `screenvideomodename_test.cpp` stays) |
| `core/tests/emulator/video/screengmx_test.cpp` | `PixelAndAttributeFetch`: pages `#39 / #79` and `#3B / #7B` by `7FFD` bit 3 (synthetic pattern: byte value = column, attribute = row), first pixel and last pixel of a line, 80 bytes per line; `AttributeColors`: ink/paper/bright table over 16 attribute values; `FlashPhase`; `ScrollStartAddress` (Q4 closed): start 640 shows source row 8 on line 0, start 15 920 shows the last line first, the low nibble of `#7AFD` is ignored, pixels and attributes move together, wrap at 16 000 |
| `core/tests/emulator/video/screengmx_test.cpp` | `FramebufferSizeIs640x200PlusBorder` through the screenshot size query; `ReturnToStandardMode` restores 256x192 raster at the next frame |

## 5. Turbo and details (phase 4)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/ports/models/scorpionturbo_test.cpp` (existing, extended) | `GmxTurboFrom7EFD`: bit 7 on -> `hw_turbo_ratio` 2, no Turbo+ overlay; reset clears; `NoStrobeFromInPorts` (Q5): `IN` from `#1FFD` / `#7FFD` leaves turbo as it is; `RomLoopExactly2x`, `RamLoopWithin18To19x` (Q21) |
| `portdecoder_gmx_test.cpp` | `MagicLockFreezesReadBacks` (if Q7 accepted): NMI freezes `#7AFD`/`#7EFD` read values, `IN #FF` releases; `Bit2GatesNmi` (Q3 recommendation): with `7EFD.2 = 1` the NMI request is ignored |
| `core/tests/emulator/config_test.cpp` | `GmxTimingDefaultsEqualScorpion`; `IntSelectPentagonOption` (Q13) |

## 6. Boot (phases 1 and 3, real ROM, may exceed 50 ms)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/machines/gmx/gmx_boot_test.cpp` | `LoaderBannerAppears`: `gmx.rom` runs to the loader text ("Loader" and the DELETE prompt in VRAM; boot-bound, `EnableTurboMode()`, one wait via `TestWait::ForAtLeast` on frames); `ReachesShadowMonitor` without a key press (the base scheme; PC in the monitor plane, its text in VRAM); `RamSizingFindsTwoMegabytes` (the loader's RAM test via `#DFFD` reports 2048K; text check) |
| `gmx_boot_test.cpp` | `SchemeSixShowsTheExtendedScreen` (Q17, only if the test scheme boots): select scheme 6, expect `7EFD` bit 3 set and 25 rows of "SCORPION" in the pages |
| `gmx_boot_test.cpp` | `LoaderReachesThe128MenuInPlane4`: from power-on, `#7EFD` becomes `#C0` (turbo, plane 4) and the 128 menu text is in VRAM |

## 7. Real software (phases 3 and 5, `testdata/machines/gmx/`)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/machines/gmx/gmx_software_test.cpp` | `EdgeGrinderLoaderShowsTheExtendedPicture`: boot `EGGMX.TRD`, wait for `#7EFD = #C8`; pixel pages `#39 / #3B`, picture in `#79`; screen digest of the 640x200 frame |
| same | `EdgeGrinderFlipsScreensEveryFrame` (Q11): on the title screen `#7FFD` bit 3 alternates on consecutive frames and both frames are complete pictures |
| same | `EdgeGrinderRunsAt25Fps` (Q21, long tier): gameplay frame counter (key R) reads 25 |
| same | `OszBootsToNoneCommander` (Q19, Q20): `OS.TRD` in A:, `osz/sdcard/` as a FAT32 SD card; "OS ver 2025.10.08" and the NC panel on the 640x200 screen |
| same | `OszConsoleUsesHardwareScroll` (Q4): `#7AFD/#7CFD` written in multiples of 640 after row 25 |

## 8. Not tested by design

Pentagon and Composit scheme behavior, flash writes, the never-built 320x200 mode.
