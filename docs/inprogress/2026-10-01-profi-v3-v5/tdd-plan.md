# Profi v3 / v5: test plan

**Date:** 2026-10-01 · part of [README.md](README.md) · follows [core/tests/README.md](../../../core/tests/README.md)

Tests come first in each phase of [design.md](design.md) section 8. Every test runs in under 50 ms unless it
boots a real ROM. No test sleeps. Scratch files use `TestPathHelper::GetUniqueTestScratchPath()`. Real-ROM tests
use the images in `data/rom/profi/` (tracked), never the untracked `testdata/machines/profi/rom/`.

## 1. Board profile and model plumbing (phase 1)

| Test file | Cases |
|:--|:--|
| `core/tests/emulator/ports/models/profiboard_test.cpp` | `ProfiBoard::For(MM_PROFI3)` and `For(MM_PROFI)`: each field (negative: every other model has no Profi board) |
| `core/tests/emulator/emulatormanager_test.cpp` | `PROFI3` creatable; `list_models` has it and the `PROFI5` alias; a non-Profi model is unchanged |
| `core/tests/emulator/memory/modelsregression_test.cpp` | `MM_PROFI3` with the synthetic tagged ROM: SYS at reset, the ROM role names, 512K wraps page 32 to page 0, 1024K does not |
| `core/tests/emulator/machines/profi/profi3_boot_test.cpp` (new, real ROM) | `kramis-v02.rom` and `kramis-v03.rom` reach the Kramis menu (its text in VRAM); the menu's TR-DOS, 128 and 48 entries each start (PC in the expected ROM, the editor or `(c) 1982` screen). The test does not assume any v5 thing: no hi-res, no palette |
| `profi_boot_test.cpp`, `profi_hdd_test.cpp` | unchanged and green: G3 |

## 2. Ports (phase 2)

Fixture: `profifixture.h` gets a `board` parameter, so every port test runs on both boards
(`INSTANTIATE_TEST_SUITE_P` over v3 / v5); a test that is about one board says so in its name.

| Test file | Cases |
|:--|:--|
| `portdecoder_profi_test.cpp` | **v3**: CP/M with ROM14 = 0 and = 1 both give the FDC at `#1F..#7F` and the system port at `#BF`; `#83/#A3/#C3/#E3`, `#3F` (as system), `#9F/#BF/#DF/#FF` (as RTC) are undecoded; a palette write (`OUT #xx7E` in DS80) changes no palette entry; `IN #FE` bit 7 = 1 in DS80. **v5**: today's cases unchanged. **Both**: `IN #DFFD` does not read the AY (A13); `IN #1F` outside DOS / CP/M is the joystick |
| `portdecoder_profi_prom_test.cpp` (new) | the PROM tables in `testdata/machines/profi/decoder/` (`556rt4-v3.2.bin` for v3, `556rt4-v4-v5.bin` for v5), read through the wiring in [decoder-prom.md](decoder-prom.md), against `PortDecoder_Profi` for 256 low bytes x CP/M x ROM14 (v3: A15) x DOS latch x read / write: the same device or none. Named case: CP/M with the DOS latch on decodes the CP/M map (CP/M forces the PROM's A2 to 1) |
| `profi_covox_test.cpp` | v3: no `#C7`/`#A7` aliases; `#5F`/`#3F` in NORMAL mode on both |
| `ideadapter_test.cpp`, `idecontroller_test.cpp` | v3 with `[HDD] Scheme=PROFI`: the adapter stays off and logs once (negative); v5 unchanged |
| `portdecoder_portmap_test.cpp`, `portdecoder_porttag_test.cpp` | the v3 port map lists no extended rows, RTC or IDE; model name "Profi v3" |

## 3. Video (phases 1 and 3)

| Test file | Cases |
|:--|:--|
| `profi_video_test.cpp` | v3 DS80: monochrome whatever the attribute page holds (and whatever `ProfiMonochrome` says); v5 with `ProfiMonochrome=1`: monochrome, the v4.02 colour case |
| `int_timing_test.cpp`, `screenzxframes_test.cpp` | every `SyncProm=` row on both boards: frame length, line length and the INT-to-first-paper distance equal the table in [design.md](design.md) 5.1 (pinned numbers, each with its PROM and decode line in a comment); the per-board defaults; an unknown `SyncProm=` value falls back to the board default with a log line (negative) |

## 3b. v5 video WAIT (phase 3b)

| Test file | Cases |
|:--|:--|
| `profivideowaitoverlay_test.cpp` (new) | v5 standard mode, `WaitConfig=profi`: a RAM access in the paper waits for the slot (the E4 pattern, case per phase), in the border it waits less, a ROM access never waits; `WaitConfig=pentagon`: no overlay installed; v3: never installed; DS80 = 1: not installed; the `contention` feature off: not installed |
| emulated test programs | floatspy and TEST 4.30 timing pages on v5 reproduce the readings quoted in [cross-check.md](cross-check.md) 4.4 / T4 (69888 per frame) |

## 4. Floating bus (phase 4)

| Test file | Cases |
|:--|:--|
| `profi_floatbus_test.cpp` (new) | v3: an undecoded `IN` during the paper returns the byte at the beam position (the byte is put in VRAM first, then read at a computed T-state); in the border it returns `#FF`. v5: always `#FF`. In DOS / CP/M: as Q3 settles |

## 5. Turbo (phase 5)

Pattern: `scorpionturbooverlay_test.cpp`.

| Test file | Cases |
|:--|:--|
| `profiturbooverlay_test.cpp` (new) | switch off: no overlay is installed, and instruction lengths are the 3.5 MHz ones. Switch on: `hw_turbo_ratio` = 2, ROM code runs without waits, and a NOP stream in RAM takes the rule's length at each phase. Each figure in design 6.2 step 3 is a named case, with its tolerance and its source in a comment |
| `profiturbo_ttd_test.cpp` (new) | turbo switched mid-recording: replay is bit-exact, the switch event is on the track |

## 6. Automation and TTD (phase 6)

| Area | Cases |
|:--|:--|
| WebAPI | `POST /emulator/start {"model":"PROFI3"}` -> 200; `/state/paging` reports the board; the OpenAPI enum lists `PROFI3` |
| CLI, Lua, Python | model-name round trip; the turbo switch verb and its state readback |
| TTD | record and replay a v3 boot to the menu: replay is bit-exact, and the checkpoint refuses to load into `PROFI` |
| Fingerprints | the other machines' model fingerprints unchanged (G4) |
