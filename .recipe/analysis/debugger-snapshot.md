# Recipe: the debugger snapshot and binary memory reads

A debugger front end (a TUI, a web page, a script) redraws its window after every step. Instead of ~20 separate
requests it asks for one **snapshot**: registers, the registers at the previous stop, what is paged in, the stack,
the time, code from PC and any memory windows - all read at **one moment**, so the panels never disagree. Large
memory reads come as **raw bytes**. Design:
[2026-10-04-debugger-snapshot/tdd.md](../../docs/inprogress/2026-10-04-debugger-snapshot/tdd.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side
> pipelines or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## When to use what

| You want | Use |
|---|---|
| One redraw of a debugger window | the snapshot (`disasm`, `stack`, `memory` windows as needed) |
| "Did anything change since my last redraw?" | the snapshot's `seq`: equal = nothing changed |
| What changed in the registers | `regs` vs `prev_regs` (the registers at the previous stop) |
| A whole 64K or page dump for a tool | `format=binary` on the memory reads (WebAPI), `mem_read_bytes` (Lua / Python), `memory save` (CLI) |

Where the snapshot was read: `consistency` is `paused` (the emulator was paused: nothing could change), `frame`
(a running machine, read between two frames on the emulation thread - no pause, no audio gap) or `stopped` (the
emulator was never started).

## MCP (preferred)

```text
# Registers, previous registers, pages, stack, time, 21 lines of code from PC, two memory windows
inspect_state {"aspects":["snapshot"],"count":21,"windows":["cpu:0x8000:256","ram5:0x1800:768"]}
# -> [snapshot] seq 42, paused (read paused), last stop breakpoint, PC=8000, frame 100 t 1234, 21 code line(s), 2 memory window(s)
```

The structured answer is the WebAPI object below; memory windows carry `base64`.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1

# The small part: seq, state, pause, consistency, regs, prev_regs, pages, stack (8 words), time
curl -s "$BASE/emulator/$EMU_ID/debug/snapshot" | jq '{seq, state, consistency, pause, pc: .regs.special.pc, pages}'

# Plus code and memory (windows repeat or go comma-separated; at most 8, each at most 65536 bytes)
curl -s "$BASE/emulator/$EMU_ID/debug/snapshot?disasm=21&stack=16&memory=cpu:0x8000:256&memory=ram5:0:6912" \
  | jq '{seq, code: [.disasm[] | "\(.address) \(.mnemonic)"], windows: [.memory[] | {space, address, length}]}'

# The bytes of a window
curl -s "$BASE/emulator/$EMU_ID/debug/snapshot?memory=ram5:0:6912" | jq -r '.memory[0].base64' | base64 -d > screen.bin

# Raw memory: the whole CPU view in one request (headers X-Unreal-Space / -Address / -Length)
curl -s "$BASE/emulator/$EMU_ID/memory/0x0000?len=65536&format=binary" -o all.bin
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/5?offset=0&length=16384&format=binary" -o ram5.bin
```

A polling client keeps the last `seq` and skips the redraw while it stays the same. A 503 answer means no
coherent moment came within 500 ms (the emulator was being stepped from another client): ask again.

## Lua / Python / CLI

```lua
snap = debug_snapshot{disasm = 21, stack = 8, memory = {"cpu:0x8000:256"}}
print(snap.seq, snap.consistency, snap.regs.special.pc, #snap.memory[1].bytes)
all = mem_read_bytes(0, 65536)                 -- the CPU view as one string
```

```python
snap = emu.debug_snapshot(disasm=21, memory=["ram5:0:6912"])
screen = snap["memory"][0]["bytes"]           # bytes
all64k = emu.mem_read_bytes(0, 65536)
```

```text
debug-snapshot --disasm 8 --stack 4 --memory cpu:0x8000:32
memory save cpu:0:65536 all.bin
```

## Fields (summary)

`seq`, `cpu` (`z80`), `state` (paused | running | stopped), `pause` {`reason` none | pause | breakpoint | step,
`breakpoint_id`, `address`}, `consistency`, `regs` (the `GET /registers` object), `prev_regs` (null before the
first stop), `pages[]` {`window`, `start`, `kind` rom | ram | cache, `page`}, `stack` {`sp`, `words[]`},
`time` {`frame`, `t`, `frame_t`, `line`, `dot`}, `disasm[]` (the `GET /disasm` lines), `memory[]` {`space`,
`address`, `length`, `base64` (WebAPI, MCP) or `bytes` (Lua, Python, CLI)} or {`space`, `address`, `error`}.
