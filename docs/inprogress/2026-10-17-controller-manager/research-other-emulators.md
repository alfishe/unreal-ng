# Research: how other emulators map host controllers

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Research |
| **Feeds** | [architecture.md](architecture.md), [mapping-and-profiles.md](mapping-and-profiles.md), [ui-ux.md](ui-ux.md), [device-database.md](device-database.md) |

This is a read-only survey of the emulator source trees in `/Volumes/TB4-4Tb/Projects/emulators`. Unless a path starts
with `/`, it is relative to `/Volumes/TB4-4Tb/Projects/emulators/github/`. Line numbers come from grep on the
checkouts as of 2026-10-17. "Missing" means grep found nothing; it does not prove the feature is absent.

**Not available on disk:**
- **Fuse.** `find /Volumes/TB4-4Tb -maxdepth 4 -iname '*fuse*'` finds only `fuse-utils` (scl2trd), `jnext/test/fuse`
  (Z80 test vectors) and unrelated libfuse trees.
- **eve-emu.** It is an FT812 GPU library with no input code.
- **CSpectPlugins.** Plugins only stub `iPlugin.KeyPressed(int _id)` (`CSpectPlugins/i2C_Sample/Class1.cs:97`).
  `eGlobal.JoyEnable` shows up only through `strings` on `Plugin.dll`.

## Contents

- [1. ZX Spectrum emulators](#1-zx-spectrum-emulators)
- [2. Reference architectures](#2-reference-architectures)
- [3. Comparison](#3-comparison)
- [4. Ideas to take](#4-ideas-to-take)
- [5. Pitfalls](#5-pitfalls)
- [6. What we take](#6-what-we-take)

## 1. ZX Spectrum emulators

### 1.1 jnext (ZX Next; SDL2 + Qt): the closest model for Next ports

**Backend:**
- SDL2 GameController; when SDL has no mapping it falls back to the raw SDL_Joystick (`jnext/src/input/gamepad_host.cpp:50` `open_device`, `:79-95`).
- Hotplug comes from JOYDEVICEADDED/REMOVED only. CONTROLLER* events are ignored on purpose, so one press is not counted twice (`:118-152`).
- Devices are identified by instance id. GUID and VID/PID are not used.

**Ports:** two connectors (`joystick_dispatcher.h:95`, `NUM_CONNECTORS=2`); a new pad takes the first free one.

**Default MD6 mapping** (`joystick_dispatcher.cpp:58-90`):
- A→B (fire 1), B→C (fire 2), X→A, and both Y and Start→START. The alias is needed because only 4 button bits reach the classic ports.
- Back→MODE, LB→X, RB→Z. These three are visible only through NR #B2.
- Raw pads map by position, buttons 0..7 → B, C, A, START, MODE, X, Y, Z (`:324-362`).
- Hats and both sticks drive the D-pad, with `AXIS_THRESHOLD=16384` (50 %, `.h:100`). Triggers are dropped.

**Next machine side:**
- NR #05 decoder: `input/joystick.h:25-35` (Sinclair 2, Kempston 1 #1F, Cursor, Sinclair 1, Kempston 2 #37, MD L/R, I/O mode).
- NR #0B pin-7 UART mux: `input/iomode.h`.
- Joystick-to-keyboard folding, including the user keymap set through NR #28/#29/#2B: `membrane_stick.h`.
- The dispatcher is resynchronized after a rewind or a state load (`:107`).

**Config:** QSettings `~/.jnext/jnext.conf`, key `joy1_source=sdl|cursor`. Only one connector can take "Cursor keys + Space" as its source (`gui/preferences_dialog.cpp:155-173`).

**Missing:** a rebinding UI, autofire, profiles, hotkeys on the pad.

### 1.2 ZEsarUX: the widest set of actions a pad button can trigger

**Backend:**
- Linux `/dev/input/js0` through JSIOC (`zesarux/src/realjoystick_linux.c:44`), or the raw SDL2 joystick (`video/scrsdl2.c:1749-1777, 1886-1960`).
- The device is chosen by index or path only (`--realjoystickindex`, `start.c:7784`).
- No hotplug; there is a manual `realjoystick_reopen_driver` (`realjoystick.h:250`). One pad.

**17 emulated types:** Kempston, Sinclair 1/2, Cursor, Cursor+Shift, OPQA-Space, Fuller, Zebra, Mikrogen, ZXpand, SAM, CPC, MSX, SVI, PCW Cascade/DKTronics (`joystick.h:27-44`). There is also a "cheap Kempston" bus-noise option (`joystick.c:78`).

**Three mapping tables** (`realjoystick.h:122-143`):
- button/axis → event: the directions, Fire 1-4, and the host events EscMenu, Quickload, OSDKeyboard, Enter, JoySelect (cycles the emulated type), Aux1-4;
- button → Spectrum key (the QAOP path);
- button → any F-key action (`--joystickaction`).
- Syntax: an axis is `+N/-N`, a button is `N` (`utils.c:4447`).
- Defaults: `realjoystick.c:249-330`.

**Config** is written as CLI switches in `.zesaruxrc` (`utils.c:5850-5880`):

```
--joystickevent -1 Up
--joystickkeybt 3 32
```

**Per-game profiles:**
- A `<file>.config` next to the tape or snapshot is loaded automatically (`autoselectoptions.c:355-359`).
- An MD5 table holds per-game remaps and steering-wheel `.postconfig` presets (`realjoystick.c:1157`).

**Other settings:**
- Autofire at 50/N Hz (`timer.c:1177`), saved in the snapshot (`snap/snap.c:1072`).
- Dead zone "autocalibrate", default 16383 (`realjoystick.c:115`).
- The keyboard fire key can be Home, Alt, Ctrl, Shift or Tab (`joystick.h:127-136`).

**UI:** a text menu with event lists, "redefine" (waits for a press), a live Test window with axis bars, and autocalibrate (`menu/menu_items_settings.c:12876-14059`).

### 1.3 Xpeccy and xpeccy-plus (Qt): best per-entry target model, best device identity

**Xpeccy backend:**
- Raw SDL2 joystick, polled every 20 ms (`Xpeccy/src/xcore/gamepad.cpp:540-548`). `GPBACKEND_QT` exists but is unused.
- Identity by name (`config.cpp:203`). On hotplug it reopens the pad with the same name (`gamepad.cpp:551-575`).
- Two pads (gpada / gpadb), each with its own map and dead zone (default 8192, `:271`).

**Map file**, one line per entry, `src:target[:repeat]` (`gamepad.cpp:117-243`):
- Sources: `A<n>±` axis, `B<n>` button, `H<n>U/D/L/R` hat.
- Targets:
  - `K<QKeySequence>`, a host key, so a pad button can fire an emulator hotkey;
  - `J<U/D/L/R/F/2/3/4>`, Kempston;
  - `B..`, the second Kempston;
  - `M<U/D/L/R/buttons/wheel>`, the Kempston mouse.
- `:rpt` gives autofire for that one entry.
- Stick to mouse moves at `abs(state/4096)` (`emulwin.cpp:309`).
- Maps are per machine, through the profile keys `gamepad.map=` and `gamepad2.map=` (`xcore/profiles.cpp:502-507`).

**Emulated types:** in practice only Kempston. A Sinclair enum exists but is unused (`libxpeccy/input/input.h:15-19`).

**UI:**
- `ui/opt_gpadwidget.ui`: device combo, map-file combo with add and delete, a dead-zone slider, a table of entries.
- `ui/padbinder.ui`: "Push to set" capture, then a radio choice of PC key (QKeySequenceEdit), Kempston or Mouse, and an autorepeat slider.

**xpeccy-plus adds:**
- SDL GameController with named sources `Cdpup`, `Xleftx-` (`xpeccy-plus/src/xcore/gamepad.cpp:130-175`), and `gamecontrollerdb.txt` loaded from the config directory (`:336-349`).
- **Device id `xPadId`:** the GUID plus a tie-break for two identical pads (serial, then OS path, then n-th with the same GUID), stored as `guid|S:serial|name` (`gamepad.h:61-89`, `gamepad.cpp:381-399`). It also reads `SDL_JoystickGetDevicePlayerIndex`.
- Hotplug triggers a full `rescan()`.
- A bound host key now fires the real hotkey (rewind, pause).
- A binder dialog with plain names ("Right trigger").
- The default `config/kempston.pad`:

  ```
  A1-:JU
  B1:JF
  Cdpup:JU
  Xleftx-:JL
  Cb:JF
  Ca:J2
  ```

### 1.4 unreal-speccy and zx-evo-unreal (Win32, DirectInput)

**Backend:**
- DirectInput enumeration (`unreal-speccy/dx.cpp:2502`; `zx-evo-unreal/Unreal/dx.cpp:1675`).
- The device is picked by `DIPROP_JOYSTICKID`, matched against `Joy=` (`dx.cpp:2076-2094`).
- Only X/Y are read; no POV hats. Range ±1000, fixed dead zone 2000. No hotplug.

**Model:**
- The pad produces virtual keys `JLEFT..JFIRE, JB0..JB31` (`input.cpp:246-264`).
- Those keys are bound in `[ZX.KEYS.<layout>]` to ZX keys or to Kempston/Fuller pseudo-keys (`vars.cpp:332-346`). So Sinclair, Cursor and QAOP are only key bindings.

**Config** (`x32/unreal.ini:79-89, 988-1000`):

```
KJoystick=1
FJoystick=0
Fire=KFIRE
FireRate=1
Joy=0
JLEFT=KLEFT ... JFIRE=KFIRE
```

- The ini says joystick keys "can't be bound to commands" (`:671`).
- There is an autofire hotkey. `JoyMouse` maps the screen as a 3x3 Kempston grid (`dx.cpp:1233-1250`).
- zx-evo-unreal is the same code without Fuller.
- unreal-ng's per-model configs still carry these keys and ignore them ([architecture.md §12](architecture.md#12-persistence)).

### 1.5 UnrealSpeccyP (SDL2, portable)

- **Backend:** SDL2 GameController, index 0 only. On hotplug it opens a pad only if none is open (`platform/sdl2/sdl2.cpp:149, 218-226`).
- **Fixed mapping** (`platform/sdl2/sdl2_joystick.cpp:91-121`):
  - The D-pad and both sticks (threshold 0.5) give the directions.
  - A = fire, B = up, X = 'e', Y = space.
  - LB = reset, RB = pause, and the fixed chord Back+Start = quit.
- **One global type:** cursorenter / cursor / kempston / sinclair2 / qaopm / qaopspace (`options_common.cpp:224-238`, translated in `speccy_handler.cpp:230-288`). Keyboard arrows + Ctrl go through the same abstraction.
- **Other:** an on-screen touch joystick (`platform/touch_ui/tui_joystick.cpp`). XML config `unreal_speccy_portable.xml`.

### 1.6 Spectral (C, single file style)

- **Backend:** the `3rd_gamepad.h` library: XInput on Windows, linux/joystick plus a udev hotplug monitor, nothing on macOS. Pad 0 only (`sys_gamepad.h:93-109`).
- **The pad drives the keyboard:**
  - A 16-bit mask; pad bit i presses host key `ZX_PAD[i]` (`app.c:763-785`).
  - The default is `{LEFT,RIGHT,UP,DOWN,TAB,'Z',UP...}` (`zx.h:153`), and the keyboard then drives the joystick.
  - Fixed dead zone 0.15.
- **Several interfaces at once:** the `ZX_JOYSTICK` bitmask, default cursor|kempston|fuller (`zx.h:125`).
  - Presets OPQAM, KLAZM and others (`zx.h:510-545`).
  - Cursor is forced on in BASIC.
- **Autofire:** 0-3 (`app.c:788`).
- **UI:** an in-app dialog, "Gamepad to joystick/keyboard bindings": press host keys in order, Backspace deletes, a long press clears (`app.c:2726-2850`).
- **Per-game `aticatac.joy`:** designed in comments only (`app.c:128-141`).

### 1.7 Zero-Emulator (C#, Managed DirectInput)

- Opens a pad by InstanceGuid and remembers it by name (`Zero-Emulator/ZiggyWin/.../JoystickController.cs:32-58`, `InputSystem.cs:34-48`). No hotplug.
- Two pads, each set to emulate NONE / KEMPSTON / SINCLAIR1/2 / CURSOR (`Ziggy/Speccy/zxSpectrum.cs:325-332`). Threshold ±100.
- "Key2Joy" maps arrows + Shift.
- The remap dialog `Tools/JoystickRemap.cs` is commented out (`Options.cs:695-703`).

### 1.8 ZX-M8XXX (browser JS)

- **Backend:** W3C Gamepad API, first pad only (`ZX-M8XXX/core/input.js:266`). Hotplug through connect/disconnect events (`ui/gamepad.js:44-50`).
- **Defaults:** any of buttons 0-3 is fire. Extended Kempston bits 5-7 come from X/LB, B/RB and Start (`input.js:306-336`).
- **Calibration wizard:** "press input for Up…" detects an edge against a baseline (an axis moving by more than 0.4, or a button edge).
  - Stored as `{type:'axis',index,direction,threshold}` in localStorage (`ui/gamepad.js:105-216`).
- **Types:** kempston, sinclair1/2, cursor, custom (any ZX keys) (`core/joystick.js:53-58`).
- **Autofire profiles:** `{name,key,rate,hold}` (`ui/autofire.js`).
- **Per-project mapping** is saved in the project file (`ui/project-io.js:130,552`).

### 1.9 pico-spec (RP2040) and Murmulator

**pico-spec inputs:**
- TinyUSB HID with a **VID/PID quirk table**: DS4 054C:05C4/09CC, DualSense 054C:0CE6, F710 046D:C219 and others (`pico-spec/src/hid_app.cpp:131-207`).
- A custom XInput host driver (`src/xinput_host.c`).
- NES/SNES pads through PIO.

**pico-spec mapping:**
- Types: Cursor, Kempston, Sinclair 1/2, Fuller (#7F), Custom (`src/Config.h:46-52`).
- Extended Kempston bits 5-7 and a configurable Kempston port (`ESPectrum.cpp:1860-1871`).
- Remapping through `joydef[14]` (`keyboard.cpp:362-398`).
- `joy2cursor` lets the pad drive menus too (`hid_app.cpp:536-568`).

**Murmulator** (`Murmulator_rp2040`):
- Inputs: NES, I2C expander or Wii classic pads, probed at boot.
- `/murmulator.cfg`:

  ```
  ; 0 - Kempston ... 4 - QAOPM keys
  def_joy1_mode:0
  def_joy2_mode:0
  ```

- **Chord hotkeys:** Start = menu, Select+Up/Down = volume, Select+Left/Right = brightness (`main.c:1907-1924, 5181-5225`).
- **Right stick → Kempston mouse** with a ±7 dead zone; L/R are the mouse buttons (`main.c:489-494`).
- Fixed 50 ms autofire on X/Y.

### 1.10 Smaller ZX projects

**8BitAnalysers:**
- Hard-coded ImGui gamepad → `zx_joystick` mask (`8BitAnalysers/ZXSpectrum/Viewers/SpectrumViewer.cpp:551-566`).
- Types None, Kempston, Sinclair 1/2. No config.

**ZXMAK2:**
- DirectInput8 by `guidInstance`, plus a pseudo-device `"keyboard"` for the numpad (`src/ZXMAK2.Host.WinForms/Mdx/DirectJoystick.cs:19-111`).
- Kempston is a machine-XML device with `port`, `mask` and **`hostId`** attributes (`KempstonJoystick.cs:74-108`).
- Several devices with different hostIds give several players. The binding is per machine, in `MACHINES/*.VMZ`.

**zxsp:**
- Backend: macOS IOKit HID, up to 4 devices by index (`Source/OS/Mac/UsbJoystick.cpp`). The Linux backend is a stub.
- Many interfaces: Kempston, TC2048/TS2068 dual, IF2, Cursor/Protek, dk'tronics Dual, Fuller, TK85 (`Uni/isa_id.h:140-158`).
- Each port has a combo: no joystick / Keyboard / USB #n (`Qt/Inspector/JoyInsp.cpp:114-140`).
- **Per-game keyboard joystick matched by filename fnmatch pattern,** stored as `"UUDDLLRRFF"+pattern` (`Qt/Dialogs/ConfigureKeyboardJoystickDialog.cpp:143-245`).
- Optional on-screen joystick overlays.

**DeZog (zsim):**
- Polls `navigator.getGamepads()`; the n-th pad drives the n-th on-screen joystick (`DeZog/src/remotes/zsimulator/zsimwebview/joysticks.ts:22-63`).
- **`customJoy` describes each button as `{port, portMask, bit, lowActive}`** (`src/settings/settings.ts:186-292`, `customjoystick.ts`). That can model any odd interface (Zebra, ZX81 Battlestar):

  ```json
  "customJoy": { "left": { "port": "0xF7FE", ... } }
  ```

- Clickable on-screen pads in the simulator view.
- On the Next serial link the joystick ports become UART, so there is no joystick while a program runs (`zxnextserialremote.ts:191`).

## 2. Reference architectures

### 2.1 MAME: three clean layers

**Emulated side (ioport):**
- Drivers declare fields: `PORT_BIT(mask, IP_ACTIVE_LOW, IPT_JOYSTICK_UP) PORT_PLAYER(n) PORT_8WAY`.
  - Classes `ioport_field`, `ioport_port` and `analog_field` are in `mame/src/emu/ioport.h:557/758/809`.
- Default sequences per logical type come from `src/emu/inpttype.ipp:22`:
  - P1 = arrows, P2 = RFDG (`:139`), P3 = IKJL, P4 = numpad, P5-8 unbound.
- Each field has STANDARD, INCREMENT and DECREMENT sequences, so keys can drive analog axes.

**Host side:**
- An `input_code` packs device class, index, item class, modifier and item id. Its text form looks like `JOYCODE_1_BUTTON1` (`src/osd/interface/inputcode.h:34`).
- An `input_seq` is a boolean expression with OR and NOT (`inputseq.h:71-74`), e.g. `KEYCODE_TAB OR KEYCODE_1 KEYCODE_3`.

**Spectrum interfaces are slot devices:**
- `SPECTRUM_EXPANSION_SLOT(..., "kempjoy")` (`src/mame/sinclair/spectrum.cpp:771`), options intf2 / fuller / kempjoy / protek (`src/devices/bus/spectrum/exp.cpp:200-266`).
- `kempjoy.cpp:23-80` is active high, `& 0x1f`.
- `intf2.cpp:30-38` writes the keyboard half-rows #F7FE/#EFFE, active low, `PORT_PLAYER(2)`.
- `fuller.cpp` is active low.
- **Each interface declares its own logical inputs, and the user binds those.**

**OSD input modules:**
- SDL joystick, SDL GameController (`src/osd/modules/input/input_sdl.cpp:2591`) and SDL3; XInput, DInput, RawInput, a winhybrid XInput+DInput module, mac. No evdev.
- `assignmenthelper.cpp` makes sensible defaults, e.g. GUIDE→UI_MENU (`input_sdl.cpp:1614`).

**Device identity and hotplug:**
- Identity = SDL GUID + `-serial` (`input_sdl.cpp:2194-2212`); on hotplug, `find_reconnect_match(guid, serial)` (`:2244`).
- `<mapdevice device="substring-of-id" controller="JOYCODE_1"/>` pins a physical pad to a logical index (`src/emu/input.cpp:1342-1389`, `ioport.cpp:2254`).

**Analog handling:**
- Dead zone and saturation per device, with rescale (`src/emu/inputdev.cpp:480-520`).
- `joystick_map`: a 9x9 character grid that turns a stick into 4-way or 8-way digital (`:111-253`).
- Options `joystick_deadzone/saturation/threshold/contradictory` (`emuopts.h:133-144`).

**Config layering:**
- Order: a ctrlr file (`-ctrlr`, `ctrlrpath`) → `default.cfg` (global defaults per type) → `cfg/<system>.cfg` (per system; only differences are saved, `ioport.cpp:2755`).
- Example `mame/ctrlr/xarcade.cfg`:

  ```xml
  <remap origcode="KEYCODE_UP" newcode="KEYCODE_8PAD"/>
  <port type="UI_UP"><newseq type="standard">KEYCODE_UP OR KEYCODE_8PAD</newseq></port>
  ```

- Per-port entries are tagged, e.g. `<port tag=":exp:kempjoy:JOY" type="P1_JOYSTICK_UP" ...>`.

**UI:**
- "Input (general)" sets the global defaults; "Input (this machine)" is per system (`src/frontend/mame/ui/inputmap.h:37/91/107`).
- **A second press appends with OR. Clear empties a binding; clearing again restores the default** (`inputmap.cpp:475-647`).
- `analogipt.cpp` draws a live graph per axis; `inputdevices.cpp` is a live device inspector.

**Autofire:** a Lua plugin, `{port, mask, type, key, on_frames, off_frames}` per system, saved as JSON (`plugins/autofire/autofire_save.lua:11-45`).

### 2.2 RetroArch: autoconfig database and remap layering

**Joypad drivers:** sdl, udev, linuxraw, dinput, xinput, xinput_hybrid, hid, mfi, android and the console drivers (`RetroArch/input/drivers_joypad/`). Hotplug calls `input_autoconfigure_connect()` (`sdl_joypad.c:158, 464`).

**Autoconfig scoring:**
- VID+PID +30, exact name +20, `input_phys` ±10, plus `_alt1.._alt9` aliases in one file.
- Code: `tasks/task_autodetect.c:114`, the external scan at `:258`, built-ins in `input/input_autodetect_builtin.c:35`.
- Format (`tests-other/autoconf/TestpadD_alternative.cfg`):

  ```
  input_device = "Test joypad device D"
  input_vendor_id = 1
  input_product_id = 2
  input_device_display_name = "..."
  input_b_btn = "0"            # "h0up" hats, "+0" axes
  input_b_btn_label = "Cross"
  ```

**RetroPad abstraction:**
- 16 buttons + 2 sticks (`libretro.h:176/260/320`). Cores such as Fuse map RetroPad to Kempston/Sinclair through `SET_CONTROLLER_INFO` (`:1506`).
- Input descriptors `{port, device, index, id, description}` (`libretro.h:5962`, `runloop.c:2027-2164`) put **game-specific labels** ("Fire") in the menu.

**Remaps** (`configuration.c:4879`):
- Lookup order: game `<core>/<game>.cfg` → content directory `<dir>.cfg` → core `<core>.cfg`.
- Keys: `input_playerN_btn_X`, `input_libretro_device_pN`, `input_remap_port_pN` (port reassignment), `analog_dpad_mode`.

**Other features:**
- Turbo modes classic / toggle / single-button / hold, with `turbo_period` and `duty_cycle` (`input/input_defines.h:229`, `input_driver.c:1518-1553`).
- Hotkeys are gated by an `enable_hotkey` modifier button, with a block delay (`input_driver.c:601-618`).
- Radial dead zone with sensitivity (`:864-890`), and an axis threshold for axis-as-button (`:811`).

### 2.3 BizHawk: controller pictures, autofire per binding

**Bindings:**
- `ControllerDefinition` names buttons ("P1 Up"), with `CategoryLabels` for the UI tabs (`BizHawk/src/BizHawk.Emulation.Common/Base Implementations/ControllerDefinition.cs`).
- Config dictionaries `AllTrollers`, `AllTrollersAutoFire`, `AllTrollersAnalog` (`Config.cs:409-412`).
- A binding is a comma list: `"Keypad8, J1 POV1U, X1 DpadUp"`.
- `AnalogBind{Value, Mult, Deadzone}` per axis.

**Backend:**
- SDL2 only, with positional names X1/J1 and no GUID (`SDL2Gamepad.cs:243-266`).
- Sticks are also offered as digital "LStickUp".
- Input runs on its own thread, latched per frame.

**Autofire:** `(frame-start) % (On+Off) < On` (`controllers/AutofireController.cs:9-36`). Autohold lives in `StickyControllers.cs`.

**Config dialog:**
- A map from controller name to PNG (`config/ControllerConfig.cs:17-52`; `images/ControllerImages/`, which includes `ZXSpectrumKeyboards.png`).
- Tabs: Normal / Autofire / Analog / Feedback.

**ZXHawk:** `JoystickType {NULL, Kempston, SinclairLEFT, SinclairRIGHT, Cursor}` × 3 ports (`Computers/SinclairSpectrum/ZXSpectrum.Controllers.cs:131`). The category label shows the type, e.g. "J1 (Kempston)".

### 2.4 Mesen2: four mapping sets and a vector-drawn pad

- **Backends:** XInput + DInput, with XInput pads filtered out of DInput (`Mesen2/Windows/DirectInputManager.cpp:62`); libevdev on Linux; GCController on macOS.
- **Key codes:** one shared space ("Pad1 A", "Joy1 X+").
- **Mapping sets:** `KeyMappingSet {Mapping1..4, TurboSpeed}`, all four ORed (`Core/Shared/SettingTypes.h:245-295`).
- **Turbo:** `1 << (4 - speed)`, i.e. 3.75 / 7.5 / 15 / 30 Hz (`Core/NES/Input/NesController.h:49`).
- **Dead zone:** 0-4 (`SettingTypes.h:332`).
- **Presets:** WASD / Arrows / Xbox / PS4 (`UI/Config/KeyPresets.cs`).
- **Hotkeys:** combinations of up to 3 keys, pad buttons included.
- **UI:**
  - Tabs Set1..Set4 plus a preset menu (`UI/Windows/ControllerConfigWindow.axaml`).
  - **Each console view, e.g. `UI/Views/SnesControllerView.axaml`, draws the pad with vector shapes on a Canvas and puts the binding buttons at the physical spots.**
- **Hotplug:** GC notifications on macOS, a periodic rescan on Linux.

### 2.5 dosbox-x mapper: clickable keyboard drawing

- **Backend:** raw SDL joystick by index, no hotplug.
- **Mapper file:**

  ```
  [sdl2]
  key_esc "key 41"
  jbutton_0_0 "stick_0 button 0"
  jaxis_0_0- "stick_0 axis 0 0"
  jhat_0_0_0 "stick_0 hat 0 1"
  ```

  - Modifiers and hold are written inline: `"key 44 mod1 hold"` (`dosbox-x/src/gui/sdl_mapper.cpp:577-588`).
  - Saving is at `:4801`, the defaults at `:4652`.
  - Joystick types: 2axis / 4axis / fcs / ch (`:1787-2381`).
- **Analog:** radial dead zone 0.25 and a response curve (`src/dosbox.cpp:4094-4160`).
- **Autofire:** a boolean that toggles once per poll, with no rate (`:1960`).
- **UI:**
  - A full-screen drawing of the PC keyboard plus joystick and mouse panels (`CreateLayout`, `:3985`).
  - Click a target, then press a source; Add / Del / Next / Hold.
  - Handler events (hotkeys) can be bound to pad buttons.

### 2.6 Amiga emulators

**fs-uae (SDL3): a two-level database.**
- A per-device file maps physical inputs to universal names; `universal_gamepad.conf` maps universal names to functions per mode. Example `fs-uae/.attic/share/fs-uae/input/xbox_360_wired_controller_15_6_0_0_macos.conf`:

  ```
  [sdl]
  guid = 5e04...
  [default]
  include = universal_gamepad
  button_0 = south_button
  ```

  and `universal_gamepad.conf`:

  ```
  [amiga]
  south_button=1
  east_button=up
  right_trigger=autofire
  left_trigger=toggle_autofire
  start_button=menu_alt
  ```

  There are separate `[cd32]` and `[cpc]` sections.
- Ports: `joystick_port_N` holds a device name ("#2" for duplicates), plus `_mode` = joystick | mouse | cd32 gamepad, plus `_autofire`.
- The dead zone is hard-coded and marked FIXME (`od-fs/fsemu/fsemu-sdlinput.c:279`).

**WinUAE:**
- **Backends:** RawInput + DInput + XInput (`WinUAE/od-win32/dinput.cpp:497, 1516, 3395`). VID/PID is parsed from the device path (`:1249`); hotplug through WM_DEVICECHANGE.
- **Port modes:** mouse / djoy / gamepad / ajoy / cd32joy / lightpen (`cfgfile.cpp:209`).
- **Autofire:** none / normal / toggle / always / togglebutton (`:211`), with the rate in scanlines (default 600).
- **Keyboard layouts as virtual devices:** kb_np numpad, kb_ck cursor + RCtrl, kb_se WASD + LAlt, plus CD32 variants (`keyboard_win32.cpp:301-319`).
- A simple "GamePorts" mode and 4 custom input configurations; joymouse has its own dead zone.
- UI: a list table plus Test/Remap and Swap ports. A per-game profile is a per-game `.uae`.

**amiberry:**
- SDL3 with gamecontrollerdb.
- Lookup order: a RetroArch `.cfg` → `retroarch.cfg` → a native `.controller` → defaults (`amiberry/src/osdep/amiberry_input.cpp:1344-1404`).
- **It reads RetroArch autoconfig files directly** (`retroarch.cpp`).
- **Two layers through a hotkey button:** `joyportN_amiberry_custom_{none|hotkey}_button` (`:1457`).
- Mouse from the left stick; autofire Off / Slow / Medium / Fast.
- **ImGui mapper with embedded PNG front and back views and marker sprites over the picture** (`src/osdep/imgui/controller_map.cpp:234-289, 895-922`).

**vAmiga (macOS IOKit):**
- **Database lookup:** builds an SDL-style GUID from bus, VID, PID and version, then matches the bundled gamecontrollerdb in three passes: exactly, ignoring the version, ignoring the platform (`vAmiga/GUI/Input/DeviceDatabase.swift:10-170`). User edits go to UserDefaults JSON.
- **Hysteresis instead of a dead zone:** on at 0.45, off at 0.35 (`GUI/Input/GamePad.swift:248-265`).
- **Slots:** Mouse / Keyset1 / Keyset2 / Joystick1-4. `disconnectJoyKeys` keeps joystick keys away from the emulated keyboard.
- **Burst autofire:** a bullet count (default 3) and a delay (5 frames) (`Core/Peripherals/Joystick/Joystick.cpp:146-311`).
- **Device page:** HID properties, a live event readout, an editable mapping string and an "unknown device" warning.

### 2.7 NES / SMS

**fceux (Qt/SDL2):**
- **Profiles per GUID** in `~/.fceux/input/<GUID>/<profile>.txt`; the keyboard uses the pseudo-GUID "keyboard".
- One line per config:

  ```
  <GUID>,<name>,config:0,a:b1,b:b0,dpup:h0.1,turboA:kL,turboB:-a2
  ```

- Three configs (Pri / Alt1 / Alt2). `press:<hotkey>` / `release:<hotkey>` bind hotkeys to pad buttons (`fceux/src/drivers/Qt/input.cpp:940-1090`).
- An option sets what happens when a new device is plugged in. Turbo uses on/off frame patterns (`src/fceu.cpp:669`).
- UI: Port / Device / GUID labels, and profile Load / Save / New / Delete (`GamePadConf.cpp`).

**Nestopia:**
- FLTK frontend: raw SDL, player index per hotplug. Trigger axes are detected by their resting value (`nestopia/source/fltkui/inputmanager.cpp:411`). Bindings look like `j0b1`; turbo every N frames (2-9).
- Win32 frontend: DInput by guidInstance, dead zone 0-100, an autofire table.

**Gearsystem:**
- SDL2 GameController + gamecontrollerdb; two slots; name and GUID shown (`Gearsystem/platforms/desktop-shared/application.cpp:418-539`).
- Triggers act as virtual buttons (threshold 3000). A D-pad-or-stick switch with `STICK_DEAD_ZONE=8000`.
- **Every hotkey has a gamepad shortcut button, per player** (`InputGamepadShortcutsN`).
- ImGui "press a button" modals.

## 3. Comparison

| Emulator | Host backend | Device id | Profile format | Per game / machine | Autofire | Players | Pad → hotkey | Stick → mouse | Rebind UI | Hotplug |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| jnext | SDL2 GC + raw | instance id | QSettings | no | no | 2 (MD6) | no | no | source combo | yes |
| ZEsarUX | js0 / SDL2 raw | index, path | CLI switches in rc | per file `.config`, MD5 | 50/N Hz | 1 | yes (many) | no | text menu + test | no |
| Xpeccy | SDL2 raw | name | `.pad` src:dst:rpt | per machine | per entry | 2 | yes (K target) | yes | table + binder | yes |
| xpeccy-plus | SDL2 GC + db | GUID + serial / path / nth | `.pad` | per machine | per entry | 2 | yes | yes | improved binder | rescan |
| unreal-speccy / evo | DInput | JoyId | ini key table | layouts | hotkey, rate | 1 | no | mouse → joy | checkboxes | no |
| UnrealSpeccyP | SDL2 GC | index 0 | XML | no | no | 1 | fixed | no | none | partial |
| Spectral | XInput / linux js | pad 0 | ini (keycodes) | designed only | 0-3 | 1 | maybe | no | press in order | udev |
| Zero | MDX DInput | name | XML | no | no | 2 | no | no | dead code | no |
| ZX-M8XXX | Web Gamepad | id string | localStorage JSON | per project | named profiles | 1 | no | no | wizard | yes |
| pico-spec | TinyUSB HID / XInput / NES | VID/PID quirks | NVS kv | no | no | 2 | OSD nav | no | OSD | USB |
| Murmulator | NES / I2C / Wii | probe | cfg kv | no | fixed | 2 | chords | yes (Wii R) | OSD | boot |
| DeZog | Web Gamepad | n-th | launch.json | per project | no | n | no | no | on-screen pads | yes |
| ZXMAK2 | DInput8 | guidInstance | machine XML | per machine | no | n (hostId) | no | no | combo | rescan |
| zxsp | IOKit HID | index | QSettings | per game fnmatch (kbd) | no | 3 | no | no | dialog | rescan |
| MAME | SDL / XInput / DInput / Raw | GUID + serial, mapdevice | XML cfg + ctrlr | per system | Lua plugin | 8+ | yes | analog | general / this machine | yes |
| RetroArch | ~20 drivers | VID/PID + name score | autoconf .cfg | core / dir / game | 4 modes, duty | 16 | enable_hotkey | n/a | bind-all wizard | yes |
| BizHawk | SDL2 | positional X1 / J1 | JSON | per core | per binding | per core | yes | no | tabs + PNGs | yes |
| Mesen2 | XInput+DI / evdev / GC | slot names | JSON | no | 4 speeds | 8 | yes (combos) | no | vector pad, 4 sets | yes |
| dosbox-x | SDL raw | index | mapper txt | per config | boolean | 2 | yes | no | keyboard drawing | no |
| fs-uae | SDL3 + db | GUID / name file | ini, 2-level db | per game | per port | 4 | yes | mode | launcher combos | yes |
| WinUAE | Raw / DI / XInput | VID/PID + name | .uae | per game | 5 modes, lines | 4 | yes | joymouse | list + test | yes |
| amiberry | SDL3 + db | GUID / name | RetroArch cfg / .controller | per game | 4 rates | 4 | 2-layer hotkey | LStick | PNG pad mapper | yes |
| vAmiga | IOKit HID | built GUID → db | UserDefaults JSON | no | burst / bullets | 2 ports, 7 slots | no | keys → mouse | HID inspector | yes |
| fceux | SDL2 GC | GUID | per-GUID txt | no | pattern | 4 | press / release | no | tabs + profiles | yes, policy |
| Nestopia | SDL raw / DI | index / guid | ini | no | N frames | 2 | ? | no | table | yes |
| Gearsystem | SDL2 GC + db | GUID (shown) | ini | no | no | 2 | per-player shortcut | paddle from mouse | ImGui modals | yes |

## 4. Ideas to take

| # | Idea | Seen in |
|:--|:--|:--|
| I-1 | **Three layers:** physical device → normalized universal pad names (south, east, dpup, leftx) → logical inputs declared by each emulated interface. Bind between the last two, so one profile serves every pad. | MAME, RetroArch, fs-uae |
| I-2 | **Each emulated interface declares its own inputs** (kempjoy, intf2, fuller), and is fitted through the machine's slot managers. Odd interfaces (Zebra, Mikrogen, Battlestar) get a generic `{port, mask, bit, activeLow}` descriptor. | MAME, DeZog |
| I-3 | **Device id = GUID / VID-PID + tie-break** (serial, path, n-th). The name is for display; on hotplug, reattach by id. Pin a physical pad to a logical player. | xpeccy-plus, MAME `mapdevice` |
| I-4 | **Ship gamecontrollerdb, accept a user override, import RetroArch autoconfig.** Score matches (VID/PID +30, name +20); keep a small quirk table. | amiberry, RetroArch, vAmiga, pico-spec |
| I-5 | **Layered profiles:** global → per machine → per content directory → per game, saving only differences. Key per-game profiles by image hash or by filename pattern. Keep the active joystick type in the snapshot. | MAME, RetroArch, ZEsarUX, zxsp, ZXMAK2 |
| I-6 | **A binding is an OR list.** Several parallel mapping sets let keyboard and pad drive the same Kempston fire. | MAME `input_seq`, BizHawk, Mesen2 |
| I-7 | **Any target type per binding:** retro control, ZX key, host action, mouse movement. Turbo per binding in on/off frames, with turbo modes (classic, toggle, hold) and burst. | Xpeccy, ZEsarUX, fceux, BizHawk, RetroArch, vAmiga |
| I-8 | **Hotkeys on the pad through a modifier layer:** hold Guide or Select, then a face button = save, rewind, break, OSD. Minimal chord form, or an explicit per-player shortcut table. | RetroArch, amiberry, Murmulator, Gearsystem |
| I-9 | **Analog:** a radial dead zone with rescale and saturation; a digital threshold with hysteresis (0.45 / 0.35) so diagonals do not chatter; a 4-way / 8-way grid; a response curve; stick → mouse with speed and acceleration. | MAME, vAmiga, dosbox-x, Xpeccy, WinUAE |
| I-10 | **Keyboard joysticks as virtual devices** with presets (cursor + RCtrl, numpad, WASD + LAlt, QAOP + Space). Keys used by a joystick do not also type on the emulated keyboard. | WinUAE, vAmiga |
| I-11 | **UI:** <ul><li>a drawing of the pad with binding buttons at the physical spots;</li><li>targets labelled by interface ("J1 (Kempston)") and by game meaning where known;</li><li>press to bind, a second press ORs, clear restores the default;</li><li>a live test panel with axis bars and HID properties;</li><li>presets and a bind-all wizard.</li></ul> | Mesen2, amiberry, BizHawk, RetroArch, MAME, ZEsarUX, vAmiga, ZX-M8XXX |
| I-12 | **Next MD6 defaults:** A→B, B→C, X→A, Y and Start→START, Back→MODE, LB/RB→X/Z. Resynchronize after a seek or a state load. Only one connector may take the cursor keys. | jnext |
| I-13 | **Hotplug policy is a setting** (auto-assign to the first free port, or leave unassigned), with connect/disconnect notices on screen. | fceux, RetroArch |

## 5. Pitfalls

| # | Pitfall | Seen in |
|:--|:--|:--|
| X-1 | Identity by index or name only: bindings move on replug, and two identical pads collide. | ZEsarUX, BizHawk, Zero, unreal-speccy |
| X-2 | Opening both the GameController and the raw joystick of one device, so each press counts twice. Not filtering XInput pads out of DInput. | jnext (avoided on purpose), Mesen2 (filtered) |
| X-3 | Hard-coded dead zones and thresholds; no hat or POV; triggers resting at -32768 on raw APIs look pressed. | fs-uae, unreal-speccy, Zero, UnrealSpeccyP, Nestopia (detects the rest value) |
| X-4 | One global joystick type with no per-port or per-game override. Dead UI code. | UnrealSpeccyP, Zero, Spectral, Xpeccy |
| X-5 | The pad can drive only emulated keys and cannot trigger emulator commands. The opposite mistake: fixed destructive chords (LB = reset, Back+Start = quit) with no modifier or confirmation. | unreal-speccy, UnrealSpeccyP |
| X-6 | Joystick-to-keyboard leaking into the matrix while Kempston is also driven. Cursor force-enabled in BASIC. Several interfaces active at once without saying so in the UI. | Spectral |
| X-7 | Autofire with no rate, or with the rate in host units. Rates must count emulated frames so replay is identical. | dosbox-x, WinUAE |
| X-8 | Silently dropped buttons (triggers dropped; only 4 bits reach the classic ports). The UI must show unmapped and unreachable controls. | jnext |
| X-9 | Configs as CLI switches, opaque key codes or platform stores. Prefer readable, diffable JSON with symbolic names that automation can read and write. | ZEsarUX, Spectral, Nestopia, QSettings / UserDefaults users |
| X-10 | No hotplug, or "open only if none is open". | dosbox-x, ZEsarUX, unreal-speccy, Zero, UnrealSpeccyP |

## 6. What we take

| Idea / pitfall | Adopted as | Where |
|:--|:--|:--|
| I-1 | Host control vocabulary → bindings → retro controls declared by retro devices | [architecture.md §1, §6](architecture.md#6-the-canonical-control-vocabulary), [mapping-and-profiles.md](mapping-and-profiles.md) |
| I-2 | Connectors declared by the machine or by a bus card; retro devices declare retro controls; a generic port-bit device for odd interfaces | [retro-devices.md](retro-devices.md) |
| I-3, X-1 | `HostDeviceId` with serial / path / n-th tie-break, reconnect match, instance pins | [architecture.md §5, §9](architecture.md#5-device-identity-and-merging) |
| I-4 | Database lookup order with gamecontrollerdb and RetroArch import, scoring, quirks | [device-database.md §3](device-database.md#3-lookup-order) |
| I-5 | Profile layering built-in ⊂ community ⊂ user; game match by signature (D7); connector plugs in the per-model INI | [mapping-and-profiles.md](mapping-and-profiles.md), [architecture.md §12](architecture.md#12-persistence) |
| I-6 | Several host controls per retro control (OR); keyboard and pad at once | [mapping-and-profiles.md](mapping-and-profiles.md) |
| I-7, X-7 | Targets: retro control, ZX key / chord, mouse, action, layer; autofire in emulated frames, sampled at frame start | [mapping-and-profiles.md](mapping-and-profiles.md), [architecture.md §7](architecture.md#7-the-event-pipeline) |
| I-8, X-5 | A separate debug layer held by a paddle or by View+Menu, plus the debugger-remote role (D8, D9); no destructive action without the layer | [debug-and-ttd.md](debug-and-ttd.md) |
| I-9, X-3 | Radial dead zone, saturation, hysteresis, 4/8-way, curves, trigger rest detection and hair-trigger calibration | [mapping-and-profiles.md](mapping-and-profiles.md) |
| I-10, X-6 | Keyboard presets as host devices; Sinclair / Cursor as a matrix overlay, not key events; the UI says which interfaces are active | [retro-devices.md](retro-devices.md), [architecture.md §8](architecture.md#8-determinism-ttd-and-rzx) |
| I-11, X-8 | Three panes, patch cords, a picture with callouts, live test, learn and bind-all wizard, unreachable controls shown | [ui-ux.md](ui-ux.md) |
| I-12 | Next MD6 default profile; resynchronize after a seek | [mapping-and-profiles.md](mapping-and-profiles.md), [architecture.md §8](architecture.md#8-determinism-ttd-and-rzx) |
| I-13, X-10 | Hotplug policy setting, `auto` connector choice, notices | [retro-devices.md](retro-devices.md), [ui-ux.md](ui-ux.md) |
| X-2 | Endpoint merge before routing; the macOS synthetic pad filtered | [architecture.md §4 rule 2](architecture.md#4-backends-per-os), [device-database.md](device-database.md) |
| X-9 | JSON with symbolic names, readable through every automation surface | [architecture.md §12](architecture.md#12-persistence), [surfaces.md](surfaces.md) |
