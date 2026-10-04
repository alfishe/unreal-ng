# SL-0 part 1: expansion connectors on the machine side

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Phase** | SL-0 (research), part 1: machines and the ZX-bus / NemoBus standard |
| **For** | [requirements.md](requirements.md) R-BUS-1, R-BUS-2; [compatibility-matrix.md](compatibility-matrix.md) §4; [architecture.md](architecture.md) §3.1, §4 |
| **Method** | Schematics, netlists, PLD/CPLD/FPGA sources and service manuals first; prose second; emulators only as a cross-check. Where sources disagree they are tabulated (reference-consensus rule). |
| **Not used** | speccy.info (blocks scripted fetches; its ZXBUS and Pentagon-1024SL pages are unread). Chris Smith's ULA book (not online). |

## Contents

1. [Main conclusions](#1-main-conclusions)
2. [Glossary](#2-glossary)
3. [Machine x signal table](#3-machine-x-signal-table)
4. [The ZX-bus / NemoBus standard](#4-the-zx-bus--nemobus-standard)
5. [Sinclair 48K](#5-sinclair-48k)
6. [Sinclair 128K and grey +2](#6-sinclair-128k-and-grey-2)
7. [Amstrad +2A / +3](#7-amstrad-2a--3)
8. [Pentagon 128 (1991)](#8-pentagon-128-1991)
9. [Pentagon 512 / 1024SL](#9-pentagon-512--1024sl)
10. [Kay-1024](#10-kay-1024)
11. [Scorpion ZS-256 and ProfScorpion](#11-scorpion-zs-256-and-profscorpion)
12. [ATM Turbo 2 v4.50 and ATM Turbo 2+ v7.10](#12-atm-turbo-2-v450-and-atm-turbo-2-v710)
13. [ZX-Evolution: Baseconf and TS-Conf](#13-zx-evolution-baseconf-and-ts-conf)
14. [Profi v3 / v5](#14-profi-v3--v5)
15. [Sprinter Sp2000](#15-sprinter-sp2000)
16. [Sinclair edge variants and adapters](#16-sinclair-edge-variants-and-adapters)
17. [Consequences for the slot design](#17-consequences-for-the-slot-design)
18. [Open / unconfirmed items](#18-open--unconfirmed-items)

---

## 1. Main conclusions

1. **The way IORQGE works is not the same on every machine.** The sources show four different behaviors:

   | Behavior | What it means | Machines |
   |---|---|---|
   | **Card wins** | A card that drives IORQGE high hides the I/O cycle from every lower-priority slot and from the whole board decoder, for reads and writes. | NemoBus standard (Kay), Pentagon-1024SL v2.2, Pentagon v2.666LE, Scorpion ZS-256 (Turbo+ netlist), ATM with the third-party CPU-socket ZX-bus adapter |
   | **Board wins** | The board never passes I/O for its own ports to the slots. IORQGE only lets slot 1 block slot 2 (and stops the FPGA from driving `#FF`). A card cannot shadow a built-in device. | ZX-Evolution (Baseconf and TS-Conf); Profi v3 / v5 (`/OUTIORQ`, the same rule in discrete logic) |
   | **ULA only** | One edge pin suppresses only the ULA's `#FE` decode. | Sinclair 48K (`/IORQULA`), grey +2 (`/IORQGE`, scope likely ULA only) |
   | **None** | No suppression input at all. | Sinclair 128K (UK and Spanish), +2A / +3, ATM 4.50 / 7.10 without the adapter, Sprinter (cards are not on the Z80 port path) |

   The design's single "IORQGE shadows a built-in" rule (open-questions Q2) is correct only for the "card wins" machines. It needs a per-bus **arbitration mode** (§17).
2. **Some machines in the matrix have no ZX-bus slot at all.**
   - The original **Pentagon 128 (1991)** has no CPU bus connector. Its 64-pin "system connector" carries only video, keyboard, tape, sound and power.
   - **ATM Turbo 2 / 2+** have only an 8-bit "I/O bus": data, an address latched by `OUT (#FB)`, and strobes from port `#FA`. There is no Z80 address or control line on it.
   - **Sprinter** has ISA-8 slots. ZX-bus cards go behind a passive "ISA to Spectrum-BUS" adapter that software reaches as a memory window.
3. **ZX-Evo has no TurboSound in the FPGA.** Baseconf and TS-Conf drive one physical YM2149 chip in a socket. [compatibility-matrix.md](compatibility-matrix.md) §4 says otherwise and must be fixed. Also, on ZX-Evo hardware the AY ports `#xxFD` never reach the slots. So a ZX-MultiSound's YM part cannot answer there unless the FPGA is built with `FREE_IORQ` (TS-Conf only, off by default). This contradicts the matrix note "MultiSound shadows the FPGA TurboSound" and needs an owner decision.
4. **Who wins a read when two devices drive the data bus without IORQGE is not documented for any machine.** Facts that do exist:
   - On the **48K / 128K / +2**, the ULA sits behind 470 Ω series resistors. A card on the CPU side therefore beats the ULA (`#FE` reads).
   - **ZX-Evo** drives `#FF` itself on every unclaimed read and on every interrupt acknowledge. A card that answers without IORQGE2 fights it, and a card cannot supply an IM2 vector.
   - **Pentagon-1024SL** and **Scorpion Turbo+** have pull-ups on D0-D7, so a read nobody answers returns `#FF`.
   - The NemoBus rule (Black_Cat guide) is simply that a card that can be read **must** drive IORQGE.

   Wired-AND is therefore a modeling choice, not a measured hardware fact. It should be documented as such, and a clash should be reported.
5. **The Scorpion does not need its own bus kind.** Its slot is a ZX-bus with "card wins" IORQGE. It differs from NemoBus only on a few pins: A8 is RAS (not CLK), B4 is DCGE (not BLK), there is no RS, TURBO, F, `/IODOS` or audio, and +12 V is on the slot only on Turbo+ boards.
6. **`/IODOS` exists only in NemoBus v1.1 (ZXM-Phoenix line) and Kay-2010.** No creatable machine has it. No creatable machine has audio on its ZX-bus slot either. A sound card needs its own audio cable, which is fine for an emulator.

---

## 2. Glossary

| Term | Meaning |
|---|---|
| **ZX-bus / NemoBus** | The 62-pin (2 x 31) slot bus of Soviet and Russian Spectrum clones. Defined around the Kay boards by Nemo (V. Skutin). "ZX-BUS" and "NemoBus" are used for the same connector. |
| **Edge connector** | The Sinclair / Amstrad rear connector: printed contacts on the main board, 2 x 28 positions with a key slot at position 5. |
| **SL-62** | The 62-contact slot socket (ISA-8 form factor, 2.54 mm pitch) used for ZX-bus slots. |
| **IORQGE** | "I/O request gate enable": a card sets this line when it recognizes its own port, so that other decoders ignore the cycle. On ZX-bus it is active **high** (pulled down by 680 Ω) on every board checked. |
| **Arbiter chain** | The OR gates that build each lower slot's `/IORQ` from the CPU `/IORQ` and the IORQGE of every higher slot. |
| **/IORQULA** | The 48K's name for the edge pin that gates the ULA's `/IORQ` input. |
| **/OUTIORQ** | Profi's copy of `/IORQ` for the slot. It is held inactive while the Profi's own decoder selects an internal port, so the board always wins. |
| **/DOS** | Output: low while the TR-DOS ROM is mapped (Beta-128 "DOS" state). |
| **/IODOS** | NemoBus v1.1 only: enables "shadow" ports in an I/O cycle (inverted `#EFF7` bit 7). |
| **/ROMCS, CSR/, RDR/** | ROM override. Sinclair `/ROMCS`: a card pulls it high to disable the board ROM. ZX-bus splits it into CSR/ (the board's ROM select, output) and RDR/ (the ROM chip's real enable, which a card can override). |
| **/ROM1OE, /ROM2OE** | The +2A / +3 replacement of `/ROMCS`: one output-enable line per ROM chip. |
| **74LS245 / 555АП6** | An 8-bit bus buffer. When one sits between the CPU and a device, its direction and enable decide who drives the bus. |
| **Wired-AND** | Two open-collector or NMOS drivers on one line: a 0 from either wins. Used in the design as the default read-conflict rule. |
| **Floating bus** | What a read returns when nothing answers: on Sinclair machines the byte the ULA is fetching. |
| **porthit** | The ZX-Evo FPGA signal "this port is decoded on the board". |
| **INTA** | The Z80 interrupt-acknowledge cycle (`/M1` and `/IORQ` low together). In mode IM2 the vector byte is read from the data bus in this cycle. |
| **Shadow mode / DOS mode** | The state in which TR-DOS ports (`#1F`, `#3F`, `#5F`, `#7F`, `#FF`) are live. |

---

## 3. Machine x signal table

Legend:
- `Y`: present.
- `-`: absent.
- `?`: unconfirmed.
- `in`: input to the board (a card drives it).
- `out`: output from the board.
- In the IORQGE column, "card wins", "board wins", "ULA only" and "none" are the behaviors from §1.

| Machine (connector) | Slots | IORQGE | /IODOS | /DOS | ROM override | /WAIT | /M1 | /RFSH | /INT | /NMI | /BUSRQ | /RESET | +12 V | -5 / -12 V | Audio | CLK |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **NemoBus v0.9-1.1** (SL-62) | board-defined | Y, card wins, active high | v1.1 only | Y out | CSR/ out, RDR/ in | Y | Y | Y | Y | Y | Y | Y | Y | - | - | Y 3.5 MHz (+ F 14 MHz) |
| **48K** (edge 2x28) | 1 | ULA only (`/IORQULA`) | - | - | `/ROMCS` in | Y | Y | Y (not issue 6A) | Y | Y | Y | Y | Y | -5 V | - | `/CK` (ULA clock, contended) |
| **128K UK** (edge) | 1 | none (pin not connected) | - | - | `/ROMCS` in | Y | Y | Y | Y | Y | Y | Y | Y | -5 V | - | `/CK` |
| **128K Spanish** (edge) | 1 | none | - | - | `/ROMCS` in | Y | Y | Y | Y | Y | Y | Y | Y | -5 V | - | - |
| **+2 grey** (edge) | 1 | ULA only? (input present, scope ?) | - | - | `/ROMCS` in | Y | Y | Y | Y | Y | Y | Y | Y | -5 V | - | `/CK` |
| **+2A / +3** (edge) | 1 | none | - | - | `/ROM1OE` + `/ROM2OE` | Y | Y | Y | Y | Y | Y | Y (+ active-high RESET?) | Y | -12 V | - | CKEXT (inverted) |
| **Pentagon 128 (1991)** | 0 (no CPU bus) | - | - | - | - | - | M1 only | - | - | - | - | out | Y | - | Y (sound out) | - |
| **Pentagon-1024SL v2.2** (SL-62) | 3 | Y, card wins (serial chain) | - | Y out | CSR/ out, RDR/ in | Y | Y | Y | Y | Y | Y | Y | Y | - | - | Y (+ F) |
| **Kay-1024** (NemoBus) | 3 (SL3), 4 (2010) | Y? card wins (by the standard) | 2010: mixed with DOS | Y | Y | Y | Y | Y | Y | Y | Y | Y | Y | - | - | Y |
| **Scorpion ZS-256 yellow** (2x30 edge on board) | 1 (+ expander?) | Y, card wins (resistor-on-IORQ scheme, ? on early boards) | - | Y out | CSR/ out, RDR/ in | Y | Y | Y | Y | Y | Y | Y | - (control port only) | - (control port) | - (control port) | - (RAS) |
| **Scorpion Turbo+ / ProfScorpion** (SL-62) | 2 | Y, card wins | - | Y out | CSR/ out, RDR/ in | Y | Y | Y | Y | Y | Y | Y | Y (B22; B29 via J6) | - | - | - (RAS) |
| **ATM 4.50** ("I/O BUS" 2x12) | 1 | - | - | - | - | - | - | - | - | - | - | out | Y | - | - | - |
| **ATM 7.10** (I1 / X3 2x12) | 2 | - | - | - | - | - | - | - | - | - | - | out | Y (I1) | -12 V (I1) | - | - |
| **ATM + CPU-socket ZX-bus adapter** | 2 | Y, card wins | - | Y (hand-wired) | CSR/, RDR/ | ? | Y | ? | ? | ? | ? | ? | Y | -12 V, -5 V | - | Y |
| **ZX-Evo Baseconf / TS-Conf** (SL-62) | 2 | Y, board wins | - | Y out (DCDOS) | CSROM out, RDROM in (polarity ?) | Y | Y | Y | Y | Y | Y | Y | Y (jumper J4) | - | - | - |
| **Profi v3** (64-pin SYS_BUS) | 1-2 (pass-through) | - (`/OUTIORQ`: board wins) | - | TR-DOS state out | `/ROMCS` out (override ?) | Y (`/READY`) | Y | Y | Y | Y | Y | Y | Y | - | L, R, beeper out | - (AY clock only) |
| **Profi v5** (64-pin SYSTEM BUS) | 1 (6 sockets in BOM ?) | - (`/OUTIORQ`: board wins) | - | TR-DOS out | `/ROMCS` out (override ?) | Y | Y | Y | Y | Y | Y | Y | Y | -5 V or -12 V ? | L, R, beeper out | 12 MHz, AY clock |
| **Sprinter ISA-8** | 2 | n/a (ISA, not on Z80 port path) | - | - | - | IOCHRDY to /WAIT | - | refresh | IRQ to PIO | - | - | RESET DRV | Y | -5, -12 V | - | CLKOUT (CPU clock) |
| **Sprinter + ISA to ZX-bus adapter** | 1 per adapter | ? (none modeled) | - | - | - | ? | ? | ? | ? | ? | - | ? | ? | ? | - | ? |

Sources: the per-machine sections below.

---

## 4. The ZX-bus / NemoBus standard

### 4.1 Origin and versions

**Origin.**
- Nemo (Vyacheslav Skutin, St. Petersburg) defined the bus for the Kay clones. It first appeared on Composite-128K AY / KAY-128 ([Black_Cat, "BC Info Guide #7. Standardization of ZXBus interfaces and buses", R2020-05-27](https://zx.clan.su/forum/7-82-1), file [BC_IG_7_R202005.rar](https://zx.clan.su/_fr/0/BC_IG_7_R202005.rar), §1, §3).
- This guide is the most complete specification found. It is a community document, not Nemo's own.
- Nemo's 1994 article "Системная шина в Spectrum-совместимых компьютерах" (*Radiolyubitel* 1/94) gives the philosophy (local collection: hardware/hdd-ide/NEMO_END.RAR, `статья.TXT`):
  - The bus carries the unbuffered Z80 pins.
  - Cards load it with ALS inputs.
  - The Sinclair connector is the prototype.
  - The article promises an in-house standard but gives **no pinout**. That standard was not found.

**Form.**
- An SL-62 slot (ISA-8 form factor), 2 x 31 contacts at 2.54 mm. The cards carry the edge (BC IG #7, Table 1, §3.3).
- The 62-pin form first appeared on KAY-1024 ([atsidaev/zxbus README](https://github.com/atsidaev/zxbus)).

**Versions** (BC IG #7 §1.1, §3.4):

| Version | First boards | Change |
|---|---|---|
| v0.9 | KAY-256 v1.1-1.3 | |
| v1.0 | KAY-256 v1.4, KAY-1024SL3, KAY-2010 | adds TURBO (5B); CLK in phase with F |
| v1.1 | | adds `/IODOS` (20B) |
| v1.2 | project only | DMA, external memory manager |

An "m" suffix means a board with signals missing:

| Board | Version |
|---|---|
| Pentagon-1024SL v1.3-1.4 | v0.9m |
| Pentagon-1024SL v2.2 | v1.0m |
| Pentagon v2.666LE | v1.0m |
| ZXM-Phoenix | v1.1m |
| ZX-Evolution rev A-C | v0.9m |

### 4.2 Pinout (BC IG #7 Table 1 / 3)

| Pin | Side A | Pin | Side B |
|---|---|---|---|
| 1A | A14 | 1B | A15 |
| 2A | A12 | 2B | A13 |
| 3A | +5V | 3B | D7 |
| 4A | /DOS | 4B | BLK |
| 5A | F (14 MHz) | 5B | TURBO (v1.0+) |
| 6A, 7A | GND | 6B, 7B, 8B | D0, D1, D2 |
| 8A | CLK (3.5 MHz) | 9B, 10B | D6, D5 |
| 9A-12A | A0-A3 | 11B, 12B | D3, D4 |
| **13A** | **IORQGE** (input, active high) | 13B | /INT |
| 14A | GND | 14B | /NMI |
| 15A | **RDR/** (ROM read enable, a card can override it) | 15B | /HALT |
| 16A | RS (ROM A14, from `#7FFD` D4) | 16B | /MREQ |
| 17A, 18A | NC | **17B** | **/IORQ** (after the arbiter) |
| 19A | /BUSRQ | 18B, 19B | /RD, /WR |
| 20A | /RESET | 20B | /IODOS (v1.1+) |
| 21A-24A | A7, A6, A5, A4 | 21B | /WAIT |
| 25A | CSR/ (board ROM select, output) | 22B, 23B | NC |
| 26A | /BUSAK | 24B, 25B | /M1, /RFSH |
| 27A, 28A | A9, A11 | 26B, 27B | A8, A10 |
| 29A | +5V | 28B, 29B | +5V, +12V |
| 30A | GND | 30B | GND |
| 31A | NC | 31B | NC |

Open-collector lines: /WAIT, /INT, /NMI, /RESET and TURBO.

**The row letters and some signals differ between sources:**

| Source | Row with A14 / A12 / +5V | IORQGE shown | 13B / 15B |
|---|---|---|---|
| [BC IG #7](https://zx.clan.su/forum/7-82-1) | A | active high | /INT, /HALT |
| [Pentagon-1024SL v2.2 schematic](https://github.com/koe1234/pentagon_2.2/blob/main/ver22.pdf) | A | high, 680 Ω pull-down | /INT, /HALT |
| ZX-Evo rev C schematic ([zxevo_sch_revc.pdf](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/revC/zxevo_sch_revc.pdf)) and the rev D PCB | A | high (R58 / R59 to GND) | /INT, /HALT |
| [Spectrum Expert #02, "ZX-BUS"](https://zxpress.ru/article.php?id=11759) | A | "IOGE: the device outputs 1" | (not listed) |
| MAME [zxbus/bus.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/zxbus/bus.h) | A | IOGE | **"-" (unused)** |
| [atsidaev/zxbus](https://github.com/atsidaev/zxbus) KiCad symbol | **B** | **`~IORQGE` (active low)** | |

Consensus: rows as in BC, IORQGE active high, 13B = /INT. MAME's "13B / 15B unused" and atsidaev's active-low label are minority readings.

### 4.3 The IORQGE rule

BC IG #7 §3.1-§4.7:

1. **How a card forms IORQGE.** A card forms IORQGE from the address lines and `/M1` only, **not** from `/IORQ`. The address is valid before `/IORQ` falls, so the chain settles in time.
2. **Priority.** The board's own ports have the **lowest priority**. That is why they may use coarse decoding.
3. **The chain.** Slot 0 gets the raw CPU `/IORQ`. Each slot's IORQGE is ORed into the `/IORQ` of the next slot. The last stage ("IORQG/") feeds the board decoder. So a card that drives IORQGE silences all lower slots and **all** board ports, for reads and writes. BC gives serial and parallel arbiters (Fig. 8, Fig. 9).
4. **Readable cards.** "If the device supports reading from its ports, an IORQGE former is mandatory." A card with incomplete decoding goes in a lower slot.
5. **Interrupt acknowledge.** During INTA, IORQGE arbitrates the IM2 vector in the same order. The board is last. Pull-ups make an unclaimed vector read `#FF`.
6. **FPGA machines.** A board may decode `#FE` and `#7FFD` **writes** without waiting for the chain. "Reading port `#FE` is possible only with the lowest priority." Port `#FF` (attributes) is selected only when nobody asserts IORQGE.
7. **ROM override.** It works the same way. A card drives RDR/ to block the board ROM, and CSR/ is the board's ROM select. Only one ROM-replacing card per machine before v1.2.
8. **Shared interrupt-mask port** `#F777` (write): D0 DMA, D1 / D2 timers, D3 ULA, D7 controller enable.

---

## 5. Sinclair 48K

**Connector.**
- A rear edge connector, 2 x 28 positions, key slot at position 5 (54 contacts) ([Sinclair Wiki: edge connector](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_edge_connector); [16K/48K page](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_16K/48K_edge_connector); MAME [exp.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/exp.h)). Full table in §16.
- Signals: all Z80 address, data and control lines; `/ROMCS`; `/IORQULA`; `/CK`; +5 V, +9 V, +12 V, -5 V and 12 V AC; composite video and Y / U / V. `/RFSH` is missing on issue 6A.
- Power: "+12V ... available at the expansion port ... The +12V, +5V and -5V are also made available" ([48K service manual §1.9](https://spectrumforeveryone.com/wp-content/uploads/2017/08/ZX-Spectrum-Service-Manual.pdf)).

**IORQGE behavior (ULA only).**
- `/IORQ` reaches the ULA through a series resistor. TR6 pulls the ULA input high when A0 = 1. "This combined /IORQ+A0 signal is connected to Lower Pin 13 and is referred to as /IORQULA or sometimes /IORQGE. Some peripherals use this as an input to inhibit the ULA /IOREQ even when A0 is low" ([48K edge wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_16K/48K_edge_connector)).
- Interface 1 drives it through Q10 / Q11 ([IF1 / IF2 / Microdrive service manual §1.1.2](https://spectrumforeveryone.com/wp-content/uploads/2017/08/ZX-Interface-1-2-Microdrive-Service-Manual.pdf)).
- The 48K has no other internal ports, so this suppresses everything internal.

**Built-in devices.**
- ULA `#FE` (A0 = 0): keyboard, EAR, MIC, beeper, border. It can be shadowed through pin 13.
- ROM: can be overridden with `/ROMCS` (R33 sits on the ULA side, "Interface 1 uses this mechanism"; 48K SM §1.5.1).

**Read conflict.**
- R1-R8 (470 Ω) separate the ULA and the lower 16K RAM from the CPU data bus (48K SM §1.5.2). Z80, ROM, upper RAM and the edge connector are on the CPU side.
- So a card beats the ULA on a `#FE` read. This is derived from the circuit, not measured.
- The card fights the upper RAM directly (it has no disable line).
- MAME ANDs card data with the keyboard bits on `#FE` ([spectrum.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/spectrum.cpp)). That is an emulator choice.

**Floating bus.** The ULA fetch byte during the display, otherwise `#FF` ([floating bus wiki](https://sinclair.wiki.zxnet.co.uk/wiki/Floating_bus)).

## 6. Sinclair 128K and grey +2

**Connector.**
- The same edge with these changes ([128 edge wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_128_edge_connector); [128K service manual p.9, p.20](https://spectrumforeveryone.com/wp-content/uploads/2017/11/ZX-Spectrum-128-Service-Manual.pdf)):
  - Video pins (Lower 15-18) not connected.
  - The Spanish 128 leaves `/CK` unconnected.
  - -5 V comes from the -12 V rail through a Zener.

**IORQGE behavior.**
- **128K (UK and Spanish): none.** "This pin is NOT connected on either the Investrónica or Sinclair ZX Spectrum 128" (128 edge wiki). [atsidaev/zxbus](https://github.com/atsidaev/zxbus/blob/master/README.md) agrees: "In 128K versions some of the signals were removed (most notable is /IORQGE)".
- **Grey +2:** Lower 13 "is connected to the /IORQGE input" (128 edge wiki). TR7 with R65 forms IORQ OR A0 for the ULA ([+2 wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_+2)). It therefore very likely gates only the ULA. Whether it also gates `#7FFD` or the AY is **unconfirmed** (no legible schematic was read).

**Built-in devices** (none can be switched off from outside):

| Device | Ports and decode | Source |
|---|---|---|
| ULA | `#FE` (A0 = 0) | |
| Paging | `#7FFD` write, A15 = 0 and A1 = 0. Bit 5 locks paging until reset | 128K SM p.10; [WoS 128K FAQ](https://worldofspectrum.org/faq/reference/128kreference.htm) |
| AY | `#FFFD` / `#BFFD`, A15 = 1, A1 = 0, A14 selects the register | 128K SM §1.7 |
| RS-232, MIDI, keypad | through AY port A | 128K SM §1.8-1.9 |

**Read conflict.**
- The R1-R8 arrangement is the same as on the 48K (128K SM p.10), so a card beats the ULA.
- Whether the AY (IC32) is on the CPU side, which would mean a direct fight on a `#FFFD` read, is **unconfirmed**.
- Floating bus as on the 48K.

## 7. Amstrad +2A / +3

**Connector changes** ([+2A/2B, +3/3B edge wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_+2A/2B,_+3/3B_edge_connector); MAME exp.h; [k1 synopsis](https://k1.spdns.de/Develop/Projects/zxsp/Info/Hardware/zx81,%20zxsp,%20zx+2A%20%20edge%20connectors.txt)):

| Removed or changed | Replaced by |
|---|---|
| `/ROMCS` (Lower 25) | `/ROM1OE` (Upper 4) and `/ROM2OE` (Lower 15) |
| Video pins Lower 16-18 | `/DRD`, `/DWR`, `/MTR` (disk) |
| -5 V and +9 V | removed |
| 12 V AC (Upper 23) | -12 V |
| | Upper 28 = RESET, active high per the wiki and MAME; the k1 synopsis says `/RESET` |
| `/CK` | CKEXT, inverted |
| `/IORQGE` (Lower 13) | not connected |

**IORQGE behavior: none.** The gate array 40077 has no IORQGE input on the connector. No source supports the idea that the +3 can suppress its internal decode from outside. None of `#FE`, `#7FFD`, `#1FFD`, `#FFFD` / `#BFFD`, `#2FFD` / `#3FFD` or `#0FFD` can be silenced.

**Built-in devices** ([WoS 128K FAQ](https://worldofspectrum.org/faq/reference/128kreference.htm); MAME [specpls3.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/specpls3.cpp); MiSTer [ZX-Spectrum.sv](https://github.com/MiSTer-devel/ZX-Spectrum_MISTer/blob/master/ZX-Spectrum.sv)):

| Device | Ports and decode |
|---|---|
| ULA function | `#FE` |
| Paging | `#7FFD` (A15 = 0, A14 = 1, A1 = 0) |
| Paging, disk motor, printer strobe | `#1FFD` (A15-A13 = 0, A12 = 1, A1 = 0) |
| AY | `#FFFD` / `#BFFD` |
| uPD765 FDC (+3 only) | `#2FFD` / `#3FFD` |
| Centronics latch | `#0FFD` |

None of these can be switched off.

**Read conflict.**
- ROM, RAM, AY, FDC and the printer latch all sit on the CPU data bus ([+3 service manual p.18](https://worldofspectrum.org/ZXSpectrum128+3ServiceManual/18.html), [p.19](https://worldofspectrum.org/ZXSpectrum128+3ServiceManual/19.html); low-resolution scan).
- No series resistors are documented. A card answering an internal port is a direct fight with no defined winner.

**Floating bus.**
- Ports matching `0000 xxxx xxxx xx0x`, while paging is not locked, return the last contended-bus byte with bit 0 set. Otherwise the read is `#FF` ([redcode 2A-3 Floating Bus Test](https://github.com/redcode/ZXSpectrum/wiki/2A-3-Floating-Bus-Test); [SoftSpectrum48 notes](https://softspectrum48.weebly.com/notes/category/floating-bus)).
- This supersedes the older "always 255" view of the WoS FAQ and MAME.

## 8. Pentagon 128 (1991)

**Connector.** No CPU bus connector exists. XP1 "СИСТЕМНЫЙ РАЗЪЕМ" (2 x 32) carries:
- Video: sync, R, G, B.
- SOUND, RESET, tape in and out.
- Keyboard: KA0-7, KD1-5.
- TURBO, M1, MAGIC.
- +5 V, +12 V and GND.

XP2 is the printer and XP3 the floppy (local collection: pentagon/PENT_SHM.ZIP, `PENT_SHM.TXT`, the corrected 1991 schematic; `PENTAGON.000` is identical).

**IORQGE behavior.**
- None.
- Add-ons tapped **OIRQ** = `/IORQ` OR DOSEN (an I/O request while TR-DOS is not active) from DD85 pin 6. Example: Dimon Hard's 1992 AY "Interface Plate" (local collection: pentagon/PENT_SHM.ZIP, `INTER_PL.TXT`; local collection: hardware/ZX_EXTEN.LZH, `ZX_EXTEN.TXT`, UA3PRQ 1993).
- This only gates add-ons off during TR-DOS. It does not stop the board's decoder.

**Built-in devices.**
- Beta-128 (КР1818ВГ93): `#1F`, `#3F`, `#5F`, `#7F`, `#FF` while in DOS.
- Printer.
- Kempston `#1F` (A7 = 0).
- `#FE` (A0 = 0): "the keyboard takes half of all addresses".
- `#7FFD` (A15 = 0, A1 = 0).
- **No AY on the 1991 board.** The AY was an add-on (bill of materials; [ru.wikipedia "Пентагон (компьютер)"](https://ru.wikipedia.org/wiki/%D0%9F%D0%B5%D0%BD%D1%82%D0%B0%D0%B3%D0%BE%D0%BD_(%D0%BA%D0%BE%D0%BC%D0%BF%D1%8C%D1%8E%D1%82%D0%B5%D1%80))).
- None can be switched off.

**Read conflict.** No buffer and no rule. UA3PRQ advises an add-on buffer (К555АП6), a sub-decoder (К555ИД7) enabled by OIRQ, and a switch to disable all add-ons (ZX_EXTEN.TXT).

## 9. Pentagon 512 / 1024SL

**Pentagon 512.**
- No board-level source was found. The local collection has only SIMM memory-upgrade notes (local collection: pentagon/SIMM_1M.ZIP, SIMM_ZX.ZIP).
- Whether any "Pentagon 512" had an expansion bus is **unconfirmed**.

**Pentagon-1024SL v1.3-1.4.**
- "System Bus ZX-BUS (2 slots)" ([KoE advert, zxpress article 8637](https://zxpress.ru/article.php?id=8637)).
- BC classifies it as NemoBus v0.9m.
- No schematic was found.

**Pentagon-1024SL v2.2 (2006).** Sources: [ver22.pdf](https://github.com/koe1234/pentagon_2.2/blob/main/ver22.pdf), [CPLD/p1024sl.tdf](https://github.com/koe1234/pentagon_2.2/blob/main/CPLD/p1024sl.tdf), [CPLD/p1024sl2.tdf](https://github.com/koe1234/pentagon_2.2/blob/main/CPLD/p1024sl2.tdf).

- **Slots:** 3 x SL-62 (XS1-XS3), labelled "ZX-BUS". The pinout is the NemoBus table (§4.2). 20B (`/IODOS`), 22B and 23B are empty. There is no -5 / -12 V and no audio.
- **Arbiter:** serial, on DD31 (К555ЛЛ1, 2-input OR gates):
  - IORQ2 = IORQ OR IOR1.
  - IORQ3 = IORQ2 OR IOR2.
  - **IORQG = IORQ3 OR IOR3.**
  - IOR1-3 are pulled down by 680 Ω.
  - Priority: XS1 > XS2 > XS3 > board.
- **The whole board decoder runs on IORQG ("card wins", complete):**
  - DD6 (EPM3032): `#7FFD`, `#EFF7`, `#FE` write, and AY BDIR / BC1 (`#FFFD` / `#BFFD` with A15 = 1, A13 = 1, A1 = 0).
  - DD3 (EPM7128): the latches and the turbo I/O wait.
  - Discrete logic: `#FE` keyboard and Kempston read through 555КП11, the printer, and Beta-128 (КР531ИД14; A7 selects VG93 or `#FF`).
  - Nothing internal is decoded from the raw `/IORQ`.
  - Inferred: a cycle claimed by a card gets no board turbo wait.
- **ROM override:** RDROM is wired to the ROM /OE; CSROM reaches it through 680 Ω. RS shares the node of ROM A14 (also through 680 Ω).
- **Read conflict:**
  - No 74245 buffer; the slots sit on the raw CPU data bus.
  - R8 (6.8 kΩ x 8) pull-ups make an unclaimed read or INTA return `#FF`.
  - A card that answers without IORQGE fights the board's tri-state outputs. The result is undefined, and the standard forbids it.
- **Built-in devices:** Kempston joystick, YM2149 / AY8910, ZX LPRINT III printer port, Beta-128, beeper. All are behind IORQG, so a card can shadow any of them. No disable jumper was found: X8 is NTSC; X9 and X10 are unidentified.
- **Cross-check, Pentagon v2.666LE FPGA:** `iorq_after_bus <= (cpu_iorq or fpga_io0 or fpga_io1 or fpga_io2)` gates all internal decoding ([PaE.vhd L1652](https://github.com/koe1234/pentagon_2_666_le/blob/main/LE/PAE_FPGA/PaE.vhd)). That `fpga_io0`-`fpga_io2` are the slot IORQGE pins is inferred from the names.

## 10. Kay-1024

**Slots.**
- KAY-1024/3SL/TURBO has three slots ([Wikipedia "Kay 1024"](https://en.wikipedia.org/wiki/Kay_1024)).
- KAY-1024/SL-4/TURBO v2010 has four NEMO-BUS slots ([zx-pk thread 13770](https://zx-pk.ru/threads/13770-kay-1024-sl-4-turbo-v2010-nemofdc-nemoide.html)).
- BC classifies both as NemoBus v1.0.

**Built-in devices.**
- The FDC is a **card** (Nemo FDC / BETA-TURBO), which is why `/DOS` is generated by the Beta card (BC Table 3).
- On the 2010 board, `/IODOS` is mixed with `/DOS` (Alex_NEMO in the same thread).

The local collection kay-1024/ holds only ROMs. The board pinout and decoder are **unconfirmed** (Kay is not creatable today).

## 11. Scorpion ZS-256 and ProfScorpion

**Sources:**
- Original schematic and board drawing (local collection: scorpion/SCORPION.ZIP: SCORPION.PCX, SCPLATA.PCX).
- Zonov's user guide (local collection: scorpion/extracted/SCORPION/scorpion.doc; [zxpress copy](https://zxpress.ru/eng/ezines/msd/03/scorpion-zs-256-i-o-ports-reference-guide-for-programmers-complete-description-of-port-allocation)).
- The Turbo+ reconstruction netlist ([Scorpion-256-Turbo.kicad_pcb](https://github.com/romychs/Scorpion256TPlus/blob/main/KiCAD/Scorpion-256-Turbo.kicad_pcb), [README](https://github.com/romychs/Scorpion256TPlus/blob/main/README.md)).
- The yellow-board reconstruction YScorp ([PCB json](https://zxgit.org/romych/YScorp/src/branch/master/Sources/PCB_Scorpion-Yellow_v12.2.1.json)).
- [Spectrum Expert #02, "ZX-BUS"](https://zxpress.ru/article.php?id=11759).
- MAME [scorpion.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp).

### 11.1 Connector

| Item | Yellow board | Turbo+ (1996) |
|---|---|---|
| Expansion ("SYSTEM PORT") | 2 x 30 gold-finger edge on the main board. Spectrum Expert #02: СНП-15-96 socket at **2.5 mm** pitch | **2** SL-62 slots (XP4, XP5) in parallel |
| Slot count | 1, plus an optional "Scorpion bus expander" (count **unconfirmed**) | 2 |
| Control port | 2 x 32 edge: keyboard, joystick, Centronics, RS-232, video, audio, **+12 V, -5 V**, MAGIC | DIN-41612, same signals |

**Pinout.** It is the ZX-bus layout (§4.2) with these differences, where all sources agree:

| Pin | NemoBus | Scorpion |
|---|---|---|
| A8 | CLK | **RAS** ("can be used as 3.5 MHz"; whether it follows turbo is **unconfirmed**) |
| B4 | BLK | **DCGE / RB** = `#1FFD` D0 through 560 Ω ("RAM page 0 instead of ROM") |
| A5, B5, A16, B20 (`/IODOS`) | F, TURBO, RS, `/IODOS` | not used |
| B22 | not connected | **+12 V on Turbo+** |
| B28, B29, A29 | +5 V, +12 V, +5 V | +5 V / +12 V through jumpers J5 / J6 (Turbo+ v16.2.8, "as in Nemo-Bus") |

**+12 V location, sources disagree:**

| Source | +12 V on the slot |
|---|---|
| Original scan, YScorp | not on the system port (control port only) |
| Turbo+ netlist | B22, and B29 through J6 |
| MAME bus.h comment | A5 ("SCORPION = +12V") |
| speccy.info search snippet | 5A on some early boards |

**Signals.**
- Present: IORQGE, `/DOS` (output from the DOS flip-flop), CSR/ (A25) and RDR/ (A15, the ROM /CE behind 210 Ω), /WAIT (wired-OR, board drives it through a diode), /INT and /NMI (driven through 560 Ω, so a card can pull them), /BUSRQ, /BUSAK, /RESET, /M1, /RFSH, /HALT.
- Absent: `/IODOS`, CLK, audio, -5 / -12 V.

### 11.2 IORQGE behavior (card wins)

**Mechanism.**
- Turbo+: R57 (560 Ω) links CPU `/IORQ` to the `/IORQGE` net, which enables both port decoders (DD32, DD65).
- YScorp has R63 (330 Ω) for the same job. Spectrum Expert #02 says: cut the IORQ trace and fit 360-430 Ω.
- A card drives the pin to 1, and the board decoders see no `/IORQ`. Spectrum Expert #02: "this blocks the internal ZX ports".
- This is the "resistor on IORQ" form of the arbiter. It allows one level of priority, so two cards both driving IORQGE are not arbitrated by the board.
- Whether the earliest yellow boards had the resistor is **unconfirmed** (the YScorp change note says it was added in v12.2.1).

**Suppressed ports (Turbo+ netlist):** everything the board decodes:
- `#FE`
- `#FF` (attributes)
- the `#1F` family read (Kempston and FDC status buffer)
- `#FFDD` printer
- the whole `#FD` group: `#7FFD`, `#1FFD`, AY `#FFFD` / `#BFFD`, and the turbo on / off reads
- the Beta `#FF` system register

The guide lists "IORQGE = 0" in the decode conditions of all of these. **No port was found that is decoded regardless.**

Not traced: the WD1793 chip select. The turbo wait GAL uses the raw `/IORQ`, so wait timing does not depend on IORQGE (derived).

**Port decode masks, sources disagree:**

| Source | Decode bits |
|---|---|
| Guide (yellow) | includes A2 |
| Turbo+ netlist | no A2; `#7FFD` and `#1FFD` = A0 = 1, A1 = 0, A5 = 1, and A15 / A14 select |
| Black_Cat table | `#1F` needs A7 = 0 |
| Turbo+ netlist | `#1F` has no A7 |

### 11.3 Read conflict

- No data buffer; the slots are on the Z80 data nets. Turbo+ has pull-ups on D0-D7, so an unclaimed read returns `#FF`.
- No source says who wins without IORQGE.
- Collisions derived from the decode masks:

| Board decode | Collides with |
|---|---|
| `#FF` family (A5 = 1, A1 = A0 = 1) | GS `#B3` / `#BB`, Z-Controller `#77` |
| `#1F` family | Kempston cards, Z-Controller `#57` |

  Cards that drive IORQGE avoid these collisions.
- Documented: a NeoGS + SMUC v1.3 conflict on a green Scorpion. It is unresolved; a "ЛЛ1 + 4 x 680 Ω port arbiter" fix is mentioned, but no circuit was found ([zx-pk 11803](https://zx-pk.ru/threads/11803-konflikt-neo-gs-amp-smuc.html)).

### 11.4 Built-in devices

| Device | Ports | Switch off? |
|---|---|---|
| AY-3-8912 | `#FFFD`, `#BFFD` | No jumper (J1-J4 only choose the stereo layout). Shadowable by IORQGE. Users replace it with a TSFM in place; whether it is socketed is unconfirmed |
| Beta-128 (WD1793 on the board) | `#1F`, `#3F`, `#5F`, `#7F`, `#FF` in DOS | No |
| Kempston joystick | `#1F` family read outside DOS (D6 / D7 carry WD DRQ / INTRQ) | No |
| Printer | `#FFDD` write; BUSY on `#FE` D7 | No |
| RS-232 | `#1FFD` D3, `#FE` D5 | No |
| Paging and turbo | `#7FFD`, `#1FFD` | No |
| RTC, Kempston mouse, SMUC | **not on the board** (SMUC is a card; the RTC lives on SMUC) | n/a |

**ProfScorpion (ProfROM).**
- A 256 KB ROM plus a GAL22V10. The plane is switched by reads of `#0100`-`#010F`. There is no I/O port, and the bus does not change ([Scorpion_ProfROM_Paging.md](https://github.com/romychs/Scorpion256TPlus/blob/main/doc/files/Scorpion_ProfROM_Paging.md)).
- The GAL is clocked by RDR/ (slot A15). Inferred: a card that overrides the ROM also freezes plane latching.
- LW ROM builds exist for SMUC and for Nemo IDE (v4n).
- **GMX** (MAME: two slots plus `#78FD`, `#7AFD`, `#7EFD`): not researched.

## 12. ATM Turbo 2 v4.50 and ATM Turbo 2+ v7.10

### 12.1 v4.50 ("ATM-TURBO 512+" on the sheet)

**Naming.** The local collection README (atm-turbo/README.md) calls 4.xx boards "ATM-turbo 1"; the repository says "ATM Turbo 2 v4.50". **Unconfirmed** which is right.

**Connectors** ([MicroArt manual, chapter "Периферия, ее подключение и разъемы"](https://zxpress.ru/ru/books/chapter/2353); schematic [sheet 6](https://zxpress.ru/chapters_images/atmturbo-6.jpg), [sheet 7](https://zxpress.ru/chapters_images/atmturbo-7.jpg)):

| Connector | Type | Signals |
|---|---|---|
| X3 "I/O BUS" | 2 x 12 | D0-D7; CTS0-CTS7 (an address latched by `OUT (#FB)`); `/IORD`, `/IOWR` (strobes from port `#FA`); `/RS` (reset); +5 V; +12 V |
| X1 "internal" | SNP-64 | video, tape, AY outputs, keyboard matrix, MAGIC, M1, TURBO, reset |

The X3 pin pairing comes from a poor scan and is **unconfirmed**.

**Not on any connector:** Z80 address lines, `/IORQ`, IORQGE, `/INT`, `/NMI`, `/WAIT`, `/DOS`, ROM override.

**Built-in devices** (none switchable, per the schematic):
- one AY8912: `#FFFD` / `#BFFD`
- 1818VG93: Beta ports in shadow mode
- Covox / printer: `#FB`
- ADC: `#7DFD`

### 12.2 v7.10

Sources: atmturbo SVN (the NedoPC ATM Turbo repository): `pcad/ver_7_10/cp7_1.pdf`, `doc/ver_7_10/Сборка и Наладка Турбо2+.doc`, `doc/ver_7_10/Описание архитектуры и портов ATM2+.doc`.

**Connectors.** I1 "INTERNAL I/O" and X3 "EXTERNAL I/O / CENTRONICS", each 2 x 12. Per the manual they carry only "buffered data bus ID0-ID7, address CTS0-CTS7, IORD / IOWR, reset RS, +5V, +12V, -12V".

| Pin | I1 | X3 | Pin | I1 | X3 |
|---|---|---|---|---|---|
| A1-A8 | IO0-IO7 | IO0-IO7 | B1 | RS | RS |
| A9, A10 | GND | GND | B2-B9 | CTS0-CTS7 | CTS0-CTS7 |
| A11 | +5V | +5V | B10 | /IORD | /IORD |
| A12 | +12V | STROBE | B11 | -12V | CTBUSY |
| | | | B12 | /IOWR | /IOWR |

**Buffer.** D87 (555АП6) has OE tied low and direction = `/IORD`. It drives the board bus only during an `IN` from `#FA`; the rest of the time it drives outward. So there is no read conflict on this bus: only one address (`#FA`) reads it.

**ZX-bus cards.**
- No slot on the board.
- A **third-party CPU-socket adapter** (atmturbo SVN: `pcad/zxbus_adapter/Adapter ZX BUS ATM.pdf`, `readme.md`) gives two Pentagon-pinout slots with a "card wins" chain on a 555ЛЛ1:
  - slot 1 gets the raw IORQ;
  - slot 2 IORQ = IORQ OR IORQGE1;
  - the board's IORQ = IORQ OR IORQGE1 OR IORQGE2;
  - 680 Ω pull-downs.
- D0-D7, DOS, 14 MHz, CLK, ±12 V and -5 V are hand-wired. Which data bus they tap is **unconfirmed**.

**Built-in devices** (port reference, appendix 3):

| Device | Ports |
|---|---|
| AY (one 8912 socket) | `#FFFD` / `#BFFD` |
| Beta-128 (shadow mode) | `#1F`, `#3F`, `#5F`, `#7F`, `#FF` |
| IDE (shadow mode) | `#xxEF` |
| keyboard (i8031 controller) | `#FE` |
| Covox / printer | `#FB` |
| ADC | `#7DFD` |
| I/O bus | `#FA` |
| attribute port | `#FF` |

- **No Kempston joystick** (assembly manual: "22. Кемпстон Джойстик — Нет").
- Only the keyboard controller can be switched off (`#FF77`).

## 13. ZX-Evolution: Baseconf and TS-Conf

### 13.1 Board (rev C)

Sources: [zxevo_sch_revc.pdf](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/revC/zxevo_sch_revc.pdf); pentevo SVN (the NedoPC ZX-Evo repository, mirrored at [tslabs/zx-evo](https://github.com/tslabs/zx-evo)): `docs/revC/zxevo_user_manual_eng.pdf`, chapter 5.

**Slots.** Two (XS1, XS2), 62-pin, NemoBus layout, with these differences:

| Pin | Signal on ZX-Evo |
|---|---|
| A4 | DCDOS |
| A5 (F), A8 (CLK) | not connected |
| A15 | CSROMCE / RDROM (the ROM chip /CE) |
| A16 | RS |
| A25 | CSROM |
| 13A | IORQGE1 on XS1, IORQGE2 on XS2 |
| 17B | /IORQ1 on XS1, /IORQ2 on XS2 (FPGA outputs) |
| B29 | +12 V, only when jumper J4 is closed |
| XS1 31A / 31B | IORQGE2 / /IORQ2 (purpose undocumented) |

- Missing: CLK, -5 / -12 V, audio, `/IODOS`, TURBO.
- IORQGE1 and IORQGE2 have 680 Ω pull-downs (active high) and go to dedicated FPGA inputs.

**No data buffers.** The Z80 data bus goes straight to the FPGA, both slots, the YM2149, the VG93 and the ROM.

**ROM override.** The FPGA drives CSROM through 680 Ω onto RDROM, which is also slot A15. So a card can override the ROM select (inferred). The polarity is **unconfirmed**: the Verilog comment says "positive polarity!" while the schematic labels look active-low.

### 13.2 Baseconf: board wins

[base/z80/zbus.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/base/z80/zbus.v) L46-50:

```verilog
assign iorq1_n = iorq_n | porthit;
assign iorq2_n = iorq1_n | iorqge1;
assign drive_ff = ( (~(iorq2_n|iorqge2)) & (~rd_n) ) | (~(m1_n|iorq_n));
```

- A port the board decodes (`porthit`) never reaches either slot.
- Slot 1 can hide a cycle from slot 2.
- The FPGA drives `#FF` on every read nobody claimed **and on every INTA**. A card that answers without IORQGE2 fights that `#FF`, and a card cannot supply an IM2 vector.
- The manual says the same (pentevo SVN: `docs/zxevo_base_configuration_eng.pdf` p.9): "all the ports that are present on the motherboard [do not get] to cards ZX-Bus. The mechanism used IORQGE cards only to block each other ... any cards that duplicate any functionality of the motherboard ... will not work."

**`porthit` set** ([base/z80/zports.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/base/z80/zports.v) L313-343; full low-byte decode):
- `#FE`, `#F6`, `#FC`
- every `#xxFD` (`#7FFD`, `#FFFD`, `#BFFD`, `#1FFD`, `#DFFD`)
- Nemo IDE `#10`-`#F0`, `#11`, `#C8` (only when built with `IDE_HDD`)
- `#DF` (mouse)
- `#1F` (Kempston outside shadow mode; VG93 group in shadow mode)
- `#F7`, `#77`, `#57`
- `#BF`, `#BE`, `#BD`
- `#EF` (COM)
- `#3B` (ULAplus, 128K raster only)

**Passed to the slots:** `#FB` (Covox: written internally **and** passed on), GS `#B3` / `#BB`, `#FF` outside shadow mode. The older SVN copy also hid `#2F`, `#4F`, `#6F`, `#8F` in shadow mode.

**Built-in devices:**

| Device | Implementation | Ports | Can it be switched off? |
|---|---|---|---|
| AY | **one YM2149 chip in a socket** (no TurboSound in the FPGA) | `#FFFD` / `#BFFD` | No |
| Beta-128 | real VG93 | shadow ports | per-drive mask `#xxBD` only |
| Kempston joystick | AVR | `#1F` | No |
| Kempston mouse | AVR | `#FADF` / `#FBDF` / `#FFDF` | No |
| Z-Controller SD | FPGA | `#57` / `#77` | No |
| Nemo IDE | FPGA | `#10`-`#F0` | FPGA build option only |
| RTC | AVR | `#BFF7` / `#DFF7` / `#EFF7` | No |
| PS/2 keyboard | AVR | `#FE` | No |
| Covox | FPGA (beeper PWM) | `#FB` | No |
| COM port | AVR | `#F8EF`-`#FFEF` | No |

No runtime switch for the port hiding was found.

### 13.3 TS-Conf: also board wins, with these differences

Sources: [current/z80/zbus.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zbus.v); [current/z80/zports.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zports.v) L330-347; [quartus/tune.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/quartus/tune.v).

```verilog
`ifdef FREE_IORQ
  assign iorq1_n = iorq_n;  assign drive_ff = !iorq2_n && !iorqge2 && !porthit && rd;
`else
  assign iorq1_n = !iorq || porthit;   // iorq is masked by M1_n!
  assign drive_ff = !iorq2_n && !iorqge2 & rd;
```

1. **INTA.** The slots never see the INTA cycle; the FPGA drives its own IM2 vector.
2. **`FREE_IORQ` build option.** It gives slot 1 every I/O cycle and stops the FPGA driving `#FF` on its own ports. It is **off** in both shipped `tune.v` files (standard and VDAC2).
3. **`porthit` set:**
   - `#FE`, `#AF` (all TS registers), every `#xxFD`
   - **`#FB` (Covox is hidden here, unlike Baseconf)**
   - `#F7` outside DOS
   - IDE (when built with `IDE_HDD`)
   - VG ports and `#FF` in DOS mode or with `open_vg`; `#1F` otherwise
   - `#DF`, `#77`, `#57`, `#EF`
   - Not hidden (unlike Baseconf): `#F6`, `#FC`, `#BF` / `#BE` / `#BD`, `#3B`.
4. **ZiFi** (`#00EF`-`#C9EF`, [zifi.md](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md)) lies inside the always-hidden `#xxEF` group.
5. **The VDAC2 build** drops `IDE_HDD`, so the IDE ports go to the bus.
6. **The [TSConf_MiSTer](https://github.com/MiSTer-devel/TSConf_MiSTer) core** adds GS, SAA1099, TurboSound FM and Soundrive. These are MiSTer additions, not ZX-Evo hardware.

## 14. Profi v3 / v5

Sources:
- v3.2 manual OCR (local collection: profi/materials/research/ocr/ProfiV32mn-p02.txt, p03.txt; original at [speccy4ever](https://speccy4ever.speccy.org/_PR.htm)).
- MDESK re-trace ([alemorf/retro_computers Profi_3_2](https://github.com/alemorf/retro_computers/tree/master/Profi_3_2)).
- v5 text schematic (local collection: profi/materials/v5-open-items/PROF5-0C.utf8.txt).
- v5.0 album (local collection: profi/materials/research/ocr/profi50-p07.txt, p11.txt).
- [Insanity #08, "Подключение к Profi различной периферии" (2001)](https://zxpress.ru/ru/ezines/insanity/08/podklyuchenie-periferii-k-kompyuteru-profi-general-sound-ustroystva-na-zx-bus-sovmestimost-s-tr-dos).
- [ZX Review #3-4 (1997), P. Fedin's modification](https://zxpress.ru/ru/ezines/zx-review/3-4/dorabotka-kompyutera-profi-dlya-beskonfliktnogo-podklyucheniya-periferii-modema-myshi-i-drugih).

### 14.1 Connector: Profi's own 64-pin bus, not ZX-bus

**Type.** A 2 x 32 СНП58-64 connector, "SYS_BUS" on v3.2 and "SYSTEM BUS" (X1) on v5. The v3.2 manual says it matches the Sinclair connector only "with an adapter insert". The pin order is Profi's own.

**Pinout** (v3.2 re-trace, v5 text schematic and Insanity #08 agree; `*` = v5 only):

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| A2 | LEFT (AY) | B1 | /TURBO |
| A3, A4 | A14, A12 | B2 | RIGHT (AY) |
| A5 | +5 V | B3, B4 | A15, A13 |
| A7 | (Fedin mod: /EXTIORQ) | B5, B8-B14 | D7, D0, D1, D2, D6, D5, D3, D4 |
| A8, A9, A16 | GND | B15 | /INT |
| A10 | CLCAY (AY clock) | B16 | /NMI |
| A11-A14 | A0-A3 | B17 | /HALT |
| **A15** | **/OUTIORQ** | B18 | /MREQ |
| A17 | -5 V * | **B19** | **/IORQ (raw)** |
| A21 | /BUSRQ | B20, B21 | /RD, /WR |
| A22 | /RESET | B22 | 12 MHz * |
| A23-A26 | A7-A4 | B23 | /READY (= /WAIT) |
| A27 | /ROMCS (machine output) | B24 | +12 V |
| A28 | /BUSAK | B25 | TIMER * |
| A29, A30 | A9, A11 | B26, B27 | /M1, /RFSH |
| A31 | ROM14 (`#7FFD` bit 4) | B28, B29 | A8, A10 |
| A32 | SOUND (beeper) | B30, B31, B32 | CP/M, /TR-DOS, /BLOK |

Pins not listed are not connected.

**Voltage on A17, sources disagree:**

| Source | A17 |
|---|---|
| Bus label | -5 V |
| v5 album p.7 text | the converter makes "-12V" |

**Slots.**
- v3.2: SYS_BUS and SYS_BUS1 pass through the controller board. There is no native multi-slot backplane. Users built "3 slots ZX-BUS / NEMO-BUS" boards (Insanity #08).
- v5.0: the parts list has "X1A СНП58-64 socket x6". Whether these are six expansion sockets is **unconfirmed**.

### 14.2 IORQGE equivalent: `/OUTIORQ` (board wins)

**v5** (text schematic PROF5-11):
- `/OUTIORQ = OR(/IORQ, CSAP5)`, where CSAP5 is active for every PROM-decoded internal port (decoder outputs F1, F2, F5, F6).
- So a card wired to `/OUTIORQ` never sees a cycle that the Profi's own peripheral decoder claims.

**v3.2:** the same structure (U15 OR U10), but U10's inputs were **not traced**.

**Insanity #08:** "ничего не надо мудрить с сигналом IORGE, вместо него подается сигнал OUTIORQ, — и все будет работать" (no need to bother with IORGE; feed OUTIORQ instead and everything works).

**Masked (proven) — the PROM group:**

| Device | Ports |
|---|---|
| VG93 | `#1F`, `#3F`, `#5F`, `#7F` (TR-DOS or CP/M) |
| FDC system register | `#FF` (TR-DOS), `#BF` (CP/M), `#3F` (extended map) |
| 8255 | `#1F`-`#7F` (DOS off; Kempston `#1F`) |
| v5 extended map (CP/M with ROM14 = 1) | VG93 `#83`-`#E3`, 8255 `#87`-`#E7`, IDE `#8B` / `#AB` / `#CB` / `#EB`, COM `#8F`-`#F3`, timer / control `#93` / `#B3`, RTC `#9F` / `#BF` / `#DF` / `#FF` |

**Not shown to be masked (unconfirmed):** `#FE`, `#7FFD`, `#DFFD`, AY and the palette. These are decoded on the CPU board.

**Fedin's modification:** `/EXTIORQ = OR(/OUTIORQ, CP/M)` on pin A7, so cards are cut off in CP/M entirely.

**A card can still use the raw `/IORQ` on B19.** It then collides with the board on the board's ports.

### 14.3 Read conflict

- The slot D lines are the **unbuffered CPU data bus**, with pull-ups ("на Profi хоть и отсутствуют буфера" — although Profi has no buffers, Insanity #08).
- A 555АП6 (v3.2 U31; v5 D8) separates the CPU bus from the local peripheral bus. It is enabled only for the internal selects (v3.2: `/OE = NOT NAND(FON, /BLOK, F1, F2, F4, F5)`; the v5 equations are partly ambiguous).
- A card on `/OUTIORQ` never overlaps the board.
- A card on the raw `/IORQ` fights the buffer, and there is no defined winner.

**Documented conflicts:**

| Conflict | Ports |
|---|---|
| MoonSound vs palette | `#7E` / `#7F` vs `#xx7E` |
| GS vs interrupt / COM control register | `#B3` (CP/M + ROM14 only) |
| Modem and Kempston mouse in CP/M | per Fedin |

**Palette address, sources disagree:**

| Source | Palette port |
|---|---|
| v5.0 album p.11 | "0FEH", CP/M with 80DS = 1 and BLOCK = 1 |
| [Karabas-Pro karabas_pro.vhd L1389](https://github.com/andykarpov/karabas-pro/blob/c210d6c/firmware/src/fpga/profi/rtl/karabas_pro.vhd#L1389), UnrealSpeccy, ZXMAK2, unreal-ng | `#xx7E` (A0 = 0, A7 = 0) with DS80 |

### 14.4 Built-in devices

| Device | Ports | Switch off? |
|---|---|---|
| AY-3-8912 / 8910 | `#FFFD` / `#BFFD` (short `#xxFD` in Sinclair mode) | Optional socket (v3.2 manual: "место для установки"). No jumper |
| VG93 FDC | as in §14.2 | v5: CP/M bit 5 of `#DFFD` moves it |
| 8255 (Centronics, Kempston) | `#1F`-`#7F`, extended `#87`-`#E7` | software only |
| IDE (v5) | extended `#8B`-`#EB` | extended map only |
| COM (v5) | `#8F`-`#F3` | extended map only |
| RTC (v5) | `#FF` / `#DF` (Sinclair map), `#9F`-`#FF` (extended) | active outside CP/M too |
| Palette (v5) | `#xx7E` (see §14.3) | No |
| Paging | `#7FFD`, `#DFFD` | No |

- v3 has no RTC, IDE, COM or palette.
- No device-disable jumpers are documented on either board.

## 15. Sprinter Sp2000

### 15.1 ISA-8 slots

Two slots, J6 and J7 ([Peters Plus schematic v1.62, sp2k_sch.pdf (archive)](https://web.archive.org/web/20031117033312/http://www.petersplus.com/download/sp2k_sch.pdf); local collection: sprinter/schematics/peters-plus/sp2k-sch.pdf). The standard ISA-8 pinout:

| Side | Signals |
|---|---|
| A | A1 /IOCHCHK, A2-A9 D7-D0, A10 IOCHRDY, A11 AEN, A12-A31 A19-A0 |
| B | GND, RESET DRV, +5 V, IRQ, -5 V, DRQ, -12 V, 0WS, +12 V, /MEMW, /MEMR, /IOW, /IOR, DACK, refresh, CLKOUT (the CPU clock), T/C, BALE, OSC |

How the slots connect to the CPU:
- **Access:** ISA is reached only through a **memory window**. Set `#1FFD` bit 4, then map page `#D0` / `#D2` (memory) or `#D4` / `#D6` (I/O) into window 3; `#9FBD` supplies A19-A14 (repository doc `docs/inprogress/2026-10-02-sprinter-isa/research.md` §4-5).
- **Wait:** IOCHRDY reaches `/WAIT` through a diode.
- **Interrupts and DMA:** IRQ and DRQ go to the Z84C15 PIO. There is no DMA controller.

**Sources disagree on two pins:**

| Pin | Peters schematic | INFO_012 and repository research |
|---|---|---|
| OSC (B30) | net BCLK14 from Z84C15 XTAL2 | "absent" |
| T/C | a labeled net, connected only at the slot pins | absent |

### 15.2 IORQGE and decode

- **The ISA bus has no IORQGE-like input.** The ACEX FPGA's inputs include no "card claims this cycle" line (`ACEX/SP2_ACEX.TDF` L24-60, [gitlab.com/sprinter-computer/hard](https://gitlab.com/sprinter-computer/hard)).
- The FPGA drives the data bus on every `IN` whose RAM-table decode code is not `#0x` / `#3x` (L408).
- Every Z80 port goes through the software-loaded decoder table (DCP), which decides which built-in device answers.
- ISA cards are not on the Z80 port path, so **a card can never compete with a built-in port**, and there is no read conflict.

### 15.3 ZX-bus cards on the Sprinter

- No native ZX-bus connector.
- Peters Plus sold an **"ISA -> Spectrum-BUS" adapter** (local collection: sprinter/peripherals/sys-docs/DOCS/INFO_014.TXT, option 9).
- A modern re-creation, "zxbus-mod" by solegstar, was built from photos of the original with a corrected pin offset. It does **not** make the card visible to Spectrum-mode software "as usual"; software maps it through the ISA window ([zx-pk.com t=21431](https://zx-pk.com/forum/viewtopic.php?t=21431)).
- MAME models the adapter as transparent: all ISA I/O becomes ZX-bus I/O for one slot, with no memory, IRQ, NMI or WAIT passed and no IORQGE ([isa/zxbus_adapter.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/isa/zxbus_adapter.cpp)).
- **No schematic of the adapter was found.**

### 15.4 Built-in devices

All sit behind the programmable decoder table, so software can move or remove each one. There are no jumpers (repository doc `docs/inprogress/2026-09-28-sprinter/hardware-reference.md`).

| Device | Ports |
|---|---|
| AY (in the FPGA) | `#FFFD` / `#BFFD` |
| Covox | `#FB`, `#4F` |
| Covox-Blaster | data `#FB` / `#4F`, control `#4E` |
| WD1793 | Beta ports |
| IDE | |
| Kempston mouse | `#FADF` / `#FBDF` / `#FFDF` |
| Kempston joystick | `#1F` |
| Paging | `#7FFD`, `#1FFD` |

---

## 16. Sinclair edge variants and adapters

### 16.1 Row naming

Sinclair's own manuals call the underside row **A** and the component side **B** (48K SM "/ROMCS ... pin 25A"; 128K SM power list). MAME exp.h uses the opposite letters. The table below uses **Upper** (component side: A15, A13, D7) and **Lower** (A14, A12, +5 V).

### 16.2 Pinout per model

Sources: [Sinclair Wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_edge_connector) model pages, MAME [exp.h](https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/exp.h) and the [k1 synopsis](https://k1.spdns.de/Develop/Projects/zxsp/Info/Hardware/zx81,%20zxsp,%20zx+2A%20%20edge%20connectors.txt). They agree except where noted. `=` means the same as the 48K, `—` means not connected.

| Pin | 48K | 128K UK | 128K Spanish | +2 grey | +2A / +3 |
|---|---|---|---|---|---|
| U1-U3 | A15, A13, D7 | = | = | = | = |
| U4 | — | — | — | — | **/ROM1OE** |
| U5 / L5 | key | key | key | key | key |
| U6-U12 | D0, D1, D2, D6, D5, D3, D4 | = | = | = | = |
| U13-U19 | /INT, /NMI, /HALT, /MREQ, /IORQ, /RD, /WR | = | = | = | = |
| U20 | -5 V | -5 V | -5 V | -5 V | **—** |
| U21 | /WAIT | = | = | = | = |
| U22 | +12 V | = | = | = | = |
| U23 | 12 V AC | = | = | = | **-12 V** |
| U24, U25 | /M1, /RFSH (not on issue 6A) | = | = | = | = |
| U26, U27 | A8, A10 | = | = | = | = |
| U28 | — | — | — | — | **RESET** (active high; k1 says /RESET) |
| L1-L3 | A14, A12, +5 V | = | = | = | = |
| L4 | +9 V | +9 V | +9 V | +9 V | **—** |
| L6, L7 | 0 V | = | = | = | = |
| L8 | /CK (ULA clock) | /CK | **—** | /CK (double-inverted) | **CKEXT** |
| L9-L12 | A0-A3 | = | = | = | = |
| L13 | **/IORQULA** | **—** | **—** | **/IORQGE** | **—** |
| L14 | 0 V | = | = | = | = |
| L15 | composite video | — | — | — | **/ROM2OE** |
| L16-L18 | /Y, V, U | — | — | — | **/DRD, /DWR, /MTR** |
| L19, L20 | /BUSRQ, /RESET | = | = | = | = |
| L21-L24 | A7, A6, A5, A4 | = | = | = | = |
| L25 | **/ROMCS** | /ROMCS | /ROMCS | /ROMCS | **—** |
| L26-L28 | /BUSACK, A9, A11 | = | = | = | = |

On every model, "an interface which uses only the signals shown should work on any model" ([generic edge wiki](https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_edge_connector)): A0-A15, D0-D7, the Z80 control lines, +5 V, +12 V, 0 V.

### 16.3 ZX-bus compared with the Sinclair edge, position by position

Derived by comparing the tables. The ZX-bus "A" row equals the Sinclair Lower row.

| Position | Sinclair | ZX-bus | Adapter must |
|---|---|---|---|
| L4 / A4 | +9 V (48K-+2) | /DOS (logic) | cut the pin (9 V on a 5 V input) |
| L5 / A5 | key slot | F 14 MHz (Kay) / +12 V (Scorpion per MAME) | mechanical adaptation |
| L8 / A8 | ULA clock, stretched by contention | CPU clock | synthesize or accept the difference |
| L13 / A13 | /IORQULA (48K), /IORQGE (+2), none (128K, +2A / +3) | IORQGE | pass through on 48K / +2 (ULA scope only); nothing possible elsewhere |
| L15, L16 / A15, A16 | video (48K); /ROM2OE, /DRD (+2A / +3) | RDR/, RS | isolate; build RDR/ from /ROMCS or ROM1OE / ROM2OE, RS from a `#7FFD` snoop |
| L25 / A25 | /ROMCS | CSR/ | map; split through diodes on +2A / +3 |
| U4 / B4 | /ROM1OE (+2A / +3) | BLK | isolate |
| U20, U22, U23 / B20, B22, B23 | -5 V, +12 V, 12 V AC (-12 V on +2A / +3) | /IODOS, NC, NC | route +12 V from U22 to B29 |

### 16.4 Known adapters

| Adapter | What it does | Source |
|---|---|---|
| **BDI 2.0 (Beta Disk clone, red v2.4)** | Pass-through ZX-BUS connector: "all system lines pass through except IORQ line - managed by beta disk interface". DIP switches for 48K, 128K, +2 grey, +2A / +3 and Harlequin. On +3 it needs its own ROM to replace the +3 ROM. This is the Sinclair-world form of DOS gating | [tlienhard forum](https://forum.tlienhard.com/phpBB3/viewtopic.php?t=2826) |
| **ZX Bus Protector (Velesoft)** | ROMCS to ROM1OE / ROM2OE conversion with diodes; 100-150 Ω data series resistors "to eliminate data collisions"; jumper for +12 V on the +9 V pin (+2A / +3) | [velesoft protector](https://velesoft.speccy.cz/protector.htm) |
| **"Fixer" (ZX FIXER)** | ORs /ROM1OE and /ROM2OE to /ROMCS on +2A / +3 | [k1 synopsis](https://k1.spdns.de/Develop/Projects/zxsp/Info/Hardware/zx81,%20zxsp,%20zx+2A%20%20edge%20connectors.txt) |
| **Spectrum Expert #02 ZX-BUS bus former** | 74LS245 buffers plus series resistors on IORQ, ROM CS, WAIT, INT, NMI and BUSRQ, to give ZX-bus slots to machines without them | [SE02](https://zxpress.ru/article.php?id=11759) |
| **UA3PRQ system bus buffer** | К555АП6 buffer plus a decoder enabled by OIRQ, with a disable-all switch (Pentagon / Leningrad class) | local collection: hardware/ZX_EXTEN.LZH |
| **ZX-RC2014 Bus Interface** | Edge to RC2014 backplane, not ZX-bus | [Tindie](https://www.tindie.com/products/quazar/zx-rc2014-bus-interface-for-the-zx-spectrum/) |

**Not found:** a documented, dedicated adapter from a ZX-bus / NemoBus card (GS, NeoGS, MoonSound, ZXM) to the Sinclair edge with a published schematic. The [atsidaev/zxbus](https://github.com/atsidaev/zxbus/blob/master/README.md) README says the 48K edge footprint "may still be used with minor limitations" on 128K machines.

---

## 17. Consequences for the slot design

These are proposals for the owner's review. They are not decided.

1. **Add an arbitration mode to `BusDeclaration`** next to `readRule`:

   | Mode | IORQGE effect |
   |---|---|
   | `CardWins` | IORQGE claims silence the board and lower slots (NemoBus, Pentagon-1024SL, Scorpion, ATM with adapter) |
   | `BoardWins` | board ports never reach cards; IORQGE only orders the slots (ZX-Evo Baseconf / TS-Conf, Profi `/OUTIORQ`) |
   | `UlaOnly` | IORQGE silences only the `#FE` decode (48K, +2 grey) |
   | `None` | no suppression (128K, +2A / +3) |

   Shadowing (R-COMP-4) exists only under `CardWins` (and for `#FE` under `UlaOnly`). Under `BoardWins`, a card port that equals a board port is **dead**: the card never sees it. That is an incompatibility, not a shadowing.
2. **Fix the built-in tables in [compatibility-matrix.md](compatibility-matrix.md) §4:**

   | Machine | Fix |
   |---|---|
   | ZX-Evo / TS-Conf | `ay` is a socketed YM2149, not an FPGA TurboSound. Baseconf passes `#FB` to cards; TS-Conf hides it |
   | ZX-MultiSound on ZX-Evo | Its `#FFFD` / `#BFFD` and SounDrive ports collide with the always-hidden `#xxFD` set and are dead unless `FREE_IORQ` is modeled as a machine option |
   | Pentagon | Choose the variant: the 1991 Pentagon 128 has no slot and no AY; Pentagon-1024SL v2.2 has 3 slots, "card wins" |
   | ATM 450 / 710 | Bus kind `atm-iobus` (2 x 12) plus optional `zxbus` through a CPU-socket adapter |
   | Profi | Bus kind `profi-bus` (64-pin), `BoardWins` on the PROM-decoded group; MoonSound vs palette clash stays |
   | Sprinter | ZX-bus only behind the ISA adapter, with no IORQGE and no Spectrum-mode visibility |
   | Scorpion | `zxbus` with a per-machine signal set (§11.1); not its own bus kind |
3. **Signals.** R-BUS-1's list should add `Rdrom` (ROM override input) separately from `CsRom` (output), and `Busrq` / `Busak`. `/IODOS` is present on no creatable machine.
4. **Read-conflict rule.** No machine documents a resolution. Keep wired-AND as the deterministic emulator rule, but label it "modeling choice" in the declaration and report every passive overlap with a built-in port as a clash.

   Machine-specific facts worth modeling:
   - **ZX-Evo:** drives `#FF` on unclaimed reads and INTA. A card with a non-`#FF` answer and no IORQGE2 ANDs with `#FF`, so the card's data wins under wired-AND. On real hardware this is a fight.
   - **48K / 128K / +2:** the ULA sits behind resistors, so the card wins against the ULA.
5. **IM2 vectors from cards.** Possible only on `CardWins` boards with pull-ups. ZX-Evo Baseconf forces `#FF`; TS-Conf drives its own vector.

---

## 18. Open / unconfirmed items

1. **Grey +2:** which ports its `/IORQGE` input suppresses (likely the ULA only). Also whether the 128K's AY sits on the CPU side of the ULA resistors.
2. **+2A / +3:** can a card override `/ROM1OE` / `/ROM2OE` directly, and through what resistance? Upper 28: RESET (wiki, MAME) or `/RESET` (k1)?
3. **ZX-Evo:** the polarity of CSROM / RDROM; the purpose of XS1 31A / 31B; rev D not checked pin by pin.
4. **Scorpion:**
   - Where +12 V sits on early boards: A5 (MAME) or not at all (scan, netlists).
   - Whether the earliest yellow boards had the IORQ-to-IORQGE resistor.
   - The yellow board's slot count and the bus expander.
   - A7 in the `#1F` decode.
   - The WD1793 chip-select gating.
   - Whether SMUC drives IORQGE.
   - The GMX bus.
5. **Profi:**
   - The v3.2 `/OUTIORQ` term (U10 inputs).
   - Whether `#FE`, `#7FFD`, `#DFFD`, AY and the palette are masked from `/OUTIORQ`.
   - The v5 D8 buffer equations.
   - Whether a card may drive `/BLOK` or `/ROMCS`.
   - The A17 voltage (-5 V or -12 V).
   - The meaning of the "X1A x6" sockets.
   - The palette port (`#xx7E` vs "0FEH").
6. **Pentagon:**
   - Any "Pentagon 512" bus.
   - The 1024SL v1.x schematic.
   - The 1024SL v2.2 `#7FFD` A15 bit (PDF text vs CPLD source) and jumpers X9 / X10.
7. **Kay-1024:** the board pinout and decoder (no schematic found).
8. **ATM:**
   - The 4.50 versus "Turbo 1 / Turbo 2" naming.
   - The X3 pin pairing (poor scan).
   - Which data bus the CPU-socket adapter taps.
9. **Sprinter:** the ISA to Spectrum-BUS adapter schematic (Peters Plus or solegstar): reset, `/INT`, `/WAIT`, `/M1`, clock and IORQGE handling; the BCLK14 frequency.
10. **NemoBus:**
    - Nemo's own standard document (promised in 1994, not found).
    - The MAME reading of 13B / 15B as unused.
    - The atsidaev active-low IORQGE label.
11. **Read conflicts:** no machine documents who wins. Measurement on real hardware would be needed to replace the wired-AND modeling choice.
12. **speccy.info:** [ZXBUS](https://speccy.info/ZXBUS), [Pentagon-1024SL](https://speccy.info/Pentagon-1024SL) and Scorpion pages not read (scripted access blocked; use a browser).

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
