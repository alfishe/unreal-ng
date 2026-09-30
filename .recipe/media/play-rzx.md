# Recipe: Play an RZX Recording

Goal: play an RZX input recording (a game completion from the RZX Archive, a
recorded session from Fuse / Spectaculator / SPIN), follow its progress, and
tell a clean playback from a desync.

An RZX file is a start snapshot plus every `IN` value the CPU read and the
number of opcode fetches between interrupts. Playing it re-runs the program
along exactly the recorded path: the keyboard, joystick, tape and disk are not
consulted, the recording answers every port read. At the end the machine runs
live from where the recording stopped. Reference: [command-interface.md §12](../../docs/emulator/design/control-interfaces/command-interface.md#12-rzx-playback).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) inside host-side Python/bash pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
rzx_playback  {"action":"play","path":"scratch/greenberet.rzx"}   # 128K recording on a 48K: the model switches
                                                                  # → answer.emulator_id is the new target
control_execution {"action":"run_frames","frames":500,"target":"<emulator_id>"}
rzx_playback  {"action":"status","target":"<emulator_id>"}        # "playing frame 500 / 39041 (1.3%), ... 0 desyncs"
rzx_playback  {"action":"seek","frame":300}                       # back via keyframes (≤ 250 frames of replay)
rzx_playback  {"action":"stop"}                                   # the machine continues live
```

`load_software {"path":"game.rzx"}` does the same as `play`.

## WebAPI

```bash
# Play (path, upload with -F "file=@game.rzx", or the raw file with X-Filename)
curl -s -X POST "$BASE/emulator/$EMU_ID/rzx/play" -H 'Content-Type: application/json' \
     -d '{"path": "/abs/scratch/greenberet.rzx"}' | jq '{status, emulator_id, model_switched, model}'
# → {"status":"success","emulator_id":"5fe4...","model_switched":true,"model":"128k"}
EMU_ID=$(...)   # continue with the emulator_id from the answer

# Progress
curl -s "$BASE/emulator/$EMU_ID/rzx/status" | jq '{state, frame, total_frames, desyncs, drift}'

# Seek back or forward (frames; 50 per second of play)
curl -s -X POST "$BASE/emulator/$EMU_ID/rzx/seek" -H 'Content-Type: application/json' -d '{"frame": 3000}' | jq .summary

# Stop early
curl -s -X POST "$BASE/emulator/$EMU_ID/rzx/stop" | jq .message
```

Turbo mode runs the playback faster; it never changes the path the program takes.

## Reading the status

| `state` | Meaning |
|:--|:--|
| `playing` | frames still to go |
| `finished` | every frame played; the machine now runs live |
| `desynced` | the program left the recorded path (strict mode stopped there); see `first_desync` |
| `stopped` | `rzx/stop`, a reset, another snapshot, or a snapshot block in the middle of the file (not supported yet) |

A desync names its kind: `too_many_ins` (the program read a port more often
than recorded), `too_few_ins` (less often), `fetch_overrun` (it ran past the
recorded instruction count). Things to try, one at a time:

- `"desync_mode": "tolerant"` - counts desyncs and keeps going (to see how far it gets).
- `"ei_short_frame_blocks_int": true` - for files that mark an interrupt blocked by `EI` with a 1-2 fetch frame.
- `"ld_air_parity_quirk": true` - for files recorded with the NMOS `LD A,I` / `LD A,R` flag quirk.
- `"ignore_later_snapshots": true` - plays past snapshot blocks after the first (some Fuse files need it).

`drift` (T-states) is how far the recorded interrupt falls from the machine's
own: a few dozen T-states for Spectaculator files on our timing; tens of
thousands for SPIN files. It moves the picture, never the program.

## Pitfalls

- **The target changes on a model switch.** Keep using the `emulator_id` from
  the play answer; the old id is gone.
- **Live input is refused while playing** (keyboard, mouse, typed commands):
  the recording owns every port read. Stop the playback to take over.
- **Fast tape / turbo tape / fast disk are off while playing** (they would
  change the program's path); they come back after the end.
- **Supported machines:** 48K, 128K, +2, +2A, +3, Pentagon 128 / 512 / 1024,
  Scorpion. TSConf and Sprinter are refused (their interrupt is not the ULA's).
