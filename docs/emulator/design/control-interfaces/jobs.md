# Jobs: long verbs without timeouts or sleep-polls

PLAN #122 S2 ([analysis.md §S2](../../../inprogress/2026-11-12-re-api-efficiency/analysis.md)). A long verb (an
analysis build, a TTD dump or load, `probe-frames`, `memory-series`, ...) can run as a **job**: the call answers at
once with the job's id, the work runs on a registry worker, and any plane reads, waits for (a long-poll, no sleep) or
cancels it. A client never loses an answer to an HTTP timeout and never polls a status every few seconds.

Without `async` every verb answers as before (blocking until its end); the job model is an addition.

Code: `core/src/emulator/jobs/jobregistry.h` (the registry), `core/src/emulator/jobs/jobcontrol.h` (the `jobs` verbs
every plane calls). Planes: [command-interface.md §19](./command-interface.md#19-jobs) (the table of every surface),
[webapi-interface.md](./webapi-interface.md#jobs), [websocket-interface.md](./websocket-interface.md#jobs),
[cli-interface.md](./cli-interface.md#jobs), [lua-interface.md](./lua-interface.md#jobs),
[python-interface.md](./python-interface.md#jobs), MCP [`jobs`](../../../features/mcp/README.md#jobs).

## 1. The verbs that run as jobs

| Domain | Verb (`async=true`) | Kind | Cancel |
| :--- | :--- | :--- | :--- |
| TTD | `dump`, `load`, `find-event`, `replay-check`, `export-clip`, `branch-create` | `ttd.<verb>` | no: runs to its end (a cancel is refused, 409) |
| TTD | `probe-frames`, `memory-series` | `ttd.<verb>` | yes: stops at the next frame, the result holds what it got (`cancelled: true`, `truncated`, `next_from_frame`) |
| TTD | `journal-build` | `ttd.journal-build` | yes: through `journal-build-cancel` |
| Analysis | `build` | `analysis.build` | yes: stops at the next replayed frame |
| Analysis | `trace`, `experiment`, `cover` | `analysis.<verb>` | no |

`async=true` on any other verb is refused (400) with the list; `async=false` is the call as before. `build` takes
`async` or `wait`, not both. The generic form `jobs start domain=<ttd|analysis> verb=<verb> <options>` runs any of
them (the same as the verb with `async=true`); the planes without a per-verb `async` (CLI, Lua and Python TTD
methods) use it.

The TTD jobs of an instance run one at a time in submission order (lane `ttd@<id>`), and so do its analysis jobs
(lane `analysis@<id>`); lanes run side by side on a pool of 4 workers. A queued dump after a running `probe-frames`
of the same instance waits for it, a build of another instance does not.

## 2. A job

```
queued ──► running ──► done
   │          ├──────► failed      (the verb refused or threw: status, error)
   └──────────┴──────► cancelled   (queued: never ran; running: its work stopped early)
```

| Field | Meaning |
| :--- | :--- |
| `job` | id, unique in the process (1, 2, ...) |
| `kind`, `summary` | `ttd.dump`, `dump path=/tmp/a.ttd` |
| `emulator_id` | the instance it works on |
| `state` | `queued`, `running`, `done`, `failed`, `cancelled` |
| `progress` | `{done, total, unit, percent}` (`total` 0: the end is not known ahead, `done` counts) |
| `cancellable`, `cancel_requested` | whether a cancel can stop it; whether one was asked |
| `created_ms`, `started_ms`, `ended_ms`, `elapsed_ms` | wall clock (ms since the epoch), the time it took |
| `queue_position` | queued: jobs ahead of it in its lane |
| `status`, `error`, `text`, `result` | when it ended: the HTTP status the call would have answered, the refusal, the readable answer, the verb's answer (its body as without `async`) |
| `result_dropped` | the result's payload went over the retention budget (the text stays) |

The text of a job is one line plus the result's text: `job 12 ttd.dump on 0f3a… (dump path=/tmp/a.ttd): done in
41 ms` and below it the verb's own text (or `HTTP 200: {compact JSON}` cut at 1500 characters). An MCP client that
reads only the text sees the id, the state and the result.

## 3. The `jobs` verbs

| Action | Options | Answer |
| :--- | :--- | :--- |
| `list` | `instance` (`this`, the selected one, the default when there is one; `all`; an id), `state` (`active`, `finished`, `all`), `limit` (newest N, default 50, 0..1000) | `{instance, state, count, total, active, jobs: [...], limits}` |
| `status` | `job`, `result` (false: without it) | the job |
| `wait` | `job`, `wait_ms` (default 30000, 0..60000; more is refused, 400), `result` | the job plus `wait_ms`, `ended`; still running: the text says "wait again" |
| `cancel` | `job` | the job plus `cancel` (`cancelled`: it was queued; `requested`: it runs and was asked); refused 409 when it ended already or cannot stop, 404 when unknown |
| `start` | `domain`, `verb`, the verb's options | 202 with `{job, state, kind, emulator_id, summary, async: true, status_url, wait_url, text}` |

A verb started with `async=true` answers the same 202 body, plus its own fields where it has some (an analysis
`build` keeps `bundle`, `frames`).

`wait` blocks the calling thread (a script's, a CLI session's). The WebAPI long-poll holds **no** thread: §4.

## 4. Long-poll (REST, WebSocket)

`GET /api/v1/jobs/{job}?wait_ms=N` registers a watch on the registry and a timer on the HTTP event loop, then
returns. The thread that ends the job calls the watch; the timer fires after `N` ms; whichever is first answers (an
atomic flag: one answer), the other does nothing (the timer takes its watch back). No server thread waits.

- `wait_ms` 0 or absent: the job now. 1..60000: the answer comes when the job ends or after `wait_ms`, with `ended`.
  The cap (60 s, `jobs::kMaxWaitMs`) is under the MCP bridge's 300 s and every client's usual timeout; a longer job is
  waited for again.
- At most 256 long-polls wait at once (`Limits::maxWatches`); one more is answered at once (`ended: false`).
- Over the WebSocket the long-poll is a `call` like every route (`{"op":"call","method":"GET","path":"/api/v1/jobs/12?wait_ms=30000"}`);
  its `result` comes when the job ends. The socket's other calls and pushes go on meanwhile (async I/O).

## 5. Topics

| Topic | Scope | Value |
| :--- | :--- | :--- |
| `jobs.progress` | an instance | `count`, `active` [{`job`, `kind`, `summary`, `state`, `done`, `total`, `unit`, `percent`, `cancel_requested`}]: its queued and running jobs |
| `jobs.done` | an instance | `last_job`, `recent` [{`job`, `kind`, `summary`, `state`, `status`, `error`, `ended_ms`}] (the last 8 that ended, newest first) |

The registry tells the instance's readouts when a job is queued, starts or ends, and at most every 100 ms of a job's
progress (`kProgressNotifyMs`); the hub's cadence coalesces the pushes. The topic values read the registry's
published state under a short lock: never the emulation thread, never the worker. `recent` holds more than one end,
so two jobs that end inside one coalesced push are both there. `analysis.progress` / `analysis.ready` stay as
they were for the build.

## 6. Retention and bounds

| Limit | Value | When over it |
| :--- | :--- | :--- |
| Jobs queued | 64 | the submit is refused (409, "the job was not queued: ...") |
| Workers | 4 | lanes wait for a free one |
| Finished jobs kept | 64 | the oldest that ended is forgotten (404 on `status`) |
| Kept for | 30 min after its end | forgotten |
| Results kept in all | 64 MiB (approximate) | the oldest results lose their payload (`result_dropped`; the text, cut at 4096 characters, stays) |
| Long-polls at once | 256 | answered at once |

The limits are in every `list` answer (`limits`).

## 7. Lifetime

A job's work looks its instance up when it starts and holds a context lease while it runs (as every WebAPI call
does). Removing the instance cancels its queued jobs (they end `cancelled`, never run) and asks its running ones to
stop; the removal's lease wait then waits for them. A job of an instance that is gone when its turn comes ends
`failed` (404). At exit the registry ends what is queued (503) and waits for the running jobs.

## 8. Example

```
→ POST /api/v1/emulator/0/ttd/dump  {"path": "/tmp/a.ttd", "async": true}
← 202 {"job": 12, "state": "queued", "kind": "ttd.dump", "status_url": "/api/v1/jobs/12",
       "wait_url": "/api/v1/jobs/12?wait_ms=30000", "text": "Job 12 (ttd.dump: dump path=/tmp/a.ttd) queued on 0f3a…. Its end: ..."}
→ GET /api/v1/jobs/12?wait_ms=30000
← 200 {"job": 12, "state": "done", "ended": true, "status": 200, "result": {"ok": true, "path": "/tmp/a.ttd",
       "bytes": 1843200, ...}, "text": "job 12 ttd.dump on 0f3a… (dump path=/tmp/a.ttd): done in 41 ms\n..."}
```

MCP: `time_travel {action: dump, path, async: true}`, then `jobs {action: wait, job: 12}`. Python:
`j = emu.jobs('start', domain='ttd', verb='dump', path='/tmp/a.ttd'); emu.jobs('wait', job=j['job'])`.
