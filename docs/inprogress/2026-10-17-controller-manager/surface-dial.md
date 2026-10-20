# Controller manager: Surface Dial

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Built: input on macOS (IOHID) and Linux (evdev), clicks on macOS, touch inferred from actions |
| **Code** | `core/controllers/src/hid/drivers/surfacedial.{h,cpp}`, the IOHID and evdev backends, `core/tests/controllers/surfacedial_test.cpp` |
| **Fixtures** | [testdata/controllers/hid-captures/surface-dial/](../../../testdata/controllers/hid-captures/surface-dial/macos-ble/README.md) |

Microsoft `045E:091B`, Bluetooth LE only. A control surface, not a player device (`"player": false` in
`data/controllers/devices.json`): it takes no player connector and binds to actions. Reference for the report layout
(no code taken, no license file): [daniel5151/surface-dial-linux](https://github.com/daniel5151/surface-dial-linux).

## Contents

- [1. Input](#1-input)
- [2. Outputs](#2-outputs)
- [3. Touch latency](#3-touch-latency)
- [4. Per OS](#4-per-os)

## 1. Input

The report descriptor (488 bytes) has one top-level collection Generic Desktop / System Multi-Axis Controller
(`0x01:0x0E`), report id 1, a Digitizer Puck (`0x0D:0x21`) inside and a Simple Haptic Controller (`0x0E:0x01`). macOS
gives it to an `IOHIDManager` opened without seizing, primary usage `1:14`, max input report 73 bytes. A device of the
same pattern (a Dial usage `0x01:0x37` inside `0x01:0x0E`) is read the same way (`SurfaceDial::Matches`).

Input report 1 (9 bytes):

| Byte | Bits | Usage | Value |
|:--|:--|:--|:--|
| 0 | | report id | `01` |
| 1 | 0 | Button 1 | `push` |
| 1 | 1 | Digitizer Touch (`0x0D:0x33`) | `touch`: the hand is on the dial (§3) |
| 2-3 | | Dial (`0x01:0x37`), int16 LE, relative | `knob`: steps since the last report |
| 4-8 | | X, Y, Width (constant) | ignored |

Report `32` (System Control sleep / wake) arrives on wake; `2A-31` (vendor `0xFF07`, §3) are ignored. One count is
one step of the Resolution Multiplier (feature report 1 bytes 1-2, 36 as found). The live view shows `knob` as the
sum of its steps since the Dial connected.

## 2. Outputs

Feature report 1 (8 bytes, read `01 24 00 00 01 05 50 00`): steps per turn (uint16, 0..3600), repeat count, auto
trigger (the waveform at every step: 1 none, 3 click), waveform cutoff, retrigger period (ms). This Dial has **no
local clicks by default** (auto trigger 1). Feature report 2 lists the waveforms: `03 04 05`.

Output report 1 (5 bytes): `01`, repeat count (the number of plays), manual trigger waveform (3 click, 4 buzz),
retrigger period (uint16 LE, ms). Measured on macOS: `[01 03 03 00 00]` plays three clicks.

The Dial is a clicker actuator (`kHapticClicker`, [haptics.md §2](haptics.md#2-actuators)): `IBackend::PlayClicks`
sends output report 1 (`SurfaceDial::HapticReport`). Its test effects, in the dialog's Test haptics and the `rumble`
verb, are `identify` (two clicks), `click`, `clicks` (three, 60 ms apart) and `buzz`. The game's motor haptics skip
it: it has no levels. Local detents (feature report 1 with auto trigger 3) are not set yet.

Its default profile makes it a time travel remote ([debug-and-ttd.md §4.4](debug-and-ttd.md#44-surface-dial)): a
turn moves a frame per step, a turn while pressed an instruction per step, a press alone pauses / continues.

## 3. Touch latency

Measured 2026-10-19 with [probes/dialtouch.c](probes/dialtouch.c) against the owner's hand: the touch bit comes on
and goes off a second or more after the hand, both ways. The release always arrives (11 of 11 touches in a minute),
only late. No other report carries the hand: the device sends input reports `01` and `32` only. Vendor page `0xFF07`
(reports `2A-31`) is Microsoft's CFU firmware update channel: feature `2A` holds the firmware version (`70 11 d5 29`
= 29d51170). [probes/dialfeat.c](probes/dialfeat.c) only reads those feature reports, and nothing is written there.

So `touch` is inferred (`DialTouch`, the same in every backend):

- a turn or a press shows the hand at once, before the sensor;
- the sensor's release ends it, and the sensor's own touch is never cut short;
- a touch the sensor never confirmed (the hand left before the sensor saw it) ends 3 s after the last action, never
  while the button is held. The backends wake 10 times a second while a Dial is connected to end it on time.

A touch without a turn or a press stays as slow as the sensor.

## 4. Per OS

| OS | Input | Outputs | Notes |
|:--|:--|:--|:--|
| macOS | IOHID, match `0x01:0x0E` besides the pad usages | `IOHIDDeviceSetReport` output report 1 (built); feature report 1 for detents (not yet) | Sleeps and wakes on touch: it can be missing from the device list until touched; a reconnect is a new IOHID device with the same id |
| Windows | HID collection `0x01:0x0E` via `HidD_*` / `ReadFile`, not opened exclusively by Windows (not built) | `HidD_SetFeature`, `HidD_SetOutputReport` | Windows also feeds `RadialController` and shows its wheel menu: to measure |
| Linux | evdev (`REL_DIAL`, `BTN_0`, `BTN_TOUCH`; built) | hidraw (udev rule; not built: Test haptics is off with the reason) | BlueZ pairing, sleeps and reconnects on touch |
