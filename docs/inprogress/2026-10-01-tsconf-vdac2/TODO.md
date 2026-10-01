# TODO — VDAC2 (FT812) for TS-Conf

Status 2026-10-01: research done, design written, nothing implemented.

- Design: [vdac2-tdd.md](vdac2-tdd.md). Research materials stay local and are not part of
  the repository.
- `qoder-tdd.md` is an earlier draft from another tool, kept for reference. `vdac2-tdd.md`
  replaces it; its section 11 lists what changed.
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
- [ ] D-D: test corpus and oracles (TS-Labs SDK programs, games, `ftview` files, ROM
  extraction tool, BT8XX golden images)
- [ ] Dependency: TTD v2 memory regions (PLAN #40 V1) before VDAC2 can be recorded
- [ ] Code (L0-L3b, I1-I4) only after the designs; the TO VERIFY list in design §12.1
