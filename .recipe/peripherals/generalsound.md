# Recipe: General Sound (GS) Card

The General Sound expansion — a sample/DAC card with its own Z80
coprocessor. Two emulation modes, selected by the `[SOUND] GSType=` config
key (new instance required, no runtime switch):

| Mode | Where | What |
|:--|:--|:--|
| `BASS` | `master` (default) | legacy HLE mode |
| `Z80` | `generalsound` branch (default on GS-capable clones) | full LLE: Z80ex copro @ 12 MHz, ROM + RAM, DACs, interrupt |
| `NONE` | Sinclairs (branch) | card absent |

Ground truth: `core/src/emulator/sound/chips/soundchip_gs.h`
(generalsound branch — file not on master), design doc
[docs/inprogress/2026-09-19-general-sound/gs-tdd.md](../../docs/inprogress/2026-09-19-general-sound/gs-tdd.md),
firmware `data/rom/gs105a.rom` (branch default; `gs104.rom` also ships).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `inspect_state` reads the card, `capture_media audio_capture` proves it
> sounds. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines
> or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## The card in one paragraph

Host-side ports (low byte decoded; model decoders cover aliases): `#B3`
GSDAT data, `#BB` GSCOM command/status, `#33` GSCTR control — bit 7 reset,
bit 6 NMI. On-card: Z80 @ 12 MHz, 32 KB ROM (2 x 16 KB pages), 128 KB RAM
stock (256/512 KB were expansions; the classic card is pinned to 128 KB),
4 x 8-bit DAC channels with 6-bit volume, 37.5 kHz periodic interrupt
(every 320 GS cycles). GS-side ports `0x00-0x0B` carry MPAG banking, the
host mailbox and volume latches. Audio mixes channels 1,2 -> L and 3,4 -> R
with 50% cross-feed. The coprocessor lazy-syncs to the ZX clock on every
host port access and at frame boundaries; its 12 MHz clock is fixed — GS
audio does not speed up with host turbo.

## MCP (preferred)

```text
# On a generalsound-branch build (clones: PENTAGON/SCORPION/ATM710/ATM3/PROFSCORP):
emulator_manage {"action":"create","model":"PENTAGON"}

inspect_state {"aspects":["audio_gs"]}
#   master  → stub (endpoint reserved)
#   branch  → mailbox flags, MPAG page, per-channel DAC sample/volume,
#             coprocessor core — 404 when the card is absent (GSType=NONE)

# Prove the card makes sound (works on both transports, both modes):
capture_media {"action":"audio_capture","seconds":2}
#   → analysis: sample_rate, duration_seconds, left/right {peak, rms},
#     dominant_hz (zero-crossing estimate), zero_crossing_rate;
#     "wav":true also exports the WAV (wav_path)

capture_media {"action":"audio_status"}   # {complete: ...} while running
```

For protocol-level debugging the branch adds a GS **port trace** (host-side
`#B3/#BB/#33` traffic with GS-side context: HOST / GS / DAC / INT sides) —
start it via `invoke_api` against the GS port-trace endpoint on the branch
build, then read the ring buffer like any trace
([port-trace.md](../analysis/port-trace.md)).

### Reset semantics (branch)

Two reset modes, both reachable via the GS state endpoint: `reset` — full
power-on reset (mailbox, volumes **and** timing); `reset_card` — the
`#33` bit-7 hardware pulse (CPU/banking/timing only; the mailbox survives).
Pick deliberately when re-running a firmware boot test.

## WebAPI

```bash
# Card state (branch build)
curl -s "$BASE/emulator/$EMU_ID/state/audio/gs" | jq .

# One-shot audio proof with offline analysis
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" \
     -H 'Content-Type: application/json' -d '{"action": "start", "seconds": 2}' | jq .
# ... let the machine run ~100 frames ...
curl -s "$BASE/emulator/$EMU_ID/audio/capture/status" | jq '.complete'
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result?wav=true" \
     | jq '{sample_rate, duration_seconds, dominant_hz, left, right, wav_path}'
```

## Pitfalls

- **`/state/audio/gs` on `master` returns `{"status":"not_implemented"}`** —
  the card is emulated (BASS HLE) but has no introspection there. Don't
  write assertions against GS state until `server.git_branch` says
  `generalsound`.
- **`GSType` is config-file only** — there is no API to switch modes;
  edit `configs/<model>/unreal.ini` and create a new instance.
- **GS silence on a real Sinclair model is policy**, not a bug — the branch
  sets `GSType=NONE` there. Use a clone.
- **Host turbo changes the mailbox race, not the card's clock** — if a
  firmware boot fails only under turbo, suspect ZX-side polling timing
  (`#BB` status), not GS-side synthesis.
- **The firmware ROM matters** — the branch moved to `gs105a.rom`;
  behavior differences between `gs104.rom` and `gs105a.rom` are real
  firmware differences. Check the `GS=` path in the model config.
