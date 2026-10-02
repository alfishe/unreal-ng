# TODO — ATM Turbo 2 v4.50 (ATM450)

**Status 2026-10-01: implemented on branch `atm450` (worktree `scratch/wt-atm450`),
not yet merged.**

- [README.md](README.md) — scope, deliverables, effort, how to start
- [requirements.md](requirements.md) — R1-R8 + open questions OQ-1..OQ-11
- [tdd-plan.md](tdd-plan.md) — phases 0-5, ordered test list
- [cross-mapping.md](cross-mapping.md) — reference survey, variant comparison,
  behavior→reference→our-code table, reference divergences (§3.3), prewire
  inventory (§4)

## Design review 2026-10-01 (applied)

- `aFE`/`aFB` latch the **low** address byte (both references), not the high one.
- Palette exists on 450: `#7DFD` group, `--grbGRB` layout (OQ-2 resolved).
- ROM page order sys/dos/128/sos verified from `atm1.rom` (OQ-1 resolved).
- `#FDFD`/`#7DFD`/`#7FFD`/AY groups are disjoint on A15/A9 (OQ-3 resolved).
- FDD decode same as Beta-128 (OQ-4 resolved).
- Reset starts in the sys ROM except `RM_DOS` (`z80.cpp:123-133`).
- `pFDFD` was missing from the TTD ATM paging blob — added (former zero filler).
- Reference divergences recorded as OQ-5..OQ-7; UnrealSpeccy behavior ships.

## Remaining work

- [x] Phase 0: `data/configs/atm450/unreal.ini`, factory case, registry test flip
- [x] Phase 1: `portdecoder_atm450.{h,cpp}` + `portdecoder_atm450_test.cpp` (18 tests,
      on a real ATM450 machine - the `#FE` arms need tape / beeper / screen)
- [x] Phase 2: DeviceState reports `afe` (not `ff77`) for 450; mode test in
      `devicestate_test.cpp`; ROM page roles in `ROM::GetROMPageRole`
- [x] Phase 3: `atm450_boot_test.cpp` - system ROM menu + Sinclair palette,
      TR-DOS 48 / SPECTRUM 128 / SPECTRUM 48 entries, `RESET=DOS`
- [x] CP/M entry: the PAL marker is INT-relative (was frame-relative: wrong copy-protection key,
      the loader stayed encrypted); the loader now decrypts and reads the system tracks
- [x] CP/M to the `A>` prompt from `testdata/machines/atm450/cpm/sys.trd`, `DIR B:` lists the floppy
- [ ] OQ-11: keys typed in CP/M sometimes arrive as scan code + 1 (ROM shift-state machine?)
- [ ] Phase 3 rest: an ATM 16-color program end-to-end (T3.4, needs a title in testdata)
- [x] Phase 4: `pFDFD` in the TTD ATM blob (former zero filler, layout unchanged),
      ATM450 seek test, `TtdClockUnits() == 1`
- [ ] Phase 4 rest: snapshot / model switch away-and-back (T4.4)
- [x] Phase 5: Qt menu, `.recipe/machines/atm.md`, `.recipe/_common/machines.md`, AGENTS.md
- [x] Hardware documentation: MicroArt manual + schematic found (requirements.md header,
      cross-mapping §3.5); no PLD dump / HDL exists
- [ ] OQ-5 follow-up: keep or drop `RAM_1024` on the ATM450 model row

Tracked as row #79 in [../PLAN.md](../PLAN.md).
