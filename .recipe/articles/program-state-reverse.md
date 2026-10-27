# Article: Where a Running Program Keeps Its State — Find, Watch, Disassemble

A worked workflow for learning how a resident program (an assembler's editor, a loader, a game) keeps its state in
RAM: where its data lives, which words are its variables, which code changes them and how. Use it instead of trying
keys and diffing screenshots: four calls give the address, the writer's PC and the code around it.

Worked example: MASM 2.0's text buffer (asm-synchronizer.md §7.12), every call below run on 2026-10-10 against
`MASM2_0D.SCL` on a `PENTAGON` 128K. Companions: [memory-search-map-and-regions.md](../analysis/memory-search-map-and-regions.md)
(find, pages, `disasm/page`), [breakpoints-and-events.md](../analysis/breakpoints-and-events.md) (watchpoints),
[debugger-snapshot.md](../analysis/debugger-snapshot.md) (`debug/wait`), [ttd-write-journal.md](../analysis/ttd-write-journal.md)
and [ttd-reverse-debugging.md](../analysis/ttd-reverse-debugging.md) (`find-last`). "Who corrupted this memory" is
[bug-hunt-ttd.md](bug-hunt-ttd.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `debug_code` searches and disassembles,
> `control_execution` sets breakpoints and waits, `time_travel` answers "who wrote it"; feature switches and page
> reads go through `invoke_api`. Use [WebAPI](#webapi) inside host-side Python/bash pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## The steps

| Step | Question | Call | MASM 2.0 |
|:--|:--|:--|:--|
| 1 | Where is the data (a text, a table) in RAM? | `memory/find` with `space: "ram"`: a known byte run (a file's first bytes) in every page | page 2 `+14DA` = `#94DA` |
| 2 | Which words are the state? | dump the program's pages in two or three states (loaded / typed / moved) and keep the words that change and point into the data | `#92C7`, `#92D3`, `#929F`, the byte `#92CD` |
| 3a | Who writes a word (no stop)? | record with the write journal, act, stop, `find-last` `access: write` | `#92C7` by PC `#92A5`, `#92D3` by `#92A9`, frame 1290 |
| 3b | Who writes it (stop there)? | a write watchpoint (`address` / `address_end`), act, `debug/wait`, read the registers | paused, PC `#92A7` (inside `LD (#92C7),DE`) |
| 4 | What does the code do with it? | `disasm` around the PC (labels apply), `disasm/page` for code in a page not mapped now | `#92C6`: `LD DE,gs` / `LD HL,#89D1` / `LD A,flag` / `OR A` / `JP NZ,#8939` / `LD HL,ge` / `LDI` to `00` |
| 5 | Confirm | change the state once more and read the words again (or a second watchpoint) | the operands move as the cursor moves |

A word that changes but is never read with `LD rr,(nn)` is often an **operand the code rewrites**: `LD DE,nn` at
`#92C6` keeps the gap start in `#92C7`, `LD A,n` at `#92CC` the flag in `#92CD`. Search the code for both forms: the
loads and stores of the address, and the instruction whose operand it is (disassemble from a few bytes before it).

## MCP (preferred)

```text
# 1 - where the text is: its first bytes in every RAM page
debug_code {"action":"find_bytes","pattern_hex":"FF 20 C9 03 AA 04 80 2C","space":"ram"}
#   → page 2, offset 0x14DA

# 2 - pages for a state diff (host side): one page per call
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/2","query_params":{"offset":0,"length":16384}}

# 3a - who wrote a word: record with the journal, act, stop, ask
time_travel {"action":"start","journal":true}
type_input  {"action":"type","text":" nop"}
type_input  {"action":"tap","key":"enter"}
time_travel {"action":"stop"}
time_travel {"action":"find_last","addr":"0x92C7","access":"write"}   # pc 37541 (#92A5), frame, value

# 3b - stop at the write: the breakpoints feature, a watchpoint, wait for the pause
invoke_api        {"method":"PUT","path":"/api/v1/emulator/{id}/debugmode","body":{"enabled":true}}
invoke_api        {"method":"PUT","path":"/api/v1/emulator/{id}/feature/breakpoints","body":{"enabled":true}}
control_execution {"action":"bp_add","type":"write","address":"0x92C7","address_end":"0x92C8"}
type_input        {"action":"type","text":" nop"}
type_input        {"action":"tap","key":"enter"}
control_execution {"action":"wait","timeout_ms":5000}                 # pause: reason breakpoint, address 37575
inspect_state     {"aspects":["registers"]}                          # PC #92A7

# 4 - the code: around the PC, or straight from a page
debug_code {"action":"disassemble","address":"0x929B","count":8}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/disasm/page","query_params":{"type":"ram","page":2,"offset":4806,"count":6}}
```

## WebAPI

```bash
# 1 - the text's first bytes in every RAM page
curl -s -X POST "$BASE/emulator/$EMU_ID/memory/find" -H 'Content-Type: application/json' \
     -d '{"pattern_hex":"FF 20 C9 03 AA 04 80 2C","space":"ram","max":8}' | jq -c '.matches[] | {page, offset}'
#   {"page":{"kind":"ram","page":2},"offset":"0x14DA"}

# 2 - a page as raw bytes, for a host diff between states
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/2?format=binary&offset=0&length=16384" -o scratch/p2-typed.bin

# 3a - write journal and find-last
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" -H 'Content-Type: application/json' -d '{"journal":true}' | jq -c '{state, write_journal_enabled}'
#   ... type, then:
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop" | jq -c .
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" -H 'Content-Type: application/json' \
     -d '{"addr": 37575, "access": "write"}' | jq -c '{found, frame, pc, value, phys_page}'
#   {"found":true,"frame":1290,"pc":37541,"value":230,"phys_page":2}

# 3b - watchpoint, wait, registers
curl -s -X PUT  "$BASE/emulator/$EMU_ID/debugmode" -H 'Content-Type: application/json' -d '{"enabled":true}'
curl -s -X PUT  "$BASE/emulator/$EMU_ID/feature/breakpoints" -H 'Content-Type: application/json' -d '{"enabled":true}'
curl -s -X POST "$BASE/emulator/$EMU_ID/breakpoints" -H 'Content-Type: application/json' \
     -d '{"type":"write","address":"0x92C7","address_end":"0x92C8"}' | jq -c '{id, hit_count}'
#   ... type, then (returns at once when it is already paused):
curl -s "$BASE/emulator/$EMU_ID/debug/wait?timeout_ms=5000" | jq -c '{state, pause}'
#   {"state":"paused","pause":{"address":37575,"breakpoint_id":1,"reason":"breakpoint"}}
curl -s "$BASE/emulator/$EMU_ID/registers" | jq '.special.pc'          # 37543 (#92A7)

# 4 - the code
curl -s "$BASE/emulator/$EMU_ID/disasm?address=37531&count=8" | jq -r '.instructions[] | "\(.address) \(.mnemonic)"'
curl -s "$BASE/emulator/$EMU_ID/disasm/page?type=ram&page=2&offset=4806&count=6" | jq -r '.instructions[] | "\(.offset) \(.mnemonic)"'
```

Host-side Python (`tools/verification/unreal-asm/lib/emulator.py`) wraps the same calls: `emu.post('/memory/find', ...)`,
`emu._request('PUT', emu.base + '/feature/breakpoints', {'enabled': True})`, `emu.get('/debug/wait?timeout_ms=5000')`.

## Pitfalls

- **The `breakpoints` feature can be off with debug mode on**: a watchpoint is added (`active: true`) but
  `hit_count` stays 0 and nothing stops (seen 2026-10-10). Switch it on with `PUT /feature/breakpoints`; read
  `GET /features` when a breakpoint never fires.
- **A memory watchpoint stops during the access**: the PC is inside or just past the writing instruction (`#92A7`
  for `LD (#92C7),DE` at `#92A5`). Disassemble from a few bytes before the PC; `find-last` gives the instruction's
  own address.
- **TTD holds the machine at 1x** with turbo off while it records ([ttd-recording.md](../analysis/ttd-recording.md)):
  waits in a script take real time; stop the recording before fast-forwarding.
- **`disasm` applies the loaded symbols**: in the ROM the names are the 48K ROM's (`MASK-INT`); a label on a RAM
  address may come from a symbol set that does not belong to the program in RAM.
- **Code in a page not mapped now** (an editor in page 2 while a 128K program shows another bank at `#C000`, the
  upper half of a text): read it with `disasm/page` and `memory/page`, not through the CPU view.
- **Instances you create stay** until `DELETE /emulator/{id}`: delete them at the end of a script.
