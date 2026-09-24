# Recipe: Record with TTD, Then Freely Navigate + Inspect Any Point

Goal: record a session through the interesting section, then move freely
back and forth to any frame and get a **guaranteed-stable** snapshot of
that exact moment — registers, memory, and a screenshot that's actually
of that frame, not of "whatever the live machine happens to be doing when
the capture call lands."

Use this instead of live pause/screenshot cycles whenever you need to be
certain two inspections (or a screenshot you're comparing against a
person's own screenshot) are of the *same* instant. A live `pause` +
`capture/screen` round-trip is not that guarantee — the present/latch
pipeline can have a frame or two of delay, and if the GUI's own render
path diverges from the capture endpoint's framebuffer (a real
possibility, not hypothetical — it happened during this session's ATM
video investigation), the two will visibly disagree even though the
machine is genuinely paused. Seeking TTD to a recorded frame removes that
ambiguity: the same frame, seeked twice, reads back bit-identical state.

This composes three recipes that already exist — read them for full
parameter detail:
[ttd-recording.md](ttd-recording.md) (start/stop/seek/bookmarks),
[../_common/setup.md](../_common/setup.md) §7 (screen_ocr/digest/image),
and [../articles/bug-hunt-ttd.md](../articles/bug-hunt-ttd.md) (the
memory-corruption-hunt variant of this same idea, `find-last` +
`reverse-continue`). This recipe is the *simpler* general-purpose version
for "let me just look around" rather than "who wrote this address."

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `time_travel` bookmarks, `inspect_state` and `capture_media` cover
> inspection, `invoke_api` the TTD transport. Use [WebAPI](#webapi) only
> inside host-side Python/bash pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# 1. record through the section you care about
emulator_manage    {"action":"create","model":"ATM710","ram_size":1024}
load_software      {"path":"scratch/demo.trd","autostart":true}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/start","body":{}}
control_execution  {"action":"run_frames","frames":2000}   # or resume+poll until the symptom
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/stop"}   # -> "detached", now scrubbable

# 2. bookmark anything worth returning to by name
time_travel        {"action":"bookmark_add","label":"glitch"}

# 3. seek to an exact frame (absolute, or by bookmark) and inspect it
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/seek","body":{"frame":3520}}
inspect_state      {"aspects":["registers","video","screen_ocr"]}
capture_media      {"action":"screenshot","mode":"full","format":"png",
                   "filename":"scratch/frame-3520.png"}   # server-side save: ../media/agent-screenshot-view.md

# 4. step one frame/instruction at a time around that point
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/step-back"}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/step-forward"}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/step-instruction","body":{"dir":"back"}}

# 5. dump memory at this exact frame for byte-level checks
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/memory/read/0xC000",
                   "query_params":{"length":256,"format":"full"}}
```

## WebAPI

The same flow over curl — right choice when a bash/Python pipeline drives
the inspection loop.

```bash
# record → stop → seek to the frame of interest
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" -H 'Content-Type: application/json' -d '{}' | jq '.state'
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop"  | jq '.state'
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/seek" -H 'Content-Type: application/json' \
     -d '{"frame":3520}' | jq '{reached, arrived_at}'

# inspect that exact frame
curl -s "$BASE/emulator/$EMU_ID/state/registers" | jq '{pc, af, hl}'
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png&mode=full&path=scratch/frame-3520.png" | jq '{saved, file}'

# byte-level checks at this frame
curl -s "$BASE/emulator/$EMU_ID/memory/read/0xC000?length=256&format=full" | jq '.data[:8]'
```

## Why record first instead of just pausing live

- **Reproducibility.** `ttd/seek` to frame N always lands on the exact
  same CPU/memory/video state. Re-running `run_frames` from a cold start
  to "get back to where I was" is not guaranteed to land on the same
  frame if anything upstream is nondeterministic (audio timing, input
  polling, turbo/speed-multiplier drift).
- **Free back-and-forth.** `step-back`/`step-forward`/`seek` all work
  once recording has stopped (`ttd/stop` — scrubbing while still
  `recording` is a `409`). You can walk one frame at a time across a
  glitch boundary to see exactly which frame introduced it, then
  `step-instruction` within that frame to find which write did it.
- **A screenshot of a seeked frame IS the frame.** There's no present-queue
  latency or live-vs-capture divergence to worry about — TTD restores
  full state (including the framebuffer) to that recorded point before
  you inspect it.

## Pitfalls (shared with ttd-recording.md, repeated because they bite here specifically)

- `ttd/seek`/`step-*` while `state: "recording"` → `409`. Call `ttd/stop`
  first (history is retained, not discarded).
- Development mode (`{}`, the default on `ttd/start`) costs ~64MB
  journal + page store for a long capture — fine for "record a few
  thousand frames to inspect a glitch," not for hours-long sessions.
- If you need the *live* machine to keep running from a past point
  (rather than just inspecting it), `ttd/seek` then `ttd/resume` —
  otherwise seeking alone leaves the instance paused at that frame,
  which is exactly what you want for inspection.
- Comparing your capture against a human's screenshot of the *live* GUI:
  if they're not also using TTD, you're comparing a reproducible frame
  against a moving target — prefer walking them through recording +
  seeking too if the comparison needs to be exact.
