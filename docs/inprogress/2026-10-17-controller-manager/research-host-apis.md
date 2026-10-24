# Research: host controller APIs and the Xbox Elite Series 2

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Research; measurements on the dev Mac (macOS 15, Darwin 24.6, Apple silicon) |
| **Feeds** | [architecture.md §4-§5, §10-§11](architecture.md#4-backends-per-os), [device-database.md](device-database.md), [debug-and-ttd.md](debug-and-ttd.md) |
| **Probes** | [probes/](probes/): `probe.c`, `probe2.c` (SDL3), `gc.m` (GameController.framework) |

## Contents

- [1. What the repository has](#1-what-the-repository-has)
- [2. Measurements on this machine](#2-measurements-on-this-machine)
- [3. Backends per OS](#3-backends-per-os)
- [4. Xbox Elite Series 2](#4-xbox-elite-series-2)
- [5. Other controllers](#5-other-controllers)
- [6. Stream Deck](#6-stream-deck)
- [7. Latency, threading, determinism](#7-latency-threading-determinism)
- [8. What the research recommended](#8-what-the-research-recommended)
- [9. Why native first (D2, D6)](#9-why-native-first-d2-d6)
- [10. Uncertainties](#10-uncertainties)

## 1. What the repository has

- **No input libraries in shipped code.** There is no SDL, hidapi or gamepad code. `lib/` holds googletest and
  benchmark.
- **Qt.** The Qt apps use Qt 6 (Homebrew qtbase 6.11.2 / 6.12.0). Qt 6 has no gamepad module ([§3.4](#34-qt)).
- **SDL3 only in proofs of concept.** SDL3 appears in `apps/steamdeck-client/` and
  `tools/poc/018-tui-debuggers/`. Homebrew has SDL3 3.4.18 (`/opt/homebrew/Cellar/sdl3/3.4.18`) and sdl2-compat.
- **The entry point exists.** `TimeTravelManager::SubmitLiveInput` with `TTDInputKind::Joystick` / `NextPad` / `PcKey`
  (`core/src/debugger/ttd/ttdinputjournal.h`). Any live source must use it and must not write devices directly.

## 2. Measurements on this machine

Devices attached at the time:

| Device | VID:PID | Seen as |
|:--|:--|:--|
| Xbox Elite Wireless Controller Series 2, USB, firmware 5.17 | 045E:0B00 (bcdDevice 0x0517) | IOHID "Controller", Microsoft, transport USB, usage page 1 / usage 5, max input report 18 |
| Synthetic legacy pad | 045E:028E, version 0 | IOHID "GamePad-1", max input report 14 |
| Elgato Stream Deck + XL | 0FD9:00C6 | USB, vendor device |

### 2.1 What "GamePad-1" 045E:028E is

- It is **not** the third-party 360Controller driver. No such kext or system extension is installed; only the Elgato
  and Little Snitch extensions are.
- **It is Apple's synthetic legacy pad.** It is published by `AppleGCSyntheticDevice` from
  `/System/Library/Extensions/AppleSyntheticGameController.kext` (IOClass `AppleGCResource`). The GameController stack
  creates it so that old IOHIDManager-only apps can see Xbox GIP pads, which are not HID.
  - It carries the Xbox 360 wired id 045E:028E with version 0.
  - Its report descriptor is 360-style, with **no paddles**.
- **The real pad is matched by `/System/Library/DriverExtensions/XboxGamepad.dext`,** IOUserClass `XboxEliteV2Gamepad`.
  - The dext publishes a vendor HID "Controller" device.
  - Report id 0x20 on that device is the raw GIP input (17 bytes).
- **The dext covers USB only:** 028E (360), 02EA (One S), **0B00 (Elite 2)**, 0B12 (Series X). **The Elite Series 1
  (02E3) is not in the list.**
- **Consequence:** a host backend must filter the synthetic device, otherwise the user sees two pads for one. SDL
  already filters it.

### 2.2 SDL3 3.4.18 probe

| Configuration | Result |
|:--|:--|
| Default hints, no video subsystem | 0 joysticks. The MFi backend needs a running NSApplication. |
| `SDL_INIT_VIDEO`, MFi / GameController path | "Xbox One Wireless Controller", **045E:02E0**: a guessed id, because GameController hides VID/PID. GUID `0500a54b…6d04` (bus 0x05 = Bluetooth, also guessed; driver byte 'm'). **No paddles.** SF Symbols names are present (`a.circle`, `logo.xbox`, `lb.rectangle.roundedbottom`). |
| `SDL_JOYSTICK_MFI=0`, HIDAPI path | **Correct identity.** "Xbox One Elite 2 Controller", 045E:0B00, version 0517, GUID `0300aa4e5e040000000b000017056800`, path `DevSrvsID:…`. Mapping includes `paddle1:b11,paddle2:b13,paddle3:b12,paddle4:b14`. Rumble, trigger rumble and RGB LED reported as capabilities; serial readable. |

- SDL's own comment in `SDL_hidapi_xboxone.c` (`IsSupportedDevice`) says that, on wired macOS, HIDAPI gets "a shortened
  version of the real report and you can't write output reports". The reported rumble capability probably does not work
  there.
- **Paddle presses** were tested later the same day: §2.3a (USB, truncated) and §2.3b (Bluetooth, full).

### 2.3 GameController.framework dump (`gc.m`), USB, firmware 5.17

| Property | Value |
|:--|:--|
| Profile class | `GCXboxGamepad` |
| `paddleButton1`-`4` | **nil** |
| `buttonShare` | nil (the Elite 2 has no Share button) |
| `vendorName` | "Controller" |
| `physicalInputProfile` | standard elements only |
| Haptics localities | Left / Right Handle, Left / Right Trigger: **impulse triggers are reachable on macOS** |
| Battery | 0.00, state -1 (unknown) over USB |
| Light | nil |

**Conclusion:** on this setup Apple's API does not expose the Elite paddles over USB, and it hides VID/PID. Apple's
documentation lists the paddles (<https://developer.apple.com/documentation/gamecontroller/gcxboxgamepad>) but does not
say when they are nil. Over Bluetooth GameController does not report them either (§2.3c); raw IOHID over Bluetooth
does (§2.3b).

### 2.3a Live paddle test, USB, raw IOHID and SDL3 HIDAPI at once

Recorded 2026-10-17 with `hiddump.c` (IOHIDManager on 045E:0B00, every input report) and `sdlev.c` (SDL3,
`SDL_JOYSTICK_MFI=0`) running together, while the owner pressed P1-P4 on two onboard profiles (starting on profile 1),
both triggers, and markers. Fixtures and the byte layout:
[testdata/controllers/hid-captures/xbox-elite2/macos-usb-dext/](../../../testdata/controllers/hid-captures/xbox-elite2/macos-usb-dext/README.md).

| Observation | Evidence |
|:--|:--|
| The only HID endpoint is the dext's "Controller" (usage page 1, usage 5, max input 18). | `added dev0 'Controller' usagePage=1 usage=5 maxIn=18` |
| **Every report is 18 bytes, but its GIP length byte says 0x2f (47).** The dext passes the first 14 payload bytes (buttons, triggers, sticks) and cuts the rest: the paddle byte and the profile byte of the firmware 5.17 report never reach user space. | all 1242 reports: `20 00 <seq> 2f ...`, `len=18` |
| No paddle event from SDL either; each paddle press arrived as the face button the active onboard profile assigns (A, B, X, Y in the session). | `session-2026-10-17-sdl3-hidapi.txt` |
| The profile switch itself is not visible. | no report byte changes at the switch |
| Triggers are 10-bit (0x3ff full). | bytes 6-9 |

**Conclusion for macOS over USB:** neither GameController nor raw IOHID nor SDL can tell a paddle from the button it
mirrors, and the onboard profile cannot be read. This matches SDL's own comment that on macOS wired, HIDAPI gets "a
shortened version of the real report and you can't write output reports" (`SDL_hidapi_xboxone.c`, IsSupportedDevice),
so the GIP 0x4d command that enables the separate unmapped-paddle packet on firmware 5.13+ cannot be sent either.

Consequences (recorded in [TODO.md](TODO.md)):

1. On macOS, paddles as distinct controls need **Bluetooth** (BLE 045E:0B22: paddles in the Consumer "Assign
   Selection" usage, profile in "Order Movie", per SDL), or a user-space USB driver that claims the device from the
   dext (out of scope). The Bluetooth test is the next measurement.
2. Over USB on macOS the dialog says so: "Paddles over USB on macOS arrive as the buttons the controller's profile
   assigns. Connect by Bluetooth to use them separately." Paddle-based defaults (the debug layer key on P4) fall back to
   the `view+menu` chord there.
3. D15 ("read the raw bits where the OS gives them") holds on Windows (GameInput), Linux (xpad, hidraw) and macOS
   Bluetooth; on macOS USB only the hint is possible.

### 2.3b Live paddle test, Bluetooth LE (045E:0B22)

Recorded 2026-10-17 with the same two probes (`hiddump 180 0b22`), two sessions: free presses, then P1-P4 in order on
each of the profiles 0-3. Fixtures and the full 20-byte layout from the report descriptor:
[testdata/controllers/hid-captures/xbox-elite2/macos-ble/](../../../testdata/controllers/hid-captures/xbox-elite2/macos-ble/README.md).

| Observation | Evidence |
|:--|:--|
| One HID endpoint, input report 0x01 of 20 bytes, readable by plain IOHIDManager (no dext in the way). | `maxIn=20`, 464-byte descriptor |
| `[17]` (Consumer 0x85, Order Movie) is the **onboard profile**, 0-3. | 0 → 1 → 2 → 3 → 0 at each press of the profile button |
| `[19]` low nibble (Consumer 0x81, Assign Selection) is the **paddle bitfield**: 0x01 P1 upper right, 0x02 P2 lower right, 0x04 P3 upper left, 0x08 P4 lower left. | in-order session |
| **The paddle bits are reported on every profile**, not only on profile 0. On profiles 1-3 the same report also carries the button the profile assigns (P1 → B, P2 → Y, P3 → A or nothing, P4 → X in the owner's profiles). | `[14]` changes in the same report as `[19]` |
| SDL3 zeroes the paddles when the profile byte is non-zero and reports only the mirrored face button. | `session-2026-10-17-profiles-sdl3-hidapi.txt` |

**Conclusion.** Over Bluetooth on macOS our own HID driver gets more than SDL: paddle identity and the profile on
every profile. D15 is fully achievable here without asking the user to switch to profile 0. The driver must then
**undo the mirror**: on a non-zero profile, a face-button edge that arrives in the same report as a paddle edge is
attributed to the paddle and removed from the face button. The mirror table per profile is learned from these
coincidences (three consistent ones fix an entry; the onboard mapping is not readable over HID) and stored per device
and profile. While the paddle is held, the mirrored button stays suppressed, and a real press of that face button
during the hold is lost: the dialog states this in Advanced. Profile 0 needs none of it. On by default, can be turned off in Advanced (D19).

### 2.3c GameController over Bluetooth and USB

Recorded 2026-10-17 with `probes/gcev.m` (a `pressedChangedHandler` on every element of `physicalInputProfile`) next
to `hiddump` in a second process; fixtures in the two folders above.

| | Bluetooth LE | USB (dext) |
|:--|:--|:--|
| `paddleButton1-4`, `buttonShare` | nil | nil |
| P1-P4 on profile 0 | **no event** (raw HID shows all four) | no event |
| P1-P4 on profiles 1-3 | the mirrored face button only | the mirrored face button only |
| Battery | 0.00, state -1 (unknown); `system_profiler` says 100 % | 0.00, state -1 |
| Haptics localities | handles, triggers, all | handles, triggers, all |
| Vendor name | "Xbox Wireless Controller" | "Controller" |
| Raw IOHID input at the same time | yes | yes |

**Conclusion.** For the Elite 2 on macOS, GameController adds nothing to input: every control it reports is also in
the raw HID report, and the paddles it never reports. It stays the path for **outputs** (haptics with trigger
localities) after the D18 match, as designed. Input comes from IOHID. The battery must come from elsewhere: the
Bluetooth battery service (as `system_profiler` shows it) or a field of the HID report (`[18]` reads 0x05 at 100 %;
unconfirmed).

### 2.4 Building the probes

```bash
cd docs/inprogress/2026-10-17-controller-manager/probes
OUT=../../../../scratch/controller-probes; mkdir -p "$OUT"
clang probe.c  -o "$OUT/probe"  $(pkg-config --cflags --libs sdl3)   # brew install sdl3
clang probe2.c -o "$OUT/probe2" $(pkg-config --cflags --libs sdl3)   # SDL_INIT_VIDEO: MFi path
SDL_JOYSTICK_MFI=0 "$OUT/probe2"                                     # HIDAPI path
clang -fobjc-arc gc.m -o "$OUT/gc" -framework GameController -framework AppKit -framework Foundation
"$OUT/gc"
clang hiddump.c -o "$OUT/hiddump" -framework IOKit -framework CoreFoundation   # raw IOHID reports, 045E:0B00
clang -fobjc-arc gcev.m -o "$OUT/gcev" -framework Foundation -framework AppKit -framework GameController  # GC button events
clang sdlev.c   -o "$OUT/sdlev"   $(pkg-config --cflags --libs sdl3)           # SDL3 gamepad events
clang dialsteps.c -o "$OUT/dialsteps" -framework IOKit -framework CoreFoundation # Surface Dial detents, read back
SDL_JOYSTICK_MFI=0 "$OUT/sdlev" 120 & "$OUT/hiddump" 120
```

## 3. Backends per OS

### 3.1 macOS

| API | Gives | Missing |
|:--|:--|:--|
| **GameController.framework** | <ul><li>`GCController`, `GCXboxGamepad` (paddleButton1-4, buttonShare), `GCDualSenseGamepad` (adaptive triggers, touchpad)</li><li>`GCPhysicalInputProfile` with `sfSymbolsName` / `localizedName` per element</li><li>`GCDeviceHaptics` (CoreHaptics, incl. trigger localities), `GCDeviceLight`, `GCDeviceBattery`</li><li>`shouldMonitorBackgroundEvents`</li><li>MFi and Bluetooth pads</li></ul> | <ul><li>Needs an NSApplication run loop</li><li>Hides VID/PID</li><li>No Elite paddles over USB or Bluetooth (§2.3c)</li><li>Elite battery unknown</li></ul> |
| **IOKit HID** (`IOHIDManager`) | <ul><li>Real VID/PID / version / serial / location</li><li>Every HID pad (DualSense, Switch Pro, DB9 adapters)</li><li>For Xbox wired pads, the dext's vendor 0x20 pipe (raw GIP) and the synthetic 360 device</li></ul> | <ul><li>We parse reports ourselves</li><li>Output reports to Xbox pads are limited (SDL comment above)</li></ul> |

SDL merges MFi, HIDAPI and IOKit internally.

What the emulator builds on them (2026-10-23, [haptics.md §9](haptics.md#9-backends)): IOHID is the primary; the
PlayStation pads, the Switch Pro controller and the Joy-Cons (alone or a pair as one pad) get their own drivers on it
(`hid/drivers/sonypad`, `switchpro`: the
full reports after the Bluetooth handshake, the native rumble, light bar, player LEDs and battery, SDL's and
hid-playstation's byte layouts); GameController is the companion that adds CoreHaptics and `GCDeviceLight` where no
output report exists, and gives the input only of a pad IOHID does not see (a `gc:` device, silent while an IOHID
device of its name waits for the match, promoted to its IOHID id once both endpoints are matched, its profiles
following through the store's alias, [architecture.md §5](architecture.md#5-device-identity-and-merging)).

### 3.2 Windows

| API | Gives | Missing |
|:--|:--|:--|
| **XInput** | 4 slots, 14 buttons, 2 triggers, 2 sticks | <ul><li>No paddles, no guide button, no trigger rumble</li><li>No VID/PID</li></ul> |
| **Windows.Gaming.Input** | `GamepadButtons.Paddle1-4` (since 1607), trigger rumble | <ul><li>Undocumented whether paddles report while an on-device mapping is active</li><li>reWASD says they must be unmapped in Xbox Accessories first</li></ul> Source: <https://learn.microsoft.com/en-us/uwp/api/windows.gaming.input.gamepadbuttons> |
| **GameInput** | <ul><li>NuGet redistributable Microsoft.GameInput 3.x</li><li>Paddles since 3.3, DualSense gyro, VID/PID</li></ul> | <ul><li>Licence "NOASSERTION" on GitHub, so not clearly open source</li><li>Needs the runtime</li></ul> Sources: <https://www.nuget.org/packages/microsoft.gameinput>, <https://github.com/microsoftconnect/GameInput> |
| **Raw Input / HID** | DS4, DualSense, generic pads and joysticks, DB9 adapters | Xbox pads expose a "fake" HID endpoint, which SDL ignores |

There is a user report of Elite 2 paddles missing on Windows under SDL:
<https://discourse.libsdl.org/t/elite-controller-2-paddles-under-windows/53199>.

### 3.3 Linux

- **evdev** (`/dev/input/event*`):
  - uaccess ACLs through udev, so logind sessions read game pads without root.
  - xpad has reported Elite paddles since kernel 6.1, as `BTN_TRIGGER_HAPPY5-8` over USB
    (<https://www.phoronix.com/news/Linux-6.1-XPad>). It enables the extra report on firmware 5.11+ and suppresses the
    paddles while a custom profile is active.
- **hidraw:**
  - Needs udev rules (e.g. Steam's `60-steam-input.rules`); SDL's HIDAPI uses hidraw when it is allowed.
  - Over Bluetooth, the paddles are a usage "Assign Selection" bitfield (KEY_UNKNOWN) until the HID-BPF fix
    (<https://lkml.rescloud.iu.edu/2404.1/04303.html>).
- xpadneo is an alternative Bluetooth driver, and SDL maps it automatically.

### 3.4 Qt

- QtGamepad was not ported to Qt 6 and is not part of it. Homebrew Qt 6.12 has no such module.
- There is only the community QtGamepadLegacy port and a 2025 "resurrect" thread
  (<https://lists.qt-project.org/pipermail/development/2025-May/046338.html>).
- Do not depend on it.

### 3.5 SDL3

- **Gamepad API:**
  - Buttons and axes, including `SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1`, `LEFT_PADDLE1`, `RIGHT_PADDLE2`, `LEFT_PADDLE2`,
    `MISC1`-`MISC6` (MISC1 = Series X Share) and `TOUCHPAD`. Source: `SDL3/SDL_gamepad.h:171-181`.
  - Hotplug events, sensors.
  - `SDL_RumbleGamepad`, `SDL_RumbleGamepadTriggers`, `SDL_SetGamepadLED`, `SDL_GetGamepadPowerInfo`.
  - `SDL_GetGamepadAppleSFSymbolsNameForButton`.
- **Hints:** `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`, `_MFI`, `_HIDAPI_XBOX_ONE`, `_GAMEINPUT`, `_WGI`, `_RAWINPUT`,
  `_THREAD`.
- **GUID layout** (`SDL_CreateJoystickGUID`, `src/joystick/SDL_joystick.c`; 16 bytes, 16-bit little-endian fields):
  - bus (0x03 USB, 0x05 Bluetooth, 0xFF virtual);
  - CRC16 of "vendor product" name;
  - VID, 0;
  - PID, 0;
  - version;
  - byte 14 is the driver signature: 'h' HIDAPI, 'x' XInput, 'w' WGI, 'g' GameInput, 'r' Raw Input, 'm' MFi, 'v' virtual;
  - byte 15 is driver data.
  - Mapping strings may carry `crc:` and `platform:`.
- **Mapping database:** SDL_GameControllerDB (<https://github.com/mdqinc/SDL_GameControllerDB>), about 2291 lines,
  zlib. Line form: `GUID,name,a:b0,…,platform:X,`.
- **Licence and version:**
  - zlib.
  - 3.4.x is current (3.4.18 bugfix; 3.4.0 added Switch 2 and Steam Controller).
  - SDL2 is maintenance-only, and on most distros it is now sdl2-compat on top of SDL3.
- **Elite paddle handling** is in the HIDAPI Xbox One driver, `SDL_hidapi_xboxone.c` around line 1120.

## 4. Xbox Elite Series 2

### 4.1 Ids

| Variant | VID:PID |
|:--|:--|
| Series 2, USB | 045E:0B00 (usb.ids: "Xbox Elite Series 2 Controller (model 1797)") |
| Series 2, Bluetooth classic (firmware 4.x) | 045E:0B05 |
| Series 2, BLE (firmware 5.x) | 045E:0B22 |
| Xbox Wireless Adapter v1 / v2 | 045E:02E6 / 045E:02FE. The pad speaks GIP: XBOXGIP on Windows, the out-of-tree xone driver on Linux. |
| Series 1 | 045E:02E3. USB only, 4 paddles, 2 profile slots (switch), no remapping UI on the pad, not in Apple's dext. |

Source: SDL `src/joystick/usb_ids.h`.

### 4.2 Report layouts (from SDL `SDL_hidapi_xboxone.c`, around L1120-1140)

| Pad / firmware | Report size | Paddles byte | Profile byte |
|:--|:--|:--|:--|
| Elite Series 1 | 29 B | data[28] (mode bit 0x10) | data[28] & 0x10 |
| Series 2, firmware 4.x | 34 B | data[14] | data[15] |
| Series 2, firmware 5.x | 46 B | data[18] | data[19] |
| Series 2, firmware **5.17+** (this pad) | **47 B** | data[14] | data[20] |

**Bit assignments:**
- Series 2 paddle bits: P1 0x01, P2 0x02, P3 0x04, P4 0x08. Series 1 paddles are always reported.
- SDL numbering: RIGHT_PADDLE1 = P1 (upper right), RIGHT_PADDLE2 = P2, LEFT_PADDLE1 = P3, LEFT_PADDLE2 = P4. Our
  vocabulary uses `paddle.p1`..`paddle.p4` in the pad's own numbering
  ([architecture.md §6](architecture.md#6-the-canonical-control-vocabulary)).

**Firmware 5.13+:** the unmapped paddle state comes in a separate 17-byte packet, which SDL enables with GIP command 0x4d.

**Bluetooth:** paddles come in the Consumer "Assign Selection" usage and the profile in "Order Movie". SDL handles both.

### 4.3 Profiles and other hardware

**Profiles:**
- Profile 0 is the default (LED off); there are 3 onboard profiles.
- If the active profile maps a paddle on the pad, SDL zeroes the paddle bits ("respect that the paddles are being used
  for other controls"). The paddle then shows up only as the button it emulates. xpad does the same.
- The profile is readable from the report byte, but SDL has no API for it.

**Hardware features:**

| Feature | What software sees |
|:--|:--|
| Profile button and LED | The profile byte above; the LED shows the profile. |
| Hair-trigger locks (3 stops) | The trigger saturates early. Calibrate the full value per session; do not assume 32767. |
| Stick tension | Mechanical, invisible to software. |
| Impulse triggers | WGI and GameInput on Windows, GameController haptics on macOS (seen in the dump), SDL HIDAPI on Linux and over Bluetooth. Not through XInput. |
| Share button | Not on the Elite 2; on the Series X\|S pad (0B12 / 0B13) as SDL MISC1. |
| Battery | Not from GameController on either transport (state -1, §2.3c); `system_profiler` shows it over Bluetooth, so the source is the Bluetooth stack or a field of the BLE report (`[18]`, unconfirmed). |

Battery sources per OS, as built (2026-10-22, from the SDK headers and kernel sources, not measured with a pad):
GameController's `GCDeviceBattery` (`batteryLevel` 0..1, `batteryState` -1 unknown, 0 discharging, 1 charging,
2 full); on Windows `XInputGetBatteryInformation` (xinput1_4 / 1_3: type wired, alkaline, NiMH, unknown; four levels),
while the GameInput v3 ABI dropped v0's `GetBatteryState`, so a pad GameInput serves has no battery there; on Linux
the HID device's `power_supply` class (hid-playstation, hid-sony, hid-nintendo with `capacity_level` only, xpadneo;
xpad over USB registers none), whose reads through hid-input may ask the device a feature report and so run on the
backend thread.

### 4.4 Ideas for the debugger and TTD (input for [debug-and-ttd.md](debug-and-ttd.md))

- **Hold a paddle to scrub.** RT = reverse scrub speed, nonlinear from 1 frame/s to 200×; LT = forward. Release
  returns to the live run or stays at the paused point.
- **Stepping with the paddle held.** The D-pad and bumpers step by instruction, scanline or frame, backward and
  forward. RS X scrubs finely inside a frame (T-states).
- **Other paddles:**
  - set a bookmark;
  - previous / next bookmark;
  - reverse-continue to the last write of the watched address.
- **Profile button.** If it can be read, it switches between the game, debug and TTD layers, and the LED gives free
  feedback. But the hardware owns the profile, so a software layer key (a held paddle) is more robust.
- **Rumble.** A pulse on a breakpoint hit or at the start of the TTD history; trigger rumble scaled to the scrub speed.
- **These controls are host commands, never journaled machine input.**

## 5. Other controllers

From SDL `controller_list.h` / `usb_ids.h` and gamecontrollerdb unless noted.

| Family | VID:PID |
|:--|:--|
| Xbox 360 wired | 045E:028E |
| Xbox One | 045E:02D1, 02DD |
| Xbox One S | 045E:02EA (Bluetooth 02E0 / 02FD, BLE 0B20) |
| Xbox Series X\|S | 045E:0B12 (BLE 0B13) |
| Xbox Adaptive | 045E:0B0A, 0B0C, 0B21 |
| Windows XBOXGIP alias | 045E:02FF |
| DualShock 4 | 054C:05C4, 09CC (dongle 0BA0) |
| DualSense | 054C:0CE6 |
| DualSense Edge | 054C:0DF2 (2 back buttons → SDL paddles, plus Fn) |
| Switch Pro | 057E:2009 |
| Joy-Con L / R / grip | 057E:2006, 2007, 200E |
| NSO SNES / N64 / Mega Drive | 057E:2017, 2019, 201E |
| 8BitDo (VID 2DC8; the PID changes with the mode switch: X-input, D-input, Switch, Bluetooth, Steam) | SF30 Pro 6000 / 6100, SN30 Pro 6001 / 6101, Pro 2 6003 / 6006, Pro 3 6009, Ultimate 2 6012, Ultimate 2C 301B / 310A, Ultimate 3 202F, Ultimate wired 3106 / 2002 |
| Sega Mega Drive Mini 6B / Genesis Mini 3B | 0CA3:0024 / 0CA3:0025 |
| Atari VCS Classic / Modern | 3250:1001 / 3250:1002 |
| Atari CX Controller | 0E6A:4011 |
| THEC64 Joystick / THEGamepad (Retro Games Ltd) | 1C59:0024 / 1C59:0026 (gamecontrollerdb). VID 1C59 is not in usb.ids. |
| THEJOYSTICK (white, bundled with THESPECTRUM), THESPECTRUM pads | **unverified**: likely 1C59:00xx, read it from ioreg when one is plugged in |
| Retro-bit adapters | 1292:4745 (Genesis), 4643 (NES), 5346 (SNES) |
| Mayflash DB9 adapter | 0E8F:3013 |
| Ultimate Atari Fight Stick | 16C0:05E1 (shared V-USB VID) |
| DragonRise generic | 0079:0006, 0079:0011 ("Retro Controller" / Retrolink SNES) |
| USB SNES clone | 0810:E501 |
| Arcade sticks | Qanba 0F30:1100-1116, Mayflash F300 0079:1830, Mad Catz 0738:8180, Venom 0079:181A/B |

- No standard "Kempston USB" device id was found.
- DB9-to-USB adapters are generic HID and often report the D-pad as axes or hats. They need the mapping wizard, which
  writes the result in the gamecontrollerdb line format.
- 8BitDo is "one device, several PIDs": the database needs a family with a PID list
  ([device-database.md](device-database.md)).

## 6. Stream Deck

- **Ids:** Elgato VID 0FD9. usb.ids knows 0060, 0063, 006C (XL) and 006D; the + XL here is **00C6**, not in usb.ids.
  SDL does not treat it as a joystick.
- **Ways to drive it:**
  - Elgato's plugin SDK (WebSocket) or plain hotkeys, calling the existing WebAPI. Elgato's app owns the device.
  - Raw hidapi with the Elgato app quit, as python-elgato-streamdeck (MIT) does: key images, dials, touch strip.
- **Recommendation:** a macro surface for TTD bookmarks and debugger actions, through the WebAPI as a companion client
  ([2026-10-16-companion-client](../2026-10-16-companion-client/README.md)), not a new driver.

## 7. Latency, threading, determinism

**Pumping:**
- Without SDL video, SDL needs `SDL_PumpEvents` / `SDL_UpdateGamepads`, from a QTimer (≥ 250 Hz) or from an input
  thread. SDL3 allows joystick use off the main thread; `SDL_HINT_JOYSTICK_THREAD` applies on Windows.
- GameController and the IOKit paths deliver on the main run loop. Qt's Cocoa loop pumps it; verify in the app.
- Keep the latest state in an atomic snapshot.

**Determinism:**
- Sample once at frame start, diff against the previous frame, and submit only the changes through
  `SubmitLiveInput`.
- That matches how ZX games poll and keeps TTD and RZX stable.
- A sub-frame mode, still journaled, is an option.

**Analog to digital:**
- Stick dead zone about 0.3 with hysteresis; 8-way quantization; a trigger threshold.
- Calibrate the hair-trigger stops.

**Latency budget:**
- USB polling runs at 125-1000 Hz: Xbox GIP about 4-8 ms, DualSense USB 250 Hz, Bluetooth 4-12 ms plus jitter.
- Frame sampling adds up to 20 ms at 50 Hz; sampling just before frame start (late latch) reduces it.

**Rumble ideas:**
- Beeper / AY low-frequency envelope → the low-frequency motor.
- Tape loading tone → a light pulse.
- Border flash or a virtual "rumble" port for homebrew → a pulse.
- Trigger rumble scaled to the TTD scrub speed.
- Rate-limit to about 30 Hz. Stop on pause, focus loss and seek. Never journal rumble.

## 8. What the research recommended

The research recommended **SDL3 as the primary backend**, gamepad and sensor subsystems only. Reasons:

- one library for three OSes;
- gamecontrollerdb;
- HIDAPI drivers for PlayStation, Switch, Xbox over Bluetooth and 8BitDo;
- paddles, trigger rumble and LEDs;
- zlib licence.

It added two notes:

- **On macOS, choose `SDL_HINT_JOYSTICK_MFI` explicitly.**
  - MFi gives SF Symbols and working haptics, but no Elite paddles and a wrong VID/PID.
  - HIDAPI gives the correct identity and the paddles, but output writes probably fail. (Measured later, §2.3a-c: over
    USB on macOS HIDAPI gets no paddles either, and over Bluetooth it drops them on profiles 1-3.)
  - So: MFi by default, plus an advanced "prefer raw HID (paddles)" switch.
- **On Windows, keep SDL's GameInput / WGI / Raw Input / XInput order.**

## 9. Why native first (D2, D6)

The owner chose a different order: the most capable native API of each OS behind our own abstraction, then our own
raw HID backend, and SDL3 later as a separate build option. The measurements support this order.

1. **On macOS the most capable source is IOKit HID with our own GIP driver, not GameController.**
   - GameController hides VID/PID and never reports the Elite paddles, over USB or Bluetooth (§2.3c).
   - IOKit sees the real id; over Bluetooth its plain HID report carries the paddles and the profile on every profile
     (§2.3b). Over USB the dext truncates the GIP report, so neither arrives (§2.3a).
   - GameController is still needed for what IOKit cannot do: haptics (including trigger localities), light,
     SF Symbols names, MFi and Bluetooth pads without a HID endpoint. Not for the Elite battery: it reports it as
     unknown (§2.3c).
   - That is exactly the primary + companion pairing in [architecture.md §4](architecture.md#4-backends-per-os).
2. **SDL would not remove that work on macOS.** Its MFi / HIDAPI split is the same trade-off, made per process with
   one hint. We make it per device and per capability instead (rule 1 of architecture §4).
3. **Windows and Linux.**
   - GameInput and evdev already expose paddles, VID/PID and trigger rumble.
   - Raw Input and hidraw cover generic HID. Our own HID drivers on top of them give one parser for all three OSes.
4. **The main client is Qt.** The library has no Qt and no SDL in its interface, so a future SDL client can use the
   SDL3 backend without the Qt client depending on SDL.
5. **SDL3 stays useful even before it is a backend.**
   - Its HIDAPI driver sources (zlib) are the reference for our drivers' report layouts: Xbox GIP and Elite offsets,
     DualSense, Switch Pro.
   - gamecontrollerdb remains the database for generic pads ([device-database.md](device-database.md)).
   - The SDL3 backend (TODO) is the fallback for whatever our drivers do not cover.

Costs we accept: our own report parsers, per-firmware tests of the Elite layout, and a merge of GameController and
IOKit endpoints on macOS (decided: D18).

## 10. Uncertainties

- Elite paddles on macOS are measured (§2.3a-c); on Windows (GameInput with an onboard profile) and Linux (xpad,
  hidraw) they are not.
- The Elite battery source on macOS (Bluetooth stack or BLE report `[18]`).
- Wired macOS output reports (rumble) through the dext pipe: SDL's comment says writes fail; not measured.
- Windows WGI / GameInput paddle behaviour with an on-device mapping is undocumented.
- THEJOYSTICK, THESPECTRUM pads and Stream Deck + XL 00C6 ids are unconfirmed.
- The Steam Input glyph licence is from memory. The GameInput licence is unclear; the owner treats it as a Microsoft
  runtime prerequisite of the installer, like the MSVC runtime (D17).
- Matching a `GCController` to its IOKit device has no public API; the correlation approach is a design proposal
  ([architecture.md §5](architecture.md#5-device-identity-and-merging)).
