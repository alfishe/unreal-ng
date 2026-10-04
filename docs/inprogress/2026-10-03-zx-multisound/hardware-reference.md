# ZX-MultiSound: hardware reference

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Card** | ZX-MultiSound by Eugene Lozovoy (UzixLS), NemoBus / ZX-bus sound card |
| **Modeled variant** | rev.A2 with the current master CPLD firmware (open-questions Q5: the least buggy variant) |
| **Sources** | [UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound) (commit `d7f3ac2`, 2026-06-17): `cpld/rtl/top.v`, `pcb/rev.*/zx-multisound.kicad_sch`, `pcb/rev.*/ERRATA.txt`, `README.md`, issues [#6](https://github.com/UzixLS/zx-multisound/issues/6), [#7](https://github.com/UzixLS/zx-multisound/issues/7), [#9](https://github.com/UzixLS/zx-multisound/issues/9), [#11](https://github.com/UzixLS/zx-multisound/issues/11). The schematic netlists were extracted without KiCad (every pin accounted for; CPLD pins cross-checked against `cpld/syn/rev_A1.qsf`) |
| **Compare with** | [TurboSound FM hardware reference](../2026-09-10-turbosound-fm/hardware-reference.md) |

Reference tags: **[RTL]** CPLD Verilog `top.v`, **[SCH]** rev.A2 schematic, **[ISS]** GitHub issue, **[RM]** README / errata.

## Glossary

| Term | Meaning |
|---|---|
| **NemoBus / ZX-bus** | The 2×30 edge connector of Pentagon-class machines and the ZX-Evo, with IORQGE and /IODOS. |
| **IORQGE** | Bus signal a card drives when it answers a port; the machine's own decoder then stays silent. On this card it is **active high** (a buffer with its input tied to +5 V, enabled by the CPLD) [SCH]. |
| **DDS** | Direct digital synthesis: a clock made by adding a constant to an accumulator at a faster clock and taking its top bit. Average frequency exact, period jitters by one fast clock. |
| **SSG** | The YM2203's AY-compatible part (three square-wave channels A, B, C, noise, envelope). |
| **CPLD** | The programmable logic chip (EPM3256) that holds all decoding and glue logic. |

## 1. Board summary

| Block | Parts | Clock |
|---|---|---|
| TurboSound FM | 2 × YM2203 (U4, U10) + 2 × YM3014B DAC (U6, U12) | YM_M 3.5 MHz average (32 MHz × 7/64 DDS) [RTL][SCH] |
| General Sound | Z80 (QFP), 27C512 ROM (GS 1.05b), 2 × AS6C4008 (1 MB; 2 MB with the `rev_A1_2mb` firmware and four chips selected) | 16 MHz (32 MHz / 2) [RTL] |
| SAA1099 | SAA1099 | 8 MHz (32 MHz / 4), gated by control bit 3 [RTL] |
| SounDrive | 4 channels, shared with the GS DACs | - |
| MIDI synthesizer | Dream SAM2695 | 12 MHz average (32 MHz × 3/8 DDS), no crystal [SCH] |
| DACs | 4 × 1-bit sigma-delta outputs of the CPLD, 6-bit volume PWM | 32 MHz [RTL] |
| Mixer | LM358 (U1) inverting summing amps, MCP602 buffers | - |
| Oscillators | two XO: 32 MHz (`clk32`) and `clkx` (unused by the RTL) | - |
| Power | +5 V and **+12 V required** (edge b29) [RM][SCH] |

## 2. Bus interface

### 2.1 Edge connector (J4, 2×30) [SCH]

| Signal | Pin | Use |
|---|---|---|
| A0-A15, D0-D7 | various | address / data |
| /MREQ b16, /IORQ b17, /RD b18, /WR b19, /M1 b24 | | strobes (/RD 47k pull-up) |
| /DOS a4, /IODOS b20 | | wired (10k pull-ups) but **not used** by the RTL |
| /WAIT b21 | | wired, **never driven** by the RTL |
| IORQGE a13 | | driven high when the card answers |
| /RESET a20 | | resets the CPLD and, through it, both YM2203, the SAM2695 and the GS Z80 |
| +5 V a3 / a29 / b28, +12 V b29, GND | | |
| Not used | | CLK, INT, NMI, BUSRQ, -12 / -5 V, audio pins |

### 2.2 How the card detects an I/O cycle [RTL]

```verilog
// iorq_n are useless in zxevo :(
ioreq <= zxm1_n == 1 && zxmreq_n == 1 && (zxrd_n == 0 || zxwr_n == 0);
```
An I/O cycle is "RD or WR without M1 and without MREQ". /IORQ is ignored. Consequence for the emulator: none
(our port calls are I/O cycles by definition), but it is why rev.A without the MREQ wire misbehaved (errata).

### 2.3 ROM-fetch lock instead of /DOS [RTL]

```verilog
// dos_n are useless in zxevo :(
if (zxm1_n == 0) rom_m1_access <= zxa[15:14] == 2'b00;
```
The flag is set by every M1 cycle: it is 1 while the last opcode fetch came from `#0000-#3FFF`. The **SAA port**
and the **SounDrive ports** are ignored while it is 1. This keeps TR-DOS (which runs from ROM and uses `#1F`, `#3F`,
`#5F`, `#7F`, `#FF`) from writing into the SAA and the SounDrive. Note the rule is coarser than /DOS: *any* code in
the ROM area (48K BASIC ROM too) cannot reach these ports.

## 3. Port map [RTL]

### 3.1 Decoding

| Function | Decode | Ports (examples) | IORQGE | ROM lock | DIP |
|---|---|---|---|---|---|
| YM register / control | A15-A14 = `11`, A3-A0 = `1101` | `#FFFD`, **`#DFFD`**, `#CFFD`, `#C00D`, ... | only if A13 = 1 (`#FFFD`, `#EFFD`, `#E00D`, ...) | no | `ym` |
| YM data | A15-A14 = `10`, A3-A0 = `1101` | `#BFFD`, `#8FFD`, `#800D`, ... | yes | no | `ym` |
| SAA write | A7-A0 = `#FF`; A8 = SAA A0 | `#FF` data, `#1FF` address | **no** (removed 2023-12) | yes | `saa` |
| GS data | A7-A0 = `#B3` | `#xxB3` | yes | no | `gs` |
| GS command / status | A7-A0 = `#BB` | `#xxBB` | yes | no | `gs` |
| SounDrive | A7 = 0, A5 = 0, A3-A0 = `#F`; channel = {A6, A4} | `#0F` ch0, `#1F` ch1, `#4F` ch2, `#5F` ch3 (and mirrors with A15-A8 any) | **no** (removed 2023-12) | yes | `sd` |

**There is no GS control port `#33`** on this card (the classic GS has it). Software that resets the GS through
`#33` gets no effect.

### 3.2 The `#DFFD` behavior

`#DFFD` (A13 = 0) matches the YM decode, so a write reaches the selected YM2203 (as an address, or as a control byte if
`>= #F0`), but IORQGE is not driven, so the **machine's own `#DFFD` paging register latches the same write**. The
firmware comment says this is on purpose ("required for compatibility with #dffd port"). A read of `#DFFD` also makes
the card drive the data bus with the YM's value while the machine may drive too (bus fight; the slot design's read
conflict rule applies).

### 3.3 Control byte and reset state

A write to the YM register port with the top **four** bits `1111` is a control byte:

| Bit | Effect | 0 | 1 |
|---|---|---|---|
| 0 | YM chip select | U4 (chip 1) | U10 (chip 2) |
| 1 | read mode of `IN #FFFD` | status (busy, timers) | selected register |
| 2 | FM mute (both chips, one bit) | FM on (`FM*_ENA` floated) | FM muted (`FM*_ENA` driven 0) |
| 3 | SAA clock | on | off (counters frozen) |

Differences from the classic TSFM (which uses the top **five** bits `11111` and bits 0-2):

1. `#F0-#F7` are control bytes here, register addresses on a TSFM.
2. The control byte **also reaches the selected YM2203 as an address write** (the chip select is not gated by data).
   Harmless for registers (no YM2203 register at `#F0-#FF`), but it replaces the chip's latched address: a later
   `#BFFD` write without a new address goes nowhere. On a TSFM the latch is untouched.
3. Any plain TurboSound chip switch (`#FF` / `#FE`) has bit 3 = 1 and **stops the SAA**, and has bit 2 = 1 and **mutes
   FM**. Software that wants FM and SAA writes `#F0-#F3` / `#F8-#FB` style values deliberately.
4. Ball Quest writes `#F0-#F7` as YM addresses and clicks on this card ([#11](https://github.com/UzixLS/zx-multisound/issues/11)).
   The unofficial patch from that issue (option `ctrlMask = classic`) compares five bits while the SAA DIP is off.

**Reset state** (CPLD reset branch): chip select 0 (U4), `ym_get_stat = 0` -> `IN #FFFD` returns the selected
**register** (not the status), `FM*_ENA` driven 0 -> **FM muted** [SCH: the lines are the YM3014B serial-data pins
themselves, the YM output feeds them through 1k, so a CPLD 0 wins], SAA clock **off**. A program must write a control
byte with bits 2 and 3 clear before it hears FM or SAA. The classic TSFM's emulated reset state (TSFM TDD) is
"status read, FM muted"; the difference is the read mode.

The control-byte reset of the YM read mode is the CPLD's; the YM2203 chips themselves are reset by the bus /RESET
(model the proper reset; issue #9 describes a too-short reset on one machine leaving the prescaler wrong, which is
not modeled).

### 3.4 Reads

| Port | Returns |
|---|---|
| `#FFFD` (and mirrors) | the selected YM2203's status or register (per bit 1) |
| `#xxB3` | GS output register (`gs_reg_out`) |
| `#xxBB` | GS status: bit 7 data flag, bit 0 command flag, bits 1-6 = 1 |
| others | the card does not drive |

## 4. Sound blocks

### 4.1 YM2203 pair

- Clock 3.5 MHz average from the DDS: the period alternates between 9 and 10 cycles of 32 MHz (7/64). The jitter is
  not audible and is **not modeled**; the emulator uses an exact 3.5 MHz on the card's own time axis, independent of
  the host CPU clock and turbo.
- FM output: YM2203 OP-O -> 1k -> YM3014B SD pin; the CPLD's `FM*_ENA` sits on that pin (open-drain style). Hold cap
  2.2 nF per DAC (corner unknown: YM3014B output impedance not documented).
- **I/O ports:** only **U4 IOA2** (YM chip 1, register 14 bit 2, port A) is connected - to the SAM2695 MIDI input
  (§4.4). Every other IOA / IOB pin and both IRQ pins are unconnected.
- Busy behavior: issue [#6](https://github.com/UzixLS/zx-multisound/issues/6) measured no busy after an address write
  on this card and on ZXM-SoundCard Extreme; busy after data writes only. The owner of the card considers it a
  YM2203 property; the shared YM2203 engine's busy model should be checked against it (TSFM TDD busy section).

### 4.2 General Sound

| Item | MultiSound | Classic GS |
|---|---|---|
| CPU clock | 16 MHz | 12 MHz |
| INT | 12 MHz / 320 = 37.5 kHz (counter on the 12 MHz DDS clock) | 37.5 kHz |
| RAM | 1 MB (2 MB firmware option) | 128-512 KB |
| ROM | GS 1.05b ([psbhlw/gs-firmware](https://github.com/psbhlw/gs-firmware)) | 1.04 / 1.05a |
| Host ports | `#B3`, `#BB` | `#B3`, `#BB`, `#33` |
| Page register | port 0, bits 0-6 (`gs_page`); ROM at page 0 above `#8000` | same scheme |
| DAC volumes | ports 6-9, 6 bits | same |
| DAC samples | memory reads at `#6000-#7FFF`, channel = A9-A8 | same |

Status flags [RTL]: data flag set by a host `#B3` write and by a GS port-3 write, cleared by a host `#B3` read and by a
GS port-2 read, a GS access to port `#0A` sets it to the inverse of page bit 0; command flag set by a host `#BB` write, cleared by GS port 5, port
`#0B` sets it from volume-3 bit 5 (the classic GS's documented quirks).

### 4.3 SounDrive and the shared DACs

Four DAC channels, each written by either the GS (memory read at `#6000-#7FFF`) or the SounDrive port. A SounDrive write
sets the channel's volume to 63 (maximum) and its sample; a GS volume write sets the 6-bit volume. Last writer wins.
The sample is converted from offset binary: values `< #80` have bits 0-6 inverted (sign-magnitude conversion in the
RTL). Each channel is a 1-bit first-order sigma-delta at 32 MHz with a PWM volume gate.

### 4.4 MIDI

- SAM2695 pin 16 (MIDI IN) is wired **directly to U4 pin 14 = IOA2** [SCH], no level shifter / inverter. This is the
  128K convention (AY register 14 bit 2 is MIDI out). The YM is 5 V, the SAM2695 3.3 V (works in practice).
- Software bit-bangs the serial line at 31 250 baud by writing register 14 of YM chip 1 (and must set register 7 bit 6
  to make port A an output).
- SAM2695 straps: XDIV tied high (= 12 MHz clock mode, datasheet; matches the 12 MHz DDS clock), MICIN grounded, parallel bus unused (/CS, /RD, A0 grounded, /WR high), reset shared
  with the YM2203 reset. No mode pins.

### 4.5 Mixer [SCH]

Two inverting summing amplifiers (LM358, single +5 V supply, bias 1.82 V), feedback 10k each side, every source
AC-coupled (10 µF) and therefore inverted, except the external input (DC-coupled).

| Source | L gain | R gain | Filter |
|---|---|---|---|
| FM chip 1, FM chip 2 | 1.0 (0 dB) | 1.0 (0 dB) | 2.2 nF hold cap (corner unknown) |
| SSG channel A (both chips) | 0.417 (-7.6 dB) | 0 | none |
| SSG channel B (both chips) | 0.213 (-13.4 dB) | 0.213 | none |
| SSG channel C (both chips) | 0 | 0.417 (-7.6 dB) | none |
| SAA L / R | 0.825 × (I × 1k) | same on R | 2-pole RC, about 7.2 kHz |
| MIDI L / R | 1.0 | 1.0 | none |
| DAC 0, 1 (GS ch 1-2, SounDrive `#0F`, `#1F`) | 0.208 (-13.6 dB) | 0 | 1-pole, about 16.3 kHz |
| DAC 2, 3 (GS ch 3-4, SounDrive `#4F`, `#5F`) | 0 | 0.208 | 1-pole, about 16.3 kHz |
| External input J3 (on-board header, not the edge) | 0.417 | 0.417 | none |

- SSG stereo is **ACB with B in the center**, from 3.3k loads per channel (the ratios hold whatever the chip's output
  impedance).
- GS stereo is **hard left / hard right** (channels 1-2 left, 3-4 right), no cross-feed - unlike our generic GS mix
  (50 % cross-feed).
- Absolute levels of the SSG, SAA and SAM2695 outputs are not in the schematic; only the mixer weights are exact. The
  emulator calibrates absolute levels per chip module and applies these weights.

## 5. DIP switch [SCH][RTL]

| Switch | Silkscreen | RTL | Option name |
|---|---|---|---|
| SW1.1 | Enable TSFM + MIDI | `cfg[0]` YM ports | `ym` |
| SW1.2 | Enable SAA1099 | `cfg[1]` | `saa` |
| SW1.3 | Enable GS | `cfg[2]` | `gs` |
| SW1.4 | Enable SounDrive | `cfg[3]` | `sd` |
| SW1.5 | Reserved | `cfg[4]` unused | - |

Each line has a 10k pull-up and the switch closes it to ground, while the RTL enables are active high, so "switch ON"
(closed) **disables** the function - the opposite of the silkscreen wording. The emulator names options by function
(`dip = ym,saa,gs,sd` = the functions enabled) and does not model switch polarity.

A disabled function also stops claiming its ports (the decode terms include the `*_ena` bits), so with `gs` off a
separate GS card can sit on the bus (slots R-COMP-2).

## 6. Revisions and firmware (not modeled; Q5)

| Item | rev.A | rev.A1 | rev.A2 |
|---|---|---|---|
| MREQ to the CPLD | missing (wire to TP1) | fixed | fixed |
| MIDI mix resistors | 18k (-5.1 dB) | 10k (0 dB) | 10k |
| DAC mix resistors | 33k (-10.4 dB) | 47k (-13.4 dB) | 47k |
| Output bleeders | none | added | same |
| 3.5 mm jack | L / R swapped (footprint) | swapped | fixed |
| GS Z80 clock | 3.3 V from the CPLD (unstable GS) | same | buffered (U23) |
| U21 (IORQGE buffer) | 74LVC1G125 | same | 74AHCT1G125 |

Firmware history (`top.v`): GS 16 MHz (2022-11), 2 MB build option (2023-02), NemoIDE compatibility (2023-03), SounDrive
ports and `#FF` removed from IORQGE (2023-12), YM chips swapped (2024-01, "fixes stellar.scl").

## 7. Facts still open

| Item | Where to find it |
|---|---|
| YM3014B output impedance (FM low-pass corner) | YM3014B datasheet / measurement |
| Absolute output levels of SSG, SAA1099, SAM2695 relative to each other | measurement on a real card (owner) or chip datasheets |
| YM2203 IOA output type (push-pull vs pull-up) | YM2203 datasheet (affects nothing logical) |
| Bus fight result on a `#DFFD` read | machine schematics (slots design SL-0) |
