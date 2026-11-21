# TTD diff of two moments: plan

**Status 2026-11-03:** done on all six planes; measured; accepted. 2026-11-21: the devices section names the fields
that changed and tells free-running counters apart ("Device fields").

## Steps

| # | Step | State |
|---|------|-------|
| 1 | `TTDDiffMomentsQuery` / `TTDDiffMomentsResult`, `TimeTravelController::QueryDiffMoments`: two silent seeks, the live state put back; cpu, paging (latches, 8K slots by `TtdKeyOf`), memory ranges per region with short ranges' bytes, the last CPU write per range (`FindAccesses`), screen, devices (the TTD registry), between (frames, T, keys, markers, barrier); the text | Done |
| 2 | The verb `diff` in `TTDControl`: options, JSON reply, `text=true` | Done |
| 3 | Planes: CLI `ttd diff`, WebAPI `GET /ttd/diff` (+ OpenAPI), Lua `ttd_diff{}`, Python `ttd_diff()`, MCP `time_travel diff` | Done |
| 4 | Unit tests D1-D7, the live MCP test, the live measurement (`scratch/ttd-diff-moments/live_diff.py` in the worktree) | Done |
| 5 | Docs chain: command-interface.md, the plane docs, the MCP README, the reverse-debugging recipe | Done |

## Tests (`core/tests/debugger/ttd/ttddiffmoments_test.cpp`)

| # | What | Time |
|---|------|------|
| D1 | A counter program on a Pentagon: the counter byte, the screen byte and the border change; values and writers (`inc (hl)`, `ld (#4000),a`) named | 39 ms |
| D2 | The same two moments backwards: `backwards`, values read first -> second | ~10 ms |
| D3 | The same moment twice: nothing differs | ~10 ms |
| D4 | Two moments inside one frame: the boundary at or after the T asked for | ~10 ms |
| D5 | 128K paging: `#7FFD` = `#13` maps bank 3: the latch and slots 6 / 7 differ | ~10 ms |
| D6 | The Next: MMU6 = `#0F` shows page 7's upper half at `#C000`; slot 6 names page 7 half 1, the range page 7 offset `#2005`, writer `#8015` | ~10 ms |
| D7 | The verb through `TTDControl`: JSON and text | 89 ms |

Live MCP (`tools/verification/mcp/test_time_travel.py`): two frame starts five frames apart, FRAMES (`#5C78`) in a RAM
5 range with its ROM writer, two moments inside one frame backwards with only the cpu section, the position kept, a
missing `from_frame` and an unknown section named.

## Measurements (2026-11-03, M-series Mac, release build, unreal-qt offscreen)

The old loop: `position`; `seek` A, registers, paging, AY; `seek` B, the same; `memory_diff` ram A -> B; per range
(at most 16) `memory_at` at A and at B, `find_last` of a write before B from A, `disassemble` of the writer; `seek`
back. Both moments are frame starts: the old loop's best case (`memory_diff` takes no T inside a frame).

| Question | Surface | Calls old / one | Bytes old / one | MCP text old / one | ms old / one |
|----------|---------|-----------------|-----------------|--------------------|--------------|
| 48K ROM main loop, 50 frames apart | REST | 18 / 1 | 4826 / 1013 (4.8x) | - | 35.9 / 3.3 (10.7x) |
| | MCP | 18 / 1 | 2002 / 1135 (1.8x) | 4588 / 426 (10.8x) | 46.6 / 4.6 (10.1x) |
| Green Beret in play, one frame | REST | 74 / 1 | 13379 / 1390 (9.6x) | - | 342 / 7.3 (47x) |
| | MCP | 74 / 1 | 11101 / 1573 (7.1x) | 10005 / 575 (17.4x) | 667 / 165 (4.0x) |
| Green Beret, 279 frames apart | REST | 74 / 1 | 16071 / 3845 (4.2x) | - | 455 / 159 (2.9x) |
| | MCP | 74 / 1 | 14113 / 4354 (3.2x) | 10226 / 1626 (6.3x) | 567 / 73 (7.7x) |

What each answer covers: the old loop gets the registers and the AY only as raw state to compare by hand, ranges split
at every unchanged byte (19 for one frame, where the one call gives 3 joined over gaps of 16 or less) and writers for
the first 16 only; the one call names the registers that differ (3-9), the devices whose state differs (1-2: the AY,
the tape), the screen split into pixels and attributes, and the writers of every listed range (16 of 22 for the long
stretch, `+6 more range(s)` said). A moment inside a frame (f149+1000 -> f149+40000: PC, AF and five stack bytes with
their writer) has no old-loop equivalent at all. The wall time of the writer search varies with what the journal
holds (7.3 ms over REST, 165 ms over MCP for the same one-frame pair, one after the other); every case is
faster than the old loop.

## Acceptance

- D1-D7 green on a Pentagon, a 128K and the Next (8K halves). Done.
- Green Beret in play compared: the stack, the game variables (`#A13C` by `ld (#A13C),sp`), the screen pixels by
  `ldi`; the same ranges as `memory_diff` + `find_last`. Done.
- One call against 18-74; 1.8-9.6x fewer bytes, 6.3-17.4x less MCP text, 2.9-47x faster. Accepted.
- Known limit (closed 2026-11-21, "Device fields" below): a device with running counters (the AY's phases) showed
  as changed at almost any two moments, and the section listed which devices to look at, not what changed in them.

## Device fields (2026-11-21, the DM limit)

The devices section names the fields that changed inside each device and tells counters apart
([design.md](design.md#the-sections), the devices bullet):

| # | Step | State |
|---|------|-------|
| F1 | `TTDSerializable::TTDDescribeFields` (name, offset, size, `kStateFieldCounter`): a device's blob as named fields, asked only by queries | Done |
| F2 | Layouts: AY-3-8912 (`SoundChip_AY8910::TTDFields`), TurboSound (`ay0.` / `ay1.`, the SSG queues, the render phases), TSFM (`fm0.` / `fm1.`: the latch, timers, ymfm's registers, operators and counters, the SSG halves `fm0.ssg.`), WD1793 with its drives (`fdd[n].`), the +D card (`fdc.`), Covox | Done |
| F3 | `TTDDiffDeviceFields`: generic over the layout; declared time fields are counters (one inside a larger field split out); bytes no field covers as `@offset` ranges; counters after the other fields, a device whose counters alone moved after the others; `limit` per device's fields | Done |
| F4 | Reply: per device `described`, `counters_only`, `fields_changed`, `counters_changed`, `fields[] {name, offset, size, changed, counter, before, after}`, `more_fields`; `devices.counters_only`; the text one line per device (`ay0.R8 #00->#0F; counters: ...`, `only counters: ...`). All planes carry it (they pass the verb's reply); docs chain and OpenAPI updated | Done |

Tests (`ttddiffmoments_test.cpp`):

| # | What |
|---|------|
| D8 | The AY layout: R7 a field with both values, the tone and envelope counters after it, little endian; counters alone are `only counters`; the limit bounds the list, the counts stay whole |
| D9 | The disk controller: track, status, a drive's track; the clock (a declared time field) and a drive's rotation as counters |
| D10 | No layout: `@offset` ranges joined over small gaps, a time field a counter of its own; a time field inside a larger field split out (`engine+8`) |
| D11 | A live Covox: a frame without writes moves only the idle countdowns; an OUT to `#FB` is `dac[n]` with its value |
| D12 | A recording on a Pentagon (the socket's TurboSound or TSFM): the AY register the program wrote (`ay0.R8` / `fm0.ssg.R8 #00->#0F`) in the diff; later, with the program idle, every described device differs in counters at most, and those come last |
| D13 | The layouts (AY, WD1793, TurboSound, TSFM, Covox) cover their blobs byte for byte; every declared time field is a counter field of its own |

Other devices name no fields yet: their changes show as `@offset` ranges, their declared time fields as counters; a
device gets named fields by overriding `TTDDescribeFields` (nothing in the diff changes).
