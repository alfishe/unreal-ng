# TODO — Peters Plus Sprinter Sp2000 machine support

**Status marker:** design drafted and **review round 1 done** (2026-09-28). **S0 done**
(2026-10-01, branch `sprinter-s0`; the MAME captures on branch `sprinter-mame`); **S1 done**
(2026-10-01, branch `sprinter-s1`); S2-S7 not started. PLAN.md row **#59** (T4): the owner started
the program on 2026-10-01 (TSConf exists; the trigger is no longer "after #41"); the shared pieces
this design introduced are PLAN row **#60** (shared infrastructure, done).

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
  - [x] MAME reference captures (2026-10-01, branch `sprinter-mame`; MAME 0.289 subset build
    `zxsp` with the `sprinter` driver, scripts in `tools/verification/sprinter/`) in
    [testdata/machines/sprinter/reference/](../../../testdata/machines/sprinter/reference/README.md): page `#40` after POST (equals the static table,
    CRC `b7f09600`), the logo frame (frame 60, 1.229 s) and the boot screen (frame 507, 10.383 s),
    INT positions for the FN_SYNC modes (Scorpion = 3.04 default, Pentagon +16 lines, Spectrum +8
    more), the first 10 000 port accesses with codes, the loader write count at run time (473 720,
    confirms Q4)

- [x] S1 (2026-10-01, branch `sprinter-s1`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §6): creatable `SPRINTER`; `PortDecoder_Sprinter`,
  `SprinterMemory`, the configuration modules (Standard + a test stub), the bitstream sink and
  fast start, `SprinterVideoRam` + `SprinterIntSource`, the Z84C15 package, the CMOS, the turbo
  waits, the TR-DOS M1 signal; BIOS 3.04 reaches its boot prompt with the fast and the full start
  (ACC-1a); page `#40` at the prompt equals the static 3.04 table
- [x] S1 checked against the MAME references (2026-10-01, roadmap §6.1): the first 10 000 port
  accesses identical (order, values, PCs, codes) and timed to the T-state at 3.5 MHz; the turbo
  port wait fixed (2 clocks early; 21-MHz drift over the trace 1 245 µs → 5.7 µs); logo palette,
  INT positions per FN_SYNC mode, INT acknowledge, loader count equal; `SprinterReference_Test`

- [x] The Sprinter runs on its own CPU library (2026-10-01, branch `sprinter-cpu`; owner decision,
  [research-cpu-z84c15.md](research-cpu-z84c15.md) §8.0): `core/src/3rdparty/z84c15/` - the CMOS
  Z84C00 core forked from unreal-z80 0.5.0 with the Z84C15's wait generator, chip selects,
  watchdog, CTC / SIO / PIO and daisy chain; the engine seam in `Z80` keeps every other machine on
  the native interpreter. Timing changes: the loader 142 T per bitstream byte (MAME 113), the BIOS
  start 22 T before `InitCpuPorts` clears WCR
  ([2026-10-01-z84c15-cpu-library](../2026-10-01-z84c15-cpu-library/README.md))

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
- Configuration end, not visible in MAME (no PLD model): when CONF_DONE rises, the extra clocks
  before the PLD starts and the CPU reset (tdd-ports-memory §6). Needs the PLD sources or real
  hardware.
- Owner decision: the default of the accelerator INT-suspend option (S0 proposes on, round 1 said
  off before the PLD check).
- BIOS 3.06 image (CRC `187f4382`): add when a copy is available (MAME set).
- Unverified items: palette byte order (S2). Settled in S1: BIOS 3.04 never programs the Z84C15
  watchdog and sends no keyboard commands (SETUP `KeyboardInit` only sets SIO A, WR1 = 0: no
  Z84C15 interrupts, the keys are polled in the frame INT). Settled against MAME and the board files
  (2026-10-01, [roadmap-and-plan.md](roadmap-and-plan.md) §6.1): IDE with no drive reads `#FF`
  (no DD7 pull-down, LS245 inputs float high), the PLD ends the INT at the acknowledge. Still open:
  the runtime CONF_DONE timing (S1's full start resets after write 473 720 and boots, consistent with
  the static count); the CPU emulation approach, the origin of the wait rule and the PLD wait on
  Z84C15 port writes (pending `research-cpu-z84c15.md`); the unacknowledged INT length (PLD 32-64 T,
  MAME 32 T).
- S7: the `SprinterPld` TTD serializer (id 25, declared in S1 so TTD refuses to record until
  then), fast RAM in TTD (cache pages are not journaled), the video RAM region.
- After v1: Game, DooM and Video PLD configuration modules, after analyzing their bitstreams
  against MAME.
