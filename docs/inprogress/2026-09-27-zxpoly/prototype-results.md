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
  - **Machine** → **ZXPoly-48k / ZXPoly-128k / ZXPoly-Pentagon**: the bare
    machine of that configuration (the menu shows which one is running);
  - File → **Open ZX-Poly…** (`.zxp`, `.trd`, `.scl`), then pick the
    configuration (the 128K-class ones: an edition needs 128K paging);
  - drag and drop a `.zxp` onto the window;
  - from the command line: `unreal-qt [--zxpoly-model <configuration>] <file>`.
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

## 5. Full platform compatibility

Everything the zxpoly board does is now emulated. Each item is covered by a
test that fails when the mechanism is turned off (mutation-checked):

| Mechanism | How | Test |
|:--|:--|:--|
| System reset | a reset of the master: `#3D00` = 0, ports unlocked, slaves reset to `#0000` and waiting, mode 0 | `MasterResetReturnsToLoaderPhase` |
| Coupled machine (unlocked, `#3D00` D0 = 1) | slaves run their own code, caught up after every master instruction | `TestRomPassesAllChecks` |
| Local reset | R0 D5 / `#3D00` D1, per module, with the injected `JP`, pending interrupts dropped | Test ROM (CPU0 resets itself, CPU1–3 by R0 = `$22…$26`) |
| R0 status | HALT, WAIT (parked or stop address), packed last-M1 address | Test ROM CPU0 check |
| IO window | reads and writes live slave RAM; a read sends the target an INT, a write an NMI (unless the target's R1 D4); any module's reads go through the window (zxpoly `readIo`) | `WindowWriteSendsNmiUnlessR1MasksIt` |
| NMI to a waiting module | dropped: the 16 T pulse expires while the module waits (the Test ROM relies on this) | Test ROM |
| Common frame INT | slaves see it only while locked; the master, while `#3D00` and `#7FFD` are unlocked, only with `#7FFD` D7 = 0 (128K-class latch) | `SlaveMissesFrameIntBeforeLockAndTakesLocalInt`, `HaltNotificationWakesTheMasterWhoseFrameIntIsGated` |
| Local INT | R0 D7, window reads, halt notification: a 36 T pulse (`Z80::RaiseLocalInt`) | same |
| Halt notification | the HALT edge of a module, unlocked only: its R1 D0–D3 targets, D6 INT, D7 NMI | `HaltNotificationWakesTheMasterWhoseFrameIntIsGated` |
| Stop address | R2/R3 park a slave at that PC; a new address releases it; zero disables it (zxpoly would stop at any `#0000`, which no program relies on) | `StopAddressParksASlaveUntilMoved` |
| Slave device writes | reach the machine's devices unless R0 D4 (every edition sets it); `#7FFD` always pages the module itself | `SlaveDeviceWritesReachTheMachineUnlessDisabled` |
| RAM0 at `#0000` | `#7FFD` D6 while unlocked | Test ROM RAM0→ROM |
| Floating bus | the master's value for every module (one video memory drives the bus), 128K and Pentagon (`FloatBus=1`) | `FloatingBusValueComesFromTheMaster` |
| Per-line picture | every module captures a paper line when its beam passed it (zxpoly renders per line); the composer works on those lines | `PictureFollowsTheBeamLineByLine` |
| Test ROM | `.prom` loading; all 8 checks OK, mode 4 and mode 5 demos, 4×128K and 4×Pentagon | `TestRomPassesAllChecks` |

Infrastructure:

- **Group members hidden:** the slaves are left out of instance listings,
  index lookup and "most recent" selection, but stay reachable by ID
  (`SlavesAreHiddenFromInstanceListings`).
- **Mouse:** the Kempston mouse is gated like the keyboard; moves, buttons
  and wheel reach all four at the frame boundary. There is no host joystick
  device in unreal-ng, so nothing to gate there.
- **Disk writes:** they stay in each machine's in-memory image; only an
  explicit `SaveDisk`, on the visible master, writes a file.
- **Machines without TR-DOS:** a stock 128K has no TR-DOS ROM, so a
  multiloader disk is refused with a message (`DiskBootNeedsAModelWithTRDOS`).
  ZX-Poly TRDs run on Pentagon, zxpoly's own default.

## 6. Configurations

`ZXPolyGroup::Configurations()` is the one table of named configurations:
`ZXPOLY-48K`, `ZXPOLY-128K` and `ZXPOLY-PENTAGON`, each four instances of one
base model.

- **Creating one:** `EmulatorManager::CreateEmulatorWithModel` resolves a
  configuration name before anything else and creates the whole group. Every
  surface creates machines through that call, so all of them take the names:
  WebAPI and MCP `model`, the CLI `create`/`start`, Lua/Python
  `zxpoly_start`, the Qt menu.
- **Listing:** the model listings (WebAPI `/models` and `/status`, MCP
  `list_models`, CLI `models`) show the configurations.
- **RAM:** a configuration fixes the RAM size, so a request with `ram_size`
  is refused.
- **48K:** a 48K group runs the synchronized quad and replicated 48K
  software. ZX-Poly editions page through `#7FFD`: the Test ROM reports
  "Check ZX-128 ... BAD", and a `.zxp` diverges. So `.zxp` and `.prom` are
  refused on 48K with the reason. There is no TR-DOS either.

Tests: `ConfigurationsCreateTheGroupByName`,
`FortyEightKGroupRunsReplicatedSoftwareButRefusesEditions`.

## 7. Group time travel

The four TTD sessions run as one timeline (QI §8):

- **Start:** `StartRecording` starts all four sessions at the same frame, so
  TTD frame N is the same machine frame on every module.
- **Input:** the group's key and mouse operations are journaled into every
  session at the frame boundary where they are applied.
- **Platform state:** `#3D00`, R0–R3, the lock and the wait flags are
  snapshotted once per frame beside the sessions.
- **Seek:** `SeekToFrame(n)` pauses the sessions (their history stays),
  restores all four to the start of frame *n* and puts the platform state of
  that frame back. A live master is paused first.
- **Resume:** `ResumeRecording` continues from there. It cuts the history
  after that frame, so new input starts a new branch.

Test: `GroupTimeTravelSeeksAndReplays` records 120 frames of Summer Santa
with keys. It then:

1. seeks back to frame 60 and checks the state matches the recording;
2. replays 30 frames with the same keys and checks every frame is identical;
3. branches from frame 65 with other keys and checks lockstep for 45 frames.

**Finding:** a group frame must end at the frame boundary. `Emulator::RunFrame`
is the debugger's frame step: it returns to a remembered T position, and a
TTD seek resets that position. Replayed frames then ran one instruction
longer than the recorded ones. The group now runs the master with
`RunUntilCondition` until the frame counter moves.

Surfaces: WebAPI (`start` with `zxpoly`, `GET /{id}/zxpoly`), MCP
(`zxpoly` on create, `zxpoly_status`), CLI, Python and Lua; see
[.recipe/machines/zxpoly.md](../../../.recipe/machines/zxpoly.md). The group
timeline is a core API for now. The per-instance TTD tools see each module
alone.

## 8. Not done yet

- **Pipelined frame scheduling:** once locked, the three slaves catch up in
  parallel at every frame end (on by default, `SetParallelSlaves`). The
  master still waits for them before its next frame; overlapping the slaves
  with the master's next frame is not done.
- **Group TTD on the automation surfaces:** seek and resume of the whole group
  through WebAPI/MCP `time_travel`.
- **Video wall:** ZX-Poly machines as tiles.
