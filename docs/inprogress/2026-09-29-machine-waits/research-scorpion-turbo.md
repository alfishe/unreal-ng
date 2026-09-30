# Scorpion ZS-256 Turbo+: CPU wait states in 7 MHz turbo mode

Research note, 2026-09-29. Read-only research for the unreal-ng core; nothing in the repository was changed.
Supporting files (schematic PDF, both original JED files, crops, a simulator) are in
[tools/scorpion/](tools/scorpion/) (the decoder, the simulator, the two JED files; the schematic PDF is on zx-pk.ru thread 940, post #1).

## 0. Summary

- The Turbo+ board's turbo logic is one 20-pin EPLD, DD30 (Intel 85C220 / Altera EP220). **Two original
  firmwares exist for the same green SC15 board**: `SC15_1` ("TURBO PLATA VER2.0", 01.01.96) and `SC15_3`
  ("TURBO 15.3", MAX+plus II 02/26/96). Both JED files are attached to zx-pk.ru thread 940 post #42. They
  give **different wait rules**. I decoded both fuse maps myself (§1.4). My decoding matches deathsoft's
  ABEL output term for term.
- **DD38 (К555ИР22 = 74LS373) is a read latch between DRAM and CPU, not a posted-write latch.** The schematic
  shows D inputs = MD0-7 (DRAM out), Q = D0-7 (CPU bus), /OE = RAM-, C = WRBFR-. DRAM inputs are wired straight
  to the CPU bus. **Writes wait exactly like reads.** Section 6.5 of `contention-by-machine.md` is wrong on
  this point.
- **Turbo contention covers ALL RAM**, every page, including RAM paged at #0000. ROM never waits. The signals
  come from the RAM select (`RAM-` = MREQ & RD & not ROM; `WR_EN` = MREQ & !RD & !RFSH & not ROM), not from
  the address. So it is nothing like the Sinclair #4000-#7FFF rule.
- **SC15.1 turbo rule** (per memory access, in 7 MHz T-states):
  - A data read or write to RAM waits until the next "CPU slot edge", which gives 0-3 T in the paper and 0-1
    T in the border.
  - An opcode fetch (M1) from RAM always waits at least 1 T and then aligns: 1-4 T in the paper, 1-2 T in
    the border.
  - Every I/O cycle gets +2 T.
  - A NOP stream from RAM runs at 8 turbo T per NOP in the paper, which is 3.5 MHz speed, and at 6 T per NOP
    in the border.
- **SC15.3 turbo rule**: the same slot rule for reads, writes and M1 (no extra M1 wait), and +1 T per I/O.
  In normal mode it has **no Even M1 at all**. Its M1 timing looks too tight for 1990s DRAM (§7, Q2).
- **Both firmwares switch the CPU back to 3.5 MHz while /INT is active** (the TRB register needs INT1 = 1).
  So the interrupt acknowledge and the first ~32 T of every ISR always run at normal speed. Nobody models
  this.
- **The paper window as seen by the wait logic** (`BRD-` = BK- AND BC-) starts **exactly 64 lines = 14336 T
  after the /INT edge**, at the start of a line. It lasts 128 T (3.5 MHz) per line for 192 lines. The line
  layout, counting from the window start, is: 0-127 paper fetch, 128-159 right border, 160-191 HSYNC,
  192-223 left border.
- MAME, Xpeccy and unreal-ng model turbo as a plain 2x clock with no waits. ZXMAK2 has no Scorpion turbo at
  all.

Glossary (terms used below):

- **count c**: the value of the 7 MHz pixel counter DD3. It changes on every falling edge of the 7 MHz clock.
- **edge**: a rising edge of the 7 MHz clock. The EPLD registers load on this edge, and in turbo it also
  starts a CPU T-state.
- **p**: `c mod 4` as sampled at an edge.
- **CPU slot**: the DRAM cycle the CPU owns.
- **H1M**: "H1 for memory". When it is 1, the video owns the DRAM address bus.

## 1. The DD30 equation sets

### 1.1 Sources

- deathsoft, zx-pk.ru thread 940: post #40 (SC15_1 ABEL, page 4:
  https://zx-pk.ru/threads/940-scorpion-zs-256-turbo-(skhema)/page4.html), post #41 (SC15_3 ABEL) and
  post #42 (both .jed files in `sc15jed.rar`, attachment 4669), both on page 5:
  https://zx-pk.ru/threads/940-scorpion-zs-256-turbo-(skhema)/page5.html. Post #42 says: "Оба исходника
  прошивки получены путем декомпиляции jed файла. Прошивки расчитаны на 'зеленую' плату версии SC15" ("both
  sources are decompilations of the jed files; the firmwares are for the green SC15 board"). #45: there is also
  an SC15.4 for an EPM7032 that cannot be decompiled.
- The schematic "Scorpion ZS 256 Turbo+ (1996)" redrawn by deathsoft (V. Vorobyov), zx-pk.ru thread 940, post #1,
  attachment 4243. On it the net
  names at DD30 are: pin 1 CLK, 2 IORQ, 3 WR_EN, 4 RAM-, 5 INT1, 6 TRB, 7 BRD-, 8 M1-, 9 H0, 11 H1; outputs
  19 WAIT- (via diode, R23 3k pull-up), 18 CLK (to the Z80, R33 1k5), 17 WE- (R36 560), 16 n/c, 15 RAS-,
  14 WRBFR-, 13 n/c, 12 H1M. The part is EP220PC-12.
- `TURBOSC.DOC` (page 6, attachment 4696, linked from post #54): Scorpion's own instructions for upgrading a yellow board to
  Turbo+ with an EPM7032 (SC15.4). Saved as text.
- `Scorpion256TPlus/GAL/turbo.jed`: a GAL22V10 port of **SC15.3** (header "TURBO 15.3", ispLEVER, 2007). The
  pin names are deathsoft's ABEL names. It is not an independent design. The repository's `doc/turbo.md`
  decodes it with a wrong column map and its equations are garbage. `doc/files/Scorpion_Turbo_Mode.md` calls
  WRBFR- a "posted-write latch" and H1 a 3.5 MHz signal. Both claims are wrong (H1 = 1.75 MHz, it is the AY
  clock).

### 1.2 SC15.1: equations (post #40, confirmed by my own fuse decode)

`X.Q` is the value of register X from the previous edge. All registers clock on the rising edge of CLK_7MHZ.
Polarity is as in the ABEL (WE is `Neg`, the rest positive).

```
H1M       = H1 & !TRB.Q  #  BORDER_ & H1 & TRB.Q                     (combinational)
WR_BUFF   = H0 & !H1M                                                (combinational)
CLK_CPU   = CLK_7MHZ & TRB.Q  #  RAS_.Q & !TRB.Q                     (combinational)
RAS_.D    = H0
WE.D      = !(WR_EN & !H1M & RAS_.Q)          WE.OE = !H1M
Pin13.D   = !IORQ_ & TRB.Q & !WAIT_.Q         (buried register, pin 13 not connected)
TRB.D     = !WR_EN & RAM_ & INT & !TRB_IN & !H1 & !RAS_.Q
          # TRB.Q & (WR_EN # H1 # RAS_.Q # !RAM_)
WAIT_.D   = IORQ_ & M1_ & !H0 & !H1M                  (a) data read/write released in CPU slot
          # IORQ_ & !WR_EN & RAM_                     (b) no RAM access, no I/O -> released
          # IORQ_ & !M1_ & H0 & !H1M & !WAIT_.Q       (c) M1: released only after >=1 wait, on H0=1
          # Pin13.Q                                   (d) I/O release, 2 edges after IORQ
          # H0 & !H1M & !TRB.Q                        \
          # !H0 & H1M & !TRB.Q                         } normal mode: Even M1
          # M1_ & !TRB.Q                               }
          # RAM_ & !TRB.Q                             /
```

### 1.3 SC15.3: equations (post #41, confirmed by my own fuse decode)

```
H1M       = same as SC15.1
WR_BUFF   = !BORDER_ & H0 & TRB.Q  #  H0 & !H1          (= H0 & !H1M in effect)
CLK_CPU, RAS_, TRB = same as SC15.1
WE.D      = !(WR_EN & RAS_.Q & (!H1 # !BORDER_ & TRB.Q))   (= !(WR_EN & !H1M & RAS_.Q)), OE = 1
Pin13.D   = !(!IORQ_ & Pin13.Q)                          (1 when idle, toggles while IORQ is active)
WAIT_     = !WR_EN & RAM_ & Pin13.Q                       (combinational!)
          # !BORDER_ & Pin13.Q & !RAS_.Q
          # !H1 & Pin13.Q & !RAS_.Q
          # !TRB.Q
          = Pin13.Q & ( (!WR_EN & RAM_) # (!RAS_.Q & !H1M) )   in turbo;   1 in normal mode
```

SC15.3 has no M1 term and never waits in normal mode.

### 1.4 Independent verification of the decompilation

I decoded both .JED files (`QF2916`, 81 rows of 36 fuses) myself. The layout is 8 macrocells × 9 rows, with
8 product terms plus an OE row last in each block, MC1 = pin 19 … MC8 = pin 12. There are 18 input pairs
(complement, true). Pair n maps to pin 1, 2, 3, 4, 5, 6, 7, 8, 9, 11 and then the feedbacks of pins 12-19. A
0 fuse means connected. With this map, every product term of both files reproduces deathsoft's ABEL
exactly, including the WAIT_ OE = 1 and the WE OE = !H1M of SC15.1. The script is
[tools/scorpion/ep220-jed-decode.py](tools/scorpion/ep220-jed-decode.py). The equations above are the fuse contents, not a guess. Confidence: high.

The JED pin comments in SC15_1.JED confirm the roles: `IORQ_ @2, WR_EN @3, RAM_ @4, INT @5, I9 @6 (TRB_IN),
BORDER_ @7, M1_ @8, H0 @9, H1 @11, Q1 @19 (WAIT), CLK_CPU @18, WE @17, TRB @16, RAS_ @15, WR_BUFF @14, H1M @12,
Q7 @MC7 (buried)`.

### 1.5 Signal meanings (from the schematic)

| EPLD name | Schematic net | Meaning | Source |
|:--|:--|:--|:--|
| CLK_7MHZ (pin 1) | CLK | 7 MHz, DD2-1 Q (a ТМ2 dividing the 14 MHz crystal ZQ1) | crop-clock-pld.png |
| H0, H1 | H0, H1 | DD3 (555ИЕ7 = 74LS193) Q0, Q1. DD3 counts on **CLK- (DD2-1 /Q), so it changes on the falling edge of CLK**. H0 = 3.5 MHz, H1 = 1.75 MHz (H1 is also the AY clock, AY pin 15) | crop-clock-pld.png, TURBOSC.DOC "H1: 15_AY8912" |
| IORQ_ | IORQ | Z80 /IORQ | |
| M1_ | M1- | Z80 /M1 | |
| RAM_ | RAM- | 0 = memory **read** from RAM: `RAM- = NAND( NOR(RD-, MREQ-), NOT ROM )`, where `ROM = NOR(A15, A14', RB)` (RB = #1FFD bit 0, RAM at #0000). Covers M1 and data reads. Not refresh (no RD) | DD10-3, DD69-2, DD67-2 |
| WR_EN | WR_EN | 1 = memory **write** cycle to RAM, detected from MREQ alone (before WR): `WR_EN = NOR3(ROM, MREQ-, NAND(RD-, RFSH-))` = MREQ & !RD & !RFSH & !ROM | DD67-3, DD12-4 |
| INT | INT1 | the /INT flip-flop DD2-2 Q before R81. 0 = interrupt active | |
| TRB_IN | TRB | /Q of turbo flip-flop DD9-2: **0 = turbo requested** | §2.3 |
| BORDER_ | BRD- | `BRD- = BK- AND BC-` (DD13-2). **1 = inside the paper window, 0 = border/blanking** | §5 |
| WAIT_ | WAIT- | to Z80 /WAIT through a diode (open-collector style) | |
| CLK_CPU | CLK | the Z80 clock | |
| RAS_ | RAS- | DRAM /RAS (DD21-DD28 565РУ7). Also drives the row/column mux select via DD11-4 | crop-dd38-dram.png |
| CAS (not in EPLD) | CAS- | `CAS- = H0 XOR 1 = !H0` (DD11-3) | |
| WE | WE- | DRAM /WE, early write, tri-stated in video slots (SC15.1) | |
| WR_BUFF | WRBFR- | latch enable of **DD38 (ИР22 = '373)**, the MD→D read latch. **1 = transparent**. /OE = RAM- | crop-dd38-dram.png |
| H1M | H1M | 1 = video owns the DRAM (the CPU address mux DD15/DD16 555КП11 is disabled, /E = H1M) | |

TURBOSC.DOC confirms that DD38 is a read latch. The upgrade replaces "DD38 было 1533ИР23 … надо 1533ИР22"
(an edge-triggered '374 becomes a transparent '373), with "RAM → 1_DD38" (/OE) and "WR_BUFF → 11_DD38" (LE).

## 2. Timing model

### 2.1 The two clock edges

- The pixel counter DD3 advances on the **falling** edge of CLK_7MHZ. The count c is stable from one falling
  edge to the next.
- The EPLD registers load on the **rising** edge, in the middle of a count, so they see a clean c. Call the
  rising edge that samples count c "edge E(c)".
- In turbo, `CLK_CPU = CLK_7MHZ`, so every edge E starts a CPU T-state.
- In normal mode, `CLK_CPU = RAS_.Q`: the CPU clock rises at E(c) for odd c and falls at E(c) for even c.
  One T-state is 2 counts.
- The Z80 samples /WAIT on the **falling** edge of T2 and of every Tw. The WAIT_ it sees is the value
  registered at the rising edge that started that T-state. In normal mode the register also updates at the
  CPU's falling edge, but the change lands after the CPU samples (EPLD tco plus the tpd of the combinational
  CLK_CPU). Either reading gives the same normal-mode result (§4).

### 2.2 DRAM slot structure (derived from RAS_/CAS/H1M)

- `/RAS` is low from E(c) to E(c+1) for even c, so it falls in the middle of an even count.
- `/CAS = !H0` is low for the whole of each odd count.
- Each pair of counts (0,1), (2,3), (4,5), (6,7) is therefore one DRAM cycle: row at the even count, column
  at the odd one.
- The owner of a pair is H1M: `H1 = 0` for counts 0,1 and `H1 = 1` for counts 2,3 (mod 4).
- The CPU's read data passes through DD38. The latch is transparent during the CPU pair's column count
  (`WR_BUFF = H0 & !H1M`) and closes at that count's end, holding the byte until the next CPU column.

### 2.3 Turbo switching (DD9-2 and the TRB register)

- **Setting the request.** The request flip-flop DD9-2 (К555ТМ2) is set by `rd_7ffd` and reset by `rd_1ffd`.
  Both come from decoder DD52 (555ИД7: A0=WR-, A1=A14, A2=A15, enabled by CSFD, the xxFD decode). The front
  button toggles it through DD55-2, which samples on KC-. Its /Q is the `TRB` net, which is TRB_IN (pin 6),
  and Q lights the LED. So an IN from the #7FFD decode turns turbo on and an IN from #1FFD turns it off. This
  matches MAME and Xpeccy. Confidence: high (schematic).
- **Applying the request.** The TRB register copies `!TRB_IN` only at a "safe" edge: `!H1 & !RAS_.Q` (that
  is E(c ≡ 1 mod 4)) and no RAM access in progress (`!WR_EN & RAM_`).
- **/INT forces normal speed.** The TRB register can only be (re)set while `INT = 1`, that is while /INT is
  inactive. While /INT is low, turbo drops at the first safe edge and comes back at the first safe edge after
  /INT ends. This is my reading of the equations; no document states it. The likely reason is that the
  Spectrum's /INT is a fixed pulse (~32 T at 3.5 MHz). At 7 MHz it would be 64 CPU T-states, long enough for
  a short `EI: RET` ISR to be interrupted twice. So **the interrupt acknowledge and the start of the ISR always
  run at 3.5 MHz, with the normal-mode rules** (SC15.1: Even M1). Confidence: high for the logic, medium for
  the stated reason.

## 3. Timeline of one 8-count period (one character = 4 T at 3.5 MHz)

Counts c = 0..7 relative to a paper-window start (the window starts at c ≡ 0 mod 16, §5).

```
count c           |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
H0 / H1 / H2      |0 0 0|1 0 0|0 1 0|1 1 0|0 0 1|1 0 1|0 1 1|1 1 1|
/RAS (RAS_.Q)     |  ‾|__|__|‾‾|‾‾|__|__|‾‾|‾‾|__|__|‾‾|‾‾|__|__|‾‾   low from mid-even to mid-odd
/CAS (=!H0)       |‾‾‾‾‾|_____|‾‾‾‾‾|_____|‾‾‾‾‾|_____|‾‾‾‾‾|_____|
normal mode   H1M |  0  |  0  |  1  |  1  |  0  |  0  |  1  |  1  |
  owner           |   CPU     |   video   |   CPU     |   video   |  (video also in the border)
  CPU clock       |   ↓ mid 0 | ↑ mid 1   ↓ mid 2 | ↑ mid 3 ...     T-state = 2 counts, starts mid-odd
turbo, paper  H1M |  0  |  0  |  1  |  1  |  0  |  0  |  1  |  1  |
  owner           |   CPU     |   video   |   CPU     |   video   |
  WR_BUFF (latch) |  -  | open|  -  |  -  |  -  | open|  -  |  -  |
turbo, border H1M |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0  |
  owner           |   CPU     |   CPU     |   CPU     |   CPU     |
  WR_BUFF         |  -  | open|  -  | open|  -  | open|  -  | open|
turbo CPU clock   | ↑ at the middle of every count (edge E(c)); 1 T-state = 1 count
```

- Normal mode: the CPU owns counts 0,1 and 4,5 of every character, border included (H1M = H1). Video does
  two fetches per character, in counts 2,3 and 6,7. I did not trace which of those is bitmap and which is
  attribute; it does not matter for waits.
- Turbo in the paper: the same 2 CPU slots per 8 counts, but the CPU now clocks once per count. That is 2
  slots per 8 CPU T-states: one slot every 4 T.
- Turbo in the border: all 4 pairs go to the CPU, one slot every 2 T. This is the "twice the slots" of
  §6.5, which is correct.

## 4. The per-access rule for an emulator (turbo active)

**Definitions.**

- Number the turbo T-states by their starting edge e.
- `p(e)` = DD3 count at that edge, mod 4.
- `paper(e)` = BORDER_ at that edge (§5).
- An edge is a **CPU-slot edge** when `H0 = 0 and H1M = 0`, that is when `p(e)` is even and
  (`!paper(e)` or `p(e) == 0`).
- For a machine cycle, let `e2` = the edge that starts T2 (= T1 edge + 1).

### 4.1 SC15.1 (the ABEL from post #40)

| Access | Condition | Wait states w (turbo T) | Length |
|:--|:--|:--|:--|
| Data read from RAM (MR) | `RAM_ = 0`, M1 inactive | `w = min{ j ≥ 0 : slot(e2 + j) }` → paper: `(4 − p(e2)) mod 4` (0-3); border: `p(e2) & 1` (0-1) | 3 + w |
| Data write to RAM (MW) | `WR_EN = 1` | same as a read (**no write buffer**) | 3 + w |
| Opcode fetch from RAM (M1) | `RAM_ = 0`, M1 active | `w = min{ j ≥ 1 : p(e2+j) odd and (!paper(e2+j) or p(e2+j) == 1) }` → paper: `1 + ((−p(e2)) mod 4)` (1-4); border: `1 + (p(e2) & 1)` (1-2). Equivalently: the read rule + 1 | 4 + w |
| Anything from ROM (M1 or data), write to the ROM area | `RAM_ = 1`, `WR_EN = 0` | 0 | nominal |
| I/O read/write, any port | IORQ, not M1 | **+2**, phase-independent | 4 + 2 = 6 |
| Interrupt acknowledge | M1 + IORQ | +2 in theory, but never in practice: /INT forces normal speed first (§2.3) | - |
| Refresh, internal T-states | - | 0 | - |

**Why the M1 rule differs from the data read rule.**

- A data read samples the bus on the **falling** edge of T3. When it is released at a slot edge E(0), T3
  falls at the end of count 1, exactly as the latch closes on the fresh byte.
- An M1 samples on the **rising** edge of T3. Released at the same edge, it would sample in the middle of
  count 1, with /CAS only half a count old.
- The `!WAIT_.Q` term therefore forces at least one wait. It then releases at `H0 = 1`, the column count, so
  that T3 rises right after the latch has closed. It also guarantees that /RAS fell no earlier than the start
  of T2, when the address has been stable for a full T-state.
- This is the same mechanism that produces Even M1 in normal mode.

**Why writes are not cheaper.** `WE.D = !(WR_EN & !H1M & RAS_.Q)` writes the CPU's bus data directly into
the DRAM in the CPU slot. It is an early write (the Z80 puts data on the bus in T1; /WR is not used). Nothing
latches the data.

**I/O detail.**

- The terms (a), (b) and (c) need `IORQ_ = 1`, so during I/O `WAIT_.D = Pin13.Q`, and
  `Pin13.D = !IORQ_ & !WAIT_.Q`.
- /IORQ goes active just after the edge that starts T2, so the register first sees it at the edge of the
  automatic TW. The sequence is: WAIT_ = 0 (TW), 0 (Tw1, Pin13 becomes 1), 1 (Tw2 releases), then T3.
- Result: T1 T2 TW Tw Tw T3 = 6 T.

### 4.2 SC15.3 (post #41)

| Access | Wait states |
|:--|:--|
| Data read, write, **and M1** from/to RAM | `w = min{ j ≥ 0 : slot(e2 + j) }` (paper 0-3, border 0-1). WAIT_ is combinational, so the CPU sees `!RAS_.Q & !H1M` for the count of the T-state it samples in. That gives the same slot condition as SC15.1's term (a) |
| ROM | 0 |
| I/O | +1 (Pin13 toggles: TW sees 0, the next Tw sees 1) |
| Normal mode | never waits: **no Even M1** |

### 4.3 Worked examples (edge-level simulation of the equations, `sc15-wait-sim.py`)

Code and data in RAM. All figures are in turbo T-states. "Steady" means the repeating pattern after the
first instruction. The ideal 7 MHz length (no waits) is in brackets.

| Sequence | SC15.1 paper | SC15.1 border | SC15.3 paper | SC15.3 border |
|:--|:--|:--|:--|:--|
| NOP stream [4] | steady 8 each (M1 T1 always at p = 0, w = 4). First NOP 5-8 depending on phase | steady 6 each (w = 2); first 5 or 6 | steady 4 (T1 at p = 3); first 4-7 | steady 4; first 4-5 |
| `LD A,(HL)` repeated [7] | steady 12 (M1 6 at p = 2 + MR 6 at p = 0). First from p = 0: 8 + 6 = 14 | steady 10 (M1 6 + MR 4); 9 when the M1 starts at odd p | steady 8 (M1 5 + MR 3) | 7 or 8 |
| `LD (HL),A` repeated [7] | 12 (identical to the read) | 10 | 8 | 7-8 |
| `OUT (n),A` [11] | 17-20 | 15-16 | 12-15 | 12-13 |
| NOP from ROM [4] | 4 | 4 | 4 | 4 |

Converted to 3.5 MHz-equivalent T (divide by 2):

- SC15.1 in the paper: a RAM NOP takes 4, which is **no gain over normal mode**, and `LD A,(HL)` takes 6
  instead of 7.
- SC15.1 in the border: 3 and 5.
- ROM code with no RAM access gets the full 2x everywhere.

The paper window is 128 of 224 T on 192 of 312 lines, 35 % of the frame. The overall gain for RAM code under
SC15.1 is therefore well below 2x. I found no measurement to compare against.

Step-by-step example, SC15.1, paper, NOP at p(T1) = 0:

```
edge e (p): T1 e0(0)  T2 e1(1): WAIT_.D=0 (forced, term c needs !WAIT_.Q)  -> Tw
            e2(2): H0=0 -> 0 -> Tw     e3(3): H1M=1 -> 0 -> Tw     e4(0): H0=0 -> 0 -> Tw
            e5(1): H0=1 & H1M=0 & !WAIT_.Q -> 1 -> T3 at e6, T4 at e7.  Total 8, next T1 at p=0 again.
```

LD A,(HL) MR in the paper at T1 p = 0: T2 is at p = 1. The next slot edge is p = 0, 3 edges later, so w = 3
and the cycle is 6 T.

### 4.4 Implementation notes for unreal-ng

- **Clock.** In turbo, unreal-ng counts CPU T-states at 2x per frame (`hw_turbo_shift = 1`). Define `u` = the
  7 MHz edge index since frame start: `u = 2·frameT` at normal-mode T boundaries, and in turbo each CPU T is
  one u.
- **Phase anchor.** The counter is common to both modes, so one constant ties turbo phase to the existing
  Even-M1 parity. In normal mode a RAM M1 does not wait when its T1 starts at `p = 3` (T2 at `p = 1`).
  unreal-ng's Even M1 (`z80.cpp:441-451`) treats **even** frame T as no-wait. Hence **`p(u) = (u + 3) mod 4`**:
  the turbo edge half a T after an even frame T is `p = 0`.
- **Paper window.** The window starts at `p = 0`, at `u = 2·P0 + 1`, where `P0` is unreal-ng's
  paper-start frame T. P0 is even (16152 = INT + 14336), so this is consistent with the anchor. Then:
  `d = u − (2·P0 + 1)`, `line = floor(d / 448)`, `x = d mod 448`, and `paper(u) = 0 ≤ line < 192 && x < 256`.
- **Scope.** Apply the rule only for RAM accesses (the RAM select, not the address: RAM at #0000 counts,
  ROM does not), in every bank. Charge the wait before the data transfer, because it changes what the CPU
  reads. The floating bus is not an issue in turbo, since waits happen before T3.
- **INT.** While /INT is low, run at 3.5 MHz with the normal rules. This changes the frame budget and ISR
  timing in turbo.
- **Firmware choice.** SC15.1 and SC15.3 differ. A model option with default **SC15.1** is the safer choice
  (§7 Q2). Whichever is chosen, keep the normal-mode Even M1 tied to it: SC15.1 yes, SC15.3 no.

## 5. Where BORDER_ comes from, and the paper window in frame T-states

Traced on the schematic (crop-hcounter-bc.png, crop-brd.png):

- **Horizontal.**
  - Parts: DD3 (H0-H3, free-running mod 16, clocked by CLK-), DD4 (H4-H7, clocked by DD3's carry, preset
    input D = 0011) and DD8-1 (ТМ2 toggle on DD4's carry). BC- = DD8-1 Q.
  - The load: DD4's /PE = OR(DD8-1 /Q, DD4 carry) (DD14-2). DD4 therefore reloads 3 (then counts up to 4)
    only at the end of the BC- = 1 half.
  - Result: BC- = 1 while DD4 counts 0..15 (256 counts = 128 T) and BC- = 0 while it counts 4..15
    (192 counts = 96 T). The line is 448 counts = 224 T.
  - Line sync: `CC- = NAND(DD8-1 /Q, H7, !H6)` → DD4 = 8..11 of the BC- = 0 half.
  - **Line layout from the window start: 0-127 T paper window, 128-159 right border, 160-191 HSYNC,
    192-223 left border.**
  - The legend on the schematic reads "BC\ - Бордюр". BC- is low in the border.
- **Vertical.**
  - Parts: DD5/DD6 (V0-V7, clocked by BC- rising, the window start). /PE = NAND(V7, V6) (DD12-3). The load
    value is BK- ? 72 : 0 (DD5 D3 and DD6 D2 are tied to BK-). DD8-2 (ТМ2) toggles on the same pulse, and
    BK- = its Q.
  - Phases: the BK- = 1 phase counts V 0..191 (192 lines, paper). The BK- = 0 phase counts 72..191
    (120 lines). 312 lines in all.
  - Frame sync: `KC- = NAND(V4, V5, V6 & border phase)` → V 112..127 of the border phase (16 lines).
  - The border phase is therefore: 40 lines bottom border (72..111), 16 lines VSYNC, 64 lines top border
    (128..191).
- **BORDER_** (EPLD pin 7) = `BRD- = BK- AND BC-` (DD13-2, 555ЛИ1). It is 1 only inside the 256 × 192 fetch
  window.
- **/INT.** DD2-2 (ТМ2): D = GND, clock = **KC- rising**, /S = B6-. INT1 = Q, /INT = Q via R81.
  - /INT falls when VSYNC ends, at the start of line V = 128 of the border phase. That line begins at the
    window-start position of the line (V increments on BC- rising).
  - It ends when B6- goes low. I did not trace B6-, so the INT length is unknown from the schematic
    (unreal-ng uses 32).
- **Result: the fetch window starts exactly 64 × 224 = 14336 T after the /INT edge.** Each of the 192 lines
  has window T 0..127 relative to `INT + 14336 + 224·L`. The window starts at a DD3 wrap (count ≡ 0 mod 16),
  so its first turbo edge has `p = 0`.
  - This agrees with unreal-ng's Scorpion constant (14336, from Xpeccy and ZXMAK2).
  - Whether the **displayed** first pixel lags the fetch window by a character or so was not traced. For the
    wait logic only the counter window matters.
  - Sub-T offsets (gate delays of ~40-150 ns on BC-, KC- and INT) are below the resolution of the model.
- The BC- edge detail: at the end of the window BC- falls about 120 ns after the edge that samples count 255
  (DD4 load glitch). The EPLD therefore still sees BORDER_ = 1 at that edge, and the sampled window is
  exactly counts 0..255.

## 6. Answers to the specific questions

1. **Equation set**: §1.2 (SC15.1) and §1.3 (SC15.3), with signal meanings in §1.5. CAS is not an EPLD
   output (`CAS- = !H0`, DD11-3). The "latch control" is WR_BUFF, the DD38 read-latch enable.
2. **Timeline**: §3.
3. **Rule plus examples**: §4.
4. **Even M1 in turbo?**
   - SC15.1: the normal-mode Even-M1 terms are all gated by `!TRB.Q` and switch off. Turbo has its own,
     stronger M1 rule instead: at least 1 wait, then alignment to the CPU column count. In practice every
     RAM M1 ends with T3 on a fixed phase (`p = 2` in the paper, even `p` in the border). Parity rounding
     alone (Xpeccy's approach) under-counts it.
   - SC15.3: no M1 rule in either mode.
   - The ISR entry (INT active) runs in normal mode, so SC15.1's normal Even M1 applies there.
5. **BORDER_ / paper region**: §5. The window is `INT + 14336 + 224·L + [0, 128)` T, L = 0..191.
6. **Other emulators**:
   - MAME `scorpiontb`: turbo is `set_clock_scale(2)` on IN #7FFD and 1 on IN #1FFD, with mirror mask 0x3fdc
     (`mame/src/mame/sinclair/scorpion.cpp:635-640`). The F11 "TURBO" key toggles it (`:547-553`). Even M1 is
     off for the TB (`m_is_m1_even = 0` at `:597`). No turbo waits. GMX: `#7EFD` bit 7 (`:786-789`).
   - Xpeccy: `scrpIn7FFD` → `compSetTurbo(comp, 2)` and `scrpIn1FFD` → 1
     (`Xpeccy/src/libxpeccy/hardware/scorpion.c:78-86, 114-115`). No waits. Its `scrp.wait` odd-to-even
     rounding (`spectrum.c:577-579`) also runs in turbo, in CPU T.
   - ZXMAK2: no Scorpion turbo. It only has the yellow Even M1 (`UlaScorpionYellow.cs:57`); the green board
     has neither.
   - unreal-ng: 2x with no waits, and Even M1 disabled in turbo (`z80.cpp:449` checks
     `hw_turbo_shift == 0`). Flip-flop on IN reads in `ports/models/portdecoder_scorpion256.cpp:139-171`.
     Side note: that file's comment says "the turbo GAL clocks its mode latch". The latch is actually the
     discrete DD9-2 flip-flop, and the EPLD only synchronizes it.

## 7. Confidence and open questions

| Claim | Confidence |
|:--|:--|
| SC15.1 / SC15.3 equations (fuse-decoded, identical to the ABEL) | high |
| DD30 pin/net mapping, DD38 is a read latch, writes not buffered | high (schematic + TURBOSC.DOC) |
| Turbo waits apply to all RAM (RAM select), never to ROM | high |
| DD3 changes on falling CLK, EPLD registers on rising CLK | high (schematic) |
| H/V counter structure, BORDER_ = BK- & BC-, window 256 × 192, INT at KC- rising, 14336 T INT→window | medium-high (traced from a redrawn schematic, see Q4) |
| Per-access wait counts §4 (SC15.1) | medium-high: they follow from the equations plus the standard Z80 sampling points (WAIT at falling T2/Tw, M1 data at rising T3, data read at falling T3, IORQ from rising T2). The design only works if RAM_/WR_EN, asserted on falling T1, settle before the next rising edge (71 ns). That must hold for the circuit to work at all |
| I/O +2 (SC15.1) / +1 (SC15.3) | medium (depends on when the register first sees /IORQ) |
| Turbo suspended while /INT is low | high for the logic; unverified on hardware |
| Phase anchor `p(u) = (u+3) mod 4` | medium: tied to unreal-ng's current Even-M1 parity, which is itself not hardware-measured |

Open questions:

- **Q1. Which firmware shipped?** SC15.1 ("VER2.0", 01.01.96) and SC15.3 (compiled 02/26/96, apparently
  later) were both posted for the green SC15. SC15.4 (EPM7032, used by the official yellow-board upgrade)
  was not decompiled. TURBOSC.DOC shows it takes extra inputs (MEMRD, ADRAM, T_ON, T_OFF) and has a
  `RAM&M1` output wired to the old board's DD59, so it is probably M1-aware too. A real-machine measurement
  would settle it: a NOP loop in RAM timed in the paper vs the border in turbo, 8 vs 4 T per NOP.
- **Q2. SC15.3's M1 timing.** Under the timing model, SC15.3 releases an M1 at the CPU's row edge. The Z80
  then samples the opcode half a count (71 ns) after /CAS falls. That needs DRAM with tCAC ≈ 30-40 ns.
  Either SC15.3 relied on fast DRAM or the model misses something. SC15.1's forced wait avoids the problem.
  This is the main reason to prefer SC15.1 as the default.
- **Q3. INT length** (B6- source not traced). Displayed-pixel lag vs the fetch window was not traced.
- **Q4. The schematic is a 2005-06 redraw by deathsoft** from a scan plus "Графика-М" (post #1). It may
  contain errors; posts #2-#24 fix some. The counter wiring used here (DD3/DD4/DD8-1/DD14-2, DD5/DD6/DD8-2,
  DD13-2) is self-consistent: it gives exactly 448 × 312 = 69888 T.
- **Q5.** Yellow boards with the Turbo+ upgrade differ on EPLD pins 3, 4 and 5 (post #38). Their equations
  are unknown.
