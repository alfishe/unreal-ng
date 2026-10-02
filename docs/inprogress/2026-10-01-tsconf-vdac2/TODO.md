# TODO — VDAC2 (FT812) for TS-Conf

Status 2026-10-01: research done, design written, nothing implemented.

- Design: [vdac2-tdd.md](vdac2-tdd.md).
- Games for testing (untracked): `testdata/machines/tsconf/vdac2/` (R-Type 1.01, Zuma 1.1,
  HMM2 v022).

## Remaining

- [ ] D-A: FT812 behavior specification - draft [ft812-behavior-spec.md](ft812-behavior-spec.md) (2026-10-01), awaiting review; open items V1-V18 in its §12
- [ ] D-B: library architecture and implementation brief - draft
  [eve-emu-architecture.md](eve-emu-architecture.md) (2026-10-01): separate static library,
  decoder options BUILTIN / EXTERNAL, standard decoder interfaces, stable snapshot +
  in-flight operation records; A1-A3 decided (own repository + submodule `lib/eve-emu`, `stb_image`, BUILTIN decoders)
- [ ] D-C: integration design - draft [vdac2-integration-design.md](vdac2-integration-design.md)
  (2026-10-01): submodule `lib/eve-emu` + `ENABLE_VDAC2`, SPI hub, `Vdac2Card`, INT line
  source, FT812-rate presentation, exact LUT, TTD id 25 + regions; open C1-C2
- [ ] D-D: test corpus and oracles - draft [vdac2-test-corpus.md](vdac2-test-corpus.md)
  (2026-10-01): layers, TS-Labs SDK programs, BT8XX harness, replay format `.evr`,
  card test programs; open O1-O3
- [ ] Dependency: TTD v2 memory regions (PLAN #40 V1) before VDAC2 can be recorded
- [x] I1 parts that need no library, on master 2026-10-01: VDAC2 LUT `b115af790`, IDE off
  `13263c804`, ROM extraction tool `e6d50bc9d`, SPI hub `d3fecc61a`
- [ ] Library L0-L3b: in progress by a separate agent (own repository)
- [ ] Integration: the rest of I1 (`Vdac2Card`) and I2-I5, after the library host side; the TO VERIFY
  list in design §12.1
