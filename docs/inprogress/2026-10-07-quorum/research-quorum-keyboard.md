# Quorum: keyboard

**Date:** 2026-10-07 · part of [README.md](README.md)

Sources: ZXMAK2 `KeyboardQuorum.cs` (Z), UnrealSpeccy `vars.cpp` table `zxk_quorum` and `input.cpp` `read_quorum` (U),
the `[ZX.KEYS.quorum]` layout comment in U's `x32/quorum.ini`, Black_Cat's port table (BC) for the decode.

## 1. Two matrices

| Matrix | Port | Rows | Bits read | Decode (BC, Z) |
|:--|:--|:--|:--|:--|
| Standard 40 keys | `#FE` | A8..A15 (a row is selected when its address line is 0) | 5 (bits 0-4) | `0x99/0x98` |
| Extra keys | `#7E` | A8..A15 | 6 (bits 0-5) | `0x99/0x18` |

Return value: `#FE` read = (bus value and `#E0`) or the 5 key bits (Z); `#7E` read = (bus value and `#C0`) or the 6 key
bits. A pressed key reads 0. Several selected rows are ANDed. The ROM reads `#7E` itself: the SYS ROM keyboard code needs
the port, "otherwise we get no keyboard at all, even the `#FE` one" (Z comment).

## 2. The `#7E` matrix (U and Z agree on every bit)

Row = the address line that is low; bit = data line. Key names are the Quorum cap labels from U's layout, with Z's PC key.

| Row (port high byte) | b0 | b1 | b2 | b3 | b4 | b5 |
|:--|:--|:--|:--|:--|:--|:--|
| A8 `#FE` | RUS (PgUp) | LAT (PgDn) | - | Num 1 | Num 2 | `.` |
| A9 `#FD` | CAPS | F2 | `~` (`^` arrow) | Num 4 | `'` | Num 6 |
| A10 `#FB` | TAB | F4 | - | Num 7 | Num 5 | Num 9 |
| A11 `#F7` | E-mode (Esc) | F5 | BS (Delete) | Num `/` | Num 8 | Num `-` |
| A12 `#EF` | `-` | - | `+` (`=`) | DEL (Backspace) | Num `*` | G-box (F6) |
| A13 `#DF` | `;` | F3 | `\` | - | `]` | `[` |
| A14 `#BF` | `,` | - | - | - | `/` | Num 3 |
| A15 `#7F` | - | F1 | - | Num 0 | Num `.` | Num `+` |

Z and U label two keys differently (BS / DEL) but put them on the same positions; the table above uses the positions.
39 of 48 positions are used. U's layout comment also lists INV and `[<` (not emulated by U) and the keys F11 / F12
for NMI and RES, which are not matrix keys.

## 3. The `#FE` matrix differences

Standard Spectrum rows, with the cursor keys wired as extra keys on the digit positions 5, 6, 7, 8 without Caps Shift
(Z: Left = `#F7FE` b4, Down = `#EFFE` b4, Up = `#EFFE` b3, Right = `#EFFE` b2; U's comment: FIRE, LT, DN, RT, UP = 0, 5, 6, 8, 7).
Left Shift = Caps Shift (`#FEFE` b0), Right Shift = Symbol Shift (`#7FFE` b1). While Alt is held no key is sent (Z; U has
`AltLock`).

## 4. NMI and reset keys

F11 = NMI (Z requests an NMI of 50000 T; one request per press), F12 = reset. On the NMI acknowledge the control port
`#00` is cleared to 0, so the SYS page handler at `#0066` runs ([research-quorum-reference-consensus.md](research-quorum-reference-consensus.md) M12).

## 5. Implementation notes for unreal-ng

- A `QuorumKeyboard` class holds a second 8 x 6 matrix next to the existing `Keyboard` (the `Keyboard` class and the
  host-key to ZX-key map stay as they are). The decoder ANDs the standard rows into the `#FE` read as today and answers
  `#7E` from the new matrix.
- The automation key names (`PressKey`, `TapKey`, `TypeText`) gain the extra names: `rus`, `lat`, `caps`, `tab`,
  `f1`..`f6`, `e`, `bs`, `del`, `kp0`..`kp9`, `kp.`, `kp+`, `kp-`, `kp*`, `kp/`. `TypeText` of ordinary characters keeps
  using the standard matrix.
- The host-key mapping follows Z (PC key to position, table in section 2), because U's PC-key names are DirectInput
  scan names. Open: host keys for RUS and LAT (Z maps PgUp / PgDn; U maps them the same way).
- The matrix is stateless apart from the pressed keys; key changes are journaled as input events like every other
  keyboard (the `KeyboardMatrix` peripheral already carries "the 8 matrix rows"; whether it can carry 16 rows is a phase 3 task).
