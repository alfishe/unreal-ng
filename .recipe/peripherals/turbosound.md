# Recipe: TurboSound Slot (2x AY or TSFM)

Every clone config carries a **TurboSound slot** — one configuration
choice, two very different devices:

| `TurboSound=` | Device | What it is |
|:--|:--|:--|
| `AY` | classic TurboSound | two AY-3-8910/YS1284912 chips behind one register pair |
| `FM` (current default) | **TSFM** — TurboSound FM | YM2203 (FM + SSG) based modern card |

Config comment of record: *"Change needs a new emulator instance - no
runtime switching."* `TSFM_FmTrimDb=7.4` calibrates FM loudness to the
real TSFM board (one FM carrier at TL 0 ≈ one SSG channel at volume 15).

Ground truth: config `[SOUND]` block in any
[data/configs/*/unreal.ini](../../data/configs/pentagon128k/unreal.ini),
chips [soundchip_turbosound.h](../../core/src/emulator/sound/chips/soundchip_turbosound.h)
(dual AY) and [soundchip_turbosoundfm.h](../../core/src/emulator/sound/chips/soundchip_turbosoundfm.h)
(YM2203 engine, `tsfm/`), AY register decode in
[state_audio_api.cpp](../../core/automation/webapi/src/api/state_audio_api.cpp).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `inspect_state` has dedicated aspects for both slot devices. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON"}   # TurboSound=FM default

inspect_state {"aspects":["audio_ay"]}     # the AY pair (each chip's registers)
inspect_state {"aspects":["audio_fm"]}     # the TSFM / YM2203 side

# Functional proof — run music, then:
capture_media {"action":"audio_capture","seconds":2}
#   → dominant_hz should land on the note you expect; silence means the
#     wrong slot device or a mute config
```

## WebAPI

```bash
# AY pair overview (chip list), one chip, one decoded register
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay/0" | jq '.registers'
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay/register/0" | jq .
#   register decode includes frequency_hz computed at the 1.75 MHz AY clock
#   (tone: 1750000/(16*(tp+1)), noise: 1750000/(16*(np+1)),
#    envelope: 1750000/(256*(ep+1)))

# TSFM overview + per-chip
curl -s "$BASE/emulator/$EMU_ID/state/audio/fm" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/audio/fm/0" | jq .

# Full mixdown view + beeper (the always-present channels)
curl -s "$BASE/emulator/$EMU_ID/state/audio/channels" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/audio/beeper" | jq .
```

## Reading the slot

- **Classic TurboSound (`AY`)**: one register pair (`#FFFD` address /
  `#BFFD` data) serves both chips; the TurboSound select line switches
  which chip answers. Software initializes each chip in turn — a port
  trace on `#FFFD/#BFFD` shows the double init pattern.
- **TSFM (`FM`)**: the YM2203 contributes FM voices plus its own SSG
  channels; `/state/audio/fm` reports the FM side. The ymfm-derived
  engine renders at any core rate (44.1–192 kHz bit-identical filter
  design — see the TSFM filter work).
- The AY pair is **also** the machine's base sound chip: with
  `TurboSound=FM` the classic second AY is gone — software probing for a
  second AY will not find one.

## Pitfalls

- **No runtime switching** — changing `TurboSound=` means editing the
  model config and creating a new instance. Tests that need both variants
  need two instances.
- **Default is `FM`** — demos from the classic-TurboSound era may sound
  thin or half-instrumented on a fresh clone instance because only one AY
  answers; flip the config to `AY` for period-correct playback.
- **Assert on decoded fields, not raw bytes** — the register endpoints
  already compute `frequency_hz`, envelope shape flags and mixer
  direction; recomputing them client-side just duplicates the decode math.
- **TSFM loudness is calibrated, not neutral** — `TSFM_FmTrimDb` shifts FM
  relative to SSG by design; a level mismatch between FM and AY channels
  is expected calibration, not a mixer bug.
