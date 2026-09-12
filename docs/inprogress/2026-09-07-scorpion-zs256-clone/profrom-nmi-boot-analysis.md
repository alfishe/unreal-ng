# ProfROM planes, the boot sequence and the NMI handler — verified findings

Evidence-based analysis of three interlocking subsystems of the Scorpion ProfROM (v4.01
image): the plane (quadrant) lifetime, the magic-button NMI path, and the power-on boot
chain — including the two emulator defects this investigation uncovered (the stuck-low
idle EAR that wedged every boot, and the wrong NMI model inherited from the first
implementation pass). Normative implementation contracts live in
[hardware-reference.md](hardware-reference.md) §9/§5.2; this document carries the
evidence, the disassembly and the reasoning.

Sources and method:

| Source | What it established |
|---|---|
| `data/rom/scorp_prof401.rom` static disassembly (`scratch/profrom-nmi-diag/dump_handlers.py`) | per-plane `#0066`/`#0038` handlers, park loops, signatures, reset stubs, turbo toggle code |
| Scorpion-256-Turbo schematic v16.2.8a (DD50 region), verified against the v4.01 image | the magic button arms **two** DD50 flip-flops; the `#1FFD` latch and GAL DD41 are not involved |
| MAME `sinclair/scorpion.cpp` (`m_nmi_pending`, `beta_disable_r`, banking) | the reference trigger model; where this emulator deliberately differs (§6) |
| UnrealSpeccy `tape.cpp` (`tape_bit()`), `input.cpp`, `io.cpp` | idle EAR level, `#FFBE` reads as ULA mirror, base `0xBF` port semantics |
| Live WebAPI traces: `scratch/profrom-nmi-diag/pass30.py`/`pass31.py` (wedge), `pass32.py` (post-fix boot), `app30.log`/`app32.log` | symptom signatures before/after the EAR fix |

---

## 1. What the button really does — the DD50 model

The magic button ("MNI") is wired to **two flip-flops in DD50** and to nothing else:

| Flip-flop | Label | Effect while armed |
|---|---|---|
| DD50.2 | NMI trigger | asserts `/NMI` — the Z80 accepts it at the next instruction boundary |
| DD50.1 | "1-DOS / 0-SOS" DOS trigger | **page 3 (TR-DOS) of the current plane is forced over `#0000-#3FFF`** — the same mechanism the Beta-128 interface uses for its own magic button |

What it explicitly does **not** touch:

| Component | Touched by the button? | Consequence |
|---|---|---|
| `#1FFD` latch (bit 1 service, bit 0 RAM0, bank bits) | no | the interrupted program's banking state survives verbatim; the monitor restores it on exit |
| `#7FFD` latch | no | ditto |
| ProfROM plane register (GAL DD41) | no | the plane survives the whole NMI session; only the `#0100+4·S` read strobe moves it ([materials/Scorpion_ProfROM_Paging.md](materials/Scorpion_ProfROM_Paging.md) §2-3) |
| `/RESET` line | no | the plane register also survives reset (§5) |

The effective `#0000` priority chain while the trigger is armed:

```
1. p1FFD[1] = 1              → ROM2 Service page           (latch outranks the trigger)
2. DOS trigger armed         → ROM3 TR-DOS of current plane (overrides RAM0 and sessions)
3. ... (the un-armed chain of hardware-reference §4.4 follows)
```

Rule 1 is not cosmetic — it is what makes the firmware entry chain of §2 work. Rule 2
sits **above** `p1FFD[0]` (RAM at `#0000`): the trigger must swap the ROM in regardless
of what the latch selected, exactly like the Beta-128 DOS session does.

The trigger releases on the **first CPU read from `#4000-#FFFF`** (the Beta-128 "leave
the ROM window" strobe; writes never release it). Monitor exit additionally rewrites
`#7FFD`/`#1FFD` from its saved copies and returns through `RETN`.

---

## 2. Plane 0 — the service-monitor entry chain

With the button pressed in plane 0 (the everyday case: BASIC/TR-DOS running), the CPU
lands on `#0066` of the TR-DOS page and the firmware walks itself into the monitor:

```z80
; TR-DOS page (plane 0, page 3)
#0066: JP  #2A56
#2A56: JP  #0807
#0807: PUSH AF / PUSH R...      ; save the interrupted context
       LD   BC,#1FFD
       LD   A,#12               ; 0001 0010: bit 1 (service) + bit 4 (bank8)
       JP   #0033
#0033: OUT  (C),A               ; <<< the trick: service page replaces TR-DOS here

; Service page (plane 0, page 2) — same addresses, continuation of the byte stream
#0035: JP   #00B6               ; fetched already from the service page
#00B6: DI
       OUT  (#1FFD),A           ; A = #12 again — latches it properly
       OUT  (#7FFD),#10
       LD   SP,(#DD6D)/(#DDF7)  ; pick the saved SP slot (monitor RAM, ≥ #4000 —
                                ;   this read releases the DOS trigger)
       ...
       LD   SP,#E38D            ; monitor stack; menu follows
```

**The `#0033` trick.** The `OUT (C),A` sits at the *same address in both pages*, and
both pages carry compatible code around `#0033` — so the page swap "under the feet" of
the executing instruction is safe: the opcode and operand are fetched from TR-DOS, the
next fetch (`#0035`) already comes from the service page. This is why the service latch
must outrank the armed trigger (§1): the chain runs `LD A,#12` while TR-DOS is forced,
and the `OUT` is what swaps the service page in. A trigger that outranked the latch
would map TR-DOS straight back and the chain would loop.

The monitor saves registers and SP in its own RAM area (`#DDxx`), and on exit restores
`#7FFD`/`#1FFD`, releases any remaining trigger state via a command fetch from `≥ #4000`
(Beta-style), and `RETN`s. The plane register never moved: it was 0 on entry and is 0
on exit.

---

## 3. Planes 1-3 — the park loops are a trap, not a defect

Static disassembly of every extension plane shows the same 8-byte program at `#0066`
**in both page 2 (service) and page 3 (TR-DOS)**:

```z80
#0066: LD  A,6          ; border = yellow (6)
       OUT (#FE),A
       XOR A            ; border = black (0)
       OUT (#FE),A
       JR  #0066        ; forever
```

Observed from the outside: a yellow/black striped border, the stripes running diagonally
because the tight loop beats against the raster. There is **no exit** — no `RETN`, no
way back; only `/RESET` (or the running tool's own reset path) recovers the machine.

This is deliberate. While a tool plane (1-3) is active:

- the plane-0 service monitor is unreachable by design — all four pages, including the
  NMI vector, come from the tool's plane;
- a real monitor handler would save its context into the `#DDxx` RAM area, which the
  running ProfROM tool owns — entering the monitor would corrupt the tool's data.

So pressing the magic button inside a tool plane simply parks the CPU where it can do
no damage. The `#0066` vector of pages 0/1 in planes 1-3 contains zeros — but NMI can
never reach them: the hardware always forces page 3 (§1).

> Emulator note: the very first ProfROM NMI test run (session 2026-09-08) saw the CPU
> "frozen" at `#006A-#006B` with IFF1 = 0 and read it as a hang. It was this loop
> running as designed — `#006A-#006B` are loop-body addresses. Plane 0 (Q0) is the only
> plane whose service page has a real handler: `#0066: JP #000D`; its `#0038` is
> `JP #0092` (Q1-3: `JP #0114`).

---

## 4. Plane lifetime — power-on, sessions, reset

| Moment | Plane | Guaranteed by |
|---|---|---|
| Power-on | 0 | hardware (GAL registers clear) |
| Monitor launches a tool | 1-3 | monitor walks the `#0100+4·S` strobe table, jumps to the tool's page 0 |
| Tool running | 1-3 | plane register holds; *all* pages (BASIC/TR-DOS/service/NMI vector) come from the tool's plane — hence the stripes of §3 |
| Tool exits | 0 | the tool's exit stub (§5) — **software**, not hardware |
| `/RESET` in plane 1-3 | stays 1-3! | the GAL keeps its bits across reset; recovery is the §5 stub |

So "the machine is always in plane 0 after reset" is a **software** guarantee, and it
only holds because every extension plane begins with a stub that returns to plane 0
before doing anything else.

---

## 5. The reset stub — how planes 1-3 boot back to plane 0

Every extension plane's page 0 starts with:

```z80
#0000: DI
       JP  #0103
...
#0100: db  #01,#06              ; plane signature (§ identify-by-read)
#0103: LD  DE,#5BEE             ; copy the return stub into RAM
       LD  HL,#0111
       LD  BC,#0011             ; 17 bytes: #0111..#0121
       LDIR
       JP  #5BEE
#0111: LD  BC,#1FFD             ; the stub itself, now in RAM:
       LD  A,2                  ;   map the service page
       OUT (C),A
       LD  HL,#010C             ;   plane 2 uses #0108 — both rows lead to 0
       LD  L,(HL)               ;   <<< the switching read (S=3 / S=2)
       XOR A                    ;   service page out
       OUT (C),A
       JP  #0000                ; cold start of plane 0
```

(plane 1 and 3 read `#010C` — `S = 3` → 1→0 and 3→0; plane 2 reads `#0108` — `S = 2` →
2→0; both rows of the transition table lead to plane 0.) The stub must run from RAM
because the instant the switching read completes, the ROM it was copied from is gone —
the same rule the software guide gives (materials §4 rule 1).

Emulator consequence: a ProfROM machine reset in quadrant 1-3 legitimately boots through
this stub; the quadrant register itself must **not** be cleared by `/RESET` (only by
power-on), or the stub becomes dead code and the tool planes unrecoverable.

---

## 6. MAME's model and the deliberate divergences

MAME (`sinclair/scorpion.cpp`) is the closest reference for the trigger mechanics:

```cpp
// button handler: m_nmi_pending = newval;            (no banking side effect)
// banking:        if (romram() && !m_nmi_pending) -> RAM0 wins
//                 else rom_page = BIT(p1FFD,1) ? SYS : ((nmi_pending||dos())<<1)|rom1();
// beta_disable_r (any read >= #4000):
//                 do_nmi() clears m_nmi_pending AFTER update_io()  (dos stays armed);
//                 a later >= #4000 read clears dos();
// beta_enable_r (#3D00-#3DFF, gated on rom1()): opens the software DOS session
```

| Aspect | MAME | This emulator | Why |
|---|---|---|---|
| Button visible effect | latches `m_nmi_pending` only | arms `scorpionDosTrigger` and maps page 3 at once | both re-bank on the next `update_io`/`UpdateZ80Banks` — same visible state |
| NMI pulse timing | deferred to the next `≥ #4000` read (`beta_disable_r`) | immediate, at the pause boundary (host-side button) | schematic reading: DD50.2 drives `/NMI` directly; deferring is a Beta-128 heritage in MAME that would also swallow the press if the CPU loops forever under `#4000` |
| RAM0 vs trigger | RAM0 suppressed only during the `nmi_pending` window | trigger outranks RAM0 for its whole armed window | the windows are identical in practice — both end at the same `≥ #4000` read — so this is a structural choice, not a behavioral one |
| Service latch priority | `BIT(p1FFD,1) ? SYS : ...` | same | the `#0033` trick (§2) requires it |
| FDC ports while armed | DOS I/O view selected | `CF_DOSPORTS` armed (IN and OUT) | MAME parity |
| Trigger release | `beta_disable_r`, reads only | `MemoryReadFast/MemoryReadDebug`, `addr >= #4000`, reads only | identical strobe; the debugger `DirectRead` path is deliberately not hooked (it must not advance machine state, same policy as the ProfROM read strobe) |

---

## 7. Boot sequence — and the EAR defect that used to wedge it

### 7.1 The wedge (pass30/pass31 evidence)

Before the fix, every cold boot of a `PROFSCORP` instance died the same way:

| Observation | Value | Probe |
|---|---|---|
| Bank 0 contents | ROM page 7 = plane 1's TR-DOS (a wedged session) | `/state/memory` → `rom.active_page = 7` |
| PC | pinned `#1D0A-#1D0E` — an input-poll spin loop | porttrace samples |
| The loop | `#1D04: LD HL,#4FD3 / XOR A / LD B,#FF` → `#1D0A: IN D,(C)` (BC = #FFBE) → `BIT 7,D / RET Z` … timeout → `LD A,#61; SCF; RET` | live disasm |
| Its caller | `#1CE5/#1CF0: IN D,(C) / BIT 6,D / RET NZ` … timeout → error `#61` | live disasm |
| `IN #FFBE` returned | `0xBF` — bit 6 (**EAR**) = 0, i.e. "no tape signal" forever | porttrace values |
| Monitor RAM `#E02D` | `00` — the turbo flag never set (see §8) | memory reads |
| Key ring `#E116` | empty after a 150-frame key hold — keyboard dead everywhere | pass30 |
| Turbo strobes | **0** events in 20000 porttrace samples | pass30 |

`#FFBE` is a `#FE`-family mirror (`A5=1, A1=1, A0=0`), so the loop reads the ULA: bit 6
is the **EAR input**, bit 7 the key-matrix line. The loop waits for EAR = 1; the
timeout returns error `#61`, the caller re-polls, and the machine lives in this pair of
loops — input never reaches the ring buffer, the boot never completes past the wedged
TR-DOS session.

### 7.2 Root cause: the idle EAR level

On real hardware the EAR line **idles high** — nothing drives it low until the first
edge of an actual tape pulse. UnrealSpeccy models exactly this: `tape_bit()` returns
`-1` (all bits set) while idle, so `IN #FE` reads `0xFF`-with-EAR-set until a pulse
arrives.

The emulator's `Tape::handlePortIn` instead generated "analogue noise" with a 16-bit
LFSR gated by `counter == 0` — the EAR bit went high **once per 65536 polls**, an
effectively stuck-low idle level. Every firmware EAR test (the ProfROM/TR-DOS input
polls above, and the classic `LOAD`-style signal waits) read "no signal" forever.

```cpp
// tape.cpp — the fix (was: LFSR noise with EAR high 1/65536 polls)
// No playback: the EAR input idles HIGH ... (UnrealSpeccy models the idle
// level as tape_bit() = -1, all bits set). Firmware EAR tests depend on this:
// the ProfROM monitor's tape-port check reads #FFBE and treats bit 6 = 0 as
// "no signal" (error #61), so a low idle level wedged its input polling
// forever. Deterministic by design - a random idle level would turn firmware
// timing into a lottery
result = 0b0100'0000;
```

This is a machine-independent defect (any model reading the tape port), discovered
through the Scorpion boot wedge.

### 7.3 The healthy boot (pass32 evidence, post-fix)

With the idle EAR high, a cold `PROFSCORP`/1024K boot behaves like hardware:

1. Reset state: plane 0, service monitor at `#0000` (hardware-reference §4.4), black
   border.
2. The monitor walks the quadrant register `0 → 2 → 5 → 7 → 0` (enumerating the tool
   planes via the signature reads — `active_page` sampled `0,2,5,7,0`).
3. Brief FDC poll (no disk mounted — `beta128` family INs observed).
4. Handoff to the default boot-menu selection: **BASIC 128 session**, `IY = #5C3A`,
   editor loop at `#3683/#3685`.
5. Keyboard alive (the input path reaches the ring buffer); `#E02D` still `00` until
   the monitor menu's turbo toggle is actually used (§8).
6. **2217** turbo-family IN strobes over the boot window (was 0 pre-fix) — all
   clear-family (3.5 MHz): the `#E02D` flag gates turbo off until menu usage, so no
   `Applied speed multiplier` line appears in the app log during plain boot.

---

## 8. The turbo firmware chain (why turbo "never engaged")

The hardware strobe itself (hardware-reference §13) is a pure address decode and needs
**no readback** — the initial suspicion "some readback from `#1FFD` failing" proved
unfounded: `IN #1FFD` legitimately reads `#FF` (the register never drives the bus; the
read's *side effect* is the strobe). The firmware chain is:

```z80
; Q0 service page — turbo apply (called at init and session restore)
#04CE: LD   A,(#E02D)     ; monitor config byte; bit 6 = 7 MHz flag
       BIT  6,A
       LD   B,#7F         ; flag set    -> BC = #7FFD  (IN -> 7 MHz)
       JR   NZ,#04DB  /  LD B,#1F   ; flag clear  -> BC = #1FFD  (IN -> 3.5 MHz)
#04DB: LD   C,#FD
       IN   A,(C)         ; the strobe (fires whether or not anything answers)
       IN   A,(C)         ; double strobe — result discarded both times
       RET

; flag writers (init / re-detect)
#026E: CALL #2C1F ... LD A,(#E02D); OR #C0; LD (#E02D),A     ; after NZ detect
#2C3F: ... RES/SET 6,(HL) depending on #2C1F                  ; re-detect
; session restore re-applies the strobe, then restores #7FFD/#1FFD from (#E012)
#053F: CALL #04CE ... LD DE,(#E012) ... OUT (C),A ...
```

Why it looked dead pre-fix: the whole chain hangs off the monitor being **operational**
— the init path must complete its detection (`#2C1F`) and set `#E02D` bit 6, and the
toggle must be reachable from the menu. The EAR wedge (§7) killed the monitor's input
path at line one, so `#E02D` stayed `00` and no strobe beyond the idle clear-family INs
ever mattered. With the EAR fixed, the strobes flow (§7.3 item 6); enabling 7 MHz is
then a menu action away (`BIT 6` selects the `#7FFD` family IN → the multiplier log
line fires — the live proof is part of the pending E2E record).

---

## 9. Emulator implementation contract (as reworked 2026-09-10)

| Piece | Location | Behavior |
|---|---|---|
| Trigger state | `EmulatorState::scorpionDosTrigger` (`platform.h`) | DD50.1; cleared at power-on (`core.cpp`) and by decoder `reset()`; carried in `TTDChipsetState` + `MachineStateSnapshot` (not port-reproducible — same doctrine as `profrom_bank`) |
| Button orchestration | `Emulator::RequestMNI()` (`emulator.cpp`) | pause-guarded: arm trigger → `UpdateZ80Banks()` → Z80 NMI pulse; **no latch writes** (plain `RequestNMI()` remains the no-side-effect pulse) |
| Bank0 priority | `Memory::UpdateScorpionBanks()` (`memory.cpp`) | armed: `p1FFD[1]` ? service : TR-DOS-of-current-plane (via `ResolveScorpionRomBases(profrom_bank)`), overriding RAM0 and session selection; arms `CF_DOSPORTS` only — the software session's unpage machinery stays disarmed |
| Release strobe | `Memory::MemoryReadFast` / `MemoryReadDebug` | first CPU read `≥ #4000` clears the trigger and rebuilds **before the byte is served** (hardware re-mux timing); debugger `DirectRead*` deliberately not hooked |
| FDC visibility | `PortDecoder_Scorpion256` (IN + OUT paths) | Beta128 ports decoded while the trigger is armed (in addition to session/service-latch gating) |
| Idle EAR | `Tape::handlePortIn` (`tape.cpp`) | steady high when not playing (§7.2) — model-independent fix |

Unit coverage: `core/tests/emulator/ports/models/scorpionmni_test.cpp` (rewritten to
this model — trigger arming/release/write-immunity, service-latch priority, RAM0
override, emulator-level `RequestMNI`/`RequestNMI` contrast).

Status at time of writing: code complete; build, full suite and the live magic-button
E2E (press in plane 0 → monitor menu; press in a tool plane → park loop stripes; reset
recovery) are the immediate next gates.

---

## 10. Open items

| # | Item | Note |
|---|---|---|
| 1 | Live E2E of the button on a running instance | plane 0 menu entry, tool-plane stripes, `#DDxx` context save/restore, exit `RETN` path |
| 2 | Turbo-on proof through the monitor menu | post-EAR-fix boot already emits clear-family strobes; the set-family + multiplier log line needs a scripted menu interaction |
| 3 | `.z80`/SNA snapshot taken while armed | the trigger is transient (cleared within the first handler read `≥ #4000`), so snapshots realistically never see it armed — acceptable to leave unsaved; documented here |
| 4 | `#FF`-read Beta-128 bit composition (§12 item 6 of the hardware reference) | untouched by this work; revisit only if software is found that needs it |
