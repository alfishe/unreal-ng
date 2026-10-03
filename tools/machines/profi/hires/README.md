# Profi hi-res (DS80) timing tools

Scripts behind the hi-res timing of both Profi boards
([research-profi-hires-timing.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/research-profi-hires-timing.md)).

| File | What it does | Run |
|:--|:--|:--|
| `ds80frames.py` | decodes the upper (DS80) half of each sync PROM with [../syncprom/profisync.py](../syncprom/README.md): ticks per line, lines, INT position and length, the paper window, and the same in CPU T at each board's DS80 clock | `python3 ds80frames.py <repo root> <folder with VR*.ROM>` |
| `ds80frames-output.txt` | its output for the five PROMs | |
| `ds80waitsim.py` | the v5.06 DRAM arbiter and CPU WAIT path in DS80: three clocks (ZQ3 for the CPU and DRAM, the asynchronous 12 MHz video clock, the ROM one-shot); an extension of [../waitmodel/v5waitsim.py](../waitmodel/README.md) | imported by the scripts below |
| `ds80rule.py` | the wait count against where T1 falls relative to a video request edge: the rule the emulator uses | `python3 ds80rule.py [zq3 MHz] [1 = turbo]` |
| `ds80summary.py`, `ds80sens.py` | loop speeds against the forum's speed-test figures; sensitivity to the assumed gate delays | `python3 ds80summary.py`, `python3 ds80sens.py` |
| `*-output.txt` | their outputs | |

Python 3 only. The PROM dumps are the speccy4ever `rom/VR*.ROM` files kept with the analysis materials.
