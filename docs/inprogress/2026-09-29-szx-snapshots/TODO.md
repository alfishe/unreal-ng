# TODO — SZX (ZX-State) snapshots (2026-09-29)

**Status:** design written, no code. PLAN row **#64** (T2); prerequisite of
**#27** (RZX ↔ TTD) beyond 48K / 128K.

## Documents
- [design.md](design.md) — goals, machine mapping, block coverage, architecture (reader, stage, commit, capture, writer, report), private `UNMC` / `UNDV` blocks, policy for machines without an SZX id, compression dependency, TTD / RZX integration, surfaces, tests, phases S0-S5, open decisions.
- [szx-format-reference.md](szx-format-reference.md) — byte-level SZX v1.5 reference with every spec page, libspectrum and other emulators cited.

## Next
- [ ] Decide design §18 (machines without an id, model mismatch, custom ROMs, media link / embed, compression library).
- [ ] S0: miniz, block layouts, reader with bounds checks, fuzz tests.
- [ ] S1: read + commit of header, CRTR, Z80R, SPCR, RAMP, AY; model switch; `.szx` accepted on every surface; load report.
- [ ] S2: writer for the same blocks.
- [ ] S3: B128 / BDSK, +3 / DSK, TAPE, COVX, AMXM, KEYB / JOY, GS / GSRP.
- [ ] S4: private blocks and the no-id machine policy.
- [ ] S5: RZX integration with #27.
- [ ] Correct the docs that claim SZX support today (snapshot-loading DONE.md, automation action plan) and the #EFF7 claim (Pentagon 1024 16-color design, PLAN #53).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — #64, #27, #53.
- RZX article: `2026-09-28-debugger-family/rzx-ttd.md`.
