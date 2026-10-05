# Recipe: Record with TTD, Then Freely Navigate + Inspect Any Point

Goal: record a session through the interesting section, then move freely
back and forth to any frame and get a **guaranteed-stable** snapshot of
that exact moment — registers, memory, and a screenshot that's actually
of that frame, not of "whatever the live machine happens to be doing when
the capture call lands."

Use this instead of live pause/screenshot cycles whenever you need to be
certain two inspections (or a screenshot you're comparing against a
person's own screenshot) are of the *same* instant. A live `pause` +
`capture/screen` round-trip is not that guarantee — a screenshot takes the
presented frame, which lags the machine by the present delay (2 frames by
default, more with ZX DLSS), so two calls a moment apart can land on
different frames while the machine is running. (The GUI and the screenshot
read that same presented frame; see
[agent-screenshot-view.md](../media/agent-screenshot-view.md).) Seeking TTD to a recorded frame removes that
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
> `time_travel` records and moves through history, `inspect_state` and
> `capture_media` inspect the chosen point (`invoke_api` only for raw
> memory reads). Use [WebAPI](#webapi) only inside host-side Python/bash
> pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# 1. record through the section you care about
emulator_manage    {"action":"create","model":"ATM710","ram_size":1024}
load_software      {"path":"scratch/demo.trd","autostart":true}
time_travel        {"action":"start"}
control_execution  {"action":"run_frames","frames":2000}   # or resume+poll until the symptom
time_travel        {"action":"stop"}                       # -> "idle" with history, now scrubbable

# 2. bookmark anything worth returning to by name
time_travel        {"action":"bookmark_add","label":"glitch"}

# 3. seek to an exact frame (absolute, or by bookmark) and inspect it
time_travel        {"action":"seek","frame":3520}         # or {"action":"seek_bookmark","label":"glitch"}
inspect_state      {"aspects":["registers","video","screen_ocr","ttd"]}
capture_media      {"action":"screenshot","area":"full","format":"png",
                   "filename":"scratch/frame-3520.png"}   # server-side save: ../media/agent-screenshot-view.md

# 4. step one frame/instruction at a time around that point
time_travel        {"action":"step_back_frame"}
time_travel        {"action":"step_forward_frame"}
time_travel        {"action":"step_back_instruction"}

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
curl -s "$BASE/emulator/$EMU_ID/capture/screen?area=full&format=png&path=scratch/frame-3520.png" | jq '{saved, file}'

# byte-level checks at this frame
curl -s "$BASE/emulator/$EMU_ID/memory/read/0xC000?length=256&format=full" | jq '.data[:8]'
```

For a whole range of frames, `POST /ttd/export-clip` writes a lossless clip in
one call instead of a seek and a capture per frame (see
[ttd-recording.md](ttd-recording.md#inspect-a-file-search-port-journals-export-a-clip)).

## Why record first instead of just pausing live

- **Reproducibility.** `ttd/seek` to frame N always lands on the exact
  same CPU/memory/video state. Re-running `run_frames` from a cold start
  to "get back to where I was" is not guaranteed to land on the same
  frame if anything upstream is nondeterministic (audio timing, input
  polling, turbo/speed-multiplier drift).
- **Free back-and-forth.** `step-back`/`step-forward`/`seek` all work
  once recording has stopped, and while it records they pause it
  (`recording_paused: true`; on `backend: v1` scrubbing while still
  `recording` is a `409`). You can walk one frame at a time across a
  glitch boundary to see exactly which frame introduced it, then
  `step-instruction` within that frame to find which write did it.
- **A screenshot of a seeked frame IS the frame.** There's no present-queue
  latency to worry about — TTD restores
  full state (including the framebuffer) to that recorded point before
  you inspect it.

## Pitfalls (shared with ttd-recording.md, repeated because they bite here specifically)

- `ttd/seek`/`step-*` while `state: "recording"` pause the recording
  (`recording_paused: true`; resume at the paused point continues it). On
  `backend: v1` they answer `409`: call `ttd/stop` first (history is retained).
- Load the software **before** `ttd/start`: a snapshot, tape or disk load
  (and disk create, ROM reload) wipes the recorded history. A reset does
  not — it stops the recording and keeps it.
- While recording, the host speed is held at 1x and turbo / fast tape /
  fast disk are off, so a recording plays at real speed. See
  [command-interface.md → TTD Session Rules](../../docs/emulator/design/control-interfaces/command-interface.md#ttd-session-rules).
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
