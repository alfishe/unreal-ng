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
`\` `;` `'` `/` and the backquote). The physical key for a PS/2 machine always comes from the native key code and never
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

## App shortcuts and the machine's keys

The app has few shortcuts of its own: Open, Save snapshot, Quit, Preferences and Help (the platform's standard
keys), and Full Screen: Cmd+F on macOS, Alt+Enter on Windows and Linux. While the emulator screen has keyboard focus
the machine's keys win over them:

| Keys | Belong to the machine |
|---|---|
| Ctrl + a key (no Alt) | on every machine: Ctrl is Symbol Shift on the matrix and Ctrl on a PC keyboard |
| Alt + a key, Ctrl+Alt + a key | where the route reaches a PS/2 keyboard (`ps2`, `both`; `auto` on a machine that has one) |
| F1-F12, Shift allowed | the same |

So Ctrl+O types `;`, Ctrl+S Symbol Shift + S, Ctrl+Q `<=` and Ctrl+F `TO` instead of opening, saving, quitting or
going full screen; on a PC-keyboard machine Alt+F, Alt+F1 and the F-keys reach DSS, Flex Navigator and Wild
Commander instead of opening a menu. The screen view accepts the key's shortcut override
(`KeyboardManager::machineOwnsKey`, used by every screen view), so the shortcut does not fire.

- Full Screen and the Cmd / Win combinations stay with the app and the OS on every machine.
- To use a shortcut the machine owns: click outside the screen, or use the menu.
- Keys the OS keeps for itself (Ctrl+Alt+Del and Win on Windows, Ctrl+Alt+F1-F12 on Linux, Cmd+Tab and the
  Spaces keys on macOS, Print Screen on Windows 11) reach the machine through the on-screen keyboard: its PC
  pictures latch Ctrl, Alt, Shift and Win.

Worked example: the Sprinter's BIOS 3.04 shows "Detecting IDE Primary Slave ... [Press F4 to skip]" (a disk on the
master, none on the slave). With the screen focused, F4 goes to the SIO as the set 2 code `#0C` and the BIOS moves on.

## Symbols and the host layout

- **Symbol Shift symbols.** `;` `'` `:` `?` `<` `>` `_` `"` `+` `*` `/` `|` and the other symbols of the host keyboard
  press Symbol Shift and their matrix key. Most of them are typed with the host Shift, and the host Shift is Caps
  Shift: CAPS SHIFT + SYMBOL SHIFT + P would be EXTEND MODE + P, not `"`. While such a symbol is down, Caps Shift is
  let go, and pressed again after it if the host Shift is still down.
- **The digit row goes by position** on every layout: Shift+2 is Caps Shift + 2 (CAPS LOCK) also on a German or a
  Russian keyboard, which print `"` there; the AZERTY key of `"` and 3 is 3. The keypad `/` `*` `-` `+` are the ZX
  symbols.

## The ZX Next's extended keys

The Next's keyboard has 16 keys beyond the 40 of the matrix: EXTEND MODE, GRAPH, TRUE VIDEO, INV VIDEO, EDIT, BREAK,
DELETE, CAPS LOCK, the four arrows and `;` `"` `,` `.`. On the hardware they sit in two more membrane columns
(`membrane.vhd`), and each one is a key of its own:

- **The matrix.** The machine presses the key's two matrix keys: LEFT is CAPS SHIFT + 5, EXTEND MODE is CAPS SHIFT +
  SYMBOL SHIFT, `;` is SYMBOL SHIFT + O. NR #68 bit 4 cancels these matrix entries; software that reads the extended
  keys itself sets it.
- **NR #B0 / #B1.** The key shows there whether the matrix entries are on or off.

A host key that stands for one of them (Left, Backspace, Esc, the backquote, Caps Lock, `,` `.`), the on-screen
keyboard's Next picture and automation (`left`, `extend`, `graph`, `truevideo`, `invvideo`, `capslock`, `;`, `"`
...) press the extended key itself. The TTD journal records it as one key. On the other machines the same names
press the shift combination, as before.

## The Orel BK-08's extra columns

The Orel BK-08 (LSY256) has 67 keys. Its matrix has the five ZX columns and two more, KD5 and KD7, read on port #FE
bits 5 and 7 (keyboard schematic БУ5.103.474; the ROM's KEY-SCAN reads `AND #BF`). АЛТ is CAPS SHIFT and УПР is
SYMBOL SHIFT. Sixteen keys sit on the extra columns, two per half-row:

| Half-row | Bit 5 | Bit 7 |
|---|---|---|
| A8 | РУС (`bk08_rus`) | РЕГ (`bk08_shift`) |
| A9 | `\` (`bk08_backslash`) | `/` Ё (`bk08_slash`) |
| A10 | `,` Б (`bk08_comma`) | `.` Ю (`bk08_period`) |
| A11 | `` ` `` `~` (`bk08_tilde`) | ТАБ (`bk08_tab`) |
| A12 | `-` (`bk08_minus`) | `=` (`bk08_equals`) |
| A13 | `[` Х (`bk08_lbracket`) | `]` Ъ (`bk08_rbracket`) |
| A14 | `;` Ж (`bk08_semicolon`) | `'` Э (`bk08_quote`) |
| A15 | Р/А (`bk08_ra`) | ЗГЛ (`bk08_caps`) |

They are plain matrix keys (`ZXKEY_BK08_*`); automation and the on-screen keyboard's Orel picture press them by
these names. Only a machine with this keyboard takes them; on the others they press nothing. The editing keys and
the arrows (ВШ, ГРАФ, РЕД, НОРМ, ИНВ, ← ↓ ↑ →) are wired as CAPS SHIFT + a digit. On the picture, РЕГ and Р/А latch
like the shifts: the ROM treats them as shifts. The LSY256 machine has no port decoder in the emulator yet, so the
model does not run.

## On-screen keyboard

View → On-screen Keyboard (or a controller button bound to `osd.keyboard`) opens the emulated machine's own
keyboard in a window of its own: the 48K's rubber keys, the 128's and the +2's, the Pentagon's sticker keyboard,
the Profi's, the PC clones' PC keyboards and so on (one picture per model, drawn by
[POC 031](../../tools/poc/031-authentic-osd-keyboards/README.md)). The window never takes the keyboard focus from
the screen.

- A click presses the key while the mouse button is held. A key printed with a combination (TRUE VIDEO is
  Caps Shift + 3 on the 128) presses its keys together.
- A modifier (Caps Shift, Symbol Shift, the Orel's РЕГ and Р/А; on a PC picture Shift, Ctrl, Alt, Win) latches
  on a click and is let go with the next key, or by a second click. Hiding the window lets go of every key.
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
