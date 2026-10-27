# TODO — bus analyzer

**Status:** B1-B5 (JSONL / VCD), B5m and B6a implemented and tested on branch `bus-analyzer` (worktree
`scratch/wt-bus-analyzer`), not landed. PLAN.md row **#110**. Found on the way: the text assembler lowercases a symbol
inside a port operand `OUT (name),A` while labels are case-sensitive (the mutation tests put the ports in as numbers).

## Documents

| File | Topic |
|:--|:--|
| [README.md](README.md) | Why and what, **start here** |
| [use-cases.md](use-cases.md) | Use cases UC-1..UC-11 |
| [requirements.md](requirements.md) | FR / NFR, planes, acceptance ACC-1..ACC-10 |
| [technical-design.md](technical-design.md) | Model, components, changes per file, tests |
| [agent-recipes.md](agent-recipes.md) | Recipes for agents |
| [mutation-tests.md](mutation-tests.md) | Driver mutation tests: the reference driver, mutants, platform forks, real software |

## Phases

| Phase | Content | Core | Planes | Status |
|:--|:--|:--|:--|:--|
| B1 | `BusManager`, `BusTap`, `BusTrace`, events; taps in `ZControllerSpi` (TS-Conf, ATM3), Next, DivMMC, NeoGS; SD protocol events from the card | yes | - | done |
| B2 | Checks: `early_access`, `select_mid_byte`, `deselect_mid_command`, `command_while_busy`, `command_drops_answer`, `crc_error`, `dma_collision` | yes | - | done |
| B3 | `DeviceState::Bus*`; WebAPI + OpenAPI, MCP `bus_analyzer`, CLI `bus`, Lua, Python; recipes `bus-analyzer.md`, `sd-driver-debugging.md`; live MCP script | - | all five | done |
| B4 | SD card timing profiles `ideal` / `realistic` / `slow`, clocked delays, state v2 | yes | `sd_timing` | done |
| B5 | Export JSON lines and VCD; B5b sigrok sessions, triggers, pre-trigger capture | yes | `export` | JSONL + VCD done, B5b open |
| B5m | Driver mutation tests ([mutation-tests.md](mutation-tests.md)): the reference SD driver and its mutants M1-M9, T1-T2, forked on TS-Conf, ZX-Evo, Next, DivMMC; real software R-WC, R-NEDOOS, R-EVO-MENU, R-UNODOS, R-NEXTZXOS, R-DSS | - | - | done |
| B6a | ATA bus (pulled forward for DSS): the tap in `AtaChannel` (every IDE board), register / command / data events, checks `ata_data_without_drq`, `ata_command_drops_data`, `ata_write_while_busy`; mutants A1-A3 | yes | same tool | done |
| B6 | Further buses: UART (+ ESP AT line decoder), Next I2C, FDC command bus; FT8xx host command decoder; the ISR heuristic (a bus access from an interrupt handler while the main code had a device selected) | yes | same tool | open |
| B7 | Rebuild `NextSdJournal`, `PortDiagRecorder`, `GsPortTrace`, `MultiSoundBusTrace` over the shared trace; Qt `W.logic` widget | yes | - | open |

## Open questions

| # | Question | Recommendation |
|:--|:--|:--|
| Q1 | Strict card mode: should a `realistic` / `slow` card also *refuse* what the ideal card forgives (a command during busy, an unread block)? | Not now: findings say it; refusing changes behaviour in ways no source pins down. Revisit with a real-card capture. |
| Q2 | TS-Conf `spi.v` (behaviour of a start during a running byte) is not in the local collection | Needed only for a strict DMA-collision model; the finding does not depend on it |
