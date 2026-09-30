# DONE — FDC clock and data rate (2026-09-29)

**Status:** complete. The WD1793 model landed on master; the remaining work belongs to other rows
and is listed under Follow-ups with the place that tracks it.

## What landed

Commits `64756638` (model) and `f304dde1` (TTD fixture corpus and CI gate re-recorded):

- Controller clock (`FdcClock`, 1 / 2 MHz) separate from the data separator rate (`FdcDataRate`,
  250 / 500 kbit/s), `core/src/emulator/io/fdc/fdc.h`.
- Step rates 6/12/20/30 ms at 1 MHz, 3/6/10/15 ms at 2 MHz; head settle 30 ms at 1 MHz, 15 ms at
  2 MHz (was a fixed 15 ms); the `E=1` settle now also applies to READ ADDRESS, READ TRACK, WRITE TRACK.
- Rate / medium mismatch: no address marks, so Record Not Found / Seek Error after the revolution
  limit, READ TRACK returns deterministic noise. A track is HD when its raw length is at least 1.5x
  the DD nominal length (`DiskImage::RawTrack::RecordedDataRate()`).
- Clock policies: `Fixed1MHz` (default), `AutoStepTurbo` (STEP selects 2 MHz, the next DRQ 1 MHz;
  ZX-Evo BaseConf, wired for `ATM3`), `Latched` (clock + rate by a machine latch,
  `WD1793::SetLatchedClock`; mechanism only, for the Sprinter). `[Beta128] TurboVG=` overrides the
  machine default.
- Type I verify per the datasheet (was an always-pass TODO); write Lost Data per the datasheet
  (first byte ends the command, later bytes are written as `00`).
- WD1793 TTD blob 251 -> 254 bytes (`ttd.ksy` updated); the FDC device-state report shows clock and rate.

## Evidence

- Tests: `core/tests/emulator/io/fdc/wd1793_clock_test.cpp` (clock, settle, policies, mismatch,
  HD byte period, write Lost Data, verify, machine wiring), `wd1793_test.cpp`,
  `ttdwd1793serializer_test.cpp`.
- TTD fixtures and the V0b CI gate: `testdata/ttd/*.ttd`, `testdata/ttd/bench/v1-ci-gate.txt`.

## Permanent documentation

- [docs/WD1793/WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md)
- [docs/WD1793/WD1793_Timeouts.md](../../WD1793/WD1793_Timeouts.md), `WD1793_Command_Type_I.md`,
  `WD1793_Command_Write_Sector.md`, `WD1793_Command_Write_Track.md`

## Follow-ups (tracked elsewhere)

| Item | Tracked in |
|---|---|
| Sprinter: wire `Latched` + `SetLatchedClock` to the `#BD` latch, reset to DD | PLAN #59, phase S3a; [Sprinter TODO.md](../2026-09-28-sprinter/TODO.md) |
| Sprinter DD-mode turbo VG that ends at the PLD's read/write strobe (`TURBING` held until WSTB/RSTB), not at DRQ. Not built: only positioning time differs, not the data | [Sprinter TODO.md](../2026-09-28-sprinter/TODO.md) (with research open question 2, the strobe pin identity) |
| Raw PC floppy loader 720 KB / 1.44 MB (HD tracks of 12 500 bytes) | PLAN #60(f); [storage manager integration-floppy.md](../2026-09-28-storage-manager/integration-floppy.md) §2 |
| TTD reference bench JSONs (`testdata/ttd/bench/v1-*.json`) after the blob growth | being refreshed separately (only `v1-ci-gate.txt` was re-exported in `f304dde1`) |

## Open questions from the research (§6), not blocking

1. Reading at 2 MHz CLK with a 250 kHz read clock is outside the datasheet's table; modeled as
   "reading does not depend on CLK", which matches ZX-Evo and the Black Crow mod in the field.
2. Sprinter `WSTB` / `RSTB` pin identity (see the follow-up above).
3. Head settle at 1 MHz: 30 ms per the datasheet was chosen (MAME 60 ms, Unreal 15 ms); no Spectrum
   hardware measurement exists.
4. KAY-1024 and Profi: no evidence either way on turbo VG (both stay `Fixed1MHz`; `TurboVG=1` if needed).
5. MiSTer `wd1793.sv`, ZX-Next and u16 were not in the corpus.
6. zx-pk.ru turbo VG threads are gone (404); only search snippets were read.
7. FDC time under CPU turbo: whether the FDC's T-state timebase stays in 3.5 MHz units when the CPU
   runs faster was not checked. It decides whether "HD works at 7 MHz, fails at 3.5 MHz" is modeled
   correctly; check it when the Sprinter (21 MHz) floppy path is built (S3a).
8. The Sprinter's VG93 clock is derived from the PLD equations and MAME only; no primary document
   states it.
