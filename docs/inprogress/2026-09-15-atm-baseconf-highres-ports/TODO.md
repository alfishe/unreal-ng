# TODO — ZX-Evo BaseConf (`ATM3`) completion

**Status:** analysis and designs done (2026-09-27); phase E0 done (2026-09-28). PLAN.md rows **#55**
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
- [ ] E1 FPGA variant switch, `#xxBD` table, official `zxevo_fe.rom`
- [ ] E2 `EvoAvr` (ERS "Baseconf:" / "AVR Boot:" versions, PS/2 buffer, NVRAM file)
- [ ] E3 NMI + breakpoint (M1 hook shared with TSConf)
- [ ] E4 virtual TR-DOS trap (ERS RAM disk and image mounting)
- [ ] E5 SD card (needs `SdCardSpi` on master)
- [ ] E6 NemoIDE (needs shared IDE R1-1) · E7 ATAPI CD (needs IDE R1-7)
- [ ] E8 `#xBF7` write protect, flash writes, font RAM, 4:4:4 palette, ULA+
- [ ] E9 (optional) AVR rasters, RS-232
- [ ] E10 automation, TTD, recipes, MCP resource
- [ ] Older small gaps still open from 2026-09-15: [verification-gaps-and-tests.md](verification-gaps-and-tests.md) items 1, 3-6 (PLAN #53)

## Decisions still open

None blocking; defaults are recorded in [gap-analysis.md](gap-analysis.md) §4 and the designs' decision tables.
