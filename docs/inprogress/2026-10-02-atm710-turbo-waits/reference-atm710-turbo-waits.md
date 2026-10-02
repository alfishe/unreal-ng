# ATM Turbo 2+ v7.10: RAM wait states at 7 MHz (reference)

**Date:** 2026-10-02 · **Folder:** [README.md](README.md) · **Status:** [TODO.md](TODO.md) ·
**Requirements:** [machine waits](../2026-09-29-machine-waits/requirements.md)

Facts read directly from a source carry no tag. Conclusions drawn from the circuit or from typical
part timings, which no document states outright, are marked **[inferred]**.

## Sources

| Short name | What | Where |
|:--|:--|:--|
| cp7_1 | v7.10 schematic, the main sheet (D68, D69, D73, D76, D79, D98, Z80, clock) | [svn atmturbo `pcad/ver_7_10/cp7_1.pdf`](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/pcad/ver_7_10/) |
| cp7 | v7.10 board placement (part types: D68, D69 = 555TM2; D98 = 555LL1; D76 = 555LN1; D73 = 555LA3) | same folder, `cp7.pdf` |
| errata-green | NedoPC green-board rework sheet (RC on D68 pin 4, CMX to D65.13) | same folder, `errataGreenRus.pdf` / `Visio-Доработки(зеленая).pdf` |
| manual-ru | Assembly manual (Russian) with **timing diagrams 1a, 1b, 2** as embedded pictures | [svn atmturbo `doc/ver_7_10/Сборка и Наладка Турбо2+.doc`](http://svn.nedopc.com/listing.php?repname=atmturbo&path=/doc/ver_7_10/) |
| manual-en | Its English translation, "TURBO 2+ Assembly and Configuration Manual" (text only; the diagrams are the same) | same folder, `TURBO 2+ Assembly and Configuration Manual.doc` |

The timing diagrams are only in the `.doc` files. They are PNG pictures in the OLE `Data` stream:
1a (normal mode), 1b (turbo), and 2 (video fetch). You can carve them out with any OLE reader by searching
for the PNG signature.

---

## 1. Summary

**The rule.** In turbo mode (port `#FF77` bit 3, CPU clock FZ = 7 MHz), every **memory** cycle
(opcode fetch M1, memory read, memory write) that addresses a window mapped to **RAM** gets **2 or 3 wait
states**. The count depends only on where T1 starts relative to the board's RAM slot grid:

- One RAM slot is one R' (RAS) cycle: 4 cycles of the 14 MHz master clock, 286 ns, which is two 7 MHz
  T-states or one 3.5 MHz T-state.
- If T1 starts on a slot boundary (even phase), the cycle gets 2 waits.
- If T1 starts in the middle of a slot (odd phase), the cycle gets 3 waits.

Put another way, the board stretches the cycle so that T3 begins exactly at the end of the slot given to the
CPU. That slot is the first whole slot that begins after T2 ends.

There are no waits in these cases:

- ROM windows.
- I/O cycles.
- The interrupt acknowledge cycle. Its stack pushes and IM 2 vector reads are memory cycles, so they wait
  when they hit RAM.
- The M1 refresh half (T3/T4).
- Internal T-states.
- 3.5 MHz mode. The wait output is gated by TURBO, and at 3.5 MHz the CPU always gets its slot in T2.

**Video mode makes no difference.** The arbiter D68 has no video-mode, border or blanking input. The R'/C'
slot clock runs all the time, because the video block also refreshes the DRAM. So the rule is the same in
the ZX screen, 640x200 hi-res, 80x25 text and 320x200 EGA modes, and on paper, border and blanking lines.

| Access (turbo, 7 MHz) | T1 at even phase (slot boundary) | T1 at odd phase (mid-slot) | Source |
|:--|:--|:--|:--|
| M1 opcode fetch from RAM (4 T) | +2 → 6 T | +3 → 7 T | diagram 1b (both cases drawn) |
| Memory read from RAM (3 T) | +2 → 5 T | +3 → 6 T | diagram 1b (even case drawn); odd case from the same logic **[inferred]** |
| Memory write to RAM (3 T) | +2 → 5 T | +3 → 6 T | RAMCS' does not depend on RD/WR (cp7_1 D98.8) **[inferred]**: no diagram |
| M1 refresh half (T3/T4) | 0 | 0 | D69.1 latches D = RFSH' = 0; RFSH' also sets both halves of D68 |
| Any memory cycle to a ROM window | 0 | 0 | RAMCS' = RAM' OR MREQ' (D98.8): ROM never asserts it |
| I/O read/write, INT acknowledge (IORQ, no MREQ) | 0 (the Z80's own automatic wait only) | 0 | RAMCS' needs MREQ' |
| I/O to the WD1793 (VGCS') | a short wait pulse from the R1·C9 differentiator ("one wait state", manual) | same | separate mechanism, not part of this rule |
| Anything at 3.5 MHz | 0 | 0 | D73 = NAND(TURBO, …) gates WAIT'; diagram 1a |

"Phase" means the 7 MHz clock count, taken from a slot-aligned origin, modulo 2 (section 6).

---

## 2. Hardware trace

### 2.1 Elements and signals (cp7_1, the pin numbers read off the sheet)

| Element | Type | Pins: function |
|:--|:--|:--|
| **D98.8** (555LL1, 2-input OR) | RAM chip select | in 9 = RAM' (window mapped to RAM), in 10 = MRQ' → out 8 = **RAMCS'** (low = memory request to RAM) |
| **D76.10** (555LN1, inverter) | | in 11 = RAMCS' → out 10 = RAMCS (active high; its rising edge = start of the request) |
| **D69.1** (555TM2 = 74LS74, first half) | request / wait latch | /R 1 = **WRES'**; D 2 = **RFSH'**; C 3 = D76.10; /S 4 = +5V; Q 5 → D98.1 (wait); /Q 6 = **VCPU** (low = CPU request pending) |
| **D98.6** (OR) | wait reset | in 4 = R', in 5 = DIS → out 6 = **WRES'**. Low only while R' = 0 **and** DIS = 0, which is the RAS phase of a CPU slot |
| **D68, second half** (pins 8-13) | slot owner for the **address multiplexer** | /R 13 = +5V; D 12 = VCPU; C 11 = **R'** (rising edge); /S 10 = RFSH'; Q 9 = **CMX**; /Q 8 = **DMX** (1 = CPU address on the DRAM) |
| **D68, first half** (pins 1-6) | slot owner for **data / video latch** | /R 1 = +5V; D 2 = CMX (D68.9); C 3 = **C'** (CAS, rising edge); /S 4 = RFSH'; Q 5 = **DIS** (1 = display slot); /Q 6 = **CPU** (1 = CPU slot) |
| **D98.3** (OR) | wait sources | D69.5 OR the VGCS' pulse (D76.12 → C9/R1) |
| **D73.6** (555LA3 NAND) | turbo gate | NAND(TURBO, D98.3): low = wait, only while TURBO = 1 |
| **D79.6** (AND) | /WAIT to the Z80 | WAIT_I AND D73.6 → **WAIT'** → Z80 pin 24 through R2. WAIT_I carries the other wait sources (keyboard controller WAIT_V, IDE WAIT_H) **[inferred: merged upstream]** |
| D65.6, D66.6 | CPU clock | FZ (F2F) = F0 XOR (TURBO AND C): the 3.5 MHz / 7 MHz switch, combinational on the TURBO latch D3.Q2 (port `#FF77` bit 3) **[inferred from the gate picture]** |
| D58 + U1, D5 (555IE10) | clocks | 14 MHz crystal oscillator; D5 divides it by 8. F0 = 7 MHz, F1 = 3.5 MHz, F2 = 1.75 MHz (diagram 2: F1 has one period per slot, F2 toggles every slot) |

Green-board rework (errata-green): cut the trace to **D68 pin 4** (RFSH' → /S of the first half) and feed it
through an RC (470 Ω, 120 pF). The English manual describes a variant that feeds **R' to D68 pin 11**
through 470 Ω / 100 pF ("for stable RAM operation, especially with Soviet CPUs"). Both only delay an edge
by roughly 25-50 ns. They do not change the slot logic, but they do touch the race discussed in 2.4.

### 2.2 How it works

1. **No RAM request** (RAMCS' = 1). D68 stays at CMX = DIS = 1, DMX = CPU = 0, and every slot belongs to
   the display (manual-ru, the paragraph with diagram 1a).
2. **Request.** MREQ' falls in the middle of T1. If the window is RAM, RAMCS' falls with it. The rising edge
   of RAMCS clocks D69.1 with D = RFSH' = 1, so Q = 1 and VCPU = 0. In turbo, D73 then pulls **WAIT' low**
   at once.
3. **Slot grant.** At the next **rising edge of R'**, about 3/4 of the way through the current slot, D68.2
   latches VCPU = 0. That gives CMX = 0 and DMX = 1, and the DRAM address multiplexers switch to the CPU
   address. At the next **rising edge of C'**, the slot boundary, D68.1 latches CMX = 0. That gives
   DIS = 0 and CPU = 1, and the following slot is the **CPU slot**.
4. **Release.** In the CPU slot, R' falls (RAS). Because DIS = 0, WRES' goes low and resets D69.1, so
   VCPU = 1 and WAIT' goes high. The manual says: "After CPU get access (CPU=1, DIS=0) on fall R' trigger
   D69.1 reset by signal WRES', after that signal WAIT'=1, and CPU keep working."
5. **Back to display.** At the next R' rise, D68.2 latches VCPU = 1, so DMX = 0. At the next C' rise,
   DIS = 1. The slot after a CPU slot is therefore always a display slot.
6. **Refresh.** During the M1 refresh, RFSH' = 0 sets both halves of D68 to "display", and D69.1 latches
   D = 0, so there is no request and no wait. The video counters refresh the DRAM ("D68 by RFSH' goes to the
   display fetch because the memory is refreshed by display block 2").

At 3.5 MHz the same arbiter runs (diagram 1a). One T-state is one slot. MREQ' falls at mid-T1, before
R' rises at 3/4 of T1, so the CPU always gets the T2 slot and the data is ready by T3 ("the CPU always reads or
writes without waiting"). WAIT' never reaches the CPU, because D73 is gated by TURBO.

### 2.3 Clock relationships

| Quantity | Value |
|:--|:--|
| Master clock F | 14 MHz (U1 + D58); in practice 14.0 MHz, 71.4 ns |
| RAM slot (one R'/C' cycle) | 4 F = 285.7 ns = 1 T at 3.5 MHz = **2 T at 7 MHz** |
| R' within a slot (diagram 1a) | high at the slot start, falls about 0.2 slot in, **rises at about 0.75 slot** (the D68.2 decision edge) |
| C' within a slot (diagram 1a) | high in the first half, low in the second; **rises at the slot boundary** (the D68.1 edge) |
| B0 (video byte phase) | toggles every 2 slots. One display fetch is needed per B0 half (pixel at B0 = 1, attribute at B0 = 0, diagram 2) |
| FZ falling edges at 7 MHz | at 0.25 and 0.75 of every slot (diagram 1b) |

### 2.4 The two phase cases (diagram 1b, transcribed)

Diagram 1b ("turbo") shows, slot by slot (two 7 MHz T per slot):

| Slot | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| Owner | Display | **CPU M1** | Display | Display | **CPU Read** | Display | Display | **CPU M1** | Display |
| 7 MHz T-states | T1 T2 | Tw Tw | T3 T4 | T1 T2 | Tw Tw | T3 · T1 | T2 Tw | Tw Tw | T3 T4 |
| B0 | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 0 | 1 |

The diagram shows three cycles:

- **M1, T1 at even phase:** T1 T2 Tw Tw T3 T4, so **6 T**.
- **Memory read, T1 at even phase:** T1 T2 Tw Tw T3, so **5 T**.
- **M1, T1 at odd phase:** T1 T2 Tw Tw Tw T3 T4, so **7 T**. Because it starts mid-slot, it misses the next
  slot (slot 7) and gets slot 8.

The trace in 2.2 gives the same counts.

- **Even phase.** MREQ' falls 0.25 slot in, well before R' rises at 0.75. The CPU gets the next slot. WAIT' is
  sampled low at the end of T2. It is released at the R' fall inside the CPU slot (about 0.25 in), which is
  too late for the Z80's set-up time (about 70 ns) at the first Tw sampling edge, so a second Tw follows.
  T3 starts at the end of the CPU slot. Total: 2 waits.
- **Odd phase.** MREQ' falls exactly on the FZ edge where R' rises (0.75 slot). Add the Z80's MREQ' delay
  (up to about 85 ns on a Z80A), D98, D76, the D69 clock-to-Q delay and the D68 set-up time, and the request
  reaches D68 after that R' edge. The CPU gets the slot after next. Total: 3 waits. **[inferred from
  typical 74LS/Z80A timing; diagram 1b draws exactly this outcome]**

**Closed form [inferred]:** with *t* = the 7 MHz clock at T1, counted from a slot boundary, the CPU slot
starts at the first even clock ≥ *t* + 2, and T3 starts at that slot's end. So **waits = 2 + (*t* mod 2)**.

### 2.5 Timing diagram (text, turbo, M1 at even phase followed by a read)

```
14 MHz F      |_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|
slot          |   s0 display  |    s1 CPU     |   s2 display  |   s3 display  |
R' (RAS)      ‾‾\_________/‾‾‾‾\_________/‾‾‾‾\_________/‾‾‾‾\_________/‾‾
                          ^R' rise = D68.2 decision (0.75 slot)
C' (CAS) rise |               ^ slot boundary = D68.1 edge
FZ 7 MHz T    |  T1   |  T2   |  Tw   |  Tw   |  T3   |  T4   |  T1   |  T2   | ...
MREQ'         ‾‾‾\_______________________________/‾‾‾\__ (refresh)  /‾‾‾\______...
RAMCS'        ‾‾‾\_______________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\______...
VCPU (D69.6)  ‾‾‾\____________________/‾‾‾ released by WRES' at R' fall in s1
DMX  (D68.8)  ___________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_________
CPU  (D68.6)  _______________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾\______
WAIT'         ‾‾‾\____________________/‾‾‾‾‾‾‾‾‾‾‾‾  (low while turbo and VCPU = 0)
RFSH'         ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\______________/‾‾‾ (T3/T4: no request)
```

---

## 3. Video modes, border and blanking

- **No mode input to the arbiter.** D68 and D69.1 see only VCPU, R', C', RFSH', RAMCS', WRES' and
  TURBO. The video mode (RG0-RG2 via `#FF77` bits 0-2) changes only the address generation (RDM on the
  D20/D21 multiplexers, the B4-B13 counters) and the pixel path (D40/D41/D42, D46/D96, D52). It does not
  change which slot the CPU gets.
- **Every mode needs one fetch per 2 slots [inferred from the fetch widths].**

  | Mode | Fetches per line | Result |
  |:--|:--|:--|
  | ZX (256 px) | 32 pixel + 32 attribute bytes over 128 slots | one per B0 half |
  | 640x200 hi-res | 80 words of pixel + attribute, read from both RAM "lines" at once | one per 2 slots |
  | EGA 320x200x16 | 80 words, both lines | one per 2 slots |
  | 80x25 text | 80 words of code + attribute; glyphs come from the 573RF2 ROM D94, not RAM | one per 2 slots |

  In turbo, CPU slots are always at least 2 display slots apart: after a CPU slot, T3 (and T4) plus the next
  T1/T2 take at least one more slot before a new request can be granted. So every 2-slot window still holds a
  display slot, and the picture is never starved. The CPU's waits do not depend on the mode.
- **Border and blanking: the same waits [inferred].** R'/C' run continuously because the video counters
  refresh the DRAM, so the arbitration grid exists on every line. The CPU's slot comes from the request
  timing, not from whether the fetched byte will be shown. This differs from the Scorpion Turbo+ and from
  Sinclair contention, where waits depend on the beam position.
- **What the CPU does change in the picture.** In the slot the CPU takes, D42 is not clocked (R' reaches it
  only while CPU = 0, through D67.4). The video latch keeps the earlier display slot's byte for that B0 half.
  This is the same mechanism as at 3.5 MHz, and the manual describes it as harmless.

---

## 4. Other emulators

| Emulator (revision) | ATM710 turbo model | Code |
|:--|:--|:--|
| Unreal Speccy, NedoPC/tslabs line ([zx-evo-unreal @86fd99b](https://github.com/tslabs/zx-evo-unreal/blob/86fd99bb1720b1439ae906b2f6a901f54efa4e8b/Unreal/atm.cpp#L267)) | **none**: a plain clock multiplier | `Unreal/atm.cpp:267-278` `set_turbo()`: `if (comp.pFF77 & 8) turbo(3); else { if (comp.pEFF7 & 16) turbo(1); else turbo(2); }`. This is the ZX-Evo's speed selection, applied to ATM710 too; `Unreal/defs.h:383` `#define turbo(a) cpu.rate = (256 / (a))`; `io.cpp:296-300` calls `set_turbo()` on every `xx77` write. No memory hook adds T for ATM. |
| Unreal Speccy 0.39 line ([alfishe/unreal-speccy @a685b6f](https://github.com/alfishe/unreal-speccy/blob/a685b6f9227ec9dbb595b6cef38addbcd083d6ac/atm.cpp)) | **none**; turbo is a config frame length, not the port bit | `io.cpp:242-244` `xx77` → `set_atm_FF77` (no speed change); `atm.cpp:332` `if (conf.frame < 80000) { // NORMAL SPEED mode` (turbo is detected from the frame length for the "3 zeros" quirk only) |
| ZXMAK2 ([@4964327](https://github.com/zxmak/ZXMAK2/blob/4964327bbf93063cefdf0036e00102c3ab8284d7/src/ZXMAK2.Hardware/Atm/UlaAtm450.cs#L283)) | **none**: a separate "[turbo]" ULA with a doubled frame | `UlaAtm450.cs:283-302` `class UlaAtmTurbo`: `c_frameTactCount *= 2` for every renderer; `MemoryAtm710.cs:388` "TURBO mode" constants for `atm710_z` only. No ATM contention or wait override. |
| Xpeccy ([samstyle @3a31127](https://github.com/samstyle/Xpeccy/blob/3a311278f077f8599aadf2005fec653bf67897a5/src/libxpeccy/hardware/atm2.c#L72)) | **none**: x2 clock | `hardware/atm2.c:72` `compSetTurbo(comp,(val & 0x08) ? 2 : 1);`; `atm2.c:346` the machine uses `stdMRd,stdMWr` (no wait) |
| xpeccy-plus ([dotkoval @84e627d](https://github.com/dotkoval/xpeccy-plus/blob/84e627d3bf182ccf58271623033ca418fd66b3c3/src/libxpeccy/hardware/atm2.c#L71)) | **none** | `hardware/atm2.c:71` `compSetHwTurbo(comp,(val & 0x08) ? 2 : 1);`; `atm2.c:359` `stdMRd,stdMWr` |
| MAME ([@f43983b](https://github.com/mamedev/mame/blob/f43983b62edf2b7d1dc8911ee4a2c08dba8a98de/src/mame/sinclair/atm.cpp#L33)) | **none**: CPU clock doubled | `sinclair/atm.cpp:33` `m_maincpu->set_clock(X1_128_SINCLAIR / 10 * (1 << BIT(m_port_77_data, 3))); // 0 - 3.5MHz, 1 - 7MHz`; `atm.cpp:510` `SPECTRUM_ULA_UNCONTENDED` |
| ZEsarUX, Fuse | no ATM Turbo 2+ machine (not in the local source collection; Fuse has no ATM) | n/a |
| unreal-ng (before this work) | **none**: `hw_turbo_ratio` x2 | [machine-waits requirements](../2026-09-29-machine-waits/requirements.md) §1 |

**Consensus: none of them models the turbo RAM waits.** All of them treat 7 MHz as a plain 2x clock, which
contradicts the schematic, both manuals and diagram 1b. There is no emulator source to cross-check the phase
rule, so the hardware trace (section 2) is the only reference. (The "3 zeros" `atm710_z` quirk shared by
Unreal and ZXMAK2 is about the `#FF` port and is unrelated.)

---

## 5. Expected effect: a sanity check against the manuals

Steady-state cost of common instructions with code (and data) in RAM, using the rule
waits = 2 + (*t* mod 2). The 7 MHz T values are what the loop settles to after its first pass; the start
phase changes at most the first pass, by 1 T. "Speed vs 3.5 MHz" is 2 × base / turbo.

| Instruction (base T) | 7 MHz T with waits | Speed vs 3.5 MHz |
|:--|:--|:--|
| NOP, LD r,r' (4) | 6 | 1.33x |
| INC rr (6) | 8 | 1.50x |
| LD A,(HL) / LD (HL),A / LD r,n (7) | 12 | 1.17x |
| ADD HL,rr (11) | 14 | 1.57x |
| JR e (12) | 16 | 1.50x |
| DJNZ, taken (13) | 18 | 1.44x |
| JP nn, POP, RET (10) | 18 | 1.11x |
| PUSH rr (11) | 20 | 1.10x |
| CALL nn (17) | 30 | 1.13x |
| LDI (16) | 26 | 1.23x |
| LDIR, repeating (21) | 30 | 1.40x |
| OUT (n),A (11) | 16 | 1.38x |
| Any instruction running from ROM, no RAM data | base | 2.00x |

The manuals give these figures:

| Statement | Source |
|:--|:--|
| 140-160% performance (not 200% because of the memory WAIT states) | manual-en |
| 70-80% faster | manual-ru, specification table |
| 2x without memory accesses (internal operations, ROM, I/O); "up to 80%" with RAM fetches; about 1.5x for RAM programs that write to the screen; block instructions (LDIR, CPIR) do best | manual-ru, the paragraph after diagram 1b |

**The rule fits these qualitatively:**

- ROM and I/O run at full speed.
- RAM-resident code gets about 1.1-1.6x.
- Block instructions and internal-heavy instructions do best.

Pure RAM code lands mostly at 1.1-1.5x, below "up to 80%". A typical benchmark of the time (BASIC, or CP/M
BIOS code running from ROM with data in RAM) would land at 1.6-1.9x, which matches "70-80%" and
"140-160%". **[inferred]** No rule with fewer waits would match diagram 1b, which shows 6 T for an M1.

---

## 6. Recommended emulation model

**State to keep: nothing beyond the CPU clock.** The arbiter's state in front of a request is always
"display, idle". The CPU slot depends only on the request time modulo the slot. The previous CPU slot
cannot interfere, because a new request always comes at least one slot after the previous grant ends.

**Phase source.** Use the frame-relative time base the CPU core already keeps. In unreal-ng, `Z80::tt` counts
in 1/256 of a 3.5 MHz T-state, so one slot is 256 `tt` units and one 7 MHz T-state is 128.

- Phase = ((`tt` at T1 of the access) >> 7) & 1, or `AccessStartClock() & 1` while `rate == 128`.
- `tt` advances in multiples of 128 at both clock rates, so this stays right after a mid-frame switch
  between 3.5 and 7 MHz.
- The frame origin (INT) is taken as slot-aligned with offset 0 **[inferred]**: INT comes from the sync
  counters, which are clocked on slot edges (D7/D8/D12/D14). See open question Q1.

**Decision per access**, in a `MemoryWaitOverlay`
(`core/src/emulator/memory/memorywaitoverlay.h`) installed while turbo is on and the `contention`
feature is enabled:

| Access | Extra 7 MHz clocks |
|:--|:--|
| Code (M1) or Read or Write, slot mapped to RAM | `2 + (startClock & 1)` |
| Slot mapped to ROM | 0 (set `SetSlotWaits(slot, false)` on every bank change; ROM/RAM per window comes from the ATM page registers `xFF7`/`#7FFD`) |
| I/O, INT acknowledge, internal cycles, refresh | 0 (no hook; the overlay sees only memory accesses) |

**Points to check in the implementation:**

1. *startClock* must be **T1 of that cycle**, with every earlier T of the instruction already charged,
   including internal T-states before the access (for example the extra T in PUSH's M1, or DJNZ's). If the
   core lumps internal T-states at the end of an instruction, the parity is wrong. Also check what
   `AccessStartClock()` (`tt/rate - 3`) means for an M1 fetch, where the core may have charged 4 T, not 3,
   before the read.
2. The overlay adds the waits after the byte transfer (see the overlay header). That is acceptable here:
   later cycles' phases are right because the waits are already in `tt` when the next access is checked.
3. HALT in RAM: each halted M1 is 6 T at an even phase. Any HALT fast-forward must use 6 T per halted M1
   (7 T for the first one at an odd phase), or it misplaces the INT acceptance by up to a few T.
4. The IM 2 vector read and the PC push at INT acceptance are RAM memory cycles: apply the rule to them.
5. The WD1793 port wait (VGCS' pulse) and the keyboard / IDE waits (WAIT_I) are separate: they belong to
   their port handlers (`Z80::AddWaitStates`), not to this overlay.

**Worked examples** (candidates for `core-tests`, 7 MHz, code in RAM):

| Code | Start phase | Expected 7 MHz T | Why |
|:--|:--|:--|:--|
| `NOP` | even | 6 | 4 + 2 |
| `NOP` | odd | 7 | 4 + 3 |
| `LD A,(HL)` | even | 11 | M1 6 (ends even) + read 5 |
| `LD A,(HL)` | odd | 12 | M1 7 (ends even) + read 5 |
| 4×`NOP` from even | even | 24 | every M1 ends even |
| `NOP` from ROM | any | 4 | no RAMCS' |
| `OUT (#FE),A` | even | 15 | M1 6, operand read 5 (from even), I/O 4 |
| any of the above at 3.5 MHz | — | base | WAIT' gated by TURBO |

## 7. Open questions

| # | Question | Impact | What would settle it |
|:--|:--|:--|:--|
| Q1 | Is the INT edge (the emulator's frame origin) on a slot boundary (offset 0) or mid-slot (offset 1) in 7 MHz clocks? | At most 1 T on the first RAM access after each INT acceptance. Steady-state loops are unaffected | A scope on INT' vs R' (or FZ vs C') on a real board; or a timing test that reads port `#FF` (the latched attribute, D43) at a known clock after HALT in turbo |
| Q2 | The odd-phase race: MREQ' falls on the same 14 MHz edge where R' rises at D68.11. The rule assumes the request always misses (3 waits), as diagram 1b draws it. Could a fast CPU (CMOS Z84C) on a board with the 470 Ω/100 pF R' rework catch the earlier slot (1 wait)? | Odd-phase accesses: 3 vs 1 wait | Scope measurement of WAIT' on a real v7.10, or a cycle-counting test (frame-loop counter) on real hardware at 7 MHz |
| Q3 | Does the WAIT' release reach the Z80 before the first Tw sampling edge on some boards? That would make 1/2 waits instead of 2/3. Diagram 1b says no | Every RAM access −1 T | Same scope or loop-count measurement; diagram 1b is the design intent |
| Q4 | Memory **write** and the odd-phase **read** are not drawn in diagram 1b | Low: RAMCS' does not depend on RD/WR, and the logic is symmetric | Loop-count test with `LD (HL),A` / `PUSH` on hardware |
| Q5 | Pulse width of the WD1793 wait (R1, C9 values are not in the text BOM) | FDC port timing at 7 MHz only | The BOM sheet or a measurement; out of scope for the RAM rule |
