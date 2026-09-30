# Emulator debugger survey (2026-09-28)

What other emulators' debuggers can do, one document per emulator, all on one
template: a capability registry table first, then detailed sections, notable
ideas and gaps. Every fact cites source lines (or, for closed-source
emulators, the reverse-engineering evidence).

Used by the [debugger family](../2026-09-28-debugger-family/) documents,
chiefly the [comparative analysis](../2026-09-28-debugger-family/comparative-analysis.md).

| Emulator | Platform | Document | Stand-out |
|---|---|---|---|
| Xpeccy+ | ZX | [xpeccy-plus-debugger.md](xpeccy-plus-debugger.md) | expression language with `RAY`, access variables, physical-cell breakpoints, heat map |
| Unreal Speccy | ZX | [unreal-speccy-debugger.md](unreal-speccy-debugger.md) | the monitor unreal-ng descends from; RPN conditions, `Alt+F9` beam screen, ripper |
| TS-Labs Unreal | ZX-Evo TSConf | [tslabs-unreal-debugger.md](tslabs-unreal-debugger.md) | per-line bus bars, step renders to the T-state, TSConf register panel |
| ZXMAK2 | ZX | [zxmak2-debugger.md](zxmak2-debugger.md) | debuggers as plug-in devices, JIT conditions (Adlers) |
| Kozynax | ZX | [kozynax-debugger.md](kozynax-debugger.md) | one model, three renderers (WinForms, SDL cell UI, terminal) |
| zxsp | ZX | [zxsp-debugger.md](zxsp-debugger.md) | breakpoint bits in memory cells, decaying heat map, run to T-state |
| Spectral | ZX | [spectral-debugger.md](spectral-debugger.md) | minimal overlay; run-ahead snapshots |
| ZX-M8XXX | ZX (web) | [zx-m8xxx-debugger.md](zx-m8xxx-debugger.md) | provenance, diff runs, behavior profiler, self-describing automation API |
| Zero | ZX | [zero-emulator-debugger.md](zero-emulator-debugger.md) | RZX-aware debugging, register-value breakpoints |
| 8BitAnalysers | ZX (+C64, CPC) | [8bitanalysers-debugger.md](8bitanalysers-debugger.md) | per-byte reader/writer PCs, pixel→code, scanline event strip |
| jnext | ZX Next | [jnext-debugger.md](jnext-debugger.md) | run to end of scanline, raster replay, partial-decode port breakpoints |
| Spectaculator | ZX (closed) | [spectaculator-debugger.md](spectaculator-debugger.md) | page-bound breakpoints, hit-count rules, one-click port/keyboard breakpoints, pixel breakpoints in the screen inspector |
| ZXSpin | ZX (closed) | [zxspin-debugger.md](zxspin-debugger.md) | assembler IDE with debug-run and restore, non-stopping profiler breakpoints, command box |
| MAME | multi | [mame-debugger.md](mame-debugger.md) | one expression language everywhere, CRC-bound comments, memory taps |
| Mesen2 | NES/SNES/GB/SMS… | [mesen2-debugger.md](mesen2-debugger.md) | event viewer, predictive breakpoints, access stamps, CDL |
| FCEUX | NES | [fceux-debugger.md](fceux-debugger.md) | CDL as a primitive, trace pipeline, scanline-timed viewers |
| BizHawk | multi | [bizhawk-debugger.md](bizhawk-debugger.md) | memory domains, RAM search, TAStudio branches |
| WinUAE | Amiga | [winuae-debugger.md](winuae-debugger.md) | per-slot DMA recorder and overlays, channel-filtered watchpoints |
| vAmiga | Amiga | [vamiga-debugger.md](vamiga-debugger.md) | bus-owner array as recorder, logic-analyzer line view, beam traps |
| DeZog | VS Code front end | [dezog-debugger.md](dezog-debugger.md) | DZRP backend contract, source-comment annotations, Z80 unit tests |
| unreal-ng | ZX (this project) | [unreal-ng-debugger.md](unreal-ng-debugger.md) | TTD from every surface, MCP, ten remote surfaces; with status (shipped / designed / planned) per capability |

Open internals of the two closed-source emulators: [ida-re-questions.md](ida-re-questions.md) (for the IDA Pro reverse-engineering agent).
