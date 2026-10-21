# Utilities

Stand-alone programs and scripts for users: they run on the user's files or against a running emulator. Extensions
loaded by other programs go to [`plugins/`](../plugins/README.md); internal build, test and verification scripts stay
in [`tools/`](../tools/).

| Directory | What it does |
|:--|:--|
| [`diskconverter/`](diskconverter/README.md) | Converts disk images (SCL, TRD, FDI, UDI, TD0) |
| [`diskinfo/`](diskinfo/README.md) | Inspects disk images: geometry, file systems, protection hints, dumps |
| [`porttrace/`](porttrace/README.md) | Captures port traces over the WebAPI and converts saved traces (CLI and GUI) |
| [`python/`](python/README.md) | Shared-memory monitors, memory dumps, emulator discovery |
| [`workload-monitor/`](workload-monitor/README.md) | Per-thread CPU workload of running emulator processes |
| [`neogs/`](neogs/) | NeoGS media: packs and checks the flash image, builds SD card images |

Layout decision: [source-layout.md](../docs/inprogress/2026-10-19-streamdeck-integration/source-layout.md#2-grouping-by-role).
