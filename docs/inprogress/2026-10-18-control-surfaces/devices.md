# Control surfaces: devices

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Design, for review; Surface Dial observed on macOS 15 (Darwin 24.6) over Bluetooth LE, felt measurements pending ([POC 030](../../../tools/poc/030-surface-dial-haptics/README.md)) |
| **Related** | [research-host-apis.md](../2026-10-17-controller-manager/research-host-apis.md), [device-database.md](../2026-10-17-controller-manager/device-database.md) |

## Contents

- [1. Surface Dial](#1-surface-dial)
- [2. Stream Deck + XL through streamdeck-bridge](#2-stream-deck--xl-through-streamdeck-bridge)
- [3. Later: DIY HID devices and MIDI](#3-later-diy-hid-devices-and-midi)
- [4. Database entry](#4-database-entry)

## 1. Surface Dial

Microsoft `045E:091B`, Bluetooth LE only, firmware 29d51170 on the owner's Dial. Layout reference (no code taken,
no license file): [daniel5151/surface-dial-linux](https://github.com/daniel5151/surface-dial-linux). Capture:
`testdata/controllers/hid-captures/surface-dial/macos-ble/`.

### 1.1 What it reports

One top-level collection Generic Desktop / System Multi-Axis Controller (`0x01:0x0E`, report id 1) with a Digitizer
Puck (`0x0D:0x21`) and a Simple Haptic Controller (`0x0E:0x01`); descriptor 488 bytes. macOS opens it without
seizing; max input report 73 bytes.

**Input report 1** (9 bytes):

| Byte | Bits | Usage | Value |
|:--|:--|:--|:--|
| 0 | | report id | `01` |
| 1 | 0 | Button 1 | push |
| 1 | 1 | Digitizer Touch (`0x0D:0x33`) | the hand is on the dial |
| 2-3 | | Dial (`0x01:0x37`), int16 LE, relative | steps since the last report: `01 00` +1, `ff ff` −1 |
| 4-8 | | X, Y (null), Width (constant 58) | ignore |

Report `32` is System Control (Sleep `0x82`, Wake `0x83`): `32 02` (the Wake bit) arrives about 2 ms after every
press report, so the driver ignores it and never treats it as a reconnect. Reports `2A`-`31` are vendor (`0xFF07`).
The capture's filter drops repeated reports, so it shows direction changes, not every step (POC 030).

### 1.2 Haptics

**Feature report 1** (8 bytes), found as `01 24 00 00 01 05 50 00`:

| Byte | Usage | Logical range (descriptor) | Found |
|:--|:--|:--|:--|
| 1-2 | Resolution Multiplier (`0x01:0x48`), steps per turn | 0..3600 | 36 |
| 3 | Repeat Count (`0x0E:0x24`) | 0..255 | 0 |
| 4 | Auto Trigger (`0x0E:0x20`): waveform played at every step | 1..7 | 1 (none) |
| 5 | Waveform Cutoff Time (`0x0E:0x28`) | 1..10, unit not given | 5 |
| 6-7 | Retrigger Period (`0x0E:0x25`), ms | 1..2000 | 80 |

**Output report 1** (5 bytes): Repeat Count, Manual Trigger (`0x0E:0x21`, a waveform ordinal 1..7), Retrigger Period.

**Waveforms** (feature report 2): 1 none, 2 stop (implicit), 3 click (`0x1003`), 4 buzz continuous (`0x1004`),
5 again `0x1004` in this firmware.

Observed so far (POC 030): feature and output writes return success; a feature write with cutoff and retrigger 0
(below the logical minimum) applied the other fields and kept the old cutoff and retrigger. **Local clicks are off
as found** (Auto Trigger 1): the driver must turn them on.

v1 driver writes (each value to confirm by feel, POC 030 A3-A6):

| Request | Report |
|:--|:--|
| steps n, clicks on | feature `01, n lo, n hi, 00, 03, 05, 50 00` |
| steps n, clicks off | feature `01, n lo, n hi, 00, 01, 05, 50 00` (Auto Trigger none; steps kept) |
| `tick` | output `01, r, 03, 50 00`, r for one play (A4) |
| `double` | output `01, r, 03, 3C 00`, r for two plays, 60 ms apart |
| `buzz` | output `01, r, 04, 50 00`, length by the cutoff (A5) |

### 1.3 Per OS

| OS | Input | Haptics | v1 |
|:--|:--|:--|:--|
| macOS | IOHID: the backend adds matching `0x01:0x0E` | `IOHIDDeviceSetReport` on the driver's writer thread | yes |
| Windows | a raw HID backend does not exist yet (only XInput, D6 plans an own raw HID backend); the system also feeds the Dial to `RadialController` and shows its wheel menu | `HidD_SetFeature`, `HidD_SetOutputReport` | later; measure first (Q-4) |
| Linux | `hid-generic` gives an input device; the evdev backend can read it; haptics need hidraw and a udev rule | hidraw | later |

## 2. Stream Deck + XL through streamdeck-bridge

`0FD9:00C6`: 36 keys (9 × 4), 6 dials, a touch window of 1200 × 100 logical points that reports TAP, PRESS and FLICK
only, with no drag points (OpenDeck `docs/hid/05-input.md`). It stays a process-side device: `streamdeck-bridge`
owns it, renders its faces and runs the scenario; raw access from the core stays out of scope
([debug-and-ttd §5](../2026-10-17-controller-manager/debug-and-ttd.md#5-debugger-remote-and-stream-deck)).

| Today | v1 |
|:--|:--|
| cpp-httplib REST client, no WebSocket | adds a WebSocket client for `/api/v1/ws` (topic `surface`) (library chosen in phase S5) |
| `ttd_seek` computes the curve and clamps on its side | `ttd.seek_by` with `{unit, min, max}`; the core clamps and returns marks |
| `run_toggle` reads `is_paused`, posts `/pause` or `/resume` | `run.pause_toggle` |
| strip readout from `/ttd/position` polled at 50 Hz, `/ttd/status` every 500 ms | `ttd.position` readouts |
| pages are its own state | a page with `"context"` calls `ctx.set`; a `ctx` from elsewhere may open the matching page |

## 3. Later: DIY HID devices and MIDI

- A DIY device (ESP32-S3, RP2040 with TinyUSB, nRF52840) presents itself as a HID device in the Dial's descriptor
  pattern (Dial usage with a Resolution Multiplier, buttons, a Simple Haptic Controller). A generic HID encoder driver
  then reads it with no code of its own. Example firmware as a POC.
- A MIDI controller is a local host device too: a MIDI backend (CoreMIDI, WinMM, ALSA) maps relative CC to
  `Encoder`, notes to buttons.

## 4. Database entry

A family in `data/controllers/devices.json`, in its schema (`families[]` with `id`, `name`, `vendor`, `match[]`,
`layout`, `images`, `glyphs`, `controls[]` names, `devicedatabase.cpp`):

```json
{ "id": "surface-dial", "name": "Surface Dial", "vendor": "Microsoft",
  "match": [ { "bus": "bt", "vid": "045e", "pid": ["091b"] } ],
  "layout": "dial", "images": "surface-dial",
  "controls": ["knob", "push", "touch"] }
```

The control types and outputs come from the driver, not from the database. No role field: a default role per
family would need a new `DeviceMatch` field and a caller of `InputRouter::SetRole` (none today); that is Q-8.
