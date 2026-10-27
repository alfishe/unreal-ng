# ZX-MultiSound: requirements

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
| **Prerequisite** | [ZX-bus slots](../2026-10-03-zx-bus-slots/requirements.md) (PLAN #82): the card is the first one written for slots |
| **Decisions** | [open-questions.md](open-questions.md) Q1-Q5 (decided 2026-10-03) |
| **Hardware** | [hardware-reference.md](hardware-reference.md) |
| **Design** | [architecture.md](architecture.md); TDDs: [SAA1099](tdd-saa1099.md), [libsam2695](tdd-libsam2695.md), [MIDI line](tdd-midi-line.md), [card logic](tdd-card-logic.md), [integration](tdd-integration.md) |

## 1. Goal

A user plugs a ZX-MultiSound into a ZX-bus slot of a Pentagon or a ZX-Evo and hears, at the same time and through one
card: TurboSound FM music, a General Sound MOD, SAA1099 music, SounDrive samples and MIDI music played on the card's
General MIDI synthesizer. Every part behaves like the real card's current firmware on rev.A2, TTD records and
replays it bit-exactly, and every automation surface can configure and inspect it.

**Worked example.** A program plays a tune that uses the TSFM and the SAA at once:
1. `OUT (#FFFD),#F7`: on this card the byte `1111 0111` is a control byte (the mask is four bits): chip select 1
   (U10, the second YM2203; bit 0 = 1), register read mode (bit 1 = 1), FM muted (bit 2 = 1), **SAA clock on**
   (bit 3 = 0). The byte also reaches the selected YM2203 as an address write. (A tune that wants FM too writes `#F3`.)
2. `OUT (#FFFD),#07` / `OUT (#BFFD),#38`: AY register 7 of that YM2203 (mixer).
3. `OUT (#1FF),#1C` / `OUT (#FF),#01`: SAA register `#1C` (sound enable).
4. The card's mixer sums YM FM, the YM SSG channels, the SAA, the GS / SounDrive DACs and the MIDI synthesizer into
   the stereo output, with the levels of the real board's analog mixer.

## 2. Functional requirements

### 2.1 The card on the bus

- **R-MS-1.** Card id `multisound` on the ZX-bus (NemoBus). Required signals: IORQGE, +12 V; /IODOS and /DOS are wired
  on the board but unused by the firmware (it detects ROM execution by M1 address). On other buses: through an
  adapter or the override (slots Q5).
- **R-MS-2.** Functions occupied, each gated by its DIP switch: `ay-socket` shadowing role (`ym`), `saa`, `gs`,
  `soundrive`, and `midi` (with `ym`: the MIDI line is driven from a YM2203 I/O port).
- **R-MS-3.** Ports and decoding exactly as the current CPLD firmware (hardware reference §3):
  `#FFFD` / `#BFFD` with the partial decode (A15-A14, A3-A0), `#DFFD` reaching both the YM2203 and the host's memory
  port (no IORQGE on A13 = 0), SAA `#FF` / `#1FF`, GS `#B3` / `#BB`, SounDrive `#0F` / `#1F` / `#4F` / `#5F`, the
  ROM-fetch lock of the SAA and SounDrive ports, IORQGE exactly on `#FFFD` (A13 = 1), `#BFFD`, `#B3`, `#BB`.
- **R-MS-4.** Control byte (`#FFFD`, top four bits `1111`): bit 0 chip select, bit 1 status / register read, bit 2 FM
  mute, bit 3 SAA clock enable (0 = on). The byte also reaches the selected YM2203 as an address write. Option
  `ctrlMask = classic` (unofficial, issue #11 patch) applies the five-bit TSFM mask when the SAA is switched off.
- **R-MS-5.** Power-on and reset state as the firmware's reset branch (hardware reference §3.3): YM chip 1 selected,
  `IN #FFFD` reads the selected register (not the status), SAA clock off, both `FM*_ENA` lines driven low, GS
  mailbox / page / DAC / volume registers zero.

### 2.2 Sound sources

- **R-MS-10. TurboSound FM:** two YM2203 on the shared YM2203 engine, clocked at a fixed 3.5 MHz average, independent of
  the host clock and turbo (the card's own 32 MHz oscillator through a DDS). The DDS jitter is documented, not modeled.
- **R-MS-11. General Sound:** the shared GS card with the MultiSound profile: Z80 at 16 MHz, INT 37.5 kHz (12 MHz /
  320), 1 MB RAM (`gsRam = 2M` for the 2 MB firmware), ROM GS 1.05b, ports `#B3` / `#BB` only.
- **R-MS-12. SounDrive:** four 8-bit channels on ports `#0F`, `#1F`, `#4F`, `#5F`, **sharing the card's four DACs with
  the GS** (a SounDrive write sets that channel's volume to maximum and overwrites the GS sample; the last writer
  wins, per CPLD clock).
- **R-MS-13. SAA1099:** our own SAA1099 module ([tdd-saa1099.md](tdd-saa1099.md)), clock 8 MHz, gated by the control
  byte's bit 3.
- **R-MS-14. MIDI synthesizer:** our own vendored library libsam2695 ([tdd-libsam2695.md](tdd-libsam2695.md)), a General
  MIDI engine with the SAM2695 MIDI implementation, playing the bank from `[MIDI] Bank=` (default GeneralUser GS).
  The MIDI line from the YM2203 I/O port to the synthesizer ([tdd-midi-line.md](tdd-midi-line.md)) is exact.
- **R-MS-15. Mixer:** the real board's analog mixer levels and panning per source (hardware reference §5), each
  source also a separate `SoundManager` row (volume, mute, recording, HUD).

### 2.3 Variants (open-questions Q5)

- **R-MS-20.** Options: DIP switches `ym`, `saa`, `gs`, `sd`; `gsRam = 1M | 2M`; `ctrlMask = pro | classic`.
- **R-MS-21.** The modeled board is rev.A2 with the current firmware, the least buggy variant: no rev.A / A1 errata,
  proper YM2203 reset, correct stereo, stable GS.

### 2.4 Compatibility (slots Q1, Q2)

- **R-MS-30.** Incompatible with any card that occupies one of its active functions: TurboSound / TSFM / TSFM Pro in the
  AY socket (shadowed, pointless), GS, NeoGS, standalone SAA cards, SounDrive / Covox on the same ports, other MIDI
  cards on the AY port. Plugging it in can remove several cards at once; the reply lists them all.
- **R-MS-31.** Shadows the machine's built-in AY / TurboSound on `#FFFD` / `#BFFD` (ZX-Evo FPGA TurboSound, the 128K
  AY, ...). The report lists them as `shadowed by zxbus.N`.

## 3. Non-functional requirements

- **R-MS-NF-1. Accuracy by co-simulation.** SAA1099 against the reference consensus (SAASound, MAME, the RTL models);
  the card's decode against the CPLD Verilog itself (`top.v` run in Verilator or Icarus as the oracle for the port /
  control-byte / DAC logic).
- **R-MS-NF-2. TTD.** The card's whole state (decoder latches, both YM2203, GS, SAA, DACs, MIDI line, synthesizer) is
  one device blob set; port writes are replayed, never the audio.
- **R-MS-NF-3. Zero cost** for machines without the card (slots R-NF-1).
- **R-MS-NF-4. Automation parity** on all five surfaces + OpenAPI + Qt, with recipes `.recipe/sound/multisound.md`.
- **R-MS-NF-5. Real software.** Wild Commander's MIDI player (via `ay-rs232/midi`), VGMPLAY.WMF (SAA, AY), TSFM
  players (TFM Music Maker files), a GS MOD player, a SounDrive player and Ball Quest (with `ctrlMask` both ways) run
  as a user would run them, with TTD recording on.

## 4. Out of scope

- Board revisions A / A1 and older firmware builds (described in the hardware reference only).
- A bit-exact SAM2695 (its mask ROM and microcode are not available); host MIDI output (CoreMIDI / WinMM / ALSA) is a
  later phase of libsam2695's integration.
