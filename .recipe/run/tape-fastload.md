# Recipe: Load and Run a Tape Program

Goal: from a fresh machine, get a tape program loaded and executing — with
the fast-load shortcut when the image allows it.

Prerequisites: tape mounted per [insert-tape.md](../media/insert-tape.md).
Model: whatever the program wants (`48K` for classic titles).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `load_software`
> collapses mount+command+play, `type_input` types `LOAD ""`. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
load_software      {"path":"scratch/game.tap","play":true}   # mount + start playback in one shot
invoke_api        {"method":"GET","path":"/api/v1/emulator/{id}/tape"}   # fast_load plan + block estimate
type_input         {"action":"type","text":"LOAD \"\"","tokenized":true}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/tape/seek","body":{"block":17}}
inspect_state     {"aspects":["screen_ocr","screen_digest"]}   # loaded? intro rendering?
```

If `LOAD ""` has not been entered yet, type it before (or right after)
`load_software {"play":true}` — playback alone does nothing without the
loading command (see pitfalls).

## WebAPI

Curl form — right choice when a Python/bash pipeline drives these steps.

### Step 1 — Ask the tape about itself

```bash
curl -s "$BASE/emulator/$EMU_ID/tape" \
  | jq '{blocks: .block_count, fast_loadable: ([.blocks[] | select(.fast_load=="yes")] | length)}'
```

If every payload block is `fast_load: "yes"`, loading takes a fraction of a
second regardless of the on-screen clock. Otherwise budget real time
(`blocks[].seconds` sums the estimate).

### Step 2 — Enter the loading command

`basic/run` handles the 48K/128K editor differences (menu navigation
included):

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/run" \
     -H 'Content-Type: application/json' \
     -d '{"command": "LOAD \"\""}' | jq '{success, basic_mode}'
```

Raw-keystroke alternative:

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/type" \
     -H 'Content-Type: application/json' \
     -d '{"text": "LOAD \"\"", "tokenized": true}' | jq .
```

MCP: `type_input` `{"action":"type","text":"LOAD \"\""}`.

### Step 3 — Start the tape

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/play" | jq '{state, position}'
```

Or let the MCP `load_software` tool do Steps 0–3 in one shot:

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"load_software","arguments":{"path":"scratch/game.tap","play":true}}}' | jq .
```

### Step 4 — Wait for completion (the right way)

Fast-loadable image — poll a bounded number of times:

```bash
for i in $(seq 1 20); do
  STATE=$(curl -s "$BASE/emulator/$EMU_ID/tape" | jq -r '.state')
  [ "$STATE" = "ended" ] && break
  sleep 0.5
done
echo "tape: $STATE"
```

Real-time image — compare against the tape's own estimate, then verify by
content, not by tape state (the program may RUN before the tape reports
`ended`, or the loader may stop the motor early):

```bash
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r '.text'
curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq -r .digest
```

Screen shows the title/game instead of red/cyan loading stripes → loaded.
A stabilized digest across two polls → the intro is rendering.

### Multi-load tapes (level-based games)

1. Play to the point where the game waits for level data.
2. When it says `LOADING...` / the border signals load-in-progress:

```bash
# Either just continue (motor control handled by the game), or seek explicitly:
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/seek" \
     -H 'Content-Type: application/json' -d '{"block": 17}' | jq '.position'
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/play" | jq '.state'
```

Use `GET .../tape/blocks/{index}` to identify which block is which (headers
carry the program name in `.name`).

## Custom loaders (the `fast_load` reject reasons)

Blocks with `fast_load != "yes"` carry a reason (custom loader detected,
non-standard pulse train...). Those play through the beeper in real time —
which is also your chance to [port-trace](../analysis/port-trace.md) the
loader's `IN FE`/edge-detect loop or capture it with
[TTD](../analysis/ttd-recording.md) for reverse execution.

## Pitfalls

- **Command typed but nothing loads**: tape must be both mounted **and**
  playing; check `GET .../tape` → `state == "playing"` while the border
  flashes.
- **Turbo/fast tape settings** can make `play` return `state: "ended"`
  instantly — verify by screen content (digest/OCR), not by tape state.
- **48K vs 128K versions**: some tapes ship two variants; the wrong one
  loads garbage. Prefer explicit `.tap` files per machine type.
- **LOAD "" on a 128K in the menu**: `basic/run` navigates the menu; raw
  typing must press `B` first.
