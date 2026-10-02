# Profi v3 / v5: every difference, checked against independent sources

**Date:** 2026-10-01 · part of [README.md](README.md)

xpeccy-plus split its Profi into two machines in [b81a23d8](https://github.com/dotkoval/xpeccy-plus/commit/b81a23d8)
and the commits after it. **None of its claims is taken on trust here.** Each one is checked against the other
emulators, the clone RTL and the hardware documents, and the consensus wins (project rule: a hardware fact needs
agreeing sources, not one emulator).

## 0. Sources

| Tag | Source | Has v3 and v5 apart? | Kind |
|:--|:--|:--|:--|
| XP+ | xpeccy-plus `src/libxpeccy/hardware/profi.c`, `res/machines/profi{,3}.conf`, `docs/machines-reference.md` (HEAD `84e627d3`) | yes | emulator, the claims under test; its board photos, PROM decodes and timing-test output were never published |
| UNR | UnrealSpeccy 0.39 and its forks (zx-evo-unreal, Unreal_NS, NedoOS `us`) | no, one `MM_PROFI`; a changelog line ties the extended ports to "v5.xx boards" | emulator |
| ZXM2 | ZXMAK2 `PROFI3XX.VMZ` ("PROFI+ 512 [V3.XX]"), `PROFI5XX.VMZ` ("PROFI+ 1024 [V5.XX]"), `Hardware/Profi/*.cs`; kozynax fork the same | yes | emulator |
| XPC | original Xpeccy `src/libxpeccy/hardware/profi.c` | no | emulator, parent of XP+ |
| KAR | Karabas-Pro RTL and dev manual v1.01 | v5 only ("repeats Profi 5.06") | FPGA clone, not an original board |
| PICO | pico-spec `src/Ports.cpp` | no; follows Karabas | emulator |
| BC | Black_Cat port table `zx-evo-docs/ZX/zx-ports-full-table.txt`, column 9 "Profi-1 (v3.x)" | v3 only | hardware port table; a missing row is weak evidence |
| S4E | [speccy4ever Profi page](https://speccy4ever.speccy.org/_PR.htm): factory ROMs, sync PROMs | yes | primary hardware material |
| MAN | the board manuals from the same page: [v3.2 manual](https://speccy4ever.speccy.org/doc/ProfiV32mn.pdf), [v3.2 interface schematic](https://speccy4ever.speccy.org/doc/ProfiV32il.pdf), [v3.2 main schematic](https://speccy4ever.speccy.org/doc/Profiv32cl.pdf), [v4.01](https://speccy4ever.speccy.org/doc/profi401.pdf), [v5.0](https://speccy4ever.speccy.org/doc/profi50.pdf), [v5.0x change notes](https://speccy4ever.speccy.org/doc/profi50ch.pdf) | yes | primary: prose, port tables, schematics, printed PROM dumps; scanned, read by OCR and by eye; "sch." = read off a schematic, less certain than prose |
| ROM | what the factory BIOS code itself does ([roms.md](roms.md) section 1) | yes | primary |
| WEB | K. Gromov's article on the Profi sync generators ([ZX-Ревю 1996 №1-2](https://zxpress.ru/book_articles.php?id=502)), Kondor's fix list MISTAK52 and the v5.06 album and netlist from [zx-pk.ru](https://zx-pk.ru/forums/102-profi.html), the [Profi 5.03 replica](https://github.com/solegstar/Profi-5.03) | yes | a hardware author's account, the maker's fix list, schematics; kept in the materials folder outside the repository (README.md) |

MAME's `profi` is the Scorpion driver with Profi ROMs (`MACHINE_NOT_WORKING`). Zero, Spectral, zxsp, ZX-M8XXX,
Murmulator and UnrealSpeccyP have no Profi. All of them are counted as "no data".

Verdicts: **confirmed** (independent sources agree), **disputed** (sources disagree; the design keeps it
switchable), **XP+ only** (nothing else has it), **measured, XP+ only** (XP+ says a real board was measured, but the
material is not available).

## 1. Ports and devices

| # | Claim | Verdict | Evidence | unreal-ng today |
|:--|:--|:--|:--|:--|
| P1 | `#7FFD` / `#DFFD` are the same on both boards, bit for bit | **confirmed** | BC v3 decode = ZXM2 3XX/5XX shared code = UNR = KAR | same on the one model |
| P2 | `#8000` = page 6 needs `DFFD.6` **and** `7FFD.3` | **XP+ / XPC only** | ZXM2, UNR and KAR use `DFFD.6` alone | `DFFD.6` alone (kept) |
| P3 | the AY decodes A13, so `IN (#DFFD)` does not read the AY | **confirmed** | BC v3 (`101x...0x` / `111x...0x`), XP+ `0xE002` | not modeled: AY on `(port & 0xC002)` |
| P4 | palette (`#xx7E`, A0 = A7 = 0, DS80 on) exists only on v5 | **confirmed** | MAN v3.2 p2: hi-res is "монохромная 512 на 240"; MAN v5.0 p2, p6: "16-ю цветами из палитры 256 цветов", palette RAM DD39/DD40/DD48, without them "цветовая гамма PROFI+ V4.02"; ZXM2 5XX has it, 3XX not; BC v3 has no palette row; v3 BIOSes never write it, Bios 2.0 does (ROM) | on every Profi |
| P4a | v3 hi-res is monochrome (no attribute page) | **confirmed** | MAN v3.2 p2; ZXM2 3XX draws 512x240 black and white | a global `ProfiMonochrome` ini flag |
| P4b | palette access needs CP/M mode: "80DS=1 и BLOCK=1" | **MAN only, open** | MAN v5.0 p11: "В режиме CP/M одновременно выставленные сигналы 80DS=1 и BLOCK=1 разрешают доступ к регистрам палитры, расположенным по адресам 0FEH"; every emulator gates on DS80 alone; what BLOCK is (a `#7FFD` bit 5 alias?) is not stated | DS80 alone |
| P4c | `#7FFD` bit 3 selects the hi-res screen page in CP/M mode only on v5 | **MAN only** | MAN v5.0 p10-11 (SEG06/SEG3A or SEG04/SEG38 by bit 3); MAN v3.2 p6 has pixels in segment 06 only | as v5 |
| P5 | `#FE` read bit 7 is always 1 | **v3: consistent; v5: contradicted** | v5: ZXM2 5XX, KAR RTL and manual, and PICO return a bit derived from the palette (the formula differs per source); v3: ZXM2 3XX reads 1 | KAR GX0 in DS80, else 1 |
| P6 | v5 extended map with CP/M and ROM14: FDC `#83/#A3/#C3/#E3`, system `#3F`, RTC `#9F/#BF/#DF/#FF` | **confirmed** | UNR, ZXM2, XPC, KAR, KAR manual p.22, PICO | yes, on every Profi |
| P7 | v3 has only one CP/M map (`#1F..#7F` + `#BF`, whatever ROM14 is) | **confirmed by the v3.2 PROM** | the v3.2 port decoder PROM (dump) has ADR15 where v4/v5 have ROM14, and its CP/M map is the same for both A15 values ([decoder-prom.md](decoder-prom.md)); MAN: the extended periphery needs "контроллер v4.0 и выше"; XP+ and the UNR changelog agree; ZXM2 (extended map on V3.XX) is the outlier | v5 map on every Profi |
| P8 | v5: in the SYS ROM (DOS latch on, ROM14 = 0) the extended map is active | **contradicted by the v5 PROM** | the extended map needs CP/M on, ROM14 = 1 and TR-DOS off ([decoder-prom.md](decoder-prom.md)); KAR, the KAR manual and PICO describe the clone, not the board | no (right) |
| P9 | the CP/M map holds with the DOS latch on | **confirmed by both PROMs' wiring** | on both boards CP/M forces the decoder PROM's A2 input to 1, so the TR-DOS latch does not matter in CP/M ([decoder-prom.md](decoder-prom.md) 2); XP+ and UNR are right, KAR and XPC (CP/M map only with DOS off) wrong | the same (right) |
| P10 | RTC, IDE and COM exist only from controller v4.0 on | **confirmed** | MAN v4.01 p22 and v5.0 p10-11: the extended periphery (RTC КР512ВИ1, COM 8251 + 8253, IDE, the Kondor modem, a timer, a control register) sits on `1xxx xx11` addresses with CP/M and ROM14; MAN v3.2 has none of them; BC v3 has neither; ZXM2 and kozynax (both on V3.XX) are the outliers. MAN v4.01 also lists the RTC at `#FFFF/#FFEF` and `#FF/#DF` in Sinclair mode, which v5.0 drops: a v4-only detail, out of scope | both on every Profi (IDE with `[HDD] Scheme=PROFI`) |
| P11 | 8255 on `#1F/#3F/#5F/#7F` (A7 = 0, A1:0 = 11) outside DOS and CP/M; Covox left = `#5F` (port C), right = `#3F` (port B) | **confirmed** | MAN v3.2 interface sheets 6-7 (КР580ВВ55, A0 = ADR5, A1 = ADR6), MAN v5.0 p2 ("плата ЦАП Covox" on the 8255), BC v3 row, UNR "VV55", XPC "BB55", ZXM2 Covox ports; the clones KAR and PICO swap left and right Soundrive-style | Covox `#5F`/`#3F`, write only |
| P12 | joystick on 8255 port A (`IN #1F`) | **confirmed** | MAN v3.2 sch.: PA0-PA4 + PB0 = Kempston joystick, PB = printer data, PC = printer handshake; MAN v5.0 p2: "KEMPSTON JOYSTICK" in software on the 8255; observed `#1F` reads: UNR, XPC, KAR, PICO | the working tree adds `#1F` outside the DOS ports |
| P13 | 8255 at `#87/#A7/#C7/#E7` in the extended map | **confirmed by the v5 PROM** | P1 (via F6 and the ИД4) together with F5 selects the 8255 ([decoder-prom.md](decoder-prom.md)); UNR and XP+ have it; KAR uses those addresses for its own SPI flash | Covox aliases `#C7`/`#A7` |
| P14 | no Covox on `#FB` | **no consensus** | ZXM2: none; UNR: a global option; KAR and PICO: yes | not decoded |
| P15 | Kempston mouse at `#FADF/#FBDF/#FFDF` with CP/M off | **confirmed**, same on both boards | the CP/M gate is in XP+, XPC, KAR and PICO; the extra DOS gate is split | refused under `CF_DOSPORTS` |
| P16 | no floating bus on either board | **v3: contradicted by the manual; v5: only with Kondor's fix 9** (I1..I8 wired to R63, MISTAK52; TEST 4.30 on a 5.06 confirms the `#FF` read) | MAN v3.2 p2: "возможность чтения пиксела экрана во время прямого хода луча по кадру делает эту модель компьютера максимально совместимой с фирменными компьютерами"; ZXM2 reads `#FF` floating on both; UNR, XPC, PICO and XP+ have none; MAN v5.0 lists port `#FF` as readable but says nothing about it | none |
| P17 | AY-3-8910 on v3, YM2149 on v5 | **contradicted** | MAN v3.2 p2 "AY-8912 или AY-8910", MAN v5.0 p2, p5 "AY8910 (AY8912)"; ZXM2: AY on both; KAR's YM is the clone's choice | one AY model for all machines |

## 2. Memory and reset

| # | Claim | Verdict | Evidence | unreal-ng today |
|:--|:--|:--|:--|:--|
| M1 | page = `((DFFD & 7) << 3) \| (7FFD & 7)` | **confirmed** | every source | same |
| M2 | v5: 512K or 1M | **confirmed** | ZXM2, XPC, UNR (1M) | 1M only |
| M3 | v3: 512K | **confirmed** | ZXM2 3XX is 512K (DFFD mask 3) | - |
| M4 | v3: also 256K and 768K, and pages of an unfitted chip row read `#FF` | **XP+ only** | in XP+'s own code `#FF` happens only on the 768K board; 256K and 512K wrap there too | wrap |
| M5 | reset lands in the SYS ROM; ROM order SYS, DOS, 128, 48 | **confirmed** | ZXM2, KAR and PICO hard-wire it; UNR and XPC have it as an option | forced `RM_SYS` |
| M6 | no contention on either board at 3.5 MHz | **v3: confirmed; v5: contradicted** | v3: Gromov ("WAIT ... в старом ПРОФИ ... не использовался"), Poltergeist ("память прозрачная"); v5: the video controller's WAIT on CPU RAM accesses in Spectrum mode (section 4.4); every emulator has none | none |

## 3. ROM content

| # | Claim | Verdict | Evidence |
|:--|:--|:--|:--|
| R1 | v3 boards came with the Kramis BIOS and TR-DOS 5.03, v5 boards with Micco Bios 1.0 / 2.0 and TR-DOS 5.04T | **confirmed** | S4E's own labels ("from Condor 5.04 board", "128Basic+TR-DOS 5.03"), XP+, ZXM2 romsets `PROFI-V03` / `PROFI` |
| R2 | the `profi.rom` xpeccy-plus shipped before was Kramis V0.2 with TR-DOS 5.04T in place of 5.03 | **confirmed** | page CRCs: SYS `CEDBE816` in both, DOS `E212D1E0` vs `C43D717F` |

## 4. Frame and INT

### 4.1 Our own decode of the sync PROMs

The sync PROMs were decoded here from the schematics, without using xpeccy-plus's decode. Sources: the v3.2 main
schematic sheet 1, the v5.0 album text and schematics, the v5.06 P-CAD netlist, KLUG's
[RF5 note](https://speccy4ever.speccy.org/rom/Profi/Pentagon_Fix_RF5.pdf), the
[SAMX6P note](https://speccy4ever.speccy.org/rom/Profi/Pentagon_Fix_SAMX6P.pdf), the Pentagon INT mod photo and the
bit labels in the Profi ROM viewer. The decoder is [tools/machines/profi/syncprom/profisync.py](../../../tools/machines/profi/syncprom/profisync.py) and its full output is
[tools/machines/profi/syncprom/profisync-output.txt](../../../tools/machines/profi/syncprom/profisync-output.txt). It reads the `VR*.ROM` dumps from the
[speccy4ever Profi page](https://speccy4ever.speccy.org/_PR.htm).

How the generator works:

- One horizontal tick is 16 master clocks, which is 4 T at 14 MHz.
- **PROM address:**
  - v3.2: A0-A4 = DA1-DA5.
  - v5: A0 = DA0 & DA1, A1-A4 = DA2-DA5. Confirmed by the v5.06 netlist.
  - Both: A5-A9 = DA10-DA14 (16 lines per PROM row); A10 = 80DS (the lower 1K is the Spectrum raster, the upper
    1K the 512x240 one).
- **Data bits:**

  | Bit | Meaning |
  |:--|:--|
  | D0 | sync |
  | D1 | blank |
  | D2 | paper |
  | D3 | INT source |
  | D4 | vsync, where used |
  | D5 | frame reset |
  | D6 | line load |
  | D7 | line count |

- **INT:** starts when D3 falls and ends at the next DA3 edge.
- **The line load value differs by board.**
  - **63** (all ones) on v3.2, and on the v5.0 album drawing (1994).
  - **61** on v5 boards with Kondor's fix list MISTAK52 for V5.02: "прошивка синхрогенератора 573РФ2 должна быть
    версии "12", a 4 ножка DD53 с +5в переключена на землю". That fix list is quoted in
    [zx-pk thread 17911](https://zx-pk.ru/threads/17911-dorabotki-kompyutera-profi.html), which adds that the DD53
    change also applies to 5.04, 5.05 and 5.06.
  - DD53 is a 74LS161, and its pin 4 is the load input D1. The v5.06 netlist has D0, D2 and D3 on VCC and D1 on GND
    (posted in [zx-pk 21644](https://zx-pk.ru/threads/21644-plata-protsessora-profi-v5-06.html)), and so does the
    [5.03 replica](https://github.com/solegstar/Profi-5.03).

| PROM | Board | Load | Line | Lines | Frame | INT -> first paper | INT length (model) |
|:--|:--|:--|:--|:--|:--|:--|:--|
| `0A1DFAFD` (= `Profi_RF2.BIN`, the original PROM read off a 3.2 board by the MDESK project, 2009) | v3/4 | 63 | 224 T | 312 | 69888 | 12584 T | 36 T |
| `15E9B638` (SAMX6, Kondor's v3/4 PROM) | v3/4 | 63 | 224 T | 312 | 69888 | 12592 T | 44 T |
| `FB0579B6` (file `VR3-5A0AB56B`) | v3/4 | 63 | 224 T | 320 | 71680 | 48 T (INT in the line before the paper) | 44 T |
| `02BB2120`, `BCD770D5` (Pentagon fixes) | v3/4, modified | 63 | 224 T | 320 | 71680 | 17992 / 17968 T | 8-44 T |
| `D2D4A7C8` (read off a Kondor 5.04; signed `Profi+v5.03+`; the 5.03 replica's PROM has the same CRC) | v5 | 61 | 224 T | 312 | 69888 | **14368 T** | 20 T |
| `57D728AD` (SAMX12, "версия 12"; signed `Profi+v5.03+`; the same bytes as SAMX12 posted on zx-pk) | v5 | 61 | 224 T | 312 | 69888 | 16144 T | 4 T (not credible) |
| the PROM printed in the v5.0 album, p.9 (signed "КОНДОР 1994") | v5.0 as drawn | 63 | 228 T | 312 | 71136 | 12816 T | 44 T |
| the same | | 61 | no frame (the counters never reach line 0) | | | | |

Certainty:
- **Firm:** line length, lines per frame, frame length.
- **Internal check:** with the right load value, every PROM gives a 64.00 us line, which is PAL. The earlier
  216 T for the v5 dumps came from decoding them with load 63: a 61.7 us line, off PAL. With load 61, the 512x240
  raster also comes out at 64.00 us.
- **Within one tick (4 T):** INT to paper. The display pipeline may add 0-1 tick.
- **A model only:** INT length. Two parts of the INT circuit are not fully traced, and the v3.2 sheet has D3
  crossed out by hand. Gromov tuned INT on v5 to "8 - 8.6" (us; the article's "мс" is a typo): 28-30 T, which is
  UnrealSpeccy's 28.
- **The album's printed PROM** is an earlier state than the boards. It runs only with load 63, and then gives an
  off-PAL 65.1 us line. Kondor changed the PROM ("version 12") and DD53 together.

### 4.2 Sync PROM generations (Gromov, ZX-Ревю 1996)

[K. Gromov](https://zxpress.ru/book_articles.php?id=502), who reworked the Profi sync generators, lists these
generations:

- **The original v3.xx PROM:** "самое мрачное творение"; a black frame around the raster.
- **SAMX6:** Kondor's, with "полноценный SPECTRUM-растр плюс правильное формирование сигнала INT" (312 lines).
- **SAMX6M, then SAM7CS (boards 3.xx-4.xx) and SAM14CS (5.xx):** his own rework, "теперь все платы будут
  комплектоваться данным синхрогенератором".

No dump of SAMX6M, SAM7CS or SAM14CS has been found. `D2D4A7C8` differs from SAMX12 only in the INT and
line-count bytes, so it may be SAM14CS, but this is unproven.

### 4.3 Verdicts

| # | Claim | Verdict | Evidence |
|:--|:--|:--|:--|
| T1 | v3 frame 69888 T, INT 12580-12583 T before the paper (every emulator) | **confirmed for two PROM families, one of them a 3.2 board's original** | `0A1DFAFD` = UNR's 12580 to within one tick, and it is the PROM dumped from a Profi 3.2 board ([MDESK set](https://github.com/alemorf/retro_computers/tree/master/Profi_3_2)); SAMX6 = ZXM2's 12583 and XP+'s 12588 to within one tick, and ZXM2's 32-line borders |
| T2 | v3 frame 71680 T, INT 47 T before the paper (XP+, measured on a v3.2 Kramis board) | **confirmed for a third PROM family** | `FB0579B6` decodes to 71680 and 48 T; this agrees with XP+'s photographs (47 T), its timing test, and Tact Meter (71680) on that board |
| T3 | Profi boards differ by sync PROM | **confirmed** | five v3/4 dumps give three distinct frames; Gromov's list of generations |
| T4 | v5: 216 T line, 67392 T frame, 13860 T to paper (XP+, PROM decode) | **contradicted** | the same load-63 mistake as our first decode. With DD53 D1 grounded (Kondor's fix, the 5.06 netlist, the 5.03 replica), the v5 board is **224 T x 312 = 69888 T, INT 14368 T before the paper**. TEST 4.30 on a 5.06 reads the original PROM as 69888 ("оригинальный соотв. 69888", [zx-pk 21644 p.2](https://zx-pk.ru/threads/21644-plata-protsessora-profi-v5-06/page2.html)) |
| T4a | v5 INT -> paper 12580 T (UNR, ZXM2 and unreal-ng today) | **contradicted** | 14368 T on the v5 board PROM. That is the 48K's position give or take one tick, which fits Gromov: the v5 is built to behave like a "фирменный" Spectrum |
| T5 | INT length 28 (UNR) / 39 (ZXM2) / 32 (XP+) | **28-30 T for v5** (Gromov's 8-8.6 us); open for v3 | the decode gives 20-44 T per PROM, but the INT circuit is only partly traced |

### 4.4 The v5 video WAIT

What v5 adds, and v3 lacks, is a WAIT from the video controller.

- **Gromov:** "в схеме процессора формируются сигналы WAIT от видеоконтроллера ... позволило полностью
  проэмулировать 'непрозрачную' шину фирменного ZX SPECTRUM". On v3: "в ПЕНТАГОНЕ и старом ПРОФИ он попросту не
  использовался".
- **The v5.0 album:** "REDYT - введение (при необходимости) циклов ожидания".
- **The v5.06 album:** jumper SB8 "WAIT CONFIG". Its PROFI3+ position (the default) is "старый режим работы Profi 5
  с торможением спектрум-режима". Its PENTAGON position switches the Spectrum-mode slowdown off.
- **The v5.06 netlist:**
  - /READY = (/REDYT AND an IORQ one-shot of about 200 ns AND /KBW), OR (in the PENTAGON position: /TURBO AND /80DS).
  - /REDYT holds a CPU RAM access (not ROM, I/O or refresh) until the CPU's DRAM slot strobe.
  - This reading of the schematic covers all RAM pages, not only `#4000-#7FFF`; no source says it in words.
  - The slot pattern is not fully traced: every slot in the border, every other one in the paper.
- **Board differences:** factory 5.03 boards carry an extra diode mod on /REDYT. Removing it gives "wait'ов стало
  меньше" (Vadim, zx-pk), so the exact pattern varies by board revision.
- **The IORQ one-shot** also fires outside turbo: "формирует wait даже не в турбе"
  ([zx-pk 25719 p.21](https://zx-pk.ru/threads/25719-podskazhite-po-profi/page21.html)).

So M6 ("no contention at 3.5 MHz on either board") holds for v3 only. For v3 it is confirmed by Gromov and by
Poltergeist on zx-pk ("память прозрачная").

## 5. Turbo

| # | Claim | Verdict | Evidence |
|:--|:--|:--|:--|
| U1 | v3 has a turbo, switched by a front-panel button, no port | **confirmed** | MAN v3.2 p2 ("режим ТУРБО ... в 1.7 раза"), p5 (the button only works with the controller fitted); no source has a port |
| U2 | v3 turbo = 7 MHz with RAM waits from a shared DRAM slot (2 / 3 waits), ROM / I/O / refresh free | **measured, XP+ only** | MAN v3.2 sch.: clock mux U16 with a READYT wait input, which is consistent but gives no rule; Unreal_NS `PROFI_TURBO` 116920 T per frame (unnamed tact meter) also shows turbo is not wait-free. The rule is derived from the schematic (design 6.2) |
| U3 | the VG93 HLD drops turbo | **XP+ only** | no manual mentions it; Karabas drops turbo on FDC port access instead (a clone) |
| U4 | v4.01: 3.5 MHz while IORQ is low | **XP+ only** | v4 is out of scope |
| U5 | v5 turbo | **exists, rule open** | MAN v5.0 p2 "до 15 МГц", a third crystal 16-24 MHz divided down, "REDYT - введение (при необходимости) циклов ожидания"; the two-crystal fallback is "как и в предыдущих версиях"; V5.02 note: do not switch turbo off in 512x240 on two-crystal boards. XP+ gives v5 the v3 turbo by inheritance, with no evidence |
