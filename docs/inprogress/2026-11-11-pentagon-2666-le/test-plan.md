# Pentagon 2.666 LE: test plan

Part of [README.md](README.md). Facts: [hardware-reference.md](hardware-reference.md) (HR §n). Design:
[design.md](design.md).

Rules from `core/tests/README.md`: about 10 ms per test, integration under 1 s, longer variants behind
`TestTier::Long()`; `TestWait::For`, never `sleep_for`; `EnableTurboMode()` on boot-bound tests that do not look at
pixels; scratch files through `TestPathHelper::GetUniqueTestScratchPath()`. Tests are written before the code of
their phase; each test name says which HR row it checks.

## 1. Layers and oracles

| Layer | What | Oracle |
|---|---|---|
| L1 decoder unit | one port write / read on a bare decoder + memory, check state and the mapped page | the tables in HR §2-§6 (each row one test) |
| L2 CPU programs | short Z80 programs on the full machine (no ROM, code in RAM), check registers / memory / port trace | HR tables; the VHDL co-simulation for timing (§6) |
| L3 firmware boot | the real ROM set: Gluk menu, TR-DOS, FATALL, NedoOS | screen hashes / OCR of known screens; file lists |
| L4 automation | WebAPI / MCP live, the recipe | `tools/verification/mcp/run_all.py` block, the recipe walk-through |
| L5 cross-check | ghdl co-simulation of the upstream VHDL | per-frame trace diff (§6) |

New files (no underscores except `_test.cpp`):
`core/tests/emulator/ports/models/portdecoderp2666_test.cpp`, `core/tests/emulator/memory/p2666paging_test.cpp`,
`core/tests/emulator/video/p2666_video_test.cpp`, `core/tests/machines/p2666/p2666_boot_test.cpp`,
`core/tests/emulator/io/p2666devices_test.cpp`.

## 2. L1: decoder (T-P, T-M, T-D)

### T-P: port decoding

| ID | Check |
|---|---|
| T-P1 | `7FFD` decoded by `A15 = 0`, `A1 = 0`, `A2 = 1`; `FFFD` and `BFFD` do not hit it (D-1) |
| T-P2 | `EFF7` decode mask (`A15..13 = 111`, `A12 = 0`, `A5 = 1`, `A3 = 0`, `A0 = 1`): `EFF7`, `EFD7`... hit; `FFF7`, `EF77` miss |
| T-P3 | every internal write ignored when `A2 = A1 = 0` (goes to IDE) |
| T-P4 | `xx77` in DOS -> ATM register + `cpm := A9` + `pen2 := A14` **and** WD data register; out of DOS -> SD config only |
| T-P5 | `xx57`: data out of DOS; in DOS data only with `A15 = 0`, config with `A15 = 1` |
| T-P6 | `xxFF` palette write gating: DOS off, or DOS on with `pen2 = 0`; never with `EFF7.2 = 1`; in DOS also the BDI system register |
| T-P7 | `xFF7` / `x7F7` only in DOS, full decode (`3FF7` hits, `3FF6`, `3EF7` miss) |
| T-P8 | BDI ports only in DOS; reads by `A7..5` (status at `0x1F` and at `0x0F` alike) |
| T-P9 | read priority: an address matching two rows returns the higher one (e.g. `0x57` in DOS reads SPI, not WD data; `0x1F` in DOS reads WD status, out of DOS the joystick) |
| T-P10 | `FE` read bit 5: INT flag with `EFF7.2 = 0`, 1 with `EFF7.2 = 1`; bit 7 = 1; bit 6 tape |
| T-P11 | `OUT (FE), 0x20`: `EFF7.2 = 0` clears the flag and sets border 0; `EFF7.2 = 1` changes nothing (D-2) |
| T-P12 | `border3 = NOT A3`: `OUT (0xF6)` gives bright border colour |
| T-P13 | TurboSound select: `FFFD := 0xFE` -> chip 1, `0xFF` -> chip 0; read `FFFD` from the selected chip; reset -> chip 0 |
| T-P14 | Covox `xxFB` value reaches the mixer, cleared by reset |
| T-P15 | UART registers at `F8EF..FFEF` by `A10..8` |
| T-P16 | unclaimed read returns `0xFF`; the SD config read (`xx77`) returns `0xFF` (D-4) |

### T-M: memory and dispatcher

| ID | Check |
|---|---|
| T-M1 | reset: windows 1..3 = pages 5, 2, `7FFD` page; window 0 = Gluk (DOS on, `7FFD.4 = 0`) in both maps |
| T-M2 | window 0 ROM page by (DOS, `7FFD.4`): Gluk / TR-DOS / BASIC 128 / BASIC 48 = `0x48` / `0x49` / `0x4A` / `0x4B`; `EFF7.3` -> RAM page 0 |
| T-M3 | Pentagon-1024 page formula for all 64 values of bits 7, 6, 5, 2..0 |
| T-M4 | 128 mode: lock honoured out of DOS, ignored in DOS; bits 7..6 keep their value; locked bit 5 maps page `32 + n` (D-3) |
| T-M5 | `xFF7` RAM: page `NOT v & 0x3F` per window; `xFF7` ROM: `0x48 + 2 v0 + v1` (bit swap) |
| T-M6 | `x7F7`: page `NOT v & 0x7F`, switches the window to RAM / dispatcher mode |
| T-M7 | two maps: writes change only the map selected by `7FFD.4`; switching `7FFD.4` switches all four windows |
| T-M8 | `xxBE` readback for every window / map after `xFF7` and after `x7F7` writes (bit 6 lost after `x7F7`) |
| T-M9 | pages `0x48..0x4C` are read-only through every mapping; `0x4D` and `0x4E` are writable |
| T-M10 | power-on upload: ROM images at `0x48..0x4C`, logo in page 5, palette defaults; CPU reset keeps RAM and palette |

### T-D: DOS machine

| ID | Check |
|---|---|
| T-D1 | reset -> DOS on; with `cpm = 0` DOS stays on after a jump to `8000` |
| T-D2 | `OUT` to `xx77` with `A9 = 1` (`cpm = 1`), then an M1 at `>= 4000` -> DOS off |
| T-D3 | M1 at `3D00..3DFF` with `7FFD.4 = 1` -> DOS on; with `7FFD.4 = 0` no change; M1 at `3C00` / `3E00` no change |
| T-D4 | the trap works when window 0 holds a RAM page (address-only check) |
| T-D5 | `BF.0 = 1` keeps DOS on in RAM; `BF.0 = 0` + `cpm = 1` releases it |
| T-D6 | `dos1` is taken from the last I/O cycle: `OUT (BF), 1` followed by a jump to RAM keeps DOS on even though `BF` was written out of DOS |

## 3. L2: CPU programs (T-C, T-V, T-S)

### T-C: clock and INT

| ID | Check |
|---|---|
| T-C1 | ratio from (`xx77.3`, `EFF7.4`): (1, x) = 8, (0, 0) = 4, (0, 1) = 1; power-on = 8 |
| T-C2 | `NOP` loop T-state count per frame at ratio 1 / 4 / 8 = 71680 x ratio |
| T-C3 | I/O slowdown (design §6): `OUT (C), A` (fetch 4 + 4, I/O cycle 4) costs `8 + 4r` turbo T-states: 12 at ratio 1, 24 at ratio 4, 40 at ratio 8; `IN A, (n)` (4 + 3 + I/O 4) costs `7 + 4r`; memory cycles are not slowed. Cross-checked by the co-simulation (the T80 `IOcycle` window may cover more than the 4 I/O T-states) |
| T-C4 | frame = 71680 base T; INT length 32 CPU clocks at each ratio |
| T-C5 | INT acknowledge reads `0xFF` (IM 2 through table `xxFF`) |
| T-C6 | UART interrupt holds INT low until its flags clear (P4) |

### T-V: video

| ID | Check |
|---|---|
| T-V1 | mode detection for every (`xx77[2:0]`, `EFF7.0`) pair, including the undefined ones: they keep the last colour (D-8) |
| T-V2 | each mode renders a synthetic page set to a known framebuffer hash: ZX, Pentagon 16c, ATM 320 x 200, 640 x 200, text 80 x 25 with the P2.666 font; screen 0 and 1 by `7FFD.3` |
| T-V3 | INT position calibration: a border-stripe program (`OUT (FE)` every line from the INT) and a multicolor program (attribute rewrite every 8 lines) give the same picture as the co-simulation; the result fixes `intstart` |
| T-V4 | palette: write all 16 entries with chosen `A15..8 : D7..0`, check the 4-bit RGB per HR §6.2; defaults equal the decoded `pollitra.hex` |
| T-V5 | palette index = current border colour (`border3` included); writes refused in 128 mode |
| T-V6 | border 16 colours in every mode; ATM paper area 320 x 200 placed per design §7; framebuffer = the board's visible window (387 x 295.5 ZX pixels, HR §1.3), nothing outside it |
| T-V7 | FLASH period 32 frames (16 per phase) |

### T-S: sound

| ID | Check |
|---|---|
| T-S1 | AY clock 1,838,710 Hz: register 0/1 period N gives a tone of `clock / (16 N)` in the mixer output |
| T-S2 | TurboSound: both chips audible, independent registers |
| T-S3 | stereo as the VHDL: A only on the right, C only on the left, B on both (D-6) |
| T-S4 | beeper / tape-out `+50` / `+100`; the 8-bit sum wraps (two chips at volume 15 + beeper overflow to a low value), as the board (D-11) |

## 4. L1/L2: devices (T-I, T-B)

| ID | Check |
|---|---|
| T-I1 | SD: CMD0 / CMD8 / ACMD41 / CMD17 through `xx57` / `xx77` out of DOS and through `xx57` + `A15` in DOS; a read returns the previous byte and clocks `0xFF` |
| T-I2 | Nemo IDE as the board (D-5): writes to `0x10..0xF0` / `0xC8` reach the drive; reads of even registers (`0xF0` status, `0x10` data) return the `FE` value; `0x11` reads the high data latch out of DOS and WD status in DOS. Named `IdeEvenReadsReturnFe`, flips when the commented `P2666-FIX D-5` is enabled |
| T-I3 | CMOS: `EFF7.7 = 0` blocks `DFF7` / `BFF7`; cells `0A..0D`, `11` constants; time cells BCD from the injected clock; a write to a time cell updates the clock; 64 cells persist across power cycle |
| T-I4 | `BFF7` in DOS with `EFF7.7 = 1` writes CMOS and window 2 |
| T-I5 | Kempston mouse: buttons, X, Y, wheel nibble; `0xFF` without a mouse |
| T-I6 | Kempston joystick at `xx1F` out of DOS only, bit order as the MCU sends its pins (D-7) |
| T-B1 | path A: TR-DOS `CAT` of a TRD in drive A; `RUN "boot"` |
| T-B2 | path B: insert a TRD -> bytes at page `0x76` downwards in the HR §5.6 layout; read sector / write sector / multi-sector; read address bytes; status bits; eject writes back |
| T-B3 | path B: drives B..D give no data; `FORMAT` on drive A writes raw sectors (documented behaviour) |
| T-B4 | path B: Gluk drive D (the RAM disk) and BDI drive A see the same files |
| T-B5 | path B: CPU stall per byte and the 0.56 s restore delay, against the co-simulation (long tier) |

## 5. L3: firmware and software

| ID | Check | Media |
|---|---|---|
| T-F1 | power-on reaches the "MR GLUK RESET SERVICE" menu (screen hash or text) within N frames | ROM set |
| T-F2 | Gluk user message: CMOS `12 = 'G'` + text in `13..2E` replaces the title | CMOS preset |
| T-F3 | from Gluk: BASIC 128, BASIC 48, TR-DOS each start | ROM set |
| T-F4 | key `F` in Gluk starts FATALL; it lists a FAT32 SD image; on the FAT32 IDE image it fails as the board's IDE reads do (witness of D-5, long tier) | `testdata/machines/p2666/sd-fat32.img`, `ide-fat32.img` (made by the existing FAT test helpers) |
| T-F5 | Gluk key `5` boots NedoOS from SD (`osp26sd.$c` renamed to `sd_boot.$c`, manual §5.1) to its shell | NedoOS release build for P2.666 in `testdata/machines/p2666/nedoos/` (to be fetched: the repo has only the Evo `SD_BOOT.$C`) |
| T-F6 | NedoOS uses the 2 MB (`x7F7`) and an ATM video mode (its text or 16c mode) without errors | same |
| T-F7 | ATM / Pentagon software sample: a Pentagon 16c demo, an ATM 320 x 200 picture viewer, a multicolor game run as on Pentagon-1024 | catalog picks |
| T-F8 | 128K software in 128 mode (`EFF7.2 = 1`) and 48K lock | a few `.trd` / `.tap` |

Boot tests use `EnableTurboMode()` and stop as soon as the screen matches; full variants (NedoOS, FATALL on IDE)
behind `TestTier::Long()`.

## 6. L5: VHDL co-simulation (POC)

A POC under `tools/poc/NNN-p2666-vhdl-oracle/` (next free number, registered in `tools/poc/README.md`, charter
first):

- **Goal**: per-frame traces from the upstream VHDL to compare with the emulator for timing and video.
- **Build**: `ghdl` with `PaE.vhd`, `zx_main.vhd`, `betadisk.vhd`, `YM2149_linmix.vhd`, `uart.vhd`, T80; stubs for
  `altpll` (two clocks, `mainclk` 114 MHz model time) and `altsyncram` (behavioural, `.hex` init); a behavioural
  2 MB SRAM; the MCU replaced by a testbench that preloads RAM pages (ROMs, test program) and releases reset;
  the I2C keyboard / mouse bytes driven by the testbench.
- **Output**: CSV per event: `mainclk` cycle, CPU address, physical page, I/O port and data, INT edges, and a
  dump of the 15-bit video stream per frame (PNG).
- **Acceptance**: the trace of a 2-frame `NOP` program matches the emulator's frame and INT numbers; then T-V3,
  T-C3 and T-B5 are decided by it.
- **Constraint**: upstream sources stay in the worktree's `scratch/`, never copied into the repo; only our
  stubs and scripts are committed.

## 7. L4: automation and recipe

- MCP live block `tools/verification/mcp/` for the new model: create the model, run to the Gluk menu, read the
  `p2666` device state, switch the ATM video mode from a test program, screenshot, check `content[]` text.
- WebAPI: `GET /api/v1/emulator/models` lists `P2666`; create, load a TRD into `fdd.a`, insert an SD image, read
  the paging state, TTD seek back over a dispatcher write and check the windows.
- `.recipe/machines/pentagon-2666.md` walked through end to end.
- Snapshot capture: `.sna` / `.z80` offered only in the plain Pentagon subset; refused (with a reason) otherwise.
- TTD round trip of `P2666State` (byte-exact blob), seek across `xx77` / `x7F7` / palette writes.

## 8. Before each landing

| Phase | Checks |
|---|---|
| any C++ | `tools/build/build.sh` zero warnings; `tools/build/test.sh --gtest_filter='*P2666*'` per sub-task; one full `tools/build/test.sh` before the land; `docker/linux/build.sh --test` |
| P5 automation | the above + `tools/verification/mcp/run_all.py --launch <unreal-qt>` |
| docs only | `python3 tools/fix-absolute-paths.py --path <files>` |
