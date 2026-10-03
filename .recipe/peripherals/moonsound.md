# Recipe: MoonSound (OPL4) Card

Yamaha OPL4 (YMF278B) — FM synthesis plus wavetable PCM driven by the
YRW801 wave ROM. The engine is on `master` (`core/src/3rdparty/opl4`,
`core/src/emulator/sound/chips/soundchip_moonsound.*`); a model with
`MoonSound=1` plays.

Ground truth: `core/src/emulator/sound/chips/soundchip_moonsound.h`, OPL4
core `core/src/3rdparty/opl4/`, design docs
[docs/inprogress/2026-09-13-moonsound/](../../docs/inprogress/2026-09-13-moonsound/)
(core TDD, TTD integration, emulator integration).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> create a clone model, read the card with `inspect_state`, prove it with
> `capture_media audio_capture`. Use [WebAPI](#webapi) only inside
> host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Where the card exists

Config keys (`[SOUND]`): `MoonSound=` (enable), `MoonSoundVol=` (0-8192
ini scale, out of range is clamped; shipped 8000); `[ROM] MOONSOUND=` is
the wave-ROM path. Shipped configs under `data/configs/`:

| Model | `MoonSound=` |
|:--|:--|
| `ATM3`, `ATM710`, `ATM450`, `pentagon128k`, `pentagon512k`, `profscorp`, `scorpion` (also `ts-conf`, `zx-diagnostics`) | `1` |
| `profi`, `profi3`, `sprinter` | `0` — the card's low-byte `#7E` claim would swallow palette writes on `#xx7E` |
| `spectrum48`, `spectrum128`, `spectrum2`, `spectrum2a`, `spectrum3` | `0` — real Sinclair models never had this card |

Wave ROM: `rom/opl4/yrw801-m-yamaha-1993.rom` (file
`data/rom/opl4/yrw801-m-yamaha-1993.rom`). Optional `[MOONSOUND]` section:
`WaveRom` (overrides `[ROM] MOONSOUND=`), `RamSizeKb` (0-1024, default
1024), `RenderMode`, `Quality`, `Punch`.

## Port map (what OPL4 software writes)

| Port | Role |
|:--|:--|
| `#C4` w | FM register address, bank 1 (read: FM status / BUSY) |
| `#C5` w | FM register data |
| `#C6` w | FM register address, bank 2 |
| `#C7` w | FM register data (aliases `#C5`) |
| `#7E` w | wave register address latch (NEW2-gated) |
| `#7F` rw | wave register data (NEW2-gated) |

The FM banks cover the two YMF278B register planes; the wave registers
(the PCM/wavetable side, incl. the YRW801 sample selection) sit behind the
`#7E/#7F` pair and only respond while the NEW2 gate is open.

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON"}   # MoonSound=1 clones

# Card state: NEW/NEW2, address latches, #F8/#F9 mix, wave memory,
# keyed FM channels and PCM slots
inspect_state {"aspects":["audio_moonsound"]}
inspect_state {"aspects":["audio_opl4_fm"]}    # 18 FM channels: F-number, block, Hz, key-on, route, timers
inspect_state {"aspects":["audio_opl4_pcm"]}   # 24 wavetable slots: wave, rate, key-on, level, pan, envelope

# Load an OPL4-capable demo/music (disk or tape recipes), then prove it
# sounds: the master mix, or the card's own halves
capture_media {"action":"audio_capture","seconds":3}
capture_media {"action":"audio_capture","seconds":3,"source":"moonsound_fm"}    # or "moonsound_pcm"
#   -> left/right {peak, rms}, dominant_hz, zero_crossing_rate,
#     sample_rate, duration_seconds; "wav":true exports wav_path

# While iterating on a driver, watch the FM/wave traffic instead:
#   port_trace with an address filter on the #C4-#C7 / #7E-#7F range
#   (see analysis/port-trace.md — filters are port masks, so one
#   mask covering 0x00C4-0x00C7 needs the low-byte form)
```

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "ATM710"}' | jq '{id, model}'

# Card state: overview, FM half, PCM half ({part} = fm | pcm, else 400)
curl -s "$BASE/emulator/$EMU_ID/state/audio/moonsound" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/audio/moonsound/fm" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/audio/moonsound/pcm" | jq .

# Level of the card's halves in the mix: GET /audio/mixer lists the fitted devices
curl -s "$BASE/emulator/$EMU_ID/audio/mixer" | jq .

curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" \
     -H 'Content-Type: application/json' -d '{"action": "start", "seconds": 3, "source": "moonsound_pcm"}' | jq .
# ...run ~150 frames...
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result" \
     | jq '{dominant_hz, left: .left.peak, right: .right.peak}'
```

## Pitfalls

- **Silence on a model with `MoonSound=0`** — check the config first
  (Sinclair models, Profi, Sprinter ship `0`); creating the card needs a
  new instance after editing the config.
- **Clone-only by policy** — don't override the config to force the card on
  Sinclair models for compatibility claims. On the Profi family and Sprinter
  it stays off because its `#7E` claim collides with the palette ports.
- **Check the state before the audio** — `audio_moonsound` shows whether
  the software ever opened NEW2 (`#7E/#7F` are NEW2-gated) and keyed any
  FM channel or PCM slot; a flat capture with nothing keyed is the
  software, not the engine.
- **FM hiss / HiFi restore are tracked test scenarios** (regression tests
  exist) — if a capture shows hiss at idle or broken filter restore after
  a core-rate switch, that is a known class of bug, not expected behavior.
- **The wave ROM path must resolve server-side** — a renamed/moved
  `data/rom/opl4/` breaks wave synthesis while FM still plays; check the
  `[ROM] MOONSOUND=` path in the model config before suspecting the engine.
