# Recipe: Find, Map, Read and Write Memory (Pages, Regions, Paging State)

Scope: static inspection and editing of memory, not access counting. Search
for a byte pattern, get a sparse "where is the data" map, read and write by
CPU address or by physical RAM/ROM page, use device memory regions (memory a
device owns outside the CPU's pages, such as the Sprinter's video RAM),
protect the ROM from tool writes, disassemble straight from a page, and read
what is paged in right now. For *how often* an address is read, written or
executed use [memory-counters.md](memory-counters.md).

Ground truth: `state_memory_api.cpp`, `memory_region_api.cpp` and
`debug_api.cpp` under
[core/automation/webapi/src/api/](../../core/automation/webapi/src/api/),
the byte-access core in
[memory.cpp](../../core/src/emulator/memory/memory.cpp), regions in
[devicememory.cpp](../../core/src/emulator/memory/devicememory.cpp), MCP
`inspect_state` / `debug_code` in
[mcp-tools.cpp](../../core/automation/mcp/src/mcp-tools.cpp) and
[mcp-analysis.cpp](../../core/automation/mcp/src/mcp-analysis.cpp).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `inspect_state` and `debug_code` cover most reads and the search; writes
> and page access go through `invoke_api`. Use [WebAPI](#webapi) only inside
> host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Which call answers which question

| Question | Call |
|:--|:--|
| Where does the data live in the 64 KB the CPU sees (or in physical RAM)? | `memory/map` (`view=address` or `ram`) |
| What is paged in at 0000/4000/8000/C000 right now? | `state/memory` (`banks`), `state/paging` (latches and decode) |
| Which ROM is selected, with its signature? | `state/memory/rom` |
| Does this byte sequence exist, and where? | `memory/find` (CPU address space, one page or all RAM; `??` wildcards, masks) |
| Bytes at a CPU address | `memory/{addr}?len=` or `memory/read/{address}?length=` |
| Bytes of a physical page, regardless of paging | `memory/page/{ram\|rom}/{n}` or `memory/{type}/{page}/{offset}` |
| Disassemble a physical page | `disasm/page?type=&page=&offset=&count=` |
| Device-owned memory (video RAM, ...) | `memory/regions`, `memory/region/{name}` |
| Page something in (or set a machine register) as the program would | `ports/out` - a port write through the machine's decoder |

## MCP (preferred)

```text
# Overview: sparse non-zero blocks, CPU view or physical RAM pages
inspect_state {"aspects":["memory_map"]}                          # view=address (default)
inspect_state {"aspects":["memory_map"],"view":"ram","min_run":256,"max_blocks":64}

# What is mapped now
inspect_state {"aspects":["memory_banks","paging"]}               # /state/memory and /state/paging
inspect_state {"aspects":["rom"]}                                 # /state/memory/rom

# Read at a CPU address (size max 4096; format hexdump | full | sparse)
inspect_state {"aspects":["memory"],"address":23296,"size":64}

# Search the CPU address space (?? = any byte, A? = any low nibble)
debug_code {"action":"find_bytes","pattern_hex":"CD 16 00","start":"0x4000","end":"0xFFFF","max":16}
debug_code {"action":"find_bytes","pattern_hex":"CD ?? 00"}
# Every RAM page, mapped or not (matches as page + offset), or one page by name
debug_code {"action":"find_bytes","pattern_hex":"C3 00 80","space":"ram"}
debug_code {"action":"find_bytes","pattern_hex":"C3 00 80","space":"ram5"}
# A bit mask: 1 bits must match (here: LD HL,#40xx..#4Fxx)
debug_code {"action":"find_bytes","pattern_hex":"21 00 40","mask_hex":"FF 00 F0"}

# Disassemble at an address (current PC when omitted)
debug_code {"action":"disassemble","address":"0x8000","count":12}

# A device memory region (the region must exist on this machine)
invoke_api    {"method":"GET","path":"/api/v1/emulator/{id}/memory/regions"}
inspect_state {"aspects":["memory_region"],"region":"vram","address":0,"size":64}

# Page-level and write operations (no dedicated tool)
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/5","query_params":{"offset":0,"length":64}}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disasm/page","query_params":{"type":"rom","page":0,"offset":0,"count":16}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/memory/write","body":{"address":"0x8000","data":[175,50,14]}}
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/memory/rom/protect","body":{"protected":false}}

# A port write through the decoder, like the CPU's OUT (here: TS-Conf RAM page #20 into window 3)
control_execution {"action":"port_out","port":"0x13AF","value":"0x20"}
# -> Port 0x13AF <- 0x20 (paused)
```

## WebAPI

```bash
BASE=http://localhost:8090/api/v1

# Map (what to read first)
curl -s "$BASE/emulator/$EMU_ID/memory/map?view=address&min_run=64&max_blocks=48" \
  | jq '{model, view, non_zero_bytes, block_count, truncated, blocks: .blocks[:8]}'
curl -s "$BASE/emulator/$EMU_ID/memory/info" | jq '.z80_banks'

# Search: pattern_hex (spaces, commas, 0x allowed) or pattern (byte array)
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/find" -H 'Content-Type: application/json' \
     -d '{"pattern_hex":"AF 32 0E","start":"0x5B00","end":"0xFFFF","max":32,"alignment":1}' \
  | jq '{count, truncated, range, first: .matches[0]}'
# Wildcards, every RAM page, a mask
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/find" -H 'Content-Type: application/json' \
     -d '{"pattern_hex":"C3 ?? 80","space":"ram"}' | jq '.matches[:3]'
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/find" -H 'Content-Type: application/json' \
     -d '{"pattern_hex":"21 00 40","mask_hex":"FF 00 F0","space":"cpu"}' | jq '.count'

# Read at a CPU address (what the CPU would see)
curl -s "$BASE/emulator/$EMU_ID/memory/0x5C00?len=32&format=hexdump" | jq -r '.hexdump'
curl -s "$BASE/emulator/$EMU_ID/memory/read/0x5C00?length=32&format=sparse" | jq '{address, length, non_zero, segments}'

# Read a physical page (paging irrelevant); sparse folds 00/FF runs
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/5?offset=0&length=256&filter=sparse" | jq '{type, page, non_zero, segments}'
curl -s "$BASE/emulator/$EMU_ID/memory/ram/5/0?len=256" | jq .             # offset form

# Write: CPU address, or a physical page
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/write" -H 'Content-Type: application/json' \
     -d '{"address":"0x8000","data":[175,50,14]}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/page/ram/5" -H 'Content-Type: application/json' \
     -d '{"offset":"0x0100","data":[255,0,255]}' | jq '{success, page, offset, bytes_written}'

# ROM write protection (default: protected)
curl -s "$BASE/emulator/$EMU_ID/memory/rom/protect" | jq .
curl -s -X PUT "$BASE/emulator/$EMU_ID/memory/rom/protect" -H 'Content-Type: application/json' \
     -d '{"protected":false}' | jq .

# Disassemble a physical page / the current CPU view
curl -s "$BASE/emulator/$EMU_ID/disasm/page?type=rom&page=0&offset=0&count=16" | jq '.instructions[] | {offset, bytes, mnemonic, label}'
curl -s "$BASE/emulator/$EMU_ID/disasm?address=0x8000&count=12" | jq '.instructions[]'

# What is paged in
curl -s "$BASE/emulator/$EMU_ID/state/memory"      | jq '{banks, paging}'
curl -s "$BASE/emulator/$EMU_ID/state/memory/rom"  | jq '{active_rom_page, mapping}'
curl -s "$BASE/emulator/$EMU_ID/state/paging"      | jq '.latches'

# Page something in as the program would: a port write through the machine's decoder
curl -s -X POST "$BASE/emulator/$EMU_ID/ports/out" -H 'Content-Type: application/json' \
  -d '{"port":"0x7FFD","value":"0x13"}'        # 128K: RAM page 3 at #C000, ROM 1
# -> {"port":"0x7FFD","value":"0x13","moment":"paused"}
```

CLI `out #7FFD #13`, Lua `port_out(0x7FFD, 0x13)`, Python `emu.port_out(0x7FFD, 0x13)`, Qt debugger toolbar
"Port OUT...".

### Device memory regions

```bash
curl -s "$BASE/emulator/$EMU_ID/memory/regions" | jq '.regions[] | {name, size_hex, page_size, pages, writable, write_path}'

# Read: offset/length are numbers (decimal, 0x or #hex); format hex | data | sparse | binary
curl -s "$BASE/emulator/$EMU_ID/memory/region/vram?offset=0x17F0&length=16&format=hex" | jq '{region, offset, length, hex}'
curl -s "$BASE/emulator/$EMU_ID/memory/region/vram?offset=0&length=262144&format=binary" -o scratch/vram.bin

# Write (hex string or byte array), or save / load a file
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/region/vram" -H 'Content-Type: application/json' \
     -d '{"offset":"0x17F0","hex":"0000A8"}' | jq '{success, bytes_written}'
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/region/vram" -H 'Content-Type: application/json' \
     -d '{"action":"save","path":"scratch/vram.bin","offset":0,"length":0}' | jq .
```

A region is also reachable as a page type: `memory/page/vram/{n}` reads and
writes its page `n` (type must be `ram`, `rom` or a region name). Region
writes go through the *device's own write path* (`write_path` says which).

## Fields worth asserting

- `memory/map`: `blocks[]` of `{address, size, type, bank, page, rom, status,
  non_zero, hash}`. `status` is `zeros` or `data`; `hash` (FNV-1a 64, only on
  data blocks, not a SHA) lets you compare a region before and after without
  dumping bytes. `truncated: true` means the block budget coarsened the
  result; raise `max_blocks` (clamped to 1-4096) or narrow with `min_run`
  (1-16384).
- `memory/find`: `success`, `space`, `count`, `matches[]` (`address` in the CPU
  view, or `page {kind, page}` + `offset`; `context_start` and `context`: 4 bytes
  before, the match, 4 after), `truncated` (hit `max`, default 64), `range`.
- Reads: `address`, `length`, and by format `hexdump` (default), `hex` +
  `data` (`full`) or `segments[] {offset, length, is_fill, fill | hex}`
  (`sparse`, with `non_zero`).
- `state/memory`: `banks` per window (`type` RAM/ROM, `page`,
  `read_write`, `contended`) and `paging` (decoded `#7FFD` bits; Pentagon
  `#EFF7` flags; Profi `#DFFD` flags). On the Sprinter a `sprinter` block
  replaces the 128K-style paging.
- `state/memory/rom`: `active_rom_page`, `total_rom_pages`, `mapping`
  (`bank0_type`, `bank0_rom_title`, `bank0_rom_signature`).
- `state/paging`: `latches[] {port, device, latch, value, decoded}` taken
  from the machine's tagged port registry, the same truth the decoder uses.
- `memory/info`: page counts (`pages.ram/rom/cache/misc`) and
  `z80_banks.bank0..3.mapping`.

## Typical uses

| Task | Steps |
|:--|:--|
| Find a game's variable block | `memory/find` for a known value or text, then `memory/page/ram/N` at that page |
| "Did my poke stick?" | `memory/write`, then `memory/{addr}` and compare the `hexdump` |
| Is this ROM the one I think? | `state/memory/rom` -> `bank0_rom_signature` / `bank0_rom_title` |
| What changed between two moments? | `memory/map` twice with `view=ram`, diff `hash` per block |
| Read the Sprinter's video RAM | `memory/regions` -> `memory/region/vram` |
| Look at a page that is not mapped, the way the program would see it | `ports/out` with the machine's paging port (`#7FFD`, TS-Conf `#13AF`...), then `memory/{addr}` |

## Pitfalls

- **CPU view vs physical page.** `memory/find`, `memory/{addr}`,
  `memory/read` and `disasm` use the 64 KB the CPU sees *now*; paging moves
  what is behind each window. Page endpoints ignore paging. `memory/find`
  searches 16-bit addresses only (`start`/`end` 0-65535, `alignment` 1 or 2,
  pattern up to 64 bytes) and cannot see an unmapped page: use the page
  endpoints and a loop, or `memory_map` `view=ram`.
- **`memory/write` is not CPU-accurate.** It pokes via the read pointer, so
  it **does write into a mapped ROM** and into write-protected RAM; the ROM
  protect flag does not apply to it. The protect flag (default on, shared by
  the whole server, not per instance) guards only `memory/page/rom/{n}`
  (`403 Forbidden`). A poke into ROM then also persists across the page
  being switched out. Re-create the instance (or reload the ROM) to undo.
- **Writes during TTD recording** (`memory/write`) insert a debugger-edit
  marker and mark the RAM page dirty, so the edit is part of the recorded
  history and seeks see it.
- **`ports/out` is the program's own path, not a poke.** The machine's decoder handles it: every side effect of
  a CPU OUT happens (paging, a TS-Conf DMA start, an AY register select, a floppy controller command), so write
  only what you mean. No port or memory breakpoint fires on it, device waits are dropped (the CPU's clock does not
  move), and a TTD recording keeps it as a debugger-edit marker. It runs paused, never started or between two
  frames of a running machine (`moment`); `503` when another client is stepping the emulator. A port the machine
  decodes as read-only does nothing.
- **Argument forms differ.** `memory/{addr}` takes `len` (max 4096) and a
  decimal or `0x` address; `memory/read/{address}` takes `length` (default
  128); `memory/page` takes `offset` and `length`; regions take `offset` and
  `length` as numbers or `#hex`. The MCP `memory` aspect passes the address
  as a decimal integer.
- **Region reads are capped at 65536 bytes as JSON** (`format=binary` has no
  cap and answers raw bytes with `X-Region` / `X-Offset` headers). Unknown
  region names are `404`.
- **Route shadowing.** `memory/info`, `memory/map`, `memory/regions` and
  `memory/rom/protect` are literal routes next to the `memory/{addr}` and
  `memory/{type}/{page}/{offset}` patterns; do not use an address string
  that looks like one of those words (unconfirmed how the router breaks a
  tie, the literal ones are what the recipes above use).
- **Large JSON.** A full `memory/map` of a 1 MB machine with a small
  `min_run` is big; keep `max_blocks` low and drill down with page reads.
- **Unconfirmed:** the exact page index limits per model for
  `memory/page/ram/{n}` (the error text states `0-N` for the machine; read
  `memory/info` and `state/memory` first) and the Sprinter's page types (use
  `memory/regions` there).
