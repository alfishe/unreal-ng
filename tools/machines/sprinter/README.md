# Sprinter tools

Tools for the Peters Plus Sprinter Sp2000 machine
([docs/inprogress/2026-09-28-sprinter](../../../docs/inprogress/2026-09-28-sprinter/README.md)).

| Folder | What it does |
|:--|:--|
| [dcp-table/](dcp-table/README.md) | decodes the port table the BIOS writes to RAM page `#40` (from a ROM image, a page dump or the BIOS `DCP.ASM` sources) and compares tables |
| [bios-build/](bios-build/README.md) | builds a byte-reproducible 256 KB BIOS image from the community Sprinter-BIOS sources with sjasmplus ([bios-versions.md](../../../docs/inprogress/2026-09-28-sprinter/bios-versions.md)) |
| [demo-runner/](demo-runner/README.md) | boots Flex Navigator from a hard-disk image through the WebAPI, starts programs one by one and sorts them into running / static / waits-for-int / exited, with screenshots and a contact sheet |
| [test-mod/](test-mod/README.md) | writes the generated test MOD (three known notes, fixed tempo) for ProPlay on the GS behind the ISA ZX-bus adapter, and compares two recordings of it (pitch, timing, envelope, waveform) |
| [mame-capture/](mame-capture/README.md) | runs MAME's `sprinter` driver headless and captures reference data (port table, frames, INT timing, port traces, loader count) into `testdata/machines/sprinter/reference/` |

`dcp-table`, `bios-build` and `demo-runner` need Python 3 (`demo-runner` also mtools) (`bios-build` also sjasmplus). `mame-capture` needs the
MAME subset build described in [tools/verification/coemu/mame/README.md](../../verification/coemu/mame/README.md).
