# TODO — SZX (ZX-State) snapshots (2026-09-29)

**Status:** S0-S2 and part of S3 implemented 2026-09-29 on branch `szx`: `.szx` loads and
saves on every surface for 48K, 128K, +2, +2A, +3, Pentagon 128 / 512 / 1024 and Scorpion,
checked both ways against libspectrum. PLAN row **#64** (T2); prerequisite of **#27**
(RZX ↔ TTD) beyond 48K / 128K.

## Documents
- [design.md](design.md) — goals, machine mapping, block coverage, architecture (reader, stage, commit, capture, writer, report), private `UNMC` / `UNDV` blocks, policy for machines without an SZX id, compression dependency, TTD / RZX integration, surfaces, tests, phases S0-S5, open decisions.
- [szx-format-reference.md](szx-format-reference.md) — byte-level SZX v1.5 reference with every spec page, libspectrum and other emulators cited.

## Next
- [x] Design review round 1 applied (design §20).
- [x] S0: miniz 3.1.2 vendored (`core/src/3rdparty/miniz`), `szxformat.h`, `SzxReader` with bounds checks and exact-size inflate, fuzz test.
- [x] S1: read + commit of header, CRTR, Z80R (frame position through `intstart`, MEMPTR incl. 1.1-1.3 `chBitReg`, Q from FSET, EI shadow, HALT, INT window), SPCR (paging replayed through each model's decoder), RAMP, AY; `.szx` on every surface (MCP `load_software`, Qt open / save dialogs and file manager, CLI / WebAPI / Lua / Python through `Emulator::LoadSnapshot`); report in the log.
- [x] S2: `SzxWriter` for the same blocks (exact sizes, deflate only when smaller); Qt save dialog offers SZX first.
- [x] S3 part: B128 both ways (WD1793 registers, #FF, SEEKLOWER, PAGED ↔ TR-DOS); `WD1793::RestoreSnapshotRegisters`.
- [x] Fixtures `testdata/loaders/szx` (libspectrum synth files for all nine machine ids, two converted snapshots, Spectaculator 1.1, ZXMAK2 1.4, ZX-M8XXX 1.4) with libspectrum oracle dumps; `tools/verification/szx` (`szxtool`, `check-interop.sh`: our files read by libspectrum match on all nine models).
- [ ] S1 rest: the load orchestrator with model switch and the new emulator id (design §6, R1); today a snapshot for another model is refused with "switch the model first". A result type with the report on every surface (R9).
- [ ] S3 rest: BDSK (media, link safety R6, `MediaSourceType::Memory` R5), +3 / DSK, TAPE, COVX, AMXM, KEYB (issue 2) / JOY, GS / GSRP. A TurboSound FM config (YM2203) has no AY-3-8910: the AY block is reported, not applied.
- [ ] S4: private blocks (`UNMC` incl. the exact `boundary` byte, `UNDV`), `TTDStateVersion()` (R4), and the no-id machine policy (ATM, ZX-Evo, Profi, TSConf, Scorpion 1024).
- [ ] S5: RZX integration with #27.
- [ ] Correct the docs that claim SZX support today (snapshot-loading DONE.md, automation action plan) and the #EFF7 claim (Pentagon 1024 16-color design, PLAN #53).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #64, #27, #53.
- RZX article: `2026-09-28-debugger-family/rzx-ttd.md`.
