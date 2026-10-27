# ZXM-Phoenix: test plan

**Date:** 2026-10-07 · part of [README.md](README.md) · follows [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase of [design.md](design.md) section 10. Rules for every case:

- under 50 ms; the only exception is the real-ROM boot test, which says why in a comment (it boots a ROM);
- no `sleep_for`: `TestWait::For` / `ForAtLeast` / `ForExactly`;
- `EnableTurboMode()` on boot-bound tests;
- scratch files: `TestPathHelper::GetUniqueTestScratchPath()`;
- test files are `<sourcefile>_test.cpp`; suites are `ClassName_Test` style; no underscores elsewhere;
- ROM-page tests use a **synthetic ROM** whose four 16K pages are tagged (page n filled with the byte `0xA0 + n`), the way
  `modelsregression_test.cpp` does, so no test depends on the real image except the boot test;
- RAM-page tests tag pages by writing a distinct byte to each page's first byte through the decoder's own windows.

## 1. Plumbing and creation (phase 1)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/ports/portdecoder_test.cpp` (existing; add) | `IsModelSupported(MM_PHOENIX)` true; the factory returns a `PortDecoderPhoenix` for `MM_PHOENIX` (type check) and still throws for `MM_KAY`, `MM_QUORUM`, `MM_LSY256`, `MM_GMX` (negative: nothing else became creatable) |
| `core/tests/emulator/emulatormanager_test.cpp` | `PhoenixIsCreatable1024`, `PhoenixIsCreatable2048`; `list_models` row has `creatable: true` and `ram_sizes` 1024 and 2048; a non-Phoenix model is unchanged; `ram_size: 512` is refused (not in `RAM_1024 \| RAM_2048`) |
| `core/tests/emulator/config_test.cpp` (existing) | `PhoenixConfigFolderResolves` (`data/configs/phoenix/unreal.ini` exists and loads); `PhoenixTimingDefaults`: `frame` 71680, `t_line` 224, `intstart` 71635, `intlen` 32 |
| `core/tests/emulator/memory/rom_test.cpp` (new, for `rom.cpp`) | `PhoenixRomPageRoles`: "SYS ROM", "TR-DOS ROM", "128K ROM", "48K BASIC ROM"; `PhoenixRomFilenameIsPhoenixPath` |
| `core/tests/emulator/memory/modelsregression_test.cpp` | `Phoenix_ResetShowsEditorRomAndFixedWindows`: with the tagged ROM, after reset `#0000` = page 2 tag, `#4000` = RAM 5, `#8000` = RAM 2, `#C000` = RAM 0 |
| `core/tests/emulator/video/int_timing_test.cpp` | Phoenix frame length 71680, line 224, INT-to-first-paper distance equal to the Pentagon's (pinned number, comment names the source) |
| `core/tests/emulator/video/screen_test.cpp` (existing) | `PhoenixUsesPentagonRasterMode`; `#EFF7` video bits (`0x01`, `0x02`, `0x10`, `0x40`) do not change the mode (negative for Q4) |

## 2. Ports and paging (phase 1)

File: `core/tests/emulator/ports/models/portdecoderphoenix_test.cpp`, fixture `PortDecoderPhoenix_Test` (new, header
`portdecoderphoenix_test.h`, a 2048K machine by default; `INSTANTIATE_TEST_SUITE_P` over 1024 / 2048 where a case is about
the mask).

| Case | What it checks |
|:--|:--|
| `Out7FFDPagesLowThreeBits` | `OUT #7FFD,n` for n = 0..7: `#C000` shows page n |
| `PageNumberFormulaAllCombinations` | all 128 values of the five paging bits (`#7FFD` bits 0-2, 7; `#1FFD` bits 4, 6, 7), written through the ports with the lock bit clear: page index equals the R11 formula; on 1024K equals the formula `& 63`. One loop, tag compare, no sleeps |
| `Out1FFDBit4IsPageBit3` / `Out7FFDBit7IsPageBit4` / `Out1FFDBit7IsPageBit5` / `Out1FFDBit6IsPageBit6` | one named case per bit, so a swap (Q2) fails one readable test and the comment cites U, X, P |
| `Page1024DropsBit6` | 1024K: `#1FFD` bit 6 changes nothing; 2048K: it selects pages 64-127 |
| `FixedWindows` | `#4000` always page 5, `#8000` always page 2 under every paging write |
| `ScreenBitSelectsPage7` | `#7FFD` bit 3 -> screen page 7 else 5, with `#1FFD` bits 6, 7 set the screen page is still 5 / 7 |
| `LockBlocksSecond7FFD` | after `#7FFD` bit 5, a second `#7FFD` write changes neither the page, the ROM nor the screen bit |
| `LockDoesNotBlock1FFD` | after the lock, `#1FFD` still pages |
| `ResetClearsLatches` | `p7FFD`, `p1FFD`, `pEFF7` = 0 after reset, including after a locked state |
| `Decode7FFD` | A15 = 0, A14 = 1, A1 = 0, A0 = 1 (`#7FFD`, `#7DFD`, `#5FFD`) page; `#FFFD`, `#3FFD`, `#7FFC`, `#7FFF` do not (negative) |
| `Decode1FFD` | `#1FFD`, `#3FFD`, `#0001`-form (A15 = 0, A14 = 0, A1 = 0, A0 = 1) page; `#5FFD`, `#9FFD`, `#1FFC` do not |
| `DecodeEFF7Full` | `#EFF7` stores the byte; `#EEF7`, `#EFF6`, `#FFF7` do not |
| `EFF7ReadsBack` | `IN #EFF7` returns the latch (or the floating bus; the case pins what the implementation decides, see Q4 in TODO) |
| `EFF7Bit7OpensBetaPorts` | with the 48 ROM paged (no TR-DOS session) `IN #1F`-class Beta port reads the FDC status after `OUT #EFF7,#80`, and the Kempston joystick before it; clearing bit 7 restores it |
| `EFF7OtherBitsChangeNothing` | `0x7F` written: banks, ROM and video mode identical to before |
| `Version00F7ReadsZero` | `IN #00F7` = 0 (named after Q6 so it is easy to remove) |
| `JoystickAndMouse` | `IN #1F`; `IN #FADF` / `#FBDF` / `#FFDF` return the Kempston mouse registers, also with a TR-DOS session active |
| `AyPortsDecode` | `#BFFD` / `#FFFD` reach the AY as on the Pentagon |
| `FloatingBusOnUnclaimedPorts` | an unclaimed port reads the same as the Pentagon decoder in paper and border |
| `PortCollisionSweep` | all 65 536 port values x read / write: each port reaches at most one device; the IDE adapter `SchemeFits` for NEMO / DIVIDE on Phoenix and does not collide |

### 2.1 ROM selection

File: `portdecoderphoenix_test.cpp`, fixture `PhoenixRom_Test`.

| Case | What it checks |
|:--|:--|
| `RomTableAllCombinations` | all 16 combinations of `#1FFD` bits 0, 1, 3 x `dosActive` x `#7FFD` bit 4 (the table of design 2.3); `#0000` shows the expected tag; one table-driven test with the row index in the failure message |
| `Rom1FFDBit0MapsWritableRam` | a write at `#0000` lands in RAM page 0 and reads back; with bit 0 clear the write is discarded |
| `RomPage0IsErasedInRealImage` | (real image, in the boot test file) page 0 reads `#FF` across 16K |
| `TrdosSessionKeepsTrdosPageWithBit4Clear` | with `CF_TRDOS` raised and `#7FFD` bit 4 = 0 the ROM is page 1 (the Xpeccy rule; named so Q9 can flip it) |
| `TrdosEntryFrom48Rom` | `#3D13` from 48 BASIC raises `CF_TRDOS` and shows page 1; leaving ends the session |

## 3. Video and timing (phase 1)

Covered in section 1 (`int_timing_test.cpp`, `screen_test.cpp`). A frame-length smoke case in
`core/tests/emulator/video/screenzxframes_test.cpp` (existing): the Phoenix frame equals 71680 T and the paper start matches
the Pentagon's.

## 4. Real-ROM boot (phase 1)

File: `core/tests/emulator/phoenix_boot_test.cpp` (new, next to `profi3_boot_test.cpp`). Boot-bound: it runs the real image through its
reset to the 128K menu, which costs more than 50 ms; the file header says so, and `EnableTurboMode()` is used.

| Case | What it checks |
|:--|:--|
| `BootsTo128Menu` | the 128K menu text is in VRAM (`TestWait::For` on the screen bytes); PC is in ROM page 2 |
| `Menu48BasicStarts` | selecting "48 BASIC": ROM page 3, `(c) 1982` screen |
| `TrdosEntersAndShowsCatalog` | with a mounted TRD fixture from `testdata/`, `RANDOMIZE USR 15616` (via the keyboard helper) then `CAT`: the directory listing is on screen |
| `ExtendedPagingProgram` | a 20-byte program in RAM writes a tag to every one of the 64 pages through the five bits and verifies them back; the result byte in RAM is `0` |

## 5. Devices (phase 3)

| Test file | Cases |
|:--|:--|
| `portdecoderphoenix_test.cpp` | RTC (if Q5 is taken): `CMOS=DALLAS` -> `#DFF7` / `#BFF7` reach the clock; `CMOS=NONE` (the Phoenix default) they do not (negative) |
| `core/tests/emulator/ports/models/kempston_mouse_decode_test.cpp` (existing) | add `MM_PHOENIX` to the decode table: exact ports, the mouse visible in a TR-DOS session |
| `core/tests/emulator/io/ide/idecontroller_test.cpp` (existing) | `SchemeFits` for Phoenix; the 65 536-port sweep with the Phoenix decoder (as the Profi sweep) |

## 6. TTD (phase 2)

| Test file | Cases |
|:--|:--|
| `core/tests/debugger/ttd/phoenix/ttdphoenixpaging_test.cpp` (new) | `StateSizeIsFourBytes`; `RoundTripKeepsP1FFD` (write, capture, clear, restore: `p1FFD` and the four windows equal); `RestoreRemapsBanks` (restore with `#1FFD` = 1 shows RAM 0 at `#0000`); `UnknownVersionIsRejected` (negative); `DeviceNameAndId` (62, "phoenix-paging") |
| `core/tests/debugger/ttd/ttdmodelstatecontract_test.cpp` | Phoenix: every id from `GetTTDModelStateIds()` has a serializer (the contract test is parameterized over models; add `MM_PHOENIX`) |
| `core/tests/debugger/ttd/phoenix/phoenixttd_replay_test.cpp` (new) | `ReplayAfterPagingWritesIsExact`: record 2 frames while a RAM program changes `#1FFD` / `#7FFD` / `#EFF7`, seek back, step forward: machine state hash equal frame by frame; `CheckpointRefusesOtherModelAndOtherRam` (1024K checkpoint into 2048K and into `PENTAGON`: refused with the reason) |
| `core/tests/debugger/ttd/machinestatehash_test.cpp` | `PhoenixHashIncludesP1FFD`: changing only `p1FFD` changes the hash |

No Phoenix TTD fixture exists, so no existing fixture is re-recorded; a new one is not needed (the tests build their sessions).

## 7. Snapshots and state transfer (phase 2)

| Test file | Cases |
|:--|:--|
| `core/tests/loaders/snapshot/machinestatetransfer_test.cpp` | `PhoenixPlain128IntoPentagon` (Mode128: allowed); `Pentagon128IntoPhoenix` (allowed, `EnterSpectrum128Paging` sets `p1FFD` = 0); `PhoenixExtendedIntoPentagonIsRefused` (reason mentions Phoenix extended paging); `Phoenix2048IntoPhoenix1024IsRefused` (RAM size); `PhoenixIntoTsConfIsRefused` (negative, unchanged rule) |
| `core/tests/loaders/snapshot/snapshotcapture_test.cpp` (new) | `PhoenixPlain128HasAView` (8 banks, `machineHint` as agreed with the WindowMap policy); `PhoenixWith1FFDSetHasNoView` (`capture_unsupported`, reason text); `PhoenixRoundTripThroughSna` (save a plain 128K state as `.sna`, load into a fresh Phoenix, banks equal) |
| `core/tests/loaders/snapshot/szx/loaderszx_test.cpp` | an SZX of machine id Pentagon 1024 loads into a Phoenix only through the 128K layout: refused with the model-mismatch reason (no silent mapping of Pentagon `#7FFD` bits onto Phoenix bits) |

## 8. Slots (phase 4)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/slots/slotplanner_test.cpp` (existing) | Phoenix row present: bus type NemoBus v1.1m; the planner lists the cards for it; a card that needs a signal the board lacks is rejected with the reason |

## 9. Automation and UI (phases 1-3)

| Area | Cases |
|:--|:--|
| WebAPI | `tools/verification/webapi/src/test_api_phoenix.py` (new, live server, in the pattern of `test_api_state.py`): `POST /emulator/start {"model":"PHOENIX"}` -> 200, `ram_kb` 1024; `ram_size` 2048 -> 200; `GET /models` lists it `creatable: true`; `GET .../state/paging` reports `p1FFD`, `ram_page` (7-bit), `rom_page` and `rom_role`; the OpenAPI enum contains `PHOENIX` (spec test) |
| CLI | model-name round trip (`create PHOENIX`), `state paging` text contains the `#1FFD` row, `ttd start` / `ttd status` on a Phoenix |
| MCP | `core/tests/automation/mcp-tools-test.cpp`: the `model` description lists `PHOENIX` as creatable and `emulator_manage create PHOENIX` is accepted; live: `emulator_manage create PHOENIX`; `inspect_state memory` returns the banks; `time_travel start` works (the TTD contract test above covers the blob) |
| Lua, Python | `get_paging()` returns the Phoenix fields; model-name constants include it |
| Qt | no menu test exists for models; the entry is checked in the recipe's live run (screenshot of the Machine menu and the created machine) |
| Recipe | `.recipe/machines/phoenix.md` commands run verbatim in the live app on every phase's last step (manual, recorded in the recipe as "Verified: date, build") |

## 10. Cross-model guards (every phase)

- `modelsregression_test.cpp`: every other model's ROM and RAM windows unchanged after the `Memory::UpdateZ80Banks` edit.
- The other machines' model fingerprints unchanged (G3).
- Full `core-tests` green; gcc:16 -O3 compile of new C++ (docker/linux).
