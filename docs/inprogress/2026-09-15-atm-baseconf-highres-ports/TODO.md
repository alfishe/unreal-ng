# TODO — ZX-Evo BaseConf (`ATM3`) completion

**Status:** analysis and designs done (2026-09-27); phases E0, E1, E2a, E3, E4, E5 done (2026-09-28); E2b (PS/2 keyboard) deferred. PLAN.md rows **#55**
(this program) and **#53** (small ATM gaps; font RAM moved into #55 phase E8).

The 2026-09-15 hi-res video / `#FF` palette / `EFF7` / `7FFD` lock work is **done** (landed with the
atm merge `59e37f38`); it was previously the only content of this folder, marked `DONE.md`. The
2026-09-27 reconciliation reopened the folder because the machine is incomplete against the
**current** BaseConf.

## Done

- [x] Hi-res modes, palette, `EFF7`, `7FFD` lock (2026-09-15; see [README.md](README.md))
- [x] Hardware reference from FPGA/AVR/ROM sources: [baseconf-hardware-reference.md](baseconf-hardware-reference.md)
- [x] Feature matrix of 9 reference emulators: [emulator-feature-matrix.md](emulator-feature-matrix.md)
- [x] unreal-ng audit: [unreal-ng-atm3-audit.md](unreal-ng-atm3-audit.md)
- [x] Gap analysis (39 gaps, IDs P/C/A/ST/R/T): [gap-analysis.md](gap-analysis.md)
- [x] Designs: [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md), [tdd-virtual-trdos.md](tdd-virtual-trdos.md), [tdd-storage-sd-ide-cd.md](tdd-storage-sd-ide-cd.md)
- [x] Plan: [implementation-plan.md](implementation-plan.md)

## Remaining (phases of the plan)

- [x] E0 decoder fixes (2026-09-28): `#FE` exact decode, FDC gating, mouse/joystick, Covox, `#EFF7` rules, 1 MB `#7FFD` bits, `#EFF7` RAM 0, 7 MHz reset test, TTD palette/CMOS-latch fields, ROM page count, port map — [e0-decoder-fixes.md](e0-decoder-fixes.md). `#xBF7` write protect moved to E8
- [x] E1 (2026-09-28): `[EVO] Fpga=trdemu|legacy`, `#xxBD` register table, official `zxevo-fe.rom` booting to the ERS menu, `#13BD` probe passing — [e1-fpga-variant-and-rom.md](e1-fpga-variant-and-rom.md)
- [x] E2a (2026-09-28): `EvoAvr` — ERS shows "Baseconf: ZXEvo 4M 07.01.2026" and "AVR Boot: ZXEvoAVRBoot 25.05.2019 beta", registers A-D, EEPROM window, `[EVO] NvramFile` — [e2a-evo-avr.md](e2a-evo-avr.md)
- [ ] E2b PS/2 keyboard (deferred; NedoOS on ZX-Evo has no keyboard without it). **How to do it:** [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) §6.1 — one key event carries both the ZX code and the physical PC key, journaled once; do it after the in-flight TTD input-journal work lands
- [x] E3 (2026-09-28): board NMI (`#BF`.3, Magic button, M1 breakpoint), NOP entry + RAM `#FF`, 2-M1 exit via `#BE`, ERS Magic Service reachable — [e3-board-nmi.md](e3-board-nmi.md)
- [x] E4 (2026-09-28): virtual TR-DOS trap (`#13BD` mask, chip deselect, swap to RAM `#FE`, `#BE` exit), legacy latches `#2F-#8F`; ERS RAM disk SAVE/LIST/LOAD on the real ROM — [e4-virtual-trdos.md](e4-virtual-trdos.md). Mounting TRD images needs E5/E6 storage
- [x] E5 (2026-09-28): SD card for image files. The reusable `SdCardSpi` over `IBlockDevice` + `SessionWriteMap`, `ZControllerSpi`, `[ZC]` keys, AVR register C. ERS SD boot and TRD mount from SD (read + write) on the real ROM. TTD ends a recording on the first SD command — [e5-sd-card.md](e5-sd-card.md)
- [ ] E5b host folder as the SD card (`HostFolderFat`, shared with IDE R1-6); NedoOS `osatm3sd.$C` from a folder; `IMAGE.MNT` automount
- [ ] E6 NemoIDE (needs shared IDE R1-1) · E7 ATAPI CD (needs IDE R1-7)
- [ ] E8 `#xBF7` write protect, flash writes, font RAM, 4:4:4 palette, ULA+
- [ ] E9 (optional) AVR rasters, RS-232
- [ ] E10 automation, TTD, recipes, MCP resource
- [ ] Older small gaps still open from 2026-09-15: [verification-gaps-and-tests.md](verification-gaps-and-tests.md) items 1, 3-6 (PLAN #53)

## Decisions still open

None blocking; defaults are recorded in [gap-analysis.md](gap-analysis.md) §4 and the designs' decision tables.
