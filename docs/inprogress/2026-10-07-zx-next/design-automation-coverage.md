# ZX Spectrum Next: automation-plane coverage for the debugger (TDD)

**Date:** 2026-10-10 · part of [README.md](README.md) · extends [design-automation.md](design-automation.md) (the report table) · pattern:
[design-nextreg-journal.md](design-nextreg-journal.md) (one `DeviceState` function per report, thin adapters per plane)

The Next debugging so far was done by guesswork, port traces and reading registers one by one (DMA writing into ROM, the tilemap reading
bank 7 as 16K, a card that answers as SDSC). Each of those was visible in one report that did not exist. This document lists the reports and
controls that make the Next as inspectable as the TS-Conf and Sprinter, and the tests that are written **first**.

## 1. Where we are (2026-10-10)

| Exists on the WebAPI | Plane coverage |
|:--|:--|
| `state/next` (machine, mmu, ctc, interrupts, divmmc, spi, rtc), `state/next/regs` | WebAPI, MCP (`inspect_state`), CLI (`state next`), Lua, Python |
| `state/next/reg-journal`, `POST next/reg-journal` | all planes ([design-nextreg-journal.md](design-nextreg-journal.md)) |

| Missing (planned in `design-automation.md`, absent) | Why it matters |
|:--|:--|
| `dma` | which port/direction/mode/prescaler a demo programmed; the counters and the status byte |
| `copper` | the 1K-word list as disassembly, the PC, the control mode |
| `sprites` | the 128 attributes decoded (x, y, pattern, palette offset, flips, scale, anchor), the visible count, the pattern memory |
| `palette` | the 8 palettes (ULA 1/2, Layer 2 1/2, sprites 1/2, tilemap 1/2) with 9-bit values and the selected ones |
| `video` | layer order and enables, resolutions, scroll/clip windows, ULA/Timex/LoRes, Layer 2 bank/offset, tilemap config (the NR #6B-#6F bits), raster position |
| `ports` | `describe(port, read|write)`: which device answers, the internal-port enable word |
| `nextreg` read / write | a test or a debugger writes a register through a chosen door (`nextreg` instruction semantics or port pair) and reads any register without side effects |
| layer capture | the composed frame is all that can be captured; "which layer drew this pixel?" needs per-layer images |
| Qt panels, breakpoints on NextREG writes, DeZog/GDB wiring, TTD of NEXTREG writes | later phases (section 7) |

## 2. Requirements

| # | Requirement |
|:--|:--|
| R1 | Each report is one `DeviceState::Next<Name>(context, args)` function returning a `StateNode` tree; JSON and text come for free. No plane formats its own output |
| R2 | **Every plane**: WebAPI + OpenAPI manifest, MCP (`inspect_state` aspect), CLI (`state next <name>`), Lua, Python. The same name in each (`next_dma`, `next_copper`, ...) |
| R3 | Read-only reports have no side effects on the machine (no register read strobes, no DMA read-sequence advance, no FIFO pops). Where the hardware read has a side effect (DMA read sequence, copper address auto-increment), the report reads the **model's** state, not the port |
| R4 | Reports work on a machine without a running program (power-on state) and during a run (any thread: through the same snapshot-by-lock path the other `state/next` reports use) |
| R5 | A report names its fields as the hardware documents them (NR numbers, the VHDL names where there is one) so a reader can check the RTL |
| R6 | Writes: `POST next/nextreg {"reg":N,"value":V,"door":"nextreg"|"port"}` goes through the board's single write choke point (`NextBoard::Write`), so the journal sees it with `source=nextreg` / `port` and a journal event carries `pc` of the current instruction; rejected with 409 when the machine is not a Next |
| R7 | Docs: `command-interface.md` (the common ECI abstraction - the "common interface" document) lists the new commands; `webapi`, `cli`, `lua`, `python` interface docs each get the section; `.recipe/machines/next.md` the recipes; `openapi` manifest (`openapi_state.inc` and the MCP tool schema) the paths and parameters |
| R8 | Every report has a golden-output test (the text form) so a change of the format is a visible diff |

## 3. Report contents (first pack)

**`next_dma`** - `mode` (`zxn`|`z80`), `enabled`, `transferring`, `waiting`, `end_of_block`, `auto_restart`, `ce_wait`, `burst`
(`continuous|burst|byte`), `prescaler`, `read_mask`, `read_seq`, `status` (the byte the read sequence would return first), ports
`a` / `b` (`address`, `type memory|io`, `step inc|dec|fixed`, `timing`), `direction a_to_b|b_to_a`, `block_length`, `counter`, `src`, `dst`, `holds_bus`,
`dma_delay` (NR #CC-#CE state), `last_run_28`, plus `interrupt_enables` (#CC/#CD/#CE).

**`next_copper`** - `control` (NR #62 mode), `address` (NR #61/#62 pointer), `pc` (running position), `running`, `instructions` (`from`,
`count`, default the first 64 and the ones around `pc`): `index, word, op (wait|move|halt|nop), reg, value, hpos, vpos`. The 2048-byte list as hex in `raw` on request.

**`next_sprites`** - `enabled`, `over_border`, `clip`, `visible_count`, `sprites` (`from`, `count`, default all visible): `index, x, y, pattern, palette_offset,
x_mirror, y_mirror, rotate, scale_x, scale_y, anchor/relative, 4bit, visible`; `pattern_memory` summary (16K, how many non-zero).

**`next_palette`** - `selected` (NR #43 palettes and auto-increment), per palette `index, rgb9, priority bit`, `transparent` indexes; filtered by
`palette=` and `range=`.

**`next_video`** - `layer_order` (NR #15), `ula {enabled, mode: standard|timex|hires|hicolour|lores, shadow, scroll, clip}`, `layer2 {enabled, resolution, bank, offset,
palette_offset, clip}`, `tilemap {enabled, columns, attrs, mode512, on_top, text, map_base+bank, tile_base+bank, scroll, clip}`, `sprites_over`, `transparency`
(NR #14/#4A/#4B), `raster {hc, vc, frame_t, int_line}`, `timing {family, hz50_60}`.

**`next_ports`** - `describe(port, access)`: `device`, `decoded_by`, `enabled_by` (the NR #82-#85 bit), `side_effect`; and the whole enable word.

**`next_nextreg`** - read one register (`reg=`) or all with names; write as in R6.

**layer capture** (phase C) - `GET /capture/screen?layer=composed|ula|layer2|sprites|tilemap|border` returns the layer's own 320x256 image (transparent as alpha).

## 4. Surfaces

| Plane | Read | Control |
|:--|:--|:--|
| WebAPI | `GET /api/v1/emulator/{id}/state/next/{dma,copper,sprites,palette,video,ports,nextreg}` with the arguments above as query parameters | `POST /api/v1/emulator/{id}/next/nextreg` |
| OpenAPI | one path + parameter list + response schema per report in `openapi_state.inc` | the POST |
| MCP | `inspect_state` aspects `next_dma`, `next_copper`, `next_sprites`, `next_palette`, `next_video`, `next_ports`, `next_nextreg` with params prefixed `nr_` | `invoke_api` for the POST |
| CLI | `state next dma|copper|sprites|palette|video|ports|nextreg [args] [--json]`; `next nextreg <reg> <value> [port]` | |
| Lua | `next_dma()`, `next_copper({from=,count=})`, `next_sprites(...)`, `next_palette(...)`, `next_video()`, `next_ports(port, "r")`, `next_nextreg(reg)`; `next_nextreg_write(reg, value, door)` | |
| Python | `emu.next_dma()`, ... same names | |

## 5. Tests (written first)

| Test | What |
|:--|:--|
| `NextDmaReport_Test` | the power-on report; after programming WR0-WR6 through `#6B` the fields equal the programmed values; a running burst shows `waiting` / `holds_bus` false; the report does **not** advance the read sequence (R3) |
| `NextCopperReport_Test` | a list of WAIT/MOVE/HALT words round-trips through the disassembler; `pc` follows the run; control modes |
| `NextSpritesReport_Test` | attributes written through `#57`/`#5B` decode to the documented fields (x 9 bit, scale, relative sprites, 4-bit); `visible_count` |
| `NextPaletteReport_Test` | 9-bit entries written through NR #40/#41/#44 read back; selected palette follows NR #43 |
| `NextVideoReport_Test` | each layer-order value of NR #15, timex modes via `#FF`, Layer 2 resolutions, tilemap bits (`#6B`), raster position follows the frame T |
| `NextPortsReport_Test` | `describe(0x6B)`, `describe(0x253B, write)`, a disabled internal port (NR #82 bit) reports `enabled_by` |
| `NextNextRegWrite_Test` | a write through each door lands in the journal with the right `source`; the value is visible in `regs`; a non-Next machine answers 409 |
| golden text (R8) | one `.txt` per report under `core/tests/automation/golden/next/` |
| surfaces | WebAPI handler tests (the journal tests are the model: route, query parsing, status codes, JSON shape), CLI processor tests, MCP aspect tests, Lua and Python smoke through the embedded interpreters; an OpenAPI test that every route in `emulator_api.h` for `state/next/*` has a manifest path |
| regression stories | each past find as a report assertion: DMA into ROM is visible as `dst` in the ROM range; bank-7 tilemap base shows `tilemap.map_bank=7` with offset `#2000`; the card type `sdhc=true` in `next_spi` |

## 6. Phases

| Phase | Content | Exit |
|:--|:--|:--|
| A | `dma`, `video`, `palette`, `ports`, `nextreg` read/write on all planes, docs, manifest | tests green on master merge; full `core-tests`; gcc-16 -O3 clean. **Done 2026-10-10** (branch `next-automation`) |
| B | `copper`, `sprites` (the larger reports), layer capture | same. **`copper` and `sprites` done 2026-10-10; layer capture not done** (see TODO.md) |
| C | Qt panels (NextREG table, MMU map, layers, sprites, copper, DMA/CTC/IM2, M1 button), breakpoints on NextREG writes, DeZog/GDB wiring, TTD of NEXTREG writes | later; see [phases.md](phases.md) N12 |

## 7. Documents to update (with the code)

- `docs/emulator/design/control-interfaces/command-interface.md` (the common ECI document: command table), `webapi-interface.md`, `cli-interface.md`, `lua-interface.md`, `python-interface.md`
- `core/automation/webapi/src/openapi/openapi_state.inc` and the MCP tool schema (`mcp-tools.cpp` aspect list, parameter descriptions)
- `.recipe/machines/next.md` (a section per report with real output) and `.recipe/README.md`
- `design-automation.md` (the table gets "done" marks), `TODO.md`, `phases.md`
- `docs/emulator/environment-variables.md` only if a switch is added (none planned)

## 8. Open questions

1. Per-layer capture: render each layer in the compose pass with a flag (cost on the hot path when off must be one branch), or re-run the layer renderer on demand from the register state (no hot path cost, but a second code path). Proposal: on demand.
2. The sprites report on a 128-sprite table can be 10 KB of JSON: default to visible sprites only, page with `from`/`count`.
3. NextREG write over a running emulator: apply at the next instruction boundary through the same command queue the keyboard uses, never from the HTTP thread directly.
