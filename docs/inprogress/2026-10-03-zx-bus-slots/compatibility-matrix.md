# ZX-bus slots: compatibility and displacement matrix

| | |
|---|---|
| **Date** | 2026-10-03 (rewritten after research SL-0) |
| **Status** | Draft for owner review. This page is the readable form; the source of truth becomes the reference data collection in the code (`core/src/emulator/slots/refdata/`, [reference-data.md](reference-data.md)), from which these tables are generated and against which the plan engine is tested |
| **Evidence** | [research-machines.md](research-machines.md) (buses, arbitration, built-ins), [research-cards.md](research-cards.md) (card decode, IORQGE, conflicts) |
| **Rules** | [requirements.md](requirements.md) §3.3, [reference-data.md](reference-data.md) §5 (D1-D12), [open-questions.md](open-questions.md) Q1, Q2, Q5, Q7 |
| **Generated** | The tables of §1-§4 are generated from the collection (`core/src/emulator/slots/slotmatrix.cpp`) and sit between `slots:generated` markers; `SlotMatrix_Test.MatrixMatchesDocs` fails when they differ. Do not edit them by hand: change the collection and regenerate (`UNREAL_SLOTS_MATRIX_UPDATE=1 tools/build/test.sh --gtest_filter='SlotMatrix_Test.MatrixMatchesDocs'`) |

## 0. How to read it

**Outcome codes** (what happens to an installed device when a card is plugged in):

| Code | Meaning | Rule |
|---|---|---|
| ✓ | coexist | - |
| **D** | the installed card is displaced (shared function) | D1 |
| **⊘** | pointless pair: the socket card would be shadowed; it is displaced and the socket returns to the machine's chip | D3 |
| **S** | the built-in is shadowed (silent, still fitted) | D2, D6 (`CardWins` buses only) |
| **X** | refused: a fixed built-in holds the function, or the card's ports are dead on this board | D4, §2 |
| **R** | a socketed built-in chip is taken out (bus fight otherwise) | Q7 |
| **P** | both stay; the later card is disabled for an accidental port clash | D7 |
| **A** | needs an adapter (or the override, then `unrealistic`); "needs +12V" names a signal missing on a native bus | D8 |
| replaces | a card put into an occupied slot replaces its content (socket boards) | - |
| partly dead | allowed, but some of the card's ports are board ports hidden from the slots (`BoardWins`, `Iorq` card) | D4 |
| `opt` | depends on the card's options | R-COMP-2 |

**Arbitration modes** (per bus, research-machines.md §1): `CardWins` (a card's IORQGE hides the cycle from the board),
`BoardWins` (the board hides its own ports from the slots), `UlaOnly` (IORQGE silences only `#FE`), `None`.

**Cycle detection** (per card): `Iorq` (default) or `RdWr` (sees cycles the board hides; the ZX-MultiSound).

## 1. Functions

One row per `Function` value of the vocabulary (`slotvocabulary.cpp`); the built-in-only functions (`palette`,
`fdc.upd765`, `rtc`, ...) let the plan refuse a card that would need a fixed built-in's role.

<!-- slots:generated:functions:begin -->
| Function | Meaning | Ports involved |
|---|---|---|
| `ay-socket` | the AY role (AY, TurboSound, TSFM, or a bus card taking it over) | `#FFFD`, `#BFFD` (`#C002` decodes) |
| `gs` | General Sound host interface | `#B3`, `#BB` (+ `#33` on NeoGS / ZXM-GS) |
| `saa` | SAA1099 | `#FF` data, `#1FF` address |
| `soundrive` | 4-channel DAC, SounDrive layout | mode 1 `#0F #1F #4F #5F`; mode 2 `#F1 #F3 #F9 #FB` |
| `covox-fb` | 8-bit DAC on `#FB` | `#FB` |
| `covox-dd` | Scorpion Covox | `#DD` |
| `opl4` | OPL4 (MoonSound) | `#C4-#C7`, `#7E`, `#7F` |
| `midi` | General MIDI synthesizer fed from the AY / YM I/O port | (AY register 14) |
| `net.zxnetusb` | ZXNETUSB | `#AB` |
| `serial.ef` | 16550 serial (ZX-WiFi, ZX-Evo COM, TS-Conf ZiFi) | `#xxEF` |
| `serial.ee` | 16550 serial, ZX-WiFi `#EE` build | `#xxEE` |
| `beta128` | TR-DOS disk interface | `#1F #3F #5F #7F #FF` (DOS) |
| `ide.nemo` | Nemo IDE | `#10-#F0`, `#11`, `#C8` |
| `ide.smuc` | SMUC IDE (Scorpion) | `#xxBE` family (DOS) |
| `ide.divide` | DivIDE | `#A3-#BF`, `#E3` |
| `ide.atm` | ATM Turbo IDE | `#xxEF` (DOS) |
| `ide.profi` | Profi v5 IDE | `#8B #AB #CB #EB` (extended map) |
| `kempston-joystick` | Kempston joystick | `#1F` |
| `kempston-mouse` | Kempston mouse | `#FADF #FBDF #FFDF` |
| `sd.zc` | Z-Controller SD card | `#57`, `#77` |
| `rtc` | real-time clock | per machine (ZX-Evo `#BFF7 #DFF7 #EFF7`) |
| `palette` | Profi v5 palette | `#xx7E` |
| `fdc.upd765` | +3 floppy controller | `#2FFD`, `#3FFD` |
<!-- slots:generated:functions:end -->

## 2. Cards: first catalog

The first migration set (cards the emulator has today, plus the ZX-MultiSound). Later cards are in §5.

<!-- slots:generated:cards:begin -->
| Card id | Card | Bus | Needs | Detection | Functions | Ports (mask / match) | IORQGE | Options |
|---|---|---|---|---|---|---|---|---|
| `ay` | machine's own AY (socket default) | ay-socket | - | - | `ay-socket` | host decode | host | - |
| `ts` | TurboSound (NedoPC, 2 x AY) | ay-socket | - | - | `ay-socket` | host decode; chip select `#FC-#FF` | host | - |
| `tsfm` | TurboSound FM (NedoPC) | ay-socket | - | - | `ay-socket` | host decode; control `#F8-#FF` | host | - |
| `gs` | General Sound (classic) | zxbus | IORQGE | Iorq | `gs` | `#FF/#B3`, `#FF/#BB` | reads only | `ram` = `128k` / `256k` / `512k` / `1m` / `2m` (default `128k`); `rom` = `1.04` / `1.05` (default `1.05`) |
| `gs-lw` | General Sound, lightweight player (emulator-only personality of `gs`) | zxbus | IORQGE | Iorq | `gs` | `#FF/#B3`, `#FF/#BB` | reads only | - |
| `neogs` | NeoGS | zxbus | IORQGE, /WAIT, /CSROM, /RDROM | Iorq | `gs` | `#FF/#B3`, `#FF/#BB`, `#FF/#33` | yes | `ram` = `2m` / `4m` (default `2m`) |
| `moonsound` | ZXM-MoonSound | zxbus | IORQGE | Iorq | `opl4` | `#FC/#C4` (JP1 open, non-DOS), `#FE/#7E` (JP1 open, non-DOS), `#FC/#C4` (JP1 fitted), `#FE/#7E` (JP1 fitted) | yes | `jp1` = `open` / `fitted` (default `open`) |
| `covox-fb` | Covox `#FB` | zxbus | - | Iorq | `covox-fb` | `#FF/#FB` (full decode, write), `#04/#00` (A2-only decode, write) | no | `decode` = `full` / `a2` (default `full`) |
| `soundrive` | SounDrive 1.05 | zxbus | - | Iorq | `soundrive`, `covox-fb` if mode 2 or modes 1 + 2 (emulator decode) | `#AF/#0F` (mode 1 or modes 1 + 2 (emulator decode), write), `#F5/#F1` (mode 2 or modes 1 + 2 (emulator decode), write) | no | `mode` = `1` / `2` / `both` (default `1`) |
| `multisound` | ZX-MultiSound rev.A2 | zxbus | IORQGE, +12V | RdWr | `ay-socket` (takeover) if `ym`, `midi` if `ym`, `saa` if `saa`, `gs` if `gs`, `soundrive` if `sd` | `#E00F/#E00D` (`ym`), `#E00F/#C00D` (`ym`), `#C00F/#800D` (`ym`, write), `#FF/#FF` (`saa`, write, ROM lock), `#FF/#B3` (`gs`), `#FF/#BB` (`gs`), `#AF/#0F` (`sd`, write, ROM lock) | `#FFFD`, `#BFFD`, `#B3`, `#BB` | `dip` = any of `ym`, `saa`, `gs`, `sd` (default all); `gsRam` = `1m` / `2m` (default `1m`); `ctrlMask` = `pro` / `classic` (default `pro`) |
| `zxnetusb` | ZXNETUSB | zxbus | IORQGE, /CSROM | Iorq | `net.zxnetusb` | `#FF/#AB` | yes | - |
| `zx-wifi` | ZX-WiFi (izzx) | zxbus | IORQGE | Iorq | `serial.ef` if `#EF` build, `serial.ee` if `#EE` build | `#FF/#EF` (`#EF` build), `#FF/#EE` (`#EE` build) | yes | `port` = `ef` / `ee` (default `ef`) |
<!-- slots:generated:cards:end -->

Notes on the generated table: `#FF/#B3` = mask / match; the conditions in brackets are the option values a claim or a
function needs, `non-DOS` = gated off in DOS mode, `write` / `read` = one direction only. "yes" in the IORQGE column
includes the address-only IORQGE of NeoGS and ZXNETUSB (formed without /M1); the plan does not need that
distinction. SounDrive uses the emulator decode; Black_Cat's Info Guide #4 gives a looser one (mode 1: A0 = 1, A5 = 0;
mode 2: A0 = 1, A2 = 0), unconfirmed, under which mode 2 would also hear the GS ports (`#B3`, `#BB`, `#33`). The
ZX-WiFi is modeled as board v1.2+ (IORQGE); v1.0-1.1 boards lack it and fight the Scorpion Turbo+ `#FF` decode.
Not modeled as options yet: NeoGS firmware, GS overclock.

Facts that differ from today's emulator (each a follow-up row in [research-cards.md](research-cards.md) §2, not part of
the slots work): the classic GS decodes no `#33`; SounDrive modes are alternatives; the TurboSound chip-select range is
`#FC-#FF` on real hardware (`#FE` / `#FF` in the emulator); MoonSound decodes the low byte (emulator: 16-bit).

## 3. Card × card (any machine)

Row = card plugged in, column = card already installed. Socket cards (`ay`, `ts`, `tsfm`) share one socket, so plugging
one replaces the other. Computed by the plan engine on the PENTAGON declaration (a `CardWins` ZX-bus and an AY socket):
the installed card in `zxbus.1` (or the socket), the new one into `zxbus.2`. A condition ("if `gs`", "if mode 2")
names the option value of the row's or the column's card that produces the outcome; a card against its own kind is
computed at its defaults.

<!-- slots:generated:card-x-card:begin -->
| new \ installed | `ts` | `tsfm` | `gs` | `gs-lw` | `neogs` | `moonsound` | `covox-fb` | `soundrive` | `multisound` | `zxnetusb` | `zx-wifi` |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **ts** | replaces | replaces | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** if `ym` | ✓ | ✓ |
| **tsfm** | replaces | replaces | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** if `ym` | ✓ | ✓ |
| **gs** | ✓ | ✓ | **D** | **D** | **D** | ✓ | ✓ | ✓ | **D** if `gs` | ✓ | ✓ |
| **gs-lw** | ✓ | ✓ | **D** | **D** | **D** | ✓ | ✓ | ✓ | **D** if `gs` | ✓ | ✓ |
| **neogs** | ✓ | ✓ | **D** | **D** | **D** | ✓ | ✓ | ✓ | **D** if `gs` | ✓ | ✓ |
| **moonsound** | ✓ | ✓ | ✓ | ✓ | ✓ | **D** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **covox-fb** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** | **D** if mode 2 or modes 1 + 2 (emulator decode) | ✓ | ✓ | ✓ |
| **soundrive** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** if mode 2 or modes 1 + 2 (emulator decode) | **D** | **D** if `sd` | ✓ | ✓ |
| **multisound** | **⊘** if `ym` | **⊘** if `ym` | **D** if `gs` | **D** if `gs` | **D** if `gs` | ✓ | ✓ | **D** if `sd` | **D** | ✓ | ✓ |
| **zxnetusb** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** | ✓ |
| **zx-wifi** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** |
<!-- slots:generated:card-x-card:end -->

## 4. Card × machine

Bus and arbitration per machine (research-machines.md §3-15), then the outcome for each first-catalog card. Built-in
outcomes use the codes of §0; "dead" = the card's ports are board ports hidden from the slots. One row per creatable
model (`refdata/machines.cpp`), then the card × machine outcomes: the plan of plugging the card (default options; one row
per value of a mode / port-build option) into an empty machine, in the slot the planner suggests; adjacent machines
with identical columns are merged.

<!-- slots:generated:machines:begin -->
| Model | Board | Buses (physical slots) | Arbitration | +12 V | Built-ins | Board ports hidden from the slots | Notes |
|---|---|---|---|---|---|---|---|
| 48K | Sinclair 16K / 48K | `ay-socket`, `edge` (sinclair-edge, 1) | UlaOnly | yes | `ula` | - | `ay-socket`: no AY on the 48K board: an AY interface on the edge connector is retrofitted (128K decode) |
| 128K | Sinclair 128K (UK / Spanish) | `ay-socket`, `edge` (sinclair-edge, 1) | None | yes | `ay`, `ula` | - | - |
| PLUS2 | Amstrad grey +2 | `ay-socket`, `edge` (sinclair-edge, 1) | UlaOnly | yes | `ay`, `ula` | - | `edge`: lower 13 is /IORQGE; its scope beyond the ULA is unconfirmed |
| PLUS2A | Amstrad +2A | `ay-socket`, `edge` (sinclair-edge, 1) | None | yes | `ay`, `ula` | - | `edge`: no /ROMCS (/ROM1OE + /ROM2OE), no IORQGE |
| PLUS3 | Amstrad +3 | `ay-socket`, `edge` (sinclair-edge, 1) | None | yes | `ay`, `ula`, `fdc` | - | `edge`: no /ROMCS (/ROM1OE + /ROM2OE), no IORQGE |
| PENTAGON | Pentagon 128 (1991): no expansion connector, ZX-bus retrofitted | `ay-socket`, `zxbus` (retrofit, no physical slots) | CardWins | yes | `ay`, `beta128`, `kempston-joystick` | - | `zxbus`: no expansion connector on the Pentagon 128 board: the ZX-bus is retrofitted (NemoBus rules) |
| SCORPION | Scorpion ZS-256 yellow board | `ay-socket`, `zxbus` (1) | CardWins | no | `ay`, `beta128`, `kempston-joystick` | - | `zxbus`: +12 V only on the control port |
| PROFSCORP | Scorpion ZS-256 Turbo+ with ProfROM | `ay-socket`, `zxbus` (2) | CardWins | yes | `ay`, `beta128`, `kempston-joystick` | - | `zxbus`: Turbo+ board: +12 V on B22 (B29 through J6) |
| ATM450 | ATM Turbo 2 v4.50 | `ay-socket`, `iobus` (atm-iobus, 1), `cpu-socket` (1) | `iobus`: None; `cpu-socket`: None (adapter `atm-cpu-socket-zxbus`: CardWins) | `iobus`: yes; `cpu-socket`: no | `ay`, `beta128`, `covox` (switchable), `adc` | - | - |
| ATM710 | ATM Turbo 2+ v7.10 | `ay-socket`, `iobus` (atm-iobus, 2), `cpu-socket` (1) | `iobus`: None; `cpu-socket`: None (adapter `atm-cpu-socket-zxbus`: CardWins) | `iobus`: yes; `cpu-socket`: no | `ay` (socketed AY-3-8912), `beta128`, `ide`, `covox` (switchable), `adc` | - | - |
| ATM3 | ZX-Evolution rev C, Baseconf | `ay-socket`, `zxbus` (2) | BoardWins | yes | `ay` (socketed YM2149), `beta128`, `kempston-joystick`, `kempston-mouse`, `sd-zc`, `ide-nemo` (switchable), `rtc`, `covox` (switchable), `com` | `#FE` `#F6` `#FC` `#FD` `#DF` `#1F` `#F7` `#77` `#57` `#BF` `#BE` `#BD` `#EF` `#1F/#10` `#11` `#C8` `#FF` (DOS) | `zxbus`: +12 V only with jumper J4 |
| TSL | ZX-Evolution rev C, TS-Conf (FREE_IORQ off) | `ay-socket`, `zxbus` (2) | BoardWins | yes | `ay` (socketed YM2149), `beta128`, `kempston-joystick`, `kempston-mouse`, `sd-zc`, `ide-nemo` (switchable), `ts-registers`, `covox` (switchable), `zifi` | `#FE` `#AF` `#FD` `#FB` `#F7` (non-DOS) `#1F/#10` `#11` `#C8` `#1F` `#3F` (DOS) `#5F` (DOS) `#7F` (DOS) `#FF` (DOS) `#DF` `#77` `#57` `#EF` | `zxbus`: +12 V only with jumper J4; the slots never see INTA |
| PROFI | Profi v5 | `ay-socket`, `profi-bus` (1) | BoardWins | yes | `ay` (socketed AY-3-8912), `beta128`, `ppi8255`, `palette`, `rtc`, `ide`, `covox` (switchable) | `#9F/#1F` `#FF` (DOS) | `profi-bus`: /OUTIORQ masks the PROM-decoded ports; `#FE`, `#7FFD`, `#DFFD`, AY, palette not shown masked |
| PROFI3 | Profi v3.2 | `ay-socket`, `profi-bus` (1) | BoardWins | yes | `ay` (socketed AY-3-8912), `beta128`, `ppi8255`, `covox` (switchable) | `#9F/#1F` `#FF` (DOS) | `profi-bus`: /OUTIORQ masks the PROM-decoded ports; `#FE`, `#7FFD`, `#DFFD`, AY, palette not shown masked |
| SPRINTER | Peters Plus Sprinter Sp2000 | `ay-socket`, `isa` (isa8, 2) | None (adapter `sprinter-isa-zxbus`: None) | yes | `ay` (switchable), `covox-blaster` (switchable), `beta128` (switchable), `kempston-mouse` (switchable), `kempston-joystick` (switchable) | - | `ay-socket`: the AY is in the FPGA; the socket is the emulator's TurboSound place; `isa`: reached through a memory window; ISA cards never compete with a Z80 port |
<!-- slots:generated:machines:end -->

<!-- slots:generated:card-x-machine:begin -->
| Card \ machine | 48K / 128K / PLUS2 / PLUS2A / PLUS3 | PENTAGON | SCORPION | PROFSCORP | ATM450 / ATM710 | ATM3 | TSL | PROFI | PROFI3 / SPRINTER |
|---|---|---|---|---|---|---|---|---|---|
| **ts** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **tsfm** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **gs** | A | ✓ | ✓ | ✓ | A | ✓ | ✓ | A | A |
| **gs-lw** | A | ✓ | ✓ | ✓ | A | ✓ | ✓ | A | A |
| **neogs** | A | ✓ | ✓ | ✓ | A | ✓ | ✓ | A | A |
| **moonsound** | A | ✓ | ✓ | ✓ | A | ✓ | ✓ | **X** (`#7E` palette, board wins) | A |
| **covox-fb** | A | ✓ | ✓ | ✓ | A | ✓ | **X** (`#FB` is the board's `covox`) | A | A |
| **soundrive** mode 1 | A | ✓ | ✓ | ✓ | A | partly dead (`#1F` is a board port) | partly dead (`#1F` is a board port) | A | A |
| **soundrive** mode 2 | A | ✓ | ✓ | ✓ | A | ✓ | partly dead (`#FB` is a board port) | A | A |
| **soundrive** modes 1 + 2 (emulator decode) | A | ✓ | ✓ | ✓ | A | partly dead (`#1F` is a board port) | partly dead (`#1F`, `#FB` are board ports) | A | A |
| **multisound** | A | **S** (`ay`) | A (needs +12V) | **S** (`ay`) | A | **R** (`ay` YM2149 out of its socket) | **R** (`ay` YM2149 out of its socket) | A | A |
| **zxnetusb** | A | ✓ | ✓ | ✓ | A | ✓ | ✓ | A | A |
| **zx-wifi** `#EF` build | A | ✓ | ✓ | ✓ | A | **X** (`#EF` is the board's `com`) | **X** (`#EF` is the board's `zifi`) | A | A |
| **zx-wifi** `#EE` build | A | ✓ | ✓ | ✓ | A | ✓ | ✓ | A | A |
<!-- slots:generated:card-x-machine:end -->

Notes the cells do not carry: the Pentagon has no Covox of its own (a Covox there is a card); a SounDrive mode-1 card
on the Pentagon co-receives the Beta-128 writes to `#1F` / `#5F` in DOS (a write, no fight); on the ZX-Evo Baseconf a
Covox card and the board Covox both play (the board writes `#FB` internally and passes it on); the ISA adapter of the
Sprinter reaches ZX-bus cards through the ISA window, not as Spectrum ports (the Sprinter network design covers
ZXNETUSB there); whether a GS works on the Profi bus through an adapter is unconfirmed.

**PENTAGON** is the Pentagon 128 as chosen from the menu (models come only from the menu, each with its own photo).
The 1991 board has no CPU bus connector (research-machines.md §8), so its ZX-bus is declared as **retrofitted**: no
physical slots, NemoBus rules (card wins), and every report and the Qt slot window say in text that the cards are
bolted on. Any other model without a physical connector for a bus gets the same treatment.

## 5. Later cards (catalog only, no slot code in the first migration)

AY interfaces for the 48K (Melodik `#C002`, Fuller `#3F/#5F/#7F`, DK'tronics, ZON X), PoS TurboSound, Quadro-AY,
ZXM-SoundCard Light / Middle / Extreme (control byte `#F0-#FF`, YM read shadow only, all three SounDrive sets), ZXM-
GeneralSound (`#33` bit 4 = disable), standalone SAA interfaces, Covox `#DD` (Scorpion), Beta 128 edge card, DivIDE,
DivMMC, Nemo IDE (IORQGE on the whole `#06/#00` group outside DOS: shadows `#FE` / `#7FFD` decodes on CardWins
machines), SMUC, Z-Controller, Multiface 1 / 128 / 3, Kempston joystick / mouse interfaces (incl. K-Mouse Turbo). Their
decode, functions and known conflicts are in research-cards.md §5-9; each joins the data when its card is built.

## 6. Worked plans

**A. One card displaces several (Pentagon).** Installed: socket `tsfm`, `zxbus.1 = gs`, `zxbus.2 = soundrive` (mode 2).
Plug `multisound` (all DIP on) into `zxbus.3`:

| Item | Content |
|---|---|
| removed | `zxbus.1 gs` (`gs`), `zxbus.2 soundrive` (`soundrive`) |
| socket | `tsfm` -> `ay` (⊘, reported with its options) |
| shadowed | the socket's `ay` (by `zxbus.3`) |
| lost functions | `covox-fb` (mode 2 offered `#FB`; the MultiSound decodes mode-1 ports only) |
| automation without the flag | refused (HTTP 409 with this plan) |
| with `replaceIfIncompatible` / Qt | applied by a restart (Q6); the reply / warning lists all of it |

**B. Options avoid the clash.** Installed `neogs`; plug `multisound` with `dip=ym,saa,sd`: both stay.

**C. Option change later.** Then `slots set zxbus.2 dip=ym,saa,gs,sd`: `neogs` displaced; its `sd.ngs` medium follows
the stranded-media rules (dirty without a disposition -> refused).

**D. Replacing with less.** Installed `multisound` (all on); put `tsfm` into the socket: `multisound` displaced (⊘ the
other way round), lost `gs`, `saa`, `soundrive`, `midi`.

**E. ZX-Evo (Q7).** Plug `multisound` on ATM3: plan "take the YM2149 out of its socket" (**R**); without it the card
and the YM2149 both drive `#FFFD` reads. Its SounDrive channel `#1F` works (RdWr card sees board ports), unlike an
`Iorq` SounDrive card's.

**F. Board wins (TS-Conf).** Plug `zx-wifi` (`#EF` build) on TSL: refused, "`#EF` is a board port (ZiFi) hidden from
the slots; use the `#EE` build". Plug `covox-fb`: refused, "`#FB` is the board Covox, hidden from the slots".

**G. Bus fit (128K).** Plug `multisound`: `zxbus` card on `sinclair-edge` -> adapter needed (the 128K edge has no
IORQGE). Qt explains and asks to confirm; automation refuses without the flag; with it the fit is `unrealistic`.

**H. Fixed built-in (Profi).** Plug `moonsound`: refused even with the flag, "`#7E` is the Profi palette port; the board
wins".
