# Controller manager: the Qt dialog (UI / UX)

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Design, for review |
| **Decisions** | D5 images, D8 personas, D9 debug layer, D10 connectors, D11 three panes in [README.md](README.md#decisions-2026-10-17) |
| **Model** | [architecture.md](architecture.md) (vocabulary §1, code placement §13), [mapping-and-profiles.md](mapping-and-profiles.md), [debug-and-ttd.md](debug-and-ttd.md), [device-database.md](device-database.md) |
| **Pattern** | the media panel (`unreal-qt/src/media/mediapanelwindow.{h,cpp}`), the drop-target overlay (`droptargetoverlay.{h,cpp}`), the status-bar joystick LED (`statusbarmanager.cpp:625`) |

## Contents

- [1. Principles](#1-principles)
- [2. Layout](#2-layout)
- [3. Interactions](#3-interactions)
- [4. Retro-side pictures](#4-retro-side-pictures)
- [5. States and edge cases](#5-states-and-edge-cases)
- [6. Accessibility, HiDPI, themes, localisation](#6-accessibility-hidpi-themes-localisation)
- [7. Widget structure](#7-widget-structure)
- [8. Main-window integration](#8-main-window-integration)

## 1. Principles

| ID | Principle | Consequence |
|:--|:--|:--|
| U1 | **Zero clicks to play.** A pad connected before or after launch drives the right connector of the loaded program without opening the dialog. | The dialog is for looking and changing, never a required step. The first connect shows a toast ([§8](#8-main-window-integration)), not the dialog. |
| U2 | **Everything visible is live.** Pressed controls light up in the picture, the patch cord of the device that is sending pulses, and the connector's retro picture shows what the guest reads now. | One 30 Hz snapshot read on the GUI thread ([architecture §11](architecture.md#11-threads)). Nothing is a preview that waits for "Apply". |
| U3 | **Nothing hidden changes behaviour.** Simple hides settings, not their effects. A binding from the debug layer, a non-default dead zone or an autofire is shown as a badge in Simple too ("2 Advanced settings active", which opens Advanced on click). | A player never meets a button that "does something strange" with nothing on screen explaining it. |
| U4 | **Same verbs as the surfaces.** The dialog is a thin adapter over `ControllerControl` (connectors, plugs) and `HostInputService` (devices, bindings, profiles), like `MediaPanelWindow` over `MediaControl`. Every change the dialog makes can be made by WebAPI / MCP / CLI with the same name and the same refusal text ([surfaces.md](surfaces.md)). | Errors shown in the dialog are the core's `message` fields, not Qt strings. |
| U5 | **Changes apply at once and persist at once.** No OK / Cancel. Undo is per change (Ctrl+Z inside the dialog, the last 50 edits) and **Reset** per binding, per layer, per profile. | Modeless window: the user plays with the dialog open and sees the effect. |
| U6 | **Two personas.** Simple (default) for players; Advanced for developers. The switch is in the title bar of the dialog and remembered in `settings.json` (D8). | The debug layer, roles other than player, backend probes, raw HID, curves and calibration exist only in Advanced. |

**Opening.** Tools > Controllers (checkable, after Tools > Media, `menumanager.cpp:1219`); a click on the status-bar
joystick LED; **Change** on the auto-profile toast; the first-connect toast. Modeless, one window per main window,
remembers geometry (`View/ControllersGeometry`), rebinds on `EmulatorBinding::bound/unbound` like the media panel.

## 2. Layout

### 2.1 Simple (default, 980 × 620 minimum)

```
┌ Controllers ─────────────────────────────────────────────────────────────────── [ Simple | Advanced ] ┐
│ Profile  Auto: signature "Elite (1985)"  ▾        [Keep]  [Change…]                                    │
├─────────────────────────┬────────────────────────────────────────────────────┬────────────────────────┤
│ HOST DEVICES            │  Xbox Elite Wireless Controller Series 2           │ PENTAGON 128           │
│                         │  [ Front | Back ]                                  │                        │
│ ┌──┐ Xbox Elite 2     ● ┼──┐                                                 │ ┌──┐ Kempston  #1F     │
│ │▣▣│ USB · player       │  │   LB  Fire 2 ─┐   ┌──────────────┐  ┌─ RB  —     │ │▣ │ Kempston joystick ▾│
│ └──┘                    │  │   LT  —   ────┼───┤              ├──┼── RT  Fire  ├─┼─▸ Xbox Elite 2       │
│                         │  │ D-pad Kempston ┼──┤   [photo]    ├──┼── Y   SPACE │ └──┘                   │
│ ┌──┐ Keyboard         ○ ┼──┼──┐  L-stick  ──┼──┤              ├──┼── B   ENTER │                        │
│ │⌨ │ numpad 8 4 6 2 0   │  │  │  Kempston ───┘  └──────────────┘  └── A   Fire  │ ┌──┐ Keyboard          │
│ └──┘                    │  │  │                                               │ │⌨ │ ZX keyboard       │
│                         │  │  └───────────────────────────────────────────────┼─┼─▸ Keyboard (typing)  │
│ ┌──┐ Mouse            ○ │  │                                                  │ └──┘                   │
│ │◖ │ not captured       │  │   ◌ unbound: View, Menu, R-stick                 │                        │
│ └──┘                    │  │                                                  │ ┌──┐ Kempston mouse    │
│                         │  │                                                  │ │◖ │ (empty)           │
│                         │  │                                                  │ └──┘  drop a device    │
│ [Identify]  [Test…]     │  └─ (cord colour = connector colour)                │                        │
├─────────────────────────┴────────────────────────────────────────────────────┴────────────────────────┤
│ ● Elite 2 drives this window (follows focus)                                  2 Advanced settings ▸  │
└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Advanced (1280 × 760 minimum)

```
┌ Controllers ───────────────────────────────────────────────────────────────────────────────── [ Simple | Advanced ] ┐
│ Profile  Auto: port reads → Kempston (3 s, 50 reads/frame of #1F)  ▾   [Keep] [Change…] [Save as…] [Reset]          │
│ Layer    [ Play | Debug ]    Layer key: hold P4 (Debug)        Debug profile: "TTD scrub (default)"  ▾               │
├──────────────────────────────┬──────────────────────────────────────────────────────────────┬───────────────────────┤
│ HOST DEVICES                 │  Xbox Elite Wireless Controller Series 2 · 045E:0B00 fw 5.17 │ PENTAGON 128 (inst 1) │
│                              │  [ Front | Back ]  Layer: Debug                              │                       │
│ ┌───┐ Xbox Elite 2         ● │                                                              │ ┌──┐ kempston   #1F   │
│ │▣▣ │ 045E:0B00 · USB        │   P1  Bookmark ─┐                         ┌─ P2  Next bookmark│ │▣ │ Kempston joy.  ▾  │
│ └───┘ battery: n/a (USB)     │   P3  Rev. cont.┼───┐   ┌────────────┐  ┌─┼─ P4  [layer key]  │ │  │ D0-D4 (+D5 F2)    │
│       role [player      ▾]   │                 └───┼───┤ [back view]├──┤ └─ (profile btn: hw)│ │  │ ▸ Elite 2         │
│       instance [follow ▾]    │   LT  Scrub back ───┘   └────────────┘  └─── RT  Scrub fwd    │ └──┘                  │
│       backends: IOHID ✓ GC ✓ │   D-pad  step: ◂ back  ▸ fwd  ▴ frame  ▾ instr.             │ ┌──┐ keyboard         │
│                              │                                                              │ │⌨ │ ▸ Keyboard        │
│ ┌───┐ Keyboard             ○ │   ◌ greyed: Menu — reserved for OSD in Play layer           │ └──┘                  │
│ ┌───┐ Mouse                ○ │                                                              │ ┌──┐ kempston-mouse   │
│ ┌───┐ Stream Deck + XL     ○ │──────────────────────────────────────────────────────────────│ │◖ │ (empty)          │
│ │▦▦ │ companion · debugger   │ TEST   lstick ●──┼──○   rstick ○─┼──○   LT ▁▂▃▅▇ 0.72        │ └──┘                  │
│ └───┘   remote (WebAPI)      │        dead zone 0.30 (ring)  hysteresis 0.45/0.35          │                       │
│                              │        RT ▁▂▂▂▂ 0.18 (hair-trigger stop: full = 0.61)       │                       │
│ [Identify] [Learn…] [Photo…] │        raw 20 00 1c 00 00 00 … (report 0x20, 17 B)  [copy]  │ [+ Connector card…]   │
├──────────────────────────────┴──────────────────────────────────────────────────────────────┴───────────────────────┤
│ Probe: IOHID ok (3 endpoints, 1 synthetic filtered) · GameController ok · Raw HID ok · SDL3 not built  [Details…]   │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 The three panes

**Left: host devices.** One row per merged host device ([architecture §5](architecture.md#5-device-identity-and-merging)):

| Element | Simple | Advanced |
|:--|:--|:--|
| Thumbnail (48 px, from the database image, or the family generic) | yes | yes |
| Name (database display name, else the HID product string) | yes | yes |
| Transport (USB / Bluetooth / adapter) | yes | yes, plus VID:PID and firmware |
| Battery (icon, percent; "n/a" where unknown: USB, and any pad whose backend does not report it) | icon | icon and percent |
| Activity dot: filled while any control is away from rest | yes | yes |
| Role combo: player / debugger remote / disabled (D9) | no (always player) | yes |
| Instance combo: follow focus / pinned to instance N / all ([architecture §9](architecture.md#9-several-emulator-instances)) | only when two or more instances run | yes |
| Backends that see it, with a tick per capability source | no | yes |
| Keyboard and Mouse rows (always present) | yes | yes |
| Companion devices (Stream Deck and other paired clients) | no | yes, read-only, "controlled through WebAPI" |

**Right: machine connectors.** One card per connector the machine (or a bus card) declares
([retro-devices.md](retro-devices.md)), in the machine's own order. Each card: the connector id and the port it
decodes (`#1F`, keys 6-0, NR #05 mode), the retro device combo (only devices the connector accepts), a picture of
the retro device ([§4](#4-retro-side-pictures)), and the host devices plugged into it. An empty connector says
"drop a device". **+ Connector card…** (Advanced) opens the bus-slot chooser for an interface card (Kempston on a
48K, a Fuller box), through the slot manager, not ad hoc.

**Centre: the controller picture.** The selected host device (click a row on the left; the last active device by
default):

- the photo in the house style ([device-database.md](device-database.md)), **Front | Back** toggle when the
  hotspot map has a back view (Elite paddles, DualSense Edge back buttons);
- **callouts** in two label columns, left and right of the image, each sorted by its anchor's y so leader lines
  never cross; a callout reads `<device label>  <target>` ("LB  Fire 2", "A  Fire", "D-pad  Kempston");
- callout colour = the colour of the connector its target belongs to (Kempston blue, Sinclair 1 orange, keyboard
  grey, actions purple, debug layer amber);
- **live highlight**: a pressed control's hotspot glows and its callout bolds; an analog control shows a small
  meter inside the callout;
- **greyed callouts** with the reason in the tooltip and in Advanced inline: unbound ("—"), reserved ("Menu:
  OSD in Play layer"), unreachable on this connector ("MD X/Y/Z: readable only through NR #B2", "Fire 2: this
  Kempston decodes D0-D4 only"), hidden by the hardware where the raw paddle bits are not visible ("P1: mapped on the
  controller by its profile 2; switch the Elite to profile 0 to bind it", macOS USB);
- a short "unbound" line under the picture lists the controls with no binding in this layer.

**Patch cords** are drawn in an overlay across the three panes: from a host device row through the picture's centre
to the connector card, one cord per plug, in the connector colour. A cord pulses while the device sends. A device
can be plugged into several connectors (one pad, Kempston directions plus keys on a shift layer); a connector can
take several devices (two pads ORed into one Kempston, as the Next ORs two sticks feeding one port).

- **Drag** a host device row onto a connector card: plug (the default play profile for that retro device applies).
- **Drag** a cord's end off a connector: unplug. Drop on empty space removes the plug, with undo.
- A cord cannot attach where the connector refuses the device: the card shows the refusal text while hovering
  (the core's `refusal`, like the media drop-target tiles).

## 3. Interactions

### 3.1 Binding editor

The first design (superseded by the table below, kept for the options it lists). Click a callout (or Enter on a focused hotspot) → a popover anchored at the callout:

```
┌ A (south) ─ Play layer ───────────────────────────┐
│ Now:  Kempston Fire                               │
│ [ Press a control… 5 s ]  [ Clear ]  [ Default ]  │
│ Target                                            │
│  ▸ Retro controls  Kempston: up down left right   │
│                    fire fire2 fire3               │
│  ▸ ZX keys         [ZX keyboard picker]  chord…   │
│  ▸ Mouse           dx dy  left right middle wheel │
│  ▸ Actions         pause  screenshot  OSD keyboard│
│  ▸ Layer           hold: Debug  /  toggle: …      │
│ Options (Advanced) autofire [off ▾] on 2 off 2 fr │
│                    tap / hold   threshold 0.50    │
└───────────────────────────────────────────────────┘
```

- **Press to bind**: "Press a control…" waits 5 s for an edge on the selected device (baseline taken first, so a
  resting trigger or a drifting stick does not bind itself). A second press within the window **OR-appends**
  (MAME): "A or Keypad 0". Esc cancels.
- **Clear** empties the binding; **Default** restores the profile's (and a second Clear on an empty binding also
  restores the default, as in MAME).
- **Target picker** grouped: retro controls of every connector the device is plugged into, ZX keys (a small ZX
  keyboard to click, chords with CAPS / SYMBOL), mouse, actions (Simple shows player actions only: pause, OSD
  keyboard, screenshot, quick save; Advanced adds debug actions), layer switches.
- Options (Advanced): autofire on / off frames, tap vs hold, threshold for analog-as-digital, 4-way / 8-way for a
  stick, response curve.

Built (C4), redesigned after the owner's review of the first editor ("not intuitive, cryptic"; the survey of other
emulators is [research-binding-ux.md](research-binding-ux.md)): the bindings are a table below the picture
(`unreal-qt/src/controllers/bindingtable.cpp`) that starts from what the game sees, as Mesen2, RetroArch, MAME,
Dolphin and PCSX2 do, not from the pad's controls:

```
 Playing | Debugging                     Works as [Machine joystick ▾]  Save for [Every dualsense pad ▾]
 Joystick
   Move          ( D-pad ) ( Left stick )  +
   Up            + Bind
   Fire          ( Cross )  ( R2 · auto )  +
   Fire 2        ( Circle )  +
 ZX keys
   Space         ( Triangle )  +
   Enter         ( Options )  +
                 + Add a ZX key…
 Mouse
   Move          ( Right stick )  +
 Emulator
   Pause / continue   + Bind
   On-screen keyboard ( Create )  +
 [Set all…] [Restore defaults]   Cross → Joystick fire
```

- **Rows** are the machine's inputs in plain words: Joystick (Move, Up, Down, Left, Right, Fire, Fire 2, and any MD
  pad button a profile binds), ZX keys (each bound key, "+ Add a ZX key…" opens a Spectrum keyboard with Caps / Symbol
  Shift), Mouse (Move, buttons), Emulator (pause, on-screen keyboard, quick menu, turbo, screenshot, reset). The
  Debugging tab (Advanced: debug layer only) lists every host action by group.
- **Chips** are the pad's controls bound to a row, named as the pad names them (Cross, LB, D-pad, Left stick ←):
  bold when the user's own, plain when a default. Click **+**, then press: the button counts down 5 s, the bar above
  says what it waits for, Esc or Cancel cancels, a timeout cancels and changes nothing. The baseline is taken first
  and a control counts again once it is let go. Move takes the D-pad or a whole stick, Mouse move a stick, other rows
  one control (a stick's direction, a trigger's click).
- **One control does one thing**: pressing one another row has asks "Cross is Joystick fire 2 now. Use it for
  Joystick fire instead?"; taking a direction from a D-pad bound to Move hides the D-pad's group binding and binds
  its other three directions alone, so they keep moving.
- **Right-click a chip**: Change…, Autofire (Off, Slow, Normal, Fast), Short tap per press, Remove, Restore default,
  Edit as text….
- **Works as** sets the scheme the Save for profile extends (`scheme` verb); **Save for** picks the profile written:
  this game (when a game profile applies), every pad of the family, only this pad, every pad. **Set all…** walks Up,
  Down, Left, Right, Fire, Fire 2 with Step N of M, Back, Skip, Cancel. **Restore defaults** removes the user's
  bindings of the tab (and the scheme) from the Save for profile.
- **The picture is the overview**: callouts name the row of each control, hovering a chip highlights its control, a
  pressed control lights its chips and scrolls to them, a click on a callout names its row.

Everything goes through the host verbs (`bindings`, `bind`, `unbind`, `scheme`), as WebAPI, MCP, CLI and Python do.
Profiles… opens the profile manager (`profilesdialog.cpp`): every profile as YAML, edited and saved as a user
profile, New, Import…, Export…, Delete, Use as game profile.

### 3.2 Layer tabs

**Play | Debug** above the picture, Advanced only. Debug shows the debug profile's bindings over the same picture,
callouts in amber; the layer key itself is marked "[layer key]". While the user holds the layer key on the pad,
the dialog switches the tab live (U2). Profile-defined shift layers appear as extra tabs ("Keys").

### 3.3 Profile bar

```
Profile  Auto: signature "Elite (1985)"          ▾  [Keep] [Change…] [Save as…] [Reset]
Profile  Auto: port reads → Kempston (3 s)       ▾
Profile  Auto: machine default (Pentagon 128)    ▾
Profile  User: "Elite — my keys" (signature)     ▾
```

- The text says **why**, in the order of DT-2 ([mapping-and-profiles.md §6](mapping-and-profiles.md)): signature,
  port reads, machine default, user.
- **Keep** saves the current state as a user profile matched to the loaded program's signature. **Change…**
  focuses the picture in edit mode. **Save as…** names a profile (and its match rule). **Reset** goes back to the
  automatic choice, with undo.
- The combo lists every profile that matches, then "Other profiles…" (a searchable list: built-in, community,
  user, with the scope as a badge).

### 3.4 Test panel

Docked under the picture (Advanced) or opened by **Test…** (Simple, a smaller sheet):

- stick plots with the dead-zone ring and the hysteresis thresholds drawn; the dot is the raw value, a second dot
  the value after dead zone and calibration;
- trigger bars with the curve and the calibrated full value (an Elite hair-trigger stop saturates early: the
  measured maximum is stored for the session);
- a row of button lamps in the canonical vocabulary order;
- Advanced: the last raw report in hex with its report id and size, copyable; the endpoint list and which backend
  supplies which capability; the backend probe report (the same as `GET /controllers/host`).

### 3.5 Learn wizard (unknown devices)

For a device the database does not know (raw `button.<n>` / `axis.<n>` controls), **Learn…** walks the canonical
controls: "Press Up…", "Press Down…", … "Press South (bottom face button)…", with **Skip** and **Back**. Edge
detection against a baseline (axis moved more than 0.4, or a button edge), as ZX-M8XXX. The result is a mapping in
`gamecontrollerdb.txt` line format, stored as a user device override and offered for **Export** (to contribute to
`data/controllers/devices.json`). The wizard opens itself, once, as a toast suggestion, when an unknown device is
first plugged in.

### 3.6 Hotspot editor (user photos)

**Photo…** loads a user image for the selected device (or a view of it: front, back, top), then shows the controls
in a list; the user clicks the spot for each, adjusts a circle or a rounded rectangle, and picks the callout side.
Pressing the control on the real device selects its row (so the user does not need to know the names). Saved as
the same JSON hotspot map as built-in images ([device-database.md](device-database.md)), in the user directory.

### 3.7 Identify

**Identify** rumbles the device for 300 ms and blinks its LED (Elite: the guide button light, DualSense: the light
bar, player LEDs) where the capabilities allow, and pulses its row and cord in the dialog. Tells two identical pads
apart.

It is the first effect of **Test haptics** in the selected device's header (owner 2026-10-18/19): an effect list and
the Test haptics button that plays the chosen one, both always there. The list holds Identify and one test effect per
actuator the device has (low motor, high motor with three clicks, left and right trigger) and an ADSR swell; a Surface Dial
gets its clicks instead (two clicks for Identify, a click, three clicks, a buzz). For a
device the emulator cannot drive both are off and their tooltip says why (keyboards and mice have no motors; a
DualSense rumbles over USB only for now; an Xbox pad over USB on macOS: connect it over Bluetooth, haptics.md §9;
Linux: no write access to the event device). The reason comes from the backend
(`IBackend::RumbleUnavailable`). Test effects are debug feedback: they play while the game's haptics are held back.

### 3.8 Auto-profile toast

Shown in the main window when an automatic choice is made for a newly loaded program and the decision is not a
signature match the user already kept:

```
┌──────────────────────────────────────────────────────┐
│ ▣ Elite 2 → Kempston  (port reads, 3 s)              │
│                              [Keep]   [Change…]   ×  │
└──────────────────────────────────────────────────────┘
```

- Auto-hides after 8 s; **Keep** saves the per-game user profile; **Change…** opens the dialog with that profile
  selected. Never shown during replay or an automation session.
- Like `DropTargetOverlay`, a frameless translucent window of its own over the screen area: the GPU screen is a
  native OpenGL window that a child widget would not draw over.

## 4. Retro-side pictures

Each connector card shows its retro device, live:

| Retro device | Picture | Live state |
|:--|:--|:--|
| Kempston / Fuller / Timex joystick | a classic stick (Quickshot style) in the house style | lit arrows and fire; the port byte in Advanced (`#1F = 0x11`) |
| Sinclair 1 / 2, Cursor | a stick **and** a ZX keyboard strip with the five keys | the keys the overlay presses light up on the strip, in the connector colour; real key presses stay grey, so the user sees the two are independent ([architecture §8](architecture.md#8-determinism-ttd-and-rzx)) |
| Keyboard connector | the machine's keyboard (48K rubber, 128 / +2, Pentagon PC keyboard, Next) | pressed keys from every source: host keyboard, Sinclair / Cursor overlays, bindings to ZX keys (each in its source's colour) |
| MD 3 / 6-button pad (Next, Evo) | a Mega Drive pad | lit buttons; X Y Z MODE carry the "NR #B2 only" mark on Next |
| Kempston / AMX / Evo / Sprinter mouse | a mouse with buttons and wheel | button lamps, wheel counter, X / Y counters in Advanced |
| Light gun | a gun silhouette | trigger lamp, the aim point drawn on a miniature of the screen |

A click on the retro picture opens the connector's settings: decode variant where the board allows a choice,
button order (Kempston mouse D0 = left or right per board, [research-retro-hardware.md](research-retro-hardware.md)),
extra fire bits, Next NR #05 mode follows the guest (read-only, shown).

## 5. States and edge cases

| State | What the dialog shows |
|:--|:--|
| No host pad connected | Left pane: Keyboard and Mouse only, plus an empty-state card "Connect a game pad or joystick: USB, Bluetooth, or a DB9 adapter. It works at once." Centre: the keyboard with its current bindings (numpad Kempston by default). |
| Permission missing, Linux hidraw | The device row says "Basic input only" with a lamp; the detail: "Paddles, LEDs and Bluetooth extras need access to /dev/hidraw*. Install the udev rule: `sudo cp data/controllers/70-unreal-ng.rules /etc/udev/rules.d/ && sudo udevadm control --reload`, then reconnect." |
| Permission missing, macOS Input Monitoring | Only for IOHID keyboard-class devices (game pads do not need it): "Allow unreal-qt in System Settings > Privacy & Security > Input Monitoring" with a button that opens the pane. |
| Backend not available (GameInput runtime missing on Windows) | Advanced probe line; Simple shows nothing unless a device loses a feature, then a lamp on its row. |
| Replay or RZX playback owns input | A banner over the connectors: "Input owned by replay: live controllers are ignored until it ends." Cords drawn dashed; bindings stay editable. |
| Automation session (remote control guard) | The dialog is locked like every other control (`RemoteControlGuard`): pane contents greyed, the banner shows the guard's line ("Remote control: DeZog (ZRCP) · 127.0.0.1"). The debug layer of a companion still follows the gate rules of the companion design. |
| Several instances | The title shows the instance; the instance combo appears on every row; cords to another instance's connectors are not drawn, the row says "drives instance 2". |
| Device without image | The family generic (Xbox-style, PlayStation-style, Nintendo-style, SNES-style, arcade stick, DB9 stick, generic pad), with the device's own name; unknown family: a neutral pad outline plus the raw control list. |
| Hardware profile hides controls (Elite profile 1-3) | A line under the picture: "The controller is on its profile 2: paddles report as other buttons. Switch to profile 0 (LED off) to bind them." (from the report's profile byte when visible). Shown only where the raw paddle bits are not visible (macOS USB); over macOS Bluetooth the paddles bind on any profile and Advanced notes "P1 also presses B on the controller's profile 2: removed here" |
| Hotplug | A new row slides in (200 ms) with a pulse; its cord grows to the connector `auto` chose. A removed device fades its row to 40 % for 3 s with "disconnected", then its plugs stay as dashed cords until it returns or the user removes them. |
| Machine switch | Connector cards change to the new machine's; plugs carry over by connector id where it exists, else follow `auto`. |

## 6. Accessibility, HiDPI, themes, localisation

- **Keyboard navigation**: Tab cycles panes; arrows move within a pane; in the picture the arrows move between
  hotspots in reading order, Enter opens the binding editor, Delete clears, Ctrl+Z undoes. **Plug** and **Unplug**
  are also context-menu items and keys (P / U on a selected device), so drag and drop is never required.
- Every hotspot and callout has an accessible name ("A button, bound to Kempston Fire"); live state changes are not
  announced (too frequent), the binding text is.
- Colours are never the only signal: each connector colour also has a short tag (K, S1, S2, CUR, KB, DBG) on cords
  and callouts.
- **HiDPI**: images ship at 1x and 2x (or as SVG when vectorized, TODO Q-1); hotspot maps use design-space
  coordinates scaled by a `QTransform`, so one map serves every scale.
- **Themes**: house-style photos are prepared on a transparent background; callouts, cords and highlight colours
  come from the palette (dark / light), not baked into images.
- **Localisation**: device and control labels come from the database (per locale where present, English
  fallback); retro labels ("Fire", "Sinclair 1") are `tr()` strings; refusals and reasons are core `message`
  strings in English (as everywhere else in the surfaces).

## 7. Widget structure

```
ControllersDialog (QWidget, Qt::Window; modeless)
├── ProfileBar            (QWidget: label + combo + buttons; Advanced: layer tabs, debug profile combo)
├── QSplitter
│   ├── HostDeviceList    (QListView + HostDeviceModel, delegate paints thumbnail, dot, battery)
│   ├── ControllerPicture (QGraphicsView; scene: pixmap/svg item, HotspotItem per control, CalloutItem columns)
│   │   └── DeviceTestPanel  (QWidget below the view; stick/trigger plots, raw report)
│   └── ConnectorList     (QListView + ConnectorModel, delegate paints the retro picture and plugs)
├── PatchCordOverlay      (transparent child over the splitter; QPainterPath cords from item geometry)
└── StatusLine            (probe summary, instance, "N Advanced settings active")

Popovers / sheets: BindingEditor, LearnWizard, HotspotEditor, ConnectorSettings
Toast (main window): ControllerToast (frameless top-level, like DropTargetOverlay)
```

- **Refresh**: like `MediaPanelWindow` (a 250 ms `QTimer`, redraw only when `Revision()` changed,
  `mediapanelwindow.cpp:111, 244`): the connector side polls `ControllerManager::Revision()`, the host side polls
  `HostInputService::Revision()` (devices, bindings, profiles). Live control state (highlight, meters, cord
  pulses) is a separate 30 Hz snapshot read, painted only while the dialog is visible; nothing runs while hidden.
- **Requests** are async (`ControllerControl` replies with `pending` where a change waits for a frame boundary);
  the UI never blocks on the emulator thread.
- Class mapping: [architecture.md §13](architecture.md#13-code-placement) (`controllersdialog.*`,
  `hostdevicelist.*`, `connectorlist.*`, `controllerpicture.*`, `patchcordoverlay.*`, `bindingtable.*`,
  `devicetestpanel.*`, `learnwizard.*`, `hotspoteditor.*`), plus `profilebar.*` and `controllertoast.*`.

## 8. Main-window integration

- **Status-bar LED** (`StatusBarManager::updateJoystick`, `statusbarmanager.cpp:625`): today it shows the Kempston
  state. It becomes a connector LED: visible when any connector has a plug; lit while any plugged retro device is
  away from rest; the tooltip lists per connector what is held ("Kempston #1F: up, fire · Sinclair 1: —") and which
  host device drives it. Click opens the dialog.
- **Layer indicator**: while the debug layer key is held, an amber "DEBUG" tag next to the LED (and in the HUD if
  enabled), so a held paddle never silently eats game input.
- **Toasts**: first connect of a device ("Xbox Elite 2 connected → Kempston. [Open controllers]"), the auto-profile
  toast ([§3.8](#38-auto-profile-toast)), a disconnect while playing ("Elite 2 disconnected: paused" when the
  pause-on-disconnect option is on, off by default).
- **Menu**: Tools > Controllers (checkable). Machine > Controllers submenu: quick plug per connector (the same
  `ControllerControl` verbs), "Swap joystick ports", "Reset bindings to default".
