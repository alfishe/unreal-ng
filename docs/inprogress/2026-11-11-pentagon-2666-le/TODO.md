# TODO: Pentagon 2.666 LE

Status 2026-11-11: design written from the upstream sources (`e808402`); no code. See [README.md](README.md).

## Phases

- [x] Owner decisions Q-1...Q-6 (below, 2026-11-11)
- [ ] P0: ROM set `data/rom/p2666/`, `data/configs/p2666/unreal.ini`, `MM_P2666` row, factory case, Pentagon-mode
      decoder with Gluk mapping, power-on upload, read-only ROM pages; T-M1, T-M2, T-M9, T-M10, T-F1, T-F3
- [ ] P1: full decoder (DOS machine, `xx77`, two maps, `xFF7`, `x7F7`, `xxBE`, `xxBF`, `7FFD` / `EFF7` rules and
      quirks, FE flag), turbo 1 / 4 / 8 with the I/O slowdown overlay, INT; Beta Disk path A; T-P*, T-M*, T-D*,
      T-C1..C5, T-B1
- [ ] POC: VHDL oracle in `ghdl` (`tools/poc/NNN-p2666-vhdl-oracle/`, charter first); decides T-V3, T-C3, T-B5
- [ ] P2: video detection, geometry, palette, border, text font; T-V*
- [ ] P3: CMOS / RTC, SD, IDE, mouse, joystick, TurboSound clock / stereo, Covox; T-S*, T-I*, T-F4..F8
      (fetch the NedoOS P2.666 build `osp26sd.$c` into `testdata/machines/p2666/nedoos/`)
- [ ] P4: Beta Disk path B (RAM disk), UART; T-B2..B5, T-C6
- [ ] P5: TTD (`PeripheralId` >= 66), snapshot rules, slots refdata, automation planes, Qt menu,
      `.recipe/machines/pentagon-2666.md`, docs rows, Linux / Windows checks; MCP live block

## Decisions (owner, 2026-11-11)

Rule: the model is the board as built; nothing is improved. Board bugs are copied; where a fix is obvious it stays
as a commented-out `P2666-FIX <D-n>` block in the code (design §4.1), never enabled.

| ID | Question | Decision |
|---|---|---|
| Q-1 | Beta Disk path | P1 the WD1793 (drives A..D); P4 the RAM-disk emulator becomes the default, WD1793 kept as `[P2666] BetaDisk=wd1793` |
| Q-2 | Base clock | as the HDL: 3.5625 MHz (114 MHz / 32; turbo 14.25 / 28.5 MHz), frame 49.70 Hz |
| Q-3 | Even Nemo IDE registers read `FE` (D-5) | copy the VHDL: even IDE reads do not work; fix recipe commented out |
| Q-4 | 8-bit sound sum wraps (D-11) | copy the wrap; saturating mix as a commented-out recipe |
| Q-5 | SD config read (D-4) | `0xFF` |
| Q-6 | Visible area | the board's crop (HR §1.3) |

Follows from the rule: stereo A right / C left (D-6), joystick bits as the MCU pins (D-7, direction from the
schematic), undefined video modes keep the last colour (D-8), CMOS 256 cells with 64 persisted (D-10).

Still to look up (facts, not decisions): joystick pin directions and stereo channel naming on the schematic
(Appendix 2 of the manual, an image); the exact INT-to-pixel offset (VHDL oracle POC).
