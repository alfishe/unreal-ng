# Debugger snapshot and binary memory reads (D7 + E3)

Status: design, 2026-10-04. Plan and progress: [TODO.md](TODO.md).

Sources of the requirement:

- [webapi-gap-analysis.md](../2026-09-24-tui-debugger/webapi-gap-analysis.md): row D7 (binary memory), row E3
  (`/debug/snapshot`), finding F3 (too many round trips per repaint), §3.4 (consistency).
- [protocol.md](../2026-09-28-debugger-model/protocol.md) §3.17 `Snapshot`: the composite a debugger front end
  ("skin") asks for after a pause.

## 1. The problem in one example

A debugger window (the TUI, a web front end, a script) redraws after every step. Today one redraw costs:

| Request | Why |
|---|---|
| `GET /registers` | the register panel |
| `GET /disasm?address=PC&count=21` | the code panel |
| `GET /memory/{addr}?len=4096` x 16 | a 64 KB memory panel: 4096 bytes is the cap, as JSON |
| `GET /state/paging`, `/video/beam`, ... | pages, beam position |

About 20 HTTP requests and ~450 KB of JSON per step. Holding the step key down (~30 steps per second) means
~13 MB/s of JSON to parse. And while the machine runs, each request sees a different moment: the register panel
can be several frames newer than the memory panel.

After this work the same redraw is one request:

```text
GET /api/v1/emulator/{id}/debug/snapshot?disasm=21&memory=cpu:0x8000:256&stack=8
```

and a client that only wants memory gets raw bytes:

```text
GET /api/v1/emulator/{id}/memory/0x0000?len=65536&format=binary      -> 65536 bytes, application/octet-stream
```

## 2. Compatibility (nothing existing changes)

Checked against the current code (master `98643fd3b`):

| Endpoint / function | Today | After | Compatible because |
|---|---|---|---|
| `GET /memory/{addr}` | `len` 1..4096 (clamped), `format` hexdump \| full \| sparse; any other `format` is 400 (`debug_api.cpp:1645`) | adds `format=binary`, `len` up to 65536 for it only | `binary` was an error before; JSON formats keep the 4096 cap and their shape |
| `GET /memory/read/{addr}` | `length` (default 128), formats as above, other values 400 (`state_memory_api.cpp:574`) | adds `format=binary` | as above. Bug fixed on the way: `length` is cast to 16 bits, so `65536` reads 0 bytes today (`state_memory_api.cpp:561`); it will read 65536. No client can rely on getting nothing |
| `GET /memory/page/{type}/{page}` | `offset`, `length` (clamped to the page), hex / sparse | adds `format=binary` | additive |
| `GET /memory/{type}/{page}/{offset}` | `len`, hex only, no `format` parameter | adds `format=binary` | a request without `format` (every existing one) answers as before |
| `GET /memory/region/{name}` | hex / data / sparse (JSON up to 65536) / **binary already** (any length, headers `X-Region`, `X-Offset`) | binary answers also carry the `X-Unreal-*` headers below; `X-Region` / `X-Offset` stay | additive headers only |
| `GET /debug/snapshot` | does not exist; no `/debug/...` route exists | new | new path |
| Lua / Python `mem_read_block` | Lua: a table of numbers; Python: `bytes`; CPU view, `len` 16-bit | unchanged | new functions instead of changed ones (§4) |
| CLI `memory save` | writes the CPU view to a file | unchanged; may gain `--space` (§4) | flag is optional |
| MCP `inspect_state` `memory` | `size` up to 4096, hexdump / full / sparse | unchanged; new `snapshot` aspect | additive |

Rule for the whole work: **only new parameter values, new routes and new functions**. Existing defaults, caps,
field names and types stay as they are.

## 3. Binary memory reads (D7)

`format=binary` on the memory read endpoints of §2 (the region read has it already):

- Body: the bytes, nothing else. `Content-Type: application/octet-stream`.
- Headers say what was read, so a client needs no second request: `X-Unreal-Space` (`cpu`, `ram5`, `rom2`, ...;
  a region's name), `X-Unreal-Address` (the start, hex), `X-Unreal-Length` (bytes in the body).
  `Access-Control-Expose-Headers` lists them, so a browser client can read them too.
- Length: CPU view up to 65536 (wrapping at #FFFF as the CPU sees it, the same as the JSON reads); a page up to its
  size from the offset; a region any length, as its binary read already allows.
- Errors stay JSON (400 / 404 with the usual `{error, message}`): a client always knows from the status code which
  it got.
- Reads are side-effect free (`DirectReadFromZ80Memory` and the page pointers), as the JSON reads are.

The reading itself moves into one core function, `MemoryRead::Bytes(context, space, address, length)`
(`core/src/debugger/memory/`), reusing the space names and parsing of `MemorySearch` (D8). The JSON formats are
rendered from the same bytes.

## 4. The snapshot (E3)

### 4.1 Contents

```json
{
  "seq": 1842,
  "cpu": "z80",
  "state": "paused",
  "pause": {"reason": "breakpoint", "breakpoint_id": 3},
  "consistency": "paused",
  "regs":      { "...": "the same object as GET /registers" },
  "prev_regs": { "...": "the same object, at the previous stop; null before the first one" },
  "pages":     [{"window": 0, "start": "0x0000", "kind": "rom", "page": 0}, "..."],
  "stack":     {"sp": "0xFF4A", "words": [4660, 32768, "..."]},
  "time":      {"frame": 1024, "t": 17920, "frame_t": 69888, "line": 79, "column": 128},
  "disasm":    [{"...": "the same objects as GET /disasm instructions[]"}],
  "memory":    [{"space": "cpu", "address": "0x8000", "length": 256, "base64": "..."}]
}
```

| Field | Meaning | Source |
|---|---|---|
| `seq` | grows by one at every stop, run start and tool edit of this emulator; equal `seq` = nothing changed | new counter in `Emulator` |
| `cpu` | `z80` (the field exists so the sound card CPUs can be added later without breaking clients) | - |
| `state` | `paused` \| `running` | `Emulator::IsPaused` |
| `pause` | why it stopped last: `pause`, `breakpoint` (+ `breakpoint_id`), `step` | the last `PauseEvent` (protocol §3.16) |
| `consistency` | `paused` (read while parked) or `frame` (read at a frame boundary of a running machine) | §4.3 |
| `regs` | exactly the `GET /registers` object | shared builder, §4.4 |
| `prev_regs` | the registers at the previous stop, for "what changed" highlighting | §4.5 |
| `pages` | what each 16 KB window of the CPU maps now (kind, page) | the memory bank state (`/state/memory` banks) |
| `stack` | `stack=<n>` words from SP (default 8; 0 = omit) | memory at SP |
| `time` | frame counter, T-state in the frame, frame length, beam line / column | `/video/beam` data |
| `disasm` | `disasm=<n>` lines from PC (default 0 = omit; at most 100), the `GET /disasm` line objects | shared builder |
| `memory` | `memory=<space>:<addr>:<len>`, repeatable, at most 8 windows, each at most 65536; bytes as base64 | `MemoryRead::Bytes` |

Query: `GET /debug/snapshot?disasm=21&stack=8&memory=cpu:0x8000:256&memory=ram5:0:6912`. A bare
`GET /debug/snapshot` returns `seq` ... `time` (the small part).

### 4.2 Why one call

- One round trip per redraw instead of ~20.
- One moment: every part of the answer describes the same machine state (§4.3).
- `seq` lets a polling client skip redraws: same `seq`, same picture.

### 4.3 Consistency: where the snapshot is taken

- **Paused:** read on the calling thread while the emulator stays parked (`Emulator::RunWhileParked`), so no step
  or resume can interleave.
- **Running:** the request is handed to the emulation thread and answered at the next frame boundary (at most one
  frame, 20 ms, of waiting). The machine does not pause, the audio does not drop out. New: a small queue in
  `MainLoop` of "work at the next frame boundary" (`MainLoop::RunAtFrameBoundary`), serviced after the frame, before
  the pause check. A request that is not served within 500 ms (the emulator was paused or stopped meanwhile) falls
  back to the paused path or answers 503.
- The answer says which it was (`consistency`).

Alternative considered: pause, read, resume (as `Emulator::EditMemoryFromTool` does). Rejected for the running
case: every snapshot would stop the sound and the frame pacing; a client polling at 30 Hz would make the machine
stutter.

### 4.4 One builder for every surface

New core module `core/src/debugger/snapshot/debugsnapshot.{h,cpp}`:

- `DebugSnapshot::Registers(context) -> StateNode`: the `GET /registers` object.
- `DebugSnapshot::Disasm(context, address, count) -> StateNode`: the `GET /disasm` lines (labels from the label
  manager).
- `DebugSnapshot::Build(emulator, options) -> StateNode`: the whole snapshot, taking care of §4.3.

`GET /registers` and `GET /disasm` are switched to the shared builders, and a test pins their JSON before and
after (same fields, same types, same order), so they cannot drift apart from the snapshot later.

### 4.5 `prev_regs`

"The previous stop" = the registers when the emulator last stopped before the current stop. The emulator keeps two
register sets: on every stop (a confirmed park after a pause or breakpoint, and the end of a direct step / run on a
control thread) the current set moves to "previous" and the new one is recorded. Cost: one register copy per stop,
nothing per instruction.

### 4.6 Every surface

| Surface | Snapshot | Binary memory |
|---|---|---|
| WebAPI | `GET /debug/snapshot` | `format=binary` (§3) |
| OpenAPI | the route, parameters, response schema | `format` enum + `application/octet-stream` responses |
| MCP | `inspect_state` aspect `snapshot` (`disasm`, `stack`, `memory` arguments) with a one-line summary | `inspect_state` `memory`: unchanged (JSON transport); large binary reads go through `snapshot` memory windows (base64) |
| Lua | `debug_snapshot{disasm=21, stack=8, memory={"cpu:0x8000:256"}}` -> table (memory windows as Lua strings) | `mem_read_bytes(addr, len [, space])` -> Lua string |
| Python | `debug_snapshot(disasm=21, stack=8, memory=["cpu:0x8000:256"])` -> dict (memory as `bytes`) | `mem_read_bytes(addr, len, space="cpu")` -> `bytes` |
| CLI | `debug-snapshot [--disasm N] [--stack N] [--memory space:addr:len]` (`snapshot` is the existing .sna load / save command) -> text: registers, pages, time, code, stack, hexdump | `memory save <space>:<addr>:<len> <file>` (any window to a file; the bank / page forms stay) |
| Qt | not needed: the Qt debugger reads the core directly on the emulator thread. Stated in the docs | - |

Docs: `webapi-interface.md`, `lua-interface.md`, `python-interface.md`, `command-interface.md`, `cli-interface.md`,
the MCP tool text, a recipe `.recipe/analysis/debugger-snapshot.md` (worked examples) and the TUI gap analysis rows
D7 / E3 marked done.

## 5. Tests

| Test | Proves |
|---|---|
| `MemoryRead_Test` | every space and length limit; wrap at #FFFF; a page shorter than asked |
| WebAPI binary vs JSON | the binary body equals the JSON bytes for the same request (checked live with curl against unreal-qt: core-tests do not link the drogon handlers) |
| `/registers`, `/disasm` golden | JSON identical before and after the switch to the shared builders (captured live from a paused snapshot before and after: byte-identical) |
| `DebugSnapshot_Test` paused | each part equals the separate reads |
| `DebugSnapshot_Test` running | all parts describe one frame boundary: `regs.special.t`, `time.t` and the PC's bytes in `memory` agree |
| `prev_regs` | step twice: `prev_regs` of the second snapshot = `regs` of the first |
| `seq` | unchanged with no activity; grows on step, resume, tool edit |
| limits | more than 8 windows, a window over 65536, a bad space -> 400 with the reason |
| Lua / Python / CLI / MCP | each surface returns the same data (one test per surface, the core does the work) |

## 6. Out of scope

- Snapshots of the sound-card CPUs (GS, NeoGS): the `cpu` field is reserved for them.
- A WebSocket push of snapshots: polling with `seq` first; push later if needed.
- TTD positions in the snapshot: the TTD v2 agent owns the timeline objects.
