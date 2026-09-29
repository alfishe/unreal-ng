# TODO

**Status:** analysis complete; implementation not started (decision pending).

## What exists in this folder

Analysis documents produced 2026-09-27 from a full source review of
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
  eight adapted-game case studies.
- [dizzy-adaptation-algorithm.md](dizzy-adaptation-algorithm.md) — worked
  example: stage-by-stage algorithm for adapting a Dizzy-class game (safety
  audit → asset inventory → plane colorization → loader → validation), with
  readback failure patterns, plane-decomposition rules and effort model.
- [cpu-synchronization-models.md](cpu-synchronization-models.md) — design note:
  symmetric lockstep vs master/replica forced-sync vs lockstep+auto-recover;
  what zxpoly's Spec256 mode already does (register alignment), pros/cons,
  and the small unreal-ng delta (+2–4 days each mode on Phase 1).
- [automated-colorization-pipeline.md](automated-colorization-pipeline.md) —
  design note: TTD-driven sprite/tile auto-discovery (grabber + mapper),
  attribute mining as free atlas seed, the two-colors-per-cell atlas model
  (plane transform formula, shape-hash portability), pattern themes, picker-
  grade editor, and engine-family replication (in-repo DIZZY_X corpus).
  ~4–6 weeks on top of Phase 3.
- [attribute-clash-automation.md](attribute-clash-automation.md) — design note:
  TTD-driven clash analysis (launch-and-discover beats full playthroughs for
  data-driven engines; static map×tile analysis bounds all potential clash
  pairs) and three display-side mitigations with pros/cons and effort: (A)
  per-scanline shadow-attribute JIT allocator (~2 wk), (B) fine-grained ULA
  sampling 8×1…per-pixel (+2–3 wk), (C) per-game metadata bundles loaded by
  content signature (~1 wk). MVP A+C ≈ 5–6 wk; shares discovery/atlas core
  with the ZX-Poly port.
- [model-agnostic-sync-layer.md](model-agnostic-sync-layer.md) — ZX-Poly as
  a pure synchronization wrapper over **any** model (4×48K, 4×128K, 4×+3,
  4×Pentagon, …).
  - Hooks: one port interceptor in `Z80::in/out` (ZX-Poly ports +
    master-authoritative floating bus), a line-capture hook in `ScreenZX`,
    and a frame-end callback on the master's `MainLoop`.
  - Includes a per-model support matrix, `slave_io = full | replay` for heavy
    devices, the start option instead of a new `MEM_MODEL`, the WebAPI/MCP/Qt
    surface, tests G1–G7 and effort (~+2 wk over QI).
- [unreal-ng-port-analysis.md](unreal-ng-port-analysis.md) — component mapping,
  phased plan with effort estimates, value and risk assessment.
- [quad-instance-architecture.md](quad-instance-architecture.md) — **the
  recommended implementation route**: four stock Pentagon instances.
  - Load on the master, replicate CPU, RAM and paging into three slaves at
    the entry point, then lay each slave's plane overlay on top (`.zxp`, the
    multiloader's `COPY2CPU` writes, or metadata).
  - Every instance is a full machine (its own `IN`/`OUT`, its own devices,
    copied at replication). Host input is gated off on slaves, and the
    master's applied input events are replicated into the slaves' input
    journals at the same T. An optional `IN` log serves as a divergence
    check.
  - Frames run independently, pipelined across threads.
  - The picture is per-line VRAM capture in every instance plus a
    pure-function composer.
  - Divergence is caught at the first differing `IN` or at frame end, and
    located by a TTD instruction bisect.
  - TTD v1 = four ordinary aligned sessions (slave input in their own
    journals) + group seek/branch.
  - Includes the test plan T1–T12 (tests first), the decisions table, effort
    (~6–7 wk) and the deferred coupled machine (Test ROM/MIMD only).

## Test corpus

Everything public, from the zxpoly repository, collected with measurements
in [testdata/machines/zxpoly/](../../../testdata/machines/zxpoly/README.md):

- 7 `.zxp` snapshots;
- 2 multiloader TRDs (After The War 2, ZX-Word) with their loader sources;
- 9 Sprite Corrector projects, including the unexported After The War 1;
- the Test ROM and its source;
- the Flying Shark base snapshot and pokes.

## What remains

- Prototype = tests T4–T6 of quad-instance §10 (replication, IN-log lockstep
  with input gated on slaves and replicated from the master, plane
  divergence → colour), in a worktree, with no
  shared-code changes. This is the go/no-go gate. Golden frames from the Java
  emulator for T3/T11.
- ZX-Poly config must set `intlen=36` (zxpoly's Pentagon profile);
  unreal-ng's Pentagon default is 32.
- Reference quirks, all decided in quad-instance §14. None of them affects
  the snapshot or loader path.
- Implementation per quad-instance §10–§11; then port-analysis Phase 3
  (adaptation tooling) and the optional Spec256 work — the Phase 4 deep-dive now lives in
  [2026-09-27-spec256](../2026-09-27-spec256/), incl. a standalone Track A
  that does not depend on this program).
- Dependencies (from PLAN.md):
  - v1 TTD needs nothing from #40, since it uses four ordinary sessions;
    only the later single group file rides #40;
  - the composer should plug into the #42 video mapper interface.
- Out of scope for v1: the Test ROM's per-CPU tests and MIMD software, which
  need the coupled machine of quad-instance §13.
- Deferred, low priority (possible later): time travel for ZX-Poly machines
  (the group timeline exists in the core only; the per-instance TTD commands are not blocked and act on the master alone) and
  ZX-Poly tiles in the video wall; the risks are in
  [prototype-results.md §9](prototype-results.md#9-not-done-yet).

## Trigger for next step

Go-ahead for the T4–T6 prototype in a worktree (quad-instance §10).
