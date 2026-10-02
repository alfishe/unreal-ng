# Sprinter Sp2000 — test plan

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): loader end-of-load and configuration-module tests (§2.3), BIOS probe of `ide0.slave` (T-IDE-8) |
| **Rules** | [core/tests/README.md](../../../core/tests/README.md): CUT pattern, `TestWait`, under 50 ms per test (boot-bound tests say why in a comment), `EnableTurboMode()` on boot-bound tests that do not check pixels, scratch files per process |
| **Inputs** | [goals-and-requirements.md](goals-and-requirements.md) (ACC-*), the `tdd-*.md` files |

## 1. Layers

| Layer | What it proves | Needs ROM? | Budget |
|---|---|---|---|
| L1 unit truth tables | port lookup, code semantics, bank formula, latches, density, CBL, accelerator, SIO | no | < 5 ms each |
| L2 component | renderer on hand-built VRAM, INT list, IDE over `MemoryDisk`, raw PC loader round trip, folder boot profile bytes | no | < 50 ms |
| L3 firmware | BIOS milestones, DSS boots, Spectrum mode, native programs | yes (skip if absent) | boot-bound: up to ~2 s with fast start + turbo mode, justified in a comment |
| L4 differential (tooling, not CI) | port traces and frames vs MAME on the same ROM and media | yes | offline |
| L5 TTD / automation | replay determinism, surfaces | yes for replay | < 50 ms for surface smoke; replay test justified |

## 2. L1/L2 tests by area

File names follow the source files (`<sourcefile>_test.cpp`), under the mirrored `core/tests/`
folders.

### 2.1 Port lookup — `portdecoder_sprinter_test.cpp` (T-DCP)

| ID | Case | Expected |
|---|---|---|
| T-DCP-1 | index of `#7785`, write, DOS on, PN5 0, map 0 | `#009D` (MAN p. 28) |
| T-DCP-2 | each index bit toggled alone (CNF0, CNF1, PN5, /DOS, /WR, A15, A14, A6, A5, A13, A7, A2, A1, A0) | exactly one index bit changes; A3, A4, A8-A12 change nothing |
| T-DCP-3 | `OUT (#BC),A` with A = `#21` / `#01` on the reference table | code `#2B` / `#2A` |
| T-DCP-4 | writes before the first IN | ignored; after `IN`, applied |
| T-DCP-5 | Z84C15 ports `#10-#1F`, `#EE`, `#EF`, `#F0`, `#F1`, `#F4` with any high byte | never reach the table on reads |
| T-DCP-6 | `#FB` / `#7B` reads | fast RAM on / off before the lookup |
| T-DCP-7 | reference table (from BIOS-TT `DCP.ASM`) loaded into page `#40`: every row of HW §4.4 | the listed code |
| T-DCP-8 | a program edits one table byte | the next access uses the new code |

### 2.2 Cells and banks — `sprintermemory_test.cpp`, `portdecoder_sprinter_test.cpp` (T-MEM)

| ID | Case | Expected |
|---|---|---|
| T-MEM-1 | `#7FFD` clean rules for CNF bits 5/7 | masks of tdd-ports-memory §4 |
| T-MEM-2 | `#F0-#FF` writes land in `cells[pg3]` | window 3 follows `#7FFD` page |
| T-MEM-3 | window-0 truth table (romOff × cacheOn × ramSys × `#1FFD` b0/b1 × `#7FFD` b4 × DOS × arom16) | page and writability generated from MAME's formula (a table committed with the test) |
| T-MEM-4 | graphics page write with bit 3 = 1 and value `#FF` | nothing written |
| T-MEM-5 | graphics page with bit 2 = 1 | VRAM written, main RAM unchanged |
| T-MEM-6 | graphics page read | main RAM at `#50 base + PORT_Y × 1024 + (A & #3FF)` |
| T-MEM-7 | Spectrum shadow address for `#4000`, `#57FF`, `#C000` with page 7, RGADR odd/even | tdd-ports-memory §5.3 formula |
| T-MEM-8 | write to page `#A0` with `#1FFD` = `#10` | soft reset requested; with other values, a normal write |
| T-MEM-9 | window 3 = `#D2` with `#1FFD` bit 4 | reads `#FF`, writes ignored (ISA stub) |
| T-MEM-10 | turbo wait rule `SprinterWaits::ExtraClocks` | extra clocks for t mod 6 = 0..5, RAM vs port vs fast RAM; banks without the wait flag cost nothing |

### 2.3 Configuration loader — `sprinterpldconfig_test.cpp` (T-CFG)

| ID | Case | Expected |
|---|---|---|
| T-CFG-1 | the full bitstream write count (constant from the S0 loader trace) while loading | configured, CPU reset, window 3 = `#40`, starting = 1; not configured after 4 096 writes |
| T-CFG-2 | a stream whose first 4 096 writes match MAME's Game constant, no Game module registered | Standard active, warning with both hashes logged |
| T-CFG-3 | fast start vs full start (ROM-gated, 3.04 and 3.06) | identical PLD state and RAM at the first BIOS instruction |
| T-CFG-4 | code `#2E` | back to loading; fast RAM kept; module chosen again after the load |
| T-CFG-5 | a load that stops before the count | watchdog ends it: Standard active, warning logged, CPU reset |
| T-CFG-6 | both hashes over the BIOS-PP `SP2K_304.BIN` bitstream | equal the values recorded in S0 |

Configuration modules — `sprinterpldconfiguration_test.cpp` (T-PLDM):

| ID | Case | Expected |
|---|---|---|
| T-PLDM-1 | registry lookup by full hash, by head hash only, and an unknown stream | the matching module; unknown → Standard + warning with the hash |
| T-PLDM-2 | a stub test module that overrides only the renderer, activated by its hash | `ScreenStub` draws; port codes, mapping and accelerator are still Standard |
| T-PLDM-3 | stub module with state, TTD checkpoint, restore into a fresh machine | same module name and state blob after restore; frame hash equal |
| T-PLDM-4 | reset and reload (`#2E`) with the stub module active | `OnReset` called; after the reload the lookup runs again |
| T-PLDM-5 | Standard through the interface | every Standard hook is reached through `SprinterPldConfiguration` (no direct calls from the decoder) |

### 2.4 Video — `screensprinter_test.cpp`, `sprinterintsource_test.cpp` (T-VID)

Built in S2 (2026-10-01): T-VID-1..7 and 9 in `screensprinter_test.cpp` (T-VID-8 and 10 since S1);
ACC-1 / R-2 in `sprintervideoboot_test.cpp`, ACC-2 / R-3 in `sprinter_boot_test.cpp`
(roadmap §7).

| ID | Case | Expected |
|---|---|---|
| T-VID-1 | graphics 320: one square with known bytes and palette 2 | golden 16-pixel strip |
| T-VID-2 | graphics 640: nibble order | high nibble first (S2: MAME and the PLD; the design said low) |
| T-VID-3 | text 320 and 640 (Line2 override) | golden strips; flash swaps at frame bit 4 |
| T-VID-4 | border square, blank square | border color = text palette 0 index `border × 9`; blank = pen `#400` (text paper colour 0, S2) |
| T-VID-5 | palette byte order | video RAM holds R, G, B: the BIOS CGA "blue" (`#A8,#00,#00` as B, G, R to function `#A4`) is `#00,#00,#A8` in video RAM and renders blue (decision HW §4.5, S2) |
| T-VID-6 | RGMOD bit 0 flips mid-frame | lines after the beam use page 1 |
| T-VID-7 | HOLD `#00` vs `#77` | picture offset 14 pixels, 7 lines |
| T-VID-8 | INT list from a hand-built mode page (run of `#FD` squares) | INT T-states match MAME's `update_int` on the same page |
| T-VID-9 | 312 vs 320 lines | 69 888 / 71 680 T per frame; INT list recomputed |
| T-VID-10 (ROM) | BIOS `FN_SINC` Pentagon / Scorpion / Spectrum | INT T-states equal the MAME captures (S0) |

### 2.5 IDE — `ideadapter_sprinter_test.cpp` (T-IDE)

| ID | Case | Expected |
|---|---|---|
| T-IDE-1 | truth table: codes `#20-#29` × A8 × read/write | register reached or no effect (tdd-storage §3.2) |
| T-IDE-2 | word order: write `#ABCD` via `#0050`←`#CD`, `#0150`←`#AB` | disk sees `#ABCD`, image bytes `CD AB`; read back the same |
| T-IDE-3 | one shared latch: read a word, then write only the A8=1 half | the word written uses the latched high byte of the read (PLD behavior) |
| T-IDE-4 | channel select `#21BC` / `#01BC` | the other channel's registers untouched |
| T-IDE-5 | 512-byte sector with `LD BC,#0050` + 512 × `INI` (real CPU) | 512 bytes in order: proves B on A15-A8 with post-decrement |
| T-IDE-6 | 512 × `OUTI` write loop | sector written correctly: proves pre-decrement |
| T-IDE-7 | reset | primary selected, latch 0, drives reset |
| T-IDE-8 (ROM, after S7 for the CD case) | BIOS device probe with `ide0.slave` empty, then with an empty CD unit there | the BIOS unit list matches each setup; boot from `ide0.master` unaffected |

As built (S3b, 2026-10-02; the Sprinter decode is a region of the shared `IdeAdapter`, so the tests sit with the
files under test): T-IDE-1..4, 7 = `IdeAdapter_Test.SprinterTruthTable`, `SprinterWordOrder`, `SprinterSharedLatch`,
`SprinterChannelSelect`, `SprinterReset` (+ `SprinterWithoutABoard`, the Sprinter in `RandomPortTrafficIsSafe`);
T-IDE-5 / 6 = `PortDecoderSprinterIde_Test.IniLoopReadsASectorInOrder` / `OutiLoopWritesASectorInOrder` (the BIOS's
unrolled loops on the Z84C15 engine, the 3.04 port table) and `Z84C15_IniOutiPutBOnTheHighAddressByte`;
T-IDE-8 = `SprinterBoot_Test.Bios304_FindsAnEmptyCdUnitOnTheSlave` (the CD case already in S3b) and the "None" line
of `Dss162_BootsFromAHardDiskImage`; R-5 = `SprinterBoot_Test.Dss162_BootsFromAHardDiskImage`.

### 2.6 Floppy — `wd1793_test.cpp` additions, `loader_rawpc_test.cpp` (T-FDD)

Built in S3a (2026-10-01): T-FDD-1/2/3 in `loader_rawpc_test.cpp` (S0, PLAN #60(f)) and
`WD1793Clock_Test.Latched_RateChange*` (the latch flipped during the search), T-FDD-4..7 as
`PortDecoderSprinter_Test.Fdc_*` / `Dos_M1HookOpensAndClosesTheFloppyPorts`; R-4 / R-7 as
`SprinterBoot_Test.Dss162_*`.

| ID | Case | Expected |
|---|---|---|
| T-FDD-1 | raw 1.44 MB image load/save round trip | byte-identical |
| T-FDD-2 | raw 720 KB | 9 sectors, DD |
| T-FDD-3 | raw 1.44 MB image in a drive, the Sprinter latch at DD (`#16`), READ ADDRESS; then at HD (`#17`) | Record Not Found after the normal index count; at HD: the ID is returned with 56 T per byte at 3.5 MHz (336 T at 21 MHz). The generic mismatch rule is already covered by `WD1793Clock_Test.RateMismatch_*` (`core/tests/emulator/io/fdc/wd1793_clock_test.cpp`); this test checks the Sprinter wiring |
| T-FDD-4 | Sprinter clock policy | `GetClockPolicy() == Latched` after machine init, also with `[Beta128] TurboVG=1`; clock 1 MHz and rate 250 kbit/s after reset; STEP and DRQ do not change the clock |
| T-FDD-5 | codes `#16`/`#17` from `OUT (#BD),A` with A = `#01`/`#21` | `GetClock()` / `GetDataRate()` = 1 MHz + 250 kbit/s / 2 MHz + 500 kbit/s (`WD1793::SetLatchedClock`) |
| T-FDD-6 | `#1F` rewrite: `OUT (#1F),A` from RAM | reaches code `#10`; from system ROM: reaches the PIO; `OUT (C),A` with C = `#1F`: PIO |
| T-FDD-7 | DOS in/out by M1 at `#3D00` with BASIC 48 vROM, out at `#4000` | index bit 10 flips; FDC ports appear/disappear |

### 2.7 CMOS and storage profile (T-RTC, T-BOOT)

| ID | Case | Expected |
|---|---|---|
| T-RTC-1 | address/data ports `#DFBD`/`#BFBD`/`#FFBD` | register file read/write |
| T-RTC-2 | fixed time, BCD and binary modes | registers `#00-#09` |
| T-RTC-3 | CMOS file save/load | 128 bytes round trip |
| T-BOOT-1 | `SprinterDssBootProfile` over a folder with `CMD/BOOT.EXE` | LBA 0 entry 0 type `#06`; LBA 1 starts with `Starting...`; LBA 1-3 = last 1 536 bytes of `BOOT.EXE` |
| T-BOOT-2 | folder without `BOOT.EXE` and no config | mounted, report "no DSS boot loader" |
| T-BOOT-3 | `fs=fat32` on a Sprinter IDE slot | refused with an error |
| T-BOOT-4 | FatFs oracle reads the folder volume | same tree (the storage manager's oracle test) |

### 2.8 Accelerator — `sprinteraccelerator_test.cpp` (T-ACC)

Fill, copy, vertical fill/copy with PORT_Y, AND/OR/XOR, length 0 = 256, prefixed `LD r,r` ignored,
`HALT` ignored, ALL_MODE bit 0 = 0 disables, the time charge (`length × 6 / 42 MHz`), writes into
page `#FD` feed the CBL. INT-suspend option on (the PLD behavior, tdd-accel-sound-input §1.3): after an INT
acknowledge a store is plain, the mode register is unchanged, the first opcode after `RETI` re-enables,
`RETN` does not, an NMI does not block; option off: the handler's store is accelerated (MAME).

### 2.9 Sound — `covoxblaster_test.cpp` (T-CBL)

Rate table (16 rows), mono/stereo, 8/16-bit pairing (`XOR #80`), INT every 128 samples, `#FE` bit 7
and bit 5 semantics, CBL off → plain Covox. Built in S6 (`core/tests/emulator/sound/sprinter/`), plus the
machine wiring (codes, INT, page `#FD`, mixer slot, single AY, TTD blob) and the 21 MHz checks (AY pitch, CBL
rate, CPU throughput against MAME): [s6-sound-outcome.md](s6-sound-outcome.md) §6.

### 2.10 Z84C15 — `z84sio_test.cpp`, `z84ctc_test.cpp`, `z84pio_test.cpp` (T-Z84)

SIO: the manual's keyboard and mouse init sequences (MAN §9.1, §9.4) leave the documented register
values; receive FIFO depth 3 with overrun; RR0 bit 0. CTC: timer mode with prescaler 16/256, ZC/TO
callback period, interrupt vector. PIO: register file. Keyboard encoder: a press and release of `A`,
`Right Arrow` (`#E0` prefix) give set-2 sequences.

## 3. L3 firmware tests (ROM-gated)

Location: `core/tests/emulator/machines/sprinter/`. Every test starts with fast start and
`EnableTurboMode()` unless it checks pixels, stops as soon as its condition holds (`TestWait::For`),
and skips with a message when the ROM or the image is missing. Each test runs on BIOS 3.04 (the
default) and 3.06 (review round 1, Q1), as two parameterized instances.

| ID | Maps to | Condition checked |
|---|---|---|
| R-1 | ACC-1a | page `#40` equals the S0 capture after "DCP opened"; boot-menu text in VRAM |
| R-2 | ACC-1 | logo frame equals the golden image (no turbo mode) |
| R-3 | ACC-2 | a SETUP setting change (BIOS 3.04's SETUP has no date page: "Memory Test") persists in the CMOS file with a valid checksum |
| R-4 | ACC-3 | "Starting DOS..." then the DSS prompt text in VRAM; density port = HD |
| R-5 | ACC-4 | same from `ide0.master` built image |
| R-6 | ACC-5 | same from a folder; folder tree hash unchanged |
| R-7 | ACC-6 | Spectrum mode: `LOAD` from a TRD, BASIC program running (marker in RAM). As built: through DSS `SPECTRUM.EXE` (BIOS 3.04 has no Spectrum ROMs); `LOAD "smReadMe" CODE` compared with the file's bytes |
| R-8 | ACC-7/8 | native program frames equal MAME captures |
| R-9 | ACC-10 | TTD record 5 s of R-4, seek, replay: equal frame hashes |

## 4. Test data provisioning

| Data | Location | Rule |
|---|---|---|
| BIOS ROMs | `data/rom/sprinter/sp2k-3.04.rom` (provisioned in S0), `sp2k-3.06.rom` (not public yet: the 3.06 instances skip until it is added, see `data/rom/README-ROMS.md`) | CRC checked against MAME's table at load (warn on mismatch) |
| DSS 1.62 floppy | `testdata/machines/sprinter/dss_1_62_92.img` | third-party, listed in `testdata/NOTICE.md` with its source URL |
| DSS 1.60R files | `testdata/machines/sprinter/dss160r/` (`SYSTEM.DOS`, `SYSTEM.EXE`, `CMD/BOOT.EXE`…) | "believed public domain" (DSS repo README); NOTICE entry |
| Built HDD image | generated **by the test** into the scratch folder from the DSS files (MBR + loader + FAT16), never committed | `TestPathHelper::GetUniqueTestScratchPath()` |
| Folder volume source | `testdata/machines/sprinter/dssfolder/` (a copy of the DSS files) | read-only for tests |
| MAME captures | `testdata/machines/sprinter/reference/` (page `#40` dump, INT positions, logo and boot-screen frames, the first 10 000 port accesses, the loader write count) with a `README.md` saying how they were made (S0, 2026-10-01) | small files only |
| Sample programs | chosen SPRINTEM `disk/*.EXE` or app.sprinter.ru titles, each with its source and license note | only after checking each license |

## 5. Coverage matrix

| Requirement | Tests |
|---|---|
| FR-3 configuration | T-CFG-1…6, R-1 |
| FR-9 configuration modules | T-PLDM-1…5, T-CFG-2 |
| FR-4 port table | T-DCP-1…8 |
| FR-5 memory | T-MEM-1…9 |
| FR-6 clock/waits | T-MEM-10, clock-ratio tests incl. the TTD round-trip of `hw_turbo_ratio` (technical design §3, PLAN #60) |
| FR-7 frame/INT | T-VID-8…10 |
| FR-8 Z84C15 | T-Z84 |
| FR-10…12 video | T-VID-1…7, R-2, R-8 |
| FR-20…21 floppy | T-FDD-1…7, R-4, R-7 |
| FR-22 IDE | T-IDE-1…8, R-5 |
| FR-23 folder | T-BOOT-1…4, R-6 |
| FR-24 CMOS | T-RTC-1…3, R-3 |
| FR-30 sound | T-CBL |
| FR-31…33 input | T-Z84 (encoder), R-4 (`DIR`) |
| FR-40 accelerator | T-ACC |
| FR-50…53 tooling | R-9, automation smoke per surface |
