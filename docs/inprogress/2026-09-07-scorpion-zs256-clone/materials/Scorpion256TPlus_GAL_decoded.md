# Scorpion ZS-256 Turbo+: GAL decoding (fapch / turbo / profrom)

Source: `romychs/Scorpion256TPlus`, folder `GAL/`, schematic `Export/Schematic_Scorpion-256-Turbo_v16.2.8a.pdf`,
images `ROM/scorp401-E5476776.rom`, `ROM/scorp_prof401-Classic-Scorp-Profi.rom`.

All equations below come from a direct parse of the fuse maps (scripts `jed22v10.py`, `jed16v8.py`);
signal names come from the `NOTE PINS` blocks in the JED files and from the labels on the schematic. The 22V10 column mapping was checked on
`turbo.jed` (it gives meaningful WAIT/CLK/RAS), the 16V8 mapping on `fapch.jed` (it gives a coherent transition table).

## 0. How to read a JED (short)

- `0` = fuse intact, the input is connected to the term; `1` = input disconnected. A row of all `1` = the term is always true;
  a row of all `0` (or a missing row with `F0`) = the term is always false.
- **GAL22V10**: 44 columns = 22 inputs × (true, inverted). Order of the column pairs:
  `1, 2, 23, 3, 22, 4, 21, 5, 20, 6, 19, 7, 18, 8, 17, 9, 16, 10, 15, 11, 14, 13`.
  Row 0 is AR; then for OLMC 23…14: an OE row + 8/10/12/14/16/16/14/12/10/8 terms; row 131 is SP.
  `L5808` = 20 bits S1,S0 for OLMC 23…14: **S1=1 combinational / S1=0 registered; S0=1 active high / S0=0 active low (inverted).**
- **GAL16V8**: 32 columns, pairs `2, 1|fb19, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 11|fb12`;
  `L2048` = XOR (1 = true output), `L2120` = AC1, `L2128` = PTD (product term enable), `L2192` = SYN, `L2193` = AC0.

Notation: `!` = inversion, `·` = AND, `+` = OR, `:=` = registered output (D flip-flop), `=` = combinational.

---

## 1. FAPCH.JED: the PLL of the floppy data separator (DD59, GAL16V8)

Purpose: digital phase lock of the FDC data window (README: "floppy controller with a PLL"). It has nothing to do
with a "frequency/address patch".

Configuration: SYN=1, AC0=0 = **simple mode**, all OLMCs are outputs. `L2048 = 00001000` → XOR=1 only for pin 15 (D3);
the other outputs are **inverted**. `L2128`: all terms enabled (this is PTD, not a signature).

| Pin | Name | Direction |
|---|---|---|
| 5 | A4 | input: data pulse |
| 6 | A3 | input: feedback (ТМ9 = 74174 register) |
| 9 | A2 | input: feedback |
| 8 | A1 | input: feedback |
| 7 | A0 | input: feedback |
| 15 | D3 | output, true |
| 12 | D2 | output, inverted |
| 13 | D1 | output, inverted |
| 14 | D0 | output, inverted |

Terms:

```
 D3 = A3·!A1 + A3·!A2 + A4·A3·!A0 + A4·!A3·A0·A1·A2

!D0 = !A4·A3·A1·!A2 + !A3·A0·A2 + A4·A0 + !A4·!A0·A1·!A2 + !A4·A3·!A0·A1

!D1 = A4·A0·A1 + A4·!A0·!A1 + !A4·!A0·A1·A2 + !A4·A0·!A1·!A2
    + !A4·A3·!A0·A1 + !A4·A3·A1·A2 + !A4·!A3·A0·!A1 + !A4·!A3·!A1·!A2

!D2 = !A0·!A1·!A2 + A4·A0·A1·A2 + A4·!A0·!A2 + A4·!A1·!A2
    + !A4·A3·A1·A2 + !A4·!A3·!A0·!A1 + !A4·!A3·!A2
```

Transition table (input A4 A3..A0 → output D3..D0), computed from the terms:

```
A4=1 (no pulse):      D = (A3..A0) + 1 mod 16        - free count
A4=0 (pulse):         0→1 1→1 2→2 3→3 4→3 5→4 6→5 7→6
                      8→B 9→D A→C B→E C→F D→F E→0 F→1
```

Interpretation: D3..D0 are latched by a register and fed back to A3..A0, a 16-phase counter. Without a pulse it
increments; on a pulse the phase is corrected: states 0…7 are "held back", 8…F are "pushed forward",
E/F jump to 0/1. The data window is pulled toward the pulse edge: a classic digital PLL.

---

## 2. TURBO.JED: turbo mode and memory controller (DD30, GAL22V10, "TURBO 15.3")

| Pin | Name | Type |
|---|---|---|
| 1 | CLK_7MHZ | input/clock |
| 4 | IORQ_ | input |
| 5 | WR_EN | input |
| 6 | RAM_ | input |
| 7 | INT | input |
| 8 | TRB_IN | input |
| 9 | BORDER_ | input (**not used in any term**) |
| 10 | M1_ | input |
| 11 | H0 | input (**not used**) |
| 13 | H1 | input |
| 14 | H1M | comb., true |
| 15 | Pin13 | **registered**, inverted: the only flip-flop |
| 16 | WR_BUFF | comb., true |
| 17 | RAS_ | comb., inverted |
| 18 | TRB | comb., inverted, OE disabled (internal node with feedback) |
| 19 | WE | comb., inverted |
| 20 | CLK_CPU | comb., true |
| 21 | WAIT_ | comb., true |
| 22, 23 | - | not used (OLMC without terms, OE disabled) |

`L5808 = 00 00 11 11 10 10 11 00 11 00` (pins 23…14).

```
WAIT_   = INT + TRB_IN·!M1_·!H1 + TRB_IN·!WR_BUFF·!M1_ + !CLK_CPU·WE·!M1_
CLK_CPU = CLK_7MHZ·!INT + INT·!TRB_IN
!WE     = INT·H1 + WR_BUFF·H1 + TRB_IN + !CLK_CPU
!TRB    = !INT·H1 + !INT·!TRB_IN + CLK_CPU·!INT + !WE·!INT
        + !CLK_CPU·WE·TRB·TRB_IN·!RAS_·!H1
!RAS_   = H1M
WR_BUFF = H1M·!H1 + !INT·!WR_BUFF·H1M
!Pin13 := M1_ + WAIT_                      (D flip-flop, clock CLK_7MHZ)
H1M     = INT·H1 + WR_BUFF·H1
```

Notes: TRB and WR_BUFF are combinational latches on feedback, not registers. Pin 22 is a free OLMC,
not a "VCC pull-up". There is no "wait states during the border" logic in this design: BORDER_ and H0 are not in any term.

---

## 3. PROFROM.JED: the ProfROM plane selector (DD41, GAL22V10)

### 3.1. Wiring per the schematic

| GAL pin | Name in JED | Real signal (schematic) |
|---|---|---|
| 1 | clk | `RDR-`: "Чтение ПЗУ" ("ROM read"; also the /CE of ROM DD29) |
| 2 | A15 | `CS1` → ROM A15 (16K page select: service/DOS) |
| 3…9 | A12, A7, A6, A5, A4, A3, A2 | Z80 address bus |
| 10, 11 | - | **GND** |
| 13, 14 (sw), 16 (p16) | - | not connected |
| 15 | A14 | `CS27` → ROM A14 (16K page select: 48/DOS) |
| 17…21 | A10, A11, A9, A8, A13 | Z80 address bus |
| 22 | A16 | → ROM A16 |
| 23 | A17 | → ROM A17 |

ROM DD29 is a W27C020 (256K×8): A0-A13 from the Z80, A14 = CS27, A15 = CS1, A16/A17 from the GAL.

### 3.2. What is actually programmed

`L5808 = 10 10 01 01 01 01 01 00 01 11`: A17, A16 are combinational inverted; sw is combinational true;
p16 is registered inverted with no D terms (a constant); the other OLMCs are inputs.

```
!A17 = !CS1·!p11 + CS1·A10·!p16·p11 + !CS1·!A10 + !A10·p16·p11
!A16 = !A12·!p11 + A12·p16·p11 + !CS1·!A12·!p16 + CS1·!A12·!A10
 sw  = A17·!A16·A6·!A3·!A4·!A5·!A7·!A8·!A9·!A11·!A13·!p10
 p16 := (empty)
```

With p10 = p11 = 0 (as on the schematic):

```
A17 = CS1
A16 = A12
sw  - decodes #0040…#0043 (A10, A12 don't care), not wired anywhere
```

**Conclusion:** this JED does not implement plane switching. There is no state (p16 latches nothing, there is no
feedback from A16/A17 through flip-flops), and `sw` decodes the wrong address and is not connected. With this fuse map the 256K image
from `ROM/` would not boot: window `#0000`-`#0FFF` comes from plane 0, `#1000`-`#1FFF` from plane 1, and so on.
The file (2007, ispLEVER) is a stub or a version that does not match the board.

### 3.3. The real ProfROM protocol (from the firmware and MAME `sinclair/scorpion.cpp`)

There is no I/O port. The plane (2 bits → ROM A16, A17) is changed by **reading** the ROM:

- condition: the service monitor page is in window `#0000`-`#3FFF` (`#1FFD` bit 1 = 1);
- trigger: a read of address `#0100 + 4·S`, `S = A3:A2`;
- new plane = `f(S, current)`:

```
S  address cur.: 0  1  2  3
0  #0100         0  1  2  3    (unchanged)
1  #0104         3  3  3  2
2  #0108         2  2  0  1
3  #010C         1  0  1  0    (P ^ 1)
```

Confirmation in the image: planes 1-3 hold a stub at `#0111` that is copied to `#5BEE`:
`LD BC,#1FFD / LD A,2 / OUT (C),A / LD HL,#010C / LD L,(HL) / XOR A / OUT (C),A / JP 0`
(plane 2 reads `#0108`: 2→0; planes 1 and 3 read `#010C`: 1→0, 3→0).
Bytes of the service page at `#0100`: `E5 02`, `01 06`, `01 0A`, `01 0E`; `(#0101)>>2 & 3` = the plane number.

ROMs larger than 256K (Scorpion GMX / ZS-1024) use another scheme: port `#7EFD`, bits D4-D6 → A16-A18
(28F400, 512K), with a "fixrom" latch.

### 3.4. The logic the GAL should implement

Clock: `RDR-` (every ROM read); two registered outputs P1→A17, P0→A16 that hold their state. A0/A1 are not wired to the GAL,
so `#0100`…`#0103` are equivalent.

```
T  = CS1·!CS27·A8·!A13·!A12·!A11·!A10·!A9·!A7·!A6·!A5·!A4      ; service page, address #01xx, A7..A4 = 0

P1 := !T·P1 + T·!A3·A2 + T·A3·!A2·!P1
P0 := !T·P0 + T·!A3·A2·!(P1·P0) + T·A3·!A2·P1·P0 + T·A3·A2·!P0
```

Bit-wise breakdown of the table: S=1: P1'=1, P0'=!(P1·P0); S=2: P1'=!P1, P0'=P1·P0; S=3: P1'=0, P0'=!P0; S=0: hold.
It fits in a 22V10 (≤10 terms per OLMC). A reset of the plane to 0 on RES- should be added through AR.

---

## Appendix. JED checksums

| File | Device | Date | Fuse checksum |
|---|---|---|---|
| fapch.jed | GAL16V8D-10LP | 2007‑02‑25 | C626B |
| turbo.jed | GAL22V10D-10LP | 2007‑03‑04 | C9413 |
| profrom.jed | GAL22V10D-10LP | 2007‑03‑04 | C3D22 |
