# Recipe: Load / Save Snapshots

Goal: restore a machine state from a `.sna`/`.z80`/`.szx` file (or run a TS-Conf `.spg` program), save states back out,
and verify a snapshot actually took effect.

An `.rzx` input recording loads the same way and then plays: see
[play-rzx.md](play-rzx.md).

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
               "body":{"path":"scratch/checkpoint-001.sna","force":true}}   # force: overwrite an existing file (else 409)
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

Multipart upload (`-F "file=@game.sna"`) and MCP both work. Upload by bytes
accepts only `.sna .z80 .szx .sp .snp .rzx` (others, `.spg` included, are
rejected with "Unrecognized file extension"): for an `.spg` send a JSON `path` the emulator can read (`snapshot/load`
with `{"path":...}`). `load_software` uploads a file it can read locally, so a local `.spg`
is rejected that way: use `invoke_api` `POST /snapshot/load` with the path instead. An MCP example:

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
- `.szx` (ZX-State, the format Fuse and Spectaculator write) — the machine id,
  the exact CPU state (MEMPTR, Q, the EI shadow, HALT) and the position in the
  frame, AY registers, the Beta 128 registers and TR-DOS paging. Through the API the
  snapshot's model must be the running one (48K, 128K, +2, +2A, +3, Pentagon
  128 / 512 / 1024, Scorpion); another model is refused with both names, e.g.
  "the snapshot was saved on a Pentagon 512K, the running machine is a
  ZX-Spectrum 128k: create a Pentagon 512K to load it". In the Qt window
  (drag and drop, File > Open, a file on the command line) an SZX for another
  model replaces the running machine by that model first, as the Machine menu
  does (media follow), then loads. The log
  lists what each block did (applied, approximated, ignored). Saving picks the
  format by the extension; ATM, ZX-Evo, Profi and TSConf have no SZX machine id
  and cannot be saved as `.szx` yet.
- `.spg` (TS-Conf "Spectrum Prog", the TS-Conf SDK's program format, v1.0 and
  v1.1) — runs on the TS-Conf machine only (`TSL`). `snapshot/load` takes
  `switch_model` (default `true`): on another model the emulator switches to
  `TSL` first and the reply carries a NEW `emulator_id` (plus `model_switched`,
  `previous_emulator_id`, `model`); use that id from then on. With
  `"switch_model": false` (or `?switch_model=false`) the load is refused with
  HTTP 409 and `required_model`. `load_software` of an `.spg` behaves the same
  (the answer's `emulator_id` is the new one). Blocks may be MegaLZ or
  Hrust packed. The machine is reset, BASIC-48 ROM at `#0000`, RAM 5 / 2 / the
  header's page, the header's PC, SP, CPU clock and INT enable; a corrupt file
  changes nothing. Load only - there is no SPG writer.

### Save a snapshot

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" \
     -H 'Content-Type: application/json' \
     -d '{"path": "/abs/scratch/checkpoint-001.sna"}' | jq .
```

Save refuses to overwrite: if the file exists the answer is HTTP 409 Conflict
("File already exists. Use 'force: true' to overwrite."). Add `"force": true` to
replace it, or use a new file name per save (`checkpoint-002.sna`).

Save early, save often — they are the "known good" anchors for
[bug-hunt workflows](../articles/bug-hunt-ttd.md). Saved snapshots land on the
emulator host's filesystem; use `scratch/` paths.

### Snapshot round-trip pattern (regression anchor)

```bash
# 1. Reach the interesting state (boot, load, play to a point)
# 2. Anchor it (force: a re-run of this recipe finds anchor.sna already there → 409 without it):
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/anchor.sna","force":true}' >/dev/null

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

## Move a running state to another instance (no file)

Same state on another machine, e.g. to compare a demo on a 128K and a Pentagon
side by side. The source keeps running; the report says per item what moved.

```text
emulator_manage {"action":"transfer_state","target":"<source-id>","to":"<target-id>","check":true}   # can it?
emulator_manage {"action":"transfer_state","target":"<source-id>","to":"<target-id>"}                # do it
emulator_manage {"action":"transfer_state","target":"<source-id>","model":"PENTAGON"}               # new instance
```

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/transfer" \
     -H 'Content-Type: application/json' \
     -d '{"model":"PENTAGON"}' | jq -r .summary
# transfer: ok (cross-model)
#   [copied] RAM: pages 0-7
#   [copied] paging: 128K state replayed through the Pentagon port decoder
#   [copied] TSFM: 2008 bytes of state
#   [copied] NeoGS RAM and flash: 2048 KB RAM + 512 KB flash
#   [copied] fdd.a: game.trd -> game.pentagon-1a2b3c4d.trd (in-memory copy, clean; written only by an explicit save)
#   [note] SD / HDD / CD: not moved (by design, for now): the target keeps its own ...
```

HTTP 422 = the target cannot hold the state (e.g. a 128K program into a 48K);
`reason` says why and nothing changed. Details:
[automation.md → Machine State Transfer](../../docs/features/automation.md#machine-state-transfer).

## Interactions to know

- **TTD invalidate**: loading a snapshot while a TTD session holds history
  makes the timeline invalid for further capture — start a fresh
  `POST /ttd/start` after a snapshot load (details in
  [ttd-recording.md](../analysis/ttd-recording.md)).
- **Media state**: `.sna`/`.z80` do not carry disks/tapes — re-insert media
  after loading if the program expects it. `.szx` does: saving links the
  file-backed disks (Beta 128 TRD / SCL / FDI / UDI, +3 DSK) and the tape with
  its current block; loading finds a linked image next to the snapshot first,
  then at the stored path, and inserts it with Session access (the linked
  file is never written). Images embedded in an `.szx` from another emulator
  are loaded too. The classic GS card (a `gs` slot card), the Covox level and the
  Kempston mouse type travel as well.
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
