# ZX Profi tools

Tools from the Profi v3 / v5 work ([docs/inprogress/2026-10-01-profi-v3-v5](../../../docs/inprogress/2026-10-01-profi-v3-v5/README.md)).
They read hardware PROMs and schematics and turn them into numbers the emulator uses.

| Folder | What it does |
|:--|:--|
| [syncprom/](syncprom/README.md) | decodes the video sync PROM (573RF2, 2K) of a Profi board into its frame: T-states per line, lines, frame length, INT position and length, border sizes |
| [profidecoder/](profidecoder/README.md) | turns the port decoder PROM (K556RT4, 256 x 4) of either board into a port map per mode, and compares it with unreal-ng's `PortDecoder_Profi` |
| [waitmodel/](waitmodel/README.md) | a gate-level model of the v5 board's DRAM arbiter: when the board holds the CPU for the video (the v5 video WAIT) |
| [netlist/](netlist/README.md) | helpers to read the v5.06 netlist (a part's pins and nets) and to disassemble a ROM range |
| [turbomodel/](turbomodel/README.md) | the v3 board's turbo wait rule, checked against a measured frame on a real board; the v3.2 port decoder dump |
| [pqdosimage/](pqdosimage/README.md) | cuts the 3 MB PQ-DOS test hard disk image out of the 2 GB Karabas Pro image (root files, `DN`, `DOS` kept; the games and demos dropped) |
| [xtkbd/](xtkbd/README.md) | an 8035 simulator of the PROFI-XT keyboard controller board, running its firmware: the key map, the Z80 wait per read |

All need only Python 3 (`turbomodel/crop.py` also needs PyMuPDF, `netlist/dis.py` z80dis). They print text; only `waitmodel/parsenet.py`
writes files.
