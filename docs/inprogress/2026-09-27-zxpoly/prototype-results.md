# ZX-Poly Quad-Instance Prototype: Results

> Implementation log, 2026-09-28, branch `zxpoly`. This is the first working
> version of [quad-instance-architecture.md](quad-instance-architecture.md)
> (QI) on four stock instances. It covers both paths: `.zxp` snapshots and
> TR-DOS multiloaders. Corpus: [testdata/machines/zxpoly/](../../../testdata/machines/zxpoly/README.md).

## 1. What runs

The whole public corpus runs in lockstep, and the composed pictures are
checked by eye:

| Content | Model(s) | Path | Result |
|:--|:--|:--|:--|
| Alien 8, Buratino (mode 5), Comando Quatro, Flying Shark (mode 7), OFC (mode 6), Summer Santa 2022, `fh.zxp` | 4×128K **and** 4×Pentagon | `.zxp` | 250 frames idle + 1500 frames of scripted play in lockstep; gameplay reached (Alien 8 rooms, Flying Shark play field, Summer Santa level) |
| After The War 2 (mode 4) | 4×Pentagon | TRD multiloader | boots in 625 frames; lock + local reset replicated; 1000 frames incl. gameplay with coloured sprites |
| ZX-Word (mode 5) | 4×Pentagon | TRD multiloader | boots in 435 frames; the editor runs in 512×384 and loads its files through TR-DOS after the lock; typed text in the hi-res font |
| Dizzy X (stock `.sna`) | 4×128K, 4×Pentagon | master load + replication | 200 frames in lockstep (calibration state) |

Test cost: a `.zxp` case runs 250–1500 frames on four machines in 0.3–2 s
(turbo, sequential pipeline).

## 2. Code

| File | Content |
|:--|:--|
| `core/src/loaders/snapshot/loaderzxp.{h,cpp}` | dedicated `.zxp` loader: parse (magic, per-module registers/ports, variable page records, strict size checks) and apply module *i* to instance *i* |
| `core/src/emulator/zxpoly/zxpolyscreencomposer.{h,cpp}` | pure composer, modes 0–7 as in zxpoly `VideoController` |
| `core/src/emulator/zxpoly/zxpolygroup.{h,cpp}` | the group: four instances of any model, `.zxp` load, disk boot, loader phase + lock, replication (CPU, RAM, paging, **device state**, cumulative clock), frame-boundary input, lockstep check, debug traces (`IN` log, M1 trace), composition |
| `core/src/emulator/zxpoly/zxpolyportinterceptor.{h,cpp}` | per-member port layer, forwards to the group |
| `core/src/emulator/ports/portinterceptor.h` | `IPortInterceptor` |

Changes to shared code:

- `Z80::in/out`: one null-checked `portInterceptor` pointer, consulted
  before the model decoder.
- `Keyboard`: `SetHostInputGated`.
- Logger: `ZXP` loader submodule.

Tests: `core/tests/emulator/zxpoly/zxpolyscreencomposer_test.cpp`,
`zxpolygroup_test.cpp`, `core/tests/loaders/snapshot/loaderzxp_test.cpp`.

Environment switches for the tests:

- `ZXPOLY_DUMP_PNG=1` writes the composed frames to `scratch/zxpoly/<model>/`;
- `ZXPOLY_PORT_CHECK=1` turns on the `IN`-log comparison;
- `ZXPOLY_M1_TRACE=1` turns on the instruction trace.

## 3. Findings (corrections to the design)

1. **Compare control state, not data registers.** Right after scripted input
   started, A or BC' differed between master and slaves at frame
   boundaries, while the `IN` logs were identical. That is legitimate: the
   game was mid-draw and the register held a graphics byte, which is exactly
   what the planes differ in. zxpoly's own trigger compares PC, SP, IM and
   IFF only. The group now compares PC, SP, I, IM, IFF1, HALT, T and
   `#7FFD`. A control-flow split always shows up there.
2. **`#7FFD` through the IO window.** While the window is active, a master
   `OUT (#7FFD)` still pages the **master**. Only with the master's R1 bit 5
   set (`COPY2CPU` sets it) does it become a memory write into the target at
   address `#7FFD`, so that the copy loop can pass that address. Routing it
   into the slave's paging latch instead left Atw2's slaves with
   `#7FFD=#77`.
3. **Replication must include device state and the cumulative clock.**
   ZX-Word loads files through TR-DOS after the lock. The slaves' WD1793
   signalled later than the master's (`IN (#FF)` polling loop at
   `#3ECE`), until:
   - device state was copied through the same serializer set a TTD
     checkpoint uses (Beta Disk, tape, mouse, AY, Covox, GS, model
     latches);
   - `EmulatorState::t_states`, the clock devices time themselves by, was
     copied as well.

   This confirms QI §4.1: replication = checkpoint capture on the master,
   restore into the slaves.
4. **Frame-boundary input is enough.** Host key input is gated on every
   member, and the group applies queued keys to all four keyboards before a
   frame. No input-related divergence showed up in 1500 frames of play on
   any title.
5. **The loader phase needs no coupled machine.** The master runs alone via
   `RunUntilCondition`, which stops exactly after the locking `OUT (#3D00)`.
   The slaves are parked, so window writes are collected in per-slave
   overlays. At the lock:
   - the master gets the post-reset CPU state with the injected `JP`
     (10 T);
   - the slaves copy the master, set their own PC from R1–R3, and have the
     overlay applied on top.

## 4. Live in unreal-qt

- **Opening a ZX-Poly machine:**
  - File → **Open ZX-Poly…** (`.zxp`, `.trd`, `.scl`), then pick the model
    (PENTAGON or 128k);
  - drag and drop a `.zxp` onto the window;
  - from the command line: `unreal-qt [--zxpoly-model <model>] <file>`.
    A `.trd` with `--zxpoly-model` boots as a ZX-Poly disk.
- **How it runs:** the master is an ordinary instance adopted by the window,
  with its own loop, pacing, sound, debugger and recording. The group hooks
  into its frame end (`MainLoop::SetFrameEndHook`, after rendering and
  before the frame is latched for display). There it:
  - runs each slave to the master's exact position (frame number + T);
  - applies the queued keys to all four keyboards;
  - writes the composed picture into the master's paper area.
- **The lock** is performed inside the master's locking `OUT (#3D00)`, so it
  works the same under the GUI loop and in tests. The slaves get the
  `OUT` tail (3 T) added.
- **Host keys** arrive through the group (MessageCenter observer, filtered
  by the master's id). Every member's keyboard is gated.

## 5. Not done yet

- **Floating bus on 128K/+3:** master-authoritative substitution (§3.2 of
  [model-agnostic-sync-layer.md](model-agnostic-sync-layer.md)). No title in
  the corpus needed it.
- **Pipelined / threaded frame scheduling:** everything runs sequentially.
- **Mode 5 (512×384) in the GUI:** shown at 256×192, one sample per
  2×2 block (CPU0's quadrant). It needs a double-size framebuffer; the
  test PNGs are full size.
- **Integration:** WebAPI/MCP and the video wall are not wired yet, and
  group members are not yet hidden from instance lists.
- **Per-line VRAM capture:** composition reads VRAM at frame end, which is
  the same as zxpoly's "less resources" mode.
- **TTD:** no group sessions or group seek.
- **Mouse and joystick:** they need the host-input gate too.
- **Disk writes on slaves:** they go to their own mounted image, which is
  not yet an in-memory copy.
- **TR-DOS on 4×128K:** the loader path was only exercised on Pentagon.
- **`core-benchmarks`:** before/after numbers for the `Z80::in/out` pointer
  check are still to be taken.
