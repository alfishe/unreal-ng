# Controller manager: Surface Dial

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Built: input on macOS (IOHID) and Linux (evdev), clicks and detents on macOS, touch inferred from actions; Linux outputs (hidraw) and Windows (HID `ReadFile`, clicks and steps; the system wheel held per window, §4.1) built, not run on a Dial |
| **Code** | `core/controllers/src/hid/drivers/surfacedial.{h,cpp}`, the IOHID, evdev (with hidraw) and Raw Input (`platform/windows/rawinputbackend`) backends, `core/controllers/src/wheelclaim.{h,cpp}` with `unreal-qt/src/platform/windows/wheelmenu_windows.{h,cpp}` (§4.1), `core/tests/controllers/surfacedial_test.cpp` |
| **Fixtures** | [testdata/controllers/hid-captures/surface-dial/](../../../testdata/controllers/hid-captures/surface-dial/macos-ble/README.md) |

Microsoft `045E:091B`, Bluetooth LE only. A control surface, not a player device (`"player": false` in
`data/controllers/devices.json`): it takes no player connector and binds to actions. Reference for the report layout
(no code taken, no license file): [daniel5151/surface-dial-linux](https://github.com/daniel5151/surface-dial-linux).

## Contents

- [1. Input](#1-input)
- [2. Outputs](#2-outputs)
- [3. Touch latency](#3-touch-latency)
- [4. Per OS](#4-per-os)
  - [4.1 Windows: the system wheel](#41-windows-the-system-wheel)

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
- gives the last steps again after a reconnect (the backend keeps them per device id);
- plays one output report at a time (`SurfaceDialPlays`). The Dial takes every output report at once (each write a
  ~30 ms BLE round trip) and plays them one after another: measured 2026-10-28, ten buzzes written in 300 ms buzzed far
  longer than one, and a fast turn at the end of the history (an `edge` buzz each step, never rate limited) kept the
  Dial buzzing for seconds. Output report 1 with Manual Trigger 2 (stop, `SurfaceDial::StopReport`) cut such a queue
  off. So the next play goes when the last one has played (a click its retrigger period, 80 ms at least; a buzz
  250 ms); meanwhile one request waits, the strongest asked for (a buzz over more clicks over fewer); a request while
  one already waits is an overflow: the weaker is dropped (`Dropped()`), and the next play goes after a stop, which
  also ends whatever the Dial still holds if the estimate was short. Single steps play as before, at once.

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
| Windows | The Raw Input backend (`platform/windows/rawinputbackend`): HID interfaces of top-level collection `0x01:0x0E`, found with SetupDi and `RegisterDeviceNotification`, opened shared (Windows keeps its own use), overlapped `ReadFile` into `SurfaceDial::Decode`; a 100 ms tick ends an inferred touch (built, not run on a Dial) | Through `SurfaceDialWriter`: `HidD_SetOutputReport` output report 1 (clicks, patterns), `HidD_SetFeature` feature report 1 (steps and detents; `HidD_GetFeature` reads the found settings back, restored at the end), each zero-padded to the collection's report length (built, not run on a Dial) | Windows also feeds the Dial to `RadialController`: while a Dial is connected the emulator's windows hold the system wheel (no built-in tools, no wheel menu; §4.1, built, not run on a Dial). The Windows host had no Dial (device listing 2026-10-21) |
| Linux | evdev (`REL_DIAL`, `BTN_0`, `BTN_TOUCH`; built) | hidraw of the same HID device, found through sysfs (`hid/hidrawnode`): output report 1 by `write()` for the clicks and patterns, feature report 1 by `HIDIOCSFEATURE` for the steps and detents, through `SurfaceDialWriter`; feature report 1 read by `HIDIOCGFEATURE` (the found settings, each write read back, restored at the end); the steps kept by id and sent again on a reconnect (built, not exercised on hardware). Needs `data/controllers/70-unreal-ng.rules` (uaccess); without access Test haptics is off with the reason (the node, the rule, the commands) | BlueZ pairing, sleeps and reconnects on touch; the event node is not a joystick, so the rule also gives it to the seat user |

### 4.1 Windows: the system wheel

Windows reads the Dial as well: its top-level collection `0x01:0x0E` is a Windows radial controller
([radial controller protocol](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/radial-controller-protocol-implementation)),
fed to `RadialController`. The collection is not one Windows opens exclusively
([top-level collections opened by Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/top-level-collections-opened-by-windows-for-system-use)
lists mice, keyboards, pens, touch screens and touchpads as exclusive; `0x01:0x0E` is not listed), so the backend's
shared `ReadFile` and Windows both get every report. In the focused app the active tool takes the turns: the built-in
ones are volume, scroll, zoom, undo / redo, brightness on the desktop, next / previous track while media plays, and a
press and hold opens the wheel menu, which then takes the turns and clicks itself
([Surface Dial interactions](https://learn.microsoft.com/en-us/windows/apps/design/input/windows-wheel-interactions)).
Without a hold, a turn in the emulator's window would step the machine and scroll or change the volume as well.

What a Win32 app can do (Microsoft Learn, and the Windows SDK 10.0.22621 headers `RadialControllerInterop.h` and
`winrt/windows.ui.input.h` on the Windows host):

| API | Since | What it does for the window |
|:--|:--|:--|
| [`IRadialControllerInterop::CreateForWindow`](https://learn.microsoft.com/en-us/windows/win32/api/radialcontrollerinterop/nf-radialcontrollerinterop-iradialcontrollerinterop-createforwindow) | 1607, desktop apps | A [`RadialController`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontroller) bound to an HWND (the desktop form of `CreateForCurrentView`) |
| `RadialController.Menu.Items` + `SelectMenuItem` | 1607 | A custom tool of the app, made active: `RotationChanged` / `ButtonClicked` go to the app instead of a built-in tool ("your app does not receive this event when the menu is active or a built-in tool is active") |
| [`UseAutomaticHapticFeedback`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontroller.useautomatichapticfeedback) | 1607 | Windows' click per `RotationChanged` of the app's tool, on or off. The built-in tools' haptics cannot be changed by an app, only by the user (Settings > Wheel) |
| [`IRadialControllerConfigurationInterop::GetForWindow`](https://learn.microsoft.com/en-us/windows/win32/api/radialcontrollerinterop/nf-radialcontrollerinterop-iradialcontrollerconfigurationinterop-getforwindow) | 1607, desktop apps | The window's [`RadialControllerConfiguration`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontrollerconfiguration) |
| [`SetDefaultMenuItems`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontrollerconfiguration.setdefaultmenuitems) (empty) / `ResetToDefaultMenuItems` | 1607 | The built-in tools shown for the window: none / the defaults again. "The menu must have at least one menu item. If all default tools are removed before you add one of your custom tools, the default tools are restored": the app's tool goes first. On the UI thread |
| [`IsMenuSuppressed`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontrollerconfiguration.ismenusuppressed) | 1703 | "When the menu is suppressed, the press-and-hold gesture does not cause it to be displayed"; `ButtonHolding` goes to the app |
| [`ActiveControllerWhenMenuIsSuppressed`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontrollerconfiguration.activecontrollerwhenmenuissuppressed) | 1703 | The controller that takes the input while the menu is suppressed |
| `AppController` / `IsAppControllerEnabled` | 1709 | One controller for every window of the process (not needed: two windows, a hold each) |

The configuration is per window: when another app is in the foreground, its own tools act there, and the user's
settings are not touched.

**Implemented** (built with MSVC and MinGW, not run on a Dial): `WheelClaim` (`core/controllers/src/wheelclaim`, pure,
`SurfaceDial_Test.WheelClaim_WhileADialIsConnected`) decides which windows hold the wheel: the main and the debugger
windows (top-level HWNDs that already exist; one made later is taken at the next 250 ms poll, one made again is
released and taken again) while any connected device's family names a `knob` (the Surface Dial). `HostControllers`
(unreal-qt) feeds it and calls `WheelMenuWindows::Take` / `Release` (`unreal-qt/src/platform/windows/wheelmenu_windows`,
only on the `windows` Qt platform). A hold, on the GUI thread (an STA already: Qt's `OleInitialize`):

1. a `RadialController` for the window, one custom tool "Unreal NG" (`CreateFromKnownIcon`), appended and selected,
   `UseAutomaticHapticFeedback` off: the emulator plays the Dial's clicks and detents itself (§2);
2. `SetDefaultMenuItems` with an empty iterable: no built-in tools for the window;
3. `ActiveControllerWhenMenuIsSuppressed` = that controller, `IsMenuSuppressed` = true.

Release (the last Dial gone, the window made again, the client quits) undoes them in reverse:
`IsMenuSuppressed` false, no active controller, `ResetToDefaultMenuItems`, the tool removed. The tool does nothing:
its events are not subscribed, the HID read stays the input. The WinRT entry points (`RoInitialize`,
`RoGetActivationFactory`, `WindowsCreateString`) are loaded from `combase.dll` at run time and the few ABI interfaces
are declared in the file from the SDK header (MinGW has no `Windows.UI.Input` headers); a hold that cannot be made
(before 1607, a Server) is no hold, and a partial one (1607: no suppression) logs what it kept with the HRESULTs.
The thread's apartment is never undone: on a thread without one of its own, a second hold after `RoUninitialize`
failed `GetForWindow` with `0x80070776` (object exporter not found).

Checked on the Windows host (Windows 10 19045, over SSH, no Dial) with a throwaway probe: three holds made and
released on a real top-level window, built with MSVC and with MinGW, in an STA as on Qt's GUI thread and without one:
every call returned `S_OK` (`CreateForWindow`, the tool, `SetDefaultMenuItems` with the empty iterable,
`ActiveControllerWhenMenuIsSuppressed`, `IsMenuSuppressed`, the release). That checks the declared ABI and the
calls; what the shell then does with a Dial is the open part below.

**No `RadialController` input path.** The HID read is possible (shared, as above) and stays the only input: it gives
the raw steps (one per Resolution Multiplier count, finer than `RotationChanged`'s default 10 degrees), the touch bit
(§3), presses without the hold time, the same decoder on every OS (`SurfaceDial::Decode`), and it reads the Dial in
the background like any pad. `RotationChanged` only reaches the foreground window while its tool is active. A
fallback would only help where the HID collection cannot be opened, which no source shows for Windows 10 / 11.

**What remains with Windows**, by the documents:

- Settings > Devices > Wheel stays the user's (default tools, vibration, the writing hand); an app cannot turn off
  the built-in tools' haptics. While the hold is active none of them is active in the emulator's windows.
- A press and hold opens no menu in the held windows, so the user cannot switch to a built-in tool there (volume
  included, against Microsoft's guideline "don't remove the built-in system volume tool"): the price of a Dial that
  only drives the emulator. In any other app the menu is as before.
- The Dial still drives the emulator when another app is in the foreground (the HID read does not follow the focus,
  like the pads), and there the other app's tool acts too.

**Not measured** (the Windows host has no Dial):

- that a turn in a held window reaches neither volume nor scroll, and a press and hold opens nothing;
- whether Windows writes the Dial's feature report 1 (Auto Trigger, which "may need to be modified or disabled by the
  host", [radial controller feature reports](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/radial-controller-feature-reports))
  when the active tool or the focus changes, and so overwrites the emulator's steps and detents (§2). If it does, the
  backend's steps have to be sent again on `ControlAcquired`;
- whether the hold survives the Dial sleeping and waking (a new HID interface), and Explorer restarting.
