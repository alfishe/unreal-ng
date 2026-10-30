# Memory access counters and watches

The memory profiler (`MemoryAccessTracker`) counts the reads, writes and executes of every cell: the CPU's 64K view
and every physical page. Two core modules give every automation plane the same access:

- `MemoryCounters` (`core/src/debugger/memory/memorycounters.h`) reads the counters: one cell, a list of cells, a
  window, the touched cells (sparse) or the heaviest ones (top), in any space;
- `MemoryWatch` (`core/src/debugger/memory/memorywatch.h`) adds, lists and removes watches over a region of CPU
  addresses or a partially decoded port, with the PCs (callers) and the values seen.

Both count while a memory session captures: `trace("memory", "start")`, `profiler memory start`,
`POST /profiler/memory/start`, MCP `analyze_performance` `profiler` + `memory`. Starting switches on the features the
session needs (`debugmode`, `memorytracking`) and zeroes the counters and the watches' counts.

## Spaces

| Space | Cells | What a cell is |
|:--|:--|:--|
| `cpu` (default) | 65 536 | a CPU address, whatever was paged in when it was touched |
| `ram5`, `rom2`, `cache0` | 16 384 | an offset in that physical page, mapped or not |
| `ram` | RAM pages × 16 384 | every RAM page back to back (page n at n × #4000) |
| `phys` | 389 × 16 384 | the whole physical array: RAM 0..255, cache 0..3, misc 0, ROM 0..127; `offset / #4000` is the page, `PhysPageName` names it |

The `cpu` counters count only in the Z80 address space mode, which is the default. The physical counters always
count. Device memories (`sprinter.vram`, `neogs.ram`) have no counters.

## Reads

| Form | Answer |
|:--|:--|
| `addresses` | exactly those cells in that order, zeros too (`0x5C00,#5C08,0x8000-0x80FF`, at most 65 536) |
| `top` (default on the tools) | the `limit` heaviest cells of the window, heaviest first; ties go to the lower address |
| `sparse` | the cells of the window whose weight is at least `min`, in address order, up to `limit` |
| `dense` | every cell of the window (at most 1 048 576) |
| `binary` (REST) | the dense window as three little-endian `uint32` arrays: reads, then writes, then executes |

A cell's weight is the sum of the counters named in `kinds`: `r`, `w`, `x` or a mix (`rw`, `x`). Every answer also
carries:

- the window's totals;
- `matched`, the number of cells at or above `min`;
- `truncated`, set when more cells matched than `limit`;
- the session state, and `allocated` (false before the first session: every count is 0).

A read takes the counters' lock shared and copies. The emulation thread is never paused and keeps counting.

## Watches

A watch counts accesses to one of two targets:

- **A region** of CPU addresses, `start` to `start + length - 1`, up to #FFFF. It counts reads, writes and executes.
- **A port**: an access counts when `(access & mask) == (port & mask)`. It counts reads and writes.
  - The default mask is `0xFFFF`, the exact port.
  - Mask `0x00FF` compares the low byte only: with port `0x1F` it catches every Kempston read, with `0xFE` every
    keyboard or border access.

Options:

- `callers` counts the PC of each access, up to `max_callers` distinct PCs (default 100).
- `values` counts the values moved, up to `max_values` distinct values (default 256).

Names are 1–64 letters, digits, `_`, `-` and `.`. A name is unique across regions and ports.

The emulation thread updates the watches without a lock, so `MemoryWatch` adds, removes, clears and lists them at a
coherent moment: paused, stopped, or between two frames of a running machine. A busy machine answers `busy`
(REST 503). At each frame's end the emulation thread publishes the watches' totals, so the `debug.watches` topic and
`monitor debug.watches` read them without waiting.

## Planes

| Plane | Counters | Watches |
|:--|:--|:--|
| CLI | `profiler memory counters space=phys view=top kinds=w limit=16`, `profiler memory cell 0x5C08` | `profiler memory watch add sysvars 0x5C00 0xC0 callers`, `profiler memory watch port kempston 0x1F 0xFF values`, `... remove <name>`, `... clear`, `... list` |
| Python | `emu.memory_counters(space="cpu", view="top", kinds="x", limit=16)`, `emu.memory_counter(0x5C08)` → `(r, w, x)` | `emu.memory_watch_add("sysvars", 0x5C00, 0xC0, callers=True)`, `emu.memory_watch_port("kempston", 0x1F, 0xFF, values=True)`, `memory_watch_remove`, `memory_watch_clear`, `emu.memory_watches()` |
| Lua | `memory_counters{space="phys", view="top", kinds="w"}`, `memory_counter(0x5C08)` → `r, w, x` | `memory_watch_add("sysvars", 0x5C00, 0xC0, {callers=true})`, `memory_watch_port("kempston", 0x1F, 0xFF, {values=true})`, `memory_watch_remove`, `memory_watch_clear`, `memory_watches()` |
| MCP | `analyze_performance {"action":"memory_counters","space":"cpu","view":"top","kinds":"x","limit":16}` | `analyze_performance {"action":"memory_watch","op":"add","name":"kempston","port":"0x1F","mask":"0xFF","values":true}` (op `add`, `remove`, `clear`, `list`) |
| REST | `GET /api/v1/emulator/{id}/profiler/memory/counters?space=cpu&format=top&kinds=x&limit=16` | `GET /profiler/memory/watches`, `POST /profiler/memory/watches {"name","start","length"}` or `{"name","port","mask"}` + `callers`, `values`, `max_callers`, `max_values`; `DELETE /profiler/memory/watches`, `DELETE /profiler/memory/watches/{name}` |
| WebSocket | `{"op":"call","method":"GET","path":"/api/v1/emulator/{id}/profiler/memory/counters?..."}` | `call` of the watch routes; topic `debug.watches` `{available, session, revision, watches [{name, kind, start + length or port + mask, reads, writes, executes}]}` |

The MCP text lists every cell and watch it returns, because an agent may see only the text:

- a cell: `0x5C08 r=12 w=3 x=0`, plus `(ram5 0x1C08)` in `phys`;
- a watch: its counts and its top callers and values.

Without `space` or `addresses`, with `format` dense or sparse and none of `kinds`, `min`, `limit`, `length`, the
REST route keeps its older form: `mode=z80|physical&page=N&start=&end=`, answering `counters` or
`addresses: [[address, r, w, x]]`.

## Uses

- **What a POKE address is:** `addresses=` with the trainer's poke addresses after a run.
  - Executed: code.
  - Read and never written: a constant.
  - Written: a variable.
- **Which input a game reads:** port watches `kempston` (`0x1F`/`0xFF`) and `keys` (`0xFE`/`0xFF`, `values`), plus
  the port trace's half-rows ([game-start recipe](../../../../.recipe/analysis/game-start-input.md)).
- **Hot code and data:** `top` with `kinds=x` (hot code) and with `kinds=w` (hot variables), in `phys` to see the
  banks.
