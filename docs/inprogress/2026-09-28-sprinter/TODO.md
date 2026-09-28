# TODO — Peters Plus Sprinter Sp2000 machine support

**Status marker:** design drafted and **review round 1 done** (2026-09-28). Nothing implemented.
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

## Remaining

- Phases S0-S7 ([roadmap-and-plan.md](roadmap-and-plan.md) §1), PLAN row #59.
- Prerequisites (all before #59): shared infrastructure PLAN #60 (clock ratio, CMOS core and
  migrations, wait-state hook, per-model `Screen`, raw PC floppy loader, WD1793 rate check,
  port-trace internal codes), TTD v2 (PLAN #40), video mappers (PLAN #42), media manager (PLAN #58),
  IDE core (PLAN #13a), ZX-Evo E2b keyboard event (PLAN #55), TSConf (PLAN #41).
- S0 now: provisioning, disassembly of BIOS 3.04 pages 8 and 0 (`docs/disasm/rom/sprinter/`,
  `data/symbols/sprinter/`), loader trace for the exact bitstream write count, PLD check for the
  accelerator INT-suspend.
- Unverified items to settle first (S0/S1): palette byte order, watchdog use by the BIOS,
  keyboard commands from the BIOS, Z84C15 interrupt use.
- After v1: Game, DooM and Video PLD configuration modules, after analyzing their bitstreams
  against MAME.
