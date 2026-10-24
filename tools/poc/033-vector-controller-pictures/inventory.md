# 033 — the controllers one can connect today

Every game controller a user can plug in or pair on macOS, Windows and Linux in 2026, with its ids, the input path
the app uses on each host, its family in `data/controllers/devices.json` and its picture. Light guns are out of scope
(owner). Sources: SDL3 `controller_list.h`, `usb_ids.h`, `SDL_hidapi_*.c` (git, 2026-10-23),
SDL_GameControllerDB (`gamecontrollerdb.txt`, the Mac OS X lines decode the HID button order, see
[§4](#4-how-the-button-maps-were-read)), Apple's `XboxGamepad.dext` `Info.plist` on this Mac (macOS 15.6), the Linux
kernel's `drivers/hid` and `drivers/input/joystick/xpad.c`. Facts marked *unverified* were not checked on a device.

Contents:

- [1. Paths per host](#1-paths-per-host)
- [2. The devices](#2-the-devices)
- [3. Gaps per host](#3-gaps-per-host)
- [4. How the button maps were read](#4-how-the-button-maps-were-read)
- [5. Left out](#5-left-out)

## 1. Paths per host

| Host | Paths the app reads | Notes |
|:--|:--|:--|
| macOS | IOKit HID (`iohidbackend.cpp`): `HidPad` (descriptor-driven, the family's layout and `hid` map), `XboxGipPad` (the dext's raw GIP report), `SurfaceDial`; GameController.framework only for outputs | Apple's `XboxGamepad.dext` claims only 045E:028E, 0B00, 02EA, 0B12 over USB; every other Xbox-protocol USB device (third-party, 360 receiver, One 2013, Elite 1, Adaptive) has no driver on macOS |
| Windows | GameInput (Xbox pads, paddles) and XInput; Raw Input HID for everything else (`rawinputbackend.cpp`) | Xbox pads over Bluetooth are HID with the `xinputhid` filter on top |
| Linux | evdev (`evdevbackend.cpp`) with the kernel driver's key codes; hidraw for vendor reports and outputs | hidraw needs a udev rule (`uaccess`, e.g. Steam's `60-steam-input.rules`); evdev pads get `uaccess` through systemd's `ID_INPUT_JOYSTICK` |

Linux drivers named below: `xpad` (Xbox 360 / One / Series USB and the 360 wireless receiver, third-party Xbox
pads), `hid-microsoft` (Xbox One S / Series over Bluetooth: rumble), `hid-sony` (DualShock 3, some licensed PS3
pads), `hid-playstation` (DualShock 4 since 6.2, DualSense, DualSense Edge), `hid-nintendo` (Switch Pro, Joy-Con,
the NSO pads since 6.3 *unverified*), `hid-steam` (Steam Controller, Steam Deck), `hid-google-stadiaff` (Stadia
rumble), `hid-wiimote` (Wii Remote, Wii U Pro), `hid-generic` (the rest).

## 2. The devices

Ids are VID:PID in hex; "BT" is Bluetooth classic or LE. Family = the `devices.json` id; picture = the image set in
`data/controllers/images/` (a shared set is named in italics). macOS column: **HidPad** = read through IOKit HID by
`HidPad` with the family's map, **GIP** = `XboxGipPad` over the dext, **none** = the host has no path.

### 2.1 Microsoft

| Device | USB | BT | macOS | Windows | Linux | Family | Picture |
|:--|:--|:--|:--|:--|:--|:--|:--|
| Xbox 360 wired | 045E:028E | — | GIP (the dext's 360 report) *unverified*; Apple's synthetic 028E v0000 ignored | XInput | xpad | `xbox-360` | `xbox-360` |
| Xbox 360 Wireless Receiver (+ pads) | 045E:0719, 0291, 02A9 | — | none (no driver) | XInput | xpad | `xbox-360-wireless` | *xbox-360* |
| Xbox One (2013, no BT) | 045E:02D1, 02DD | — | none (dext does not match) | GameInput / XInput | xpad | `xbox-one` | `xbox-one` |
| Xbox One S | 045E:02EA | 02E0, 02FD, BLE 0B20 | USB GIP, BT HidPad | GameInput / XInput | xpad; BT hid-microsoft | `xbox-one-s` | *xbox-one* |
| Xbox Series X\|S | 045E:0B12 | BLE 0B13 | USB GIP, BT HidPad | GameInput / XInput | xpad; BT hid-microsoft | `xbox-series` | `xbox-series` |
| Xbox Elite (Series 1, no BT) | 045E:02E3 | — | none (dext does not match) | GameInput (paddles) / XInput | xpad | `xbox-elite-1` | *xbox-elite-2* |
| Xbox Elite Series 2 | 045E:0B00 | 0B05, BLE 0B22 | USB GIP, BT HidPad (paddles) | GameInput (paddles) | xpad (paddles since 6.1) | `xbox-elite-2` | `xbox-elite-2` |
| Xbox Adaptive Controller | 045E:0B0A | 0B0C, BLE 0B21 | USB none (dext does not match), BT HidPad | GameInput / XInput | xpad; BT hid-generic | `xbox-adaptive` | `xbox-adaptive` |
| Xbox Wireless Adapter | 045E:02E6, 02FE | — | none | the pads behind it report their own ids | xone (out of tree) | — | — |
| Windows XBOXGIP alias | 045E:02FF | — | — | GameInput | — | `xbox-gip` | *generic-xbox-like* |

### 2.2 Sony

| Device | USB | BT | macOS | Windows | Linux | Family | Picture |
|:--|:--|:--|:--|:--|:--|:--|:--|
| DualShock 3 | 054C:0268 | 0268 (pairing needs the host address written over USB) | HidPad over USB after the enable feature report 0xF4 (sent on open, *unverified*); BT pairing not possible from macOS | Raw Input after DsHidMini (no Microsoft driver) | hid-sony (USB and BT) | `dualshock-3` | `dualshock-3` |
| DualShock 4 v1 / v2 / USB dongle | 054C:05C4, 09CC, 0BA0 | 05C4, 09CC | HidPad (USB report 1; BT simple report 1) | Raw Input | hid-playstation (6.2+), hid-sony before | `dualshock-4` | `dualshock-4` |
| DualSense | 054C:0CE6 | 0CE6 | HidPad | Raw Input | hid-playstation | `dualsense` | `dualsense` |
| DualSense Edge | 054C:0DF2 | 0DF2 | HidPad; Fn L / Fn R / back buttons as raw bits of USB report 1 | Raw Input | hid-playstation (back buttons as BTN_TRIGGER_HAPPY *unverified*) | `dualsense-edge` | `dualsense-edge` |
| Licensed PS3 / PS4 / PS5 pads (Hori, Nacon, Razer, Mad Catz, PowerA, …) | 90 ids from SDL | some | HidPad, PlayStation layout | Raw Input | hid-generic (a few hid-sony) | `playstation-compatible` | *generic-playstation-like* |

### 2.3 Nintendo

| Device | USB | BT | macOS | Windows | Linux | Family | Picture |
|:--|:--|:--|:--|:--|:--|:--|:--|
| Switch Pro Controller | 057E:2009 | 2009 | SwitchProPad + SwitchProtocol (ctl-hid): the USB handshake 0x80 02 / 03 / 02 / 04, SPI calibration, full report 0x30, HD rumble | Raw Input (BT; USB after the handshake) | hid-nintendo | `switch-pro` | `switch-pro` |
| Joy-Con (L) / (R), single | — | 057E:2006 / 2007 | HidPad, simple report 0x3F: the stick is the hat (`"hat": "lstick"`), held sideways | Raw Input | hid-nintendo (joycond pairs them, 2008) | `joy-con-l` / `joy-con-r` | `joy-con-l` / `joy-con-r` |
| Joy-Con charging grip / joined pair | 057E:200E / 2008 | — / (2008 is joycond's virtual pair) | SwitchProPad: the grip's halves by the device info, the pair joined (ctl-hid) | Raw Input after the handshake | hid-nintendo | `joy-con-grip` | `joy-con-grip` |
| NSO NES (pair) | — | 057E:2006 / 2007 (they report as Joy-Cons; SDL tells them by the device type in the SPI) | SwitchProPad, layout `Nes` by the product name ("NES Controller" / "HVC Controller", SDL); A east, B south; no motors | Raw Input | hid-nintendo | the Joy-Con families | `nso-nes` (picked by the user) |
| NSO SNES / N64 / Sega Genesis | 057E:2017 / 2019 / 201E (USB = charging) | same | SwitchProPad, layouts `Snes` / `N64` / `Genesis` (bits after hid-nintendo and SDL; N64 Z `lt`, C buttons `misc.1-4`; Genesis C `rb`, Mode `view`); only the N64 pad rumbles | Raw Input | hid-nintendo (6.3+ *unverified*) | `nso-snes` / `nso-n64` / `nso-megadrive` | `nso-snes` / `nso-n64` / `nso-genesis` |
| Switch 2 Pro / Joy-Con 2 / NSO GameCube | 057E:2069 / 2066 2067 2068 / 2073 | BLE, proprietary | none: USB needs the bulk init on the vendor interface (an IOUSBLib claim, proposal §3.1); BT not supported by anyone | WinUSB + init | userspace (SDL with libusb) | `switch-2-pro` / `joy-con-2` / `nso-gamecube-2` | *switch-pro* (no orthographic view of the Switch 2 Pro found) / *joy-con-grip* / *gamecube* |
| GameCube Controller Adapter (Wii U / Switch) | 057E:0337 | — | GameCubeAdapter over IOHID: output 0x13, then report 0x21; each port its own device `<id>:p1..p4` (*unverified*: that IOHID passes 0x13 to the interrupt OUT pipe; no rumble yet) | WinUSB (Zadig) | userspace (no in-kernel driver) | `gamecube-adapter` | `gamecube` |
| Mayflash GameCube adapter (PC mode) | 0079:1843, 1844, 1846 | — | HidPad (1844 map from the Mac line) | Raw Input | hid-generic | `gamecube-mayflash` | *gamecube* |
| Wii Remote / Wii Remote Plus / Wii U Pro | — | 057E:0306 / 0330 | recognised, no input: needs the Wiimote protocol (proposal §3.1) | Raw Input + Wiimote protocol | hid-wiimote | `wii-remote` | *generic-nintendo-like* |
| Switch-licensed wired pads (Hori, PowerA, PDP) | 0F0D:0092 00C1 00F0, 0E6F:0180-018B, 20D6:A711-A718 | — | HidPad with the shared map | Raw Input | hid-generic | `switch-compatible` | *generic-nintendo-like* |
| Switch-Pro-protocol third-party (Hori wireless, PDP Afterglow / REALMz) | 0F0D:00F6, 0E6F:0186, 018C | yes | as Switch Pro | Raw Input | hid-nintendo *unverified* | `switch-pro-compatible` | *generic-nintendo-like* |

### 2.4 Others

| Device | USB | BT | macOS | Windows | Linux | Family | Picture |
|:--|:--|:--|:--|:--|:--|:--|:--|
| Steam Controller (2015) | 28DE:1102, receiver 1142, 1101 | BLE 1105, 1106 | SteamPad + SteamProtocol: lizard mode off (feature 0x81, 0x8E, 0x87), the vendor state report; the receiver's slot appears when a pad connects; BLE 1105 / 1106 not read (gap) | Raw Input, vendor reports | hid-steam | `steam-controller` | `steam-controller` |
| Steam Controller (2026, "Triton") | 28DE:1302, dongles 1304 (Proteus), 1305 (Nereid) | BLE 1303 | SteamPad + SteamProtocol: lizard mode off (feature 1 `87 03 09`) every 3 s, reports 0x42 / 0x45 / 0x47; dongles by the wireless report 0x79; BLE not read | Raw Input, vendor reports | hid-steam *unverified* | `steam-controller-2026` | *steam-controller* (only a perspective photo exists) |
| HORIPAD for Steam | 0F0D:01AB | 0196 | HidPad, Xbox layout *unverified* | Raw Input | hid-generic | `hori-steam` | *generic-xbox-like* |
| Google Stadia Controller (BT firmware) | 18D1:9400 | 9400 | HidPad, map from the Mac line; triggers are Brake / Accelerator | Raw Input | hid-generic + hid-google-stadiaff | `stadia` | `stadia` |
| Amazon Luna Controller | 1949:0419 | 0171:0419 | HidPad; USB and BT maps differ (USB: triggers on Rx / Ry; BT: guide, view and the Luna key are consumer usages, *unverified*) | Raw Input | hid-generic | `luna` | `luna` |
| Logitech F310 / F510 / F710 | 046D:C216 / C218 / C219 (D mode) | — | D mode HidPad with the map; X mode none (Xbox protocol, no driver) | X mode XInput, D mode Raw Input | X mode xpad, D mode hid-generic | `logitech-f310` / `-f510` / `-f710` | `logitech-f310` |
| Logitech F310 / F510 / F710, X mode | 046D:C21D / C21E / C21F | — | none | XInput | xpad | same families (`modes`) | `logitech-f310` |
| Third-party Xbox 360 / One pads (PowerA, PDP, Hori, Razer, Mad Catz, Nacon, GameSir, …) | 104 / 114 ids from SDL | some | none over USB (no driver); BT models HidPad by their own ids | XInput / GameInput | xpad | `xbox-360-compatible` / `xbox-one-compatible` | *generic-xbox-like* |
| 8BitDo pads | VID 2DC8, PID per mode | yes | HidPad in D-input / Switch / Bluetooth modes; X-input mode none | XInput (X mode), Raw Input | xpad (X mode), hid-generic, hid-nintendo (Switch mode) | `8bitdo-*` | per model, see [§2.5](#25-8bitdo) |
| Arcade sticks, PlayStation mode (Hori RAP / FC4, Qanba, Razer Panthera, Mad Catz TE, Victrix, Brook) | 28 ids | — | HidPad, PlayStation layout | Raw Input | hid-generic | `arcade-stick-playstation` | `arcade-stick` |
| Arcade sticks, Xbox mode | 30 ids | — | none (no driver) | XInput / GameInput | xpad | `arcade-stick-xbox` | *arcade-stick* |
| Fight pads (Hori Fighting Commander, Mad Catz, Razer, PDP) | 11 PS-mode / 10 Xbox-mode ids | — | PS mode HidPad; Xbox mode none | Raw Input / XInput | hid-generic / xpad | `fight-pad-playstation` / `fight-pad-xbox` | `fight-pad` |
| Arcade sticks, Switch mode (Hori RAP for Switch, PowerA Fusion) | 0F0D:00AA, 20D6:A715 | — | HidPad, map from the Mac line | Raw Input | hid-generic | `arcade-stick-switch` | *arcade-stick* |
| USB SNES-style pads (NEXT, iBuffalo, Retrolink, "Retro Controller") | 0810:E501, 0583:2060, 0079:0011 | — | HidPad; the cross on X / Y (`dpadAxes`); 0583:2060 and 0079:0011 split by the version | Raw Input | hid-generic | `generic-snes-usb` | *nso-snes* |
| Retro-bit adapters / pads | 1292:4745, 4643, 464B, 5346 | — | HidPad, generic | Raw Input | hid-generic | `retrobit-genesis` / `-nes` / `-snes` | *nso-genesis* / *nso-nes* / *nso-snes* |
| Mayflash Saturn adapter | 0E8F:3010 *unverified* | — | HidPad, generic | Raw Input | hid-generic | `mayflash-saturn` | `saturn-pad` |
| Hyperkin N64 adapter | 2E24:0BFF *unverified* | — | HidPad, generic | Raw Input | hid-generic | `hyperkin-n64` | *nso-n64* |
| Mega Drive Mini / Genesis Mini pads, Atari VCS, Atari CX, THEC64, THEGamepad, Mayflash DB9 | as before (device-database.md §11) | — | HidPad | Raw Input | hid-generic | unchanged | DB9 stick, *nso-genesis*, *nso-snes* |
| DragonRise generic PCB | 0079:0006 | — | HidPad, generic | Raw Input | hid-generic (hid-dr for rumble) | `dragonrise-generic` | *generic-unknown* |

A device whose ids are in no family still gets a picture through its name: the templates match "xbox", "dualshock",
"dualsense", "8bitdo arcade", "8bitdo", "joy-con", "pro controller", "fight stick", "arcade", "saturn", "genesis", "mega drive",
"snes", "gamecube", "n64", "db9", "joystick", "retro".

### 2.5 8BitDo

VID 2DC8; the PID changes with the mode switch (`modes` names it). X-input mode is the Xbox protocol: no path on
macOS, XInput on Windows, xpad on Linux. D-input, Switch (the pad reports as 057E:2009 there, so it is the
`switch-pro` family) and Bluetooth modes are HID on all three hosts.

| Model | PIDs | Family | Picture |
|:--|:--|:--|:--|
| SF30 Pro / SN30 Pro / SN30 Pro+ | 6000 6100 / 6001 6101 / 6002 6102 | `8bitdo-sf30-pro` / `-sn30-pro` / `-sn30-pro-plus` | own sets |
| SN30 / SF30 / M30 / Zero 2 | 2840 5103 AB20 / 3001 2830 AB21 / 5001 5006 5101 / 3230 9018 | `8bitdo-sn30` / `-sf30` / `-m30` / `-zero-2` | own sets |
| Pro 2 / Pro 3 | 6003 6006 3010 6103 / 6009 | `8bitdo-pro-2` | `8bitdo-pro-2` |
| Ultimate (1), Ultimate 3, Ultimate wired | 202F 3106 2002 3011 3012 3013 6007 | `8bitdo-ultimate` | `8bitdo-ultimate` |
| Ultimate 2 Wireless | 6012 (BT / Steam mode), 310B (X-input) | `8bitdo-ultimate-2` | `8bitdo-ultimate-2` |
| Ultimate 2C | 301B 301C 301D 310A | `8bitdo-ultimate-2c` | *8bitdo-ultimate-2* (same shell, no profile key, no light rings) |
| Lite 2 | 5112 | `8bitdo-lite-2` | `8bitdo-lite-2` |
| Micro | 9020 (a Bluetooth keyboard mode reports as 05AC:0004 *unverified*) | `8bitdo-micro` | `8bitdo-micro` |
| 64 / N64 (2019) | 1930 / 2869 9002 3830 | `8bitdo-64` | `8bitdo-64` |
| F30 Arcade Stick | 2810 AB11, 1235:AB11, 0810:0009 | `8bitdo-f30-arcade` | *arcade-stick* |
| Arcade Stick (2020) | no id in SDL or the DB: Switch mode is 057E:2009, X-input 045E:028E *unverified* | by name (`8bitdo arcade`) | `8bitdo-arcade-stick` |

## 3. Gaps per host

### 3.1 macOS (this branch fixes the small ones)

| Gap | Status |
|:--|:--|
| Families without a button map read their buttons in the layout's guessed order | **fixed**: `devices.json` `hid` maps (`buttons`, `rxRyTriggers`, `dpadAxes`, `hat`, `consumer`, `bits`), per family or per match; `HidLayoutFor(DeviceMatch)` |
| The cross reported on X / Y (SNES-style pads) came out as a stick | **fixed**: `dpadAxes` |
| A single Joy-Con's stick is a hat in its simple report | **fixed**: `"hat": "lstick"` emits `lstick.x` / `.y` |
| DualSense Edge Fn keys and back buttons | **fixed** over USB: raw `bits` of report 1 (payload byte 9, bits 4-7, SDL); over BT the Edge sends the simple report 1 without them until the extended report 0x31 is enabled (proposal below) |
| DualShock 3 over USB sends nothing until the feature report 0xF4 `42 0C 00 00` | **fixed** on open (*unverified*, no device here) |
| DualShock 4 / DualSense over BT: only the simple report 1 (no touchpad, no gyro, no Edge extras) | proposal: request the extended report (read feature 0x05 / 0x09, then report 0x11 / 0x31), as SDL `SDL_hidapi_ps4.c` / `ps5.c` |
| Switch Pro / Joy-Con grip / NSO pads over USB: no reports before the 0x80 handshake | **fixed** (ctl-hid SwitchProtocol: handshake, mode 0x30, calibration; the NSO pads' layouts here) |
| Steam Controller: lizard mode only | **fixed** (`steampad.cpp`, *unverified*, no device here); left: rumble (haptic pulses), BLE 1105 / 1106 / 1303, gyro |
| GameCube adapter 057E:0337 | **fixed** over IOHID (`gamecubeadapter.cpp`, *unverified*); if IOHID does not deliver 0x13, the fallback is an IOUSBLib interrupt pipe as for Switch 2; left: rumble 0x11 |
| Switch 2 controllers over USB | proposal: IOKit's IOUSBLib (already linked, no libusb): find the vendor interface (class 0xFF) of 057E:2066 / 2067 / 2069 / 2073, `USBInterfaceOpen`, write SDL's ten 0x91 commands to its bulk OUT pipe, read each reply, read the stick calibration from flash 0x13080 / 0x130C0 (0x1FC040 user); the state then arrives on the HID interface as report 0x09 (64 bytes, buttons bytes 5-8, sticks 12-bit at 11-16) through IOHID. No device here to verify the claim works beside the HID driver |
| Wii Remote / Wii U Pro | proposal: IOHID first (macOS pairs a Wiimote as HID *unverified*; Dolphin uses IOBluetooth instead: output 0x11 LEDs, 0x12 report mode 0x31 / 0x35, extensions by register writes 0x16 at 0xA400F0 / FB); IOBluetooth L2CAP (PSM 0x11 / 0x13) only if IOHID refuses the outputs: it needs the Bluetooth entitlement and the pairing PIN trick, not clean |
| Xbox-protocol USB devices other than the four the dext claims (360 receiver, One 2013, Elite 1, Adaptive USB, third-party Xbox pads and sticks, Logitech X mode, 8BitDo X-input mode) | without a dext: an IOUSBLib user-space claim of the vendor interface (class 0xFF, subclass 0x5D protocol 0x01 for 360, subclass 0x47 protocol 0xD0 for GIP) is possible when no kernel driver holds it (no entitlement outside the sandbox, the app is not sandboxed); then the 360 report (20 bytes) or the GIP handshake (0x05 0x20 power on) on its interrupt pipes. Not done: no device to verify; proposal |
| Nintendo layout sends `lt.click` / `rt.click` for ZL / ZR, so the picture's `lt` / `rt` hotspots do not light | **fixed**: the layout sends `lt` / `rt` (full value); a binding on `lt.click` / `rt.click` still reads them (InputRouter alias) |

### 3.2 Windows (for ctl-win)

| Gap | Note |
|:--|:--|
| The `hid` maps of `devices.json` | `rawinputbackend.cpp` still calls `HidLayoutFor(match.layout)`; switching to `HidLayoutFor(match)` gives the Raw Input path every map of this inventory |
| DualShock 3 | no Microsoft driver: needs DsHidMini (or the 0xF4 enable once a HID handle exists); BT needs BthPS3 |
| Switch Pro / grip / NSO over USB | the 0x80 handshake as on macOS |
| GameCube adapter, Switch 2 controllers | WinUSB (Zadig) + init: not Raw Input |
| Steam Controllers | vendor reports as on macOS |
| DualShock 4 / DualSense over BT | the extended report as on macOS |
| Wii Remote / Wii U Pro | the Wiimote protocol as on macOS |
| Xbox Elite paddles, Xbox share button | GameInput (XInput has neither) |

### 3.3 Linux (for ctl-linux)

| Gap | Note |
|:--|:--|
| The `hid` maps | `evdevbackend.cpp` calls `HidLayoutFor(match.layout)`; the maps matter on the hidraw path; on evdev the kernel driver already names the keys (`BTN_SOUTH` …), so the maps apply only where a device is `hid-generic` |
| DualSense Edge back buttons, Elite paddles | the driver's `BTN_TRIGGER_HAPPY*` codes to `paddle.*` |
| GameCube adapter, Switch 2 controllers | no in-kernel driver: libusb on the USB device (udev rule) |
| Steam Controllers | hid-steam gives evdev only while Steam is not running; hidraw otherwise |
| hidraw access | a udev rule; without it, outputs (rumble, LEDs) and vendor reports are unavailable |

## 4. How the button maps were read

SDL's IOKit backend lists a device's buttons sorted by usage, so in a Mac OS X line of SDL_GameControllerDB `bN` is
HID button usage N + 1, and the axes are in usage order (X, Y, Z, Rx, Ry, Rz, then Simulation Accelerator / Brake).
The same HID descriptor is what Raw Input and hidraw read, so one `hid.buttons` list serves the three hosts. SDL's
`a` / `b` / `x` / `y` are positions (bottom / right / left / top), which are our `south` / `east` / `west` / `north`.
A Mac line that differs by version for one VID:PID (0583:2060 iBuffalo v0000 / Retro Controller v0100, 0079:0011 v0105
/ v0106) became a version match ([device-database.md §3](../../../docs/inprogress/2026-10-17-controller-manager/device-database.md#3-lookup-order)).
The licensed families are generated from `controller_list.h` by the controller type (`XBox360Controller`,
`XBoxOneController`, `PS3/4/5Controller`, `SwitchInputOnlyController`, `SwitchProController`), with sticks split by
the device's comment; guitars, drums, wheels and flight sticks are left out.

## 5. Left out

| Device | Why |
|:--|:--|
| Light guns (GunCon, Sinden, AimTrak) | owner: never |
| Guitars, drums, turntables, dance mats, wheels, flight sticks | not game pads; no use for a ZX emulator's player controls |
| Wii Classic Controller, Nunchuk | extensions of the Wii Remote: with its protocol (proposal) |
| GameCube controllers on a Wii (no adapter) | not connectable to a computer without the adapter |
| PS2 / PS1 pads | only through generic USB adapters (the DragonRise / generic families) |
| Ouya, OnLive, NVIDIA Shield controller (2015), Razer Serval, Moga | discontinued; the Shield and Moga still pair as generic HID (name templates) |
| Stadia controller in its Wi-Fi firmware | needs Google's Bluetooth update (2023-2025); the BT firmware is listed |
| Xbox 360 "Play and Charge" cable | charging only, no data |
| Steam Deck | not an external controller (28DE:1205 is its built-in pad) |
