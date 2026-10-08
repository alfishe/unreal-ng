# P-08 input inspector (gate)

**Question.** What does an SDL3 app see of the Deck's controller as a non-Steam game, with Steam Input
on (the default) and off, and as a Flatpak? This covers the back buttons, both trackpads (position,
pressure, click), the gyro / accelerometer, capacitive stick touch, and rumble on the trackpads. How
fast do events arrive?
([input-and-profiles.md §2.1](../../../../docs/inprogress/2026-10-08-steamdeck-client/input-and-profiles.md))

**Pass.** Steam Input off exposes every control; Steam Input on gives a usable fallback (the
"compatible mode" of the design).

## What it shows and logs

- **Header:** environment (`SteamAppId`, `SteamGameId`, `SteamDeck`,
  `SDL_GAMECONTROLLER_IGNORE_DEVICES`, `FLATPAK_ID`, display variables) and the HIDAPI Deck hint.
- **Devices:** every joystick SDL reports (name, VID / PID, gamepad type, path); per opened gamepad,
  its touchpads, gyro, accelerometer, rumble, trigger rumble and Steam handle.
- **Live state:** gamepad buttons, the raw joystick buttons (back buttons appear here even without a
  gamepad mapping), axes, gyro / accelerometer, and both trackpads drawn with finger dots (size =
  pressure).
- **Rates:** events per second by kind, which gives the sensor / HID report rate.
- `p08-events-*.csv`: every event with its SDL timestamp and the time it was handled.
  `p08-summary-*.txt`: devices plus the button event handling delay.

Rumble: **Y** = high band (expected: right pad), **X** = low band (left pad), **B** = trigger rumble.
View + Menu = quit.

## Run matrix

| # | Setup | Arguments |
|---|---|---|
| 1 | non-Steam game, Steam Input default | — |
| 2 | same, Steam Input **off** for this shortcut (controller settings) | — |
| 3 | same as 2 | `--no-hidapi-steamdeck` (what the generic path sees) |
| 4 | Desktop Mode terminal | — |
| 5 | Flatpak (after P-19) | — |

On each run: press every button including L4 / L5 / R4 / R5, touch and click both pads, rest a thumb
on each stick, tilt the Deck, and try Y / X / B rumble.

## Results

| Control | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 |
|---|---|---|---|---|---|
| ABXY, D-pad, bumpers, triggers | | | | | |
| L4 L5 R4 R5 | | | | | |
| Left / right trackpad position | | | | | |
| Trackpad pressure / click | | | | | |
| Gyro / accelerometer (rate Hz) | | | | | |
| Capacitive stick touch | | | | | |
| Rumble high / low / triggers | | | | | |
| Gamepad type, VID / PID | | | | | |
| Button handling delay median (ms) | | | | | |
