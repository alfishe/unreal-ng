# Recipe: Symbols, Listings and Source-Level Stepping

Goal: debug by name instead of by number — define or load labels, see them in
disassembly, assemble small patches, and step or run by source line using an
sjasmplus listing.

Three layers, each usable alone:

- **Labels** — name <-> address table (`/labels`, `/symbols/load|save`,
  MCP `manage_symbols`). Annotates `/disasm` output.
- **Assembler** — `/assemble` turns Z80 source text into bytes (optionally
  written into memory) and returns its own symbol table.
- **Listing** — a loaded sjasmplus `.lst` file maps addresses to source
  lines (`/listing/*`): "what source is at PC", "step to the next source
  line", "run to line N".

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `manage_symbols` covers load/list/resolve/listing, `debug_code` covers
> disassemble and assemble, `invoke_api` reaches label editing and symbol
> save. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines or
> when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# labels
manage_symbols {"action":"load_labels","path":"scratch/game.sym"}
manage_symbols {"action":"list"}
manage_symbols {"action":"resolve","name":"MainLoop"}          # exactly one of name / address
manage_symbols {"action":"resolve","address":"0x8000"}

# listing (sjasmplus .lst)
manage_symbols {"action":"load_listing","path":"scratch/game.lst"}
manage_symbols {"action":"source_at","address":"0x8010","context":3}
manage_symbols {"action":"step_line"}
manage_symbols {"action":"step_line","max_tstates":200000}
manage_symbols {"action":"run_to_line","line":42}

# disassembly with labels, and assembling
debug_code {"action":"disassemble","address":"0x8000","count":16}
debug_code {"action":"assemble","address":"0x8000","code":"start: ld a,1\n ret","write":false}

# not covered by manage_symbols: label add/edit/delete and symbol save (invoke_api)
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/labels",
            "body":{"name":"MainLoop","address":"0x8000","type":"code"}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/symbols/save","body":{"path":"scratch/out.sym"}}
```

Notes on the MCP forms (from `mcp-symbols.cpp` and `mcp-analysis.cpp`):

- `manage_symbols` actions: `load_labels`, `list`, `resolve`, `load_listing`,
  `source_at`, `step_line`, `run_to_line`. `load_labels` needs `path`;
  `resolve` needs exactly one of `name` / `address`; `source_at` needs
  `address`; `run_to_line` needs `line`.
- `debug_code assemble` defaults `write` to `true` (the WebAPI default is
  `false`) and requires `code`. `debug_code disassemble` defaults `count` to
  16 (clamped 1-256); the WebAPI clamps to 100 (see Pitfalls).

## WebAPI

The curl walkthrough below. `$BASE` is `http://localhost:8090/api/v1`;
`$EMU_ID` is the instance id ([_common/setup.md](../_common/setup.md)).
Addresses in request bodies accept a number or a string (decimal, `0x..`,
`#..`, `$..`) for `POST /labels` and `POST /assemble`.

### Labels

```bash
# add / read / update / delete
curl -s -X POST "$BASE/emulator/$EMU_ID/labels" -H 'Content-Type: application/json' \
  -d '{"name":"MainLoop","address":"0x8000","type":"code","comment":"entry"}' | jq .
curl -s "$BASE/emulator/$EMU_ID/labels/MainLoop" | jq .
curl -s -X PUT "$BASE/emulator/$EMU_ID/labels/MainLoop" -H 'Content-Type: application/json' \
  -d '{"active":true,"comment":"main game loop"}' | jq .
curl -s -X DELETE "$BASE/emulator/$EMU_ID/labels/MainLoop" | jq .
curl -s -X DELETE "$BASE/emulator/$EMU_ID/labels" | jq .          # clear all

# list with filters: module, type, bank, from, to, active=true
curl -s "$BASE/emulator/$EMU_ID/labels?type=code&from=0x8000&to=0x9000" | jq '.count, .total, .labels[:5]'
```

`POST /labels` body: `name` and `address` required; optional `bank`,
`type`, `module`, `comment`. Reply `{status:"success", name, address}`.
`PUT` updates only `active`, `comment`, `type`, `module`. Label objects carry
`name`, `address`, `active`, and when set `bank`, `bankType` (`rom`|`ram`),
`type`, `module`, `comment`. `GET /labels` returns `count` (after filter),
`total`, `labels[]`.

### Resolve (name <-> address)

```bash
curl -s "$BASE/emulator/$EMU_ID/labels/resolve?name=MainLoop" | jq .
curl -s "$BASE/emulator/$EMU_ID/labels/resolve?address=0x8004" | jq .
```

Provide exactly one of `name` / `address`, else 400. By name: `{query:"name",
name, found:true, label}`, or 404 (with a `hint` to load symbols when no
labels exist). By address (`0x`, `$` or decimal): always 200 with `query`,
`address`, `address_hex`, `found`, and when present `label` (exact),
`all_at_address` (aliases), `nearest_below` / `nearest_above` (each
`{name, address, distance}`). So `found:false` plus `nearest_below` answers
"which routine contains this address" — use `distance` as the offset.

### Load / save symbol files

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/symbols/load" -H 'Content-Type: application/json' \
  -d '{"path":"scratch/game.sym"}' | jq .       # -> {status, count, path}
curl -s -X POST "$BASE/emulator/$EMU_ID/symbols/save" -H 'Content-Type: application/json' \
  -d '{"path":"scratch/out.sym"}' | jq .
```

The loader picks the parser from the file extension (case-insensitive):
`.map`, `.sym`, `.vice`, `.s` / `.asm` (sjasm-style `NAME EQU $ADDR ; (TYPE)`),
`.z88`; for other extensions it sniffs the first line for a map or VICE
header. The simple `.sym` format is `ADDR NAME [(TYPE)] [; COMMENT]`, one per
line; `save` writes that shape (4-digit hex address). `count` in the reply is
the total number of labels now loaded, not the number the file added. Load
failures return 400 `{error:"Failed", message}`.

### Symbolic disassembly

```bash
curl -s "$BASE/emulator/$EMU_ID/disasm?address=0x8000&count=10" | jq '.instructions[]'
```

Each instruction has `address`, `bytes` (hex string), `mnemonic`, `size`.
With labels loaded it adds `label` (a label at the instruction's own
address), `targetLabel` (for jumps/calls whose target is known; the `target`
field holds the address), and `effectiveAddressLabel` for IX/IY+d accesses
(`displacement`, `effectiveAddress`). `address` defaults to PC; `count`
defaults to 10 and the handler clamps it to 1-100.

### Assemble

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/assemble" -H 'Content-Type: application/json' \
  -d '{"address":"0x8000","write":true,"code":"start: ld a,1\n inc a\n jr start"}' | jq .
```

`code` and `address` are both required (400 `Required: code, address`
otherwise); `write` defaults to `false` and, when true, writes the bytes
into memory. Success reply: `status:"success"`, `address`, `end_address`,
`size`, `bytes[]`, `listing[]` (per line: `address`, optional `label`,
`source`, `bytes`), `symbols{}` (name -> value) and `written:true` when
written. A source error returns 400 `{error:"Assembly failed", message,
line, source_line}`.

The assembler's `symbols` are returned only; the handler does not add them to
the label table. To use them in disassembly, `POST /labels` for each one.

### Listing and source-level stepping

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/listing/load" -H 'Content-Type: application/json' \
  -d '{"path":"scratch/game.lst"}' | jq .
curl -s "$BASE/emulator/$EMU_ID/listing/source_at?address=0x8010&context=3" | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/listing/step_line" | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/listing/step_line" -H 'Content-Type: application/json' \
  -d '{"max_tstates":200000}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/listing/run_to_line" -H 'Content-Type: application/json' \
  -d '{"line":42}' | jq .
```

Response fields worth asserting:

- `listing/load`: `status`, `path`, `lines`, `code_lines`, `total_bytes`,
  `min_address`, `max_address`. 400 `Failed` if the file did not load.
- `source_at` (`address` required; `context` default 5, clamped 0-50):
  `found`, `address_hex`, `line` (`{line, has_code, address, address_end,
  size, source}`), `context[]` of the same shape, `context_from`,
  `context_to`, `source_path`. 404 with a `hint` giving the covered address
  range if the address is outside the listing.
- `step_line` (body optional; `max_tstates` 0/absent = frame size x 100,
  about 2 s emulated): `status`, `message`, `pc`, `sp`, `state`,
  `line_changed`, `from_line`, `to_line`. Executes until PC lands on a
  listing line different from the starting one; if the starting PC is not
  covered it stops on the first covered line.
- `run_to_line` (`line` required; `max_tstates` 0/absent = frame size x 500,
  about 10 s): `status` (`success` | `timeout`), `reached`, `already_at`,
  `pc`, `sp`, `state`, `target_line`. The target is the first line at or
  after `line` that has code bytes; 404 if there is none.

Both stepping calls leave the machine paused, and run with breakpoints
skipped.

## Typical workflow: break at a label, step by source line

```text
1. debug_code {"action":"assemble","address":"0x8000","code":"..."}   # or load a built program
2. manage_symbols {"action":"load_labels","path":"scratch/game.sym"}
3. manage_symbols {"action":"load_listing","path":"scratch/game.lst"}
4. manage_symbols {"action":"resolve","name":"MainLoop"}            # -> label.address
5. invoke_api POST /api/v1/emulator/{id}/breakpoints {"type":"exec","address":"0x8000"}
6. control_execution {"action":"resume"}  ... wait for the breakpoint stop
7. debug_code {"action":"disassemble","count":12}                   # labelled view at PC
8. manage_symbols {"action":"source_at","address":"<pc>"}           # source around PC
9. manage_symbols {"action":"step_line"}   (repeat; or run_to_line)
```

The breakpoint call shape is the one used in
[breakpoints-and-events.md](breakpoints-and-events.md); read that recipe for
the stop-event fields. The address passed to the breakpoint comes from step 4
(the API takes an address, not a label name; name support there is not
confirmed).

## Pitfalls

- **Listing not loaded -> 409.** `source_at`, `step_line` and `run_to_line`
  return 409 `Conflict` "No listing loaded" until `listing/load` succeeds.
- **`clear` is not applied.** MCP `load_listing` forwards a `clear` flag, but
  the `listing/load` handler reads only `path`; whether a second load
  replaces or merges the first is not confirmed from the handler. Load one
  listing per session, or verify `lines` after loading another.
- **Address outside the listing -> 404** (`source_at`), with the covered
  range in `hint`. Only lines with code bytes have addresses.
- **Step limits are t-states, not instructions.** A `step_line` that hits
  `max_tstates` returns normally with `line_changed:false` and the message
  "Stopped without reaching a different source line"; a `run_to_line` that
  does returns `status:"timeout"`. Check those fields instead of assuming
  success.
- **Breakpoints do not fire during `step_line` / `run_to_line`.** Both skip
  breakpoints; a routine you want to catch with a breakpoint needs
  `control_execution resume`, not `run_to_line`.
- **Label file extensions.** The MCP schema text mentions `.sld` / `.lbl`
  files, but the loader's extension table does not list them (see above);
  an unrecognized extension is only accepted if the first line looks like a
  map or VICE file. Prefer `.sym`, `.map`, `.vice`, `.s`/`.asm` or `.z88`
  names, or `POST /labels` directly. Whether `.sld` loads is not confirmed.
- **`POST /labels` conflict.** An existing name or invalid parameters returns
  409 `Conflict` "Label already exists or invalid parameters"; update with
  `PUT /labels/{name}` instead (it changes only `active`, `comment`, `type`,
  `module`, not the address).
- **Assemble defaults differ by transport.** MCP `write` defaults to `true`,
  WebAPI to `false`; the WebAPI also rejects a missing `address` (400) while
  the MCP schema marks it optional. Always pass both explicitly.
- **Disasm count limits differ.** MCP clamps `count` to 256, the WebAPI
  handler to 100.
