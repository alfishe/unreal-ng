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

**The Scorpion magic-button NMI is missing the "PC must be in RAM" acceptance gate that
both reference emulators implement. Without it the NMI fires while the CPU is executing
ROM, and on a ProfROM machine that ROM is often an extension plane whose `#0066` handler
is a dead border-flash loop — the hang.**

- The magic button (`Emulator::RequestMNI`) pulses /NMI, vectoring the Z80 to `#0066` of
  whatever ROM plane is mapped at that instant.
- Plane 0 holds the real service monitor. Extension planes 1–3 hold ROM-disk tools; per
  the ProfROM contract (`materials/Scorpion_ProfROM_Paging.md` §4 rule 6) their `#0066`
  is only a "wrong plane" indicator — a border flash:

  ```
  #0066:  3E 06   LD A,#06
          D3 FE   OUT (#FE),A
          AF      XOR A
          D3 FE   OUT (#FE),A
          18 F7   JR #0066        ; infinite DI loop — the hang
  ```

- During the ~1.5–3.5 s (emulated) boot, the firmware runs a cross-plane ROM-disk scan
  and executes from planes 1–3 (`materials`/live trace: PC in `#0300`-page with active
  ROM page 6/7 = plane 1). If the button is taken then, the NMI lands on that plane's
  border-flash stub. The loop is `DI`, so the frame-interrupt keyboard-scan escape
  (`#0038 → #0114` in plane 1) never runs — hence "no keyboard, interrupts disabled".

**Both reference emulators prevent this by refusing the magic NMI while PC is in ROM:**

- **ZXMAK2** (`MemoryScorpion256.cs` `BusNmiRq`):
  `e.Cancel = (m_cpu.regs.PC & 0xC000) == 0;` — cancel whenever `PC < #4000`.
- **UnrealSpeccy** (`emulkeys.cpp` `main_nmidos` + `z80_main.inl` `z80loop`):
  `if (Scorpion && !(flags & CF_TRDOS) && pc < 0x4000) { nmi_pending = frame*50; return; }`
  then fire `m_nmi(RM_DOS)` only once `pc >= 0x4000` (drop after the countdown).

**Our build removed this guard on a mistaken rationale.** `implementation-plan.md:229`:
*"keep `pc > 0x4000` Scorpion guard **out** (MNI latch handles ROM selection; the
original guard applied to a different NMI source)."* The guard is **not** about ROM
selection — it prevents the NMI from being taken while the CPU executes ROM, so the
monitor is only entered from user RAM where plane 0 is the stable ROM. Removing it is the
defect.

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

**Open discrepancy (2026-09-10):** the user reports a *100 %* hang in the GUI when
pressing MNI at the idle 128 menu, with the 4.01 service menu displayed. Over the WebAPI
this exact case is reproducibly **working**: 12/12 idle presses enter the monitor, and
Down/Up move the highlight, Enter opens a submenu (962 screen bytes change, code enters
plane 1), Space/1 react — with default, 256 KB and 1024 KB RAM, with and without port
trace. GUI and WebAPI keys converge on the same `Keyboard::PressKey` matrix path and the
GUI ignores OS autorepeat, so the divergence is not yet located; it needs a live GUI
reproduction inspected over the WebAPI (see §7 step 5).

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
| **Magic-NMI PC gate** | cancel if `PC<#4000` | defer until `pc>=#4000`, drop after 50 frames | **absent (bug)** |
| ProfROM plane strobe | `RdMemM1`+`RdMem`, mask `0xFFF0`, gate `SYSEN` | any read (dbg), 4 addr | `RdMem`+M1, mask `0xFFF0`, gate = service ROM mapped — matches ZXMAK2/GAL |
| DOS-session leave | `SubscribeRdMemM1` (M1 only) | `if (m1 && flgDOS && RAM)` | any read `>=#4000` (no M1 gate) |

The GAL (`materials/Scorpion256TPlus_GAL_decoded.md` §3) clocks the plane on every ROM
read (M1 included) with A0/A1 unbonded, so our `0xFFF0`/M1-included strobe is the
hardware-faithful one; the transition table matches all references.

---

## 5. Status of earlier hypotheses

| Claim | Status | Note |
|---|---|---|
| Missing `pc>=#4000` magic-NMI gate → NMI into extension-plane `#0066` | **VERIFIED (§1–2)** | The root cause. 8/8 mid-boot, 0/12 idle. |
| Keyboard needs long holds (`#E051` debounce) | **VERIFIED (§3)** | Real, compounding, not the hang. |
| DOS-trigger release should require M1 (`isExecution`) | **PLAUSIBLE, not the cause** | Code does lack the M1 gate and references gate on M1; a legitimate fidelity fix, but the `#0814` crash it predicted was never observed. |
| ProfROM strobe should be data-only + mask `0xFFF3` | **REFUTED** | Contradicts the GAL and ZXMAK2; our `0xFFF0`/M1 form is correct. `#0101` read = selector 0 = hold, no plane change. |
| Plane "reverts to page 2" corrupting the menu | **REFUTED** | The hang is stuck in plane 1 / page 6, the opposite. |
| Quoted "current code" (`quadrant=(addr>>2)&3; …`) | **STALE** | Not the working tree; live path uses `ScorpionRomWindow::OnRomRead` + the verified table. |

---

## 6. Fix plan (concrete)

Restore the reference-emulator gate: **the Scorpion magic NMI is accepted only when the
CPU is executing from RAM (`PC >= #4000`); while PC is in ROM it is held pending.** This
guarantees the monitor is entered from user code, where plane 0 is the stable ROM, and
makes the extension-plane `#0066` unreachable by the button.

Because our `RequestMNI` arms the paging (`scorpionDosTrigger`) at *request* time, the
whole action must be deferred together — otherwise the forced page would sit over
still-running ROM code. Consolidate the paging into the accept, matching UnrealSpeccy's
`m_nmi`.

### 6.1 Primary change — defer the magic NMI to a RAM boundary

**a. Distinguish the magic NMI from a plain NMI.**
Add a flag set by the magic path only, e.g. `Z80::_nmi_is_magic` (or a small enum on the
pending count). `RequestNMI` (plain) leaves it clear; `RequestMNI` sets it.

**b. `Emulator::RequestMNI()` (`core/src/emulator/emulator.cpp:765`) — stop pre-paging.**
Remove the immediate `scorpionDosTrigger = 1; UpdateZ80Banks();`. For Scorpion models
just request a *magic* NMI; do the paging at accept time (6.1c). Keep the
pause/​request/​resume guard. Non-Scorpion models keep plain-NMI semantics.

**c. `Z80::ProcessInterrupts()` (`core/src/emulator/cpu/z80.cpp:707`) — gate + page at accept.**
Replace the unconditional accept with:

```cpp
if (_nmi_pending_count > 0)
{
    const bool magic = _nmi_is_magic;
    const bool scorpion = (model == MM_SCORP || model == MM_PROFSCORP);

    // Reference gate (ZXMAK2 BusNmiRq / UnrealSpeccy main_nmidos):
    // the Scorpion magic button is taken only while the CPU runs from RAM.
    // While PC is in ROM (boot scan / menu / monitor) hold it pending so the
    // NMI can never vector into an extension plane's #0066 border-flash stub.
    if (magic && scorpion && cpu.pc < 0x4000 && !(state.flags & CF_TRDOS))
    {
        if (--_nmi_magic_countdown == 0)   // e.g. seeded to 50*frame steps
            _nmi_pending_count = 0;        // drop, like UnrealSpeccy
        // else: leave pending, re-check next boundary
    }
    else
    {
        _nmi_pending_count = 0;
        if (magic && scorpion)             // page the monitor now, not at request
        {
            state.scorpionDosTrigger = 1;
            _memory->UpdateZ80Banks();
        }
        // ... existing accept: HALT skip, 11T, push PC, pc=0x66, iff handling ...
        _nmi_is_magic = false;
        return true;
    }
}
```

`_nmi_magic_countdown` is seeded when the magic NMI is requested (e.g. `50 * frames per
frame` steps, per UnrealSpeccy). Dropping after the countdown matches hardware; omitting
the countdown (defer indefinitely until RAM) matches ZXMAK2 and is also acceptable.

**d. Checkpoint/TTD.** The magic-pending flag + countdown are CPU state that affects
execution; add them to `TTDCpuState`/`machine_state_hash` alongside `nmi_in_progress`
(`core/src/debugger/ttd/…`) so reverse-debug and divergence hashing stay coherent.

### 6.2 Idle-menu convenience (decision point)

With the pure gate, pressing the button while sitting in the 128 menu (PC in ROM, plane
0) also defers until RAM runs — matching the references but changing today's behaviour,
where an idle-menu press works immediately because plane 0 happens to be mapped. If
keeping the idle-menu convenience is wanted, widen the accept to also fire when the
current plane is 0 (safe ROM):

```cpp
if (magic && scorpion && cpu.pc < 0x4000
    && _memory->CurrentProfRomPlane() != 0            // plane 0 = real monitor, safe
    && !(state.flags & CF_TRDOS)) { /* defer */ }
```

This is a superset of the reference behaviour: it still blocks every extension-plane
press (the hang) but keeps immediate entry when plane 0 is mapped. Recommended if the
team wants F11 to work at the boot menu; otherwise use the plain reference gate (6.1c).

### 6.3 Independent fidelity fix (optional, not the cause)

M1-gate the magic DOS-trigger release to match Xpeccy/ZXMAK2, in `MemoryReadFast` and
`MemoryReadDebug` (`core/src/emulator/memory/memory.cpp`):

```cpp
if (_scorpionDosTriggerActive && isExecution && addr >= 0x4000) [[unlikely]] { … }
```

Do **not** change the ProfROM plane-strobe mask or add `!isExecution` to it (§4/§5).

---

## 7. Verification protocol

1. `ninja -C cmake-build-release` — zero warnings.
2. `./cmake-build-release/bin/core-tests --gtest_filter="*Scorpion*:*Nmi*:*Mni*"` green.
   Add a unit test: on `MM_PROFSCORP`, set `PC < 0x4000`, `RequestMNI()`, step — assert
   the NMI is **not** taken and PC does not become `#0066`; then set `PC >= 0x4000`, step
   — assert the NMI is taken, `scorpionDosTrigger` armed, PC = `#0066`, `#C000` banking
   preserved.
3. Live regression (the pre-fix failure): 8×, boot `PROFSCORP`, fire `…/nmi {"magic":true}`
   ~2 s into boot (PC in `#03xx`, plane 1). Pre-fix: 8/8 border-flash at `#0066`.
   Post-fix: 0/8 — the NMI defers, then enters the real monitor once RAM/plane-0 executes.
4. Live sanity: at the settled 128 menu, MNI enters the monitor (`#0101 = 02`), a
   `frames=25` key hold advances the `#E116` ring and dispatches, and the documented exit
   returns to BASIC with `#C000` intact.
5. GUI reproduction of the open §2 discrepancy: in unreal-qt, boot `PROFSCORP`, wait for
   the 128 menu, press F11, then hold a cursor key ≥1 s. Leave the instance running
   (not paused). Over the WebAPI on that instance record: registers (PC, IFF1), ROM
   `active_page`, `#0101`, whether PC sits in `#0066`–`#006D` (border-flash) or the
   `#2800`/`#0700` menu loop, and a port trace of `IN #xxFE` values while the key is held
   (does the GUI key reach the matrix: row byte ≠ `0xFF`?). Also note whether the border
   flashes yellow/black.
