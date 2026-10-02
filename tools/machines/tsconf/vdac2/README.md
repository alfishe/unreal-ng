# VDAC2 tools

Tools for the TS-Conf VDAC2 card, an FT812 graphics chip on the IDE connector
([docs/inprogress/2026-10-01-tsconf-vdac2](../../../../docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-tdd.md)).

| Tool | What it does |
|:--|:--|
| [extract-ft81x-rom.py](extract-ft81x-rom.py) | Extracts the FT81x ROM image (fonts 16-34, chip addresses 0x1E0000-0x2FFFFF) from a Bridgetek EVE emulator DLL, validates its font table and glyphs, and writes the 1152 KB image unreal-ng loads from `[VDAC2] RomImage` (default `rom/ft81x.rom`) |

Usage:

```bash
python3 tools/machines/tsconf/vdac2/extract-ft81x-rom.py bt8xxemu.dll -o rom/ft81x.rom
python3 tools/machines/tsconf/vdac2/extract-ft81x-rom.py bt8xxemu.dll --list   # only list the copies found
```

The DLL comes with the Bridgetek EVE Emulator (https://github.com/Bridgetek/EVE_Emulator,
`bin/`) or with TS-Labs Unreal (https://github.com/tslabs/zx-evo-unreal, `Unreal/cfg/`). The
image is third-party: it is never committed. Needs only Python 3.
