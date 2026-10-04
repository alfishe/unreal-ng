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
