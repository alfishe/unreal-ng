# Platform debugging: peripherals and extended machines

Status: plan (2026-10-27), on `deck-controllers` after the GS card debugger
([gs-debugger TODO](../2026-09-27-gs-debugger/TODO.md)).

The card debugger gave the General Sound / NeoGS Z80 its own breakpoints, conditions, events and script callbacks.
The machines with more hardware than a 128K need the same depth: ZX Next (MMU, DMA, copper, NextREG, DivMMC),
Sprinter (accelerator, Z84C15, PLD), TS-Conf (DMA, FMaps, raster), ATM / Profi / Evo (extended latches), and the
sound peripherals (MultiSound, NeoGS DMA, the register-driven chips). This plan lists the gaps three surveys found and
the phases that close them. Each phase is tested and committed on its own.

## Principles

- **The fast path stays as it is.** A new observation point costs one predictable branch when nothing listens
  (`if (_hooks) [[unlikely]]`), and the hooks exist only while a breakpoint or a tap asks for them.
- **One breakpoint model.** New kinds go through `BreakpointManager` (`BRK_*`), carry conditions, groups, hit counts
  and notes like the others, and reach every plane (CLI, Lua, Python, MCP, REST, WebSocket).
- **Events are generic.** `BRK_EVENT` is today the GS protocol's; it becomes a machine event space with a source
  (card, DMA, interrupt controller, FDC, journal), so a new device adds kinds, not a new mechanism.
- **Bus masters hit watchpoints.** A DMA, a blitter or an accelerator that writes memory is visible to a write
  watchpoint, marked with its master in the stop (`master: dma`).

## Gaps by platform

### Sound peripherals

| Gap | Where | Phase |
| :-- | :-- | :-- |
| The MultiSound card's GS is not a debug target (a private `SoundChip_GeneralSound`, not in SoundManager) | `multisoundslotcard` | P1 |
| NeoGS ZX-DMA start, GSCFG0 write, flash write, MIDI byte: no events | `soundchip_neogs.cpp` | P2 |
| NeoGS DMA RAM writes bypass the card's watchpoints | `neogsdma.cpp` | P2 |
| No register-write breakpoint for AY / TS-FM / OPL4 / SAA / Covox (`BRK_CHIPREG`: chip, register, value) | chip `writeRegister` paths | P3 |

### ZX Next

| Gap | Where | Phase |
| :-- | :-- | :-- |
| DMA writes and reads do not hit watchpoints | `NextMemory::DmaWrite` (nextmemory.cpp:158) | P5 |
| No events for DMA start / end, interrupts (line, CTC, ULA), NMI (multiface, DivMMC), DivMMC automap, esxDOS entry | Next devices | P5 |
| Conditions know four 16K `PG0`-`PG3`: need `MMU0`-`MMU7`, `NR(x)`, `CPC` (copper PC), `HC` / `VC`, `CPUSPEED` | `breakpointcondition.cpp` | P4 |
| No `--page bank8N` form (8K banks) | breakpoint page parsing | P4 |
| No conditions or taps on NEXTREG, copper, beam, magic breakpoints (refused) | `breakpointmanager.cpp:417`, `:755` | P4 |
| DivMMC and Multiface RAM have no physical pages a breakpoint can name | Next memory map | P5 |
| ZRCP MMU string reports four 16K slots, not eight 8K (suspected) | `zrcpserver.cpp:171-196`, `:1465-1480` | P4 |
| DZRP stop message has no text; GDB `monitor` has no `nextreg` / `mmu` | dezog, gdb | P4 |

### Sprinter, TS-Conf, ATM, Profi, Evo

| Gap | Where | Phase |
| :-- | :-- | :-- |
| TS-Conf DMA writes are invisible to watchpoints | `tsconfdma.cpp:185` | P6 |
| TS-Conf FMaps register writes miss port breakpoints | `portdecoder_tsconf.cpp:1165` | P6 |
| No TS-Conf raster (line / position) breakpoint | TS-Conf video | P6 |
| No TS-Conf interrupt events (frame, line, DMA) | TS-Conf | P6 |
| Sprinter accelerator writes and reads do not stop | `sprintermemory.cpp:270`, `sprintermemory.h:122` | P7 |
| No Z84C15 CTC / PIO / SIO interrupt events | Sprinter | P7 |
| Sprinter journal events (portdecoder_sprinter.cpp:1608) not breakpointable | Sprinter | P7 |
| No breakpoint on Sprinter PLD internal codes (FR-52) | Sprinter | P7 |
| Conditions miss the extended latches: `DFFD`, `1FFD`, `EFF7`, `FF77`, `ROMPGn` per window | `breakpointcondition.cpp` | P4 |
| No FDC command events (WD1793 observer: command, sector, status) | `wd1793` | P8 |

## Phases

| # | Phase | Tests | State |
| :-- | :-- | :-- | :-- |
| P0 | Headless runner (`unreal-qt --headless --script f.lua --exit-on-script`) | `AutomationLua_Test.RunFile*`, `tools/verification/headless/` | done |
| P1 | MultiSound GS as a debug target: the `ms` CPU (`DebugCpuId::MultiSoundGS`), `ParseCpuName`, `ICard::DebugGeneralSound`, `SlotManager` tells the target | `GSCardTarget_Test.TheMultiSoundsGeneralSoundIsTheMsCpu` | done |
| P2 | NeoGS: a `gscfg0` event (every write); DMA writes (SD, ZX-DMA) through the card's page-tied watchpoints. ZX-DMA start / stop are `dma_start` / `dma_done` of module 0 already; a flash write is a write watchpoint on a `romN` page | `GSCardTarget_Test.TheNeoGSDmaWritesHitThePagesWatchpoint`, `NeoGSDma` / `NeoGSZxDma` write reports | done |
| P3 | `BRK_CHIPREG`: a sound chip's register write (chip, register, value mask), every plane | per chip, a ROM tune writes the register | |
| P4 | Condition operands for the extended machines (`MMU0-7`, `NR(x)`, `CPC`, `HC`, `VC`, `CPUSPEED`, `DFFD`, `1FFD`, `EFF7`, `FF77`, `ROMPGn`), `--page bank8N`, conditions on NEXTREG / copper / beam / magic; ZRCP MMU fix; DZRP / GDB Next stops | condition compiler tests per variable, Next fixtures | |
| P5 | Next: DMA through watchpoints, machine events (DMA, interrupts, NMI, DivMMC automap, esxDOS), DivMMC / Multiface pages | Next DMA and DivMMC fixtures | |
| P6 | TS-Conf: DMA through watchpoints, FMaps port breakpoints, raster breakpoint, interrupt events | TS-Conf DMA / raster fixtures | |
| P7 | Sprinter: accelerator through watchpoints, Z84C15 events, journal events, PLD codes | Sprinter accelerator fixtures | |
| P8 | FDC command events (all WD1793 machines) | TR-DOS boot stops on the first read sector | |

P0-P2 are done on this branch with the GS debugger's X13. The generic event space (`BRK_EVENT` with a source) is built in
P5, the first phase with machine events; P6-P8 add their kinds to it.

## Open decision

- Writing the card (or a device) from a hook: a `logic` role for the card CPU with a time travel journal, or only the
  read and byte replacement there is now. Waits for the owner (gs-debugger Q3).
