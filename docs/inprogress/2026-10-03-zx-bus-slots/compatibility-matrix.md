# ZX-bus slots: compatibility and displacement matrix

| | |
|---|---|
| **Date** | 2026-10-03 (rewritten after research SL-0) |
| **Status** | Draft for owner review. This page is the readable form; the source of truth becomes the reference data collection in the code (`core/src/emulator/slots/refdata/`, [reference-data.md](reference-data.md)), from which these tables are generated and against which the plan engine is tested |
| **Evidence** | [research-machines.md](research-machines.md) (buses, arbitration, built-ins), [research-cards.md](research-cards.md) (card decode, IORQGE, conflicts) |
| **Rules** | [requirements.md](requirements.md) §3.3, [reference-data.md](reference-data.md) §5 (D1-D11), [open-questions.md](open-questions.md) Q1, Q2, Q5, Q7 |

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
| **A** | needs an adapter (or the override, then `unrealistic`) | D8 |
| `opt` | depends on the card's options | R-COMP-2 |

**Arbitration modes** (per bus, research-machines.md §1): `CardWins` (a card's IORQGE hides the cycle from the board),
`BoardWins` (the board hides its own ports from the slots), `UlaOnly` (IORQGE silences only `#FE`), `None`.

**Cycle detection** (per card): `Iorq` (default) or `RdWr` (sees cycles the board hides; the ZX-MultiSound).

## 1. Functions

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
| `serial.ef` / `serial.ee` | 16550 serial (ZX-WiFi) | `#xxEF` / `#xxEE` |
| `beta128` | TR-DOS disk interface | `#1F #3F #5F #7F #FF` (DOS) |
| `ide.nemo`, `ide.smuc`, `ide.divide`, ... | IDE interfaces | per card |
| `kempston-joystick`, `kempston-mouse` | input | `#1F`, `#FADF #FBDF #FFDF` |

## 2. Cards: first catalog

The first migration set (cards the emulator has today, plus the ZX-MultiSound). Later cards are in §5.

| Card id | Card | Bus | Needs | Detection | Functions | Ports (mask / match) | IORQGE | Options |
|---|---|---|---|---|---|---|---|---|
| `ay` | machine's own AY (socket default) | ay-socket | - | - | `ay-socket` | host decode | host | - |
| `ts` | TurboSound (NedoPC, 2 × AY) | ay-socket | - | - | `ay-socket` | host decode; chip select `#FC-#FF` | host | - |
| `tsfm` | TurboSound FM (NedoPC) | ay-socket | - | - | `ay-socket` | host decode; control `#F8-#FF` | host | - |
| `gs` | General Sound (classic) | zxbus | IORQGE | Iorq | `gs` | `#FF/#B3`, `#FF/#BB` (**no `#33`**) | reads only | RAM 128 K-2 M, ROM 1.04 / 1.05 |
| `gs-lw` | General Sound, lightweight player (emulator-only personality of `gs`) | zxbus | as `gs` | Iorq | `gs` | as `gs` | as `gs` | - |
| `neogs` | NeoGS | zxbus | IORQGE, /CSROM, /RDROM, /WAIT (ZX-DMA) | Iorq | `gs` (+ ZX-DMA) | `#FF/#B3`, `#FF/#BB`, `#FF/#33` | address only | RAM 2 / 4 MB, firmware | 
| `moonsound` | ZXM-MoonSound | zxbus | IORQGE (/IODOS optional) | Iorq | `opl4` | `#FC/#C4`, `#FE/#7E` | yes | JP1 (PentEvo) |
| `covox-fb` | Covox `#FB` | zxbus | - | Iorq | `covox-fb` | `#FF/#FB` | no | port width (A2-only homebrew) |
| `soundrive` | SounDrive 1.05 | zxbus | - | Iorq | mode 1: `soundrive`; mode 2: `soundrive` + `covox-fb` | mode 1 `#AF/#0F`; mode 2 `#F5/#F1` (emulator decode; Info Guide #4 gives a looser one, unconfirmed) | no | **S1 mode switch: 1 or 2** |
| `multisound` | ZX-MultiSound rev.A2 | zxbus | IORQGE, +12 V | **RdWr** | `ym` -> `ay-socket` (takeover) + `midi`; `saa`; `gs`; `sd` -> `soundrive` | multisound hardware reference §3.1 | `#FFFD` family, `#BFFD`, `#B3`, `#BB` | DIP `ym,saa,gs,sd`, `gsRam`, `ctrlMask` |
| `zxnetusb` | ZXNETUSB | zxbus | IORQGE, /CSROM | Iorq | `net.zxnetusb` | `#FF/#AB` | address only | - |
| `zx-wifi` | ZX-WiFi (izzx) | zxbus | IORQGE (v1.2+) | Iorq | `serial.ef` (`serial.ee` build) | `#FF/#EF` (`#FF/#EE`) | v1.2+ | port build, board version |

Facts that differ from today's emulator (each a follow-up row in [research-cards.md](research-cards.md) §2, not part of
the slots work): the classic GS decodes no `#33`; SounDrive modes are alternatives; the TurboSound chip-select range is
`#FC-#FF` on real hardware (`#FE` / `#FF` in the emulator); MoonSound decodes the low byte (emulator: 16-bit).

## 3. Card × card (any machine)

Row = card plugged in, column = card already installed. Socket cards (`ay`, `ts`, `tsfm`) share one socket, so plugging
one replaces the other.

| new \ installed | ts / tsfm (socket) | gs | gs-lw | neogs | moonsound | covox-fb | soundrive | multisound | zxnetusb | zx-wifi |
|---|---|---|---|---|---|---|---|---|---|---|
| **ts / tsfm** | replaces | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** if `ym` (the bus card would shadow the socket) | ✓ | ✓ |
| **gs** | ✓ | **D** | **D** | **D** | ✓ | ✓ | ✓ (mode 2 loose decode: **P**, unconfirmed) | **D** if `gs` | ✓ | ✓ |
| **gs-lw** | ✓ | **D** | **D** | **D** | ✓ | ✓ | as `gs` | **D** if `gs` | ✓ | ✓ |
| **neogs** | ✓ | **D** | **D** | **D** | ✓ | ✓ | as `gs` | **D** if `gs` | ✓ | ✓ |
| **moonsound** | ✓ | ✓ | ✓ | ✓ | **D** | ✓ | ✓ | ✓ | ✓ | ✓ |
| **covox-fb** | ✓ | ✓ | ✓ | ✓ | ✓ | **D** | **D** if mode 2 | ✓ | ✓ | ✓ |
| **soundrive** | ✓ | ✓ | ✓ | ✓ | ✓ | **D** if mode 2 | **D** | **D** if `sd` | ✓ | ✓ |
| **multisound** | **⊘** if `ym` | **D** if `gs` | **D** if `gs` | **D** if `gs` | ✓ | ✓ | **D** if `sd` | **D** | ✓ | ✓ |
| **zxnetusb** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** | ✓ |
| **zx-wifi** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **D** |

## 4. Card × machine

Bus and arbitration per machine (research-machines.md §3-15), then the outcome for each first-catalog card. Built-in
outcomes use the codes of §0; "dead" = the card's ports are board ports hidden from the slots.

| Machine (model) | Card bus | Arbitration | +12 V | Built-ins that matter |
|---|---|---|---|---|
| 48K | `sinclair-edge` | UlaOnly | yes | beeper / ULA `#FE` |
| 128K, +2 | `sinclair-edge` | None (+2 IORQGE scope unconfirmed) | yes | AY (on the board) |
| +2A, +3 | `sinclair-edge` (+3 variant: no /ROMCS, no IORQGE) | None | yes | AY, +3 FDC |
| PENTAGON (1024SL class) | `zxbus` (3 slots) | CardWins | yes | AY socket, Beta-128, Kempston, all behind IORQG |
| SCORPION, PROFSCORP | `zxbus` (Scorpion pinout) | CardWins | Turbo+ boards only | AY, Beta-128, SMUC |
| ATM450, ATM710 | `atm-iobus`; `zxbus` only through the CPU-socket adapter | adapter: CardWins | adapter | AY, Beta-128, ATM IDE |
| ATM3 (ZX-Evo Baseconf) | `zxbus` (2 slots) | BoardWins | jumper J4 | **socketed YM2149**; board ports: `#FE #F6 #FC`, every `#xxFD`, `#1F`, `#DF`, `#EF`, `#57 #77`, `#F7`, `#BF-#BD`, IDE; passes `#FB` (Covox written internally and passed on), `#B3 #BB`, `#FF` |
| TSL (TS-Conf) | `zxbus` | BoardWins | jumper J4 | as Baseconf, but `#FB` and `#AF` hidden, `#F6 #FC #BF-#BD` passed |
| PROFI, PROFI3 | `profi-bus` (64-pin, not ZX-bus) | BoardWins (`/OUTIORQ`) | ? | AY, palette `#7E` (port unconfirmed: `#xx7E` vs "0FEH") |
| SPRINTER | `isa8`; `zxbus` through the ISA adapter | adapter | adapter | AY, Covox-Blaster |

| Card \ machine | 48K | 128K / +2 | +2A / +3 | PENTAGON | SCORPION | ATM450 / 710 | ATM3 (Evo) | TSL | PROFI | SPRINTER |
|---|---|---|---|---|---|---|---|---|---|---|
| **ts / tsfm** | X (no AY socket) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ (into the YM2149 socket) | ✓ | ✓ | ✓ |
| **gs, gs-lw** | A | A | A | ✓ | ✓ | A | ✓ (`#B3 #BB` passed) | ✓ | A (unconfirmed) | A (ISA adapter) |
| **neogs** | A | A | A | ✓ | ✓ | A | ✓ | ✓ | A | A |
| **moonsound** | A | A | A | ✓ | ✓ | A | ✓ | ✓ | **X** (`#7E` palette, board wins) | A |
| **covox-fb** | A | A | A | ✓ (shadows nothing: Pentagon Covox is a card) | ✓ | A | ✓ both play (board passes `#FB`) | **X** (`#FB` hidden, board Covox) | A | A |
| **soundrive** mode 1 | A | A | A | ✓ (`#1F` vs Beta: DOS-gated built-in) | ✓ | A | partly dead: channel `#1F` is a board port | partly dead (`#1F`) | A | A |
| **soundrive** mode 2 | A | A | A | ✓ | ✓ | A | ✓ (`#FB` also to the board Covox) | partly dead (`#FB`) | A | A |
| **multisound** | A | A | A | **S** (AY socket shadowed) | ✓ on Turbo+ (12 V), **S** | A | **R** (YM2149 out of its socket, Q7) | **R** | A | A |
| **zxnetusb** | A | A | A | ✓ | ✓ | A | ✓ | ✓ | A | A (Sprinter network design) |
| **zx-wifi** | A | A | A | ✓ | ✓ (board v1.2+: `#FF` conflict before) | A | **X** (`#EF` board COM; `#EE` build ✓) | **X** (`#EF` ZiFi; `#EE` build ✓) | A | A |

**PENTAGON** in the emulator is one model for 128 / 512 / 1024 K; the slot declaration follows the 1024SL class (3
slots, CardWins). The 1991 Pentagon 128 has no CPU bus connector at all (research-machines.md §8); if a "classic
Pentagon 128" variant is ever modeled, it declares no `zxbus`.

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
