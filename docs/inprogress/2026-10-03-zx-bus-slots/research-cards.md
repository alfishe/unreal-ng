# ZX-bus slots, research SL-0 part 2: the real cards

| | |
|---|---|
| **Date** | 2026-10-03 |
| **For** | [compatibility-matrix.md](compatibility-matrix.md), [architecture.md](architecture.md) (card catalog `CardType`, `PortClaim`, functions, `BusDeclaration`) |
| **Model of detail** | [ZX-MultiSound hardware reference](../2026-10-03-zx-multisound/hardware-reference.md) |
| **Method** | Primary sources first (schematics, CPLD / FPGA sources, original manuals, the repo's own design folders for cards already emulated), then emulators (MAME, FUSE, Unreal Speccy, Xpeccy, zxsp) as secondary evidence. Where sources disagree, the disagreement is tabulated. |
| **Status** | Research notes; every fact carries its source; guesses are marked **unconfirmed** |

Reference tags: **[SCH]** schematic read, **[RTL]** CPLD / FPGA source read, **[DOC]** original manual / article,
**[EMU]** emulator source, **[REPO]** an unreal-ng design document, **[DER]** derived from decode arithmetic (no
real-world report found).

Local files are named as "local collection: `<folder>/<file>`" (the owner's ZX Spectrum collection, not in the
repository). "ZX-Evo RTL" means the ZX-Evolution FPGA sources ([tslabs/zx-evo](https://github.com/tslabs/zx-evo),
`pentevo/fpga/...`; BaseConf and TS-Conf trees).

## Contents

1. [Glossary](#1-glossary)
2. [Headline findings (changes to the current design)](#2-headline-findings-changes-to-the-current-design)
3. [The buses](#3-the-buses)
4. [Summary table](#4-summary-table)
5. [Sound cards](#5-sound-cards)
6. [Storage, DOS and NMI cards](#6-storage-dos-and-nmi-cards)
7. [Network and serial cards](#7-network-and-serial-cards)
8. [Input cards](#8-input-cards)
9. [Pairwise conflicts](#9-pairwise-conflicts)
10. [Open / unconfirmed](#10-open--unconfirmed)
11. [Sources](#11-sources)

## 1. Glossary

| Term | Meaning |
|---|---|
| **Decode / mask / match** | Which address lines a card compares. A card claims port `p` when `(p & mask) == match`. Lines outside the mask are "not decoded": the card answers every *mirror* (e.g. GS `#B3` answers `#00B3`, `#12B3`, `#FFB3`). |
| **Low-byte decode** | Only A7-A0 compared (mask `#00FF`). Most ZX-bus cards decode this way. |
| **IORQGE** | "IORQ gate enable". A ZX-bus / NemoBus line (pin A13, **active high**). A card drives it to 1 when it answers a port; the machine's own port decoder then stays silent for that cycle. On the 48K edge the same pin exists (lower 13) and inhibits the ULA. |
| **Passive card** | A card that never drives IORQGE. On writes it just *co-receives* (the machine and every other listener latch the same byte). On reads it fights any other driver. |
| **Shadowing** | A card claims, with IORQGE, a port the machine also decodes; the machine device stays fitted but neither answers nor sounds (owner decision Q2). |
| **porthit** | ZX-Evo / TS-Conf FPGA signal: "this port belongs to the mainboard". It masks /IORQ to the slots, so a slot card **never sees** the cycle (the reverse of NemoBus priority). |
| **/DOS (DCDOS)** | ZX-bus pin A4, active low: "the TR-DOS ROM is paged" (and on most machines the Beta-128 ports are open). Output of the machine. |
| **/IODOS** | NemoBus v1.1 pin B20, active low: "shadow ports open" (inverse of `#EFF7` D7 on Pentagon-1024 class boards). Only some boards drive it; ZX-Evo leaves B20 unconnected. |
| **ROM-fetch lock** | A card-side substitute for /DOS: the card ignores some ports while the last opcode fetch (MultiSound) or memory read (ZXM-SoundCard "PentEvo" mode) came from `#0000-#3FFF`. |
| **/ROMCS** | Sinclair edge line that switches the internal ROM off so a card can map its own ROM (Beta, DivIDE, Multiface). The +2A/+3 edge replaces it with /OE ROM1 and /OE ROM2. NemoBus: CSR/ (25A, out) and RDR/ (15A, card blocks ROM). |
| **Automap** | DivIDE / DivMMC: map the card ROM after an opcode fetch from a trap address (`#0000`, `#0008`, `#0038`, `#0066`, `#04C6`, `#0562`, `#3Dxx`). |
| **AY socket** | The DIP-40 / DIP-28 socket of the board's AY. A TurboSound board plugs in there and **inherits the machine's decode**: it sees only BDIR, BC1, BC2, A8, /A9, D0-D7. |
| **Function** | The slot design's unit of exclusivity ([architecture.md](architecture.md) §3.3): two cards that occupy the same function are incompatible. |
| **BC IG #4 / #7** | Black_Cat's *Info Guide* #4 (full ZX port table, [PDF](https://wiki.speccy.org/_media/cursos/ensamblador/zx-ports-full-table.pdf); a copy is in the repo as `docs/ports/zx-ports-full-table.txt`) and #7 ("Standardization of ZX BUS interfaces and buses", R20200527, [zx.clan.su thread](https://zx.clan.su/forum/7-82-1)). #7 is the closest thing to a NemoBus standard. |

## 2. Headline findings (changes to the current design)

1. **ZX-Evo and TS-Conf reverse the IORQGE priority.** In the ZX-Evo RTL (`z80/zbus.v`): `iorq1_n = iorq_n | porthit`,
   `iorq2_n = iorq1_n | iorqge1`. A slot card never sees /IORQ for a mainboard port (`#FE`, `#FD` family, `#1F`
   outside shadow, `#DF`, `#EF`, `#57`, `#77`, NemoIDE, and on TS-Conf also `#AF`, `#FB`); slot 1 outranks slot 2;
   unclaimed reads and every INTA return `#FF`. `FREE_IORQ` in `quartus/tune.v` (off by default) would lift the mask.
   **Consequence:** `BusDeclaration` needs a per-machine priority rule (`cardWins` for NemoBus / Scorpion,
   `machineWins` for ZX-Evo / TS-Conf). Example C/D of the matrix ("MultiSound shadows the FPGA TurboSound") is
   **wrong for ZX-Evo as built**: the FPGA TurboSound answers `#FFFD` / `#BFFD` and the card never sees them. The
   MultiSound author's comments ("iorq_n are useless in zxevo", "dos_n are useless in zxevo") are this effect.
2. **The classic General Sound has no port `#33`.** The schematic and the GS manuals v1.03 / v1.4 decode only `#B3`
   and `#BB`; `#33` was introduced by NeoGS (2006). ZXM-GeneralSound and Black_Cat's GS mod have a different `#33`.
   The matrix lists `#33` for `gs` / `gs-lw`: make it a per-card claim. The classic GS also drives IORQGE **on reads
   only**.
3. **The classic GS decode is a full low byte** (`#00FF` / `#B3`, `#BB`), not partial; Unreal's `(p & #F7) == #B3`
   is the same set (A3 = register select).
4. **SounDrive 1.05 decodes far more loosely than every emulator** (BC IG #4): mode 1 compares only A0 = 1, A5 = 0
   (channel = A4, A6); mode 2 only A0 = 1, A2 = 0 (channel = A1, A3). Mode is a switch (S1), so the two port sets
   are **alternatives, not both at once**. With the loose decode, mode 1 hears Beta `#1F` / `#5F` in TR-DOS, and
   mode 2 hears GS `#B3` / `#BB` / `#33`. Schematic not found; flagged unconfirmed.
5. **Nemo IDE drives IORQGE for every non-M1 cycle with A2 = A1 = 0 outside DOS** (original schematic, local
   collection `hardware/hdd-ide/NEMOHDD.ZIP` → `NEMOIDE.TXT`): mask `#0006` / `#0000`. That is a huge claim
   (a quarter of the port space) that shadows the Pentagon `#FE`, `#7FFD` mirrors and any A5-only Kempston on those
   ports. It is deliberate: without it `OUT (#10)` would also hit the border.
6. **/IODOS exists but is optional.** BC IG #7 defines it (NemoBus v1.1, pin B20); ZXM-MoonSound and ZXM-SoundCard
   use it (pulled up on the card, so absent = inactive); ZX-Evo does not drive it; MAME lists B20 as unused. No card
   *requires* it. Model it as an optional signal.
7. **The 128K and +3 edges have no IORQGE** (BC IG #7; MAME says "/IORQGE (+2 only)" for the 128K edge pin 13:
   disagreement). Every IORQGE card on a 128K / +3 is `fit = unrealistic` without an adapter.
8. **"TSFM Pro" is not a board.** It is the ZX-Art / tracker name for the target "TurboSound FM + SAA1099" (6 FM +
   6 SSG + 6 SAA channels). Hardware that plays it: ZXM-SoundCard (rev 00-03, Extreme), ZX-MultiSound, Zaxon's
   "Turbosound FM and SAA1099" card. Treat it as a function bundle (`ay-socket` YM2203 pair + `saa`), not a card id.
9. **Three different TurboSound control-byte widths** decide which bytes switch chips: NedoPC TS hardware `#FC-#FF`
   (6-bit compare), TSFM `#F8-#FF` (5 bits), ZXM-SoundCard / MultiSound `#F0-#FF` (4 bits). unreal-ng's TS accepts
   `#FE` / `#FF` only (`soundchip_turbosound.cpp:438`).
10. **The repo's Kempston mouse doc says "no Kempston Mouse Turbo"** (`2026-09-12-kempston-mouse/hardware-reference.md`
    §8); BC IG #4 and [Velesoft](https://velesoft.speccy.cz/kmturbo-cz.htm) document K-Mouse Turbo (A15 = master /
    slave, `#FEDF` detect).

## 3. The buses

| Bus | Machines | IORQGE | /IODOS | /DOS | ROM control | /WAIT | +12 V | Priority on overlap | Source |
|---|---|---|---|---|---|---|---|---|---|
| Sinclair 48K edge | 16K / 48K (all issues), clones with a 48K edge | lower 13, input, high inhibits the ULA (+ IORQ + A0 = 0) | - | - | /ROMCS lower 25 | lower 21 | upper 22 (also +9 V, -5 V, 12 V AC) | IORQGE card wins over the ULA; card vs card undefined | BC IG #7 table 1; [Sinclair Wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_16K/48K_edge_connector); [MAME exp.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/exp.h) |
| 128K edge | 128K, +2 (grey) | **disagreement:** BC IG #7 = not connected; MAME = "/IORQGE (+2 only)" | - | - | /ROMCS lower 25 | yes | yes | two drivers = TTL fight (undocumented) | same |
| +3 edge | +2A / +2B / +3 / +3B | not connected (both sources) | - | - | **no /ROMCS**: /OE ROM1 upper 4, /OE ROM2 lower 15 | yes | +12 V and -12 V | as 128K | BC IG #7; MAME exp.h; [Sinclair Wiki +3 edge](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_+2A/2B,_+3/3B_edge_connector) |
| NemoBus v0.9 / 1.0 / 1.1 | KAY-256 / 1024, Pentagon-1024SL v1.3-2.2, Pentagon 2.666, ZXM-Phoenix | 13A, active high; serial slot arbiter: master slot > slot 1 > ... > **mainboard lowest** | 20B, v1.1 only | 4A | CSR/ 25A out, RDR/ 15A in, RS 16A (`#7FFD` D4), BLK 4B | 21B, open collector | 29B | **card wins** | BC IG #7 §3.4, tables 3-6 |
| ZX-Evo slots (NemoBus v0.9m) | ZX-Evolution rev A-C (BaseConf, TS-Conf) | 13A; slot 1 → slot 2 cascade (XS1 A31 / B31) | **none** (B20 unused) | 4A, = `rompg[1]` | A15 CSROMCE, A25 CSROM, A16 RS | 21B | 29B **only with jumper J4** | **machine wins** (porthit); slot 1 > slot 2; nothing claimed → `#FF` | ZX-Evo user manual rev C slot table; ZX-Evo RTL `zbus.v`, `zports.v` |
| Scorpion bus | Scorpion ZS256, Turbo+, ProfScorpion | present: all board ports qualify on IORQGE = 0 (`#7FFD`, `#1FFD`, `#FF`, `#FE`, Centronics, TR-DOS) | - | pin 4 | CSR/, RDR/, BLK | yes | yes, pin disputed (BC row 22; MAME: A5 = +12 V on Scorpion) | card wins over the board; no slot arbiter (not NemoBus by BC's definition) | local collection: `scorpion/` (`scorpion.doc` §1.1); BC IG #7; [MAME zxbus/bus.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/zxbus/bus.h) |
| Profi bus | Profi+ / Profi 3+ / 5 | **OUTIORQ** (pin 15A): the board uses `/IORQ OR /OUTIORQ`, so a card vetoes the board decode (same role, own name) | - | TR_DOS 31, CP/M 30, BLOK 32 | ROMCS 27, ROM14 | READY 23 | pin 24 | card wins (if OUTIORQ driven) | BC IG #7 col. 10; repo `2026-10-01-profi-v3-v5/research-profi-v5-open-items.md` Q5 |
| ATM internal I/O | ATM Turbo 2+ 7.10 | none; not a ZX-bus (buffered ID0-7, CTS0-7 from `#FB`, IORD / IOWR from `#FA`) | - | - | - | - | yes | n/a; ZX-bus only through a third-party CPU-socket adapter | ATM 7.10 architecture document; repo `2026-10-02-atm2ioesp/` |
| Pentagon 128 (1991) | Pentagon 128 | **none on the board**; a ZX-bus needs an add-on IORQGE gate | - | internal | - | - | - | n/a | [zx-pk.ru thread 28751](https://zx-pk.ru/threads/28751-pentagon-128-zx-bus-ili-kak-prikrutit-naprimer-z-controller.html); [ZX-Review 1994 #5](https://zxpress.ru/book_articles.php?id=462) |

**Read-conflict rule per bus** (for `BusDeclaration.readRule`): NemoBus / Scorpion / Profi: the IORQGE card wins over
the machine; two cards on the same port are "the user's responsibility" (BC IG #7 §4: tri-state outputs, undefined).
ZX-Evo: the machine wins, slot 1 beats slot 2, nobody → `#FF`. 48K: IORQGE card beats the ULA; without IORQGE both
drive (the common claim that the ULA sits behind series resistors so the bus wins was **not verified**). 128K / +3:
undefined fight.

**BC IG #7 design principles** ("Altwasser principles"): (1) IORQGE is formed from the address and M1 only, without
IORQ (so it is valid early); (2) mainboard ports have the lowest priority. Cards that violate (1): ZXNETUSB (address
only, no M1), NeoGS (address only). IM2 vector arbitration uses IORQGE during INTA.

**Who drives the DOS lines:** /DOS is driven by the machine's DOS trigger (ZX-Evo `dos_n = rompg[1]`). No *card* in
this list drives /DOS or /IODOS; Beta-128 on the Sinclair edge drives /ROMCS instead. The storage research found no
source naming /IODOS; the network research found it in BC IG #7 and on the ZXM cards (disagreement resolved in
favor of "exists on NemoBus v1.1, optional").

## 4. Summary table

Ports are low-byte mask / match unless a 16-bit mask is given. ✓ = drives IORQGE, · = passive, R✓ = IORQGE on reads
only. "ROM lock" = card-side ROM-fetch lock. Functions in `code`.

| Card | Bus | Required signals | Functions | Ports (mask / match) | IORQGE | Gating | Options | Media |
|---|---|---|---|---|---|---|---|---|
| Melodik / Zaxon AY-Magic | 48K edge | /IORQ /RD /WR A15 A14 A1 | `ay-socket` (as an AY role) | `#C002/#C000` reg+read, `#C002/#8000` data | · | none | - | - |
| Fuller Box | 48K edge | /IORQ /RD /WR | `fuller-ay`, `fuller-joystick` | `#3F` reg, `#5F` data, `#7F` joy (decode width disputed) | · | none | Orator speech `#9F`/`#BF` | - |
| DK'tronics 3-channel | 48K edge | /IORQ /WR | `fuller-ay` | `#3F`, `#5F` (decode unknown) | · | none | - | - |
| Bi-Pak ZON X | 48K edge | A0-A4 A7 /IORQ /WR /CLK | `zonx-ay` | `#9F/#9F` reg, `#9F/#1F` data (W only) | · | none | ZON X-81: `#9F/#8F`, `#9F/#0F` | - |
| NedoPC TurboSound | AY socket | socket only | `ay-socket` | host decode; select `#FC-#FF` on `#FFFD` | (host) | (host) | - | - |
| PoS TurboSound (Bitwalker) | 48K / 128K edge | /IORQ /M1 /RD /WR /DOS | `ay-socket`, `kempston-joystick` | `#C002/#C000`, `#8002/#8000`, select+joy `#F3/#13` (`#1F`) | · | select / joy off in DOS | - | - |
| Quadro-AY | edge (replaces host AY decode) | /IORQ /M1 /WR A15 A14 A12 A1 | `ay-socket` | `#D002/#D000` `#FFFD`, `#D002/#C000` `#EFFD`, data `#9000`/`#8000` | · | none | - | - |
| TurboSound FM (NedoPC) | AY socket | socket only | `ay-socket` | host decode; control `#F8-#FF` | (host) | (host) | - | - |
| ZXM-SoundCard Extreme rev01 | NemoBus | A0-A15 /IORQ /MREQ /M1 /RD /WR /RESET /DOS /IODOS IORQGE | `ay-socket` (R✓), `saa`, `soundrive`, `covox-fb` | YM `#C0FF/#C0FD`, `#C0FF/#80FD`; `#F0FF/#F0FC`; SAA `#C0FF/#00FF` (A8 = A0) or `#CEFF/#04FF`; SD `#AF/#0F`, `#FB`, `#3F` | R✓ on YM reg read | SAA, SD: /DOS + /IODOS, or ROM lock (PentEvo) | JP1 SAA port, JP2 PentEvo, JP3 YM off | - |
| ZXM-SoundCard Light / Middle | NemoBus | as Extreme | `saa` / `saa` + `soundrive` | SAA `#FF` / `#1FF` | unconfirmed | /DOS | - | - |
| ZX-MultiSound rev.A2 | NemoBus | A0-A15 /RD /WR /M1 /MREQ /RESET IORQGE **+12 V** | `ay-socket` (shadow) + `midi`, `saa`, `gs`, `soundrive` | YM `#C00F/#C00D`, data `#C00F/#800D`; SAA `#00FF/#00FF` (A8); GS `#B3`, `#BB`; SD `#AF/#0F` | ✓ `#FFFD` (A13 = 1), `#BFFD`, `#B3`, `#BB`; · SAA, SD, `#DFFD` | SAA, SD: ROM lock | DIP `ym,saa,gs,sd` | - |
| General Sound (classic) | ZX-bus | A0-A7 /IORQ /WR /M1 /RESET IORQGE | `gs` | `#FF/#B3`, `#FF/#BB` | R✓ | /M1 = 1 | RAM 128 K-2 M; ROM 1.04 / 1.05; Black_Cat mod (`#33`, full IORQGE) | - |
| NeoGS | ZX-bus | A0-A7 (A14 A15 /MREQ /CSROM /RDROM /WAIT for DMA) /IORQ /RD /WR IORQGE | `gs` (+ `zxdma`) | `#FF/#B3`, `#BB`, `#33` | ✓ address only | none | J1 reset link; RAM 2 / 4 MB; firmware | SD (`sd.ngs`), flash |
| ZXM-GeneralSound | ZX-bus (62 pin) | A0-A7 /IORQ /MREQ /RD /WR /M1 /RESET IORQGE | `gs` | `#F7/#B3`; `#33` W (bit 4 = disable) | ✓ GS ports (v1.02); · `#33` | /M1 = 1 | clock 12 / 15 / 18 MHz; RAM | - |
| ZXM-MoonSound | ZX-bus | A0-A7 /RD /WR /IORQ /MREQ /M1 /RES /DOS /IODOS IORQGE (+12 V?) | `opl4` | `#FC/#C4`, `#FE/#7E` | ✓ | off in /IODOS; also /DOS with JP1 open | JP1 PentEvo; IRQ jumpers | sample ROM (flash) |
| Covox (Pentagon) | ZX-bus / internal | A2 (homebrew) or A0-A7, /IORQ /WR | `covox-fb` | `#04/#00` (A2 only) or `#FF/#FB` | · | none | port width | - |
| Covox (Scorpion) | Scorpion internal | /IORQ /WR | `covox-dd` | `#DD` | · | none | - | - |
| SounDrive 1.05 | ZX-bus | /IORQ /WR (unconfirmed) | `soundrive` (mode 1) or `soundrive` + `covox-fb` (mode 2) | mode 1 `#21/#01` (BC IG #4) or `#AF/#0F` (emulators); mode 2 `#05/#01` or `#F5/#F1` | · | none known | S1 mode switch | - |
| Simple SAA IF (and SAM-style SAA) | 48K edge | unconfirmed | `saa` | `#FF` data, `#1FF` address (A8) | · | unconfirmed | - | - |
| Beta 128 (edge card) | 48K / 128K edge | /ROMCS /M1 /NMI /RESET, `#7FFD` snoop | `beta128` | `#83/#03` (`#1F #3F #5F #7F`), `#83/#83` (`#FF`) | · (edge has no meaning) | only while TR-DOS ROM paged (`#3Dxx` in, ≥ `#4000` out) | switch off / normal / reset; magic button | FDD × 4 |
| Beta (built in) | machine | drives /DOS | `beta128` | per machine (§6.1) | n/a | DOS | - | FDD × 4 |
| DivIDE | 48K / 128K / +2 edge (+3 with jumper A) | /ROMCS /M1 /MREQ /RFSH /RESET | `divide` (automap), `ide.divide` | `#E3/#A3` IDE, `#FF/#E3` control | · | automap | jumper E, A; firmware | IDE ×2, EEPROM 8 KB |
| DivMMC | edge | as DivIDE | `divmmc`, `sd.divmmc` (+ joystick on EnJOY!) | `#FF/#E3`, `#E7`, `#EB` | · | automap | DIP (esxDOS, joystick) | SD ×1-2, EEPROM |
| Nemo IDE / Nemo-A8 | NemoBus | A0-A7 (A8) /IORQ /RD /WR /M1 /DOS IORQGE /RESET | `ide.nemo` | board `#06/#00` (all ✓); task file `#1F/#10`, `#C8`; latch `#07/#01` (A8 var.: A8 = 1) | ✓ whole `#06/#00` | off in DOS | A0 or A8 latch | IDE ×2 |
| SMUC v1 / v2 | Scorpion bus | /DOS A15 A13-A11 A10-A8 A7-A5 A2-A0 /INT | `ide.smuc`, `rtc.ds1685`, `nvram.24c16` | family `#B8E7/...` (§6.5) | unconfirmed | DOS (v1); none (v2 clone) | version | IDE ×2, EEPROM 2 KB, RTC CMOS |
| Z-Controller (KOE) | ZX-bus | A0-A15 /M1 /DOS IORQGE /NMI /RES | `sd.zc`, `ide.nemo`, `kempston-mouse`, `ps2-keyboard` | `#FF/#57`, `#FF/#77`, + Nemo IDE, `#DF` mouse, `#FE` keyboard | likely (unconfirmed) | unconfirmed | - | SD, IDE ×2 |
| Multiface 1 / 128 / 3 | 48K / 128K / +3 edge | /NMI /M1 /ROMCS (+3: /OE ROM1/2) | `multiface` (+ MF1 `kempston-joystick`) | MF1 `#9F`/`#1F`; MF128 `#BF`/`#3F`; MF3 `#3F`/`#BF` | · | NMI-driven | J1 joystick, hide | - |
| ZXNETUSB | NemoBus | A0-A15 /IORQ /RD /WR /MREQ /RESET /CSROM IORQGE /INT, ROM block | `net.zxnetusb`, `usb.sl811` | `#FF/#AB` (A15, A9-A8 select) | ✓ address only | none | CPLD build | - |
| ZX-WiFi (izzx) | ZX-bus | A0-A10 /IORQ /RD /WR /M1 (/RESET v1.4+) IORQGE (v1.2+) | `serial.ef` (or `serial.ee`) | `#FF/#EF` (A10-A8 = register) | ✓ v1.2+ | none | `#EF`/`#EE`; crystal; ESP / RS-232 switch | - |
| Kempston joystick | 48K edge / built in | /IORQ (/RD), A5 (A6 A7) | `kempston-joystick` | `#20/#00` (original) … `#FF/#1F` (§8.1) | · | built-ins: off in DOS | - | - |
| Kempston mouse | 48K edge / ZX-bus | /IORQ /RD A0 A5 (A7) A8 A10 (A15) | `kempston-mouse` | `#FADF`, `#FBDF`, `#FFDF` (width per variant, §8.2) | · original; ✓ ZX_BUS_Mouse | ZX_BUS_Mouse: A15 | Turbo master / slave | - |

## 5. Sound cards

### 5.1 AY interfaces for the 48K

#### Didaktik Melodik (and Zaxon AY-Magic)

One AY-3-8912 on the 48K edge with the 128K's ports; ACB stereo and an LM386 speaker amplifier.

- **Bus:** 48K edge (through connector). **Signals [SCH]:** /IORQ, /RD, /WR, A15, A14, A1, D0-D7, /RESET. No /M1,
  /DOS, /ROMCS, /WAIT, /INT, IORQGE. Own 3.579545 MHz crystal / 2 = 1.7898 MHz.
- **Decode [SCH]:** 74LS138-type decoder: inputs A = A14, B = /WR, C = /RD; enables /IORQ, A1 (low), A15 (high).
  BC2 and A8 tied high.

| Function | Decode lines | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| AY register select / data read | A15, A14, A1 | `#C002/#C000` | `#FFFD` | W / R | no | none |
| AY data write | A15, A14, A1 | `#C002/#8000` | `#BFFD` | W (a read decodes to nothing) | no | none |

- **Functions:** the AY role on `#FFFD` / `#BFFD` (= `ay-socket` as a function). **Options:** none.
- **Disagreement (clock):** schematic and MAME 1.7898 MHz; zxsp 1.75 MHz. Zaxon AY-Magic: zxsp models it exactly
  like Melodik (no schematic; unconfirmed). Stavi (Poland, 1986): no decode data.
- **Conflicts:** on a 128K / +2 / Pentagon both chips latch every write and both drive `#FFFD` reads. FUSE allows
  Melodik only on 16K / 48K / TC2048.
- **Sources:** [Melodik schematic (Kio)](https://k1.spdns.de/Vintage/Sinclair/82/Peripherals/Didaktik%20Melodik%20Sound%20Interface/schematic%20melodik.jpg),
  [MAME melodik.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/melodik.cpp),
  [FUSE melodik.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/sound/melodik.c),
  [zxsp AySubclasses.cpp](https://github.com/Megatokio/zxsp/blob/master/Source/Uni/Items/Ay/AySubclasses.cpp).
- **unreal-ng today:** the 48K decoder always decodes a Melodik-style AY (`portdecoder_spectrum48.cpp:79-95`); it
  should become a selectable card.

#### Fuller Box (1983) and DK'tronics 3-channel synthesizer (1984)

AY-3-8912, Atari-style joystick port (Fuller), beep amplifier, through bus. No schematic for either.

| Function | Port | R/W | Decode by source |
|---|---|---|---|
| AY register select | `#3F` | W | FUSE full low byte; zxsp A7-A5 = `001` ("to be verified"); nocash A7-A6 = `00` |
| AY data write | `#5F` | W | FUSE `#FF/#5F`; zxsp A7-A5 = `010` |
| AY data read | `#3F` (FUSE, zxsp, nocash) **or** `#5F` (MAME, Kio's `Fuller Box.txt`) | R | disputed |
| Joystick (Fuller) | `#7F` | R, `F---RLDU` active low | FUSE `#FF/#7F`; zxsp A7-A5 = `011` |

- **Signals:** /IORQ, /RD, /WR, low address lines; no /DOS, /ROMCS, IORQGE in any source.
- **Functions:** `fuller-ay` (a separate AY role, does not take `#FFFD`), `fuller-joystick`. DK'tronics occupies the
  same `fuller-ay` (mutually exclusive). **Options:** Orator speech `#9F` W / `#BF` R (Fuller Master Unit).
- **Clock:** MAME 1.7898 MHz ("unverified"); zxsp 1.7 MHz ("guessed").
- **Conflicts [DER]:** Beta-128 `#3F` / `#5F` / `#7F` (TR-DOS writes program the AY, `#7F` reads fight; no /DOS
  gating known); SounDrive mode 1 `#5F`; Profi Covox `#3F` / `#5F`; ZON X data.
- **Sources:** [FUSE fuller.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/sound/fuller.c),
  [MAME fuller.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/fuller.cpp),
  [zxsp FullerBox.cpp](https://github.com/Megatokio/zxsp/blob/master/Source/Uni/Items/Ay/FullerBox.cpp),
  [Kio Fuller Box folder](https://k1.spdns.de/Vintage/Sinclair/82/Peripherals/Fuller%20Box/),
  [WoS ports FAQ](https://worldofspectrum.org/faq/reference/ports.htm), [nocash zxdocs](https://problemkaputt.de/zxdocs.htm),
  [Spectrum Computing DK'tronics entry](https://spectrumcomputing.co.uk/entry/1000118/Hardware/DKTronics_3_Channel_Sound_Synthesiser).

#### Bi-Pak ZON X / ZON X-81

Write-only AY (no /RD). Signals: A0-A4, A7, /IORQ, /WR, /CLK (CPU clock / 2 ≈ 1.75 MHz). Decode (zxsp, from the issue-5
PCB): A4-A0 = `11111`, A7 = function; A5, A6 not decoded.

| Board | Function | Mask / match | Ports |
|---|---|---|---|
| ZON X | index | `#9F/#9F` | `#FF`, `#DF`, `#BF`, `#9F` |
| ZON X | data | `#9F/#1F` | `#7F`, `#5F`, `#3F`, `#1F` |
| ZON X-81 | index / data | `#9F/#8F` / `#9F/#0F` | nocash: `#DF` (sometimes `#CF`) / `#0F` |

Functions `zonx-ay`. Conflicts [DER]: index hears SAA `#FF`, SpecDrum `#DF`, Orator `#9F`, Beta `#FF`; data hears
Fuller, Beta, SounDrive `#1F` / `#5F`, PoS TS `#1F`. Sources: zxsp `AySubclasses.cpp`,
[Kio ZON X folder](https://k1.spdns.de/Vintage/Sinclair/82/Peripherals/Bi-Pak%20ZON%20X%20Sound%20Module/), nocash.

#### For comparison: Timex TS2068 built-in AY, Cheetah SpecDrum

- Timex: index `#FF/#F5` W, data `#FF/#F6` R/W (A8 / A9 select the joystick on R14 reads). Timex `#FF` is its video
  mode port, so SAA cards (`#FF`) do not work on Timex ([Jungsi](https://www.jungsi.de/turbosound-fm-and-saa1099-sound-card-retro-sinclair-zx-spectrum/)).
- SpecDrum: DAC on `#DF`, write only; FUSE and MAME full low byte. Function `specdrum`. Overlaps the Kempston mouse
  low byte (`#DF`) only on writes, which the mouse ignores.

### 5.2 TurboSound (2 × AY)

#### NedoPC TurboSound (CHRV & Ronin, 2005)

Two YM2149 on a board in the AY socket (IDC40 plugs in the AY-3-8910 pinout).

- **Bus:** `ay-socket`; inherits the host decode completely (128K / Pentagon: `#C002`), sees only BDIR, BC1, BC2,
  DA0-7, A8, /A9, /RS, CLK. No IORQGE, /DOS, /M1.
- **Chip select [SCH, local collection `hardware/turbo-sound/TS240420.PNG`, `TS030520.RAR`]:** an 8-input NAND on
  BDIR, BC1, DA7-DA2 clocks a flip-flop with D = DA0; Q / /Q drive the chips' A8. /RS sets the flip-flop: reset
  selects the first chip. Both chips also see the select byte as an address write (≥ 16, so both deselect internally).
  The earlier schematic (`TURBOSOU.PNG`) has R and S tied high: power-up chip undefined.

| Item | Hardware (6-bit compare) | Unreal (CHRV scheme) | MAME `ay/slot.cpp` | unreal-ng `soundchip_turbosound.cpp:438` | Shiru article |
|---|---|---|---|---|---|
| Values that switch | `#FC-#FF` | `#F8-#FF` | `#FE`, `#FF` (not forwarded to the chip) | `#FE`, `#FF` (also written into the address latch) | `#FE`, `#FF` |
| Chip at reset | first (`#FF`) | - | index 0 = the `#FE` chip | - | first |

- **Functions:** `ay-socket`. **Conflicts:** software writing register numbers `#FC-#FF` (none known). Only chip 0's
  I/O port pins reach the connector.
- **Sources:** local collection `hardware/turbo-sound/`; [Shiru, "Programming Turbo Sound", Info Guide #8](https://zxpress.ru/article.php?id=8612);
  [MAME ay/slot.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/ay/slot.cpp);
  [Unreal io.cpp](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/io.cpp).

#### Power of Sound TurboSound ("Turbo-Sound port by Bitwalker")

A full edge interface with its own decode: AY-3-8912 + AY-3-8910 and a Kempston joystick
(local collection `hardware/turbo-sound/TURBO-AY.ZIP`).

| Function | Decode | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| AY select / read | A15, A14, A1, /M1 | `#C002/#C000` | `#FFFD` | W, R | no | none |
| AY data | A15, A1 (A14 not used for BDIR) | `#8002/#8000` | `#BFFD` (and `#FFFD` writes) | W | no | none |
| Chip select | A7-A4, A1, A0 + /DOS | `#00F3/#0013` | `#1F` (`#13 #17 #1B`) | W: D0 = chip | no | off when /DOS low |
| Kempston joystick | same | `#00F3/#0013` | `#1F` | R | no | off when /DOS low |

Functions `ay-socket`, `kempston-joystick`. On a 128K / Pentagon it doubles the board AY unless that chip is
removed. Conflicts: SounDrive mode 1 `#1F` (sample writes flip the chip select); a second Kempston (`#1F` read fight);
Beta `#1F` protected by /DOS. Open: flip-flop reset wiring.

#### Quadro-AY ("Квадрасистема", Amazing Soft Making)

Decoder K555ID7 on A12, A15, /WR, enabled by /IORQ, A1 = 0, /M1 high (local collection `TURBO-AY.ZIP`,
`quadro-ay.gif`). Chip TWO: `#D002/#D000` (`#FFFD`) and `#D002/#9000` (`#BFFD`); chip ONE: `#D002/#C000` (`#EFFD`)
and `#D002/#8000` (`#AFFD`). Disagreement: original text `#EFFD`, Shiru `#EEFD` (likely typo), Unreal QUADRO scheme
chip = A12. It must replace the host decode (installation unconfirmed). No known software.

### 5.3 TurboSound FM (NedoPC, 2 × YM2203)

Repo normative document: `docs/inprogress/2026-09-10-turbosound-fm/hardware-reference.md` (CPLD 2006 / 2022 sources
in its `materials/`).

- **Bus:** `ay-socket` (40 or 28 pin); inherits the host decode; signals BDIR, BC1, BC2, A8, /A9, /RES, CLK. No
  oscillator: YM clock = 2 × socket clock. No IORQGE, no gating.
- **Control byte [RTL]:** address-phase write with DA7-DA3 = `11111` (`#F8-#FF`); it does not reach the YMs. Bit 0
  chip, bit 1 read mode (0 status, 1 register), bit 2 FM mute. Reset = `#FE` (first chip, register read, FM muted).
- **Old conventions:** rev A used `#FC` / `#FD`; TFM Compiler US031DX used `#FE` / `#FF` for status.
- **Functions:** `ay-socket` (FM included).

### 5.4 "TSFM Pro"

No board of that name was found. It is the ZX-Art / tracker name for the "TurboSound FM + SAA1099" target
([ZX-Art example](https://zxart.ee/tune/604694)). Boards that play it: ZXM-SoundCard (rev 00-03, Extreme),
ZX-MultiSound, and Zaxon's "Turbosound FM and SAA1099" (2 × YM2203 + SAA1099 on the 48K / 128K edge;
[Jungsi](https://www.jungsi.de/turbosound-fm-and-saa1099-sound-card-retro-sinclair-zx-spectrum/): "on the 128K the
internal AY must be disabled or removed (?)", "SAA does not work with Timex"; decode unknown, speccy.pl blocked
scripted fetch). **Recommendation:** no `tsfm-pro` card id; model the Zaxon board as its own card when sources appear.

### 5.5 ZXM-SoundCard family (micklab, M. N. Tarasov "Mick")

The author is Tarasov (CPLD headers), not Karimov.

| Revision | Content |
|---|---|
| 00 / 01 / 02 | TSFM (2 × YM2203) + SAA1099, NemoBus; rev 02 small SMD board |
| 03 | + jumper ENFD (TSFM part off); CPLD sees only A0, A1, A8, A14, A15 + an external CSIO decode; IORQGE for YM **and** SAA |
| Light | SAA only |
| Middle | SAA + SounDrive (TLC7226) |
| Extreme | TSFM + SAA + SounDrive + YM clock modes (EPM7064) |
| Extreme rev01 | + A9-A11 so the `#04FF` / `#05FF` SAA ports work |
| Extreme rev02 / 03 | third-party batches (component changes only) |

**Extreme rev01 [RTL + SCH]:** NemoBus, 62-pin edge. Signals A0-A15, D0-D7, /IORQ, /MREQ, /M1, /RD, /WR, /RESET,
/DOS, /IODOS, IORQGE (74LVC1G125). Not used: /WAIT, /INT, /ROMCS, ±12 V. Crystals 8 MHz (SAA) and 7 MHz (YM / 2).

| Function | Decode lines | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| YM register / control | A15, A14, A7-A0 | `#C0FF/#C0FD` | `#FFFD`, `#DFFD`, `#C0FD` | W; R status / register | **reads only** | /M1, /MREQ |
| YM data | A15, A14, A7-A0 | `#C0FF/#80FD` | `#BFFD` | W | no | same |
| Clock / AY2 register | A15-A12, A7-A0 | `#F0FF/#F0FC` | `#FFFC` | W | no | same |
| SAA (JP1 = 1) | A15, A14, A7-A0; A8 = SAA A0 | `#C0FF/#00FF` | `#FF` data, `#1FF` address | W | no | /DOS **and** /IODOS (normal); ROM lock (PentEvo) |
| SAA (JP1 = 0) | + A11-A9 | `#CEFF/#04FF` | `#04FF`, `#05FF` | W | no | same |
| SounDrive mode 1 | A7, A5, A3-A0; ch = A6, A4 | `#00AF/#000F` | `#0F #1F #4F #5F` | W | no | same |
| SounDrive "mode 2" | full | `#00FF/#00FB` | `#FB` (channel D; `#B3` removed in v1.01) | W | no | same |
| SounDrive "mode 3" | full | `#00FF/#003F` | `#3F` + `#5F` (Profi-style) | W | no | same |

All three SounDrive port sets are decoded at once (no mode jumper).

- **Control byte:** top four bits `1111` (`#F0-#FF`), not passed to the YMs: bit 0 chip, bit 1 read mode, bit 2 FM
  mute, bit 3 SAA clock off. Reset: chip 0, register read, FM muted, SAA off. micklab examples: SAA on `#F6`, off `#FE`.
- **`#FFFC`:** bit 0 clock mode (3.5 MHz / extended), bit 1 extended clock (2 MHz Amstrad / 4 MHz Atari ST), bit 7
  block AY2. Reset 0.
- **Jumpers:** JP1 "Port FF" (SAA `#FF` / `#04FF`), JP2 "PentEvo" (ignore /IORQ; replace /DOS by a lock on the
  last memory read from `#0000-#3FFF`), JP3 "Disable port XXFD" (YM part off).
- **Older revisions:** Extreme rev00 SAA = `#00FF/#00FF` (any A15 / A14), no `#04FF`. Rev 03: IORQGE on YM and SAA.
- **Functions:** Extreme `ay-socket` (shadows on reads only; writes reach the socket too), `saa`, `soundrive`,
  `covox-fb`; with JP3: `saa`, `soundrive`. Light `saa`; Middle `saa` + `soundrive`.
- **Conflicts:** socket AY / TSFM (both take every write; pointless pair); `#DFFD` paging (Profi, KAY, P1024: A13 not
  decoded, a page value ≥ `#F0` becomes a control byte [DER]); `#FFFC` has A0 = 0 (border write on A0-only ULAs
  [DER]); Ball Quest-style `#F0-#F7` register writes (same as MultiSound issue #11).
- **Sources:** [micklab ZXM-SoundCard page](http://micklab.ru/My%20Soundcard/ZXMSoundCard.htm) (TLS fails, plain
  HTTP works); CPLD sources in the same site's `file/zxm_soundcard/` folder (`zxm_soundcard_extreme01src0100.rar`,
  `..._extremesrc0100.rar`, `..._middlesrc0102.rar`, `..._lightsrc0101.rar`, `..._rev03src.rar`,
  `zxm_soundcard_extreme01.pdf`).

### 5.6 ZX-MultiSound (UzixLS, rev.A2)

Full detail in [the MultiSound hardware reference](../2026-10-03-zx-multisound/hardware-reference.md). Summary for
the matrix:

- **Bus:** NemoBus. **Requires +12 V** (b29) and IORQGE. /DOS, /IODOS, /WAIT wired but unused. Detects I/O as
  "RD or WR, no M1, no MREQ" (ignores /IORQ: "iorq_n are useless in zxevo").
- **Ports:** YM `#C00F/#C00D` (IORQGE only if A13 = 1: `#FFFD` ✓, `#DFFD` passive, co-written with the machine's
  `#DFFD`), YM data `#C00F/#800D` ✓, SAA `#00FF/#00FF` with A8 = SAA A0 (passive since 2023-12, ROM lock), GS `#B3`,
  `#BB` ✓ (**no `#33`**), SounDrive `#00AF/#000F` (passive, ROM lock).
- **Functions by DIP:** `ym` → `ay-socket` + `midi`; `saa`; `gs`; `sd` → `soundrive`.
- **ZX-Evo:** the FPGA's own ports (TurboSound on `#FFFD` / `#BFFD` among them) win by porthit (§2 item 1), so on an Evo the card's YM
  never answers unless the Evo firmware is built with `FREE_IORQ` or its TurboSound is disabled. The author tested on
  an Evo: **to verify** how (open item).

### 5.7 General Sound (classic, Stinger / X-Trade, 1995-97, boards v1.0-v1.4)

Z80 at 12 MHz, 32 KB ROM, 128 KB RAM (512 KB extension, 2 MB on later boards), 4 DACs, 37.5 kHz INT, mailbox.

- **Bus:** ZX-bus (2 × 30, keyed from v1.4). On a Sinclair edge the host must provide "IORQCE" (GS_INFO: cut Z80 pin
  20 through 330-470 Ω).
- **Signals [SCH, DOC]:** A0-A7, D0-D7, /WR, /IORQ, /M1 (**no /RD**: a read is any IORQ cycle with /WR and /M1
  high), /RESET (A20, to the card Z80's /RES per the v1.4 schematic), IORQGE (A13, PNP transistor), +5 V. Makes its
  own -5 V. No /DOS, /IODOS, /ROMCS, /WAIT, /INT, /NMI, +12 V.
- **Decode [SCH v1.4]:** 1533ИД7: A = A3, B = /WR, C = A2; enables /IORQ, A6 (low) and a diode-AND of A0, A1, A4,
  A5, A7, /M1 → `1011x011`, A2 = 0.

| Function | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|
| Data / output | `#00FF/#00B3` | `#xxB3` | W latch / R output | **reads only** | /M1 = 1 |
| Command / status | `#00FF/#00BB` | `#xxBB` | W command / R status (bit 7 data, bit 0 command) | reads only | /M1 = 1 |
| `#33` | none on the original | - | - | - | - |

- **Disagreements:**

| Item | Source A | Source B |
|---|---|---|
| `#33` | Unreal `io.cpp`, unreal-ng (`portdecoder_pentagon128.cpp:475` and others): decoded for every GS | schematic, GS manuals 1.03 / 1.4: not present |
| IORQGE | original: reads only | Black_Cat mod, ZXM-GS v1.02, NeoGS, MultiSound: every access |
| Host reset | v1.4 schematic: bus /RESET → GS /RES | manual: "load a module, reset the Spectrum ... work to the music" (maybe soft reset); unresolved |

- **Functions:** `gs`. **Options:** RAM size; overclock 14.4 MHz; firmware 1.04 / 1.05a / 1.05b / 1.10;
  Black_Cat mods (2013-15): full IORQGE + /RD, and a write-only `#33` latch that disables the GS ports and resets the
  card Z80 (bit layout unreadable in the source image).
- **Conflicts:** DivIDE (`#A3-#BF` contains `#B3`, `#BB`); SounDrive mode 2 loose decode; A2-only Covox.
- **Sources:** local collection `hardware/general-sound/GS.ZIP` (`GS_GENER`, `GS_INFO`, `GS_PORTS`), `GS104SRC.ZIP`
  (manual v1.03); [alfishe/GeneralSound](https://github.com/alfishe/GeneralSound) (`v.1.4/docs/gs14-sch.pdf`,
  programming manual v1.4, Black_Cat mods); BC IG #4; repo `docs/inprogress/2026-09-19-general-sound/`.

### 5.8 NeoGS (NedoPC, rev B 2008, rev C)

EP1K30 FPGA + CPLD, Z80 at 10-24 MHz, 2-4 MB RAM, 512 KB flash, MP3 decoder, SD.

- **Signals [RTL `zxbus.v`, DOC `GS_info_v0.4.2.2.txt`]:** A0-A7, D0-D7, /IORQ, /RD, /WR, IORQGE; DMA: A14, A15,
  /MREQ, /CSROM in, /RDROM out, /WAIT out; /RES via jumper J1. No /M1, /DOS, /IODOS, /INT, +12 V.

| Function | Mask / match | R/W | IORQGE | Gating |
|---|---|---|---|---|
| Data `#B3` | `#00FF/#B3` | W / R | ✓ on address match alone | none |
| Command / status `#BB` | `#00FF/#BB` | W / R | ✓ | none |
| Control `#33` | `#00FF/#33` | W: d7-d5 exact: `100` reset, `010` NMI, `001` LED toggle; R not driven, IORQGE still asserted | ✓ | none |
| ZX-DMA | memory `#0000-#3FFF`, /MREQ | card RAM replaces host ROM on reads (with /CSROM), takes writes | - | `dma_on`; drives ROM block and /WAIT |

- **Functions:** `gs` (+ `zxdma` as a ROM-block user). **Media:** SD (`sd.ngs`), flash (loader, ROM 1.09-1.11, FPGA).
- **Options:** J1 (host reset → card warm reset; without J1 the card keeps playing), RAM 2 / 4 MB, firmware.
- **Disagreement:** MAME `zxbus/neogs.cpp` acts on bits (bit 7 → reset, bit 6 → NMI, LED = bit 5 level); the RTL
  compares d7-d5 exactly and toggles the LED. IORQGE is address-only, so an IM2 INTA with PC low byte `#B3` / `#BB` /
  `#33` would assert it [DER].
- **Sources:** [alfishe/neogs](https://github.com/alfishe/neogs) (`fpga/current/zxbus/zxbus.v`, `docs/GS_info_v0.4.2.2.txt`);
  repo `2026-09-19-general-sound/` (`neogs-tdd.md` §3.4, `neogs-zxdma-design.md`), `2026-09-29-neogs-bringup/`.

### 5.9 ZXM-GeneralSound (micklab, rev 00 2014 / 01 2019 / 02.x 2021)

EPM7128 CPLD; 512 KB-2 MB RAM; 128 KB ROM with card-internal banking port `#0E`; TLC7528 (rev 02 TLC7225) DACs;
crystal 12 / 15 / 18 MHz; rev 01 separate reset.

- **Bus:** ZX-bus, 62-contact edge. **Signals:** A0-A7 (A8-A9 wired, unused), D, /IORQ, /MREQ, /RD, /WR, /M1,
  /RESET, IORQGE. `C_ENDOS` / `C_PEVO` declared but unused: no DOS gating.

| Function | Mask / match | R/W | IORQGE | Gating |
|---|---|---|---|---|
| GS data / command | `#00F7/#00B3` (= `#B3`, `#BB`) | R/W | ✓ (v1.02; earlier versions differ, not seen) | /M1 = 1; off while `#33` bit 4 = 1 |
| Control `#33` | `#00FF/#33` | W: bit 4 = 1 disables the card ports (reset 0) | **no** | /M1 = 1 |

- **Why `#33` bit 4:** "proposed by Black_Cat to avoid the DivIDE conflict", and NeoGS does not use bit 4, so NeoGS
  commands (`#80` / `#40` / `#20`) leave a ZXM-GS enabled.
- **Sources:** [micklab ZXM-GeneralSound](http://micklab.ru/My%20Soundcard/ZXMGeneralSound.htm); CPLD source
  [zxm_generalsound_dd2src0102.rar](http://micklab.ru/file/zxm_generalsnd/zxm_generalsound_dd2src0102.rar).

### 5.10 ZXM-MoonSound (OPL4 YMF278B, micklab, rev 00 2015, rev 01 2016)

- **Bus:** ZX-bus (2 × 31). **Signals [SCH rev 01]:** A0-A7, D0-D7, /RD, /WR, /MREQ, /IORQ, /M1, /RES, /DOS (A4),
  /IODOS (B20, pulled up on the card, so optional), IORQGE (A13, tri-state buffer), /INT (B13, optional via jumpers
  JP7 / JP8), +12 V and -12 V on the pin list (+12 V likely the +8 V analog rail; unconfirmed).

| Function | Decode | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| FM | A7-A2 = `110001` | `#00FC/#00C4` | `#C4` addr 1 / status, `#C5` data, `#C6` addr 2, `#C7` data | R/W | ✓ | off when /IODOS low, or /DOS low with JP1 open; also /M1 = 1, /MREQ = 1 |
| Wave | A7-A1 = `0111111` | `#00FE/#007E` | `#7E` addr, `#7F` data | R/W | ✓ | same |

- **Option JP1 "PentEvo":** fitted → /DOS ignored, only /IODOS gates; open → off whenever /DOS is low.
- **Functions:** `opl4`. **Media:** 2 MB YRW801 sample ROM in flash (rev 01 reprogrammable via MoonService v03+),
  1 MB SRAM.
- **unreal-ng difference:** the emulator decodes 16-bit exact ports and gates `#7F` reads by the chip's NEW2 bit
  (`soundchip_moonsound.h:43-70`); hardware compares A7-A0 and gates by /DOS / /IODOS.
- **Conflicts:** Profi palette `#xx7E` (whether Profi honors OUTIORQ for its palette is unconfirmed; the repo refuses
  the pair); Beta `#7F` (avoided by /DOS / /IODOS gating); ULA `#FE` family (`#C4`, `#C6`, `#7E` have A0 = 0) and
  Pentagon `#7FFD` partial decode (`#C5`, `#C7` with A15 = 0): IORQGE solves them on NemoBus; on an edge without
  IORQGE border / paging corruption; Quorum `#7E` [DER, unconfirmed].
- **Sources:** [alfishe/zxm-moonsound](https://github.com/alfishe/zxm-moonsound) (`hardware/firmware/.../dd2.tdf`,
  `hardware/schematics/zxm_moonsound_01.pdf`), [micklab ZXM-MoonSound](http://micklab.ru/My%20Soundcard/ZXMMoonSound.htm),
  repo `docs/inprogress/2026-09-13-moonsound/`, repo `docs/hardware/profi-1024.md`.

### 5.11 Covox

Write-only, passive, no DOS gating known in any variant.

| Variant | Port | Decode | Source |
|---|---|---|---|
| Classic homebrew Pentagon / ATM (ZX Format #5, 1996) | `#FB` | **A2 = 0 only** + /IORQ + /WR (one NOR gate) | [zxdn: LPT COVOX (ZX Format #5)](http://zxdn.narod.ru/hardware/zf5covox.htm); [dukeyusupov 2025](https://dukeyusupov.ru/2025/02/17/zx-covox.html) (A2-only clashes with DivMMC, fixed with a full decode) |
| Full decode | `#FB` | A7-A0 | dukeyusupov; ZX-Evo BaseConf `zports.v` (`covox_wr = (loa == COVOX)`, not in porthit → passive); Xpeccy `soundrive.c` |
| Unreal Speccy | `#FB` | effectively A2 = 0 after earlier arms | Unreal `io.cpp:631` |
| unreal-ng | `#FB` | `#FFFF/#00FB` and `#00FF/#FB` in two tables (inconsistent) | `portdecoder.cpp:728,767` |
| Scorpion | `#DD` | exact in Unreal / Xpeccy; Scorpion printer-port mod | [zxdn Scorpion mods](https://zxdn.narod.ru/hardware/dpt1scrp.htm) |
| Profi | `#5F` L / `#3F` R outside DOS (repo) vs `#DD`, `#BB55` (VELESOFT) | disputed | repo `docs/hardware/profi-1024.md`; [VELESOFT D/A for ZX](https://velesoft.speccy.cz/da_for_zx-cz.htm) |
| TS-Conf | `#FB` | board built-in | repo matrix §4 |

Functions: `covox-fb`, `covox-dd`. The A2-only decode co-receives every write with A2 = 0 (GS `#B3` / `#BB` / `#33`,
`#7FFD` family, `#EF`, DivMMC traffic): model as option `decode = full | a2`.

### 5.12 SounDrive 1.05 (Flash Inc., Novosibirsk, about 1994-95)

K555ИД7 + 4 × K555ИР23 + R-2R ladders; switch S1 "Soundrive / COVOX" selects the mode
([City #20, V. Kazakov](http://zxpress.ru/article.php?id=13608)). Write-only, passive. No schematic found.

| Mode | Ports | BC IG #4 decode | Emulator decode (Unreal, Xpeccy, unreal-ng, MultiSound RTL) | Channel |
|---|---|---|---|---|
| 1 (SounDrive) | `#0F` LA, `#1F` LB, `#4F` RC, `#5F` RD | only A0 = 1, A5 = 0 → `#0021/#0001` | `#00AF/#000F` | A4 + 2·A6 |
| 2 (COVOX) | `#F1` LA, `#F3` LB, `#F9` RC, `#FB` RD | only A0 = 1, A2 = 0 → `#0005/#0001` | `#00F5/#00F1` | A1 + 2·A3 |

- **Functions:** mode 1 `soundrive`; mode 2 `soundrive` + `covox-fb` (`#FB` = channel RD). **One mode at a time**
  (the current matrix lists both port sets at once: correct it to an option `mode`).
- **Loose-decode consequences [DER]:** mode 1 hears Beta `#1F` (command) / `#5F` (sector) in TR-DOS (clicks; no
  bus fight), Scorpion Covox `#DD`, Z-Controller `#57`, MoonSound `#C5` / `#C7`; mode 2 hears GS `#B3` (LB), `#BB`
  (RD), `#33` (LB), NemoIDE `#11` (LA). VELESOFT's note "`#F3` = LB (GS covox – port `#B3`)" supports it. Kempston
  `#1F` is read-only (no clash).
- **unreal-ng:** gates mode 1 behind "TR-DOS not paged" (`portdecoder.cpp:716-723`); real hardware has no gate.
- SounDrive v1.02 is a different card (8255, `#3F` / `#7F` control, A7 decoded).
- **Sources:** BC IG #4; [VELESOFT](https://velesoft.speccy.cz/da_for_zx-cz.htm); [zxdn SounDrive 1.51](http://zxdn.narod.ru/hardware/sd151inf.htm);
  repo `docs/inprogress/2026-09-23-sounddrive-quad-wiring/`, `2026-07-19-covox-audio-review/`.

### 5.13 Standalone SAA1099 cards

| Card | Port | Decode | Notes / source |
|---|---|---|---|
| SAM Coupé (origin of the convention) | `#FF` data, `#1FF` address | low byte `#FF`, A8 = SAA A0 | [SimCoupe SAMIO.h](https://github.com/simonowen/simcoupe/blob/master/Base/SAMIO.h) |
| Simple SAA IF (Jiiira / zxsparrow, 2014) | `#1FF` / `#FF` | unconfirmed (CPLD, no schematic) | [Simple SAA IF](https://zxsparrow.com/speccy_hw/simple/Simple_SAA_IF/Simple_SAA_IF_eng.html) |
| ZXM-SoundCard Light / Middle | `#FF` / `#1FF` | §5.5 | /DOS gated |
| Unreal Speccy | `#FF`, A8 = control | A15-A9 not decoded | Unreal `io.cpp` |

The SAA1099 has no reset pin: ZXM and MultiSound gate its clock via the control byte; a standalone card may keep
playing after a CPU reset (unconfirmed). Function `saa`. Conflicts: ZON X index, Timex `#FF`, TR-DOS `#FF` unless
gated, Scorpion attribute port `#FF` (A0, A1, A2, A5 = 1 decode).

## 6. Storage, DOS and NMI cards

### 6.1 Beta 128 / Beta Disk (TR-DOS)

Two families; model them as different cards: the **edge card** (Technology Research Beta Disk V2 / V3 / plus, pages at
`#3Cxx`; Beta 128, TR-DOS 5.x, pages at `#3Dxx`) and the **built-in** controller of Pentagon, Scorpion, ATM, KAY,
ZX-Evo (a machine device that drives /DOS on the ZX-bus).

- **Edge card signals:** /ROMCS (B25), /M1, /NMI (magic button on Beta plus / Beta 128), /RESET. It snoops `#7FFD`
  writes itself (the ROM-select bit is not on the edge): MAME `beta128.cpp` `(offset & #8002) == 0` latches D4. On
  the +3 edge /ROMCS is replaced by /OE ROM1 / ROM2 (MAME `exp.h`).

| Function | Decode lines | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| WD1793 registers | A7 = 0, A1 = A0 = 1; register = A6-A5 | `#0083/#0003` | `#1F #3F #5F #7F` | R/W | n/a on the edge | TR-DOS ROM paged |
| System register | A7 = 1, A1 = A0 = 1 | `#0083/#0083` | `#FF` | R (D7 INTRQ, D6 DRQ) / W (drive, side, HLT, /MR, DDEN) | n/a | same |
| Original Beta V2 / V3 / plus | adds A2 | `#0087/#0007`, `#0087/#0087` | `#1F`…, `#FF` | R/W | | always, unless disabled by a master-disable write (`#0003/#0000`, D7 = 1) |
| 128K ROM snoop (Beta 128) | A15 = 0, A1 = 0 | `#8002/#0000` | `#7FFD` | W (D4) | passive | always |

**Paging:** Beta 128 pages in on an M1 fetch from `#3D00-#3DFF` while the 48K ROM is selected (switch "normal" also
`#3Cxx`); original Beta on **any** access at `#3Cxx` (the `#FF` D7 latch holds it); out on an M1 fetch ≥ `#4000`.
Disagreement: MAME's Pentagon pages on any read including data reads; FUSE and Beta 128 by M1 only.

**Built-in decode by machine** (disagreements):

| Machine | Black_Cat (BC IG #4, repo `docs/ports/zx-ports-full-table.txt`) | MAME | FUSE | unreal-ng |
|---|---|---|---|---|
| Pentagon 128 / 1024, KAY, Scorpion | `0BAxxx11` | Pentagon: full low byte | full low byte | Pentagon: `#83/#03` + `#FF` full (avoids ATM `#F7`) |
| Scorpion (in DOS) | `0BAxxx11` | `0nnxxx11` | - | A2-A0 = `111` (MiSTer), also claims `#xxDF`, `#xxBF` |
| ATM | full low byte | | | |
| Quorum | `0BAx11x1` | | | |
| Profi | A15 = 0 + `0BAxxx11` | | | |

- **Functions:** `beta128` (`fdc.wd1793`). **Media:** FDD × 4 (V2: 3). **Options:** system switch off / normal /
  reset; TR-DOS ROM version; magic button.
- **Conflicts:** Kempston `#1F` (built-ins serve Kempston only outside DOS: unreal-ng `portdecoder_pentagon128.cpp:148`,
  ZX-Evo `zports.v` `KJOY && !shadow`, MAME Scorpion view 0); SounDrive in TR-DOS (§5.12); Multiface 1 (§6.7);
  Fuller / ZON X (§5.1); DivIDE / DivMMC (both trap `#3Dxx`).
- **Sources:** [MAME beta.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/beta.cpp),
  [MAME beta128.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/beta128.cpp),
  [MAME pentagon.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/pentagon.cpp),
  [MAME scorpion.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp),
  [FUSE beta.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/disk/beta.c),
  [FUSE z80_ops.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/z80/z80_ops.c).

### 6.2 DivIDE (and DivIDE Plus)

- **Bus:** 48K / 128K / +2 edge; +2A / +3 with jumper A. **Signals:** /ROMCS, /M1, /MREQ, /RFSH (trap in the refresh
  cycle), /RESET. No IORQGE needed: all ports have A0 = 1, so the ULA never answers (unconfirmed whether the board
  drives it).

| Function | Decode | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| ATA command block | A7 = 1, A6 = 0, A5 = 1, A1 = A0 = 1; register = A4-A2 | `#00E3/#00A3` | `#A3` (data, 16-bit toggle) … `#BF` | R/W | no | none |
| Control | full low byte | `#00FF/#00E3` | `#E3` (CONMEM, MAPRAM, bank) | W | no | none |
| DivIDE Plus mode | unconfirmed | ? / `#17` | `#17` | W | ? | |

Source: [DivIDE programming model](http://divide.speccy.cz/files/pgm_model.txt) ("decoded using A0..A7 only");
FUSE `divide.c` uses the same mask; Unreal uses `#A3/#A3` (too wide; repo `2026-09-28-storage-controllers-survey/divide-divmmc-esxdos.md` §2.1).

- **Automap:** after the fetch at `#0000`, `#0008`, `#0038`, `#0066`, `#04C6`, `#0562`; instantly at `#3D00-#3DFF`;
  off at `#1FF8-#1FFF`. Needs jumper E (EEPROM) or MAPRAM; MAPRAM survives /RESET.
- **Functions:** `divide` (automap / ROM override), `ide.divide`. **Media:** IDE × 2, 8 KB EEPROM, 32 KB RAM
  (Plus: 512 KB RAM + 512 KB flash).
- **Options:** jumper E, jumper A, firmware (esxDOS, FATware, MDOS3, +DivIDE); Plus modes via `#17` and a `#7FFD` D4
  latch that blocks automap in 128 BASIC ([RWAP notes](https://www.rwapsoftware.co.uk/spectrum/spectrum_divide_notes.html)).
- **Conflicts:** Beta 128 (both trap `#3Dxx`, DivIDE maps instantly, TR-DOS unreachable while automap is on; both
  drive /ROMCS); GS family (`#B3`, `#BB` inside `#A3-#BF`; the reason for ZXM-GS `#33` bit 4); Multiface (both page
  at `#0066`); DivIDE Plus `#17` vs MB-02.

### 6.3 DivMMC (and DivMMC EnJOY! / PRO ONE)

| Function | Mask / match | Port | R/W |
|---|---|---|---|
| Control | `#00FF/#00E3` | `#E3` | W |
| SPI chip select | `#00FF/#00E7` | `#E7` (D0 card 0, D1 card 1, active low) | W |
| SPI data | `#00FF/#00EB` | `#EB` | R/W |
| EnJOY! joystick | Kempston-style (mask unconfirmed) | `#1F` | R |

Bus and automap as DivIDE; passive. Functions `divmmc`, `sd.divmmc` (EnJOY!: + `kempston-joystick` / Sinclair /
cursor / Fuller, DIP selectable). Media SD × 1-2, EEPROM, 128-512 KB RAM. Options: DIP 1 esxDOS on / off, joystick
mode. The PRO ONE manual: conflicts with the Investronica 48K+ joystick; on the +2A / +3 its Sinclair ports switch off.
Sources: [FUSE divmmc.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/ide/divmmc.c),
[EnJOY! PRO ONE manual](https://www.bytedelight.com/wp-content/uploads/2020/06/DivMMC-EnJOY-PRO-ONE-Manual.pdf).

### 6.4 Nemo IDE and Nemo-A8 (NemoBus)

- **Signals [SCH]:** A0-A7 (A8 on the A8 variant), D0-D7, /IORQ, /RD, /WR, /M1, /DOS (4A), IORQGE (13A), /RES (20A).
  No /ROMCS, /NMI, /WAIT.
- **Logic** (local collection `hardware/hdd-ide/NEMOHDD.ZIP` → `NEMOIDE.TXT`): board enable = A1 = 0, A2 = 0, /M1 =
  1, **/DOS = 1** (IORQ not in the term); IORQGE driven from the board enable; strobe decoder on A0, /RD, /WR enabled
  by IORQ (A0 = 0 → task file, A0 = 1 → high-byte latch); /CS0 = A3, /CS1 = A4; register = A7-A5. The A8 variant
  (Kirill Frolov) feeds A8 instead of A0 to the strobe decoder.

| Function | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|
| Whole-board claim | `#0006/#0000` | every port with A2 = A1 = 0 | - | **✓ all** | outside DOS |
| Task file (CS0) | `#001F/#0010` | `#10 #30 … #F0` | R/W | ✓ | same |
| Control block (CS1) | `#001F/#0008` | `#C8` (register 6) | R/W | ✓ | same |
| High-byte latch (Nemo) | `#0007/#0001` | `#11` (any A3-A7) | R/W | ✓ | same |
| High-byte latch (A8) | A8 = 1, A2 = A1 = 0 | `#110`… | R/W | ✓ | same |

| Item | Schematic | Black_Cat | MAME `nemoide.cpp` | Unreal / unreal-ng | ZX-Evo built-in |
|---|---|---|---|---|---|
| Latch | A0 = 1, A2 = A1 = 0 | `xxxxx001` | exactly `#11` | A0 = 1, A2 = A1 = 0 (`ideadapter.cpp:131-185`) | exactly `#11` |
| DOS gate | outside DOS | | Scorpion only (non-DOS view) | outside DOS | **none** |

- **Functions:** `ide.nemo`. **Media:** IDE × 2 (HDD / ATAPI). **Options:** latch on A0 / A8.
- **Conflicts:** with IORQGE honored it shadows the Pentagon `#FE` (A0-only decode), `#7FFD` mirrors (`OUT (C)`
  with B = 0 on `#0010`), A5-only Kempston on `#10`, `#11`, `#50`, `#90`, `#D0`, `#C8`, and any other card's port with
  A2 = A1 = 0 outside DOS [DER]. Without IORQGE (128K edge via adapter; ZX-Evo where the board wins) IDE writes also
  hit border / paging: unrealistic fit.
- **Sources:** local collection `hardware/hdd-ide/NEMOHDD.ZIP`; [MAME nemoide.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/zxbus/nemoide.cpp);
  repo `2026-09-28-storage-controllers-survey/profi-atm-nemo.md`, `2026-09-28-ide-atapi/`.

### 6.5 SMUC (Scorpion)

Repo consensus document: `docs/inprogress/2026-09-28-scorpion-smuc/hardware-reference.md`. Summary:

- **Bus:** Scorpion bus. The original v1.0 does **not** work on NemoBus / ZX-Evo without a hardware mod
  ([zxpress, SibNews #08](https://zxpress.ru/article.php?id=18278)). **Signals:** /DOS (gate), A15, A13-A11, A10-A8
  (register), A7-A5, A2-A0, /INT or /NMI (8259 via `#FFBA` D3), ISA8 window.

| Function | Mask / match (`#B8E7`) | Port | R/W |
|---|---|---|---|
| Version / revision | `#18A2`, `#18A6` | `#5FBA`, `#5FBE` | R |
| Virtual FDD | `#38A2` | `#7FBA` | R/W |
| 8259 (absent on 2.x) | `#38A6` (A8 = 8259 A0) | `#7EBE` / `#7FBE` | R/W |
| DS1685 RTC | `#98A2` | `#DFBA` (address / data by `#FFBA` D7) | R/W |
| IDE high latch | `#98A6` | `#D8BE` | R/W |
| System register | `#B8A2` | `#FFBA` | R/W |
| IDE registers | `#B8A6`, register = A10-A8 | `#F8BE-#FFBE` | R/W |
| ISA I/O `#200-#3FF` | `0ED11CBA111GF110` | `#18E6-#7FFE` | R/W |

- **Gating:** DOS on v1.x; the v2.0 clone opens all ports without TR-DOS (option). **IORQGE:** unconfirmed; all ports
  have A5 = 1, A1 = 1, A0 = 0, which matches the Scorpion `#FE` decode `xx1xxx10` (MAME leaves `#FE` unmapped in the
  DOS view).
- **Functions:** `ide.smuc`, `rtc.ds1685`, `nvram.24c16` (+ `pic.8259`, `isa8`). **Media:** IDE × 2, 2 KB EEPROM,
  RTC CMOS. Version register values disagree between emulators. [MAME smuc.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/zxbus/smuc.cpp).

### 6.6 Z-Controller (KOE, 2007)

A multifunction ZX-bus card, not only SD: PS/2 keyboard (emulates the matrix, blocks the mechanical keyboard), PS/2
mouse as Kempston mouse, Nemo-compatible IDE, SD over SPI, F11 = NMI, F12 = RESET. CPLD EPM7128 sees A0-A15, /M1,
/DOS, IORQGE, /NMI, /RES; no /WAIT. CPLD source never published.

| Function | Mask / match | Port | R/W | IORQGE | Gating |
|---|---|---|---|---|---|
| SD config | `#00FF/#0077` | `#77` | W: D0 power, D1 /CS; R per KOE: D0 = 0 card present, D1 = 1 read-only (Unreal and ZX-Evo return `#00`) | likely (unconfirmed) | unconfirmed; ZX-Evo: outside shadow only, `#8057` in shadow |
| SD data | `#00FF/#0057` | `#57` | R/W (read returns the previous byte and starts a new exchange) | likely | ZX-Evo: never gated |
| Kempston mouse | `#xxDF` family | | R | ? | ? |
| Keyboard | `#FE` | | R | must (replaces the matrix) | ? |
| Nemo IDE | §6.4 | | | | |

Unreal: write `(port & #FF) == #57`, read `(port & #DF) == #57`, no DOS gate. Functions `sd.zc`, `ide.nemo`,
`kempston-mouse`, `ps2-keyboard`, `nmi`; treat the SD-only Z-Controller (ZX-Evo built-in, [ZX-SDC](https://github.com/7FFD/ZX-SDC))
as a separate card. Sources: [KOE description (Wayback)](http://web.archive.org/web/2016/http://pentagon.nedopc.com/info.htm)
(the live page is 404), [romychs/zcontroller](https://github.com/romychs/zcontroller) (schematic reconstruction).

### 6.7 Multiface One / 128 / 3 (Romantic Robot)

NMI button; 8 KB ROM at `#0000` + 8 KB RAM at `#2000` (MF1 v1: 2 KB); MF1 / MF128 on the 48K / 128K edge, MF3 on
the +3 edge (/OE ROM1 / ROM2).

| Model | Page in (IN) | Page out (IN) | OUT | Joystick | Snoop | MAME decode | FUSE decode |
|---|---|---|---|---|---|---|---|
| MF1 early | `#9F` | `#5F` | `#5F` NMI clear | `#1F` | - | full byte | |
| MF1 late | `#9F` | `#1F` | `#1F` NMI clear | `#1F` (J1) | - | `#00F2/#0012`, `#0092` | `#0072/#0012` |
| MF128 v1 | `#9F` | `#1F` | `#1F` hide, `#9F` NMI clear | - | `#7FFD` D3 | `#00F0/#0010`, `#0090` | |
| MF128 v2 | `#BF` | `#3F` | `#3F` hide, `#BF` NMI clear | - | as v1 | `#00F4/#0034`, `#00B4` | `#0072/#0032` |
| MF3 | `#3F` | `#BF` | `#3F` hide, `#BF` NMI clear | - | `#xFFD` (`#90FF/#10FD`) | full byte (PAL) | `#0072/#0032` |

Functions `multiface` (+ MF1 `kempston-joystick`). No media. Options: model / ROM, J1, hide. **Worth modeling:** low
priority (games probe it, e.g. reads of `#9F`); after DivMMC. Conflicts: MF1 vs Kempston (`#1F`, J1 disables the MF
joystick); MF1 vs Beta (works only Spectrum → MF1 → Beta with J1 open, Beta V3 / plus; not Beta 128); MF128 v1 vs
DISCiPLE / +D (fixed in v2); MF1 / MF128 on +2A / +3: do not work. Sources:
[MAME mface.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/mface.cpp),
[FUSE multiface.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/multiface.c).

## 7. Network and serial cards

### 7.1 ZXNETUSB (NedoPC, rev C; EPM3128A, W5300 + SL811HS)

- **Signals [RTL `top.v`]:** A0-A15, D0-D7, /IORQ, /RD, /WR, /MREQ, /RESET, /CSROM in, /INT (open collector out),
  IORQGE (driven high), ROM block out (presumably RDR/ 15A; inferred). No /DOS, /IODOS, /M1, /WAIT, +12 V.

| Function | Decode | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| Card select | A7-A0 | `#00FF/#00AB` | `#xxAB` | - | **✓ address only** (no M1, no IORQ) | none |
| Control (INT / reset) | A15 = 1, A9-A8 = 11 | `#83FF/#83AB` | `#83AB` | R/W | ✓ | none |
| Mode | A15 = 1, A9-A8 = 10 | `#83FF/#82AB` | `#82AB` (bit 7 VBUS, bit 6 M/S) | R/W | ✓ | none |
| W5300 address high | `#83FF/#81AB` | `#81AB` | R/W | ✓ | none |
| SL811 address | `#83FF/#80AB` | `#80AB` | W | ✓ | none |
| W5300 / SL811 data window | A15 = 0 | `#80FF/#00AB` | `#00AB-#7FAB` (PRM: `#40AB-#7FAB`) | R/W | ✓ | `#82AB` bit 4 picks the chip |
| Memory window | `#0000-#3FFF` while `#82AB` bit 2 | memory | W always; R with /CSROM | ROM block | |

- PRM: "IO address decoded from bits [7:0]; IORQGE is generated from the address only".
- **Functions:** `net.zxnetusb` (+ `usb.sl811`, so a second card is refused). **Options:** CPLD builds
  (`revC_newports`, `p2666fix_*`, ...; same base address). INT: level, no vector (PRM recommends a 257-byte IM2 table).
- **Conflicts:** other `#xxAB` users (KAY Kjoy A0-only decode); Beta `#FF` system register decode `1xxxxx11`
  matches `#AB` in DOS [DER]; address-only IORQGE asserts during an INTA with PC low byte `#AB` (drives no data) [DER].
- **Sources:** [lvd2/zxnet_usb](https://github.com/lvd2/zxnet_usb) (`cpld/rtl/zbus.v`, `doc/revC/zxnetusb_prm_revC.odt`);
  repo `docs/inprogress/2026-09-30-nedoos-integration/reference-w5300-model.md` §1.3, `2026-10-02-sprinter-network/`.

### 7.2 ZX-WiFi (izzx; 16C550 + ESP-12E/F + MAX3232)

- **Signals:** A0-A10, D0-D7, /IORQ, /RD, /WR, /M1, /RESET (v1.4+), IORQGE (v1.2+), /MREQ (v1.6, unused). No /INT,
  /WAIT, +12 V (from schematic net labels; not traced).

| Function | Decode | Mask / match | Ports | R/W | IORQGE | Gating |
|---|---|---|---|---|---|---|
| 16550 registers | A7-A0 = `#EF` (or `#EE`), A10-A8 = register, A15-A11 not decoded | `#00FF/#00EF` | `#F8EF` RBR/THR … `#FFEF` SCR | R/W | v1.0-1.1 none; v1.2+ ✓; v1.6 GAL var. 1 `!M1 & IORQ & addr`, var. 2 `!M1 & addr` | none |

- **Functions:** `serial.ef` (or `serial.ee`). **Options:** `#EF` / `#EE` (jumper v1.3-1.5, GAL v1.6), crystal
  1.8432 / 14.7456 MHz, SW1 ESP / RS-232, board revision (IORQGE). Presence test `IN 239` = 0 with the card.
- **ReadMe conflicts:** ZX-Evo (port exists on the board → use `#EE` with BaseConf); ZXMC-1/2 (same port); Scorpion
  Turbo+ (needs v1.2+ IORQGE or the board's `#FF` chip DD53 disabled: the Scorpion `#FF` decode A0, A1, A2, A5 = 1
  matches `#EF`); TS-Conf (`#EF` is porthit → card never selected; reset issues → do not fit diode U8). Tested on
  Scorpion Turbo+ / GMX (with SMUC and GS), +2A, KAY-1024, Evo rev C BaseConf with `#EE`.
- **Other `#EF` / ESP devices:** ZiFi (TS-Conf built-in; `#00EF-#BFEF` data, `#C0EF-#C9EF` regs, `#F8EF-#FFEF` 16550;
  machine function; repo `2026-10-02-tsconf-zifi/reference-zifi.md`); ZX-Evo COM (BaseConf AVR; repo
  `reference-evo-com-port.md`); ATM2IOESP (ATM internal connector `#FB` / `#FA`, not a bus card; repo
  `2026-10-02-atm2ioesp/`); AY-UART (no bus port: AY R14 port A bit-bang, needs AY port A wired;
  [nihirash/zx-net-tools ay-uart.asm](https://github.com/nihirash/zx-net-tools/blob/master/uGophy/ay-uart.asm));
  other `#EF` users: Interface 1, +D, C-DOS modem, Kondratyev ISA COM (`xxxxA000xxx0xxxx`, very loose).
- **Sources:** [izzx-git/ZX-WiFi](https://github.com/izzx-git/ZX-WiFi) ([ReadMe](https://github.com/izzx-git/ZX-WiFi/blob/main/ReadMe.txt),
  [v1.6 GAL](https://github.com/izzx-git/ZX-WiFi/tree/main/v1.6/GAL)).

## 8. Input cards

### 8.1 Kempston joystick

Read only; D0-D4 = right, left, down, up, fire, active high; D5-D7 = 0 in MAME / FUSE (unconfirmed on the original).

| Variant | Decode | Mask / match | Notes / source |
|---|---|---|---|
| Original 1-chip (74LS365) | /IORQ + A5 | `#0020/#0000` | no /RD: also drives the bus in INTA, breaks IM2 ([MDFS](https://mdfs.net/Info/Comp/Spectrum/Joystick/)) |
| 2-chip (74LS366 + 74LS32) | /IORQ, /RD, A7, A6, A5 | `#00E0/#0000` | MDFS |
| BC IG #4 "KempstonIF" | A5 = 0 | `#0020/#0000` | |
| KAY-1024 built-in | A0 = 1 | `#0001/#0001` | answers every odd port |
| Pentagon-1024SL built-in | A5 = 0, A0 = 1 | `#0021/#0001` | |
| Quorum | A7 = 0, A4 = A3 = 1, A0 = 1 | `#0099/#0019` | |
| Scorpion ZS256 Turbo+ | A7 = 0, A5 = 0, A1 = A0 = 1 | `#00A3/#0003` | |
| "Leningrad" clone | odd ports | `#0001/#0001` | local collection `hardware/ZX_EXTEN.LZH` |
| FUSE external / TC2048 | A7-A5 = 0 / A5 = 0 | `#00E0` / `#0020` | [FUSE joystick.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/joystick.c) |
| MAME `kempjoy` | A7-A5 = 0 | `#00E0/#0000` | [kempjoy.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/kempjoy.cpp) |
| ZX-Evo BaseConf / TS-Conf | `#1F` full, outside shadow / DOS | `#00FF/#001F` | mainboard (porthit) |
| unreal-ng | Pentagon / Evo `#00FF/#001F` outside DOS; Scorpion `#FFFF/#001F` | | `portdecoder.cpp:678,755` |

- BC IG #4 lists no Kempston for the original Pentagon 128 (unconfirmed whether it had one).
- **Functions:** `kempston-joystick`. **Conflicts:** Beta `#1F` (and `#5F` for A5-only) in DOS unless gated;
  Multiface 1 (`#1F`); Kempston mouse (`#DF`: A5 = 0, A0 = 1 is inside A5- / A0-only decodes); PoS TurboSound `#1F`;
  no conflict with the ULA `#FE` (A5 = 1). SounDrive `#1F` is write-only (no clash).

### 8.2 Kempston mouse

| Variant | Buttons | X | Y | Notes / source |
|---|---|---|---|---|
| Standard (BC IG #4) | A9 = 1, A8 = 0, A5 = 0 | A10 = 0, A9 = A8 = 1, A5 = 0 | A10 = A9 = A8 = 1, A5 = 0 | repo `2026-09-12-kempston-mouse/hardware-reference.md` §2; zxsp agrees |
| USSR / Kondratyev 1994 | A10 = 0, A8 = 0, A7 = 1, A5 = 0, A0 = 1 | A10 = 0, A8 = 1, ... | A10 = 1, A8 = 1, ... | "the computer must block port `#DF`" (no IORQGE); local collection `hardware/KEMPMOUS.LZH` |
| FUSE | A8 = 0, A5 = 0, A0 = 1 | A10 = 0, A8 = 1, A5 = 0, A0 = 1 | A10 = 1, A8 = 1, ... | masks `#0121` / `#0521` ([kempmouse.c](https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/peripherals/kempmouse.c)) |
| MAME | `#FADF` exact | `#FBDF` | `#FFDF` | [kempmouse.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/kempmouse.cpp) |
| ZX_BUS_Mouse v1.2 (proboterror) | A0 = A1 = A7 = 1, A5 = 0, **A15 = 1**, A8 = 0, A10 = 0 | A8 = 1, A10 = 0 | A8 = 1, A10 = 1 | **IORQGE ✓** from A0, A1, A7, /A5, A15, M1; A15 added for the Scorpion ProfROM ([repo](https://github.com/proboterror/ZX_BUS_Mouse)) |
| K-Mouse Turbo (Velesoft) | A15 = master / slave, A10 = 0, A8 = 0, `#DF` | A8 = 1 | A10 = 1, A8 = 1 | `#7EDF` / `#FEDF` detect ([Velesoft](https://velesoft.speccy.cz/kmturbo-cz.htm)) |
| ZX-Evo | `#DF` full + A8 / A10, also in DOS | | | porthit |
| unreal-ng | MiSTer: A5-A0 = `011111`, A9 = 1, A8 / A10 | | | `portdecoder.cpp:1256-1274` |

Buttons D0-D2 = L, R, M active low; D3 = 1; D4-D7 = wheel (`1111` when absent). Functions `kempston-mouse`.
Conflicts: Kempston joystick overlap; Beta `#FF` decode `1xxxxx11` matches `#DF` in DOS [DER]; ZX-Evo / TS-Conf (card
dead, board mouse answers); Scorpion ProfROM (ZX_BUS_Mouse v1.1 without A15 breaks the ROM disk).

## 9. Pairwise conflicts

"Real-world result" is from a source; rows marked [DER] are derived from the decodes, with no report of the effect.

| Card A | Card B | Real-world result | Source |
|---|---|---|---|
| Any IORQGE card | ZX-Evo / TS-Conf built-in on the same port | **Card never sees the cycle; the board wins** (porthit). Shadowing of board devices is impossible | ZX-Evo RTL `zbus.v` |
| ZX-MultiSound (`ym`) | ZX-Evo FPGA TurboSound | Board TurboSound answers; the card's YM is silent [DER from porthit]; the author's "useless in zxevo" comments | ZX-Evo RTL; MultiSound `top.v` |
| Melodik / AY-Magic | 128K / Pentagon board AY | Both chips take every write; `#FFFD` read fight | Melodik schematic; FUSE restricts Melodik to 16K / 48K |
| NedoPC TS / TSFM (socket) | ZXM-SoundCard Extreme, ZX-MultiSound | One AY role; ZXM shadows reads only (writes reach both), MultiSound shadows reads and writes on `#FFFD` / `#BFFD` | ZXM CPLD; MultiSound RTL |
| NedoPC TS (`#FC-#FF`) | TSFM (`#F8-#FF`), ZXM / MultiSound (`#F0-#FF`) | Same software behaves differently (Ball Quest clicks on MultiSound) | schematics, CPLDs, MultiSound issue #11 |
| PoS TurboSound (`#1F` select) | SounDrive mode 1 / ZXM SounDrive (`#1F`) | Sample writes flip the AY chip select [DER] | Bitwalker schematic; ZXM CPLD |
| PoS TurboSound | Kempston joystick card | `#1F` read fight (the PoS has its own Kempston) | Bitwalker schematic |
| PoS TurboSound | Beta-128 | No conflict (select gated by /DOS) | Bitwalker schematic |
| Quadro-AY | host AY decode | Host AY also answers `#EFFD` / `#AFFD` unless replaced [DER] | Quadro schematic |
| Fuller Box | DK'tronics 3-channel | Same ports; exclusive | nocash |
| Fuller Box | Beta-128 | `#3F` / `#5F` TR-DOS writes program the AY; `#7F` read fight [DER] | WoS / nocash ports |
| ZON X | SAA card, SpecDrum, Fuller, Beta | Loose decode co-receives their writes [DER] | zxsp decode |
| Any `#FF` SAA card | Timex TC2048 / TS2068 | SAA unusable | Jungsi |
| ZXM-SoundCard Extreme | `#DFFD` paging (Profi, KAY, P1024) | Page value ≥ `#F0` becomes a YM control byte [DER] | ZXM CPLD |
| ZXM-SoundCard Extreme `#FFFC` | ULA with A0-only decode | Border / beeper write [DER] | ZXM CPLD |
| ZX-MultiSound `#DFFD` | machine `#DFFD` | Both latch the write (intended); read is a bus fight | MultiSound RTL comment |
| GS / ZXM-GS / NeoGS | DivIDE | `#B3` / `#BB` clash; ZXM-GS `#33` bit 4 lets software switch the GS off | micklab ZXM-GS page; BC IG #4 |
| GS family | each other; MultiSound with `gs` | Same mailbox ports | function `gs` |
| NeoGS `#33` | ZXM-GS `#33` | Designed to coexist (bits 7-5 vs bit 4) | micklab; NeoGS `zxbus.v` |
| Classic GS | software writing `#33` | No effect (port absent) | GS schematic, manuals |
| GS family | SounDrive 1.05 mode 2 | GS traffic audible on LB / RD [DER, VELESOFT note] | BC IG #4; VELESOFT |
| GS family | Covox with A2-only decode | GS writes leak into the Covox [DER] | ZX Format #5 |
| ZXM-MoonSound | Profi palette `#7E` | Port clash; whether OUTIORQ helps is unconfirmed; repo refuses | `docs/hardware/profi-1024.md`; MoonSound CPLD |
| ZXM-MoonSound | Beta-128 `#7F` | Avoided by /IODOS (and /DOS with JP1 open) | MoonSound CPLD |
| ZXM-MoonSound | ULA `#FE` / Pentagon `#7FFD` partial decode | Solved by IORQGE on NemoBus; corruption on an edge without IORQGE | MoonSound CPLD; repo port decoders |
| ZXM-MoonSound | SounDrive mode 1 (loose) | `#C5` / `#C7` land in channel RC [DER] | BC IG #4 |
| Covox `#FB` | SounDrive mode 2 | Same function `covox-fb` | VELESOFT |
| Covox (A2-only) | DivMMC | SD traffic audible; fixed by a full decode | dukeyusupov 2025 |
| SounDrive mode 1 | Beta-128 in TR-DOS | Clicks on disk command / sector writes; no bus fight | BC IG #4 (emulators gate it out) |
| SounDrive mode 1 | Scorpion Covox `#DD`, Z-Controller `#57` | Writes leak [DER] | BC IG #4 |
| Beta (built-in) | Kempston joystick | Coexist: Kempston only outside DOS | unreal-ng `portdecoder_pentagon128.cpp:148`; ZX-Evo `zports.v`; MAME Scorpion |
| Beta 128 (edge) | DivIDE / DivMMC | Both trap `#3Dxx`; Div maps instantly → TR-DOS unreachable while automap is on; both drive /ROMCS | repo `divide-divmmc-esxdos.md` §3 |
| Beta V3 / plus | Multiface One | Works only Spectrum → MF1 → Beta with J1 open; not with Beta 128 | MAME `mface.cpp` |
| Multiface (any) | DivIDE / DivMMC | Both page at `#0066` after NMI; expected /ROMCS conflict (unconfirmed) | - |
| Multiface 1 | Kempston interface | `#1F` fight unless J1 disables the MF joystick | MAME / FUSE |
| Multiface 128 v1 | DISCiPLE / +D | Port clash (fixed in v2) | MAME `mface.cpp` |
| Multiface 1 / 128 | +2A / +3 | Does not work (edge pinout) | MAME `mface.cpp` |
| Nemo IDE | Pentagon `#FE`, `#7FFD`, A5-only Kempston | Shadowed by IORQGE (intended) | `NEMOIDE.TXT`; [zxpress 11759](https://zxpress.ru/article.php?id=11759) |
| Nemo IDE card | ZX-Evo built-in NemoIDE / Z-Controller | Same function `ide.nemo`; on the Evo the board wins | KOE doc; ZX-Evo RTL |
| Nemo IDE | SMUC | No address overlap (SMUC A1 = 1); opposite DOS gates | decodes |
| SMUC v1.0 | NemoBus machines / ZX-Evo | Does not work without the mod | [zxpress 18278](https://zxpress.ru/article.php?id=18278) |
| Z-Controller card | ZX-Evo / TS-Conf | Built-in `sd.zc` / NemoIDE win (porthit) | ZX-Evo RTL |
| DivIDE Plus | MB-02 | `#17` shared | RWAP notes; BC IG #4 |
| DivMMC EnJOY! | Investronica 48K+ joystick / +2A Sinclair ports | Joystick clash; PRO ONE turns its ports off on the +2A | EnJOY! manual |
| ZX-WiFi | ZX-Evo / TS-Conf board COM / ZiFi (`#EF`) | Card never selected; use the `#EE` build | ZX-WiFi ReadMe; ZX-Evo RTL |
| ZX-WiFi | ZXMC-1/2 | Same port; does not work | ZX-WiFi ReadMe; BC IG #4 |
| ZX-WiFi v1.0-1.1 | Scorpion Turbo+ `#FF` port | Read fight; v1.2+ (IORQGE) or disable DD53 | ZX-WiFi ReadMe |
| ZX-WiFi | TS-Conf | Reset problems; do not fit diode U8 | ZX-WiFi ReadMe |
| ZX-WiFi | Interface 1, +D, C-DOS modem, Kondratyev ISA COM | `#EF` overlap [DER] | BC IG #4 |
| ZXNETUSB | other `#xxAB` users (KAY Kjoy) | Card wins where IORQGE is honored | `zbus.v`; BC IG #4 |
| Kempston mouse (no IORQGE) | A5- / A0-only joystick decodes | Machine must block `#DF` | `KEMPMOUS.LZH` |
| ZX_BUS_Mouse (IORQGE) | Pentagon-1024SL Kempston | Card wins | ZX_BUS_Mouse README |
| Kempston mouse card | ZX-Evo / TS-Conf | Card dead (board mouse answers) | ZX-Evo RTL |
| ZX_BUS_Mouse v1.1 | Scorpion ProfROM | ROM disk does not load; fixed by A15 / /DOS | ZX_BUS_Mouse README |
| Mouse, ZX-WiFi, ZXNETUSB | Beta `#FF` decode `1xxxxx11` in DOS | Overlap in TR-DOS; IORQGE cards win where honored [DER] | BC IG #4 |
| +12 V card (ZX-MultiSound) | ZX-Evo | Needs jumper J4 | ZX-Evo user manual |

## 10. Open / unconfirmed

**Buses**
- 128K / +2 edge pin lower 13: BC IG #7 "not connected" vs MAME "/IORQGE (+2 only)".
- Scorpion +12 V pin; whether Pentagon-1024SL's own ports honor IORQGE (implied by "NemoBus v0.9m", schematic not
  read); KAY bus beyond BC IG #7; the 48K "ULA behind resistors, bus wins" claim.
- What two drivers on one read port produce on real NemoBus / 128K boards (owner question Q2). No source; BC IG #7
  calls it the user's responsibility.
- Whether the ZX-MultiSound author's ZX-Evo tests used `FREE_IORQ` or a disabled FPGA TurboSound.
- Whether any card *drives* /IODOS (none found; it is a machine output on NemoBus v1.1).

**Sound**
- Fuller: decode width, read port (`#3F` vs `#5F`), clock; DK'tronics decode and read port; Stavi decode; AY-Magic
  only from zxsp; ZON X decode from zxsp's PCB trace only.
- PoS TurboSound flip-flop reset and chip mapping; Quadro-AY installation; NedoPC TS early board power-up chip.
- Zaxon "Turbosound FM and SAA1099": signals, decode, SAA enable byte (speccy.pl blocked scripted fetch).
- ZXM-SoundCard rev 00-03 external CSIO decode; IORQGE polarity through DD3; Middle control-byte compare; `#FFFC` vs
  A0-only ULA in practice.
- Simple SAA IF decode, gating, SAA reset.
- Classic GS on host /RESET (schematic vs manual); Black_Cat `#33` bit layout; ZXM-GS IORQGE before v1.02.
- ZXM-MoonSound: reason for the "PentEvo" name, /INT jumpers, +12 V use; Profi palette vs OUTIORQ.
- SounDrive 1.05 schematic (confirm the BC IG #4 loose decode, S1 wiring, no DOS gate).
- Profi Covox port (`#5F` / `#3F` vs `#DD` / `#BB55`).

**Storage**
- IORQGE on a Sinclair-edge Beta, on SMUC, on Z-Controller SD / mouse ports.
- Z-Controller decode beyond the low byte and its DOS gating (CPLD source unpublished).
- DivIDE Plus `#17` decode; DivMMC `#EB` read rule; DivMMC + Multiface electrically.
- Real Scorpion `#FE` behavior in DOS (SMUC ports match it); Scorpion Beta decode (MiSTer A2-A0 vs MAME / Black_Cat
  A7, A1, A0); Pentagon DOS trigger on M1 only or any read.

**Network / input**
- ZXNETUSB ROM-block pin; effect of its p2666fix CPLD branches.
- ZX-WiFi /INT, /WAIT absence (net labels only).
- Pentagon 128 (1991) built-in Kempston; Kempston D5-D7 on the original board.

**Not fetched:** zx-pk.ru was mostly not tried (one thread fetched fine); speccy.info skipped (blocks scripted
fetches); speccy.pl returned empty pages; micklab.ru works only over plain HTTP; `pentagon.nedopc.com/info.htm` is 404
(Wayback copy used); the Sinclair Wiki "Kempston interface" page returned 404.

## 11. Sources

**Standards and port tables**
- Black_Cat, BC Info Guide #4, full ZX port table: [PDF](https://wiki.speccy.org/_media/cursos/ensamblador/zx-ports-full-table.pdf); repo copy `docs/ports/zx-ports-full-table.txt`.
- Black_Cat, BC Info Guide #7 R20200527 (ZX bus standardization): [zx.clan.su thread](https://zx.clan.su/forum/7-82-1).
- [zxpress: ZX-BUS, Spectrum Expert #02](https://zxpress.ru/article.php?id=11759); [zxpress: SMUC on NemoBus, SibNews #08](https://zxpress.ru/article.php?id=18278).
- [MAME zxbus/bus.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/zxbus/bus.h), [MAME exp.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/exp.h).
- [Sinclair Wiki 48K edge](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_16K/48K_edge_connector), [+3 edge](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_+2A/2B,_+3/3B_edge_connector); [WoS ports FAQ](https://worldofspectrum.org/faq/reference/ports.htm); [nocash zxdocs](https://problemkaputt.de/zxdocs.htm).
- ZX-Evo FPGA (BaseConf / TS-Conf `zbus.v`, `zports.v`) and the ZX-Evo user manual rev C: [tslabs/zx-evo](https://github.com/tslabs/zx-evo); [alfishe/pentevo](https://github.com/alfishe/pentevo).
- [zx-pk.ru: Pentagon 128 ZX-BUS](https://zx-pk.ru/threads/28751-pentagon-128-zx-bus-ili-kak-prikrutit-naprimer-z-controller.html); [ZX-Review 1994 #5](https://zxpress.ru/book_articles.php?id=462).

**Card sources** (direct links in each section): Kio's Spectrum archive (Melodik, Fuller, ZON X); MAME
`src/devices/bus/spectrum/` (melodik, fuller, ay/slot, beta, beta128, mface, kempjoy, kempmouse, zxbus/nemoide,
zxbus/smuc, zxbus/neogs); FUSE `peripherals/` (melodik, fuller, specdrum, beta, divide, divmmc, multiface, joystick,
kempmouse); [zxsp](https://github.com/Megatokio/zxsp); [Unreal Speccy io.cpp](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/io.cpp);
[alfishe/GeneralSound](https://github.com/alfishe/GeneralSound); [alfishe/neogs](https://github.com/alfishe/neogs);
[alfishe/zxm-moonsound](https://github.com/alfishe/zxm-moonsound); micklab.ru (ZXM-SoundCard, ZXM-GeneralSound,
ZXM-MoonSound pages and CPLD sources); [UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound);
[lvd2/zxnet_usb](https://github.com/lvd2/zxnet_usb); [izzx-git/ZX-WiFi](https://github.com/izzx-git/ZX-WiFi);
[proboterror/ZX_BUS_Mouse](https://github.com/proboterror/ZX_BUS_Mouse); [Velesoft K-Mouse Turbo](https://velesoft.speccy.cz/kmturbo-cz.htm);
[Velesoft D/A for ZX](https://velesoft.speccy.cz/da_for_zx-cz.htm); [DivIDE programming model](http://divide.speccy.cz/files/pgm_model.txt);
[RWAP DivIDE notes](https://www.rwapsoftware.co.uk/spectrum/spectrum_divide_notes.html); [EnJOY! PRO ONE manual](https://www.bytedelight.com/wp-content/uploads/2020/06/DivMMC-EnJOY-PRO-ONE-Manual.pdf);
[KOE Z-Controller (Wayback)](http://web.archive.org/web/2016/http://pentagon.nedopc.com/info.htm); [romychs/zcontroller](https://github.com/romychs/zcontroller);
[7FFD/ZX-SDC](https://github.com/7FFD/ZX-SDC); [MDFS joystick notes](https://mdfs.net/Info/Comp/Spectrum/Joystick/);
[Shiru, Programming Turbo Sound](https://zxpress.ru/article.php?id=8612); [City #20 SounDrive](http://zxpress.ru/article.php?id=13608);
[zxdn SounDrive 1.51](http://zxdn.narod.ru/hardware/sd151inf.htm); [zxdn ZX Format #5 Covox](http://zxdn.narod.ru/hardware/zf5covox.htm);
[zxdn Scorpion mods](https://zxdn.narod.ru/hardware/dpt1scrp.htm); [dukeyusupov Covox](https://dukeyusupov.ru/2025/02/17/zx-covox.html);
[Jungsi TSFM + SAA](https://www.jungsi.de/turbosound-fm-and-saa1099-sound-card-retro-sinclair-zx-spectrum/);
[Simple SAA IF](https://zxsparrow.com/speccy_hw/simple/Simple_SAA_IF/Simple_SAA_IF_eng.html); [SimCoupe SAMIO.h](https://github.com/simonowen/simcoupe/blob/master/Base/SAMIO.h);
[nihirash/zx-net-tools](https://github.com/nihirash/zx-net-tools).

**Local collection:** `hardware/turbo-sound/` (TURBO-AY.ZIP, TS030520.RAR, TS240420.PNG, TURBOSOU.PNG),
`hardware/general-sound/` (GS.ZIP, GS104SRC.ZIP), `hardware/hdd-ide/` (NEMOHDD.ZIP), `hardware/KEMPMOUS.LZH`,
`hardware/ZX_EXTEN.LZH`, `hardware/SPECHW.ZIP`, `hardware/sound/zx-multisound*`, `scorpion/` (scorpion.doc).

**Repo design folders:** `docs/inprogress/2026-09-10-turbosound-fm/`, `2026-09-19-general-sound/`,
`2026-09-29-neogs-bringup/`, `2026-09-13-moonsound/`, `2026-07-19-covox-audio-review/`,
`2026-09-23-sounddrive-quad-wiring/`, `2026-09-28-scorpion-smuc/`, `2026-09-28-ide-atapi/`,
`2026-09-28-storage-controllers-survey/`, `2026-10-02-sprinter-network/`, `2026-10-02-atm2ioesp/`,
`2026-09-12-kempston-mouse/`, `2026-10-03-zx-multisound/`; `docs/hardware/profi-1024.md`.

## Review correction (2026-10-03): ZX-MultiSound on ZX-Evo

The conclusion above that the MultiSound's YM part is dead on ZX-Evo is **wrong**. It assumes every card decodes from
/IORQ. The MultiSound does not: its CPLD detects an I/O cycle as "RD or WR without M1 and without MREQ"
(`top.v`: "iorq_n are useless in zxevo"), so the `porthit` masking of /IORQ does not hide `#FFFD` / `#BFFD` from it.
With Baseconf (`zbus.v`): `drive_ff` needs /IORQ2, which `porthit` keeps inactive, so the FPGA does not drive the data
bus on an AY read; the board has no data buffers. Result:

- **Writes** to `#FFFD` / `#BFFD` reach both the socketed YM2149 and the card.
- **Reads** of `#FFFD` are driven by both chips at once (a bus fight), unless the YM2149 is taken out of its socket;
  then only the card drives and the card works fully. This is the likely setup of the card's author, who tested only
  on ZX-Evo.

Modeling consequence: a card declares how it detects I/O cycles (`iorq` or `rdwr`); a `rdwr` card sees cycles the
board hides by `porthit`. The ZX-Evo AY is a socketed chip, so "socket empty" is a real configuration of the built-in.
