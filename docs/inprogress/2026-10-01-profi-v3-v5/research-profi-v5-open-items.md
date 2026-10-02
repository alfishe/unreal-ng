# Profi v5 open items (phase 7): palette gate, CP/M switch, 15 MHz turbo

**Date:** 2026-10-02 · **Status:** research; implemented (`[PROFI] DffdDecode`, the CP/M switch) · part of [README.md](README.md)

Sources are in the materials folder outside the repository (README.md), written below as `materials/...`; the
board schematics collection as `schematics/...`. Netlist helper: [tools/machines/profi/netlist/](../../../tools/machines/profi/netlist/README.md).

Confidence: **H** = traced in the drawing and confirmed by a second, independent source. **M** = traced in one
drawing only. **O** = open (the sources do not settle it).

Helper scripts in this folder:

- `part.py DD33 DD30 ...` prints each pin of a part in the v5.06 netlist (`materials/v5-wait-netlist/netdump.txt`),
  with the net and every other pin on that net.
- `dis.py <rom> <offset> <addr> <count>` is a linear Z80 disassembler (uses `z80dis`).
- `PROF5-*.utf8.txt` are the Profi 5.0 text schematics, converted from CP866 to UTF-8.

The chip types used to read the netlist: 555ЛА3 = 7400, ЛЛ1 = 7432, ЛЕ1 = 7402, ЛИ1 = 7408, ЛН1 = 7404,
ЛА2 = 7430 (8-input NAND), ТМ2 = 7474, ТМ8 = 74175, ТМ9 = 74174, КП11 = 74257 (2:1 mux, S=0 selects A),
531РУ9 = 16x4 RAM with inverted outputs (74S189 type). For diodes, pin 1 = anode and pin 2 = cathode. The
v5.01 change note checks this on VD14 ("cathode to DD29/6"; DD29/6 = `/MAG` = VD14.2).

---

## Q5. The palette write gate

### Answer (H)

On the real board, the palette is written by **any I/O write with A0=0 and A7=0, while DFFD bit 7 (80DS) = 1**.
Neither CP/M (DFFD bit 5) nor BLOCK (in either sense) gates it. The manual sentence "CP/M + 80DS=1 + BLOCK=1"
does not match the schematic in the same album, and it does not match the v5.06 netlist either.

### Write strobe trace (v5.06 netlist)

| Step | Part / pin | Logic | Net |
|:--|:--|:--|:--|
| 1 | DD103.3 (7432) | `/IORQ OR /OUTIORQ` (an external device can veto) | `/IORQGE` |
| 2 | DD19.8 (7432) | `ADR0 OR /IORQGE` | NET00101 |
| 3 | DD19.3 (7432) | `NET00101 OR /WR` | `C_BORD` (low = I/O write, A0=0; also clocks the border latch DD29/DD50) |
| 4 | DD5.12 (7404) | `NOT ADR7` | NET00084 |
| 5 | DD30.11 (7400) | `NAND(80DS, NOT ADR7)` | NET00086 (low = 80DS and A7=0) |
| 6 | DD33.11 (7432) | `C_BORD OR NET00086` | NET00088 = WR/RD of DD40 and DD48 (low = write) |

- The palette RAMs are DD40 and DD48 (531РУ9). Their CS pin is tied to GND.
- Data in: DD48 DI0-3 = ADR8-11 and DD40 DI0-3 = ADR12-15. **The data comes from the high address byte.** The
  outputs are inverted, so software writes the inverted colour.
- RAM address: A0-A3 = BX, RX, GX, YX. This is the colour index from the video pipeline (DD51/DD52), not from the
  CPU. The entry written is whatever index the video pipeline holds during the write. In the border, that is the
  border colour.
- `80DS` = DD46.15. DD46 is a 74175: D4 = DC7 and CLK = `C_CMR1`, so `80DS` is DFFD bit 7.
- No other signal enters this chain. That includes `/CP/M` (DD46.6), `BCMR` (7FFD bit 5) and `/BLOCK` (DD75.5).

### The same in the Profi 5.0 album (Kondor 1994)

Text schematic sheet 6А (`PROF5-09`) draws the same gates. D33 (ЛЛ1) pin 11 = `WR-RU`. Its inputs are pin 12
(the border strobe) and pin 13 = D30 (ЛА3) `NAND(80DS, D5 = NOT ADR7)`. The border strobe `C-BORD` is drawn on
sheet 2Б (`PROF5-04`) as `/WR OR (ADR0 OR /IORQ)`. So the gate is the same in 5.0 and in 5.06, and the album
contradicts its own text on p.11.

### Independent check: the factory BIOS

Micco BIOS 2.0 (SYS page CRC `3A185C8D`, file `speccy4ever/rom/PB20-36F5F7BD.ROM`) runs this from reset:

```
0000 IM 1 / JP #0334
0334 LD SP,#034D / LD BC,#DFFD / LD A,#80 / OUT (C),A   ; DFFD = #80: 80DS=1, CP/M=0
...  JP #0454 -> DI / EI / HALT / LD C,#7E / OUT (C),A / LD A,(HL) / CPL / LD B,A / OUT (C),A ...
```

The BIOS loads the palette with DFFD = #80 (CP/M = 0). 7FFD has not been written since reset, so 7FFD bit 5
(BLOCK) = 0. It uses `CPL`, which fits the inverted RAM outputs. On a board with the gate the manual describes,
the BIOS's own palette load would do nothing.

### What "BLOCK" means (two different signals)

| Name | Where | What it does |
|:--|:--|:--|
| BLOCK = 7FFD bit 5 (`BCMR`, DD35.15) | manual p.11, CMR0 D5 | Locks 7FFD. DD10.3 = `BCMR AND /NOROM` feeds DD20 (7402), so `C_7FFD = IOWR & A1=0 & A15=0 & NOT(lock)`. DFFD bit 4 (NOROM) lifts the lock. It has no effect on the palette. |
| `/BLOCK` = DD75.5 (7474 Q) | added on v5.06 only (solegstar change list, item 4) | The "port #FD protection". See below. It has no effect on the palette. |
| `/BLOK` (X1.64) | bus signal, pulled up by R7 | An external ROM disable (DD12 → `ZXROMCS`). It is not related. |

**What `/FD` is.** `/FD` = DD76.8, a 7430 NAND of DC0, DC1, /DC2 (DD60.10), DC4, /DC5 (DD60.12), DC6 and DC7.
DC3 is not an input. So `/FD` = 0 when the data bus holds **#D3 or #DB**. Those opcodes are `OUT (n),A` and
`IN A,(n)`. DD75 stores `/FD` at the rising edge of `/M1` (D = /FD, CLK = /M1, /R = `/ONOFF`, /S = VCC). So
`/BLOCK` is low for the whole of an `OUT (n),A` or `IN A,(n)` instruction. That includes the DD/FD-prefixed forms,
because the opcode is fetched in a second M1.

`/BLOCK` gates only the DFFD strobe: DD71.6 = `AND(NET00109, /BLOCK)`, then DD23.3 = `C_CMR1`. In full:

`C_CMR1 = IOWR & (A15..A8 = #DF, full decode by DD74 7430) & A1=0 & NOT(opcode was #D3/#DB)`.

solegstar explains the reason on zx-pk 21644, post 618538. On the 5.0x boards, the DFFD decode was only A13=0 and
A1=0 (5.0 sheet 4А: D8/D23). So a 128K program doing `LD A,x : OUT (#FD),A` with A bit 5 = 0 also wrote DFFD.
v5.06 adds the full high-byte decode (item 3) and this opcode filter (item 4).

### Emulator rule (palette)

- Palette write = **I/O write with A0=0 and A7=0 while DFFD bit 7 = 1**. Do not add a CP/M or BLOCK condition.
  This is the current `portdecoder_profi.cpp:388` rule, so no change is needed.
- Data = `NOT (A15..A8)`. The same OUT also writes the border latch.
- Side note: the current DFFD decode (`IsPort_DFFD`: A15=1, A13=0, A1=0) is close to neither board. The 5.0 decode
  is A13=0, A1=0, with A15 not decoded. The v5.06 decode is high byte = #DF, A1=0, and no write from `OUT (n),A`.
  If a `v506` board variant is wanted, these are the two differences.

---

## Q6. The front-panel CP/M switch

### Answer: the processor-board part is settled (H); the ROM start page is not settled (O)

The CP/M switch **does not preset any bit**. It **holds DFFD (all 8 bits) cleared** for as long as it is pressed,
through the asynchronous clear of the DFFD latches. It also holds the v5.06 `/BLOCK` flip-flop reset, which
blocks DFFD writes as well. 7FFD and the ROM page lines are not touched.

### Wiring (v5.06 netlist and the board silkscreen)

The album page 7 silkscreen of front-panel header P106 has these pin pairs: `GND RST`, `CP/M`, (empty),
`GND TURBO`, `GND PWRLED`.

| P106 pin | Net | Goes to |
|:--|:--|:--|
| 7 (CP/M) | NET00055 | DD1.12. DD1 is an inverter whose input DD1.13 (NET00120) is the power-on RC node (C1, R6). After power-up DD1.12 sits at **low**. |
| 8 (CP/M) | NET00056 | R13 3k pull-up, C9 0.1 µF to GND, DD18.1 |
| 3/9 | GND | |
| 4 (TURBO) | NET00080 | VD20 cathode. VD20 anode = `/TURBO`. |
| 10 (RST) | `/SRESET` | soft reset |

- `/ONOFF` = DD18.3 = `NET00056 AND /HRESET` (7408).
- `/ONOFF` goes to the /CLR pins of **DD36 and DD46** (74175: DFFD bits 0-3 and 4-7) and to /R of **DD75** (the
  BLOCK flip-flop). Nothing else uses it.
- **Switch open** (released): C9 holds `/ONOFF` low for a moment at power-on (DFFD = 0), then R13 pulls it high.
  DFFD is then free to change.
- **Switch closed** (pressed): pins 7 and 8 are joined, so `/ONOFF` follows DD1.12. That is high for a moment at
  power-on and then low for good. DFFD is held at #00. CP/M, 80DS, NOROM, SCR, SCO and the extended pages all stay
  off. That is "a standard Spectrum 128".
- The Profi 5.0 album sheet 4Б (`PROF5-07`) draws the same thing: SB6 "CP/M" sits between D1 pin 12 and D18 pin 1,
  with R13 3K and C9 0.1. The v3.2 manual describes the earlier ON/OFF button the same way: "при нажатом
  состоянии отключает все дополнительные режимы ... стандартный Spectrum-128" (3.2mn p.5).

### Reset levels (the same netlist, plus album p.11 for jumper P107)

| Signal | Source | Clears |
|:--|:--|:--|
| `/ONOFF` | the CP/M switch, power-on, `/HRESET` | DFFD, BLOCK flip-flop |
| `/RESET` = DD18.6 = `/HRESET AND /SRESET` | the front-panel RST button (soft), the keyboard reset (hard) | 7FFD (DD35 /CLR), CPU |

The album p.11 says the same about P107: "при HARD-RESET будут сбрасываться порты DFFD и 7FFD, a при SOFT-RESET
... только 7FFD". So **the front-panel RST does not clear DFFD**.

### What is not settled (O): the start page

The manual (5.0 p.6) says: "released → TR_DOS=0, ROM14=0 → microDOS boot or test; pressed → TR_DOS=1 → Spectrum
128". On the processor board, the ROM DD28 gets A15 = `/TR_DOS` (from bus X1.63, driven by the periphery board) and
A14 = `ZXROM14` (7FFD bit 4 through the ROM14 jumper). `/ONOFF` does not reach the system bus. The 5.0 bus table
(`PROF5-0C`) has no ONOFF pin either. So the switch cannot change the reset ROM page through any path on the
processor board. The BIOS 2.0 reset path (above) does not test anything before it writes DFFD = #80 and draws its
menu.

To settle it, any one of these would do:

1. The periphery board's DOS-latch reset logic. In the 5.0 text sheet `PROF5-10`, that is D13 (ТМ2), D32, D29 and
   D24. This needs a careful read of the PDF album (profi50.pdf p.13-16). For v5.06, it needs the periphery CPLD
   source (zx-pk thread 21356).
2. A full trace of BIOS 2.0 from #0454 on, to see whether it detects a DFFD that cannot be written and jumps to the
   128 page.
3. A test on a real board.

### Emulator rule (switch), H for the parts given

- Add `FrontPanelSwitch::CpmLock` (a "CP/M" or "ON/OFF" switch), kept like TURBO: set by the ini at power-on, kept
  across resets.
- While pressed: DFFD = #00 immediately (asynchronous clear), and every DFFD write is ignored. 7FFD works as usual.
- Released: DFFD is writable again. It stays #00 until software writes it.
- Power-on and hard reset clear DFFD. A soft reset (front-panel RST) clears only 7FFD and the CPU.
- Do not change the reset ROM page from this switch until the open item above is settled.

---

## Q7. The 15 MHz turbo (third crystal)

### Answer: the crystal and its selection are settled (H); "15 MHz" is not settled (O)

The v5.06 board has ZQ3. The BOM on album p.10 lists "ZQ3 КВАРЦ 16-20MHZ". It runs in oscillator DD31
(R23, R24, C12) and gives the net `XMHZ` (DD31.8). **No jumper, port bit or switch selects it. 80DS selects it.**

### Clock chain (v5.06 netlist)

| Part | Function |
|:--|:--|
| DD25 (74257), S = `/80DS` | 80DS=1: 1Y `TCPU` = `XMHZ`, 2Y `TRAM` = `XMHZ`, 4Y `V14` = 14 MHz. 80DS=0: `TCPU` = `ZX14MHZ`, `TRAM` = 14 MHz, `V14` = 14 MHz. |
| DD34 (74257), S = `80DS` | Video pixel clock `F0`: 80DS=0 gives `V14` (14 MHz), 80DS=1 gives `12MHZ` (ZQ1). **ZQ1 12 MHz is the hi-res pixel clock only.** It also goes to X1.54 for the periphery. The CPU never gets it. |
| DD2.1 (7474) + DD9.3 (OR) + DD2.2 (7474) | `F2T` = `TCPU / 4` when `/TURBO` = 1. When `/TURBO` = 0, DD2.1 is held reset and `F2T` = `TCPU / 2`. |
| DD22 (two inverters in parallel) | `F2CPU` = `F2T` → Z80 CLK (DD24.6) |
| `/TURBO` | R64 3k pull-up. Pulled low by the TURBO switch (P106.4 through VD20), by bus X1.33, and by DD75.2 through VD21 (the v5.06 wait fix, item 6). |

The 5.0 album sheet 1А (`PROF5-01`) draws the same D25 mux (`SE` = 80DS, `B0` = `XMHZ` → `TCPU`, `B1` → `TRAM`,
`B3` → `V14`). It also has the ZQ3 table: 16/18/20/22/24 MHz with C12 and R23/R24 values, and the RAM grade for
each. Manual p.6: "В минимальном варианте (при замене не установленной м/с DD25 перемычками) ... двумя тактовыми
генераторами, как и в предыдущих версиях".

### Resulting CPU clock (no waits counted)

| Mode | Normal | Turbo (`/TURBO` = 0) |
|:--|:--|:--|
| Spectrum (80DS=0) | 14 / 4 = **3.5 MHz** | 14 / 2 = **7 MHz** |
| DS80 (80DS=1), ZQ3 = 16 MHz | 4 MHz | 8 MHz |
| DS80, ZQ3 = 20 MHz (v5.06 BOM upper end) | 5 MHz | 10 MHz |
| DS80, ZQ3 = 24 MHz (5.0 table upper end) | 6 MHz | 12 MHz |

In DS80 the DRAM arbiter clock `TRAM` also moves to ZQ3. That explains why the album ties the ZQ3 choice to the
RAM speed ("15-17 mHz ... 23-25 mHz" against RAM grades, p.20).

### Open points (O)

- **"до 15 МГц"** (5.0 p.2) does not follow from the drawn /2 and /4 divider with a 16-24 MHz crystal. The most it
  gives is 12 MHz. It may be a marketing number, or it may refer to the crystal. No source here settles it.
- **The minimal two-crystal build** (DD25 not fitted, jumpers instead, v5.01 change note I.1 "соединить DD25/12 с
  DD25/13"). The 5.02 note "в режиме большого растра 512х240 (CP/M) не отключать турбо-режим" hints that the CPU
  then runs slower in hi-res. The jumper pattern for TCPU is not drawn in the sources read, so the clock in that
  build is not settled.
- The scope here is the clock source and divider only. The waits (DD75.2 → VD21 → `/TURBO`, `/REDYT`) were
  covered by the earlier WAIT work.

### Emulator rule (clock), H

- CPU clock = base / (turbo ? 2 : 4). The base is 14 MHz with DS80=0, and the ZQ3 frequency with DS80=1.
- Add an ini value for ZQ3. Default 16 MHz (a safe 16-20 BOM value). Allowed 16-24.
- The CPU clock changes when DFFD bit 7 is written. The 12 MHz crystal sets only the hi-res pixel or raster
  timing.
- A "two-crystal" board option, if ever wanted, stays open until its jumper wiring is found.

---

## Sources

- `materials/v5-wait-netlist/netdump.txt`: parts DD1, DD2, DD5, DD8, DD9, DD10, DD12, DD18,
  DD19, DD20, DD22, DD23, DD25, DD27, DD28, DD30, DD31, DD33, DD34, DD35, DD36, DD40, DD46, DD48, DD60, DD71,
  DD74, DD75, DD76, DD103, and the net index.
- `materials/web/zx-pk/files/extracted/profi506-proc-album.pdf`: p.7 (P106 silkscreen), p.10
  (BOM, ZQ3), p.11 (jumpers, P107 HARD/SOFT).
- `materials/web/zx-pk/zx-pk-21644-page1.txt`: solegstar's v5.06 change list, items 3-6.
- `schematics/profi-5.0-scheme/extracted/PROF5-01, -04, -06, -07, -09, -0C, -10.TXT`.
- `materials/research/ocr/profi50-p02, p05, p06, p11, p12, p20.txt` and `profi50ch.txt`.
- `materials/speccy4ever/rom/PB20-36F5F7BD.ROM`: SYS page, BIOS 2.0.
