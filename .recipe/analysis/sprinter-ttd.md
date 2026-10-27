# Recipe: Time travel on the Sprinter Sp2000

TTD records the Sprinter like any other machine since phase S7 (before, `ttd/start` was refused:
"declares TTD state id 25 but its port decoder supplied no serializer"). Everything in the general
recipe [ttd-recording.md](ttd-recording.md) applies: start / stop, seek, step, dump / load, bookmarks.
This page lists what is specific to the Sprinter.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred; [WebAPI](#webapi) and [CLI](#cli)
> do the same over HTTP / the text console (policy: [_common/transports.md](../_common/transports.md)).
> Machine basics (BIOS, full / fast start, DSS floppy): [machines/sprinter.md](../machines/sprinter.md).

Outputs below are real, from a build of branch `sprinter-ttd` (2026-10-02), trimmed.

## What a Sprinter checkpoint holds

Besides the CPU registers, the 128K chipset struct, the 256 RAM pages (4 MB, the port table page `#40`
and the graphics pages included) and the shared devices (WD1793, IDE, AY, Kempston mouse and joystick,
CMOS), a checkpoint carries the Sprinter blobs:

| Id | Blob | Content |
|---|---|---|
| 25 | `sprinter-pld` | PLD cells and registers, configuration load (bitstream count, hashes, watchdog), active module by name and its state, INT source, frame height, block accelerator |
| 28 | `sprinter-vram` | the 256 KB video RAM (whole, until TTD memory regions) |
| 29 | `z84c15` | the Z84C15 beside its registers: WCR / MWBR / CSBR / MCR, the power-on wait window, watchdog, CTC, SIO with its receive FIFOs, PIO, daisy chain |
| 30 | `sprinter-fast-ram` | the 64 KB fast RAM (whole) |
| 31 | `sprinter-input` | the AT keyboard's bytes on the wire (typematic, held keys) and the serial mouse's packet in flight |
| 35 | `wd1793-context` | the floppy command in flight (queued steps, sector and offset), so a restore inside a sector continues it |

Input: keys travel as journaled PC key events (`PcKey`), the mouse as the journaled Kempston counters
(the serial mouse samples them). A seek replays them; a restore in the middle of a PS/2 byte or of a
mouse packet resumes on the same bit. Port journals stay off on the Sprinter ("the machine's interrupt
source supplies the IM2 vector"; with a NeoGS added behind the ISA ZX-bus adapter also "NeoGS: its ZX-DMA ..."): replay runs against
the live devices, so keep the same floppy / HDD images inserted.

Recording turns the host turbo mode off (as on every machine): 21 MHz Sprinter frames run at their
real cost, ~10 ms of host time per frame with the full renderer.

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"SPRINTER"}
control_execution {"action":"run_frames","frames":40}         # inside the PLD load (full start)
time_travel {"action":"start"}
control_execution {"action":"run_frames","frames":120}
time_travel {"action":"stop"}
time_travel {"action":"seek","frame":80}
#   → structuredContent: {"arrived_at":{"frame":80,"tinframe":0},"halt_reason":"target","reached":true,"state":"detached"}
inspect_state {"aspects":["sprinter","ttd"]}                  # PLD state at that frame, TTD position
time_travel {"action":"dump","path":"/abs/path/scratch/sprinter.ttd"}
```

## WebAPI

```bash
B=http://localhost:8090/api/v1
ID=$(curl -s -X POST $B/emulator/start -H 'Content-Type: application/json' -d '{"model":"SPRINTER"}' | jq -r .id)
E=$B/emulator/$ID
curl -s -X POST $E/pause
curl -s -X POST $E/run_frames -H 'Content-Type: application/json' -d '{"count":40}'
curl -s -X POST $E/ttd/start -H 'Content-Type: application/json' -d '{}'
#   → {"already_active":false,"history_limit_bytes":0,"history_limit_frames":0,"started":true,"state":"recording","write_journal_enabled":false}
curl -s -X POST $E/run_frames -H 'Content-Type: application/json' -d '{"count":120}'
curl -s -X POST $E/ttd/stop
curl -s -X POST $E/ttd/seek -H 'Content-Type: application/json' -d '{"frame":60}'
#   → {"arrived_at":{"frame":60,"tinframe":430080},"halt_reason":"target","reached":true,"state":"detached"}
#     (a frame alone is its end: the machine stands at frame 61, T-state 0)
curl -s $E/state/sprinter | jq .pld.state            # → "loading": frame 60 is inside the PLD load
curl -s -X POST $E/ttd/dump -H 'Content-Type: application/json' -d "{\"path\":\"$PWD/scratch/sprinter.ttd\"}"
#   → {"bytes":1104056,"ok":true,...}                 # 121 checkpoints
```

## CLI

```text
> ttd status
  ...
  Port journals:          off - NeoGS: its ZX-DMA serves host memory reads without IN (not isolated by the first version)
> ttd seek 100
TTD: Seek reached target (frame=100, tInFrame=430080)
> ttd position
  Current: (frame=101, tInFrame=18)
  End:     (frame=123, tInFrame=0)
```

## Inspecting a dump

```bash
tools/verification/ttd-analyzer/run.sh validate scratch/sprinter.ttd
#   → scratch/sprinter.ttd: OK (121 checkpoints, 1300 pages)
```

The corpus fixture `testdata/machines/sprinter/ttd/boot.ttd` (the cold full start: PLD load, BIOS
POST of the shipped default, 3.06 Hotfix 2 since 2026-10-03) is re-recorded with `record_fixtures.py --only sprinter_boot` ([testdata/ttd/README.md](../../testdata/ttd/README.md)).

Ground truth: [debugger/ttd/sprinter/ttdsprinter.h](../../core/src/debugger/ttd/sprinter/ttdsprinter.h),
[s7-ttd-outcome.md](../../docs/inprogress/2026-09-28-sprinter/s7-ttd-outcome.md).
