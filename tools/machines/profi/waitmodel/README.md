# Profi v5 wait model

A gate-level timing model of the Profi v5.06 board's DRAM arbiter and its CPU WAIT path, in Spectrum mode. It
answers one question: when does the v5 board hold the Z80 for the video, and for how long? The answer is the rule
`ProfiWaitOverlay` uses ([research-profi-v5-wait.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/research-profi-v5-wait.md)).

Every net and pin in the model comes from the v5.06 P-CAD netlist (`profi506-proc-ascii.sch`, from
[zx-pk.ru](https://zx-pk.ru/forums/102-profi.html)). The netlist and its dump are kept with the analysis materials
outside the repository; `parsenet.py` rebuilds the dump from the netlist.

| File | What it does | Run |
|:--|:--|:--|
| `v5waitsim.py` | the model: the 14 MHz master clock, the video counter, the RAS/CAS ring, the slot owner flip-flop, `/REDYT` and the Z80 bus, in steps of 1/8 master clock (8.9 ns). As a script it prints the wait count per instruction for each clock phase | `python3 v5waitsim.py [--dz N] [--setup N]` |
| `examples.py` | the worked examples: steady-state T per instruction for a loop of identical instructions, per phase, paper against border | `python3 examples.py` |
| `examples-output.txt` | its output, with the `VD22` diode fitted (the 5.06 board) | |
| `examples-novd22-output.txt` | the same without the diode (set `VD22 = False` in `v5waitsim.py`) | |
| `lineedge.py` | where the waited accesses start and end within a line, per phase: the paper fetch window's edges | `python3 lineedge.py` |
| `sweep.py`, `sweep2.py` | the wait count against the clock phase and the gate delays (`dz`, `setup`), in paper, border and turbo | `python3 sweep.py [dz] [setup]` |
| `parsenet.py` | parses the P-CAD ASCII netlist into `net.json` and `netdump.txt` (every pin of every net), in the current folder | `python3 parsenet.py profi506-proc-ascii.sch` |

Python 3 only. The scripts import `v5waitsim` from this folder, so run them from here.

## What the model shows

At 3.5 MHz a RAM access waits only in the paper fetch window (192 lines x 128 T): one T on every other T. Which T
that is depends on the board's phase at power-on, and one of the four phases never waits. The border never waits.
A ROM read may wait one T too: the ROM one-shot ends inside the Z80's WAIT setup window, so the model cannot
decide it (the `ROM:` line of the examples; `[PROFI] RomWait=`). With jumper SB8 in its PENTAGON position the board
never waits at 3.5 MHz.
