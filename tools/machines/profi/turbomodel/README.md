# Profi v3 turbo model

Scripts behind the v3.2 board's turbo wait rule and its port decoder check
([research-profi-v3-turbo-floatbus.md](../../../../docs/inprogress/2026-10-01-profi-v3-v5/research-profi-v3-turbo-floatbus.md)).
The rule comes from the MDESK re-trace of the v3.2 schematics. The scripts compute what the rule means for real
loops and check it against a measurement made on a real v3.2 board (the Tact Meter reading, 88208 T per frame).

| File | What it does | Run |
|:--|:--|:--|
| `turbo-model.py` | the wait rule: a RAM memory cycle waits 2 clocks when it starts on an even 7 MHz clock and 3 on an odd one; ROM, I/O and refresh never wait. Prints the clocks per pass and the speed against 3.5 MHz for a set of loops, and the T per frame a Tact Meter would show | `python3 turbo-model.py` |
| `turbo-alt-rules.py` | the same loops under other wait rules. Only the 2 / 3 rule gives the measured 88208 | `python3 turbo-alt-rules.py turbo-model.py` |
| `decode-rt4-ports.py` | decodes the v3.2 interface board's port decoder PROM (U5, 556RT4) into port patterns | `python3 decode-rt4-ports.py <dump.bin>` (the dump: [testdata/machines/profi/decoder/556rt4-v3.2.bin](../../../../testdata/machines/profi/decoder/556rt4-v3.2.bin)) |
| `crop.py` | cuts a region of a schematic PDF page into a PNG (needs PyMuPDF) | `python3 crop.py <pdf> <page> x0 y0 x1 y1 <out.png> [dpi]` |

Python 3 only (`crop.py` also needs `pip install pymupdf`).

## Worked example

`INC DE : JP loop` in RAM: 16 T at 3.5 MHz, 26 clocks in turbo, so it runs 1.23 times faster, not 2 times. A real
v3.2 board shows the same ratio: 88208 / 71680 = 1.2306.
