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

Worked example: the Sprinter's BIOS shows "Detecting IDE Secondary Slave ... [Press F4 to skip]".
With the screen focused, F4 goes to the SIO as the set 2 code `#0C` and the BIOS moves on; the
emulator's speed stays as it was. On a Pentagon, F4 still selects speed 8x.
