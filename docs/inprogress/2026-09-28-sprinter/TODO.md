# TODO — Peters Plus Sprinter Sp2000 machine support

**Status marker:** design drafted and **review round 1 done** (2026-09-28). **S0 done except the
MAME captures** (2026-10-01, branch `sprinter-s0`); S1-S7 not started.
PLAN.md row **#59** (T4, trigger: TSConf #41 landed); the shared pieces this design introduced
are PLAN row **#60** (shared infrastructure, before TSConf). The Sprinter is the last machine
program; only S0 can start earlier.

## Goal

A creatable `SPRINTER` model that runs the real BIOS and Estex DSS (floppy, HDD image, PC folder),
the Spectrum mode with TR-DOS, and Sprinter-native software, with TTD, debugger and automation
parity. Details: [README.md](README.md), [goals-and-requirements.md](goals-and-requirements.md).

## Progress

- [x] Source survey ([materials.md](materials.md)): MAME, ZXMAK2, SprintEm, the designer's manual,
  BIOS sources (Peters Plus 2.17 and the Tolik-Trek continuation), DSS sources and binaries, the
  PLD (AHDL) design, a published DSS 1.62 boot floppy
- [x] Hardware reference ([hardware-reference.md](hardware-reference.md)) incl. the decoded
  standard port table and the source-disagreement table
- [x] High-level and technical designs ([high-level-design.md](high-level-design.md),
  [technical-design.md](technical-design.md) and five `tdd-*.md` files)
- [x] Mapping onto unreal-ng ([unreal-ng-mapping.md](unreal-ng-mapping.md)), plan
  ([roadmap-and-plan.md](roadmap-and-plan.md)), tests ([test-plan.md](test-plan.md))
- [x] Review round 1 (2026-09-28): decisions D1-D11 (high-level design §7), Q1-Q6 and the shared
  infrastructure decisions ([roadmap-and-plan.md](roadmap-and-plan.md) §5); modular PLD
  configurations (`SprinterPldConfiguration`)
- [x] S0 (2026-10-01, branch `sprinter-s0`; [roadmap-and-plan.md](roadmap-and-plan.md) §1):
  - [x] BIOS 3.04 in `data/rom/sprinter/sp2k-3.04.rom` + `data/rom/README-ROMS.md` + ROM signature
    catalog (`rom.cpp`); 3.06 **not found publicly** (recorded; add from the MAME set later)
  - [x] disassembly of ROM pages 8 and 0, SETUP (unpacked from page 0) and the PLD loader in
    [docs/disasm/rom/sprinter/](../../disasm/rom/sprinter/README.md), names carried from BIOS-TT
    `0271ac3` and BIOS-PP `1273243`; symbol files in `data/symbols/sprinter/` (load with
    `LabelManager`)
  - [x] port-table decoder `tools/sprinter/dcp-table.py`; the 3.04 table checked statically
    ([hardware-reference.md](hardware-reference.md) §4.4: three differences to the BIOS-TT table)
  - [x] Q4: 473 720 writes, statically ([tdd-ports-memory.md](tdd-ports-memory.md) §6)
  - [x] Q3: the PLD has the INT-suspend; default on (owner decision 2026-10-01)
    ([tdd-accel-sound-input.md](tdd-accel-sound-input.md) §1.3)
  - [x] DSS 1.62 boot floppy and DSS 1.60R files in `testdata/machines/sprinter/` +
    `testdata/NOTICE.md`; `LoaderRawPcFloppyDss_Test` reads the real floppy through the WD1793 at
    500 kbit/s
  - [x] Sprinter sources added to the local corpus ([materials.md](materials.md))

## Remaining

- Phases S0-S7 ([roadmap-and-plan.md](roadmap-and-plan.md) §1), PLAN row #59.
- Prerequisites (all before #59): shared infrastructure PLAN #60 (clock ratio, CMOS core and
  migrations, wait-state hook, per-model `Screen`, raw PC floppy loader, port-trace internal
  codes), TTD v2 (PLAN #40), video mappers (PLAN #42), media manager (PLAN #58),
  IDE core (PLAN #13a), ZX-Evo E2b keyboard event (PLAN #55), TSConf (PLAN #41).
- WD1793 clock / data rate: **built** (2026-09-29, commits `64756638`, `f304dde1`; outcome in
  [2026-09-29-fdc-clock-and-data-rate/DONE.md](../2026-09-29-fdc-clock-and-data-rate/DONE.md)). In S3a:
  - wire the `#BD` latch (codes `#16`/`#17`) to `WD1793::SetLatchedClock` with the decoder returning
    `FdcClockPolicy::Latched`; reset = DD ([tdd-storage.md](tdd-storage.md) §2.3, tests T-FDD-3..5);
  - check that FDC timing stays in real time at 21 MHz (research open question 7);
  - optional: DD-mode turbo VG ending at the PLD read/write strobe (`TURBING`, `SP2_MAX.TDF:272-306`);
    not modeled, only seek time differs. Settle first which pins `WSTB`/`RSTB` are (research open
    question 2).
- S0 remainder, **deferred: needs MAME** (not installed; owner: do not install it): page `#40`
  after POST, the BIOS logo frame, INT T-states for the three FN_SINC modes, the first 10 000 port
  accesses of BIOS 3.04 with codes, a runtime trace of the loader (confirms the 473 720 writes, the
  extra clocks before the PLD starts and the CPU reset). From S1 on the emulator itself can take
  the page `#40` and loader captures.
- Owner decision: the default of the accelerator INT-suspend option (S0 proposes on, round 1 said
  off before the PLD check).
- BIOS 3.06 image (CRC `187f4382`): add when a copy is available (MAME set).
- Unverified items to settle first (S0/S1): palette byte order, watchdog use by the BIOS,
  keyboard commands from the BIOS, Z84C15 interrupt use.
- After v1: Game, DooM and Video PLD configuration modules, after analyzing their bitstreams
  against MAME.
