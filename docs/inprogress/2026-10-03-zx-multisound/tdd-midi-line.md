# MIDI line (AY / YM2203 I/O port to the synthesizer): technical design

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | ML-0..ML-2 built 2026-10-04 (branch `multisound`, not committed); §5 records what was built and where it deviates. Card wiring (the second half of ML-2) waits for the MultiSound integration |
| **Hardware** | [hardware-reference.md](hardware-reference.md) §4.4: SAM2695 MIDI IN wired directly to YM chip 1 (U4) IOA2 |
| **Consumer** | `sam2695::Synth::WriteLine` ([tdd-libsam2695.md](tdd-libsam2695.md) §3.1) |
| **Effort scale** | S < 1 week, M 1-2 weeks, L 2-4 weeks |

## 1. Goal

Software that bit-bangs MIDI through the sound chip's I/O port (the 128K convention: register 14, bit 2) drives the
MultiSound's synthesizer exactly as on the real card: every level change of the pin reaches the synthesizer's UART at
the T-state of the `OUT (#BFFD)` that caused it, so bytes are assembled (or rejected) exactly as the real chip would
assemble them from the same timing.

**Worked example.** WC's MIDI player sends `#90` on YM chip 1:
1. Once: `R7 |= #40` (port A output). Idle line = R14 bit 2 = 1.
2. Start bit: `R14 = %xxxxx0xx` at T0. Then 8 data bits LSB first, each `R14` write ~112 T-states apart (32 µs at
   3.5 MHz), then the stop bit (1).
3. `IoPortListener` on chip 1 sees each register 14 write; `MidiLine` turns bit 2 changes into `(time, level)` events.
4. `Synth::WriteLine(t, level)` receives them on the card axis; its UART samples mid-bit and emits `#90` at the stop
   bit's middle.

If the program's loop timing is off (wrong CPU speed, contended memory), the real chip gets framing errors and so does
ours - bit-exact cause and effect.

## 2. Pieces

### 2.0 Hardware facts (ML-0)

The I/O port block is the same on the three chips: R7 bit 6 makes port A (R14) an output, bit 7 port B (R15);
`0` = input. What the board sees on the pins:

| Fact | AY-3-8910 (GI) | YM2149 (Yamaha) | YM2203 (Yamaha) |
|---|---|---|---|
| Direction | R7 B6 / B7, "Input Enable" active low (p. 5-21) | R7 B7 / B6, "Input is selected when 0 is written" (p. 6) | register 07 "IN/OUT IOB IOA" (p. 7) |
| Input mode | "Each pin is provided with an on-chip pull-up resistor, so that when in the input mode, all pins will read normally high" (p. 5-19) | pull-up 60-600 kOhm on IOA / IOB in input mode (p. 9) | "Each terminal incorporates pull up resistance" (p. 3), 60-600 kOhm (p. 8) |
| Output high | VOH >= 2.4 V at IOH = 100 uA (p. 5-23) | VOH >= 2.5 V at 100 uA (p. 9) | VOH1 >= 2.4 V at 0.4 mA, VOH2 >= 3.3 V at 40 uA, every output except IRQ (p. 8) |
| Output low | VOL <= 0.5 V at 1.6 mA | VOL <= 0.4 V at 1.6 mA | VOL <= 0.4 V at 2 mA |
| Reset | all registers 0 (p. 5-19) | all registers 0 (p. 3) | /IC: all registers 0 (p. 3) |

Conclusions for the model:

- **An input port presents `#FF`** on every chip. After reset both ports are inputs, so a MIDI line wired to IOA2 idles
  high from power-on until software drives it.
- **An output pin follows its latch bit** and is actively driven both ways: the specified VOH currents (100 uA, and
  0.4 mA on the YM2203) are more than a 60 kOhm pull-up can source at 2.4 V (about 43 uA), so the high level is not
  the pull-up alone. This closes the [hardware-reference.md](hardware-reference.md) §7 item "YM2203 IOA output type":
  push-pull for the logic level, with the pull-up active in input mode. MAME's `ay8910.cpp` comment calls the ports
  open collector with switched pull-ups; the datasheets' VOH figures do not support that, and for a logic-level
  consumer (the SAM2695 MIDI IN, a CMOS input) the result is the same: the level follows the latch bit at once.
- **Switching the direction changes the pins**: output -> input shows `#FF`, input -> output shows the latch (the
  latch keeps what was written while the port was an input). MAME's `ay8910_write_reg` does the same (it calls the
  port write callback with `#FF` when R7 makes the port an input and with the latch when it makes it an output, and
  ignores R14 / R15 writes for the pins while the port is an input).
- Reads of R14 / R15 are not part of the MIDI line (MultiSound reads nothing back from IOA); unchanged.

Sources: [GI AY-3-8910 / 8912 / 8913 datasheet](http://map.grauw.nl/resources/sound/generalinstrument_ay-3-8910.pdf)
(pages 5-19, 5-21, 5-23), [Yamaha YM2149 catalog LSI-2121492](http://map.grauw.nl/resources/sound/yamaha_ym2149.pdf)
(pages 3, 6, 9), [Yamaha YM2203 catalog LSI-2122032](http://bitsavers.informatik.uni-stuttgart.de/components/yamaha/YM2203_198911.pdf)
(pages 3, 7, 8), [MAME `ay8910.cpp`](https://github.com/mamedev/mame/blob/ce4fc166e50628c765e051ab0f356a3862f57ed3/src/devices/sound/ay8910.cpp)
(`ay8910_write_reg`, cases `AY_ENABLE`, `AY_PORTA`, `AY_PORTB`; the read-path FIXME).

### 2.1 I/O port listener on the AY and the YM2203 SSG

```cpp
// core/src/emulator/sound/chips/ayioport.h (sketch)
class IAyIoPortListener
{
public:
    virtual ~IAyIoPortListener() = default;
    // port: 0 = A (R14), 1 = B (R15). value = the pin levels as seen outside the chip
    virtual void OnIoPortPins(uint64_t t, int port, uint8_t pins) = 0;
};
```

- **Pin levels**, not register values: a port set as input (R7 bit 6 / 7 = 0) presents its pins as pulled high
  (`#FF`) - the documented behavior of all three chips (§2.0). A change of R7's direction bits re-evaluates the pins.
- Called only when the pin byte changes. Zero cost without a listener (one null check on writes to R7 / R14 / R15).
- Same hook on `SoundChip_AY8910` (for the 128K's own MIDI / RS-232 later) and on the SSG of `Ym2203Pair`.
- **Reads** of R14 are unchanged (no input path is added here).

### 2.2 `MidiLine`

```cpp
// core/src/emulator/sound/midi/midiline.h (sketch)
class MidiLine : public IAyIoPortListener
{
public:
    MidiLine(int port, int bit);                              // MultiSound: port A, bit 2
    void Connect(sam2695::Synth* synth, CardClock* clock);    // card axis conversion
    void OnIoPortPins(uint64_t t, int port, uint8_t pins) override;
    void Describe(MidiLineReport& out) const;                 // level, edges, last change time
    // TTD: level + last change time (the UART state lives in the synth's blob)
};
```

- Forwards only changes of the selected bit as `WriteLine(t, level)`.
- Power-on level: high (pin pulled up while the port is still an input).

### 2.3 Chip select subtlety

The MIDI pin belongs to **YM chip 1 (U4)** regardless of which chip the control byte currently selects: register
writes go to the selected chip only, so a program must select chip 1 before bit-banging. The listener is attached to
chip 1's SSG only. Chip 2's IOA is unconnected and needs no listener.

## 3. Tests (core-tests, under 50 ms)

| Test | Checks |
|---|---|
| `AyIoPort_Test.PinsFollowDirection` | R14 value appears on the pins only with R7 bit 6 = 1; input direction reads as `#FF` pins |
| `AyIoPort_Test.NoListenerNoCost` | no listener: R14 writes behave exactly as before (register readback unchanged) |
| `AyIoPort_Test.OnlyChangesNotify` | repeated identical writes produce no events |
| `MidiLine_Test.BitTimelineToSynth` | a scripted `OUT` sequence at 112-T-state spacing produces `#90 #3C #64` in the synth's parser |
| `MidiLine_Test.FramingErrorFromBadTiming` | the same sequence at half speed (224 T per bit) produces framing errors, counted, and no Note On (as built: §5) |
| `MidiLine_Test.DoubleSpeedGivesWrongBytes` | at 2× speed (56 T per bit) the stop samples land on high bits: other bytes than sent, no Note On on channel 1 (as built: §5) |
| `MidiLine_Test.Chip2DoesNotDrive` | bit-banging chip 2's R14 produces nothing (moved to the MultiSound integration: it tests the card's chip select) |
| `MidiLine_Test.TtdRoundTrip` | save mid-byte, restore, the byte completes identically |

A program-level test runs a Z80 snippet (the 128K ROM-style MIDI send routine) under `RunNFrames` against the
MultiSound and checks the synthesizer's parsed messages, with TTD recording on.

## 4. Phases

| Phase | Content | Size | Status |
|---|---|---|---|
| ML-0 | YM2203 / AY datasheet check of the I/O port output stage and the input-mode pin level | S | done 2026-10-04 (§2.0) |
| ML-1 | `IAyIoPortListener` on the AY and the YM2203 SSG, tests §3 first block | S | AY done 2026-10-04; YM2203 SSG with MS-1 (§5) |
| ML-2 | `MidiLine` + wiring into the MultiSound card, tests §3 second block | S | `MidiLine` done 2026-10-04; wiring, `Chip2DoesNotDrive` and the program-level test with the card (§5) |

## 5. As built (2026-10-04)

**Files.** `core/src/emulator/sound/chips/ayioport.h` (`IAyIoPortListener`, `AyIoPort` pin model: direction bit,
`Pins(r7, port, latch)`, `AffectsPins(reg)`), the listener in `soundchip_ay8910.{h,cpp}`, the time argument in
`soundchip_turbosound.cpp`, `core/src/emulator/sound/midi/midiline.{h,cpp}`. Tests:
`core/tests/emulator/sound/chips/ayioport_test.cpp` (`AyIoPort_Test`: `PinsFollowDirection`, `NoListenerNoCost`,
`OnlyChangesNotify`) and `core/tests/emulator/sound/midi/midiline_test.cpp` (`MidiLine_Test`: `BitTimelineToSynth`,
`FramingErrorFromBadTiming`, `DoubleSpeedGivesWrongBytes`, `TtdRoundTrip`), each a few ms. `core/tests/CMakeLists.txt`
links `sam2695` (core-tests compiles core sources itself and needs the library's include path, as with `opl4`).

**AY listener.**

- `SoundChip_AY8910::setIoPortListener(listener)` (null detaches), `ioPortPins(port)` (computed from the register
  file). The pins change at the **latch** (`latchRegister`), not at the generator-side apply: the board sees them at
  the `OUT`. `latchRegister` / `writeRegister` take the write's time `t` (default 0 for callers without a time, e.g.
  the snapshot loaders); `SoundChip_TurboSound` passes the T-state of the `OUT` on its audio axis (`_lastSeenT`).
- Only a write to R7 / R14 / R15 is evaluated, only with a listener, and the listener hears a port only when its pin
  byte changes. The pin cache is not chip state: it is refreshed (silently) when a listener attaches, on `reset()` and
  after `TTDLoadState`, so the AY TTD blob is unchanged (73 bytes).
- `reset()` is silent: the owner resets its listeners on its own time axis (`MidiLine::Reset(t)` takes the line high,
  as the pull-ups do).

**Zero cost without a listener.** The render path (`updateState`, `applyRegister`, the mixer) is untouched. The only
addition on the write path is `if (_ioPortListener) [[unlikely]]` in `latchRegister`, which runs once per `#BFFD` OUT
(64 per frame in the player-load benchmark), never per sample. A/B of `BM_TurboSoundFrame_PlayerLoad` (Pentagon,
64 register pairs per frame, 1000 frames x 5 repetitions, 4 interleaved before / after rounds, separate Release build
`cmake-build-bench`, load average 13-16): CPU medians 2176 / 2154 / 2171 / 2147 us before, 2157 / 2164 / 2166 / 2150 us
after - mean 2162 vs 2159 us, no change within the noise (`BM_TurboSoundFrame_Idle`: 2504-2637 us both, no change).

**`MidiLine`.** `MidiLine(port, bit)` (default: port A, bit 2), `Connect(sam2695::Synth*)`, `Reset(t)`,
`OnIoPortPins`, `Describe(MidiLineReport&)` (port, bit, connected, level, last change time, edges). TTD blob (version
1, 18 bytes): level, last change time, edge counter; no `PeripheralId` of its own - the card carries it in its blob set.
The header forward-declares `sam2695::Synth`; the `.cpp` includes the library under `UNREALNG_HAVE_SAM2695`.

**Deviations from §2.**

1. **No `CardClock` in `Connect`.** `MidiLine` passes the owner's time through unchanged; the synthesizer is configured
   with the same tick rate (`SynthConfig::hostTickRate`). The card integration decides the axis (the card's own clock
   or the machine's T-states) and converts before the chip, so one conversion serves the whole card.
2. **R7 reset value (open, owner decision).** The real chip clears every register on reset: both ports are inputs and
   the pins read `#FF`. `SoundChip_AY8910::reset()` sets R7 = `#FF` (all generators off), which in the pin model makes
   both ports outputs carrying latch 0 - a MIDI line on this chip would sit low from power-on until software writes
   R7 / R14. Not changed in ML-1: any change of the reset register file shifts the TTD bench gate
   (`testdata/ttd/bench/v1-ci-gate.txt`, compressed device-blob sizes at 0 % tolerance: R7 = `#3F` moved
   `Ci/TTDBench_Test.CiGate` for 48K idle, Pentagon idle and Pentagon game by a few bytes per frame; R14 / R15 = `#FF`
   changes the same blob bytes and was not run), and R7 = `#3F` also changes what programs read
   from R7 (`TsfmPlayerHarness_Test.InitWritesChipResetThenParksInWaitStatus` relies on bit 7 of the power-on value
   on the legacy device). The MultiSound does not depend on it (its MIDI pin is on the YM2203 SSG, MS-1). Recommended
   fix when the 128K MIDI / RS-232 use lands: R14 / R15 = `#FF` at reset (same pins as the real chip, and the same
   reads - an input port reads its pulled-up pins) plus a gate baseline refresh.
3. **YM2203 SSG not hooked yet** (resolved in MS-1: the pair's SSG is a `SoundChip_AY8910`, so `Ym2203Pair::setIoPortListener(chip, listener)` reuses the AY's pin cache and listener as they are; pin changes carry the write's host tick) (owner decision 2026-10-04: `soundchip_turbosoundfm.*` and `tsfm/*` change on the
   `ttd-engine` branch). The interface is chip-agnostic: `Ym2203Pair`'s SSG (MS-1) keeps a pin cache of two bytes,
   calls `AyIoPort::AffectsPins(reg)` on its register writes behind one listener test, and reports
   `AyIoPort::Pins(r7, port, latch)` changes to the same `IAyIoPortListener`.
4. **Bad-timing tests.** "2x speed produces framing errors" does not hold for `#90 #3C #64`: at 56 T per bit the stop
   samples land on high bits and the UART assembles other bytes (`#6A`, `#FD`) without a framing error - what a real
   UART does too. `FramingErrorFromBadTiming` uses half speed (224 T per bit: a routine timed for a 7 MHz CPU run at
   3.5 MHz), which reads each bit twice and finds low stop bits; `DoubleSpeedGivesWrongBytes` covers 2x speed.
5. **Parsed bytes check.** The library has no parser tap in its public API; `BitTimelineToSynth` checks the UART count
   (3 bytes, 0 framing errors) and the parser's effect (one voice on channel 1 and nowhere else, from a one-preset bank
   built in the test). A public "last parsed message" tap in `SynthReport` would let the test compare the bytes
   themselves (noted for the library owner).
6. `MidiLine_Test.Chip2DoesNotDrive` and the program-level Z80 test move to the MultiSound integration: both need the
   card's chip select.
