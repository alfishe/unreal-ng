# Profi v5.06 netlist helpers

Small helpers used to trace the v5.06 processor board
([research-profi-v5-open-items.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/research-profi-v5-open-items.md)).

| File | What it does | Run |
|:--|:--|:--|
| `part.py` | prints each pin of the named parts with its net and every other pin on that net | `python3 part.py <netdump.txt> DD75 DD33` |
| `dis.py` | a linear Z80 disassembly of a ROM range (used to read the BIOS reset path) | `python3 dis.py <rom> <offset> <addr> <count>` |

`netdump.txt` is written by [../waitmodel/parsenet.py](../waitmodel/README.md) from the P-CAD netlist, which is kept
with the analysis materials outside the repository. `dis.py` needs `pip install z80dis`.

Example: `part.py netdump.txt DD75` shows the flip-flop that holds `/BLOCK` (D = `/FD`, clock = `/M1`, reset =
`/ONOFF` from the CP/M switch).
