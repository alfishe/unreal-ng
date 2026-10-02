# ZX Profi tools

Tools from the Profi v3 / v5 work ([docs/inprogress/2026-10-01-profi-v3-v5](../../../docs/inprogress/2026-10-01-profi-v3-v5/README.md)).
Both read hardware PROMs and turn them into numbers the emulator uses.

| Folder | What it does |
|:--|:--|
| [syncprom/](syncprom/README.md) | decodes the video sync PROM (573RF2, 2K) of a Profi board into its frame: T-states per line, lines, frame length, INT position and length, border sizes |
| [profidecoder/](profidecoder/README.md) | turns the port decoder PROM (K556RT4, 256 x 4) of either board into a port map per mode, and compares it with unreal-ng's `PortDecoder_Profi` |

Both need only Python 3. They print text and write nothing.
