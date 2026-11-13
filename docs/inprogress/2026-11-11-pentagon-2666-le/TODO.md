# TODO: Pentagon 2.666 LE

Status 2026-11-12: P0..P2 built and tested; P3 and P4 written, tests in `core/tests/emulator/machines/p2666/`.
See [README.md](README.md).

## Phases

- [x] Owner decisions Q-1...Q-6 (below, 2026-11-11)
- [x] P0: ROM set `data/rom/p2666/`, `data/configs/p2666/unreal.ini`, `MM_P2666` row, factory case, decoder with
      Gluk mapping, power-on upload, read-only ROM pages
- [x] P1: full decoder (DOS machine, `xx77`, two maps, `xFF7`, `x7F7`, `xxBE`, `xxBF`, `7FFD` / `EFF7` rules and
      quirks, FE flag), turbo 1 / 4 / 8 with the I/O slowdown, INT; Beta Disk path A
- [x] POC 051: the board's VHDL in `ghdl` as the timing oracle (CPU clock, I/O stretch, INT, video fetch)
- [x] P2: video modes, geometry, palette, border, text font (FLASH: dropped by the owner 2026-11-12, "not worth
      it"; the shared ZX renderer has none)
- [ ] P3: CMOS / RTC (64 MCU cells, `[P2666] CmosFile`), SD (Z-Controller, `sd.zc`), IDE (even registers read FE),
      mouse, joystick, sound (two YM2149 cores, Covox, beeper in one wrapping 8-bit DAC sum, `P2666Audio`): written,
      tests `P2666Devices_Test`; to verify with the test software
- [ ] P4: Beta Disk on the RAM disk (`P2666BetaDisk`, default; `[P2666] BetaDisk=wd1793` = path A), UART
      (`P2666Uart`, machine serial port `p2666-uart`): written, tests `P2666BetaDisk_Test`, `P2666Uart_Test`
- [ ] Test software, headless (`P2666SoftwareProbe_Test.DISABLED_Run`): Gluk + TR-DOS from the RAM disk, Pentagon
      timing tests (`tests.trd`, `stime.scl`), AY / TurboSound tests, FATALL on SD, NedoOS `osp26sd.$C`
- [ ] P5: TTD (`PeripheralId` >= 66: sound, Beta Disk, UART, CMOS; the AY state blob left the slot list with the
      socket), slots refdata (writes reach the ZX-BUS only with A2 = A1 = 0, `ebl`: a Covox `#FB` card is dead, a TS
      card loses to the board), snapshot rules, automation planes, Qt menu,
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
