# Profi v3.2: the turbo waits and the floating bus, from the schematic

**Date:** 2026-10-02 · **Status:** research; implemented (`ProfiWaitOverlay`, `PortDecoder_Profi::FloatingBusV3`)

Evidence step E3 for this folder ([requirements.md](requirements.md) Q3, Q4; [design.md](design.md) 4.4, 6.2).

Sources:
- MDESK re-trace of the v3.2 boards (`Profi3.zip`: `profi32cl-mdesk-sch.pdf` main board, sheets "List 1..9";
  `profi32il-mdesk-sch.pdf` interface/controller board, "Interface list 1..6"). The re-trace differs from the
  original only in the points its readme lists (U56 jumpered, X8 added on the LD bus, ~M1 in the AY select,
  FON/FON1 merged). None of them touch the circuits below.
- The v3.2 manual (`ProfiV32mn`, OCR p2, p5).
- The ROM set in the zip: `Profi_RT4_line.BIN` (interface board port decoder U5, 556RT4).

Crops (kept with the materials outside the repository, `materials/v3-turbo-crops/`, rendered with
[crop.py](../../../tools/machines/profi/turbomodel/crop.py) from the MDESK PDFs): `cl0-left.png` (U16, U25:A, S1), `cl0-tl.png`/`cl0-mid.png`/
`cl0-e.png`/`cl0-g.png` (oscillators, U4, clock gates), `cl0-h.png`/`cl0-i.png` (PROM U3, U21, FLD, INT),
`cl4-a.png`/`cl4-b.png`/`cl4-c.png` (the arbiter U27:A/U28:A, READYT, CASCPU), `cl4-d.png` (OEPIK1/2),
`cl5-a.png` (U9, U19, U57), `cl6-a.png` (RAMS/ROMCS), `il3-a.png` (VG93 HLD -> TURBO).
Scripts, in [tools/machines/profi/turbomodel/](../../../tools/machines/profi/turbomodel/README.md): `turbo-model.py`
(wait rule, loop table, Tact Meter figures), `turbo-alt-rules.py` (the same against other wait rules),
`decode-rt4-ports.py` (U5 dump).

Notation: c = state of the master counter U4 (74LS163, 555IE10) clocked at 14 MHz, Q0..Q3 = c bits 0..3.
One horizontal tick (DA0 step) = 16 masters = 4 T at 3.5 MHz. A turbo T = 2 masters. A DRAM cycle = 4 masters.

## A. Turbo

### A1. Clocks (traced, List 1)

| Net | Gate | Function |
|:--|:--|:--|
| master | U15 (KP11) 1Y: 14 MHz (Z1) or 12 MHz (Z2) by 80DS | clocks U4 |
| F2T | U30:D (LN1) = /Q0 | 7 MHz; rises at every even c |
| F2 | U31:A (LA3, inputs tied) = /Q1 | 3.5 MHz; rises at c = 0 mod 4 |
| /RAS | U37:D xor U37:C = not(Q0 xor Q1) via RRAS 27R | low at c = 1, 2 mod 4: **one DRAM cycle per 3.5 MHz T** |
| CLCAY | U39:C = Q2 | AY clock 1.75 MHz |
| STBI | U40:C = Q0+Q1+Q2+Q3 | active low at c = 0 of each tick |

### A2. The switch (traced, List 1 + Interface list 4)

- U16 (555KP11 = 74LS157), /OE = GND. A/B = U25:A /Q.
  - A (normal): 1A = F2 -> 1Y -> F2CPU (Z80 CLK, R22 680R pull-up); 2A = +5V -> 2Y -> R4 100R -> /READY (Z80 /WAIT).
  - B (turbo): 1B = F2T, 2B = /READYT.
- U25:A (TM2): D = node of R5 (3.3k to +5V) and S1 contact 3; S1 common (2) = /TURBO from the system bus B1;
  CLK = /CASCPU. So turbo = (S1 pressed) AND (/TURBO low), taken over **at the rising edge of /CASCPU**, i.e. at
  the end of the next CPU RAM cycle (the mux changes clocks at a slot edge, glitch-free). Code that runs only
  from ROM never changes speed until it touches RAM.
- /TURBO is made on the controller board: VG93 (U1) **HLD, pin 28, is wired to READY (pin 32) and to the net
  TURBO**, which also feeds U12:D (LN2) -> /MOTOR. HLD high = head loaded = motor on -> /TURBO high -> D = 1 ->
  normal speed. Without the controller /TURBO floats, R5 holds D high: no turbo (manual p5: "при снятом
  контроллере она влияния не оказывает").
- Nothing else drives /TURBO or U25:A (no port bit, no INT term). **The VG93 HLD is the only thing that drops
  turbo** (xpeccy-plus's claim is confirmed). HLD rises with any type II/III command or type I with h=1 and drops
  after 15 index pulses without a command (WD1793 behavior; without a disk there are no index pulses).

### A3. The DRAM arbiter and READYT (traced, List 5)

```
G14     = U44:B NOR(/RAMS, /MREQ)          CPU memory cycle to RAM
REQ     = U45:A AND(G14, /REF)            ...but not refresh (/REF = Z80 /RFSH)
D       = U45:B AND(REQ, /Q28)            request, not already granted
U28:A   D=D, CLK=F2,  Q = NOCPU, /Q = YCPU     grant: the next DRAM cycle is the CPU's
U27:A   D=D, CLK=F2T, Q = Q27                  holds the wait one turbo T after the grant
/READYT = U44:C NOR(D, Q27)
/CASCPU = U38:D OR(F2, /Q28)              CPU CAS: c = 2,3 of the granted cycle
/S_IR22 = U44:D NOR(F2, /Q28)             CPU read latch U19 (74LS373) open in c = 2,3 of the grant
/WE     = U31:C NAND(NOCPU, /RD)          write in the granted cycle when not reading
/RAMS   = U47:B AND(U44:A NOR(A14,A15), /NOROM)   high = ROM area with ROM on (List 7)
ROMCS   = U33:D NAND(/RAMS, /BLOK) via R34
```

Video side (List 5): X = U38:B OR(U38:C OR(F2, NOCPU), Q26A) clocks U26:B (pixel/attribute toggle, reset by
STBI); /STBP = U41:B OR(X, Q26B), /STBA = U41:A OR(X, /Q26B); U26:A (/S = FLD, CLK = /STBA, /R = STBI) stops the
fetches after the attribute. So the video takes **the first two DRAM cycles of each tick that the CPU did not
take**, and U28:A has no video term at all: **the CPU is never held for the video**. A CPU cycle can never get two
grants in a row (D includes /Q28), so the video always gets 2 of the 4 cycles of a tick.

Rules that follow:
- **Only RAM memory cycles wait**: opcode fetch, operand/data read, data write, including RAM paged at #0000
  (/NOROM). ROM (/RAMS high), I/O (no MREQ), interrupt acknowledge (IORQ, no MREQ) and refresh (/REF) never wait.
- **No paper/border difference**: waits are identical in paper, border and blank (the video never blocks U28).
- **Normal speed**: U16 feeds +5V to /WAIT: no waits (the M6 "transparent memory" item is confirmed).

### A4. The wait count per phase (traced logic + inferred timing)

Z80: MREQ falls after the falling CLK edge in T1; /WAIT is sampled on the falling edge of T2 and of each Tw.
Phase A = T1 starts on an F2 rising edge (c = 0 mod 4; even turbo T counted from INT); phase B = T1 starts
between (c = 2 mod 4; odd turbo T). m0 = master clock at the start of T1.

| | Phase A (m0 = 0 mod 4) | Phase B (m0 = 2 mod 4) |
|:--|:--|:--|
| /MREQ falls | m0+1 (+ ~50-85 ns) | m0+1 (+ ~50-85 ns) |
| F2 edges seen by U28 | m0+4: grant | m0+2 is 71 ns after MREQ's clock edge: missed (needs MREQ delay + 3 gates + setup, ~110 ns); grant at m0+6 |
| Q27 | 1 at m0+4, 0 at m0+6 | 1 at m0+4 and m0+6, 0 at m0+8 |
| Tw | 2 (end m0+8) | 3 (end m0+10) |
| T3 starts | m0+8 (an F2 edge) | m0+10 (an F2 edge) |

**Rule: a RAM memory cycle gets 2 waits if its T1 starts on an even turbo T, 3 if odd.** T3 always starts
on the slot edge right after the CPU's DRAM cycle. Status: the gate logic is traced; the phase-B miss is inferred
from the propagation budget (71 ns from the T1 falling edge to the F2 edge). The manual's turbo fix (p5:
"емкость 200-400 пф на землю в одно из мест на ноги 4(U44), 3(U45) или 6(U45)") puts the capacitor exactly on
G14 / REQ / D, which slows the request's rising edge and so makes the miss certain: it confirms that this path is
the timing-critical one. A one-wait phase B would be the "fast" board without the capacitor; the Tact Meter
figure below rules it out on the measured board.

For the overlay (`MemoryWaitOverlay::ExtraClocks`): RAM slots marked, `extra = (startClock & 1) ? 3 : 2`, where
`startClock` is the turbo clock of T1 from INT. Inferred parity: INT falls at a tick edge (c = 0), so even turbo T
from INT = F2 edge. Only valid with turbo on and HLD low.

### A5. Worked table (`turbo-model.py`)

| Code (in RAM) | 3.5 MHz T | turbo T, steady state | speed vs 3.5 MHz |
|:--|--:|--:|--:|
| NOP stream | 4 | 6 (converges to phase A: 4+2) | 1.33x |
| LD A,(HL) stream | 7 | 12 (M1 from phase B 4+3, read from phase A 3+2) | 1.17x |
| LD (HL),A stream | 7 | 12 | 1.17x |
| OUT (n),A / IN A,(n) | 11 | 16 (I/O cycle no wait) | 1.375x |
| INC DE | 6 | 8 | 1.5x |
| JR e / DJNZ | 12 / 13 | 16 / 18 | 1.5x / 1.44x |
| JP nn | 10 | 18 | 1.11x |
| INC DE : JP loop | 16 | 26 | 1.231x |
| any code in ROM | n | n | 2.0x |

### A6. Tact Meter and the manual

| | 69888-T frame (PROM 0A1DFAFD) | 71680-T frame (PROM FB0579B6 family) | measured (xpeccy-plus, v3.2 board) |
|:--|--:|--:|--:|
| code in ROM | 139776 | 143360 | 143206 (-154: the interrupt handler and stack in RAM do wait) |
| code in RAM, INC DE : JP-type loop | 86016 | 88223 | 88208 (-15) |
| code in RAM, NOP / INC DE : JR | 93184 / 104832 | 95573 / 107520 | - |

`turbo-alt-rules.py`: with the 88208 ratio (1.2306) only the 2/3 rule fits (2/1 -> 104262, 2/2 -> 95573,
3/3 -> 81920 for the same loop). The loop body of Tact Meter 1.0 is not known; the INC DE : JP fit is a
consistency check, not a proof. Unreal_NS `PROFI_TURBO` 116920 = 1.673x of 69888 and the manual's "в 1.7 раза"
mean a mix of ROM and RAM code (ROM 2x, RAM 1.1-1.5x), not a single rule.

### A7. INT in turbo (traced circuit, inferred consequence)

/INT = U42:A OR(PROM D3 node (C14 3300 pF), U27:B /Q); U27:B D = that node, CLK = DA3. Clocked by the sync
counter only: turbo does not move INT or change its real-time length; in CPU T both double (frame 139776 turbo T;
the 36 T INT of 0A1DFAFD = 72 turbo T). A HALT in RAM repeats 6-T M1s (INT latency jitter 6 turbo T), in ROM 4.
In 80DS (12 MHz master) the same circuit gives 6 MHz turbo / 3 MHz normal with the same rule.

Not traced: video fetch artifacts in turbo when the CPU takes cycles 1 and 3 of a tick (the attribute strobe then
lands at c = 16, on STBI); the CPU's U19 hold timing for a read in phase B.

## B. Floating bus

### B1. Circuit (traced, List 6 + List 5)

- U9 (555IR23 = 74LS374): D = DRAM outputs M0-M7, CLK = **STBP** (the pixel fetch), /OE = **/OEPIK1**,
  Q -> P0-P7 (the shifter bus, U14 74LS166 parallel load).
- **U57 (8 x 820R) joins P0-P7 to the CPU data bus DC0-DC7.** DC has 10k pull-ups (X16).
- U8 (attribute, CLK = STBA, /OE = GND) drives AT0-AT7 only (to U12/U20), never DC: **the attribute byte cannot
  be read**. U10 (CLK = STBA, /OE = /OEPIK2) drives P only in 80DS mode.
- /OEPIK1 = U34:D NAND(Y, FLD1), /OEPIK2 = U34:A NAND(/Y, FLD1), Y = U35:B NAND(/SI4, 80DS) (List 5).
  Spectrum raster (80DS = 0): U9 drives P whenever FLD1 = 1.
- FLD = U46:C AND(PROM U3 D2, /DA5); FLD1 = U21 (74LS175) Q2, D2 = FLD, CLK = STBI (List 1). U26:A /S = FLD:
  no fetches outside FLD.
- Any read cycle in which no other source drives DC returns P through the 820R: the latched pixel byte in the
  paper window, otherwise #FF from the pull-ups.

### B2. What drives DC instead (traced / dump-decoded)

- RAM reads: U19 (373). ROM: on the controller board, via its 74LS245 U31 (/OE from U8/U21:E = any of F1, F2,
  F4, F5 (port PROM U5), FON (AY), BLOK). Port #FE (A0 = 0): keyboard latch U13 (all 8 bits).
- So **every IN with A0 = 1 that the controller does not claim floats**, port #FF included.
- U5 (556RT4, /CS = /IORQ) address = ADR5, ADR6, X, ADR15, ADR7, ADR1, ADR0, CP/M; X is a line from the DOS
  latch area (U30, DISK/BAS). Decoded dump (`decode-rt4-ports.py`): with CP/M = 0 ports are claimed only with
  X = 1 (FDC #1F/#3F/#5F/#7F on F1, and one more pattern on F2); with CP/M = 1 another map (FDC at the same low
  bits on F1 or F5, #xxE3-pattern on F2). So **the DOS latch and CP/M mode change which ports float, not what a
  floating read returns**; outside DOS in Spectrum mode no #xxFF read is claimed. Not traced: which level of X
  means "DOS on" and the exact port set (the F2 pattern decodes as A7=1, A6=0, A5=1 with the pin order read from
  the drawing, which does not give #FF in DOS; the dump's line order or the X net needs a check on a board).

### B3. Which byte at which T (inferred from the traced pipeline)

Pipeline: in tick k the video fetches the pixel byte for tick k+1 into U9 (STBP) and the attribute into U8; at
c = 1 of tick k+1 U14 loads P (load enable V_IR10 = NAND(not STBI, FLD1) uses the FLD1 latched one tick earlier)
and shows it. So U9 is enabled (FLD1 = 1) for **one tick before the first displayed byte until one tick before the
end of the paper line**: the read window leads the displayed paper by 4 T. The analog delays (PROM outputs carry
1500 pF caps BC0/BC2) decide whether FLD1 equals FLD of the same or of the previous tick; the "leads by one tick"
relation holds either way because the display needs it.

Let t_L = frame T (from INT) at which pixel byte 0 of paper line L starts to show
(t_L = t_P + 224 L; t_P = 12580 for 0A1DFAFD per the Unreal preset, 12584 per the PROM decode at the PROM level).
The Z80 latches IN data on the falling edge of T3, i.e. at c = 2 of a 3.5 MHz T. Let s = frame T of T3,
k = floor((s - t_L) / 4), p = (s - t_L) mod 4:

| s - t_L | U9 | IN returns (3.5 MHz) |
|:--|:--|:--|
| -4 .. -1, p = 0 | on | stale: byte 31 of the previous paper line (line 0: byte 31 of line 191 of the previous frame) |
| -4 .. -1, p = 1..3 | on | pixel byte 0 |
| 0 .. 123, p = 0 | on | pixel byte k (the one now showing) |
| 0 .. 123, p = 1..3 | on | pixel byte k+1 |
| >= 124 (byte 31 showing), border, blank, sync, border lines | off | #FF |

STBP is at c = 4 when the CPU does not take the tick's first DRAM cycle (always true at 3.5 MHz during an I/O
cycle). In turbo the CPU samples at c = 2q+1 for turbo T q of the tick, and STBP moves to c = 8 if the CPU took
cycle 1 (an operand fetch just before): byte k for c < 4 (or < 8), k+1 after. Pixel bytes come from the screen
being displayed (bank 5/7 per #7FFD bit 3). 80DS (512x240): U9 and U10 alternate on SI4, the read returns the
hi-res bytes; not worked out further.

## C. Certainty

| Item | Status |
|:--|:--|
| F2 = 3.5 MHz = /Q1, F2T = 7 MHz = /Q0, one DRAM cycle per 3.5 MHz T | traced |
| Turbo = S1 AND /TURBO, latched by U25:A at /CASCPU | traced |
| /TURBO = VG93 HLD (also READY, /MOTOR); nothing else drops turbo; no controller = no turbo | traced |
| Wait only on RAM memory cycles (M1, read, write); ROM, I/O, INTA, refresh free | traced |
| Video never holds the CPU; no paper/border difference; no waits at 3.5 MHz | traced |
| 2 waits in phase A | traced (logic) + inferred (Z80 edges) |
| 3 waits in phase B (request misses U28) | inferred (71 ns budget; manual's 200-400 pF fix on that path; matches 88208) |
| Phase parity = turbo T from INT | inferred |
| Tact Meter 143206 / 88208 | consistent (ROM 2x minus RAM-side overhead; RAM 16/13 with an INC DE:JP-type loop) |
| INT unaffected by turbo in real time | traced |
| Floating bus = U9 pixel latch via U57 820R; attribute never | traced |
| Window leads the display by one tick; byte k / k+1 by phase | inferred (pipeline) |
| #FF outside FLD1 (border/blank) | traced (U9 /OE, X16 pull-ups) |
| DOS/CP/M only change which ports are claimed | traced (U5 inputs); exact port map not traced |
