# Keyboard: the host keyboard, the ZX matrix and PC keyboards

Each host key goes to the emulated machine as two events: the ZX key for the Spectrum key matrix
and the physical PC key for a machine's PS/2 keyboard (ZX-Evo / TS-Conf through the AVR, ATM
Turbo 2+ through its keyboard controller, the Sprinter through the Z84C15 SIO). Where they go is the
**host keyboard route**, `[INPUT] HostKeyboard=` or the Machine menu:

| Route | Matrix | PS/2 keyboard |
|---|---|---|
| `auto` (default) | yes | yes, where the machine has one |
| `matrix` | yes | no |
| `ps2` | no | yes |
| `both` | yes | yes |

## Keyboard layouts

The ZX key normally comes from the character the host layout puts on the key, so a Latin layout keeps
its own symbols. A layout without Latin letters (Russian, Greek) reports a character the map does not
know; the key's position then decides: the key where `K` sits on a US keyboard is `K` under any
layout. This covers the letters, the digits and the keys the ZX extended keys use (`,` `.` `-` `=`
`\` and the backquote). Other punctuation of such a layout (`;` `'` `/`) has no ZX key and sends nothing
to the matrix. The physical key for a PS/2 machine always comes from the native key code and never
depended on the layout.

## Host keys on macOS

A Mac keyboard has Control and Command where a PC has Ctrl and Win. The emulator follows the key
caps, not Qt's names for them (Qt on macOS calls Command "Ctrl" and Control "Meta"):

| Mac key | Machine sees | PS/2 (set 2) | ZX matrix |
|---|---|---|---|
| Control | Ctrl | `14` / `F0 14` | Symbol Shift |
| Command | nothing (a host key) | - | - |
| Command, option on | Win (GUI) | `E0 1F` / `E0 F0 1F` | - |

- **Command stays with the Mac.** Cmd+Tab, Cmd+Q, Cmd+F (full screen) and every other Command
  combination send nothing to the machine: neither Command nor the key pressed with it. A key that
  was already down when Command went down still sends its release.
- **The option:** Machine > Host Keyboard > *Pass Command as Win Key* (the app settings file,
  `Keyboard/MacCommandKey=gui`; `host`, the default, keeps Command on the Mac). For guest software that
  wants the Win key. Command combinations then reach the machine too.
- Keys held when the window loses focus (Cmd+Tab away) are released for the machine, so nothing
  stays down.
- Windows and Linux: Ctrl is Ctrl and the Win / Super key is the GUI key, as before.
- The mouse release key (`[INPUT] MouseReleaseKey=`) names the keys by their caps as well: `Ctrl` is
  the Control key on a Mac.

Why it matters, a worked example: before this rule a Command press reached the Sprinter as PS/2
Left Ctrl (`14 F0 14`). One stray Cmd press while deMarche's dontBlink (final version) was loading
latched the Sprinter's keyboard interrupt and crashed the demo.

The mapping lives in one place, `KeyboardManager` (`physicalQtKey` / `physicalModifiers`) in unreal-qt.

## Function keys on PC-keyboard machines

The Qt menu binds bare function keys: F1-F4 speed, F5 / F6 / F7 / Shift+F5 Start / Pause / Resume /
Stop, F8-F11 debugger stepping. A PC-keyboard machine needs them too: the Sprinter BIOS skips an IDE
wait on F4, DSS and Flex Navigator, ZX-Evo and TS-Conf file managers use F-keys.

**Rule:** while the emulator screen has keyboard focus and the route reaches the machine's PS/2
keyboard (`ps2` or `both`; `auto` on a machine that has one), F1-F12 without Ctrl, Alt or Cmd
(Shift allowed) belong to the machine. The screen view accepts the key's shortcut override
(`KeyboardManager::machineOwnsKey`, used by all three screen views), so the menu shortcut does not
fire and the key press reaches the machine.

- Matrix-only machines (48K, 128K, Pentagon, Scorpion, Profi, ...) keep every F-key shortcut.
- Shortcuts with Ctrl, Alt or Cmd stay with the GUI on every machine (Ctrl+Tab turbo, Ctrl+R reset).
- To use the F-key shortcuts on a PC-keyboard machine: set the route to `matrix`, click outside the
  screen, or use the menu.

Worked example: the Sprinter's BIOS 3.04 shows "Detecting IDE Primary Slave ... [Press F4 to skip]" (a disk on the master, none on the slave).
With the screen focused, F4 goes to the SIO as the set 2 code `#0C` and the BIOS moves on; the
emulator's speed stays as it was. On a Pentagon, F4 still selects speed 8x.

## On-screen keyboard

View → On-screen Keyboard (or a controller button bound to `osd.keyboard`) opens the emulated machine's own
keyboard in a window of its own: the 48K's rubber keys, the 128's and the +2's, the Pentagon's sticker keyboard,
the Profi's, the PC clones' PC keyboards and so on (one picture per model, drawn by
[POC 031](../../tools/poc/031-authentic-osd-keyboards/README.md)). The window never takes the keyboard focus from
the screen.

- A click presses the key while the mouse button is held. A key printed with a combination (TRUE VIDEO is
  Caps Shift + 3 on the 128) presses its keys together.
- A modifier (Caps Shift, Symbol Shift; on a PC picture Shift, Ctrl, Alt, Win) latches on a click and is let go
  with the next key, or by a second click. Hiding the window lets go of every key.
- Keys held on the host keyboard light up on the picture too.
- Machines with more than one keyboard get a switch over the picture, the choice kept per model: the PC
  clones (ATM Turbo 2+, ZX-Evo, TS-Conf, Sprinter, KAY, Phoenix) have their PC keyboard and the 40-key ZX
  matrix; the ATM Turbo 2+ also the XT keyboard of its first firmware; the Pentagon its bare 40-key board and the
  "2+" case; the Profi its keyboard and the Profi+ one.
- A ZX picture presses the matrix only, a PC picture the machine's PC keyboard controller only (`MC_KEY_*` and
  `MC_PCKEY_*`, the two halves of a host key), so the route `both` does not double a PC picture's key on the
  matrix. When the route keeps a picture's keys from the machine (`matrix` with a PC picture, `ps2` with a ZX
  one), or the machine has no PC keyboard controller in the emulator yet (KAY, Phoenix), the line under the
  picture says so.
- The Profi+ keyboard's cursor keys, its `*` (their own joystick lines) and its RESET are not wired in the
  emulator yet; a click on them says so.

The pictures and their key maps are `data/controllers/keyboards/` (`index.json`: the pictures of each model, the
default first; `<id>.photo.svg`; `<id>.keys.json`: each key's rectangle in mm and the matrix keys or the PC key
(`pckey::Name`) it presses), generated by `tools/poc/031-authentic-osd-keyboards/generate.py`.
