# Plugins

Extensions that are loaded **by other programs**: their host is Wireshark, ImHex, IDA, Ghidra or an IDE, not the
emulator. Stand-alone programs a user runs go to [`utilities/`](../utilities/README.md); internal build, test and
verification scripts stay in [`tools/`](../tools/).

| Directory | Host | What it does |
|:--|:--|:--|
| [`wireshark/`](wireshark/README.md) | Wireshark (extcap) | Lists running emulators and their network adapters, captures their traffic |
| [`imhex/`](imhex/) | ImHex | Patterns for TRD, SCL and MFM disk images |
| [`idapro/`](idapro/) | IDA Pro | Script that guides disassembly with the emulator's memory access counters |

Layout decision: [source-layout.md](../docs/inprogress/2026-10-19-streamdeck-integration/source-layout.md#2-grouping-by-role).
