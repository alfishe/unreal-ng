# ProfROM Magic-NMI Hang — Verified Root Cause & Fix Plan

Investigation of the reported Scorpion ProfROM defect: *"after NMI and getting to the
service monitor menu, the code hangs or loops — no reaction on keyboard; interrupts
disabled."*

All live evidence below was reproduced against the running release build
(`cmake-build-release/bin/unreal-qt.app`, commit `9e85136e` + working tree) over the
WebAPI, image `data/rom/scorp_prof401.rom` (512 KB, 8 quadrants), model `PROFSCORP`,
RAM 1024 KB. Claims that could not be reproduced were removed; every earlier hypothesis
is graded in §5.

---

## 1. Root cause (VERIFIED)

**The magic-button NMI maps the entry ROM page of whatever ProfROM plane is current.
The service monitor exists only in plane 0. Whenever the firmware is in an extension
plane (1-3) - which it is after the 128 menu times out, and during the boot ROM-disk
scan - `#0066` is fetched from that plane and is a dead border-flash stub. That is the
yellow/black stripes and the hang.**

- `Emulator::RequestMNI` arms `scorpionDosTrigger` -> page 3 **of the current plane**
  (`ResolveScorpionRomBases(state.profrom_bank)`), then pulses /NMI -> `#0066`.
- Plane 0: page-3 `#0066` -> `#2A56` -> `#0807` -> `OUT (#1FFD),#12` -> plane-0 monitor. Works.
- Planes 1-3: every page's `#0066` is the firmware's "wrong plane" stub
  (`materials/Scorpion_ProfROM_Paging.md` §4 rule 6):

  ```
  #0066:  3E 06   LD A,#06        ; yellow
          D3 FE   OUT (#FE),A
          AF      XOR A           ; black
          D3 FE   OUT (#FE),A
          18 F7   JR #0066        ; infinite DI loop - stripes, no keyboard
  ```

- The loop is `DI`, so the frame-interrupt keyboard scan can never run: "no keyboard,
  interrupts disabled".

The plane register itself is correct (strobe, table and gate match the GAL and ZXMAK2,
§4). The defect is that the button does not select plane 0 before mapping the entry
page. Both reference emulators additionally refuse the magic NMI while `PC < #4000`
(ZXMAK2 `BusNmiRq` cancel; UnrealSpeccy `main_nmidos` defer) - a guard this build
removed (`implementation-plan.md:229`); with plane-0 selection in place that guard is
optional (§6.2).

---

## 2. Reproduction — the hang is deterministic on PC (VERIFIED)

Fresh `PROFSCORP` instance; fire `POST …/nmi {"magic":true}` at a controlled moment;
read PC and the `#0101` plane signature 0.7 s later:

| Condition at button press | PC (pre) | ROM page | Result | n |
|---|---|---|---|---|
| **Idle** — 128 menu settled | `#3683` (ROM) | 0 (plane 0) | monitor entered, **works** | **0 / 12 hang** |
| **Mid-boot** — plane-1 scan | `#0318` (ROM) | 7 (plane 1) | `#0066` border-flash | **8 / 8 hang** |

- Every hung run: PC pinned at `#0066`–`#006D`, `#0101 = 06/07` (plane 1), IFF1 = 0.
- The earlier "≈4/6 hang at idle" observation was an artefact of running many emulator
  instances at once: host-CPU contention slows the *emulated* boot, so a fixed wall-clock
  wait lands the button mid-boot. With instances stopped between runs the idle case never
  hangs (0/12) and the mid-boot case always hangs (8/8).
- Base `SCORPION` (single plane) cannot hit this: its only service page is the monitor,
  so `#0066` always lands on it. Verified: base MNI enters and exits cleanly (E2E-3).

**GUI "100 % hang at the 128 menu" - resolved:** the menu is not idle. After ~10 s
without a key it times out and the firmware moves into plane 1 and stays there
(timeline in §6.0). A press after that maps plane-1 page 7 -> stripes, every time. The
headless idle presses above were all made within 6-15 s, before the timeout.

Why a press can still land mid-boot "after the menu": in the GUI the emulator can run below real time
(audio underruns are visible in the app log), so the boot scan finishes later in
wall-clock than it does headless; a button press that feels "after the menu" can still
land during the plane-scan. The fix removes the dependence on timing entirely.

---

## 3. Secondary effect — keyboard needs a sustained hold (VERIFIED)

Independent of the hang: when the monitor *does* run (plane 0), its page-6 key scanner
debounces at `#07E0`/`#07E4` (`LD DE,(#E051) / DEC D / … / RET NZ`) and only enqueues a
key into the ring at `#E116` after ~15–30 frames of continuous contact. Verified live: a
30-frame hold advances the ring head (`91e3`→`94e3`); a 2-frame tap does not. Short
WebAPI/GUI taps are dropped as switch bounce and look like a dead keyboard. This is
firmware behaviour; any monitor test must use `frames >= 25`. Worth checking that the GUI
key-injection path holds keys long enough.

---

## 4. Reference-emulator facts (VERIFIED against source at `/Volumes/TB4-4Tb/.../github`)

| Mechanism | ZXMAK2 | UnrealSpeccy | This build |
|---|---|---|---|
| **Magic-NMI PC gate** | cancel if `PC<#4000` | defer until `pc>=#4000`, drop after 50 frames | absent (optional once §6.1 is in) |
| ProfROM plane strobe | `RdMemM1`+`RdMem`, mask `0xFFF0`, gate `SYSEN` | any read (dbg), 4 addr | `RdMem`+M1, mask `0xFFF0`, gate = service ROM mapped — matches ZXMAK2/GAL |
| DOS-session leave | `SubscribeRdMemM1` (M1 only) | `if (m1 && flgDOS && RAM)` | any read `>=#4000` (no M1 gate) |

The GAL (`materials/Scorpion256TPlus_GAL_decoded.md` §3) clocks the plane on every ROM
read (M1 included) with A0/A1 unbonded, so our `0xFFF0`/M1-included strobe is the
hardware-faithful one; the transition table matches all references.

---

## 5. Status of earlier hypotheses

| Claim | Status | Note |
|---|---|---|
| Magic NMI maps the current (non-zero) plane's `#0066` stub | **VERIFIED (§1, §6.0)** | The root cause. 8/8 mid-boot, 100 % after menu timeout, 0/12 in plane 0. |
| Missing `pc>=#4000` magic-NMI gate | **VERIFIED divergence, optional** | References have it; unnecessary once plane 0 is selected on the button. |
| Keyboard needs long holds (`#E051` debounce) | **VERIFIED (§3)** | Real, compounding, not the hang. |
| DOS-trigger release should require M1 (`isExecution`) | **PLAUSIBLE, not the cause** | Code does lack the M1 gate and references gate on M1; a legitimate fidelity fix, but the `#0814` crash it predicted was never observed. |
| ProfROM strobe should be data-only + mask `0xFFF3` | **REFUTED** | Contradicts the GAL and ZXMAK2; our `0xFFF0`/M1 form is correct. `#0101` read = selector 0 = hold, no plane change. |
| Plane "reverts to page 2" corrupting the menu | **REFUTED** | The hang is stuck in plane 1 / page 6, the opposite. |
| Quoted "current code" (`quadrant=(addr>>2)&3; …`) | **STALE** | Not the working tree; live path uses `ScorpionRomWindow::OnRomRead` + the verified table. |

---

## 6. Fix plan (concrete)

> **Status 2026-09-10: §6.1 applied in the working tree** (`emulator.cpp` `RequestMNI`:
> `GetScorpionRomWindow().Reset(state)` before arming the trigger on `MM_PROFSCORP`), plus
> `ScorpionMniEmulator_Test.ProfRomMagicButtonSelectsQuadrantZero`
> (`scorpionmni_test.cpp`). `ninja core-tests unreal-qt` clean, 0 warnings;
> `--gtest_filter="*Scorpion*:*Nmi*:*Mni*"` 103/103 pass. Live: one post-fix MNI on a
> fresh PROFSCORP instance entered the monitor (`#0101 = 02`, key Down moved the
> highlight). GUI confirmation of the 100 % case (F11 after the menu timeout) still owed.
> §6.2/§6.3 not applied.

### 6.0 Where the machine really is when F11 is pressed (VERIFIED, headless timeline)

The "idle 128 menu" is not idle. Trajectory of a `PROFSCORP` boot, no input
(`pc` / active ROM page, sampled every 2 s):

```
 2s  #031D  page 7   plane-1 DOS page   (boot ROM-disk scan)
 4s  #00E5  page 0   plane-0 ROM0
 6s  #3685  page 0   128 menu, plane 0            <- MNI works here (0/12 hang)
14s  #075C  page 6   plane-1 service   (menu timed out; 4.01 menu loop)
16s  #2883  page 2   plane-0 service   (4.01 menu render loop)
26s  #11AB  page 5   plane-1 page 1    (ProfROM tool page - no "1982 Sinclair"
                                        string, "Prof" string present; stays here)
```

After ~10 s without a key the 128 menu times out and the firmware moves into the
ProfROM 4.01 menu, then into a plane-1 tool page, and stays in **plane 1**. A magic press
there maps plane-1 page 7 and vectors to its `#0066` = the yellow/black stripe stub.
The GUI hang is 100 % because by the time a user reaches for F11 the timeout has
already fired; the headless runs pressed within 6-15 s and hit plane 0.

### 6.1 Primary fix - the magic NMI must map plane 0 (ROM-page mapping)

`Emulator::RequestMNI()` (`core/src/emulator/emulator.cpp:765`) selects the entry page
(`scorpionDosTrigger` -> page 3) **of whatever plane is current**. The service monitor
and its `#0066` entry chain exist only in plane 0; planes 1-3 carry a border-flash stub
at `#0066` of every page. So before mapping the entry page the button must select
plane 0:

```cpp
if (config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP)
{
    if (config.mem_model == MM_PROFSCORP)
    {
        // Magic button: the monitor lives in quadrant 0 only. Select it before
        // the entry page is mapped so #0066 is fetched from plane-0 page 3
        // (firmware chain #0066 -> #2A56 -> #0807 -> OUT (#1FFD),#12 -> plane-0
        // monitor). Emulator-side decision: hardware keeps the GAL plane, which
        // is why planes 1-3 carry the border-flash #0066 stub; the emulator's
        // button exists to reach the monitor, so it resets the plane.
        _context->pMemory->GetScorpionRomWindow().Reset(state);   // profrom_bank = 0, p7EFD = 0
    }
    state.scorpionDosTrigger = 1;
    _context->pMemory->UpdateZ80Banks();   // ResolveScorpionRomBases(0) + page 3 at #0000
}
_core->GetZ80()->RequestNonMaskedInterrupt();
```

`ScorpionRomWindow::Reset()` already exists (`scorpionromwindow.cpp`) and clears both the
GAL state and the `#7EFD` window latch, so quadrant 0 is selected for 128 K-2 MB images
alike. `UpdateScorpionBanks()` then calls `ResolveScorpionRomBases(0)` and maps page 3
of quadrant 0. No other code path changes.

Consequences to document (hardware-reference §9, design §4.2 "plane survives"):
- Entering the monitor from inside a plane-1..3 tool now works and the monitor exit
  returns to plane-0 BASIC/TR-DOS - which is what the firmware's own exit stubs do
  anyway (`Scorpion_ProfROM_Paging.md` §4 rule 5).
- Not hardware-literal: on real hardware a press inside an extension plane shows the
  stripes. If a hardware-literal mode is ever wanted, gate the reset on a config flag.

Also fix the stale claim in `hardware-reference §9` / `RequestMNI` comment: the button
maps *page 3 of the current plane*, and that is exactly why it failed.

### 6.2 Secondary hardening - reference PC gate (optional)

ZXMAK2 cancels and UnrealSpeccy defers the magic NMI while `PC < #4000`. With 6.1 in
place this is no longer needed to avoid the stripes; keep it out unless a hardware-
literal mode is added, because at the 128 menu (`PC = #3683`, ROM) it would defer the
button until RAM code runs.

### 6.3 Independent fidelity fix (optional, not the cause)

M1-gate the magic DOS-trigger release, matching Xpeccy/ZXMAK2, in `MemoryReadFast` and
`MemoryReadDebug` (`core/src/emulator/memory/memory.cpp`):

```cpp
if (_scorpionDosTriggerActive && isExecution && addr >= 0x4000) [[unlikely]] { ... }
```

Do **not** change the ProfROM plane-strobe mask or add `!isExecution` to it (§4/§5).
The Qt debugger reads only through `DirectReadFromZ80Memory` (verified in
`disassemblerwidget.cpp`, `memory16kbwidget.cpp`, `stackwidget.cpp`, `debuggerwindow.cpp`),
which never clocks the plane strobe - the debugger does not interfere with planes.

## 7. Verification protocol

1. `ninja -C cmake-build-release` — zero warnings.
2. `./cmake-build-release/bin/core-tests --gtest_filter="*Scorpion*:*Nmi*:*Mni*"` green.
   Add a unit test: on `MM_PROFSCORP`, set `PC < 0x4000`, `RequestMNI()`, step — assert
   the NMI is **not** taken and PC does not become `#0066`; then set `PC >= 0x4000`, step
   — assert the NMI is taken, `scorpionDosTrigger` armed, PC = `#0066`, `#C000` banking
   preserved.
3. Live regression (the pre-fix failure): boot `PROFSCORP`, wait 30 s (firmware now in
   plane 1, page 5), fire `…/nmi {"magic":true}`. Pre-fix: yellow/black stripes, PC in
   `#0066`-`#006D`. Post-fix: `#0101 = 02`, ROM page 2, monitor menu, keys work. Repeat at
   2 s (mid-boot, plane 1): pre-fix 8/8 stripes, post-fix 0/8.
   Unit test: `MM_PROFSCORP`, set `profrom_bank = 1`, `RequestMNI()`, step - assert
   `profrom_bank == 0`, `#0000` maps quadrant-0 page 3, PC = `#0066`.
4. Live sanity: at the settled 128 menu, MNI enters the monitor (`#0101 = 02`), a
   `frames=25` key hold advances the `#E116` ring and dispatches, and the documented exit
   returns to BASIC with `#C000` intact.
5. (Resolved - see §6.0.) GUI reproduction notes: in unreal-qt, boot `PROFSCORP`, wait for
   the 128 menu, press F11, then hold a cursor key ≥1 s. Leave the instance running
   (not paused). Over the WebAPI on that instance record: registers (PC, IFF1), ROM
   `active_page`, `#0101`, whether PC sits in `#0066`–`#006D` (border-flash) or the
   `#2800`/`#0700` menu loop, and a port trace of `IN #xxFE` values while the key is held
   (does the GUI key reach the matrix: row byte ≠ `0xFF`?). Also note whether the border
   flashes yellow/black.

---

## 8. Hardware turbo: detection fails, then the monitor runs 7 MHz anyway (VERIFIED 2026-09-10)

Observed in the GUI after the §6.1 fix (monitor now enters correctly): audio
"hard resync - dropped ~120-140 ms of overfilled audio" every frame, the monitor screen
blinking on alternate frames, and the Turbo on/off item disabled in both the base and
the ProfROM service menus.

### 8.1 Measurements (live, WebAPI, `run_tstates` 139776 T vs frame counter)

| State | frames per 139776 T | `#E02D` (monitor cfg, bit 6 = turbo) | turbo strobes seen |
|---|---|---|---|
| after boot, 128 menu | **2** (3.5 MHz) | `00` | `IN #1FFD` x2 at `#04DD/#04DF` (turbo OFF, boot) |
| inside the service monitor | **1** (7 MHz) | `00` | `IN #7FFD` x2 at `#04DD/#04DF` (turbo ON, MNI entry) |

So the monitor **switches turbo on** at entry (`#0261: CALL #04D5` = `LD B,#7F; LD C,#FD;
IN A,(C); IN A,(C)`), immediately measures the speed, concludes "no turbo hardware"
(`#E02D` bit 6 stays 0 -> menu item disabled), and never switches it off again -> the
machine keeps running at 7 MHz.

### 8.2 Cause 1 - strobe latency (detection)

Firmware speed test (`#2C1F`, called right after the strobe at `#0269` and from `#2C30`):
an interrupt-bounded count loop of ~100 k T-states. At 7 MHz (139 776 T/frame) it
completes before the next INT -> "turbo present" (`#E02D |= #C0`); at 3.5 MHz
(69 888 T/frame) the INT hits -> "no turbo".

Hardware: the flip-flop takes effect on the next cycle (GAL `TRB` latch,
`Scorpion_Turbo_Mode.md`); Xpeccy `compSetTurbo()` calls `comp_update_timings()`
immediately. **This build applies the strobe only at the next frame boundary**
(`Z80::ApplyQueuedFrequencyMultiplier` is called at `Z80FrameCycle` entry;
`frameLimit`/`int_start`/`int_end` are locals computed once per frame - `z80.cpp:404-412`
and the ten stepping paths in `emulator.cpp:1978-2721`). The loop therefore runs its
first ~70 k T at 3.5 MHz, the INT hits, detection fails - while the flip-flop is set.

### 8.3 Cause 2 - hardware turbo is folded into the host speed multiplier (audio/video)

`current_z80_frequency_multiplier = host << scorpion_turbo` (`z80.cpp:379`). Every
consumer treats it as "the frame runs N× faster in wall-clock", which is what the
**host** speed control means, but hardware turbo keeps the frame at 20 ms real time and
only doubles CPU T-states per frame (AY, FDC, video have their own clocks - hardware-
reference §13):

- `SoundManager::handleFrameEnd` (`soundmanager.cpp:432`): `samplesThisFrame` derives from
  `config.frame * multiplier` -> **2× samples per 20 ms frame** -> the ring overfills and
  the app drops ~120-140 ms per resync. Same in `Covox::handleFrameEnd` (`covox.cpp:80`).
- `SoundChip_TurboSound::handleStep` (`soundchip_turbosound.cpp:50`) scales `t` by the
  multiplier for the AY PLL - correct only for host speed.
- `tape.cpp:525/692` scale tape timing by the multiplier (documented pragmatic choice).
- Video: `Screen::GetCurrentTstate` descales `t` (correct). The alternate-frame blink is
  **not yet root-caused**; re-check after 8.4/8.5 (it may be the menu's own flash driven
  by the wrong detection result). If it persists, audit per-frame render paths that
  compare scaled `cpu.t` against unscaled raster positions.

### 8.4 Fix - apply the turbo strobe immediately (APPLIED 2026-09-10)

> Implemented: `Z80::ApplyHardwareTurboNow` / `RecomputeFrameTiming`, called from
> `PortDecoder_Scorpion256::DecodePortIn`; `Z80FrameCycle` reads the frame geometry from
> members. Verified by `ScorpionTurboDetect_Test` (real ROMs, both models, 8/8 phases
> set the firmware flag) and `ScorpionMachine_Test.TurboStrobeAppliesMidFrame`.
> Base v2.9x monitor uses the same `#0553` loop with flags at `#DFF8` (=`#C0`) and
> `#DFDD` bit 4 (enabled), entry `#0211`.

- Make `frameLimit`, `int_start`, `int_end` Z80 members recomputed by one function
  (`RecomputeFrameTiming()`), read every loop iteration in `Z80FrameCycle` and in the
  `emulator.cpp` stepping paths (replace the ten local copies).
- On the strobe (`PortDecoder_Scorpion256::DecodePortIn`, turbo arm): call
  `Z80::ApplyTurboNow()` which, if the effective multiplier changes mid-frame, rescales
  the in-frame position `cpu.t = cpu.t * new / old` (and `eipos`, `haltpos` likewise),
  updates `current_z80_frequency_multiplier`, and calls `RecomputeFrameTiming()`. INT
  position stays at the same *raster* instant. Keep the frame-boundary path for host
  speed changes.
- Expected result: `#0269` loop runs at 7 MHz from its first iteration -> `#E02D = C0`
  (and `#DFF8 = C0` in Base ROM), the menu's Turbo item becomes enabled in both ROMs;
  toggling the item stages bit 6 in RAM, and `IN #1FFD` issued on monitor exit
  (`#04CE` in ProfROM, `#048C` in Base ROM) returns the machine to 3.5 MHz.

### 8.5 Audio overfill - exact mechanism (VERIFIED from code) and fix (APPLIED, commits aab04e27 + fbd89355)

**Mechanism.** Wall-clock frame pacing never changes: `MainLoop` releases one frame every
`config.frame_duration_us` (20 ms) regardless of the multiplier (`mainloop.cpp:151`).
With the Scorpion turbo flip-flop set, `current_z80_frequency_multiplier` = 2, and the
audio path scales *everything* by it:

| Site | Expression | Effect at turbo (x2) |
|---|---|---|
| `SoundManager::handleFrameEnd` (`soundmanager.cpp:432-436`) | `samplesThisFrame = frame*mult*rate / 3.5 MHz` | 1764 instead of 882 samples per 20 ms frame |
| `Beeper::handleFrameEnd` / `Covox::handleFrameEnd` | `blip_end_frame(frame*mult)` with blip clock fixed at `CPU_CLOCK_RATE` (3.5 MHz, `beeper.cpp:26`, `covox.cpp:23`) | blip emits 2x samples |
| `SoundChip_TurboSound::handleStep` (`soundchip_turbosound.cpp:50`) | `t * mult` into the AY PLL | AY emits 2x samples |

So every 20 ms the core pushes 40 ms of audio into the ring; the Qt consumer drains
20 ms; the ring gains one frame per frame until `AppSoundManager` hits
`HARD_RESYNC_MS` and discards down to `DRC_TARGET_MS` - the logged
"dropped 5800-6700 frames (121-140 ms)" every few frames (`unreal-qt/src/emulator/soundmanager.cpp:218-223`).
This is by design for the **host** speed control ("turbo has no realtime constraint;
drop the excess knowingly", `soundmanager.cpp` comment), but hardware turbo is not that:
the frame is still 20 ms of real time, only the CPU executed 2x T-states, and the AY /
beeper / Covox clocks are unchanged (hardware-reference §13).

**Fix - descale hardware turbo out of the audio T-domain (same idea as `Screen::GetCurrentTstate`).**
Keep `current_z80_frequency_multiplier` = host x turbo for the CPU, INT window and screen.
Introduce the two factors explicitly in `EmulatorState` (host multiplier already exists
as `next_z80_frequency_multiplier`; turbo is `scorpion_turbo`), and:

1. `SoundManager::handleFrameEnd`: `frameDuration = config.frame * hostMultiplier` (not
   effective). At hardware turbo this restores 882 samples/frame; the host x2 behaviour
   is untouched.
2. Every T-state handed to audio is divided by the turbo factor first:
   - beeper: the `frameTState` passed to `handlePortOut` / `handleTapeAudio`
     (`soundmanager.cpp:176` and the port-OUT caller) -> `t / turbo`;
   - covox: `covox.cpp:163` (`GetZ80()->t`) -> `t / turbo`, and `frameDuration` as in 1;
   - AY: `soundchip_turbosound.cpp:47-50` -> `currentTStates = t / turbo`, then the
     existing `* hostMultiplier`.
   Add one inline helper, e.g. `EmulatorState::AudioTstate(uint32_t t)` =
   `t / (scorpion_turbo ? 2 : 1)`, and use it at those three sites so the rule lives in
   one place. Integer division loses half a T-state (0.14 us) - far below one sample.
3. Blip clock rates stay at `CPU_CLOCK_RATE`; no reconfiguration on turbo toggles (that
   would glitch the resampler).
4. `tape.cpp:525/692`: unchanged (documented pragmatic choice) - the tape is CPU-timed by
   loaders, so scaling with the effective multiplier is what makes tape loading work in
   turbo.

**Why this also fixes the visible symptom ordering:** with 8.4 the monitor's detection
succeeds, so `#E02D` bit 7/6 are set and the menu can switch turbo off; with 8.5 the
audio is correct in either state. 8.5 is independent of 8.4 and can ship first.

### 8.6 Verification

1. Unit: strobe mid-frame -> `cpu.t` rescaled, `frameLimit` doubled the same frame; a
   scripted `IN #7FFD` followed by a 100 k-T loop with INT enabled completes before the
   INT (and is interrupted without the strobe).
2. Live: boot `PROFSCORP`, MNI -> `#E02D = C0`, menu Turbo item enabled; toggle it ->
   `run_tstates 139776` spans 1 frame (on) / 2 frames (off). Same on `SCORPION`.
3. Live: no "hard resync - dropped … overfilled audio" lines while the monitor runs at
   7 MHz; screen does not blink.
