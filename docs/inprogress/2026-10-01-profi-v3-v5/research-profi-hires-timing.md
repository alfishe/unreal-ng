# Profi hi-res (DS80, 512x240) timing on v3.2 and v5.0x

**Date:** 2026-10-03 · **Status:** research; implementation planned in [design-hires.md](design-hires.md) · part of [README.md](README.md)

Sources are in the materials folder outside the repository (README.md), written as `materials/...`, the schematics
collection as `schematics/...`. The v3.2 renders are kept with the materials (`materials/hires-v32-renders/`). Scripts:
[tools/machines/profi/hires/](../../../tools/machines/profi/hires/README.md).

Question: what does a Profi do in hi-res (`#DFFD` bit 7 = 1, the board signal `80DS`)? CPU clock, frame, INT, waits,
floating bus and AY clock, for the v3.2 (Kramis) and the v5.0x / 5.06 (Kondor) boards. Today the emulator runs hi-res
on the Spectrum-mode timing (3.5 MHz, 224 T x 312, INT from the lower PROM half).

Confidence: **H** = traced on a drawing or netlist, or stated by the board's own documents; **M** = inferred from
traced logic plus a model, or one independent statement; **O** = open.

Short names of sources:

| Name | Source |
|:--|:--|
| NET506 | v5.06 P-CAD netlist dump `materials/v5-wait-netlist/netdump.txt` (read with `tools/machines/profi/netlist/part.py`) |
| ALB506 | v5.06 album `materials/web/zx-pk/files/extracted/profi506-proc-album.pdf` (BOM p.9-10, jumper text p.11) |
| ALB50 | v5.0 album text schematics `materials/v5-open-items/PROF5-*.utf8.txt` and OCR `research/ocr/profi50-p*.txt` |
| CH50 | Kondor change lists V5.01 / V5.02 (`research/ocr/profi50ch.txt`; zx-pk 14599 p.43, 17911 p.1) |
| MDESK3 | v3.2 re-trace `schematics/profi-3.2-pcad-mdesk-2020/Profi3.zip`: `profi32cl-mdesk-sch.pdf` "List 1..9", `profi32il-mdesk-sch.pdf` "Interface list 1..6" (rendered to PNG in `materials/hires-v32-renders/`) |
| MAN32 | v3.2 manual OCR `research/ocr/ProfiV32mn-p*.txt` |
| SG506 | solegstar's v5.06 change list, zx-pk 21644 p.1 |
| ZXPK | forum posts quoted with thread/page |
| DEC | `tools/machines/profi/syncprom/profisync.py`, upper PROM half (A10 = 80DS); run by `tools/machines/profi/hires/ds80frames.py` |
| SIM | `tools/machines/profi/hires/ds80waitsim.py`, an extension of `tools/machines/profi/waitmodel/v5waitsim.py` |

---

## 1. CPU clock in DS80

### 1.1 v3.2: 3 MHz (6 MHz in turbo) — H

Evidence chain (MDESK3 List 1, crops `p3/z1.png`, `p3/z2.png`; earlier trace in
`research-profi-v3-turbo-floatbus.md` A1):

1. Z1 14 MHz (U30:E/F) and Z2 12 MHz (U30:C/B) go to U15 (555KP11 = 74LS157) 1A / 1B. `A/B` (pin 1) = `80DS`.
   1Y (pin 4) is the master clock.
2. The master clocks U4 (555IE10 = 74LS161), the only master counter.
3. `F2` = U31:A NAND of U4 Q1 = master / 4, `F2T` = U30:D NOT of Q0 = master / 2.
4. U16 (KP11) picks `F2` (normal) or `F2T` (turbo) into `F2CPU` (Z80 CLK). Nothing else clocks the CPU.

So with 80DS = 1 the CPU runs at 12 / 4 = **3 MHz**, and at 12 / 2 = **6 MHz** in turbo. The DRAM slot ring
(`/RAS` = not(Q0 xor Q1)) also runs from the same master, so one DRAM cycle per CPU T stays true.

Supporting statements:
- MAN32 p.2: hi-res "за счет изменения частоты тактового генератора увеличивается площадь видимого изображения".
- ZXPK 21644 p.12 (termik, 2014): on a v3.2 the BIOS speed test in the hi-res menu "скорость 1 к 1 показывает"; the
  test is calibrated against the 12 MHz crystal (solegstar: "расчет в данном тесте идет относительно кварца 12МГц").
  So a v3.2 reads 1.00 = 3 MHz.
- ZXPK 25719 p.28 (juka1868, 2016): a v3.x with turbo shows "1.65 во встроенном тесте" = 6 MHz less the v3 turbo
  waits, which fits the same 2/3-wait rule as in Spectrum mode (6.2 of design.md: RAM-heavy 1.17-1.5x, ROM 2x).

### 1.2 v5.03-5.06: ZQ3 / 4 (ZQ3 / 2 in turbo); 12 MHz is never the CPU clock — H

Evidence chain (NET506):

| Part | Pins | Function |
|:--|:--|:--|
| DD31 (ЛН1) | 8 = `XMHZ` | oscillator of ZQ3 (R23, R24, C12) |
| DD27 (ЛН1) | 4 = `12MHZ` (ZQ1), 5/8 = `ZX14MHZ`, 6 = `NET00004` (ZQ2 14 MHz) | the two video crystals |
| DD25 (74257), 1 = S = `/80DS` | 2 1A = `XMHZ`, 3 1B = `ZX14MHZ`, 4 1Y = `TCPU`; 5 2A = `XMHZ`, 6 2B = `NET00004`, 7 2Y = `TRAM`; 14/13 4A/4B = `NET00004`, 12 4Y = `V14` | 80DS = 1 -> S = 0 -> A inputs: **TCPU = TRAM = XMHZ** |
| DD34 (74257), 1 = S = `80DS` | 2 1A = `V14`, 3 1B = `12MHZ`, 4 1Y = `F0`; 5 2A = `NET00016` (DD7 QA = F0/2), 6 2B = `F0`, 7 2Y = `T_IR10` | 80DS = 1: **F0 = 12 MHz**, shifter clock `T_IR10` = 12 MHz (7 MHz = F0/2 in Spectrum mode) |
| DD2:1, DD9:1, DD2:2 | DD2.3 CLK = `TCPU`, DD2.1 /CLR = `/TURBO`, DD9.1 = `TCPU`, DD2 pins 8/9 = `/F2T`/`F2T` | `F2T` = TCPU / 4; with `/TURBO` = 0 DD2:1 is held clear and `F2T` = TCPU / 2 |
| DD22 | `F2CPU` = `F2T` -> Z80 (DD24) pin 6 | |

`12MHZ` reaches only DD34.3, DD27 and the bus X1.54. It never reaches DD25 or DD2. So in DS80:

| ZQ3 | Normal | Turbo |
|:--|:--|:--|
| 16 MHz | 4 MHz | 8 MHz |
| 18 MHz | 4.5 MHz | 9 MHz |
| **20 MHz (v5.06 as built, see below)** | **5 MHz** | **10 MHz** |
| 24 MHz | 6 MHz | 12 MHz |

Which ZQ3 frequency:
- ALB506 BOM: "ZQ3 КВАРЦ 16-20MHZ", **C12 = 27 pF**, R23 = 680, R24 = 510. The ALB50 table (PROF5-01) gives
  C12 27 pF / R 510 for **20 MHz** (16 MHz: 47 pF / 750; 18: 33 pF / 680; 24: 18 pF / 470). So the 5.06 kit is
  built for 20 MHz — M (the crystal is the builder's choice within 16-20).
- ALB506 BOM: DD24 = **Z84C020** (20 MHz CMOS Z80), all logic 1533 (ALS). The CPU is not the limit.
- ZXPK 21644 p.11-12: solegstar "в расширенном экране подключается третий кварц, на 20МГц"; "кварц ZQ3 надеюсь
  20MHz?"; Vadim ran 24 MHz and hit a too-short /IORQ for the keyboard controller (fixed by a one-shot on the
  peripheral board); ZXPK 14599: "Сейчас 3-й кварц стоит на 20 мгц, работает стабильно".
- Measurement (ZXPK 21644 p.12, termik, a real 5.06 at 20 MHz, BIOS test in the hi-res menu, scale = 3 MHz):
  **1.50 normal, 2.45 turbo**. Vadim: "1.5 это примерно соотв 4,5Мгц если вейтов нет и 5 если они есть". So the
  nominal 5 / 10 MHz lose about 10 % (normal) and 26 % (turbo) to waits on that test (section 3).

### 1.3 Which boards select ZQ3 by 80DS — H for 5.03+, M for older

- ALB50 sheet 1А as drawn in our text copy: D25 `SE` = `80DS`. But MDESK compared a 5.04 board with the paper
  album (ZXPK 14599 p.39, item 6): "5.04 - DD25-1 - /80DS, альбом - DD25-1 - /ONOFF", with the KP11 halves swapped.
- The album text (profi50-p05) still describes the /ONOFF version: "Мультиплексор DD25 позволяет ЦП работать в двух
  подрежимах ... 3.5 Mhz (ONOFF = 0) ... максимальной для данной конфигурации (ONOFF = 1)".
- Kondor's V5.02 notes (ZXPK 14599 p.43, item 4): "В 3-х кварцевом режиме ... рекомендуется отсоединить DD25/1 и
  соединить его с DD30/12" (DD30.12 is the `80DS` net in NET506).
- ZXPK 11582 (Lexx!/Vadim): "В версии 5.02 при отжатой кнопке проц работает от 3-го кварца ... В 5.03 с третьего
  кварца частота идёт только если включен расширенный экран, если включен экран спектрума то на проце 7 или 3.5Мгц".

So: **5.03, 5.04, 5.05, 5.06**: ZQ3 only in DS80 (H). **5.0-5.02 unmodified** (album wiring): ZQ3 whenever the CP/M
(ONOFF) button is released, in both rasters, and 3.5 / 7 MHz with it pressed (M). The emulator's v5 is a 5.06,
so the 5.03+ rule applies.

### 1.4 v5.0 / 5.01 minimal (two-crystal) build — M

ALB50 p.6: without DD25 (jumpers instead) and without DD31 the board works "двумя тактовыми генераторами, как и в
предыдущих версиях"; CH50 5.01 I.1: "не устанавливая DD25, соединить DD25/12 с DD25/13" (V14 = 14 MHz). The other
jumpers are "указанные на схеме" and not in our copies. DD25's inputs carry only `XMHZ`, `ZX14MHZ` and the 14 MHz
net, never `12MHZ`, so without DD31 the CPU can only get 14 MHz: **3.5 MHz, 7 MHz turbo, in DS80 too** (M; the jumper
pattern itself is not drawn). The V5.02 note "При компоновке компьютера с двумя кварцами в режиме большого растра
512х240 (СР/М) не отключать турбо-режим" fits a CPU that does not speed up in hi-res on such boards. Settle with a
photo of a two-crystal board's DD25 jumpers.

---

## 2. The DS80 frame

### 2.1 What runs the generator — H

- v3.2: U5/U6/U7 count the master clock from U15 through U4 (one tick = 16 master clocks), MDESK3 List 1.
- v5.06: DD53 (h counter) CLK = `TCS` = DD5.4 = NOT `NET00022` = NOT DD7 QD; DD7 counts `F0`. So one tick =
  16 F0 clocks (NET506). In DS80 F0 = 12 MHz: **one tick = 1.3333 us** on both boards.
- The PROM's upper 1K (A10 = 80DS) is the DS80 raster (cross-check.md 4.1, DEC).

### 2.2 Decode (DEC, `frames/ds80frames-output.txt`)

Line load 63 on v3.2, 61 on v5 boards with the MISTAK52 / DD53 fix (cross-check.md 4.1).

| PROM | Board | Line | Lines | Frame | Rate | INT -> first paper tick | INT length (DA3 model) |
|:--|:--|:--|:--|:--|:--|:--|:--|
| `0A1DFAFD` (3.2 original, emulator v3 default) | v3 | 48 ticks = 64.00 us | **320** | 15360 ticks = 20.480 ms | 48.83 Hz | 3844 ticks | 12 ticks |
| `15E9B638` (SAMX6) | v3 | 48 ticks | 312 | 14976 ticks = 19.968 ms | 50.08 Hz | 3460 ticks | 12 ticks |
| `FB0579B6` | v3 | 48 ticks | 320 | 15360 ticks | 48.83 Hz | 770 ticks | 10 ticks |
| `D2D4A7C8` ("v503", emulator v5 default) | v5 | 48 ticks | 312 | 14976 ticks | 50.08 Hz | 2690 ticks | 10 ticks |
| `57D728AD` (SAMX12) | v5 | 48 ticks | 312 | 14976 ticks | 50.08 Hz | 2690 ticks | 10 ticks |

Every DS80 raster has 240 paper lines; the paper fetch window (FLD1) is 32 ticks of the 48-tick line (v3: PROM D2
AND /DA5, plus the one-tick pipeline lead; v5: PROM D2 registered in DD44). The vertical runs are
"P240 b16 B7 V17 B15 b17" (312-line PROMs) or "P240 b16 B15 V17 B15 b17" (0A1D, 320 lines).

Notes:
- The README / design.md 5.3 figure "46 ticks on the v5 ones" is the old load-63 decode. With the DD53 fix the v5
  DS80 line is 48 ticks = 64.00 us like every other raster (the album-wiring decode gives 46 ticks = 61.33 us, which
  is off-PAL and belongs only to an unmodified 1994 board with the printed PROM).
- `D2D4A7C8` and `57D728AD` differ only in the Spectrum half; the DS80 halves decode the same.
- **0A1DFAFD has 320 lines in DS80 but 312 in Spectrum mode**: the v3 default changes frame rate when hi-res is on.
- INT -> paper: within one tick (DEC 4.1 note); the v5 output register DD44 adds one tick to the picture.

### 2.3 In CPU T — H for the tick counts, the conversion follows from 1.x

T per tick = 16 x f_CPU / 12 MHz.

| Board, PROM, CPU | T/tick | Line | Frame | INT -> paper | INT length |
|:--|:--|:--|:--|:--|:--|
| v3, 0A1DFAFD, 3 MHz | 4 | 192 | **61440** | 15376 | 48 (model) |
| v3, 0A1DFAFD, 6 MHz turbo | 8 | 384 | 122880 | 30752 | 96 |
| v3, SAMX6, 3 MHz | 4 | 192 | 59904 | 13840 | 48 |
| v3, FB0579B6, 3 MHz | 4 | 192 | 61440 | 3080 | 40 |
| v5, D2D4A7C8, ZQ3 16 MHz (4 MHz) | 5 1/3 | 256 | 79872 | 14346.7 | <= 53.3 |
| v5, D2D4A7C8, **ZQ3 20 MHz (5 MHz)** | 6 2/3 | **320** | **99840** | **17933.3** | **<= 66.7** |
| v5, D2D4A7C8, ZQ3 24 MHz (6 MHz) | 8 | 384 | 119808 | 21520 | <= 80 |
| v5, D2D4A7C8, ZQ3 20 MHz turbo (10 MHz) | 13 1/3 | 640 | 199680 | 35866.7 | <= 133.3 |

On v5 the CPU clock and the raster are separate crystals (XMHZ vs ZQ1), so the T counts are exact only for an ideal
crystal; the phase between them drifts on a real board (H). For the emulator a frame of 312 x 48 ticks at
(16/12) x f_CPU T per tick is exact enough; for 16 and 20 MHz the line is a whole number of T (256, 320), and the
frame too (79872, 99840); INT -> paper needs a fractional T (round, or keep the raster in 1/3-T units).

### 2.4 INT circuit — H (gates), M (length)

- v3.2 (MDESK3 List 1): /INT = U42:A OR(PROM D3 node with C14 3300 pF, U27:B /Q); U27:B D = that node, CLK = DA3,
  /S and /R on +5 V. INT starts when D3 falls and ends at the next DA3 rising edge. Same in DS80.
- v5.06 (NET506): /INT = R15 (680) from NET00043 = DD33:1 OR(`BL_INT`, NET00051); NET00051 = PROM D3 (DD37.15, C15
  3300 pF); DD29:2 D = NET00051, CLK = DA3, **/CLR (pin 13) = NET00028 = DD33:3 OR(`/IORQGE`, `/M1`)**, /Q (pin 8)
  = `BL_INT`. So on v5 **INT also ends at the interrupt acknowledge** (IORQ and M1 low), or at the next DA3 edge,
  whichever is first. That is CH50 5.01 I.3 ("Схема прерывания"). Karabas-Pro RTL models the same (`bl_int <= '1'
  when INTA = '0'`).
- The DA3-edge length is a model (DEC 4.3: two parts not fully traced on v3); it is an upper bound on v5.

### 2.5 INT rate — Q6 — H for the PROMs decoded

CP/M sees the DS80 frame rate: **50.08 Hz** on v5 (D2D4A7C8, SAMX12) and with SAMX6 on v3; **48.83 Hz** with the
v3.2 original 0A1DFAFD and with FB0579B6. INT keeps its source and circuit (only the PROM half changes), so there
is one INT per frame in both rasters. The keyboard on both boards is a matrix read by `IN #FE` (v5.06 with the PS/2
controller on the peripheral board aside); nothing on the processor boards ties it to the raster, so a CP/M BIOS
that scans on INT scans at 48.83 or 50.08 Hz. In CPU T the INT period grows on v5 (99840 T at 5 MHz).

---

## 3. CPU waits in DS80

### 3.1 v3.2 — H (same circuit as Spectrum mode)

The arbiter U27:A/U28:A, READYT and U16 run from F2/F2T, which are the U4 master divided down, whatever crystal U15
selects. Nothing in that logic sees 80DS (MDESK3 List 5, research-profi-v3-turbo-floatbus.md A3). So:
- **normal (3 MHz): no waits** (U16 2A = +5 V to /WAIT);
- **turbo (6 MHz): the Spectrum-mode rule**, RAM M-cycles 2 waits when T1 starts on an even turbo T, 3 when odd;
  ROM, I/O, INTA, refresh never wait. The video takes 2 of the 4 DRAM cycles of a tick in DS80 too (two fetches per
  tick: STBP into U9, STBA into U10), so the slot arithmetic is unchanged.
- Supported by the 1.65 reading on a v3.x (1.2 above).

### 3.2 v5.06 — M (model), H for the gate list

Traced facts (NET506, research-profi-v5-wait.md 1):
- The DRAM ring (DD49:1, DD62) and the slot owner (DD21) run on `TRAM` = XMHZ; the CPU on TCPU = XMHZ (same
  polarity in DS80; inverted in Spectrum mode).
- Video requests: `/STBI0` = F0 | QA | QB | QC (DD4, DD15) every 8 F0 = **666.7 ns**, two per tick, from the 12 MHz
  domain, so asynchronous to the ring.
- DD14:2 /S = NET00196 = `/PS` & (FLD1 | NET00194), NET00194 = `BCMR` & `80DS` (R5, VD1); `BCMR` = DD35 pin 15 =
  Q6 of the `#7FFD` latch (D6 = DC5): **`#7FFD` bit 5**. In DS80 with bit 5 = 1 the video requests run the whole line
  (only the /PS one-shot blocks them); with bit 5 = 0, only in the 32-tick paper window of the 240 paper lines.
  (On v5 bit 5 locks `#7FFD` only while ROM is on: DD10:1 = BCMR AND /NOROM.)
- `/READY` SB8 term = /TURBO & /80DS = 0 in DS80: SB8 never removes waits in hi-res.
- The ROM-read one-shot NET00122 (C7 82 pF, R11 2 kΩ, "TI = 200nS") pulls /READY for ~160-200 ns at every ROM read.
  At 5 MHz (T = 200 ns) and 10 MHz that is no longer marginal as it is at 3.5 MHz.

Model (SIM, `waitmodel-ds80/`): `v5waitsim.py` with three clocks (XMHZ for TRAM/TCPU, 12 MHz F0 with a free phase),
4.17 ns steps, parts of the 5.06 BOM (Z84C0020: MREQ delay ~38 ns, WAIT setup ~21 ns; 1533 flip-flops ~12.5 ns),
and the ROM one-shot (180 ns). It reproduces the Spectrum-mode result of the original model with the original LS
parameters (LD A,(HL) loop 8 T on 3 of 4 phases, 7 T on one, border 7 T).

Results (`waitmodel-ds80/ds80summary-output.txt`, `ds80rule-output.txt` (5 MHz), `ds80rule-turbo-output.txt`
(10 MHz); waits in CPU T at that speed; "requests on" = inside the fetch window):

| ZQ3, speed | RAM access, no requests | RAM access, requests on | ROM read (one-shot) |
|:--|:--|:--|:--|
| 16 MHz, 4 MHz | 0 | 0 or 1 (mean 0.4-0.6 in a M1+read loop) | 0 |
| **20 MHz, 5 MHz** | **0** | **0 or 1**, mean 0.20 per RAM M-cycle over random mixes | **1** |
| 24 MHz, 6 MHz | 0 | 0 or 1 | 1 |
| 16 MHz turbo, 8 MHz | 1 | 1-3 | 1 |
| **20 MHz turbo, 10 MHz** | **1** | **1-3**, mean 1.48 | **2** |
| 24 MHz turbo, 12 MHz | 1 | 1-3 | 2 |

Where the wait falls (5 MHz, T = 200 ns; 1536 RAM M-cycles over random instruction mixes): a RAM M-cycle waits 1 T
exactly when its T1 starts from **130 ns before to 40 ns after a video request edge** (/STBI0 falling); never
otherwise. At 10 MHz (T = 100 ns): 1 wait always (the ring restarts from idle on the CPU's request), **3** when T1
starts 90-0 ns before a request edge, **2** when it starts 0-90 ns after it, else 1.

Check against the real 5.06 (termik, 20 MHz, BIOS speed test, scale 3 MHz): 1.50 normal, 2.45 turbo. A ROM loop such
as `DEC BC / LD A,B / OR C / JR NZ` (26 T, 5 ROM reads) gives 1.40 / 2.41 with these rules, the same loop in RAM
1.63 / 2.67. The test's loop is not identified (it runs from the BIOS ROM or from RAM), so the measurement is
consistent with the rules but does not prove them.

### 3.3 Sensitivity — read before trusting the turbo / 24 MHz figures

`ds80sens-output.txt` (older LS-like delays, no ROM pulse): with no video requests an access waits 0 at 16 MHz for
all but very slow parts, but **1 at 20-24 MHz once the WAIT setup exceeds ~40 ns or the MREQ delay ~60 ns**. The
ring restarts from idle on the CPU's request and needs ~1.5 TRAM clocks plus three flip-flop delays before the CPU's
CAS; at 20 MHz that is close to the T1-T2 window. With the Z84C0020 and 1533 parts of the 5.06 BOM the border is
wait-free at 16, 20 and 24 MHz in the model. The 2.45 turbo measurement (26 % loss) shows that real turbo DS80 costs
well over one wait per access on average.

---

## 4. Floating bus in DS80

### 4.1 v3.2 — exists; M for which byte

MDESK3 List 5 (crop `materials/v3-turbo-crops/cl4-d.png`), List 1:
- Y = U35:B NAND(U35:A NOT `SI4`, `80DS`); /OEPIK1 = U34:D NAND(Y, FLD1) enables U9 (STBP latch);
  /OEPIK2 = U34:A NAND(NOT Y, FLD1) enables U10 (STBA latch). `SI4` = U4 Q3 (List 1, `p3/z1.png`).
- In DS80, Y = SI4: **U9 drives the P bus in the second half of each tick (Q3 = 1, masters 8-15), U10 in the first
  half (masters 0-7)**, while FLD1 = 1. U57 (8 x 820 Ω) joins P to the CPU data bus as in Spectrum mode. Outside FLD1
  nothing drives P: the 10 kΩ pull-ups give `#FF`.
- So a floating read (an `IN` that no device claims, e.g. `#FF` outside DOS) in DS80 returns a hi-res byte: the
  STBA-fetched byte (U10) when the read strobe (T3 falling edge, master 2 or 6 of the tick at 3 MHz) is in the
  first half, the STBP-fetched byte (U9) when it is in the second half (masters 10, 14). At 3 MHz: of the 4 T of a
  tick, T0/T1 read U10, T2/T3 read U9 (H for the gating, M for the phase: same one-tick lead as in Spectrum mode).
- Which screen page each latch holds (the DS80 address mux U15 3Y/4Y -> DA20/DA21) is not traced: O. In DS80 the
  attribute latch U8 still never reaches DC.
- Emulator: `byte = FLD1 ? (tick-half 0 ? second fetch of the next 16-pixel cell : first fetch) : #FF`; until the
  page order is traced, return the pixel byte shown in that half-cell, one tick ahead (M).

### 4.2 v5.06 — none, `#FF` — M

The v5 has no pixel-latch-to-CPU-bus path in either raster (design.md 4.4, research-profi-v5-open-items.md): the
video latches DD66/DD68 (pixels) and DD65 (attributes) feed only the shifter / palette. DS80 adds nothing: `#FF`.

---

## 5. AY clock

### 5.1 v3.2: master / 8 — 1.75 MHz Spectrum, **1.5 MHz in DS80** — H

- MDESK3 List 1 (`p3/z2.png`): `CLCAY` = U39:C (LL1, both inputs tied) from U4 **Q2** = master / 8.
- MDESK3 Interface list 3: `CLCAY` on SYS_BUS A10; Interface list 2: AY-3-8912 U6.1 pin 15 CLK = `CLCAY`
  (and the AY-3-8910 footprint U6.2 pin 22 = `CLCAY`).
- Master = 14 MHz / 12 MHz by 80DS -> 1.75 / 1.5 MHz.

### 5.2 v5.0-5.05: F0 / 8 — 1.75 / **1.5 MHz** — H

- ALB50 p.5: "С выхода м/с DD5.1 снимается сигнал CLCAY"; PROF5-01: D5 pin 1 <- D7 Q2, pin 2 = CLCAY.
- NET506: DD5.1 = `NET00024` = DD7.12 (QC of the F0 counter), DD5.2 = `NET00046` -> SB7.3. F0 = 14 / 12 MHz by 80DS.
- SG506 item 16: "Раньше в спектрум-режиме частота AY была равна 1,75 МГц, а в расширенном экране 1,5 МГц, из-за чего
  мелодии в расширенном экране играли более низким тоном."

### 5.3 v5.06: jumper SB7, default 1.75 / **1.5 MHz** — H

- NET506: SB7.2 = `CLCAY` (X1.10 / X1A.10, to the peripheral board); SB7.3 = `NET00046` (F0 / 8, "CLCAY OLD");
  SB7.1 = `NET00039` = DD70.12, DD70 (ИЕ10) counts `ZX14MHZ` -> 14 / 8 = 1.75 MHz always ("CLCAY NEW").
- ALB506 p.11: "SB7 – “AY CLOCK” ... CLCAY NEW – ... неизменной 1,75МГц, ... CLCAY OLD - ... 1,75МГц ... или расширенного
  экрана Profi 512х240 (1,5МГц) — старый режим работы. **По умолчанию должна быть в положении “CLCAY OLD”**."
- Turbo does not touch CLCAY on either board (it is a video-counter output).

---

## 6. Other emulators (cross-check only)

| Source | DS80 CPU clock | DS80 frame | DS80 INT | AY in DS80 |
|:--|:--|:--|:--|:--|
| UnrealSpeccy (`io.cpp`, `unreal.ini`) | unchanged (3.5 MHz) | unchanged (PRESET.PROFI 69888, 224) | unchanged (28 T) | unchanged |
| ZXMAK2 (`profi-renderer.cs`) | unchanged | line 192 T for the 512x240 renderer but `c_frameTactCount = 69888`, with the comment "59904 for profi mode (312x192)" (= SAMX6 DS80 at 3 MHz) | 39 T "needs approve" | unchanged (AY8910 device) |
| Xpeccy (`profi.c`) | unchanged | video mode only (`VID_PRF_MC`) | unchanged | unchanged |
| xpeccy-plus (`profi.c`, `machines-reference.md`) | unchanged (3.5 MHz, turbo x2) | unchanged | unchanged | AY / YM 1.75 |
| pico-spec (`ports.cpp`) | no clock change found | - | - | - |
| Karabas-Pro RTL (a clone) | `clk_bus` 24 MHz in DS80 (28 MHz otherwise); CPU = bus / 8 = **3 MHz** (/4, /2 in turbo) | 768 dots x 312 lines at 12 MHz = 64 us x 312 | `v = 257, h > 656` to line end, ended by INTA (`bl_int`) | TurboSound `I_ENA = ena_div16` of `clk_bus` = **1.5 MHz** |

Only Karabas-Pro changes anything, and it follows a two-crystal (v3-like) design: 3 MHz CPU and 1.5 MHz AY.

---

## 7. What would settle the open points

| Open point | What settles it |
|:--|:--|
| v5 DS80 wait rule (M) | the speed test behind termik's 1.50 / 2.45 (find the BIOS routine, run it on the emulator with the rule, compare); or Tact Meter / TEST 4.30 in a hi-res context; or a scope on Z80 pin 24 against /STBI0 |
| ZQ3 actually fitted on a given board | the crystal marking; the 5.06 BOM's C12 = 27 pF says 20 MHz |
| v3 DS80 floating byte order | trace U15 3Y/4Y (DA20/DA21) and the STBP/STBA page order on List 1 / 5 |
| v3 INT length | finish the INT trace (DEC 4.3) |
| v5.0/5.01 two-crystal CPU clock in DS80 | DD25 jumper pattern (photo or the paper album's sheet 1К) |
| 5.0-5.02 ONOFF-selected ZQ3 | only matters if a pre-5.03 board is emulated |

---

## Emulator rules

### v3.2

| Item | Rule | Conf. |
|:--|:--|:--|
| CPU clock in DS80 | **3 MHz** (12 / 4); **6 MHz** in turbo. Switch with `#DFFD` bit 7. No ini value | H |
| Frame | PROM upper half, 48 ticks x 4 T = **192 T** line. Default PROM 0A1DFAFD: **320 lines, 61440 T**, 48.83 Hz. SAMX6: 312 lines, 59904 T. FB0579B6: 320 lines, 61440 T. Turbo: all T doubled | H |
| INT | 0A1DFAFD: INT -> first paper **15376 T**; length **48 T** (DA3 model; no INTA reset on v3). SAMX6: 13840 T / 48 T. FB0579B6: 3080 T / 40 T | H position (+-1 tick), M length |
| Paper fetch window | 240 lines x 32 ticks (128 T) per 192-T line, one tick lead | H |
| Waits | normal: **none**. Turbo: the Spectrum-mode v3 rule (RAM M-cycles 2 waits on an even turbo T, 3 on odd; ROM/IO/INTA/refresh free), in 6 MHz T | H (logic), M (2/3 phase, as in Spectrum mode) |
| Floating bus | exists: in FLD1, first half of each tick (T 0-1 of the 4-T tick at 3 MHz) the U10 (second-fetch) byte, second half (T 2-3) the U9 (first-fetch) byte, one tick ahead of the picture; `#FF` outside FLD1 | H gating, M byte/phase, O page order |
| AY clock | **1.5 MHz** in DS80, 1.75 MHz otherwise; turbo no effect | H |

### v5.06 (and 5.03-5.05)

| Item | Rule | Conf. |
|:--|:--|:--|
| CPU clock in DS80 | **ZQ3 / 4**, **ZQ3 / 2** in turbo; Spectrum mode keeps 3.5 / 7 MHz. Add `[PROFI] ZQ3MHz=` (16-24, **default 20**: the 5.06 BOM's C12 = 27 pF and the forum's builds; 16 the safe low end). Clock changes on the `#DFFD` bit 7 write | H (selection), M (default 20) |
| Frame | D2D4A7C8 / SAMX12 upper half: 312 lines x 48 ticks of 1.3333 us = 19.968 ms (50.08 Hz). In T: 16 x f_CPU / 12 T per tick: **20 MHz: 320 T line, 99840 T frame**; 16 MHz: 256 / 79872; 24 MHz: 384 / 119808; turbo doubles | H (ticks), H (conversion) |
| INT | INT -> first paper **2690 ticks** (20 MHz: 17933.3 T; 16 MHz: 14346.7 T); length: until INTA or **10 ticks** (66.7 T at 5 MHz), whichever first | H position (+-1 tick), H INTA end, M 10-tick bound |
| Paper fetch window | 240 lines x 32 ticks (42.67 us) per line; video requests every 666.7 ns there; with `#7FFD` bit 5 = 1 the requests run the whole line | H |
| Waits | v5.06 parts, model (`waitmodel-ds80/`). Video request edges: every half tick (8 F0 clocks = 0.6667 us = 3 1/3 T at 5 MHz) inside the fetch window (240 lines x 32 ticks; whole line when `#7FFD` bit 5 = 1). **Normal (ZQ3/4):** a RAM M-cycle (M1, read, write; not refresh, I/O, INTA) waits 1 T if its T1 starts from 0.65 T before to 0.2 T after a request edge (at 5 MHz; in ns: -130..+40), else 0; ROM reads (M1, read) 1 T (the 200 ns one-shot). **Turbo (ZQ3/2):** every RAM M-cycle 1 T, plus 2 more if T1 starts up to 90 ns before a request edge, plus 1 more if up to 90 ns after it; ROM reads 2 T. Simple average if a slot rule is too costly: normal 0.25 T per RAM access inside the window, 0 outside, 1 per ROM read; turbo 1.5 per RAM access inside, 1 outside, 2 per ROM read. SB8 has no effect in DS80 | M |
| Floating bus | none: `#FF` | M |
| AY clock | SB7 "CLCAY OLD" (default): **1.5 MHz** in DS80, 1.75 MHz otherwise; "CLCAY NEW": 1.75 MHz always. Suggest `[PROFI] AyClock=old|new`, default old | H |
| 5.0-5.02 unmodified | ZQ3 chosen by the CP/M (ONOFF) button, not by 80DS, in both rasters | M |
| 5.0/5.01 two-crystal build | DS80 CPU stays 3.5 / 7 MHz; DS80 raster and AY as above | M |

Files in this folder: `frames/ds80frames.py` (+ output), `waitmodel-ds80/ds80waitsim.py`, `ds80rule.py`,
`ds80summary.py`, `ds80sens.py` (+ outputs), `p3/` (MDESK v3.2 renders and crops, `crop.py`).
