# TODO — Peters Plus Sprinter Sp2000 machine support

**Status marker:** design drafted and **review round 1 done** (2026-09-28). **S0 done**
(2026-10-01, branch `sprinter-s0`; the MAME captures on branch `sprinter-mame`); **S1 done**
(2026-10-01, branch `sprinter-s1`); CPU library (2026-10-01, branch `sprinter-cpu`); **S2 done** (2026-10-01, branch `sprinter-s2`); **S3a done** (2026-10-01, branch `sprinter-s3a`); S3b, S4-S7 not started. PLAN.md row **#59** (T4): the owner started
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
- [x] S2 (2026-10-01, branch `sprinter-s2`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §7): `ScreenSprinter` (every mode of the mode table,
  palettes, border, flash, HOLD, RGMOD page, 320 / 312 lines), `M_SPRINTER` / `R_736_288`,
  `SprinterVideoRenderer` + configuration-module hook 3, `SprinterVideoMapper`, screenshots and
  recordings; palette order settled (R, G, B in video RAM); ACC-1 (the logo frame equals MAME's
  `logo.png` exactly when drawn with the frame-end state, within one fade step as the beam drew it),
  ACC-2 adapted (SETUP 1.58 has no date page: "Memory Test" saved to the CMOS file)

- [x] The Sprinter runs on its own CPU library (2026-10-01, branch `sprinter-cpu`; owner decision,
  [research-cpu-z84c15.md](research-cpu-z84c15.md) §8.0): `core/src/3rdparty/z84c15/` - the CMOS
  Z84C00 core forked from unreal-z80 0.5.0 with the Z84C15's wait generator, chip selects,
  watchdog, CTC / SIO / PIO and daisy chain; the engine seam in `Z80` keeps every other machine on
  the native interpreter. Timing changes: the loader 142 T per bitstream byte (MAME 113), the BIOS
  start 22 T before `InitCpuPorts` clears WCR
  ([2026-10-01-z84c15-cpu-library](../2026-10-01-z84c15-cpu-library/README.md))

- [x] S3a (2026-10-01, branch `sprinter-s3a`; outcome and deviations in
  [roadmap-and-plan.md](roadmap-and-plan.md) §8): the `#BD` latch on the WD1793 `Latched` policy, the FDC
  off bit, code `#15` with the Kempston bits, the `#1F` operand rewrite, the WD1793 time base at 21 MHz;
  shared WD1793 fixes: drive select (bits 1-0 of `#FF` were ignored) and a separator-rate change during an
  ID search; **ACC-3**: DSS 1.62.92 boots from the HD floppy in drive B to `B:\>` (the BIOS probe flips to
  1.44 MB); **ACC-6**: Spectrum mode through DSS `SPECTRUM.EXE` (BIOS 3.04 has no Spectrum ROMs), TR-DOS
  7.01 lists a TRD in drive A and `LOAD ... CODE` is byte-exact; MAME 0.289 cannot read the HD floppy
  (its PLL is set at the command start), its probe timing equals ours

## Remaining

- Phases S0-S7 ([roadmap-and-plan.md](roadmap-and-plan.md) §1), PLAN row #59.
- Prerequisites (all before #59): shared infrastructure PLAN #60 (clock ratio, CMOS core and
  migrations, wait-state hook, per-model `Screen`, raw PC floppy loader, port-trace internal
  codes), TTD v2 (PLAN #40), video mappers (PLAN #42), media manager (PLAN #58),
  IDE core (PLAN #13a), ZX-Evo E2b keyboard event (PLAN #55), TSConf (PLAN #41).
- Floppy follow-ups after S3a ([roadmap-and-plan.md](roadmap-and-plan.md) §8):
  - optional: DD-mode turbo VG ending at the PLD read/write strobe (`TURBING`, `SP2_MAX.TDF:272-306`);
    not modeled, only seek time differs. Settle first which pins `WSTB`/`RSTB` are (research open
    question 2);
  - a Type II command (READ / WRITE SECTOR) does not re-run its ID search when the latch changes the rate
    mid-command (READ ADDRESS and Type I verify do); nothing seen needs it;
  - the WD1793 rate-retry state is not in the TTD blob (S7 decides with the `SprinterPld` blob);
  - the WD1793 time base in 3.5 MHz T-states under a hardware turbo is opt-in (Sprinter only): the other
    turbo machines (ATM3 / ZX-Evo, Scorpion, ATM710 turbo) still run the FDC N times fast; switching them
    moves the ATM3 CI-gate figures (device blobs) and needs a re-recording - a separate shared change;
  - FDC off bit (density write data bit 1, MAME) unverified in the PLD;
  - a MAME reference for the floppy boot time needs a MAME whose WD1793 PLL follows `set_clock_scale`
    during a command (0.289 does not).
- Flex Navigator (ACC-8, S4) stops after its splash: the BIOS `RESETD` RESTORE from track 71 (213 ms)
  outlasts the BIOS `WREST` wait (65 536 polls, ~184 ms here), the BIOS zeroes the track register and
  the RESTORE ends at track 9 ([roadmap-and-plan.md](roadmap-and-plan.md) §8). The wait needs at least
  68 T per poll at 21 MHz (ours ~59 T): settle with the origin of the wait rule (below).
- Configuration end, not visible in MAME (no PLD model): when CONF_DONE rises, the extra clocks
  before the PLD starts and the CPU reset (tdd-ports-memory §6). Needs the PLD sources or real
  hardware.
- Owner decision: the default of the accelerator INT-suspend option (S0 proposes on, round 1 said
  off before the PLD check).
- BIOS 3.06 image (CRC `187f4382`): add when a copy is available (MAME set).
- ACC-6 as written ("ESC at the boot menu → Spectrum mode") does not hold for BIOS 3.04: the Spectrum
  ROMs come from DSS `ZX\SPECTRUM.EXE` (or a later BIOS). Whether 3.06 carries them is open.
- Settled in S2: palette byte order R, G, B in video RAM; 640 graphics high nibble first; blank
  square = pen `#400`; HOLD power-on `#77` (hardware-reference §4.5, §6.3). Settled in S1: BIOS 3.04 never programs the Z84C15
  watchdog and sends no keyboard commands (SETUP `KeyboardInit` only sets SIO A, WR1 = 0: no
  Z84C15 interrupts, the keys are polled in the frame INT). Settled against MAME and the board files
  (2026-10-01, [roadmap-and-plan.md](roadmap-and-plan.md) §6.1): IDE with no drive reads `#FF`
  (no DD7 pull-down, LS245 inputs float high), the PLD ends the INT at the acknowledge. Still open:
  the runtime CONF_DONE timing (S1's full start resets after write 473 720 and boots, consistent with
  the static count); the CPU emulation approach, the origin of the wait rule and the PLD wait on
  Z84C15 port writes (pending `research-cpu-z84c15.md`); the unacknowledged INT length (PLD 32-64 T,
  MAME 32 T).
- Renderer speed (naive v1): `BM_SprinterRender_Logo` 512 µs per frame against 46 µs for the TS-Conf
  setup screen. Idea for the backlog: cache decoded squares (MAME's tilemap: the mode bytes and the
  source address once per square, invalidated by video RAM writes into the mode table or the
  square's source) and measure with the same benchmark.
- Hook 3, second half: a configuration module's own INT source (with the first module that needs
  it, e.g. Game).
- S7: the `SprinterPld` TTD serializer (id 25, declared in S1 so TTD refuses to record until
  then), fast RAM in TTD (cache pages are not journaled), the video RAM region.
- After v1: Game, DooM and Video PLD configuration modules, after analyzing their bitstreams
  against MAME.
