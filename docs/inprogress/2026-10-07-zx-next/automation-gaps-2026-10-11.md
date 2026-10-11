# ZX Spectrum Next: automation gaps against other debuggers and the hardware (2026-10-11)

**Date:** 2026-10-11 · part of [README.md](README.md) · follows [design-automation-coverage.md](design-automation-coverage.md) (phase A, layer capture) ·
pattern: [design-nextreg-journal.md](design-nextreg-journal.md)

Question asked: are all data and methods needed to debug a Next in the automation planes (WebAPI, MCP, CLI, Lua, Python)? This document
compares what we expose with jnext, ZEsarUX, MAME, CSpect (plugins only), DeZog/DZRP and with what the hardware (VHDL 3.02.03, wiki
mirror) makes observable, ranks the gaps and plans the changes. Method and limits are in section 1; nothing here was measured on a running
emulator.

## 1. Sources read, and what could not be read

| Source | Read | Not read |
|:--|:--|:--|
| jnext 0.99.155 (`/Volumes/TB4-4Tb/Projects/emulators/github/jnext`) | `src/debug/`, `src/debugger/` panels, `cli_options.h`, `log.h`, env-var hooks, `doc/design/SCRIPTABLE-DEBUGGER.md` | the subsystem test-plan docs in detail |
| ZEsarUX (`zesarux/src/zrcp/remote.c`, `debug.c`, menus) | the ZRCP help table and the `tbblue-*` handlers, memory zones, expression variables | no ZRCP manual on disk; debug-window bodies |
| MAME `specnext*` device sources | log flags per device, `state_add` of the MMU | MAME's own debugger docs, `spi_sdcard_device` |
| CSpect | only the plugin sources (`mikedailly-CSpectPlugins`) and DeZog's notes | CSpect itself and its readme are not on disk: its debugger, magic breakpoints and logging are **unverified** |
| DeZog / DZRP | `DeZogProtocol.md`, `Usage.md`, our `dzrptypes.h`, the dezogif design | - |
| Hardware | register table, VHDL signals for NR `#02`, `#D8`-`#DA`, `#F0`, `#FF`; our reports and tests | the wiki mirror pages in depth |

## 2. What the others have that we do not (and what they lack)

| Capability | jnext | ZEsarUX | MAME | CSpect / DeZog | unreal-ng today |
|:--|:--|:--|:--|:--|:--|
| Programmatic control plane | none (CLI flags, env vars; scripting is design only) | ZRCP text commands | none | DZRP | WebAPI, MCP, CLI, Lua, Python, DZRP, GDB, ZRCP |
| NextREG read / write | NextREG panel (editable) | `tbblue-get/set-register` | `rR/wR` log lines | NextRegisterViewer plugin, DZRP `GET_TBBLUE_REG` | **yes** (`next_nextreg`, `POST next/nextreg`, the table) |
| NextREG **write breakpoint** | no | no | no | not in local sources | **no** (journal only) - nobody has it, so this is ahead of the field |
| NextREG write log | no | no | per-register log line | no | **yes** (`reg-journal`) |
| Palette / sprite / pattern / clip window | panels | `tbblue-get-palette/sprite/pattern/clipwindow`, memory zones "TBBlue patterns/copper/sprites" | no | sprite viewer, DZRP sprite commands | **yes** and richer (decoded, no side effects) |
| Copper | panel, decoded | copper PC as an expression variable | disassembler + log | CopperDisassembler plugin | **yes** (`next_copper`); copper PC is not usable in a breakpoint condition |
| DMA state | no | no | LOG_UNDOC only | no | **yes** (`next_dma`) |
| Per-layer images, pixel provenance | per-layer screenshots | layer reveal | no | no | **yes** (`capture/next-layers`, `next_pixel`) |
| SD / SPI command log | log channel `sdcard`/`spi` | no | MAME's sdcard device log | no | **no** (counters only) |
| ESXDOS call trace (`RST $08` with name and registers) | **yes** (`esxdos` log, needs `--esxdos-stub`) | no | no | no | **no** |
| DivMMC automap trace | boot-probe env vars, log channel `divmmc` | no | no | no | **no** (current flag only) |
| Magic breakpoint (`ED FF`, `DD 01`) and magic output port | **yes** | `ED FF` | no | `DD 01` (CSpect) | **not checked** - see G9 |
| UART / ESP state | log channel `uart`, ESP-01 emulation | no | UART data log | no | **no, and the device is missing** (ports `#133B`-`#163B` are not decoded) |
| Per-subsystem log masks | `--log-level cpu=trace,dma=...` | no | per-device LOG_ flags | no | partly (generic logging, no Next channels) |
| Profiler by page+PC | **yes** | no | no | Profiler plugin | generic profiler; Next-aware paging not checked |
| Reverse debugging | rewind buffer | no | no | DeZog "lite" | **TTD** (exact, journals NextREG writes only as a tool edit; see G10) |

Read: against the other tools the Next surface is already ahead on structure and reach (every report on five planes, no side effects).
What they have and we lack is **SD/ESXDOS tracing**, **DivMMC automap tracing** and, for jnext, the magic port/breakpoint. Nobody offers a
NextREG breakpoint, and none of them exposes UART state with a working device.

## 3. What the hardware makes observable, and the gap (hardware column in the audit of 2026-10-11)

Exposed fully: NextREG table, MMU slots, DMA, copper, sprites, palettes, video/layers, pixel, ports enable word, CTC, CPU speed.
Partly or missing, ranked by the value to a Next software developer or an emulator author:

| # | Gap | Today | Needs a device first? |
|:--|:--|:--|:--|
| G1 | **SD/SPI**: command log (CMD, argument, R1/token, sector, length, card type SDSC/SDHC, CS state, the engine's pending byte), plus an **ESXDOS call trace** at `RST $08` | counters and `too_fast_accesses` only; "answers as SDSC" was found by guessing | no |
| G2 | **DivMMC automap**: latch state (armed / active / hold), trigger journal (entry address, PC, reason, ROM-3 condition, release by `RETN`/`#1FF8`/reset), CONMEM/MAPRAM changes | current `automapped` flag only | no |
| G3 | **NextREG breakpoints** (write, read, value mask, the three doors incl. copper) | journal polled after the fact; port breakpoints do not see `NEXTREG` | no |
| G4 | **NMI and Multiface**: NMI source (M1 button, DRIVE button, DivMMC, IO trap), in-service state, Multiface active/enabled/invisible, `POST next/nmi` (button) | slot kind in `mmu` and a journal label; the button is a host UI action | no |
| G5 | **Interrupts**: per-source event log with vector; **reset reason** (NR `#02` bits 1:0) as a field and a reset-event log; hardware ID decode (NR `#0F`) | raw masks / values | no |
| G6 | **Audio**: DAC A-D levels, stereo/mono routing and mixer gains (NR `#08`, `#09`, `#06`), turbosound select | generic AY registers only | no |
| G7 | **Input**: joystick modes (NR `#05`, `#0B`), Kempston `#1F`/`#37` and MD pad (NR `#B2`) state; extended keys (NR `#B0`/`#B1`) | `#1F` only; the rest not modelled | partly (device work for NR `#B0`-`#B2`, `#37`) |
| G8 | Small decodes: `ports` report for NR `#86`-`#89` bus enables, NR `#FF` debug LED, Layer 2 `#123B` windows in `mmu`, NR `#D8`-`#DA` IO trap | table rows | IO trap: yes |
| G9 | **Magic breakpoint / magic port** (`ED FF`, `DD 01`, an output port) as in jnext/ZEsarUX/CSpect: needed by the NEX programs written for those emulators | not checked | maybe |
| G10 | **TTD of NEXTREG writes**, DZRP Next extras (`READ_BANK_MEM`, `WRITE_BANK_MEM`, `SET_NEXTREGS`, `READ_MEM_BLOCKS`: seen in the CSpect plugin's enum, to be verified against the spec) | open question 2 of the journal design; not in our `dzrptypes.h` | no |
| D1 | UART / ESP / Pi (ports `#133B`-`#163B`, FIFO, baud, NR `#A8`/`#A9`) | not decoded; TODO "UART skeleton (no peers)" | **yes**: the report follows the device |
| D2 | Kempston mouse (`#FADF`/`#FBDF`/`#FFDF`), ULA+ / Radastan modes | not modelled | **yes** |

Out of scope here (device work, tracked in [TODO.md](TODO.md)): D1, D2. The reports for them are written with the devices, on the same
pattern.

## 4. Plan of changes

Each phase follows the project rules: tests first, one `DeviceState` function per report with thin adapters on the planes
([design-automation-coverage.md](design-automation-coverage.md) R1-R7), no side effects on read, OpenAPI manifest and MCP schema updated in
the same change, zero warnings. Order = value / cost. Phases land one by one on `zx-next` and `master` after their own tests.

| Phase | Content | Core | Planes (names the same everywhere) |
|:--|:--|:--|:--|
| **P1** (G1) | SD/SPI journal (ring, off by default, one branch when off): command, argument, R1, block, frame, T, PC, card; the card type and the SPI engine in the report. **Landed with `SdCardSpi::setTraceListener` (any machine can use it)**. The ESXDOS call trace (`RST $08`: name, registers, result) moves to P2: it needs the DivMMC entry hook | `NextSdJournal` next to `NextRegJournal`; the card's trace listener, wired in `portdecoder_next.cpp` | `state next sd` (report: engine state, card type, CS, pending byte, the journal), `next sd-journal on\|off\|clear`; WebAPI `GET /state/next/sd`, `POST /next/sd-journal`; MCP aspect `next_sd`; Lua/Python `next_sd()`, `next_sd_journal_control()` |
| **P2** (G2) | automap latch state and trigger journal; the ESXDOS call trace (`RST $08` / `DOT` entry with the call name and registers) | `NextDivMmc` exposes `AutomapState()`; journal entries at every map/unmap | `state next divmmc`, `next divmmc-journal ...`; `GET /state/next/divmmc`; aspect `next_divmmc`; `next_divmmc()` |
| **P3** (G3) | NextREG breakpoints: `reg`, `access` (`w`/`r`/`rw`), `door` mask, `value`/`mask`; stop at the instruction boundary of the write (copper: at the line) | breakpoint kind next to port breakpoints; fires from the single write choke point | `POST /breakpoints` kind `nextreg` (OpenAPI), MCP `debug_code`, CLI `bp nextreg ...`, Lua/Python `add_nextreg_breakpoint()` |
| **P4** (G4, G5) | `state next nmi` (sources, in service, Multiface), `POST /next/nmi {source}`; interrupt-source event log; reset reason field | read-only getters on `NextMultiface`, `NextInterruptSource`, `NextBoard` | `state next nmi`, `next nmi <source>`; `next_nmi()`, `next_nmi_trigger()` |
| **P5** (G6, G7, G8) | `state next audio` (DAC levels, routing, mixer), `state next input` (modelled modes only), small decodes | getters on `NextAudio`, the port decoder | `state next audio\|input`; aspects; `next_audio()`, `next_input()` |
| **P6** (G9, G10) | magic breakpoint/port (if the Z80N core allows `ED FF`/`DD 01` as opt-in), DZRP Next extras after checking the spec, NEXTREG writes in the TTD journal | per design | per design; a separate decision of the owner for TTD |

Documents updated **with each phase** (never ahead of the code):

| Document | Change |
|:--|:--|
| `docs/emulator/design/control-interfaces/command-interface.md` | one row per new command in the Next block (CLI / WebAPI / Lua / Python / MCP columns) |
| `docs/emulator/design/control-interfaces/cli-interface.md`, `webapi-interface.md`, `lua-interface.md`, `python-interface.md` | the sections and examples of the new reports (the layout of the existing `reg-journal` entries) |
| `core/automation/webapi/src/openapi/openapi_next.inc` and `openapi_state.inc` | paths, parameters, response schemas (the OpenAPI route test must pass) |
| `core/automation/mcp/README.md`, `mcp-tools.cpp` aspects and parameters | new aspects and `nr_` parameters; MCP live check `tools/verification/mcp/run_all.py` ([MCP testing](../../guidelines/mcp-testing-guidelines.md)) |
| `core/automation/cli/README.md` | command list |
| `.recipe/machines/next.md` | recipes: "what does the card see", "why did the DivMMC page in", "stop at the write of NR x" |
| `docs/inprogress/2026-10-07-zx-next/{README.md,TODO.md,design-automation-coverage.md,esxdos-and-sd.md}` | status marks, links |
| `tools/verification/webapi/src/test_api_next.py` | live checks of the new routes |

## 5. Tests (written first)

- Core: a golden-text test per report (`core/tests/automation/golden/next/`), an event test per journal (SD: `CMD0`, `CMD8`, `CMD17` at sector
  N appears with card type and PC; DivMMC: an automap entry at `#0008` with ROM 3 shows the trigger, `RETN` releases it; ring wrap, clear,
  capacity, off = no cost).
- Planes: WebAPI route test, OpenAPI manifest test, CLI format test (`cli-next-format_test.cpp`), MCP aspect test, Lua and Python smoke.
- Breakpoint (P3): a program writes NR `#07` through `NEXTREG` and through the copper; the breakpoint stops after the first, at the right
  line for the second; a read breakpoint on a stable register; masks.
- Live: `test_api_next.py` against an own unreal-qt instance; MCP `run_all.py`.

## 6. Acceptance

A developer can answer, with one report each and no guessing: *what did the program ask the card* (P1), *why is the DivMMC paged in at this
PC* (P2), *who writes NR x and stop there* (P3), *which NMI is active* (P4), *what does the mixer do* (P5). Every report exists on all five
planes with the same name, has no side effect on the machine, and is described in `command-interface.md`.

## 7. Open questions

1. P6 needs an owner decision: opt-in magic breakpoint opcodes change the Z80N decode (`ED FF`, `DD 01` are NOPs on the hardware).
2. TTD recording of NEXTREG writes was left open in [design-nextreg-journal.md](design-nextreg-journal.md) (question 2).
3. CSpect behaviour is known only from plugin sources; if its binary becomes available, the magic-breakpoint and logging rows get checked.
