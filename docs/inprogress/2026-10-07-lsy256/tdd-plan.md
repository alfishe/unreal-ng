# LSY256: test plan

**Date:** 2026-10-07 · part of [README.md](README.md) · follows [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase of [design.md](design.md) section 9. Each is under 50 ms unless it boots the real ROM (those carry a
comment saying so). No `sleep_for`: use `TestWait::For`. Turbo mode on boot tests. Scratch files via
`TestPathHelper::GetUniqueTestScratchPath()`. File names are `<sourcefile>_test.cpp`.

## 1. Plumbing (phase 1)

| Test file | Test names |
|:--|:--|
| `core/tests/emulator/emulatormanager_test.cpp` | `LsyIsSupportedAndCreatable`; the old loop over unsupported models loses `MM_LSY256` (`KayQuorumPhoenixGmxStayUnsupported`) |
| `core/tests/emulator/config_test.cpp` | `LsyConfigFolderIsLsy256`, `LsyHasRam256Only` |
| `core/tests/emulator/memory/rom_test.cpp` | `LsyRomRolesAreOrdered128_48_Sys_Dos` (synthetic tagged ROM: four pages, each role lands on its page) |
| `core/tests/emulator/ports/models/portdecoder_lsy256_test.cpp` (new) | `ResetLatchIsZero`, `ResetShowsSysRomAtZero`, `LatchWrittenOnLowByte7BAnyHighByte`, `OtherLowBytesDoNotTouchTheLatch`, `LatchIsNotReadable` |
| `core/tests/emulator/ports/portdecoder_porttag_test.cpp` | `LsyModelNameInPortTags` |

## 2. Memory modes (phase 2)

Fixture builds a machine with a synthetic ROM whose pages carry distinct tags, and marks RAM pages 8..15 with their numbers.

| Test file | Test names |
|:--|:--|
| `portdecoder_lsy256_test.cpp` | `Mode00ShowsSysRom`, `ModeEmulShowsRomBy7ffdBit4` (128 / 48), `ModeEmulInDosShowsDosOrSys`, `ModeBlkromDv0Selects12Or13Writable`, `ModeEmulBlkromShowsRam8To11ReadOnly` (a write leaves the page unchanged), `Window3IsPage7ffdPlus8TimesPa3` (all 16 pages), `Bits2And5To7Ignored`, `Window12StayRam5AndRam2`, `ScreenBitPicksPage5Or7`, `LockBitBlocks7ffd`, `Bits6And7Of7ffdDoNotPage` |
| `core/tests/emulator/memory/memory_test.cpp` | `LsyWindowZeroRamIsReportedAsRam` (bank mode), `LsyReadOnlyRamIsWriteProtected` |
| `core/tests/emulator/ports/models/portdecoder_lsy256_test.cpp` | `NmiMapsRam13`: execute `#0066` code from the SYS ROM image and assert window 0 |

## 3. Real ROM boot (phase 2)

| Test file | Test names |
|:--|:--|
| `core/tests/emulator/machines/lsy256/lsy256_boot_test.cpp` (new; boot-bound, justified in a comment) | `SysRomWritesFirstLatchValue10` (the first `OUT` is `#7B` = `#10`), `SysRomRamTestPasses` (the 16 pages), `SysRomReachesItsScreen` (VRAM changes by a `TestWait::For`), `PortTraceShowsWhichModesTheRomUses` (records the set of `#7B` values; documents Q2, no pass/fail on the EMUL bit) |

## 4. Keyboard (phase 3)

| Test file | Test names |
|:--|:--|
| `core/tests/emulator/io/keyboard/keyboard_test.cpp` | `Bk08RowsReadSixKeysAndLayerBit` (a pressed key clears its bit in the right row), `Bk08LayerKeyReadsOnBit7`, `Bk08ZxMatrixUnchangedOnOtherModels` |
| `core/tests/emulator/machines/lsy256/lsy256_boot_test.cpp` | `HostKeyReachesTheMenu` (a key typed through the keyboard manager changes the menu) |

## 5. Snapshots and TTD (phase 4)

| Test file | Test names |
|:--|:--|
| `core/tests/loaders/snapshot/machinestatetransfer_test.cpp` | `LsyTakesA128kState`, `LsyInNonRomModeRefusesToSave`, `LsyToPentagonNeedsMode128`, `LsyPage8To15UsageNeedsSameModel` |
| `core/tests/loaders/snapshot/snapshotcapture_test.cpp` | `LsyViewHas16Banks`, `LsyViewRefusesWhenWindowZeroIsRam` |
| `core/tests/loaders/snapshot/snapshotgolden_test.cpp` | the golden table gains `LSY256` rows; all other rows unchanged (G3) |
| `core/tests/debugger/ttd/ttdlsypaging_test.cpp` (new) | `SaveLoadRoundTrip`, `HashChangesWithLatch`, `WrongIdIsRejected`, `ReplayAfterLatchWriteIsBitExact` |
| `core/tests/debugger/ttd/ttdmachineperipherals_test.cpp` | `LsyListsItsBlob`, `OtherModelsListNoLsyBlob` |

## 6. Automation (phase 5)

| Area | Test names |
|:--|:--|
| WebAPI (`core/automation/webapi/tests/...`) | `StartLsy256Returns200`, `PagingStateHasLsyLatchAndWindowMode`, `OpenApiModelEnumListsLsy256` |
| CLI | `ListModelsMarksLsy256Creatable`, `PagingCommandShowsLatch` |
| MCP | `CreateLsy256ByName`, `PagingResourceHasLatch` |
| Lua / Python | `ModelNameRoundTrip`, `GetLsyLatch` |
| Slots | `LsyMachineRowHasBuses` |
| Fingerprints | `OtherModelsFingerprintsUnchanged` |
