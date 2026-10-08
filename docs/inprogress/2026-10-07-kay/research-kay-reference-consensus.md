# Kay: what each source says, and the consensus

**Date:** 2026-10-07 · part of [README.md](README.md)

Only sources that were actually read are listed. "No Kay" means a case-insensitive search for `kay` in the
source tree found no machine, only unrelated words.

## 1. Sources

| ID | Source | Kind | What it gave |
|:--|:--|:--|:--|
| S1 | [Nemo's passport "KAY-1024/3SL/TURBO", dated 29.06.2000](https://speccy4ever.speccy.org/doc/KAY1024.pdf) (scanned, page 3) | primary, vendor | Port table 1 (bit by bit), 1024 KB RAM, 64 KB ROM, 3.5 / 7.0 MHz, ROM / RAM turbo ratio 2 / 1.75, TR-DOS 5.04T + IS-DOS 5.0, Kempston built in |
| S2 | [Kay-1024 schematic, redrawn 2010 from Nemo's original](https://speccy4ever.speccy.org/doc/KAY/Kay-1024_redraw.pdf) and the scanned original on page 1 of S1 (dated 19.01.2000) | primary | Port decoder DD46, latches DD40 (#1FFD) and DD45 (#7FFD), ROM select gates, INT shift register DD53, 14 MHz crystal, AY, slots |
| S3 | [Nemo's article on the Kay-1024 (zxpress.ru 15217)](https://zxpress.ru/article.php?id=15217), read through a web fetch tool | primary, designer | D7 of #1FFD and D7 of #7FFD extend RAM, 64 KB ROM, turbo rules, WAIT, INT, IORQGE |
| S4 | [BC Info Guide #4, the Black_Cat port table](https://github.com/tslabs/zx-evo-docs/blob/master/ZX/zx-ports-full-table.txt) (Kay is machine "7") | secondary, collated | Decode masks of #FE, #1FFD, #7FFD, #BFFD, #FFFD, #1F, Beta ports |
| S5 | [Unreal Speccy `memory.cpp`, `io.cpp`, `config.cpp`](https://github.com/alfishe/unreal-speccy/blob/a685b6f9227ec9dbb595b6cef38addbcd083d6ac/memory.cpp) (`MM_KAY`) and its [`unreal.ini`](https://github.com/alfishe/unrealspeccy/blob/b424255f899ce329e38e9432a25dbfec470a27ba/x32/unreal.ini) (`[ROM.KAY1]`, `[ROM.KAY2]`) | emulator | The only emulator with a Kay paging model |
| S6 | [Unreal Speccy ULA preset "KAY1024"](https://speccy4ever.speccy.org/doc/KAY/Timing.png) (a screenshot of its settings dialog) | emulator, indirect | 69887 T per INT, 224 T per line, paper start 16132, 50 Hz, INT length 32, 4T border |
| S7 | [MAME `scorpion.cpp`](https://github.com/mamedev/mame/blob/f43983b62edfd59959065f24c7fac4c83339454b/src/mame/sinclair/scorpion.cpp) (`kay1024`, flagged `MACHINE_NOT_WORKING`) | emulator | ROM names and checksums only; the machine is the Scorpion driver |
| S8 | [Beta-Turbo controller manual](https://speccy4ever.speccy.org/doc/KAY_BETA.pdf) | primary | `/DOS` and `RS` go to the 27512 ROM pins 1 and 27; `/CSR` to pin 22 |
| S9 | [Wikipedia, Kay 1024](https://en.wikipedia.org/wiki/Kay_1024) | tertiary | Variants: Kay 128 / 256, 1024/3SL/TURBO, 2006 NB (CPLD: multicolor, GigaScreen, 512x192), 2010/SL4 |
| S10 | [speccy4ever KAY page](https://speccy4ever.speccy.org/_KA.htm) | index | ROM images, schematics of every board version |
| S11 | [Unreal-NG slots research, Kay-1024 entry](../2026-10-03-zx-bus-slots/research-machines.md) and [contention research](../2026-09-28-m1-contention/contention-by-machine.md) section 10 | repo | NemoBus, IORQGE rules, no contention at 3.5 MHz |

No Kay machine: ZXMAK2, UnrealSpeccyP, Xpeccy, xpeccy-plus, Fuse-family sources, Zero, ZX-M8XXX (it ships the two Kay
ROMs and a one-line note "Pentagon-based, 1 MB RAM, turbo", no code).

## 2. Comparison

| Fact | S1 passport | S2 schematic | S4 BC #4 | S5 Unreal Speccy | Take |
|:--|:--|:--|:--|:--|:--|
| #7FFD decode | A15 = 0, A14 = 1, A1 = 0, A0 = 1 | DD46: A2 = A15, A1 = A14, A0 = /WR, E1 = A1 (low), E2 = A0 (high) | `01xxxxxxxxxxxx01` | A15 = 0 and A1 = 0 only (A0 and A14 ignored) | the three primary / collated sources agree: **tight decode** (Q2) |
| #1FFD decode | A15 = 0, A14 = 0, A1 = 0, A0 = 1 | same DD46, output 0 | `00xxxxxxxxxxxx01` | `(port & 0xC003) == 0x0001` | all agree |
| AY | #BFFD (write data) and #FFFD (select, read): A15, A14, A1 = 0, A0 = 1 | DD46 outputs 4 to 7 feed BDIR / BC1 | `10xxxxxxxxxxxx01`, `11xxxxxxxxxxxx01` | the generic Unreal AY rule (low byte #FD), not Kay-specific | passport, schematic and BC agree |
| Kempston | A0 = 1 (any odd port); blocked while reading #FFFD | | `xxxxxxxxxxxxxxx1` | A5 = 0 (standard) | passport + BC (A0 = 1) |
| #FE | A0 = 0; read D5 = 0, D6 = EAR, D7 = BUSY (Centronics); write D0-D2 border, D3 tape, D4 speaker, D5-D7 = 0 | BUSY pulled up by 1.5 kOhm | `xxxxxxxxxxxxxxx0`, "Key, Tape, Prn" on read | standard | passport (Q6) |
| #1FFD bits | D0 RAM page 0 at #0000, D1 Centronics Q8, D2 SLCTIN: 0 = TURBO, 1 = NORM, D3 ROMS, D4 RAM bank, D5 STROBE, D6 Centronics O6, D7 RAM a18 | DD40 latch, same wiring | | D0 RAM at 0, D3 ROM high bit, D4 / D7 RAM bits; D1, D2, D5, D6 unused | passport |
| #7FFD bits | D0-D2 page in the top 16K, D3 screen, D4 128 / 48 (and TR-DOS), D5 lock, D6 INIT (Centronics), D7 RAM a17 | DD45 latch; the lock gates its clock (DD49.3) | | D0-D4 standard, D5 lock, D7 RAM bit 5 | passport + Unreal |
| RAM page index | | | | `bank += ((1FFD & 0x10) >> 1) + ((1FFD & 0x80) >> 3) + ((7FFD & 0x80) >> 2)`: bank bit 3 = 1FFD.4, bit 4 = 1FFD.7, bit 5 = 7FFD.7 | Unreal's order; the passport labels the two top bits the other way round (a17 = 7FFD.D7, a18 = 1FFD.D7). Only a permutation of the upper pages (Q3) |
| ROM image | 64 KB | 27512 | | four 16K pages; roles 0 = 128, 1 = 48, 2 = SYS, 3 = DOS | see [roms.md](roms.md) |
| ROM select (roles) | D4 of #7FFD "128 / TR-DOS / 48" | A15 of the ROM = ROMS xor a DOS term chosen by jumper JP5; A14 = 7FFD.D4 | | `idx = (1FFD.3 ? 2 : 0) ^ (TR-DOS ? 2 : 0) + 7FFD.4`; 0 = 128, 1 = 48, 2 = SYS, 3 = DOS | Unreal's formula; the page that holds each role is a jumper-dependent fact (Q1) |
| RAM at #0000 | D0 of #1FFD | /BLK; ROM chip select = NOR(BLK, A14, A15) | | `1FFD & 1` | all agree. There is no Scorpion-style special paging, no monitor bit |
| TR-DOS "leave" rule | | | | `CF_LEAVEDOSRAM`: the TR-DOS trap does not start from RAM at #3Dxx | Unreal |
| Frame | | 14 MHz crystal, discrete counters | | 69887 T (as typed in the dialog), 224 T line, paper at 16132 | Unreal preset; counters not yet decoded (Q4) |
| INT | | DD53, a shift register clocked by /M1: INT lasts a few M1 cycles | | length 32 T | S3 says the length depends on the instructions at the moment of the request: matches the shift register. Start with 32 T (Q4) |
| Contention | "WAIT-free NORMAL mode" (S3) | | | none | none at 3.5 MHz |
| Turbo | switch on the front panel, #1FFD.D2 = 0, bus line TURBO; ratio ROM 2.0 / RAM 1.75 (passport) or 1.9 (S3); 7 MHz | `*TURBO` through JP3 | | not modelled | Q9 |
| Beta disk | BETA-TURBO card in a slot, built-in nothing | | `WD1793(7)`: `0BAxxx11`, system `#FF`: `1xxxxx11` | built-in Beta 128 | the card's decode (Q10) |
| IDE | NemoIDE card (A2 = A1 = 0 scheme) | | | Nemo / Scorpion / ATM IDE | `IDE_NEMO` exists |

## 3. Things the sources disagree on, and how this folder settles them

1. The decode of #7FFD: Unreal Speccy ignores A0 and A14. The vendor passport, the schematic and the BC table agree.
   The design follows them. A program that writes #7FFD through `OUT (#FC),A` (the Navy Seals trick, a comment in
   Unreal's `io.cpp`) would not page a real Kay.
2. The order of the two top RAM bits: a permutation of pages 16-63 only.
3. The Kempston bit order: the passport table prints D0 = Left, D1 = Right, D2 = Up, D3 = Down, which is not the
   standard Kempston order (D0 = Right, D1 = Left, D2 = Down, D3 = Up). Every program expects the standard order. The
   design keeps the standard order until the wiring (D36 / D37 and XS3 in S2) is traced (Q5).
4. The ROM page that holds TR-DOS and the one that holds the Kramis service: see [roms.md](roms.md) section 3.
