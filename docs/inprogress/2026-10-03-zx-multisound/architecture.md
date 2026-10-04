# ZX-MultiSound: architecture

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
| **Requirements** | [requirements.md](requirements.md) |
| **Hardware** | [hardware-reference.md](hardware-reference.md) |
| **Slots** | [ZX-bus slots architecture](../2026-10-03-zx-bus-slots/architecture.md) (`ICard`, `CardType`, `PortClaim`, `SlotManager`) |
| **TDDs** | new modules: [SAA1099](tdd-saa1099.md), [libsam2695](tdd-libsam2695.md), [MIDI line](tdd-midi-line.md), [card logic](tdd-card-logic.md); [integration](tdd-integration.md) |

## 1. Composition

The card is glue logic around shared modules. It forks none of them (owner rule); where a module needs a
card-specific behavior, it gets a parameter in its configuration.

```mermaid
flowchart LR
    BUS["PortClaimTable<br/>(slots)"] --> CARD["MultiSoundCard : ICard"]
    CARD --> DEC["MultiSoundLogic<br/>decode, control byte,<br/>ROM lock, flags"]
    DEC --> YM["Ym2203Pair<br/>(shared YM2203 engine,<br/>own 3.5 MHz clock)"]
    DEC --> SAA["Saa1099<br/>(new shared module)"]
    DEC --> GS["SoundChip_GeneralSound<br/>(MultiSound profile:<br/>16 MHz, 1-2 MB, no #33)"]
    DEC --> DAC["MultiSoundDacs<br/>4 ch, GS + SounDrive,<br/>last writer wins"]
    GS -- "DAC sink" --> DAC
    YM -- "chip 1 IOA2" --> LINE["MidiLine<br/>(new)"]
    LINE --> SAM["sam2695::Synth<br/>(new vendored library)"]
    YM --> MIX["MultiSoundMixer<br/>board weights"]
    SAA --> MIX
    DAC --> MIX
    SAM --> MIX
    MIX --> SM["SoundManager rows:<br/>MS FM, MS SSG, MS SAA,<br/>MS DAC, MS MIDI"]
```

| Piece | Kind | Location |
|---|---|---|
| `MultiSoundCard` | new card (the `ICard` + `CardType` entry) | `core/src/emulator/slots/cards/multisound/multisoundcard.{h,cpp}` |
| `MultiSoundLogic` | new, pure logic, no audio: the CPLD's decode and latches; testable against the Verilog | `.../multisound/multisoundlogic.{h,cpp}` |
| `MultiSoundDacs` | new, the four shared DAC channels | `.../multisound/multisounddacs.{h,cpp}` |
| `MultiSoundMixer` | new, applies the board's weights (hardware reference §4.5) | `.../multisound/multisoundmixer.{h,cpp}` |
| `Ym2203Pair` | **extracted** from `SoundChip_TurboSoundFM`: two YM2203 with timers, busy, FM and SSG rendering, parameterized by master clock; TSFM keeps its board logic on top | `core/src/emulator/sound/chips/tsfm/ym2203pair.{h,cpp}` |
| `Saa1099` | new shared module | `core/src/emulator/sound/chips/saa1099/` |
| `SoundChip_GeneralSound` | existing, gains a **profile** (clock, INT divider clock, RAM size up to 2 MB, host port set, DAC sink) | `core/src/emulator/sound/chips/gs/` |
| AY / YM2203 I/O port output | existing SSG, gains an **I/O port output callback** (register 14 / 15 writes and register 7 direction) | `soundchip_ay8910.*`, the SSG part of `Ym2203Pair` |
| `MidiLine` | new: line level timeline from IOA2 to the synthesizer | `core/src/emulator/sound/midi/midiline.{h,cpp}` |
| `sam2695::Synth` | new vendored library | `core/src/3rdparty/sam2695/` |

## 2. Time

- The card's time axis is the emulator's audio time `AudioTstate(z80->t)` (turbo removed), the same axis as GS, Covox
  and MoonSound use. Each module converts it to its own clock with an integer ratio accumulator whose phase is in
  its state:

| Module | Clock | Ratio from 3.5 MHz T-states |
|---|---|---|
| YM2203 pair | 3.5 MHz exact (DDS average) | 1 : 1 on a 3.5 MHz host, a true ratio on 3.5469 MHz hosts (128K) |
| SAA1099 | 8 MHz | 16 : 7 |
| GS Z80 | 16 MHz, INT from 12 MHz / 321 (RTL) | card units as today (`GSCardRunner`) |
| SAM2695 | its internal rate | library `hostTickRate` |

- **Why the YM pair needs a ratio:** today TSFM uses "one YM master clock = one CPU T-state" because the TSFM's clock
  *is* the host's AY clock × 2. The MultiSound has its own oscillator, so on a 128K-family host (3.5469 MHz) its FM
  pitch is 1.3 % lower than a TSFM's. `Ym2203Pair` takes `masterClockHz` and the host rate; with equal rates the ratio
  is 1 : 1 and the TSFM path stays bit-identical (golden tests prove it).

## 3. Port path

`MultiSoundCard::Ports(options)` returns the claims of hardware reference §3.1 (with the DIP functions applied):

| Claim | mask / match | dir | IORQGE | ROM lock |
|---|---|---|---|---|
| YM register, A13 = 1 | `#E00F` / `#E00D` | InOut | yes | no |
| YM register, A13 = 0 (`#DFFD` family) | `#E00F` / `#C00D` | InOut | **no** | no |
| YM data | `#C00F` / `#800D` | Out (reads float on the card) | yes | no |
| SAA | `#00FF` / `#00FF` | Out | no | yes |
| GS data, command | `#00FF` / `#00B3`, `#00FF` / `#00BB` | InOut | yes | no |
| SounDrive | `#00AF` / `#000F` | Out | no | yes |

`MultiSoundCard::Out` hands the write to `MultiSoundLogic`, which returns the actions (YM address / data to chip N,
control byte latched, SAA address / data, GS mailbox write, SounDrive channel write); the card executes them on the
modules at the access time. The split keeps the logic free of audio so it is tested cycle for cycle against `top.v`
in Verilator ([tdd-card-logic.md](tdd-card-logic.md)).

## 4. Module changes needed

### 4.1 `Ym2203Pair` extraction (from `SoundChip_TurboSoundFM`)

- Moves: the two chips (ymfm YM2203 via `ym2203_engine.h`), timers, busy, FM word queues, SSG render, `syncTo` /
  `advanceChip`, the decimator setup.
- Stays in `SoundChip_TurboSoundFM`: `TsfmBoard` latches and the `#FFFD` parse (five-bit mask), the TSFM's mixer rows.
- New parameters: `masterClockHz` and `hostTickRate` (ratio accumulator), `addressWriteOnControlByte` (the MultiSound
  passes the control byte through as an address; the TSFM does not - kept in the board logic, not the pair), an
  I/O port output callback per chip.
- Proof of no regression: the existing TSFM tests (`core/tests/emulator/sound/tsfm/*`, `ttdtsfm_test.cpp`) unchanged
  and green; golden digests of the TSFM output identical before and after; A/B benchmark on the TSFM render path.
- The `ITurboSoundDevice::SetPsgClock` contract ("TSFM stays at `PSG_CLOCK_RATE`") becomes implementable as a side
  effect; not used here.

### 4.2 GS profile

| Parameter | Classic (default) | MultiSound |
|---|---|---|
| CPU clock | 12 MHz | 16 MHz |
| INT period | 320 clocks of 12 MHz | **321** clocks of 12 MHz (37.383 kHz from the 12 MHz DDS, not the CPU clock; low 33 clocks), per the RTL ([tdd-card-logic.md](tdd-card-logic.md) §7 F8) |
| RAM | 128-512 KB (`[SOUND] GSRamSize`) | 1 MB or 2 MB (`gsRam`); `_ramPairMask` widened, banking unchanged |
| Host ports | `#B3`, `#BB`, `#33` | `#B3`, `#BB` |
| ROM | `[ROM] GS` | GS 1.05b (`data/rom/gs105b.rom`, shipped with the card profile) |
| DAC output | the card's own stereo mix (50 % cross-feed) | a **DAC sink** interface: the GS reports (channel, sample) and (channel, volume) events with their time; the MultiSound's `MultiSoundDacs` consumes them |

`GSClassicTiming` stays the classic default; the profile carries the clocks per instance (`GSHostClock` already takes
`unitsPerSecond`). The lightweight player personality (LW) is not offered on the MultiSound (the card has a real Z80;
LW stays a GS-card option).

### 4.3 AY / SSG I/O port output

Today register 14 / 15 writes are stored and nothing else happens (`soundchip_ay8910.cpp` `applyRegister` default).
New: an optional `IoPortListener` on the AY and on the YM2203 SSG part, called with `(time, port, value, isOutput)`
when register 14 / 15 changes or register 7 bits 6 / 7 change the direction. Zero cost without a listener (one null
check on register 7 / 14 / 15 writes only). The same hook later serves the 128K's own MIDI / RS-232 out on the AY
(PLAN follow-up, not in this work).

### 4.4 `MultiSoundDacs`

Four channels `{sample, volume}`; inputs: GS sample event (memory read at `#6000-#7FFF`), GS volume (ports 6-9),
SounDrive port write (sample + volume 63). Output: ideal DAC value `level × gain / (128 × 64)` per channel (`MultiSoundLogic::SampleLevel` / `VolumeGain64`: level -127..+127, gain = volume, 63 counts as 64; measured exact against the RTL, tdd-card-logic.md §7 F9) (sigma-delta
noise not modeled; optional `Authentic` mode adds the 1-pole 16.3 kHz board filter), stereo by the board weights
(channels 0-1 left, 2-3 right), deltas into a blip pair on the card axis.

## 5. Mixer rows

Each source is its own `SoundManager` row so the user can mute / solo / record it (and the HUD shows activity): `MS
FM`, `MS SSG`, `MS SAA`, `MS DAC` (GS + SounDrive), `MS MIDI`. The board weights (hardware reference §4.5) are applied
inside the card before the rows, so the rows' unity volume equals the real board. New `AudioSourceType` values go
before `Custom`; `AudioActivityIndicators::HUD_SOURCES` grows accordingly.

## 6. Shadowing and compatibility

The card's IORQGE claims on `#FFFD` / `#BFFD` shadow the machine's AY socket content (slots Q2). Its functions (DIP
dependent): `ym` -> `ay-socket` role (shadowing) and `midi`; `saa`; `gs`; `soundrive`. See the slots
[compatibility matrix](../2026-10-03-zx-bus-slots/compatibility-matrix.md).

## 7. TTD

| Blob | Id | Content |
|---|---|---|
| `MultiSoundCard` | new id (next free when it lands; 48-50 free on master today, shared with SAA1099 / SAM2695 below) | logic latches (chip select, read mode, FM mute, SAA clock, ROM lock flag), DAC channels, the YM pair (both chips, ratio phase), the MIDI line state |
| `Saa1099` | new id | chip state (tdd-saa1099 §5), saved through the card |
| `Sam2695` | new id | synthesizer state incl. bank SHA-256 (tdd-libsam2695 §4) |
| General Sound | 5 (existing) | unchanged layout; RAM 1-2 MB (the GS RAM as a TTD v2 memory region is PLAN #45; until then the blob is large, which the TTD v2 engine's per-device dedup handles) |

The MIDI line itself is driven by YM register 14 writes, which the port journal records; replay re-executes them.

## 8. Configuration

```ini
[SLOTS]
zxbus.1 = multisound
zxbus.1.dip = ym,saa,gs,sd      ; functions enabled (default: all)
zxbus.1.gsRam = 1M              ; 1M | 2M
zxbus.1.ctrlMask = pro          ; pro | classic (unofficial issue #11 patch)

[MIDI]
Bank = data/midi/generaluser-gs.sf2   ; default; any SF2
```
