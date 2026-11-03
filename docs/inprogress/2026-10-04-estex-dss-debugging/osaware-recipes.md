# OS-aware analyzers: MCP / WebAPI / CLI / Lua / Python recipes

Applies to both OS-aware analyzers: `nedoos` (PLAN #78) and `dss` (PLAN #99).
Throughout, `{os}` = `nedoos` | `dss`, `{id}` = the emulator UUID. Requires a build
after `cd4b9857b` (the merge of both analyzers): before it the MCP tool
`analyze_performance` does not know the `analyzers` op, and the WebAPI answers
"Events not implemented".

## 1. MCP (port 8092, streamable HTTP `POST /mcp`)

Tool `analyze_performance`, `action: "analyzers"`:

```jsonc
// list and status
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "list"}}
// enable/disable (the traps are set after the kernel is detected - on the first frame)
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "enable", "name": "dss"}}
// event stream (the same for nedoos)
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "events", "name": "dss"}}
// clear the ring
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "clear", "name": "dss"}}
```

OS snapshot (tasks/blocks/handles): the MCP `analyzers` operation has no separate `state` op,
it is taken with the generic tool (everything the WebAPI has is reachable this way):

```jsonc
{"name": "invoke_api", "arguments": {"method": "GET",
  "path": "/analyzer/dss/events?include=snapshot&limit=5"}}
```

## 2. WebAPI (port 8090)

```bash
# list of analyzers
curl -s http://localhost:8090/api/v1/emulator/$ID/analyzers
# enable
curl -s -X PUT http://localhost:8090/api/v1/emulator/$ID/analyzer/dss \
     -H 'Content-Type: application/json' -d '{"enabled":true}'
# events (last N) + profile/detection
curl -s "http://localhost:8090/api/v1/emulator/$ID/analyzer/dss/events?limit=50"
# the same with a snapshot: tasks[], blocks[] (pages/blocks by owner), handles[]
curl -s "http://localhost:8090/api/v1/emulator/$ID/analyzer/dss/events?include=snapshot&limit=1"
# clear
curl -s -X DELETE http://localhost:8090/api/v1/emulator/$ID/analyzer/dss/events
```

Event schema: `kind` (`OS_DETECTED|PROFILE_UNKNOWN|SYSCALL|TASK_START|TASK_EXIT|
PAGE_ALLOC|PAGE_FREE|FILE_OPEN|FILE_CLOSE|FINDING`), `severity`, `pc`, `caller`,
`task_id`, `syscall`, `message`, `detail`, `frame_number`, `formatted`.

The NedoOS snapshot (since 2026-10-24) is richer: tasks have `name` (the first word of the command line
from the task page), `stdin/stdout/stderr` (pipes 0x80+) and `last_time` (kernel quanta;
the relative share = CPU%, which CLI `analyzer nedoos state` prints),
plus `pipes[]` (ends/reader/lines/bytes) and `handles[]` from `ffilearray`
(open FAT files with owner, position and size). Example of a live snapshot
(idle → term.com → cmd.com, pipes crossed): task 2 'term.com' io=#80/#81,
task 3 'cmd.com' io=#81/#80, pipe #80 reader=2, pipe #81 reader=3.

## 3. CLI (telnet :8765)

```
analyzer dss enable
analyzer dss events --limit=100
analyzer dss state        # tasks, block owners, file handles
```

## 4. Lua / Python

```lua
analyzer_enable("dss")
local ev = analyzer_events("dss", 100)
local st = analyzer_state("dss")   -- tasks/blocks/handles
```

```python
em.analyzer_events("dss", limit=100)
em.analyzer_state("dss")
em.analyzer_clear("dss")
```

## 5. Ready-made scenarios

### 5.1 Bring up a Sprinter with DSS and look at tasks/memory (full flow)

```bash
# create a machine and insert a disk (the image is built by scratch/osaware-live/build_dss_hdd.py from the spec
# BuildDssHdd in core/tests/emulator/machines/sprinter/sprinterdssmedia.h)
ID=$(curl -s -X POST http://localhost:8090/api/v1/emulator/create \
     -H 'Content-Type: application/json' -d '{"model":"SPRINTER","ram":4096}' \
     | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')
curl -s -X POST http://localhost:8090/api/v1/emulator/$ID/media/ide0.master/insert \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/osaware-live/dss171-session.img","access":"session"}'
curl -s -X POST http://localhost:8090/api/v1/emulator/$ID/reset -d '{}' >/dev/null
curl -s -X POST http://localhost:8090/api/v1/emulator/$ID/start >/dev/null   # NOT /emulator/start with the id in the body - it creates a new instance
sleep 15                                                                    # DSS boots in real time
# then - the MCP calls from §1 (enable -> events -> snapshot)
```

NedoOS: the same with `{"model":"ATM3"}` and the `sd.zc` slot (a host folder with the sdcard fixture,
`InsertOptions.freeBytes >= 16 MB`); after boot, `analyzer nedoos …`.

### 5.2 "Shout to the console if OS memory was not returned"

1. `analyzer {os} enable` before starting the program under test.
2. Take a baseline: `GET .../events?include=snapshot` (remember `blocks`/`handles`).
3. Run the program in the guest OS and let it finish.
4. `GET .../events?limit=200` and filter `kind=FINDING`:
   - NedoOS: `task N exits owning K page(s)`: the task exited while owning pages
     (the kernel cleans up, the application forgot `CMD_DELPAGE`);
   - DSS: `task N exits owning block(s)` (a forgotten `FREEMEM`) and
     `task N exits leaving file handle(s) open` (class C: LEAVE does not close files).
   Every finding carries `pc`/`caller`: the address of the culprit.

### 5.3 A program's "greediness" profile

- stream: `PAGE_ALLOC`/`PAGE_FREE` by `task_id` → how many pages the task held
  at each moment (peak = max simultaneously live);
- `blocks` from the snapshot after the program exits ≠ the baseline → a permanent leak
  (BIOS level, class B: the alloc bypassed MEMTAB);
- DSS `handles[]` from the snapshot → unclosed files by name.

## 6. Pitfalls

- The analyzers are **not active by default** and do not disturb machines without their OS:
  on a "foreign" machine detection stays silent (no events, no error).
- Detection fires on the first frame after enable: events come only after
  `OS_DETECTED`; on an unknown build, `PROFILE_UNKNOWN` (vector mode:
  a call stream without structures/audit).
- Hot calls (NedoOS YIELD/PRCHAR, DSS WAITKEY/PUTCHAR/SETWIN mapper #C4)
  do not go into the ring: they are counted, so they do not push out useful events.
- Tests/automation that run frames directly must use
  `RunFrame(false)` (without skip-breakpoints), otherwise the traps do not fire.
- Automation that changes the emulator without `take_control` gets an implicit session;
  for long runs take control (`POST /control/session`, MCP
  `take_control`) and release it afterwards.
