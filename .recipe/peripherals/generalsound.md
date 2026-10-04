# Recipe: General Sound (GS) Card

The General Sound expansion: a sample/DAC card with its own Z80
coprocessor. The card kind is set by the `[SOUND] GSType=` config key (read
at instance creation) and can be swapped at runtime
([personality switch](#mcp-preferred)):

| `GSType` | What |
|:--|:--|
| `Z80` | full LLE classic card: Z80 coprocessor (unreal-z80) @ 12 MHz, ROM + RAM, DACs, interrupt |
| `LW` (alias `LIGHT`) | lightweight in-tree mod player driven by host commands (no coprocessor) |
| `BASS` | deprecated alias of `LW` (logs a warning; no BASS library is linked) |
| `NGS` | NeoGS FPGA card (`SoundChip_NeoGS`): SD slot, MP3 decoder, DMA |
| `NONE` | card absent; the default when the key is missing (an unknown value warns and falls back to `NONE`) |

The shipped configs under `data/configs/` all set `GSType=NGS`. Classic-card
firmware `[ROM] GS=` defaults to `rom/gs105a.rom` (`gs104.rom` also ships);
`bootGS.rom` and `rom/neogs/` hold the NeoGS flash image. Other keys:
`GSVol` (0-8192 ini scale, shipped 8000), `GSReset=1` makes a ZX reset
reinitialize the card too.

Ground truth: `core/src/emulator/sound/chips/gs/soundchip_gs.h` (LLE),
`soundchip_gslw.h` (lightweight), `generalsoundcard.h` (common card
interface), `gsmailbox.h` (host mailbox), NeoGS in
`core/src/emulator/sound/chips/neogs/soundchip_neogs.h`; design doc
[docs/inprogress/2026-09-19-general-sound/gs-tdd.md](../../docs/inprogress/2026-09-19-general-sound/gs-tdd.md).

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
emulator_manage {"action":"create","model":"PENTAGON"}

inspect_state {"aspects":["audio_gs"]}
#   mailbox flags, MPAG page, per-channel DAC sample/volume, coprocessor
#   core; a "neogs" object (windows, clock, SD, MP3, DMA) on NeoGS;
#   reports unavailable when no card is fitted. The card CPU's #4000-#7FFF
#   window: WebAPI GET .../state/audio/gs?ram=1

# Prove the card makes sound (any kind), master mix or the card alone:
capture_media {"action":"audio_capture","seconds":2}
capture_media {"action":"audio_capture","seconds":2,"source":"gs"}      # "gs_mp3": NeoGS MP3 decoder
#   -> analysis: sample_rate, duration_seconds, left/right {peak, rms},
#     dominant_hz (zero-crossing estimate), zero_crossing_rate;
#     "wav":true also exports the WAV (wav_path)
capture_media {"action":"audio_status"}   # {complete: ...} while running
```

Drive the card with `emulator_manage` (all `gs_*` actions go to
`POST /control/audio/gs`; 404 when no card is fitted):

| Action | Fields | Effect |
|:--|:--|:--|
| `gs_reset` | | full power-on reset (mailbox, volumes **and** timing) |
| `gs_reset_card` | | `#33` bit-7 pulse (CPU/banking/timing only; the mailbox survives) |
| `gs_nmi` | | `#33` bit-6 pulse |
| `gs_send_command` / `gs_send_data` | `value` 0-255 (required) | `OUT #BB` / `OUT #B3` semantics |
| `gs_read_status` / `gs_read_data` | | side-effect-free peek: `IN #BB` value (status or `#7E`) / the card-to-ZX byte (bit 7 not cleared) |
| `gs_switch_personality` | `personality`: `z80`/`lle`, `lw`/`lightweight`, `ngs`/`neogs` | swap the card at the next frame boundary; mailbox and counters survive, a module held by the lightweight card is replayed through fresh LLE firmware |
| `gs_dump_module` | `path` (optional, relative, no `..`; default `gs-module-dump.mod`) | write the last completed COM30..D2 module upload; 404 if none |
| `gs_sd_insert` / `gs_sd_eject` / `gs_flash_save` | `path` (insert: raw image) | NeoGS only; insert/eject refused while TTD records |
| `gs_stereo_mode` | `mode`: `separated` (as on the board), `gs` (50% cross-feed), `mono` | NeoGS DAC mix, applied next frame |

The writes, resets and NMI are live input: applied at the next instruction
boundary and journaled for TTD (409 while a TTD replay owns input). Pick
`gs_reset` vs `gs_reset_card` deliberately when re-running a firmware boot
test.

Protocol triage: `analyze_performance {"action":"gs_porttrace","frames":10,"limit":32}`
starts a GS trace, runs the frames, stops it and returns the always-on
activity counters (CPU steps, interrupts, DAC pushes) plus the last `limit`
events (sides: host / gs / dac / interrupt / zxdma); no feature flag needed.
Per-device level: `inspect_state {"aspects":["audio_mixer"]}`, WebAPI
`GET /audio/mixer`.

## WebAPI

```bash
# Card state (add ?ram=1 for the card CPU's #4000-#7FFF window)
curl -s "$BASE/emulator/$EMU_ID/state/audio/gs" | jq .

# Control: reset / reset_card / nmi / send_command / send_data / read_* ...
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"reset_card"}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"send_command","value":243}' | jq .
#   -> {"status":"success","action":"send_command","note":"applied at the next instruction boundary"}
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"switch_personality","personality":"lw"}' | jq .
#   -> {"personality":"lw","current":"...","requested":true,"note":"applied at the next frame boundary"}
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs" \
     -H 'Content-Type: application/json' -d '{"action":"stereo_mode","mode":"gs"}' | jq .

# Port trace: counters + last N events; control start|stop|pause|resume|clear
curl -s "$BASE/emulator/$EMU_ID/state/audio/gs/porttrace?events=32" | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/control/audio/gs/porttrace" \
     -H 'Content-Type: application/json' -d '{"action":"start"}' | jq .

# One-shot audio proof with offline analysis ("source":"gs" for the card alone)
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" \
     -H 'Content-Type: application/json' -d '{"action": "start", "seconds": 2}' | jq .
# ... let the machine run ~100 frames ...
curl -s "$BASE/emulator/$EMU_ID/audio/capture/status" | jq '.complete'
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result?wav=true" \
     | jq '{sample_rate, duration_seconds, dominant_hz, left, right, wav_path}'
```

## Pitfalls

- **`reset` / `reset_card` are control actions**, not state: they go through
  `POST /control/audio/gs` (the state endpoint is read-only).
- **`GSType` is read at creation; the runtime swap is `switch_personality`** —
  it is refused (409) while a TTD recording runs, and takes effect at the
  next frame boundary, so poll `GET /state/audio/gs` before asserting.
- **GS silence on a real Sinclair model is policy, not a bug** — use a
  clone, or `GSType=NONE` models report no card (404 / unavailable).
- **Host turbo changes the mailbox race, not the card's clock** — if a
  firmware boot fails only under turbo, suspect ZX-side polling timing
  (`#BB` status), not GS-side synthesis.
- **The firmware ROM matters** — behavior differences between `gs104.rom`
  and `gs105a.rom` are real firmware differences. Check the `GS=` path in
  the model config.
- **`dump_module` paths are sandboxed** — relative, no `..`; absolute paths
  get 400.
