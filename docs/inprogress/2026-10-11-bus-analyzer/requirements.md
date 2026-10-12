# Bus analyzer: requirements

**Date:** 2026-10-11 · part of [README.md](README.md) · use cases in [use-cases.md](use-cases.md) · design in
[technical-design.md](technical-design.md)

## 1. Functional requirements

| ID | Requirement |
|:--|:--|
| FR-1 | One `BusManager` per emulator knows every bus of the running machine and of the cards in its slots. A bus has a stable id, a protocol, a human label, its devices (slot number, name) and a byte time. |
| FR-2 | A bus master records transactions through a tap: chip-select change, byte exchanged (MOSI, MISO, slot, source CPU / DMA / card CPU), early access, truncated byte. |
| FR-3 | Devices that decode a protocol record protocol events on the bus they sit on: for the SD card a command (index, ACMD flag, argument, CRC valid, R1), a block queued for the host, a block received (stored or refused). |
| FR-4 | Every event carries a sequence number, an epoch, frame, T in the frame, PC and source. Events are kept in time order across a TTD seek (later events are dropped when time goes back), and nothing is recorded while TTD replays history. |
| FR-5 | Tracing (events) and checks (findings) are switched per bus or for all buses at once; the ring has a capacity; clearing keeps the sequence counting. Off by default. |
| FR-6 | Queries filter by event kind, slot, sequence (`since`), frame range and count (the newest `limit`). The same parser serves every plane. |
| FR-7 | Checks on SPI buses, each a named rule with severity: `early_access` (a read or write before the byte finished), `select_mid_byte` (chip select changed with a byte in flight), `deselect_mid_command` (chip select dropped inside a 6-byte SD command), `command_while_busy` (a command started while the card still signals write busy), `command_drops_answer` (a command started while the card still had answer bytes queued, CMD0 / CMD12 excluded), `crc_error` (the card answered with the CRC error bit), `dma_collision` (the CPU accessed the bus ports while an SPI DMA was running). Each finding names the event it belongs to. |
| FR-8 | The SD card has a timing profile: `ideal` (today's behaviour, default), `realistic` (random NCR, read latency, write busy and ACMD41 polls within typical card ranges, deterministic by seed), `slow` (the SD specification's worst case). The profile is part of the card's saved state. |
| FR-9 | Export of a bus's events: JSON lines (every field) and VCD (wires synthesized from transactions with the bus's byte time). |
| FR-10 | The existing Next SD journal keeps working with its routes and output until it is rebuilt over the bus trace (B7). |

## 2. Non-functional requirements

| ID | Requirement |
|:--|:--|
| NFR-1 | Cost with everything off: one predictable pointer / bool test per bus transaction; no allocation, no lock. The ring is allocated when tracing is switched on and freed when it is switched off (coding-guidelines: lazy diagnostic buffers). |
| NFR-2 | Cost with tracing on: one fixed-size POD append under an uncontended mutex per event. |
| NFR-3 | No change of emulated behaviour with the analyzer on (traces and checks observe only). The timing profile changes behaviour and therefore counts as a machine change for automation (`ChangesMachine`). |
| NFR-4 | Thread safety: the emulator thread appends; any thread reads a copy; a bus may unregister (model switch, card removed) while a reader holds the manager. |
| NFR-5 | Deterministic: the same session and seed give the same events and findings. |
| NFR-6 | Zero warnings on clang, gcc, mingw, msvc; tests under 50 ms each. |

## 3. Automation planes

Same names on every plane (design-automation-coverage.md R2), one `DeviceState` implementation behind them
(R1), reads without side effects (R3).

| Operation | WebAPI | MCP (`bus_analyzer` action) | CLI | Lua / Python |
|:--|:--|:--|:--|:--|
| List buses | `GET /api/v1/emulator/{id}/bus` | `list` | `bus` | `bus_list()` |
| Events | `GET .../bus/{bus}/events?kinds=&slot=&since=&from=&to=&limit=` | `events` | `bus <id> events ...` | `bus_events(bus, ...)` |
| Findings | `GET .../bus/{bus}/findings?rules=&since=&limit=` | `findings` | `bus <id> findings ...` | `bus_findings(bus, ...)` |
| Control | `POST .../bus/{bus}/trace-journal {enabled, checks, clear, capacity}` (`bus` = `all` for every bus) | `enable`, `disable`, `clear` | `bus <id> on|off|clear [checks|nochecks] [capacity=N]` | `bus_control(bus, ...)` |
| SD timing | `GET` / `POST .../bus/{bus}/sd-timing {slot, profile, seed}` | `sd_timing` | `bus <id> sd-timing [profile] [seed=N] [slot=N]` | `bus_sd_timing(bus, ...)` |
| Export | `GET .../bus/{bus}/export?format=vcd|jsonl&since=&limit=` | `export` | `bus <id> export <vcd|jsonl> <file>` | `bus_export(bus, format, ...)` |

The control route ends in `-journal`, so `automationgate.cpp` `IsJournal` treats it as not changing the
machine (no TTD invalidation, allowed while the emulator runs). `sd-timing` POST changes the machine.

The OpenAPI manifest (`core/automation/webapi/src/openapi/openapi_bus.inc`) lists every route in the same
change; `invoke_api` / `search_api` reach them from MCP.

## 4. Acceptance

| ID | Check |
|:--|:--|
| ACC-1 | TS-Conf boots from an SD image with `zc.spi` traced: the events show the firmware's CMD0, CMD8, ACMD41 polls, CMD58 and CMD17 / CMD18 with R1; the same on ATM3 (Evo), Next (`next.spi`), DivMMC (`divmmc.spi`) and NeoGS (`neogs.sd`). |
| ACC-2 | A Z80 test program on TS-Conf that starts an SPI DMA and reads `#57` before the DMA ends produces one `dma_collision` finding with the program's PC; the correct program (waits on DMA status) produces none. |
| ACC-3 | A program that sends CMD24, the block, and the next command without polling busy produces `command_while_busy`; with the poll it produces none. |
| ACC-4 | Next: a read of `#EB` sooner than 16 clocks after the previous access produces `early_access`; the existing `too_fast_accesses` counter still counts it. |
| ACC-5 | NeoGS: a restart of the SD master mid-byte produces `early_access` with the GS CPU's PC (the restart itself, `truncatedByte`, is an event). |
| ACC-6 | With profile `slow` the card makes a driver poll at least the specification's worst case: ACMD41 answers idle >= 1000 times, a read delivers the token after >= 100 000 bytes, a write stays busy >= 250 000 bytes; `ideal` keeps every existing test green. |
| ACC-7 | A saved state with profile `realistic` restores the profile, seed and the delay in progress. |
| ACC-8 | The MCP `bus_analyzer` text of `events` and `findings` contains every field (frame, T, PC, kind, decoded command); the live script `tools/verification/mcp/test_bus.py` passes. |
| ACC-9 | `core-tests` stays green; build has zero warnings. |
| ACC-10 | VCD export of a traced CMD17 opens in GTKWave / PulseView with the four SPI lines and the bytes at the bus's byte time. |
