# Profi v5 (Kondor 5.0x) CPU wait states in Spectrum mode, from the schematics

**Date:** 2026-10-02 · **Status:** research; implemented (`ProfiWaitOverlay`)

**Sources:**
- the v5.06 P-CAD netlist `profi506-proc-ascii.sch`, parsed by `parsenet.py` into `netdump.txt` (every pin and net
  below comes from there; the netlist and the dump are kept with the materials outside the repository,
  `materials/v5-wait-netlist/`);
- the v5.06 album pages 2 and 3, rendered with PyMuPDF to check diode directions and the one-shot label;
- the Profi 5.x text schematic `PROF5-04/05/07.TXT`, which has the same arbiter, the same WAIT gate and the
  same /REDYT diode;
- the sync PROM decode (`profisync-output.txt`, D2D4A7C8).

**Scripts**, in [tools/machines/profi/waitmodel/](../../../tools/machines/profi/waitmodel/README.md):

| Script | What it does |
|:--|:--|
| `parsenet.py` | parses the netlist |
| `v5waitsim.py` | a gate-level timing model of the arbiter and the Z80 bus, in 8.9 ns steps |
| `sweep.py`, `sweep2.py` | wait count against clock phase, in paper, border and turbo |
| `lineedge.py` | where the paper window starts and ends |
| `examples.py` | the worked examples (`examples-output.txt`, `examples-novd22-output.txt`) |

## 1. The circuit

Chip types: ЛН1 = 7404, ЛИ1 = 7408, ЛА3 = 7400, ЛЛ1 = 7432, ЛЕ1 = 7402, ТМ2 = 7474, ТМ9 = 74174, ИЕ10 = 74161,
КП11 = 74257. Signal names starting with `/` are active low.

### 1.1 Clocks (Spectrum mode, /80DS = 1)

| Signal | Source |
|:--|:--|
| Master clock | crystal ZQ2, 14 MHz, oscillator DD27. Two outputs: NET00004, and its inverse ZX14MHZ |
| TRAM, the DRAM clock | DD25.7, picks NET00004 because /80DS = 1 |
| F0, the video clock | DD34.4 via V14, also NET00004 |
| TCPU | DD25.4 = ZX14MHZ, the inverse of the master clock |
| CPU clock | DD2:1 (pin 3, clocked by TCPU) toggles NET00033. DD9:1 makes NET00037 = TCPU OR NET00033. DD2:2 (pin 11, clocked by NET00037) toggles F2T. DD22:3/4 turn F2T into F2CPU, which drives Z80 pin 6 |
| CPU speed | F2T = 14 MHz / 4 = 3.5 MHz. In turbo, /TURBO = 0 holds DD2:1 cleared (pin 1), so the CPU runs at 14 MHz / 2 = 7 MHz |

Nothing synchronizes the CPU divider DD2 with the video counter DD7. Both run from the same crystal, so the CPU
clock edge sits at one of **4 master-clock phases** against the video counter. That phase is set at power-on, and
again after every turbo toggle.

### 1.2 The video counter and the fetch request

- **DD7** (74161) counts F0. Its outputs are QA (pin 14) to QD (pin 11).
- **TCS** = /QD (DD5:2). One count of the horizontal counter DD53/DD45 ("tick") is 16 master clocks = 4 T.
- **FLD1** is the paper field signal. It is PROM bit D2 (DD37 pin 13), registered in DD44 (pin 10) on TCS.
- **/STBI0** = F0 OR QA OR QB OR QC (DD4:1, DD4:2, DD15:2). It goes low for half a clock every **8 master clocks**,
  at QA..QC = 0. That is twice per tick, once every 2 T.
- **DD14:2** holds NET00198, the video fetch request, active low:
  - /STBI0 clears it (pin 13), which makes a request.
  - It is set again (pin 11) by the rising edge of NET00203 = CPU OR NET00198 OR /CAS (DD6:1, DD6:2). That edge is
    the end of a video CAS, so each request gets exactly one fetch.
  - Its /S input (pin 10) is NET00196 = /PS AND (FLD1 OR NET00194) (DD23:4, DD4:4).
  - NET00194 is BCMR wired through R5 (470) and VD1. VD1 has its cathode on 80DS, so NET00194 = BCMR AND 80DS,
    which is 0 in Spectrum mode.
  - So in Spectrum mode, **video fetches are requested only while FLD1 = 1**. Outside the paper NET00198 is held at
    1.
- **DD14:1** alternates PIKS on each video fetch, between pixel byte and attribute byte. FLD1 clears it.
  - /STBA = NET00203 OR PIKS latches the attribute byte (DD65).
  - /STBP latches the pixel byte (DD66/DD68).
- **/PS** is a one-shot (C11 100 pF, R19 2 kΩ, DD22:6) on DD44 Q6 = PROM bit 7, the line count pulse. It also
  presets NET00198. I did not check where it falls in the line. It only matters if it fell inside FLD1.

### 1.3 The DRAM slot ring (one slot = 4 master clocks = 1 T at 3.5 MHz)

- **DD49:1** holds /RAS. CLK = TRAM, D = NET00075.
- **DD62** (74174, clocked by TRAM) copies it on: /CAS (Q5) takes /RAS, and NET00075 (Q6) takes RAS.
- The loop therefore runs with a period of 4 master clocks: RAS is low for 2 clocks, and CAS follows one clock
  later.
- The /S input of DD49:1 is NET00063 = NAND(/RAS AND /REDYT, CPU) (DD13:2, DD26:1). When the slot belongs to the
  CPU and the CPU is not asking for RAM, the loop is **held idle**. It restarts on the next TRAM edge once a
  request arrives, so the ring is not locked to the video counter.
- **DD21:2** holds CPU, the slot owner:
  - CLK = /CAS rising edge, D = /CPU. The owner changes after every CAS.
  - /S = NET00066 = NOT NET00198 (DD22:1). While video has no request, the CPU owns every slot.
  - /R = NAND(NET00062, NET00066) (DD26:2). NET00062 = CPU AND NET00009 (DD11:1).
  - NET00009 = NOT S_IR22 (DD5:4, through R69 300 Ω), wire-ANDed with /REDYT by VD22 (cathode on /REDYT).
  - So when video has a request, it takes the slot at once if the CPU is idle and no CPU CAS is under way. A CPU
    request that is waiting blocks this, so the CPU wins ties (that is VD22's job).
- **S_IR22** = NOR(/CPU, /CAS) = CPU AND CAS (DD20:2). It is the CPU's CAS strobe: it latches read data in
  DD67/DD69 and clocks DD21:1.

### 1.4 /REDYT and the WAIT path

- **G14** = NOR(/RAMS, /MREQ) (DD20:1).
  - /RAMS = /NOROM AND NOT(A15 OR A14) (DD10:2, DD8:4). It is high for the ROM area while ROM is paged in.
  - So G14 means "a CPU memory request that goes to DRAM": opcode fetch, memory read and memory write in any RAM
    page, including RAM at #0000 when /NOROM pages it in.
- **NET00041** = G14 AND /RFSH (DD13:1). Refresh cycles do not count.
- **DD21:1**: /R = NET00041, D = VCC, CLK = S_IR22. Its /Q output is NET00045.
- **/REDYT** = NAND(NET00041, NET00045) (DD26:3). It goes low as soon as a CPU DRAM cycle starts, and high at the
  CPU's own CAS edge (S_IR22 rising).
- **/READY** (Z80 pin 24, through R12 100 Ω) = DD103:4 OR(NET00027, NET00034).
  - NET00027 = /REDYT AND NET00122 AND /KBW (DD18:3, DD18:4).
  - /KBW comes from the keyboard connector X9.18. It is idle (high).
  - NET00034 comes from SB8:
    - PROFI3+ position (1-2): ground.
    - PENTAGON position (2-3): NET00067 = /TURBO AND /80DS (DD71:4).
  - So PENTAGON turns off every wait from this circuit, but only in non-turbo Spectrum mode.
- **NET00122 is a one-shot on ROM reads, not on IORQ.** This corrects the earlier reading.
  - NET00114 = DD10:3 = MRD AND /RAMS. MRD is DD5:5, the inverse of /RD OR /MREQ (DD19:4).
  - NET00114 drives C7 (82 pF), with R11 (2 kΩ) to ground, into DD5:3. The album labels it "TI = 200nS".
  - It pulls /READY low for about 160-200 ns at the start of every **ROM** read: opcode fetch or data read in
    #0000-#3FFF with ROM paged in.
  - It has no IORQ input. The 5.x text schematic draws the same gate (/RAMS & MRD into C7 82 pF).

**Which accesses wait:**

| Access | Video WAIT (/REDYT) | ROM one-shot |
|:--|:--|:--|
| RAM opcode fetch, read, write | **yes** | no |
| ROM read (opcode fetch, data) | no | **yes** |
| ROM write | no | no |
| I/O | no | no |
| Refresh | no | no |

## 2. The rule (Spectrum mode, 3.5 MHz, SB8 = PROFI3+, VD22 fitted as on 5.06)

Results of the simulation. Each case was run for 4 sub-phases, random instruction mixes, and Z80 delays of 18-80 ns
with WAIT setup of 0-70 ns:

1. **The border: no waits.** Video never asks for a slot. The CPU owns every slot, and its CAS comes about 2 master
   clocks after /MREQ, well before T2 falls.
2. **The paper: at most 1 wait per DRAM access.** No case gave 2.
   - Video asks for a slot every 8 master clocks (2 T). Within each 2-T pair, at most one CPU start position loses
     to video.
   - A RAM M-cycle gets 1 wait if its T1 starts at master clock x, with x mod 8 in **{6, 7, 0}**. Here x = 0 is the
     clock at which /STBI0 makes a video request (QA..QC = 0).
   - This holds for Z80 delays of 18-45 ns. A slower Z80 (80 ns) widens the set to {5, 6, 7, 0}. A fast Z80 with
     no setup time narrows it to {6, 7}.
   - The CPU T starts every 4 clocks at phase r = x mod 4. That gives four kinds of board:

     | r | Which T waits |
     |:--|:--|
     | 0 | the T starting at the request edge |
     | 2 | the T starting 2 clocks before it |
     | 3 | the T starting 1 clock before it |
     | 1 | **none**, so the board runs as if there were no contention |

   - Which r a board has is decided at power-on (and at a turbo toggle). It is not traced to any reset.
3. **The paper window.**
   - Requests come at x = 0, 8, ..., 504 from the start of the first FLD1 tick. That is 64 per line: 32 pixel bytes
     and 32 attribute bytes. They span the 128 T in which FLD1 = 1.
   - `lineedge.py` gives the waited T1 positions from x = -2 (or -1, or 0, depending on r) up to x = 504.
   - With the D2D4A7C8 PROM, the first FLD1 tick of paper line L starts at **14368 + 224·L T** after the INT tick
     edge, for L = 0..191.

**Emulator rule (MemoryWaitOverlay).** P0 is the paper start as the emulator counts it from INT: 14368, or 14367
(see the uncertainty section). For a DRAM access (M1 fetch, read, write; not ROM, I/O or refresh) whose T1 starts at
frame T c:

```
d = c - P0;  L = floor((d + 1) / 224);  q = d - 224*L          # q in -1..222
if 0 <= L < 192:
    phase 0 (default):  wait = (0  <= q <= 126 and q even) ? 1 : 0
    phase 2 or 3:       wait = (-1 <= q <= 125 and q odd)  ? 1 : 0
    phase 1:            wait = 0
else: wait = 0
```

In plain terms:
- **Default, r = 0:** wait = 1 when c - (14368 + 224·L) ∈ {0, 2, 4, ..., 126}. That is the even T of the window,
  64 positions per line.
- **r = 2 or 3:** wait = 1 when c - (14368 + 224·L) ∈ {-1, 1, 3, ..., 125}. That is the odd T, shifted one T
  earlier.
- **r = 1:** no waits.
- Otherwise 0.

As a pattern this is **(1, 0)** repeated over the 128-T fetch window of each paper line. The Sinclair 48K uses
(6, 5, 4, 3, 2, 1, 0, 0). The worst case is 1 T, not 6 T, and a stream of 4-T instructions settles into a
phase that never waits. A `[PROFI] WaitPhase=` value (0..3, default 0) should select the case.

**ROM one-shot.** At 3.5 MHz the pulse ends roughly 350-400 ns after T1 starts, and the Z80 samples WAIT when T2
falls at 429 ns. That puts the end of the pulse inside the Z80A's 70 ns setup window, so the result is **marginal:
0 or 1 wait per ROM read**. The forum's "формирует wait даже не в турбе" (it makes a wait even outside turbo)
points to 1 on at least some boards. Suggested default: 0, with a setting for 1. **Not settled.**

### Worked examples

`examples-output.txt`: steady-state T per instruction for a loop of one instruction, code and data in RAM. The
r = 0, 2 and 3 columns are identical, so they are merged.

| Instruction (base T) | Paper, r = 0/2/3 | Paper, r = 1 | Border |
|:--|:--|:--|:--|
| NOP (4) | 4 (settles after at most 1 wait) | 4 | 4 |
| LD A,(HL) (7) | 8 | 7 | 7 |
| LD (HL),A (7) | 8 | 7 | 7 |
| INC (HL) (11) | 12 | 11 | 11 |
| PUSH BC (11) | 14 | 11 | 11 |
| LD A,(nn) (13) | 16 | 13 | 13 |
| LDIR, one repeat (21) | 22 | 21 | 21 |
| OUT (n),A (11) | 12 | 11 | 11 |
| LD A,(HL), code in ROM, data in RAM (7) | 8 | 7 | 7 |
| NOP in ROM (4) | 4 | 4 | 4 |

The ROM rows leave out the ROM one-shot.

In short, each DRAM M-cycle that starts on the waiting parity is stretched to the next even boundary. After that,
an M-cycle with an odd length moves the following access onto the other parity.

## 3. Turbo and 80DS

- **Turbo in Spectrum mode** runs at 7 MHz from the same 14 MHz crystal. TCPU = ZX14MHZ whenever /80DS = 1.
  - The slot stays 4 master clocks long, which is now 2 CPU T. The model gives, in 7 MHz T:
    - border: 1 wait per DRAM access;
    - paper: 1-3 waits, depending on phase.
  - The ROM one-shot gives a clean 1 wait per ROM read.
  - SB8 has no effect in turbo.
  - These figures are less certain than the 3.5 MHz rule, because they depend more on the delay values.
- **The third crystal** (ZQ3, DD31, XMHZ) drives TCPU and TRAM **only when 80DS = 1** (the DD25 select is /80DS).
  So "up to 15 MHz" applies to the 80DS/CP/M mode on this netlist.
- **In 80DS:**
  - F0 = 12 MHz, while TRAM and the CPU run on XMHZ. The two clock domains are asynchronous.
  - Video requests run outside FLD1 too when BCMR = 1 (NET00194).
  - SB8 never turns waits off (NET00067 = 0).
  - **Not simulated.**

## 4. Uncertainty

**Traced with certainty**, every gate from the netlist and checked against the album drawings:
- the slot ring and the arbiter;
- the /REDYT conditions: RAM only, no ROM, I/O or refresh, all RAM pages;
- the /READY gate;
- SB8;
- the one-shot on ROM reads (not IORQ);
- the clock dividers;
- video fetches only in FLD1 in Spectrum mode, 2 per 4-T tick.

**From the model (inferred):**
- the {6, 7, 0} phase set. It shifts by about one clock with Z80 delay and setup, because the real sampling
  margin is a few tens of ns;
- the 4-way power-on phase lottery, including r = 1 having no waits;
- the window edge, ±1 fetch;
- every turbo figure.

**Not traced:**
- where frame T = 0 falls against the INT tick edge. INT comes from an unregistered PROM bit (DD37 O3 into DD29),
  and the 27512 access time of about 150-250 ns delays INT by about 0.5-1 T. So the 14368 can be 14367 in emulator
  T;
- where /PS falls in the line;
- the factory 5.03 "diode mod" that Vadim says gave fewer waits once removed. On 5.06 the diode on /REDYT is VD22,
  and removing VD22 in the model gives **more** waits (`examples-novd22-output.txt`). So the 5.03 mod is probably a
  different change. Unknown;
- the 80DS mode;
- why the Profi was designed this way. Gromov's "идеальная синхронизация" (perfect synchronization) shows only that
  his test programs ran.

**What would confirm it:**
- **TEST 4.30** (timing pages) or a Tact Meter run with code in RAM at #8000. A loop of `LD A,(HL)` that spans the
  paper should come out 8 T instead of 7 per instruction in the paper. Phase r = 1 shows no change. Several
  power-ons tell the phases apart.
- **floatspy.** Ratibor's 14147 against 14347 is 200 T. That is far more than these waits can explain, so it is
  more likely a PROM or INT difference.
- **Gromov's QARX, ACADEMY and SHOCK MEGADEMO** as a regression set: border and multicolor effects should stay
  stable with the overlay on.
- **A scope** on Z80 pin 24 against /STBI0 would settle the phase set and the ROM one-shot directly.
