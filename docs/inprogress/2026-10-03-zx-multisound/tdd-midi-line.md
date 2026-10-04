# MIDI line (AY / YM2203 I/O port to the synthesizer): technical design

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
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
  (`#FF`) - the documented AY behavior; the datasheet check for the YM2203 is a SAM-0 / ML-0 item (hardware reference §7
  lists the IOA output type as open). A change of R7's direction bits re-evaluates the pins.
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
| `MidiLine_Test.FramingErrorFromBadTiming` | the same sequence at 2× speed produces framing errors, counted |
| `MidiLine_Test.Chip2DoesNotDrive` | bit-banging chip 2's R14 produces nothing |
| `MidiLine_Test.TtdRoundTrip` | save mid-byte, restore, the byte completes identically |

A program-level test runs a Z80 snippet (the 128K ROM-style MIDI send routine) under `RunNFrames` against the
MultiSound and checks the synthesizer's parsed messages, with TTD recording on.

## 4. Phases

| Phase | Content | Size |
|---|---|---|
| ML-0 | YM2203 / AY datasheet check of the I/O port output stage and the input-mode pin level | S |
| ML-1 | `IAyIoPortListener` on the AY and the YM2203 SSG, tests §3 first block | S |
| ML-2 | `MidiLine` + wiring into the MultiSound card, tests §3 second block | S |
