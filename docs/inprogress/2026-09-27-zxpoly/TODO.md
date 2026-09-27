# TODO

**Status:** analysis complete; implementation not started (decision pending).

## What exists in this folder

Four analysis documents produced 2026-09-27 from a full source review of
[raydac/zxpoly](https://github.com/raydac/zxpoly) (quad-Z80 lockstep ZX Spectrum
128 clone; GPL-3 Java reference emulator) cross-checked against unreal-ng's
architecture:

- [zxpoly-platform.md](zxpoly-platform.md) — concept, lockstep theory, hardware
  structure, ports (`#3D00`, module regs), video modes 0–7.
- [zxpoly-emulator-internals.md](zxpoly-emulator-internals.md) — how the Java
  emulator implements it: per-`ctx` bus, instruction-rotated lockstep, 512K
  heap windows, sync primitives, Spec256 mode, formats, timing.
- [zxpoly-adaptation-pipeline.md](zxpoly-adaptation-pipeline.md) — the
  adaptation framework: Sprite Corrector, `zxpoly.i` loader macros, Test ROM,
  seven adapted-game case studies.
- [unreal-ng-port-analysis.md](unreal-ng-port-analysis.md) — component mapping,
  phased plan with effort estimates, value and risk assessment.

## What remains

- Phase 0 spike (decision gate): golden-frame corpus from the Java emulator +
  4× unreal-z80 harness running the Test ROM detection.
- Phases 1–4 per the port analysis (core platform → content bring-up →
  adaptation tooling → optional Spec256).

## Trigger for next step

Explicit approval of the Phase 0 spike (see port analysis §7).
