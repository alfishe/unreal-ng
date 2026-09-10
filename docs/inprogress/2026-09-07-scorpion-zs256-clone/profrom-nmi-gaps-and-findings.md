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
