# Controller manager: Surface Dial

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Built: input on macOS (IOHID) and Linux (evdev), clicks and detents on macOS, touch inferred from actions; Linux outputs (hidraw) and Windows (HID `ReadFile`, clicks and steps) built, not run on a Dial |
| **Code** | `core/controllers/src/hid/drivers/surfacedial.{h,cpp}`, the IOHID, evdev (with hidraw) and Raw Input (`platform/windows/rawinputbackend`) backends, `core/tests/controllers/surfacedial_test.cpp` |
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
it: it has no levels.

Local detents are feature report 1 with auto trigger 3 (`SurfaceDial::FeatureReport`): the profile's `steps` /
`clicks` per layer ([debug-and-ttd.md §4.4](debug-and-ttd.md#44-surface-dial)) and the `output` verb's `steps` /
`clicks` reach `IBackend::Output(StepsOut)`, and the macOS backend hands them to the Dial's writer thread
(`SurfaceDialWriter`). The writer:

- reads feature report 1 first (the settings as found) and keeps the found cutoff and retrigger in every write;
- reads each feature write back: a write the device took but did not apply counts as failed (POC 030 saw fields out of
  the logical range dropped silently);
- writes the found settings back when it ends (the backend stops, the device goes): the Dial keeps its detents until it
  sleeps and must not stay clicking after the emulator quits;
- gives the last steps again after a reconnect (the backend keeps them per device id).

Unit-tested with a fake device (`Surfaces_Test.DialWriter_*`). Live: the write and read-back of feature report 1 were
measured with the POC 030 probe on 2026-10-18 (`01 0c 00 00 03 00 00 00` read back `01 0c 00 00 03 05 50 00`, then
restored). On 2026-10-22 [probes/dialsteps.c](probes/dialsteps.c) wrote the driver's exact bytes over IOHID with a
read-back each, all applied as written: found `01 24 00 00 01 05 50 00`; 36 steps with clicks `01 24 00 00 03 05 50 00`;
72 with clicks `01 48 00 00 03 05 50 00`; 36 free wheel `01 24 00 00 01 05 50 00`; the found report written back and
read back. Not measured yet: the writes from the running emulator, and a click per step by feel (POC 030 A3).

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
| macOS | IOHID, match `0x01:0x0E` besides the pad usages | `IOHIDDeviceSetReport` output report 1 (built); feature report 1 for the detents, read back with `IOHIDDeviceGetReport` and restored at the end (built; live check of the backend pending, §2) | Sleeps and wakes on touch: it can be missing from the device list until touched; a reconnect is a new IOHID device with the same id |
| Windows | The Raw Input backend (`platform/windows/rawinputbackend`): HID interfaces of top-level collection `0x01:0x0E`, found with SetupDi and `RegisterDeviceNotification`, opened shared (Windows keeps its own use), overlapped `ReadFile` into `SurfaceDial::Decode`; a 100 ms tick ends an inferred touch (built, not run on a Dial) | Through `SurfaceDialWriter`: `HidD_SetOutputReport` output report 1 (clicks, patterns), `HidD_SetFeature` feature report 1 (steps and detents; `HidD_GetFeature` reads the found settings back, restored at the end), each zero-padded to the collection's report length (built, not run on a Dial) | Windows also feeds `RadialController` and shows its wheel menu: still to measure whether both get the turns while the emulator reads the Dial, and whether the wheel menu has to be turned off. The Windows host had no Dial (device listing 2026-10-21) |
| Linux | evdev (`REL_DIAL`, `BTN_0`, `BTN_TOUCH`; built) | hidraw of the same HID device, found through sysfs (`hid/hidrawnode`): output report 1 by `write()` for the clicks and patterns, feature report 1 by `HIDIOCSFEATURE` for the steps and detents, through `SurfaceDialWriter`; feature report 1 read by `HIDIOCGFEATURE` (the found settings, each write read back, restored at the end); the steps kept by id and sent again on a reconnect (built, not exercised on hardware). Needs `data/controllers/70-unreal-ng.rules` (uaccess); without access Test haptics is off with the reason (the node, the rule, the commands) | BlueZ pairing, sleeps and reconnects on touch; the event node is not a joystick, so the rule also gives it to the seat user |
