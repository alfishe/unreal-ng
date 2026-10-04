# PROFI-XT keyboard controller: 8035 simulator

A small Python model of the Profi's PROFI-XT keyboard controller, written for the keyboard research
([research-profi-keyboard.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/research-profi-keyboard.md)). It is
the reference the emulator's MCS-48 core and `ProfiXtKbc` were checked against, and the way the reconstructed
firmware image ([data/rom/profixt/](../../../../data/rom/profixt/README.md)) was verified: with the 5 patched bytes,
every key gives the expected matrix positions.

| File | What it does |
|:--|:--|
| `mcs48.py` | MCS-48 (8048 / 8035) disassembler: `python3 mcs48.py <rom> <start hex> <length hex>` |
| `sim.py` | the 8035 simulator plus the board from the schematic `PROFI-XT.PDF`: P1 = Z80 A8-A15, MOVX with address bit 7 = 0 writes the output latch (KD0-KD5, reset), MOVX with bit 5 = 0 ends the Z80's WAIT, T1 = the WAIT flip-flop, T0 = the inverted keyboard data bit, /INT = the keyboard clock. `Board.sendByte(code)` sends one XT frame (start bit 1, 8 data bits LSB first); `Board.readFE(high)` is a Z80 `IN` of `#hhFE` and returns the answer and the number of MCU instructions run while the Z80 waited |
| `keymap.py` | every set-1 make code 01-58 through the firmware: the matrix while the key is held and after its break |

```bash
cd tools/machines/profi/xtkbd
python3 keymap.py                       # the image in data/rom/profixt
python3 keymap.py path/to/another.rom   # a re-dump, or the original dump (receives no key)
python3 mcs48.py ../../../../data/rom/profixt/profi-xt-v1.27.rom 0 80
```

Simplifications against the emulator (`core/src/emulator/io/keyboard/profixtkbc.cpp`): the timer instructions are
no-ops (the firmware does not use the timer); the /INT pulse ends when the interrupt is taken; time is counted in
instructions, not in machine cycles of the 8 MHz crystal. The key map agrees with the emulator
(`ProfiXtKbc_Test.TheKeyMapOfTheResearch`, `TheTableAgreesWithTheFirmware`).

Only Python 3 is needed.
