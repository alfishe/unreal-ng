# Control surfaces: controls and feedback

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review |
| **Extends** | [controller manager §6 canonical controls](../2026-10-17-controller-manager/architecture.md#6-the-canonical-control-vocabulary), [§10 outputs](../2026-10-17-controller-manager/architecture.md#10-outputs-rumble-leds-triggers), `core/controllers/src/hostcontrols.h` |

## Contents

- [1. What v1 adds](#1-what-v1-adds)
- [2. Encoder](#2-encoder)
- [3. Outputs](#3-outputs)
- [4. Haptic patterns](#4-haptic-patterns)
- [5. Names](#5-names)

## 1. What v1 adds

| Added | Why |
|:--|:--|
| `ControlType::Encoder` | the Dial's knob |
| output `haptic` (a pattern) | marks |
| output `steps` (steps per turn, clicks on / off) | the context's feel |

`Delta` stays the mouse's: an encoder has steps, a speed and feedback, and never maps to a mouse axis by default.
Touch strips, keys with displays, text, images and lights are not in v1 ([TODO.md](TODO.md#later)).

## 2. Encoder

- `ControlEvent.value` is the signed number of steps since the last event; the driver turns raw counts into steps
  and carries remainders (on the Dial one count is one step, POC 030 A2).
- **Time**: IOHID gives each report a host timestamp; the backend fills `hostTimeNs` with it (today no backend
  fills it, and the router falls back to its own clock, `inputrouter.cpp:246`).
- **Speed** (`rate`, steps/s) per control. Starting rule, the one `streamdeck-bridge` uses for its dials
  (`runtime.cpp`): `inst = |delta| / interval`; `rate = 0.5 × rate + 0.5 × inst`; after more than 400 ms without a
  step `rate = 0.3 × inst`. To retune for the Dial in phase S3: its step rate differs from a Stream Deck dial and BLE
  delivers reports in connection-interval bunches.
- The router coalesces the steps of one pass into one `Delta` action value with the current rate
  ([architecture §5](architecture.md#5-the-routers-part)).

## 3. Outputs

`IBackend` has `HasRumble`, `RumbleActuators`, `SetRumble`. Added:

```cpp
enum class OutputKind : uint8_t { Haptic, Steps };

struct HapticOut { HapticPattern pattern; };
struct StepsOut  { uint16_t perTurn = 36; bool clicks = true; };   // clicks false: free wheel, the steps stay

class IBackend {
    ...
    virtual uint32_t Outputs(const HostDeviceId&) const { return 0; }                 // OutputKind bits
    virtual void Output(const HostDeviceId&, const HapticOut&) {}                      // never blocks
    virtual void Output(const HostDeviceId&, const StepsOut&) {}                       // never blocks
};
```

Both calls only queue; the device's driver writes on its own thread ([architecture §9](architecture.md#9-haptic-feedback)).
Rumble keeps its own path through `HapticMixer`.

As built (S3, 2026-10-20): `OutputKind` is a bit set (`kOutputHaptic`, `kOutputSteps`), `Outputs` returns `uint8_t`,
the `Output` calls return whether they queued; `HapticPattern` and the outputs live in `hostbackend.h`. Rumble and
`PlayClicks` (test effects) are unchanged for callers; on the IOHID backend `PlayClicks` now queues on the writer.

## 4. Haptic patterns

| Pattern | Use | Dial (to confirm in POC 030 A4, A5) |
|:--|:--|:--|
| `tick` | a second | output report: Manual Trigger 3 (click), Repeat Count for one play |
| `double` | 10 s, a bookmark | click, Repeat Count for two plays, Retrigger 60 ms |
| `buzz` | end of history, a barrier | Manual Trigger 4 (buzz continuous), length set by Waveform Cutoff |

Profiles name patterns, never reports; the driver chooses the bytes (as built: [architecture §9.1](architecture.md#91-as-built-s3-2026-10-20)). Whether Repeat Count `r` plays `r` or `r + 1`
times and how long a buzz lasts are measured in POC 030 before the driver is written.

## 5. Names

`key.<PcKey>` is the keyboard's, so surface controls do not use `key.`.

| Name | What |
|:--|:--|
| `knob`, `push`, `touch` | Surface Dial (and any single-knob puck); `touch` = the hand is on the dial |
