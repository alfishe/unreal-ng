# ZX Spectrum Next: automation parity

**Date:** 2026-10-07 · part of [README.md](README.md) · pattern: [Sprinter outcome](../2026-09-28-sprinter/automation-outcome.md)

Principle: no Next-only mechanism. Generic aspects (registers, memory regions, ports, media slots, TTD, breakpoints)
learn the Next; a small set of Next reports is added once and exposed through every surface from the same function
in `DeviceState` (the way `sprinterdevicestate.cpp` does).

## 1. Reports (names are proposals)

| Report | Content | Phase |
|:--|:--|:--|
| `next_regs` | NR number, name, value, differs-from-reset | N2; `next_nextreg` (one register without side effects, `changed`) and `POST next/nextreg` (write) **done 2026-10-10** |
| `next_mmu` | 8 slots: page value, resolved source (ROM/RAM/L2/DivMMC/MF/bootrom), r/w flags | N2 |
| `next_ports` | port decoder: describe(port, read/write), enable word state | N2; **done 2026-10-10** ([design-automation-coverage.md](design-automation-coverage.md)) |
| `next_video` | layer enables, order, resolution, scroll/clip, palette selections, raster hc/vc, copper state | N6; **done 2026-10-10** (copper state is in `next_copper`) |
| `next_sprites`, `next_palette`, `next_copper` | attributes/patterns, palettes, copper list disassembly + position | N7; `next_palette` **done 2026-10-10** |
| `next_reg_journal` | who wrote which NextREG, when (frame, T, PC), through the NEXTREG instruction / port #253B / the copper - [design-nextreg-journal.md](design-nextreg-journal.md); WebAPI, OpenAPI, MCP, CLI `state next journal`, Lua, Python | **done 2026-10-09** |
| `next_dma`, `next_ctc`, `next_im2` | device state | N5, N8; `next_dma` **done 2026-10-10**, `next_ctc` / `next_im2` are in `state/next` |
| `next_spi`, `next_divmmc`, `next_uart`, `next_i2c` | device state | N5, N9 |
| `next_boot` | boot ROM / firmware / personality, config mode | N9 |
| audio | existing `audio ay` (three chips) + `next_dac` | N4 |

## 2. Surface matrix

| Surface | How it learns the Next |
|:--|:--|
| CLI | `create NEXT [--next-board ... --next-personality ...]`; `state next ...` subcommands for each report; `media insert sd.next0 <path>`; `ttd ...` unchanged; help text lists the verbs |
| WebAPI + OpenAPI | `POST /emulator/start {"model":"NEXT","next":{"board":"issue4","personality":"plus3"}}`; `GET /emulator/{id}/state/next/{regs,mmu,ports,video,...}`; `POST .../next/nextreg` (write for tests); enum of models already lists NEXT; schemas added to `openapi_schemas.inc`; `models` shows `creatable:true` |
| MCP | `emulator_manage create model NEXT`; `inspect_state aspects:[next_regs,...]`; resource `unreal://machine/next`; `media` tool slots; `time_travel` unchanged; `invoke_api` reaches the rest |
| Lua / Python | `next_regs()`, `next_nextreg_read/write(n[,v])`, `next_mmu()`, sprites/palette/copper getters; `media_insert(slot, path)` |
| Qt | model menu; panels: NextREG table, MMU map, layers viewer (per-layer toggles), sprites viewer, copper viewer, palette viewer, DMA/CTC/IM2; machine menu: speed, 50/60 Hz, buttons (M1, DRIVE), card slots, personality; status line shows CPU speed |
| DeZog / GDB | `ZXNEXT = 4` machine type and bank/sprite commands already in `dzrptypes.h`: wire to the reports (N12) |

Existing generic features that must be checked on the Next: memory region API for 8K pages (region ids per MMU
page and for the 2 MB array), breakpoint on banked address ("page:offset"), port trace/breakpoints (port `#243B/#253B`
reports the register number), symbol manager with bank-aware symbols, screen capture of the composed frame and per
layer (jnext offers per-layer screenshots).

## 3. Recipes

`.recipe/machines/next.md` is created in N2 and extended each phase. Rule: every command in it was run, with real
output. Starting a TTD recording precedes the run section. Sections: create and boards, personality and ROMs,
registers and MMU, video layers, audio, DMA, card and boot, snapshots (NEX), TTD seek. Shared docs:
`.recipe/_common/machines.md` gets a row; `AGENTS.md`/docs model lists updated at N2.
