# Analysis layers

Typed program analysis built from what the emulated CPU does. The design and the phases are in
[docs/inprogress/2026-10-24-analysis-layers](../../inprogress/2026-10-24-analysis-layers/README.md); this page
describes what is built. Phase P1 is in: the **L0 access facts** and the `analysis` verbs on every plane.

## L0 access facts

The memory tracker (`MemoryAccessTracker`) has a level besides its mode:

| Level | What is recorded |
|-------|------------------|
| `counters` | the read / write / execute counters per address, the regions, ports, segments (as before) |
| `facts` | the counters plus, per byte, the facts below |

Per byte (`ByteFacts`, `core/src/emulator/memory/memoryaccessfacts.h`, 16 bytes, one cache line per access):

| Fact | Meaning |
|------|---------|
| kinds | bitmask of `OpcodeFetch` (the first byte of an instruction), `OperandFetch` (any other byte of an instruction: prefixed opcodes, displacements, immediates), `DataRead`, `DataWrite`, `StackRead` (POP, RET, RETI, RETN, EX (SP)), `StackWrite` (PUSH, CALL, RST, EX (SP), interrupt and NMI acceptance), `BlockAccess` (with DataRead / DataWrite: LDI.., CPI.., INI.., OUTI.. and their repeats), `WrittenWhileCode` (written after it was fetched as code: self-modifying code) |
| last reader, last writer | the PC of the instruction of the last data or stack read / write |
| multi reader, multi writer | two or more instructions read / wrote the byte |
| first frame, last frame | the frame of the first and the last access |
| written values | the smallest and the largest value written, and how many distinct values: 1, 2 or 3+ (exact: two values are always the min and the max) |

Stack accesses carry the stack kinds instead of the data kinds. The HALT idle fetch (the byte after a HALT, fetched
and discarded while the CPU waits) is no instruction byte and records nothing.

### Modes

- `z80`: one 64K table (1 MB) for the Z80 address space.
- `physical`: one table per physical page (RAM, ROM, cache, misc), allocated when the page is first touched (256 KB a
  page); the same Z80 address in two banked pages keeps two sets of facts. `explain` takes the page (default: the page
  mapped at the address now).

The facts are allocated with the counters at level `facts` only and freed when the level goes back to `counters` or
tracking is switched off. A reset of the counters (and a new session) clears them; segments do not touch them; a
stopped session keeps them until the next start.

### How a byte gets its facts

The memory code sees reads and writes, not instructions. The Z80 marks the running instruction's class in a
one-byte access context: after the opcode fetch it looks the opcode up in a 256-entry table per prefix (base for
none / DD / FD, ED; CB has no stack or block opcode), interrupt and NMI acceptance set `Stack` around their two
pushes, a halted CPU sets `HaltFetch`. The Z80 stores the context only while a tracker at level `facts` has
attached its byte (`Z80::SetAccessContextSink`): with the tracker off or at `counters`, the cost is one relaxed load
of a null pointer per instruction. Opcode vs operand: an execute-kind fetch at the instruction's M1 PC is the opcode,
every other one an operand.

Cost (M1 Ultra, [memorytracker-p1.md](../../inprogress/2026-10-24-analysis-layers/baselines/memorytracker-p1.md)):
level `facts` stays within the budget of 1.25x level `counters`.

Machines on their own CPU engine (`ICpuEngine`: the engine runs the instruction, not `Z80Step`) get no access
context: their stack and block accesses are recorded as plain data accesses.

## The `analysis` verbs

One core handler, `analysis::Execute(emulator, action, options)` in
`core/src/debugger/analyzers/analysis/analysiscontrol.h`, checks the options, runs the action and builds the answer:
its fields and a `text` (the lines the CLI prints and the MCP tool's text). Every plane is a thin mapping onto it.

| Action | Options | Answer |
|--------|---------|--------|
| `start` | `level` (`facts`, default, or `counters`), `mode` (`z80`, default, or `physical`) | switches on debug mode and memory tracking, sets level and mode, starts a fresh memory session; the status |
| `stop` | - | stops the session (the facts stay readable); the status |
| `status` | - | `state` (capturing, paused, stopped), `level`, `mode`, `frame`, `facts` {allocated, bytes, z80_bytes_touched, pages [{page, name}]} |
| `explain` | `address` (`$80BC`, `#80BC`, `0x80BC`, `80BCh`, decimal or a label), `page` (physical mode: index, `ram:N`, `rom:N`) | `address`, `mode`, `bank` or `page` {page, name, offset}, `label`, `accessed`, `kinds`, `kinds_mask`, `first_frame`, `last_frame`, `last_reader` {pc, label} or null, `multi_reader`, `last_writer`, `multi_writer`, `written` {distinct, min, max}, `counters` {reads, writes, executes} |

Refusals: an unknown action or option, a value that does not parse (400); no such label (404); `explain` at level
`counters` (409); no memory tracker (503).

Example text of `explain`:

```text
$80BC (score) [RAM 2]: DataRead, DataWrite
  written by $8A10 (add_score) (the only writer), values $00..$39 (3+ distinct)
  read by $8B02 (print_score) (last of several readers)
  frames 12..840
  counters: 1210 reads, 403 writes, 0 executes
```

### Planes

| Plane | Shape | Doc |
|-------|-------|-----|
| CLI | `analysis start [facts\|counters] [z80\|physical]`, `analysis stop`, `analysis status`, `analysis explain <addr> [page]`, `--json` for the fields | [cli-interface.md](../design/control-interfaces/cli-interface.md#analysis-layers) |
| REST | `POST /api/v1/emulator/{id}/analysis/start` {level?, mode?}, `POST .../analysis/stop`, `GET .../analysis/status`, `GET .../analysis/explain?address=&page=` | [webapi-interface.md](../design/control-interfaces/webapi-interface.md#analysis-layers) |
| WebSocket | every route as a `call` on `/api/v1/ws`; no topic yet (P2 adds `analysis.progress`, `analysis.ready`) | [websocket-interface.md](../design/control-interfaces/websocket-interface.md#analysis-layers) |
| Python | `emu.analysis(action="status", **options)`: the answer as a dict (with `text`), or `{ok: False, error}` (error: the reason) | [python-interface.md](../design/control-interfaces/python-interface.md#analysis-layers) |
| Lua | `emu.analysis(action, opts)`: opts a table, or a string (the address for `explain`, the level for `start`); a refusal `{ok = false, error}` | [lua-interface.md](../design/control-interfaces/lua-interface.md#analysis-layers) |
| MCP | one tool `analysis` {action: start, stop, status, explain; level, mode, address, page}; `content[]` carries the text | [docs/features/mcp/README.md](../../features/mcp/README.md) |

The MCP tool calls are counted per tool and action in `host.clients.mcp.tools` (`GET /api/v1/monitor/host.clients`):
`{"analysis": {"calls": N, "actions": {"explain": n, ...}}}`, every tool alike (`(none)` for a call without an
action), so a split of `analysis` into several tools can be decided on use.

Overview of all commands: [command-interface.md](../design/control-interfaces/command-interface.md#55a-analysis-layers-l0-access-facts).
