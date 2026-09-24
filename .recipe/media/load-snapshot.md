# Recipe: Load / Save Snapshots

Goal: restore a machine state from a `.sna`/`.z80` file, save states back out,
and verify a snapshot actually took effect.

Snapshots are the cheapest way to reach a known state — much faster than
booting through TR-DOS or tape. Use them as the entry point for
[TTD](../analysis/ttd-recording.md) capture and for regression testing.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `load_software`
> loads, `inspect_state` verifies, `invoke_api` saves. Use [WebAPI](#webapi)
> only inside host-side Python/bash pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
load_software  {"path":"scratch/game.sna"}                    # load (local file → uploaded automatically)
invoke_api    {"method":"GET","path":"/api/v1/emulator/{id}/snapshot/info"}
inspect_state {"aspects":["registers","screen_digest"]}     # PC landed where expected? same screen?
invoke_api    {"method":"POST","path":"/api/v1/emulator/{id}/snapshot/save",
               "body":{"path":"scratch/checkpoint-001.sna"}}
```

Snapshot save has no smart tool — it rides the `invoke_api` router. The
round-trip pattern below translates 1:1 (save → diverge → load → compare
`screen_digest` aspects).

## WebAPI

The curl walkthrough below — right choice when a Python/bash pipeline drives
these steps directly.

### Load a snapshot

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/load" \
     -H 'Content-Type: application/json' \
     -d '{"path": "/abs/scratch/game.sna"}' | jq .
# → {"status":"success", ...}
```

Multipart upload (`-F "file=@game.sna"`) and MCP both work:

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"load_software","arguments":{"path":"scratch/game.z80"}}}' | jq .
```

### Confirm the state took

```bash
# Which file is loaded
curl -s "$BASE/emulator/$EMU_ID/snapshot/info" | jq .
# → {"status":"loaded","file":"/abs/scratch/game.sna"}

# Registers (PC tells you where you landed)
curl -s "$BASE/emulator/$EMU_ID/registers" | jq '.registers | {pc, sp, af, bc}'

# Screen content fingerprint
curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq '.digest'
```

Snapshot format notes:

- `.sna` (48K) — on 128K machines the extension state (paging registers) is
  reconstructed; some snapshots carry 128K extensions.
- `.z80` — carries model + paging info natively; the emulator applies what it
  recognizes and starts the machine paused or running per snapshot flags.

### Save a snapshot

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" \
     -H 'Content-Type: application/json' \
     -d '{"path": "/abs/scratch/checkpoint-001.sna"}' | jq .
```

Save early, save often — they are the "known good" anchors for
[bug-hunt workflows](../articles/bug-hunt-ttd.md). Saved snapshots land on the
emulator host's filesystem; use `scratch/` paths.

### Snapshot round-trip pattern (regression anchor)

```bash
# 1. Reach the interesting state (boot, load, play to a point)
# 2. Anchor it:
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/anchor.sna"}' >/dev/null

# 3. Let the machine diverge (run frames, inject input, ...)

# 4. Return deterministically:
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/load" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/anchor.sna"}' >/dev/null
DIGEST_A=$(curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq -r .digest)
# ...run the same 300 frames...
DIGEST_B=$(curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq -r .digest)
[ "$DIGEST_A" = "$DIGEST_B" ] && echo "deterministic" || echo "DIVERGED"
```

## Interactions to know

- **TTD invalidate**: loading a snapshot while a TTD session holds history
  makes the timeline invalid for further capture — start a fresh
  `POST /ttd/start` after a snapshot load (details in
  [ttd-recording.md](../analysis/ttd-recording.md)).
- **Media state**: `.sna`/`.z80` do not carry disks/tapes — re-insert media
  after loading if the program expects it.
- **Model mismatch**: a 128K snapshot loaded into a 48K instance (or vice
  versa) either fails cleanly or drops extension state; create the right
  model first ([setup.md](../_common/setup.md) §3).

## Pitfalls

- **Emulator-side paths**: as with all media, the emulator process must be
  able to read/write the path. Absolute `scratch/` paths save headaches.
- **Snapshot mid-frame**: saving while free-running produces a valid state,
  but the exact frame boundary may vary between saves — for byte-exact
  comparisons, `POST /pause` (or `run_frames` to park) before saving.
- **Loaded ≠ running**: after `snapshot/load` check the emulator run state
  (`GET /api/v1/emulator/$EMU_ID` → `state`) and `resume` if the next step
  expects motion.
