# Recipe: Insert / Play / Inspect a Tape

Goal: mount a `.tap`/`.tzx` image, understand its block layout, drive
playback, and know when loading is done.

Related: [tape-fastload.md](../run/tape-fastload.md) (actually loading a
program from tape).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `load_software`
> covers loading (and playback), `invoke_api` the transport/inspection
> endpoints. Use [WebAPI](#webapi) only inside host-side Python/bash
> pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
load_software {"path":"scratch/game.tap","play":true}   # load + start playback immediately
invoke_api   {"method":"GET","path":"/api/v1/emulator/{id}/tape"}          # block catalog + fast_load plan
invoke_api   {"method":"POST","path":"/api/v1/emulator/{id}/tape/play"}
invoke_api   {"method":"POST","path":"/api/v1/emulator/{id}/tape/seek","body":{"block":3}}
invoke_api   {"method":"POST","path":"/api/v1/emulator/{id}/tape/eject"}
invoke_api   {"method":"POST","path":"/api/v1/emulator/{id}/tape/import",
              "body":{"sourcePath":"/abs/scratch/side-a.wav",
                       "outputPath":"/abs/scratch/side-a.tzx","insert":true}}
```

Tape endpoints have no dedicated smart tool — everything after `load_software`
rides the `invoke_api` router; responses are the same JSON the WebAPI returns.

## WebAPI

The curl walkthrough below — right choice when a Python/bash pipeline drives
these steps directly.

### Load the tape

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/load" \
     -H 'Content-Type: application/json' \
     -d '{"path": "/abs/scratch/game.tap"}' | jq .
# → {"status":"success"}
```

Multipart upload (`-F "file=@game.tap"`) works too, as does MCP:

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"load_software","arguments":{"path":"scratch/game.tap"}}}' | jq .
```

`load_software` accepts `play: true` to start playback immediately.

### Inspect the tape (the block catalog)

```bash
curl -s "$BASE/emulator/$EMU_ID/tape" | jq '{status, state, block_count, format, position}'
```

Snapshot fields:

| Field | Meaning |
|:--|:--|
| `status` | `loaded` / `empty` / `error` |
| `state` | `idle` / `playing` / `ended` |
| `block_count` | recognized blocks in the image |
| `format` | `tap` / `tzx` |
| `position` | playback position (`block`, plus offsets) |
| `blocks[]` | per-block: `kind` (header/data/control), `size`, `seconds`, `checksum_valid`, `fast_load` |
| `fast_load` | `"yes"` when the emulator will instant-load this block, or the reject reason |

Per-block detail: `GET .../tape/blocks/{index}`.

The `fast_load` plan is the important one: blocks marked `"yes"` never come
out of the speaker — the LOAD routine is intercepted and the block delivered
instantly. `play` can therefore race to `state: "ended"` almost immediately
on pure-fast-load tapes. That is success, not a bug.

### Transport controls

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/play"  | jq '.state'   # → "playing" (or "ended" if fast-loaded)
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/pause"  | jq '.state'
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/stop"   | jq '.state'
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/rewind" | jq '.position.block'   # → 0

# Position the head at an exact block (skip pilot/loaders you don't want)
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/seek" \
     -H 'Content-Type: application/json' \
     -d '{"block": 3}' | jq '{status, state, position, cursor}'
```

`seek` fails with `400` when the block index is out of range. Use it to skip
multi-load tapes straight to the block that matters.

### Eject

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/eject" | jq '.status'
```

### Importing tape audio (WAV → TAP/TZX)

Have a recording of a real tape? The emulator can decode it:

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/tape/import" \
     -H 'Content-Type: application/json' \
     -d '{"sourcePath": "/abs/scratch/side-a.wav",
          "outputPath": "/abs/scratch/side-a.tzx",
          "insert": true}' | jq '{decoder, blocks_recognized, warnings}'
```

- `outputPath` must end in `.tzx` or `.tap` (`.tap` refuses images the format
  cannot represent — read `tap_refusal_reason` and re-target `.tzx`).
- Optional `hysteresis` (0.05–0.45) tunes pulse discrimination.
- `insert: true` swaps the decoded image straight into the tape deck.

The reverse — rendering a tape image back to audio — is
`POST .../tape/render`.

### Detecting "loading finished"

Three signals, pick by need:

1. `GET .../tape` → `state == "ended"` and
   `position.block == block_count - 1` (tape consumed);
2. Screen: `GET .../capture/ocr` shows the program's first screen instead of
   the loading stripes (see [setup.md](../_common/setup.md) §7);
3. Deterministic: `GET .../state/screen/digest` stabilizes across two polls
   an interval apart.

## Pitfalls

- **`play` while the machine is in a menu** does nothing useful — the tape
  plays but nobody listens. Enter `LOAD ""` first
  ([tape-fastload.md](../run/tape-fastload.md)).
- **Block seeking is only legal while the image is loaded**; on `error`
  status check the path was readable by the emulator process.
- **TZX with exotic blocks** (custom loaders, tape-stop symbols): those blocks
  may be `playable: false` and excluded from fast load — real-time audio
  timing then matters, give the emulator wall-clock time.
