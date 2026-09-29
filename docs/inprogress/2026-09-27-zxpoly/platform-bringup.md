# ZX-Poly Platform Bring-up: From the Brief to the Final Polish

> A retrospective of how ZX-Poly support was brought up in unreal-ng. It
> covers every step: the brief, the analysis, the specification, the proof
> of concept, the code, the live checks, the porting to every surface, the
> tests and the polish. It records what was hard, what did not work the
> first time, and every bug found on the way. The session log with commit
> ids and the time accounting are at the end.
>
> Period: 2026-09-27 21:18 to 2026-09-29 14:29 (local time, UTC−4).
> Result: [prototype-results.md](prototype-results.md) (what runs, the
> mechanisms, the tests). Design: [quad-instance-architecture.md](quad-instance-architecture.md).

## Contents

1. [The platform in one page](#1-the-platform-in-one-page)
2. [Glossary](#2-glossary)
3. [The brief](#3-the-brief)
4. [Step 1: analysis of the source material](#4-step-1-analysis-of-the-source-material)
5. [Step 2: the specification](#5-step-2-the-specification)
6. [Step 3: the proof of concept](#6-step-3-the-proof-of-concept)
7. [Step 4: live in unreal-qt](#7-step-4-live-in-unreal-qt)
8. [Step 5: full platform compatibility](#8-step-5-full-platform-compatibility)
9. [Step 6: closing the gaps](#9-step-6-closing-the-gaps)
10. [Step 7: porting to every surface and keeping up with master](#10-step-7-porting-to-every-surface-and-keeping-up-with-master)
11. [Step 8: time travel, built and then fenced off](#11-step-8-time-travel-built-and-then-fenced-off)
12. [Step 9: performance](#12-step-9-performance)
13. [Step 10: polish](#13-step-10-polish)
14. [Bug catalogue](#14-bug-catalogue)
15. [What was hard and what we learned](#15-what-was-hard-and-what-we-learned)
16. [Result in numbers](#16-result-in-numbers)
17. [Session log: commits](#17-session-log-commits)
18. [Time accounting](#18-time-accounting)

## 1. The platform in one page

ZX-Poly (1994) is four ZX Spectrum 128 computers on one board. All four
Z80 processors run **the same program at the same time**. Each one draws its
own copy of the screen in its own memory. The video logic takes one bit of
every pixel from each of the four screens. Four bits per pixel make 16
colors per pixel, with no attribute clash.

A worked example: a game draws a sprite. On an ordinary Spectrum it has one
bitmap and one color per 8×8 cell. On ZX-Poly the same drawing code runs on
all four processors, but each processor's memory holds a different version
of the sprite bitmap (a "plane"). The four planes combine into one colored
sprite. The program code is unchanged; only the graphics data differs.

The board also has platform ports:

- `#3D00`: the main control port (video mode, the IO window, the lock);
- R0–R3 per module: status, local reset, INT and NMI, halt notification,
  stop address.

These let the first processor (CPU0) load the other three before the game
starts. After the **lock** (a write to `#3D00` with bit 7), the ports freeze
until a system reset and the four processors just run.

## 2. Glossary

| Term | Meaning |
|:--|:--|
| Module, CPU0–CPU3 | One of the four Spectrum computers on the board |
| Master, slave | In unreal-ng: the master is module 0, an ordinary visible instance; the three slaves are hidden instances |
| Plane | One module's copy of the graphics; the four planes combine into the color picture |
| Lock | `#3D00` bit 7: from then on the four modules run in lockstep and the platform ports are frozen |
| Lockstep | All four processors at the same instruction and the same T-state |
| Loader phase | Before the lock: the master loads the program and streams plane data into the slaves |
| IO window | Before the lock, the master's port writes can land in a slave's memory (`COPY2CPU`) |
| `.zxp` | A ZX-Poly snapshot: all four modules' memory and registers in one file |
| Multiloader | A TR-DOS disk whose loader fills the four planes itself and then locks the machine |
| Test ROM | The platform's own self-test ROM (`.prom`) |
| Composer | The code that combines four screens into one picture (video modes 0–7) |
| Group | `ZXPolyGroup`: the class that runs four unreal-ng instances as one ZX-Poly machine |
| Frame boundary | The end of one 20 ms frame; the one moment the group lines up all four modules |
| TTD | Time-travel debugging: recording and rewinding a machine |
| Turbo | Unlimited speed: frames run back to back, the picture is shown only now and then |
| Host speed ×N | The speed control ×2…×16: each shown frame holds N machine frames |

## 3. The brief

The starting point was one request, on 2026-09-27 at 21:18:

> Analyze `docs/inprogress/2026-09-27-zxpoly`, find inconsistencies and gaps,
> fix them. I want specifically to know: do we understand how to efficiently
> create a ZX-Poly configuration based on unreal-ng (4 × synchronized 128K or
> Pentagon instances) + ULA interceptors + a shared screen renderer, to make
> everything work smoothly?

The folder already held the platform description and a port analysis written
earlier that day ([unreal-ng-port-analysis.md](unreal-ng-port-analysis.md),
commit `ab25b54b`). That analysis proposed a different design: one emulator
instance with the legacy Z80 as CPU0 and three co-processors from the
separate `unreal-z80` core.

So the brief was open: first answer whether the four-instance approach works,
then make it work.

## 4. Step 1: analysis of the source material

**What was read:**

- the platform docs in the folder;
- the zxpoly reference emulator source (Java, by the platform's author):
  - the video controller;
  - the module and motherboard logic;
  - the `.zxp` format;
- the unreal-ng engine:
  - instance creation;
  - frame stepping;
  - the port decoder;
  - the TTD checkpoint serializers;
  - the framebuffer.

**Problems found in the existing documents:**

- **The recommended design carried a hidden risk.** It used two different
  Z80 cores and rated this "no semantic risk". The two cores handle
  memory contention differently, and one has no WAIT pin at all. Keeping
  two different cores exactly in step, T-state by T-state, would have been
  the hardest part of the whole port.
- **A contradiction about `#3D00` bit 0.** The bit is active-low (nWAIT). A
  `.zxp` export therefore leaves the slaves *running*, while the docs said
  they are held in WAIT.
- Several smaller inconsistencies, for example the Pentagon INT length:
  zxpoly uses 36 T, unreal-ng's default is 32.

**The key insight.** Once `#3D00` is locked, the four modules no longer
interact:

- the lock is permanent until a full reset;
- every ZX-Poly port is frozen;
- each module has its own memory and its own copy of the devices.

So four ordinary unreal-ng instances can each run a whole frame on their own,
as long as they start from the same state and get the same input. No
instruction-by-instruction coupling is needed for the locked phase, and that
phase is where every game spends its time.

The answer to the brief was therefore "yes, and the four-instance design is
simpler than the one on file". It went into a new document,
[quad-instance-architecture.md](quad-instance-architecture.md).

## 5. Step 2: the specification

The specification was shaped in a fast exchange with the product owner. Each
decision removed a whole class of work:

| # | The question | The decision | Effect |
|:--|:--|:--|:--|
| 1 | Do the CPUs need a synchronized reset? | No. Snapshots load all four at the same T-state; nothing is reset | The snapshot path became the main path; `Z80::Reset` is never called |
| 2 | Is a separate POC project needed? | No. Work in a worktree; the first checks are ordinary unit tests | No throwaway code; the POC became the first tests |
| 3 | How to handle the loader phase? | Load anything on the master alone, pause at the entry point, copy the master's state into the slaves, then continue in ZX-Poly mode | The loader phase needs no slave execution at all |
| 4 | Input? | The master alone captures input; the slaves get exactly the same, applied at the frame boundary | No input-driven divergence |
| 5 | Port I/O on the slaves? | `OUT` is not dropped, and `IN` works too: the slaves are full machines with their own devices. Only host input from MessageCenter is gated | Slaves need no special device model |
| 6 | TTD in v1? | Separate per instance | Deferred later (Step 8) |
| 7 | One machine model only? | No: a model-agnostic sync layer, 4×48K, 4×128K, 4×+3, 4×Pentagon | [model-agnostic-sync-layer.md](model-agnostic-sync-layer.md) |
| 8 | Why keep four copies of the program in a synchronous system? | Storage is one base snapshot plus per-plane atlases (graphics slots), not four copies | The `.zxp` storage model section |

Decision 5 overturned the first proposal. That proposal had the slaves
replay the master's port reads (an `IN` log) and drop their writes. The
owner's simpler rule is closer to stock behavior and needs less code.

**The test corpus** was collected next: every public `.zxp`, both
multiloader TRDs, the Test ROM and its source, the Sprite Corrector
projects. It went into [testdata/machines/zxpoly/](../../../testdata/machines/zxpoly/README.md)
with sources and measurements of how much each plane differs. One
constraint: the Russian wiki with the platform's history blocks automated
fetching, so it can only be read in an interactive browser.

A process correction came here too. The first attempt ran the full C++ build
and test suite before a docs-only commit. The project rules require that only
for C++ changes; a docs commit needs a link check. The rules were followed
from then on.

Commits: `95fce44d` (architecture), `7292cd41` (sync layer, storage model,
corpus).

## 6. Step 3: the proof of concept

**Approach.** A worktree and a branch, and the prototype written as unit
tests from the first line. Both models from the start (4×128K and
4×Pentagon), so that nothing model-specific slipped in.

**What was built:**

- **The `.zxp` loader** (`LoaderZXP`): parses the file (magic, per-module
  registers and ports, variable page records, strict size checks) and
  applies module *i* to instance *i*.
- **The composer** (`ZXPolyScreenComposer`): a pure function from four
  screens to one picture, modes 0–7, following the reference video
  controller.
- **The group** (`ZXPolyGroup`):
  - creates the four instances and loads the media;
  - runs the loader phase and performs the lock;
  - replicates the master into the slaves;
  - feeds input at the frame boundary;
  - checks lockstep.
- **The port interceptor:** one pointer in `Z80::in`/`Z80::out`, consulted
  before the model's port decoder. It is the only change to the CPU core.

**Result after about 30 minutes:**

- **All seven `.zxp` files**, on both models: in lockstep for 250 idle
  frames and 1500 frames of scripted play. Gameplay was reached.
- **Both multiloader disks** boot, lock and play.
- **41 new tests.** The composed frames are saved as PNG for visual review.

**What went wrong in the POC, and the fixes** (these became the design
corrections in [prototype-results.md §3](prototype-results.md#3-findings-corrections-to-the-design)):

1. **False lockstep alarms.** The first check compared every register. Right
   after scripted input started, register A or BC' differed between master
   and slaves at frame boundaries, although the port reads were identical.
   The cause is legitimate: mid-draw, a register holds a graphics byte, and
   graphics are exactly what the planes differ in. **Fix:** compare the
   control state only (PC, SP, I, IM, IFF1, HALT, T-state, `#7FFD`), as the
   reference emulator does.
2. **`#7FFD` through the IO window was routed the wrong way.** While the
   window was open, a master write to `#7FFD` was sent into the slave's
   paging latch. After Atw2 loaded, its slaves had `#7FFD=#77` and showed
   garbage. **Fix:** `#7FFD` still pages the master. Only with the master's
   R1 bit 5 set (the loader's `COPY2CPU` sets it) does it become a memory
   write into the slave at address `#7FFD`.
3. **ZX-Word diverged after the lock.** The word processor loads files
   through TR-DOS after the lock. The slaves' disk controller answered later
   than the master's, visible as a polling loop at `#3ECE`. **Fix:**
   replicate the device state through the same serializers a TTD checkpoint
   uses (Beta Disk, tape, mouse, AY, Covox, GS, model latches), and copy the
   cumulative T-state clock the devices time themselves by.
4. **Wrong T-state after the lock.** The lock happens inside the master's
   `OUT (#3D00)`, halfway through the instruction. The slaves were placed at
   the frame position instead of the master's exact position. **Fix:** run
   the slaves to the master's exact (frame, T-state), then add the
   instruction's tail (3 T) and the injected `JP` (10 T).

## 7. Step 4: live in unreal-qt

The owner asked: "can we look at the result?", and then: "why not wire it
into the UI and watch it live?"

**What was done:**

- **The master is an ordinary instance in the window.** It keeps its own
  loop, pacing, sound, debugger and recording.
- **A new frame-end hook** (`MainLoop::SetFrameEndHook`). At the end of
  every master frame the group runs the slaves to the master's position,
  applies queued keys to all four keyboards, and writes the composed
  picture into the master's screen.
- **The lock is performed inside the master's own `OUT` instruction.** So
  the same code works under the UI loop and in tests.
- **Opening a machine:** File → Open ZX-Poly…, drag and drop of a `.zxp`,
  and a command-line argument with `--zxpoly-model`.

The owner's verdict: "уиииии, работает!!!" ("it works!"). Commit `f680b058`.

**The 512×384 mode.** Video mode 5 is twice the Spectrum resolution. The
master's framebuffer is 1:1, which is right for screenshots and recording.
So the group now builds a separate double-size display frame (704×576: the
master's border scaled ×2 plus the real 512×384 composite). The window
shows that frame.

**A question that was not a bug.** The owner asked: "`fh.zxp` is
monochrome — by design or a bug after resets?" It is by design. `fh.zxp` is
a unit-test file from the reference emulator's format tests, not an adapted
game. Its planes are almost identical: CPU1 equals CPU0, and CPU2/CPU3
differ in 8 bytes. With four identical planes every pixel index is `0000`
or `1111`, which is black or bright white. The corpus README's plane-
difference measurements showed this at once.

## 8. Step 5: full platform compatibility

**The reset bug.** The owner reported: "after a reset, the three other
screens keep the previous program's picture." The slaves kept running the
old program, and the composer mixed their old screens with the master's new
one.

- **Root cause:** a real ZX-Poly system reset clears `#3D00`, unlocks the
  ports, parks the slaves and shows CPU0 alone. The group did not know a
  reset happened, and the core has no reset notification.
- **Fix:** every reset path (menu, automation, disk autostart, snapshot
  load) restarts the master's frame counter, so a counter that goes
  backwards is a system reset. The group then:
  - resets the platform state;
  - resets the slaves to `#0000` and parks them in WAIT;
  - returns to the loader phase.

The owner: "finish it to full compatibility, it won't take long." What
followed:

- **The coupled machine.** Unlocked, with `#3D00` D0 = 1, the slaves run
  their own code and talk to the master through the ports. The master's
  per-instruction hook catches them up after every master instruction.
- **The Test ROM:**
  - `.prom` loading;
  - RAM0 mapped at `#0000` by `#7FFD` D6 while unlocked (the ROM's
    RAM0→ROM check needs it);
  - the ROM's 8 checks pass on both models, and its mode 4 and mode 5
    demos render correctly.

  It passed on the first run. That looked suspicious, so the frames were
  checked as PNG before the result was believed.

Commit `78fb1005`. The owner: "commit to the branch and push to both
remotes — the experiment is a success. Now fix every gap you found."

## 9. Step 6: closing the gaps

Every mechanism of the board was implemented and covered by a test that
fails when the mechanism is switched off (a mutation check). The table is in
[prototype-results.md §5](prototype-results.md#5-full-platform-compatibility).
Highlights and the problems they brought:

- **Interrupts.**
  - Local INT (a 36 T pulse, `Z80::RaiseLocalInt`) for R0 D7, IO-window
    reads and the halt notification.
  - The common frame INT is gated per module (`Z80::frameIntMasked`).
  - **Problem:** the Test ROM broke once window writes sent an NMI. On the
    real board an NMI to a module that waits in WAIT expires before the
    module runs, and the Test ROM relies on that. **Fix:** an NMI to a
    waiting module is dropped.
- **Stop address and halt notification.** R2/R3 park a slave at an
  address; the HALT edge notifies the modules chosen in R1.
- **Floating bus.** The master's value for every module: one video memory
  drives the bus.
  - **Problem:** the first test passed with the mechanism switched off.
    The floating bus is off in the default config, and the test screen had
    no attribute data, so there was nothing to read.
  - **Fix:** the test enables `floatbus` and fills the attributes. Then
    the mutation check showed the test catching the fault.
- **Beam-accurate picture.** Each module captures a paper line when its beam
  passes it, and the composer works on those lines.
  - **Problem:** a hand-assembled test program had a wrong relative jump
    offset (`#E8` instead of `#E7`).
- **Hidden group members.** The slaves are left out of instance listings,
  index lookup and "most recent" selection, but stay reachable by ID.
- **Kempston mouse** gated and applied like the keyboard.
- **Group registry.** `EmulatorManager` owns groups by master ID. Removing
  the master removes the group.
  - **Problem:** `std::unique_ptr` of an incomplete type in the manager's
    header did not compile. **Fix:** `std::shared_ptr`.
- **Build problems:**
  - missing `ttd::` namespace qualifications;
  - a changed `ResolveEmulator` signature;
  - a private `openFromCommandLine` that the new code needed to call.
- **A 128K has no TR-DOS.** A multiloader disk on a stock 128K is refused
  with a message instead of a half-started machine.

## 10. Step 7: porting to every surface and keeping up with master

The project rule: every feature on CLI, WebAPI with OpenAPI, MCP, Lua,
Python and the Qt UI, each with its docs.

- **WebAPI:**
  - start as ZX-Poly (`"zxpoly": {...}`);
  - `GET /api/v1/emulator/{id}/zxpoly` for the group status;
  - a `zxpoly` block in the machine identity;
  - OpenAPI schemas.
- **MCP:** `zxpoly` and `zxpoly_file` on create, and a `zxpoly_status`
  action.
- **CLI:** `zxpoly start <model> [file]` and `zxpoly status`. There is also
  a new `UNREAL_CLI_PORT` variable, so a second instance can run next to
  the user's own.
- **Python and Lua:** `zxpoly_start` and `zxpoly_status`.
- **Recipe** [.recipe/machines/zxpoly.md](../../../.recipe/machines/zxpoly.md)
  and the interface docs.

**Named configurations** `ZXPOLY-48K`, `ZXPOLY-128K` and `ZXPOLY-PENTAGON`
(commit `83a8624c`):

- **One table** in the group; `EmulatorManager::CreateEmulatorWithModel`
  resolves the names, so every surface takes them.
- **Menu:** Machine → ZXPoly-48k / ZXPoly-128k / ZXPoly-Pentagon.
- **Model listings** show the configurations.
- **A 48K group refuses `.zxp` and the Test ROM** with the reason: ZX-Poly
  editions page through `#7FFD`. The Test ROM reports "ZX-128 BAD" there.

**Keeping up with master.** Master moved fast during the work: 67 commits
in one merge alone. Master was merged into the working branches five times,
and master was fast-forwarded to the result three times.

- **The one real conflict** was in `Z80::in`. Master had split the ULA I/O
  wait into a part before and a part after the port access (`IORQ`), and
  ZX-Poly had added a hook after the read. Both stay.
- **Master's change also exposed a ZX-Poly gap:** a port access consumed by
  the ZX-Poly interceptor skipped the after-IORQ wait. It is still a bus
  cycle, and on a 128K the module-register ports `#40FF`–`#7FFF` are in the
  contended range. **Fix:** the wait moved into one helper,
  `Z80::IoWaitAfterIorq`, called on every exit of `in()` and `out()`.
- **A merge with "media follow a model switch"** touched the same
  model-switch path; a test now switches a plain machine to a ZX-Poly
  configuration and back.

**A process slip.** The configurations work was done in a branch of the
conversation that the owner later rewound (`/rewind`). Git does not rewind,
so the commits stayed. After the rewind the assistant saw commits it did not
remember and reported them as another session's work. The same happened
with a leftover benchmark file (`portio_benchmark.cpp`). Both were in fact
this session's own work. The benchmark came from a performance comparison
against master in the rewound branch, which found no regression; the first
difference was load noise. The owner's visual report from that branch ("the
border turned black, character cells and stripes appeared") was never
investigated after the rewind; see [§15](#15-what-was-hard-and-what-we-learned).

## 11. Step 8: time travel, built and then fenced off

The group's own time travel was built in the core:

- the four TTD sessions start at the same frame;
- group input is journaled into each;
- the platform state is snapshotted per frame;
- a group seek and a branch.

**Two bugs on the way:**

1. **Seek refused while recording.** The TTD manager rejects a seek during
   recording. **Fix:** pause the four sessions first (the history stays),
   then seek, then resume from the current position.
2. **Replayed frames ran one instruction longer.** The group advanced the
   master with `Emulator::RunFrame`. That is the debugger's "frame step",
   which returns to a remembered T-state position, and a TTD seek resets the
   remembered position. After a seek, frame 60→61 ended at T 15 instead of
   T 4. The instruction traces were identical up to the last instruction;
   only the frame end differed. **Fix:** the group runs the master to the
   frame boundary (`RunUntilCondition` until the frame counter moves).

Then the owner decided: no TTD for ZX-Poly for now, and no video wall either
— low priority, many risks. The risks are listed in
[prototype-results.md §9](prototype-results.md#9-not-done-yet).

The first write-up claimed "no surface enables it". That was wrong: the
ordinary per-instance TTD commands were not blocked. They acted on the
master alone, and a seek split it from the slaves. The docs were corrected
to say so honestly. A later step then made it a hard refusal on every member
([§13](#13-step-10-polish)).

## 12. Step 9: performance

**Measure first.** A benchmark (`core/benchmarks/emulator/zxpoly_benchmark.cpp`)
split the cost of a group frame (Summer Santa, 4×Pentagon):

| Variant | Per frame |
|:--|--:|
| Group, slaves in parallel (the default) | 3.72 ms |
| Group, slaves one after another | 5.93 ms |
| Master alone with the group's per-instruction hook | 2.22 ms |
| Master alone without the hook | 2.16 ms |

Findings:

- **The per-instruction hook** (line capture) is cheap: 3%.
- **One slave frame** costs about 1.24 ms. Three in parallel take 1.5 ms.
- **At normal speed** a frame uses 3.7 ms of the 20 ms budget: nothing to
  gain there.
- **Only at unlimited speed** does the master wait for its slaves: it
  idles for more than a third of every frame.

**Design options, and the owner's choice.** Overlapping the slaves' frame
with the master's next one gives about 1.6×. The cost is either:

- a picture one frame late relative to the border and the sound; or
- the whole displayed frame delayed by 20 ms.

The owner chose: **pipelining only at unlimited speed (turbo)**, where the
picture is shown only now and then; normal speed stays exact.

**The rule for "no regressions".** Every module must have the same state at
every frame boundary in every schedule. Three mechanisms make that hold:

- **Input** of a boundary: the master takes it at once, each slave when it
  arrives at that boundary.
- **R0 status:**
  - a master read of a slave's R0 first waits for the slaves;
  - a slave read of the master's R0 gets the master's status as it was at
    the boundary.
- **Floating bus:** the master's floating-bus log keeps the previous frame
  and sits behind a lock.

**Implementation:**

- **`ZXPolyWorkers`:** three persistent worker threads instead of three new
  threads every frame. Measured separately, the pool alone gains nothing
  (±1%); it is what makes the overlap possible.
- **`WaitForSlaves()`** at every point that reads or changes the slaves:
  status, compose, loading, the lockstep check, a master R0 read, and a
  shown frame.

**Result:** 1.4 ms per frame instead of 2.4 ms at unlimited speed (1.75×).
The floor, the master alone, is 1.15 ms.

**Problems on the way:**

- **A new test was not deterministic.** Two synchronous runs of the same
  program differed. Power-on RAM is not zero, and the test hashed all
  eight RAM pages. **Fix:** zero the RAM first.
- **Benchmark numbers three times too high.** Many sessions share the
  development machine; its load average reached 121. A group frame
  measured 11.4 ms instead of 3.4 ms. **Fix:** wait in the background
  until the load drops below 12, then run twice.
- **The mutation checks paid off.** Four deliberate faults were each caught
  by a test:
  - a slave reading the master's live status;
  - the master not waiting before reading a slave;
  - slave input applied before the boundary;
  - the floating-bus log pruned too early.

**The host speed control ×2…×16.** The owner said: "try, measure, decide."
Measured per shown frame against the 20.48 ms budget:

| Speed | Parallel (kept) | With overlap (not used) |
|:--|--:|--:|
| ×2 | 5.6 ms | 4.0 ms |
| ×4 | 9.7 ms | 7.1 ms |
| ×8 | 17.8 ms | 12.8 ms |
| ×16 | 30.7 ms (runs at about ×10.7) | 21.0 ms |

**Decision: no overlap at ×N:**

- Up to ×8 the parallel schedule fits the budget.
- At ×16 overlap would help, but every frame is shown, so it would cost a
  picture one frame late, and screenshots and recording would lag with it.
- Turbo is faster than any ×N anyway: 1.4–1.65 ms per machine frame
  against 1.9 ms at ×16.

## 13. Step 10: polish

**Real bugs found during verification:**

1. **False divergence in the status.** In turbo, 5 of 20 status requests
   reported a divergence; the same was true on the build before pipelining.
   - *Cause:* the check compared the slaves with the *live* master. A
     request from another thread (WebAPI, MCP) caught the master already
     inside its next frame. At normal speed the master mostly sleeps at the
     boundary, which hid the race.
   - *Fix:* the master's control state is snapshotted at the boundary, and
     each slave compares itself with it when it arrives. Result: 0 of 20.
     A slave on another branch is still detected.
2. **Host speed ×N broke the machine.** Only the master took the
   multiplier; the slaves' frames stayed short and diverged at once.
   - *Fix:* the slaves take the master's multiplier before crossing the
     same frame start.
   - A second fault hid behind the first: the slave's safety limit (two
     frames) stopped a slave short of the master at ×4. It now scales with
     the multiplier.
3. **The multiplier fix did not hold live.** Diverged: 9 of 10 requests at
   ×8, and 8 of 10 after returning to ×1.
   - *Symptom:* a headless test through the same frame loop showed no
     divergence at all. A debug build with instruction tracing showed none
     either: tracing slows the CPU and hides the timing.
   - *Cause:* a race. The UI, WebAPI or CLI writes the multiplier from its
     own thread at any moment. The group copies it at the start of the
     frame-end hook, but the master applies it only after the hook. At ×4
     and above the slaves' frame fills most of the hook, so a change nearly
     always landed in the window: the master took it one frame before the
     slaves, and the machine diverged for good.
   - *Fix:* `Emulator::SetSpeedMultiplier` hands a group master's change to
     the group (`SetSpeedChangeInterceptor`). The group queues it like
     input and applies it to all four at one boundary.
   - *Test:* a speed change requested from a slave's worker thread during
     the hook. Without the interceptor the test fails at frame 0.
4. **The picture at ×N showed the wrong moment.** The line capture compared
   the CPU's T-state (scaled ×N) with unscaled line positions, so a
   stretched frame took all its lines from its first 1/N. *Fix:* divide by
   the multiplier, as `Screen::GetCurrentTstate` does.

**Time travel refused on every member.** `TimeTravelManager::SetUnavailableReason`:

- **Refused:** recording (and so the DeZog live history) and loading a
  `.ttd` file.
- **Shown everywhere:**
  - WebAPI answers 409, and MCP reports it as an error;
  - the status carries `unavailable_reason` (WebAPI, MCP, Lua, Python);
  - the CLI prints it in the status and on a failed start, and gdb on a
    failed start;
  - the Qt TTD panel disables its buttons and shows the reason as a
    tooltip.
- **Found on the way:** MCP reported "recording started" even when the
  start failed.

**Cleanup:** the three temporary worktrees and both ZX-Poly branches were
removed locally and on both remotes once everything was in master.

## 14. Bug catalogue

| # | Symptom | Root cause | Fix | Found by |
|:--|:--|:--|:--|:--|
| 1 | Lockstep alarm right after input starts | Data registers compared; they hold plane bytes mid-draw | Compare control state only | corpus test |
| 2 | Atw2 slaves with `#7FFD=#77`, garbage screens | Window write to `#7FFD` routed into the slave's latch | `#7FFD` pages the master unless R1 b5 | TRD test |
| 3 | ZX-Word diverges after the lock (FDC poll at `#3ECE`) | Device state and cumulative clock not replicated | Replicate through the TTD serializers + `t_states` | port-read log |
| 4 | T-state mismatch right after the lock | Slaves placed at frame position, not the master's exact one | Run slaves to (frame, T), add the `OUT` tail | lockstep check |
| 5 | Old screens on the slaves after a reset | Group unaware of a system reset | Detect frame counter going backwards → system reset | owner, live |
| 6 | Test ROM failing once window NMI was added | NMI to a waiting module delivered | Drop NMI to a waiting module | Test ROM |
| 7 | Floating-bus test passes with the feature off | Feature off by default; no attribute data | Enable `floatbus`, fill attributes; mutation-check | mutation check |
| 8 | Beam test wrong | Hand-assembled `JR` offset `#E8` | `#E7` | test |
| 9 | Build: incomplete type in `unique_ptr` | Header dependency | `shared_ptr` | compiler |
| 10 | Group TTD seek refused | Seek not allowed while recording | Pause sessions, seek, resume | test |
| 11 | Replay one instruction long after a seek | `Emulator::RunFrame` is a debugger step anchored to T | Run to the frame boundary | test + trace diff |
| 12 | Intercepted ports skip the after-IORQ wait | Master's contention split vs. early returns | `Z80::IoWaitAfterIorq` on every exit | merge review |
| 13 | Non-deterministic new test | Power-on RAM not zero, hashed | Zero RAM in the test | sync-vs-sync run |
| 14 | False divergence in status (5/20 in turbo) | Live master compared from another thread | Compare at the frame boundary | live check |
| 15 | Machine diverges at ×2 | Multiplier not given to slaves | Copy before the frame start | live check |
| 16 | Slave stops short at ×4 | Fixed two-frame safety limit | Scale with multiplier | test |
| 17 | Machine diverges at ×4/×8 live | Cross-thread speed write races the hook | Speed change through the group's queue | live check |
| 18 | ×N picture from first 1/N of the frame | Scaled T vs. unscaled lines | Descale T | review, test |
| 19 | MCP "recording started" on failure | Response flag ignored | 409 from WebAPI → MCP error | while adding the refusal |

Not bugs, but questions that looked like bugs:

- **`fh.zxp` is monochrome:** it is a test file with near-identical planes.
- **The first benchmark "regression":** it was load noise.

## 15. What was hard and what we learned

**Technical:**

- **The lock is the whole design.** Once it was clear that locked modules do
  not interact, four stock instances became the obvious design, and the
  hard problem (two CPU cores in exact step) disappeared.
- **Replication is a TTD checkpoint.** Copying "the CPU and the RAM" is not
  enough. The device state and the clock the devices time themselves by must
  come along. The TTD serializers already capture exactly that set.
- **Compare what must be equal, not what happens to be equal.** Data
  registers differ legitimately between planes; control state never does.
- **A frame is not a debugger step.** Code that means "one machine frame"
  must run to the frame boundary.
- **Cross-thread writes need a boundary.** Keys, mouse and speed all reach
  the group from other threads. Everything that changes the machine goes
  through the group's queue and is applied at a frame boundary on the
  master's thread.
- **Debug instrumentation can hide timing bugs.** The speed race vanished
  under instruction tracing. The fix came from reasoning about the window,
  then a test that reproduces the window on purpose.

**Process:**

- **Mutation checks.** Several tests passed with the mechanism switched off
  (the floating bus, the first beam test). Every mechanism test was checked
  by breaking the mechanism on purpose.
- **Measure on a quiet machine.** On a shared machine, check the load
  average before believing a benchmark.
- **Live checks find what tests do not.** The status race and the speed race
  only showed in the running application.
- **Git outlives `/rewind`.** After a rewind, commits and files from the
  rewound branch remain. They must be recognized as one's own work, not
  reported as someone else's.
- **Open item:** in the rewound branch the owner reported a visual artifact
  in the running application ("the border turned black, character cells and
  some stripes appeared"). After the rewind it was not investigated. It
  should be reproduced before calling the video path finished.

## 16. Result in numbers

| Item | Count |
|:--|:--|
| New core code (group, composer, interceptor, workers, `.zxp` loader) | 3170 lines in 10 files |
| Tests and benchmark | 1815 lines; 40 test cases, most run on both models (about 90 test runs) |
| Changes to shared code | port-interceptor pointer in `Z80::in/out`, local INT and frame-INT gate in `Z80`, host-input gates in keyboard and mouse, frame-end hook in `MainLoop`, `RunUntilCondition` debugger flag, hidden group members, speed-change interceptor, TTD unavailable reason |
| Surfaces | WebAPI + OpenAPI, MCP, CLI, Python, Lua, Qt (menu, open dialog, drag and drop, command line) |
| Test corpus | 7 `.zxp`, 2 multiloader TRDs with loader sources, 9 Sprite Corrector projects, the Test ROM and its source |
| Content in lockstep | the whole public corpus, on 4×128K and 4×Pentagon; the Test ROM's 8 checks |
| Full test suite at the last merge | 4779 tests, 0 failures, no compiler warnings |

## 17. Session log: commits

Local time (UTC−4). All commits by Ilia Sharin with the assistant.

| Time | Commit | What |
|:--|:--|:--|
| 09-27 16:43 | `ab25b54b` | *(before the session, the brief's input)* the port analysis |
| 09-27 22:32 | `95fce44d` | Docs: quad-instance architecture, verified platform notes, Spec256 research |
| 09-28 17:19 | `7292cd41` | Docs + test data: model-agnostic sync layer, `.zxp` storage model, the full test corpus (to master) |
| 09-28 18:52 | `f680b058` | The machine: quad-instance ZX-Poly on stock models, `.zxp` loader, live in unreal-qt |
| 09-28 19:27 | `78fb1005` | Full platform compatibility: coupled machine, Test ROM, 512×384 display, system reset |
| 09-28 20:52 | `bda29faf` | Interrupts, beam capture, group registry, automation surfaces, group TTD |
| 09-28 20:59 | `27c5dd60` | Merge master: I/O contention split (the `Z80::in` conflict), PLUS2/PLUS2A, media slots |
| 09-28 21:19 | `83a8624c` | Named configurations `ZXPOLY-48K/128K/PENTAGON` on every surface (rewound branch) |
| 09-28 21:26 | `33c9e551` | Merge master: media follow a model switch, GS/Covox state reports |
| 09-28 22:37 | `58343e9c` | Docs: time travel and video wall deferred |
| 09-28 22:38 | `7470561f` | Merge master: MoonSound state reports; **master fast-forwarded here** |
| 09-29 01:44 | `a73bf1ef` | Pipelined slaves at unlimited speed; lockstep at the boundary; host speed fixes |
| 09-29 01:47 | `28a12320` | Merge master; **master fast-forwarded here** |
| 09-29 11:13 | `9fce87a2` | Time travel refused on every member; beam capture at host speed |
| 09-29 11:14 | `a79a3955` | Merge master; **master fast-forwarded here** |

Every push went to both remotes (GitHub and the internal GitLab).

## 18. Time accounting

**How it was measured.** From the session transcript's timestamps. Each tool
call counts until the next event and is assigned to a category by its phase
window and by what it touched:

- tests: test files, test runs;
- porting: `core/automation`, `unreal-qt`, merges;
- verification: live runs, WebAPI/CLI checks, benchmarks;
- coding: the rest.

Waiting for the owner (a gap of more than 10 minutes that ends with a
message from the owner) is excluded. Waiting for builds and tests is shown
separately; it is an underestimate, because a few long builds ran inside a
single tool call. The numbers are approximate to about ±10 minutes per
category.

**Calendar span:** 2026-09-27 21:18 → 2026-09-29 14:29, about 41 hours with
long breaks (overnight, the owner's day).

**Active work: about 8.5 hours**, plus at least 1 hour of machine time for
builds, test runs and benchmarks.

| Category | Time | Share |
|:--|--:|--:|
| Analysis of the source material (platform docs, reference emulator, engine, corpus sourcing) | 1 h 05 min | 13% |
| Writing the specification (architecture, sync layer, storage model) | 0 h 47 min | 9% |
| Proof of concept (loader, group, composer, corpus in lockstep) | 0 h 40 min | 8% |
| Coding (platform mechanisms, fixes, scheduling) | 1 h 55 min | 23% |
| Verification (live runs, WebAPI/CLI checks, measurements, diagnostics) | 1 h 19 min | 15% |
| Porting (Qt, WebAPI, MCP, CLI, Lua, Python, merges with master) | 0 h 31 min | 6% |
| Tests (writing tests, mutation checks, suite runs) | 1 h 41 min | 20% |
| Version control and housekeeping (commits, pushes, worktrees, cleanup) | 0 h 24 min | 5% |
| Docs during implementation (results, recipe, interface docs) | 0 h 07 min | 1% |
| **Total** | **≈ 8 h 30 min** | |

Porting is undercounted: many surface edits were made in the same steps as
the core code and counted as coding. The docs-during-implementation time is
low because doc edits were mostly done inside larger scripted steps.

**By phase:**

| Phase | Local time | Active | Main content |
|:--|:--|--:|:--|
| Analysis | 09-27 21:18–21:56 | 0:37 | Source check, contradictions, the lock insight |
| Specification | 09-27 21:56–22:40 | 0:43 | Architecture rework with the owner's decisions |
| Specification | 09-28 16:19–16:57 | 0:03 | Model-agnostic sync layer (mostly the owner reading) |
| Analysis | 09-28 16:57–17:25 | 0:27 | `.zxp` storage model, test corpus |
| POC | 09-28 17:40–18:21 | 0:40 | Loader, group, composer; corpus in lockstep |
| Live in unreal-qt | 09-28 18:21–18:54 | 0:32 | Frame-end hook, opening, 512×384 display |
| Full compatibility | 09-28 18:54–19:30 | 0:35 | System reset, coupled machine, Test ROM |
| Gaps | 09-28 19:30–20:52 | 1:21 | Interrupts, floating bus, capture, registry, surfaces, group TTD |
| Configurations, merges | 09-28 20:52–21:30 | 0:37 | Named configurations, two master merges |
| Status, deferral | 09-28 21:30–22:45 | 0:10 | TTD and video wall deferred, master sync |
| Performance and fixes | 09-28 22:45–01:47 | 2:05 (+0:55 machine) | Pipelining, lockstep at the boundary, host speed |
| Merge to master | 09-29 01:47–02:30 | 0:02 | Build, tests, fast-forward, push |
| TTD refusal, polish | 09-29 10:24–11:30 | 0:23 (+0:10 machine) | Refusal on every surface, ×N picture, merge |
| Cleanup | 09-29 14:18–14:29 | 0:10 | Worktrees and branches |

Not counted: the port analysis written before the session (the brief's
input), and the writing of this document.
