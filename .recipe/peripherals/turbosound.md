# Recipe: TurboSound Slot (2x AY or TSFM)

Every machine has an **AY socket** slot (`ay-socket` in `[SLOTS]`, the
[slots recipe](../machines/slots.md)) — one choice, very different devices:

| `ay-socket =` | Legacy `TurboSound=` | Device | What it is |
|:--|:--|:--|:--|
| `ay` | `Single` / `None` on a board with an AY | the machine's own AY | one AY-3-8910 / YM2149 |
| `ts` | `AY` | classic TurboSound | two AY-3-8910/YS1284912 chips behind one register pair |
| `tsfm` (shipped default) | `FM` | **TSFM** — TurboSound FM | YM2203 (FM + SSG) based modern card |
| `none` | | empty socket | no chip: the AY ports float |

A change is a slot change that **restarts the machine** (a new instance,
the media kept): `emulator_manage {"action":"slots_plug","slot":"ay-socket",
"card":"ts","replace_if_incompatible":true}`, CLI `slots plug ay-socket ts
--replace`. The legacy `[SOUND] TurboSound=` key still works in an INI
without `[SLOTS]`. `TSFM_FmTrimDb=7.4` calibrates FM loudness to the
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
emulator_manage {"action":"create","model":"PENTAGON"}   # ay-socket = tsfm default
emulator_manage {"action":"create","model":"PENTAGON","slots":{"ay-socket":"ts"}}   # classic TurboSound instead

inspect_state {"aspects":["audio_ay"]}     # the AY pair (each chip's registers)
inspect_state {"aspects":["audio_fm"]}     # the TSFM / YM2203 side

# Functional proof — run music, then:
capture_media {"action":"audio_capture","seconds":2}
#   → dominant_hz should land on the note you expect; silence means the
#     wrong slot device or a mute config

# Spectrum / level analysis of the AY itself: switch the tone voicing to
# flat first (the default classic trims the very low bass and softens the highs)
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/settings/ay_voicing","body":{"value":"flat"}}
```

Sound character settings (applied at the next frame, same values on every
surface - WebAPI `settings/<name>`, CLI `setting <name>`, Lua / Python
`set_sound_character(name, value)`; MCP via `invoke_api`):

| Setting | Values (default first) | Notes |
|---|---|---|
| `ay_voicing` | `classic` (default, alias `legacy`) \| `headphones` \| `flat` \| `warm` \| `tv` \| `small_speaker` | Tonal balance of the AY / SSG output, HQ and LQ. `flat` for analysis; also `[SOUND] AYVoicing=` in `unreal.ini` |
| `ay_punch` | `on` \| `off` | AY transient enhancement, Sound HQ only |
| `ay_room` | `9db` \| `off` \| `15db` ... `1db` | Headphone crossfeed level, Sound HQ only |
| `beeper_punch` | `off` \| `on` | Beeper attack enhancement, Sound HQ only |

What each profile sounds like, its curve and when to pick it:
[ay-tone-voicing](../../docs/emulator/design/audio/ay-tone-voicing.md).

## WebAPI

```bash
# AY pair overview (chip list), one chip, one decoded register (/ay/{chip}/register/{reg})
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay/0" | jq '.registers'
curl -s "$BASE/emulator/$EMU_ID/state/audio/ay/0/register/0" | jq .
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
  `ay-socket = tsfm` the classic second AY is gone — software probing for a
  second AY will not find one.

## Pitfalls

- **A change restarts the machine** — `slots_plug` on `ay-socket` creates the
  machine again with the other board (a new emulator id; the machine state is
  lost). Tests that need both variants create two instances (`create` with
  `"slots": {"ay-socket": "ts"}`).
- **Default is `tsfm`** — demos from the classic-TurboSound era may sound
  thin or half-instrumented on a fresh clone instance because only one AY
  answers; plug `ts` for period-correct playback.
- **Assert on decoded fields, not raw bytes** — the register endpoints
  already compute `frequency_hz`, envelope shape flags and mixer
  direction; recomputing them client-side just duplicates the decode math.
- **TSFM loudness is calibrated, not neutral** — `TSFM_FmTrimDb` shifts FM
  relative to SSG by design; a level mismatch between FM and AY channels
  is expected calibration, not a mixer bug.
