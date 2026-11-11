# Pentagon 2.666 LE

Created: 2026-11-11 · Status: **design** (nothing built) · PLAN #121

A new machine model for the Pentagon ver. 2.666 LE (koe, 2007-2025; FPGA EP2C8 + LPC2368 MCU, microATX board):
a Pentagon-1024 with the ATM Turbo 2 shadow layer (`xx77`, `xFF7` dispatcher, ATM video modes, palette), the Evo
BaseConf additions (`x7F7`, `xxBE`, `xxBF`), 2 MB, 3.5 / 14 / 28 MHz, Gluk CMOS, Z-Controller SD, Nemo IDE, Kempston
mouse, TurboSound, Covox, a 16550 UART and a hardware Beta Disk emulator over a RAM disk.

Upstream: <https://github.com/koe1234/pentagon_2_666_le>, read at revision `e808402` (2025-04-20): VHDL of the FPGA,
the MCU firmware, the ROM set, the user manual `tfm_le.pdf` (20.04.2025). The clone lives in the worktree's
`scratch/pentagon-2666-le/src/repo` and is not part of the repository.

| Document | What it holds |
|---|---|
| [hardware-reference.md](hardware-reference.md) | Clocks, frame, INT, memory map, dispatcher, DOS logic, every port (write table, read priority), devices, video modes, palette, reset state, manual-vs-VHDL list D-1...D-12; every fact with its VHDL line |
| [design.md](design.md) | How it fits unreal-ng: model row, decoder, devices, clock, video, checklist, phases P0-P5, the VHDL oracle |
| [test-plan.md](test-plan.md) | Tests per layer: decoder, CPU programs, devices, firmware boot, automation, VHDL co-simulation |
| [TODO.md](TODO.md) | Status marker, phase checklist, owner decisions |

## What we must build (summary)

1. **Model** `MM_P2666` ("Pentagon 2.666 LE", 2048 K), config folder, ROM set `data/rom/p2666/` (Gluk, TR-DOS
   6.11E, FATALL, logo are new; BASIC 128 / 48 equal files we have), power-on upload of the ROMs into
   write-protected RAM pages `0x48..0x4C`.
2. **Decoder** `PortDecoderP2666`: the port tables of HR §4 with their overlaps and quirks; the DOS state machine
   (enter at `3Dxx` with `7FFD.4`, leave at M1 `>= 4000` unless CP/M or `BF.0`); two dispatcher maps by `7FFD.4`
   with 10-bit window registers; Pentagon-1024 `7FFD` rules with the 128 mode of `EFF7.2`; readback `xxBE`.
3. **Clock**: base 3.5625 MHz (49.70 Hz frame of 71680 T), turbo 14.25 / 28.5 MHz (ratio 1 / 4 / 8), power-on at 28 MHz, every I/O
   cycle at the base clock (new wait overlay), INT 32 CPU clocks.
4. **Video**: ZX, Pentagon 16c, ATM 320 x 200, 640 x 200, 80 x 25 with existing renderers inside a Pentagon raster;
   the P2.666 palette (16 entries, 4 bits per component from `D` and `A8..A15`, index = border colour, kept over
   reset); 16-colour border.
5. **Devices** through their managers: ZC SD with DOS-dependent ports, Nemo IDE, Gluk CMOS with its constant
   cells, Kempston mouse / joystick, TurboSound at 1.8387 MHz, Covox, UART; Beta Disk first as the standard WD1793,
   then the board's RAM-disk emulator.
6. **Everything else a model needs**: TTD state, snapshot rules, slots refdata, automation planes, Qt menu, recipe.

How to test it: [test-plan.md](test-plan.md). The decoder and paging are covered row by row from the hardware
tables; timing (INT position, I/O slowdown, border / multicolor offsets, BDI timing) is settled by running the
upstream VHDL itself in `ghdl` as an oracle; the firmware layer is Gluk menu, TR-DOS, FATALL on SD and IDE, and
NedoOS from SD.

## Findings in the sources worth knowing before coding

- The machine powers on at 28 MHz (`xx77 = 0xAB`) and with DOS locked on (`cpm = 0`): Gluk has to release it.
- `OUT (FE), 0x20` behaves the opposite way to the manual (D-2); a locked 128-mode `7FFD` maps page 32 + n (D-3).
- The FPGA read mux answers every even port with the keyboard, so the even Nemo IDE registers never answer (D-5);
  FATALL uses them. Emulated as on the board: IDE reads do not work; the fix is a commented-out recipe.
- The SD "card present" read is not implemented (D-4); A goes right and C left in the stereo mix (D-6).
- In DOS, `xx77`, the dispatcher ports and the palette port also write Beta Disk registers; `BFF7` is both CMOS
  data and dispatcher window 2.

## Decisions

The owner decided on 2026-11-11: the board as built, nothing improved; board bugs copied, fixes only as
commented-out recipes. Details in [TODO.md](TODO.md#decisions-owner-2026-11-11).

The clocks are the HDL's: 12 MHz MCU quartz -> 72 MHz -> PWM 18 MHz into the FPGA -> PLL x19/3 = 114 MHz, CPU
114 / 32 = 3.5625 MHz, 114 / 8 = 14.25 MHz, 114 / 4 = 28.5 MHz; "3.5 / 14 / 28 MHz" are the nominal names.
