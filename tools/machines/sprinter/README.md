# Sprinter tools

Tools for the Peters Plus Sprinter Sp2000 machine
([docs/inprogress/2026-09-28-sprinter](../../../docs/inprogress/2026-09-28-sprinter/README.md)).

| Folder | What it does |
|:--|:--|
| [dcp-table/](dcp-table/README.md) | decodes the port table the BIOS writes to RAM page `#40` (from a ROM image, a page dump or the BIOS `DCP.ASM` sources) and compares tables |
| [bios-build/](bios-build/README.md) | builds a byte-reproducible 256 KB BIOS image from the community Sprinter-BIOS sources with sjasmplus ([bios-versions.md](../../../docs/inprogress/2026-09-28-sprinter/bios-versions.md)) |
| [mame-capture/](mame-capture/README.md) | runs MAME's `sprinter` driver headless and captures reference data (port table, frames, INT timing, port traces, loader count) into `testdata/machines/sprinter/reference/` |

`dcp-table` and `bios-build` need Python 3 (`bios-build` also sjasmplus). `mame-capture` needs the
MAME subset build described in [tools/verification/coemu/mame/README.md](../../verification/coemu/mame/README.md).
