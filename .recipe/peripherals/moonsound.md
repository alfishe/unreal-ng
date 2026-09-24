# Recipe: MoonSound (OPL4) Card

Yamaha OPL4 (YMF278B) — FM synthesis plus wavetable PCM driven by the
YRW801 wave ROM. **Branch-only hardware:** the engine
(`core/src/3rdparty/opl4`, `soundchip_moonsound`) lives on the `moonsound`
branch. On `master` the `MoonSound=` config key is parsed but inert —
no engine, no sound.

Ground truth:
`core/src/emulator/sound/chips/soundchip_moonsound.h` (moonsound branch —
file not on master), OPL4 core `core/src/3rdparty/opl4/` (same branch),
design docs
[docs/inprogress/2026-09-13-moonsound/](../../docs/inprogress/2026-09-13-moonsound/)
(core TDD, TTD integration, emulator integration).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> create a clone model and prove the card with
> `capture_media audio_capture`. Use [WebAPI](#webapi) only inside
> host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## Where the card exists

Config key `MoonSound=` (plus `MOONSOUND=` wave-ROM path):

| Model | Value |
|:--|:--|
| `PENTAGON`, `SCORPION`, `PROFSCORP`, `ATM710`, `ATM3` | `1` |
| `48K`, `128k`, `PLUS3` | `0` — "real Sinclair never had this card - clones only" |

Caveat: on `master`, every model config (`spectrum48`/`spectrum128`
included) ships `MoonSound=1` — the engine simply isn't compiled in there,
so the key is inert everywhere on this branch. The `0`-on-Sinclair-models
policy above could not be verified from `master`; confirm against the
`moonsound` branch's own configs before relying on it.

Wave ROM: `data/rom/YRW801-M - Yamaha - 1993.rom` (config key `MOONSOUND=`,
verified in `data/configs/pentagon128k/unreal.ini` on `master` — no
`opl4/` subfolder exists under `data/rom/`; if the branch config differs,
that's the path to check first).

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
# On a moonsound-branch build:
emulator_manage {"action":"create","model":"PENTAGON"}   # MoonSound=1 clones

# Load an OPL4-capable demo/music (disk or tape recipes), then:
capture_media {"action":"audio_capture","seconds":3}
#   → left/right {peak, rms}, dominant_hz, zero_crossing_rate,
#     sample_rate, duration_seconds; "wav":true exports wav_path

# While iterating on a driver, watch the FM/wave traffic instead:
#   port_trace with an address filter on the #C4-#C7 / #7E-#7F range
#   (see analysis/port-trace.md — filters are port masks, so one
#   mask covering 0x00C4-0x00C7 needs the low-byte form)
```

## WebAPI

```bash
# Confirm the build actually has the engine
curl -s "$BASE/emulator/status" | jq '.server.git_branch'   # "moonsound"

curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "ATM710"}' | jq '{id, model}'

curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" \
     -H 'Content-Type: application/json' -d '{"action": "start", "seconds": 3}' | jq .
# ...run ~150 frames...
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result" \
     | jq '{dominant_hz, left: .left.peak, right: .right.peak}'
```

## Pitfalls

- **`MoonSound=1` on `master` produces silence** — the key is parsed, the
  engine is not there. Check `server.git_branch` before debugging "dead
  card" reports.
- **Clone-only by policy** — on `48K/128k/PLUS3` the branch itself sets
  `MoonSound=0`; don't override the config to force it there for
  compatibility claims.
- **No per-card state endpoint** — unlike AY/TSFM (`/state/audio/ay`,
  `/state/audio/fm`) the OPL4 has no introspection route yet; the card is
  verified by capture analysis and port traces, not register dumps.
- **FM hiss / HiFi restore are tracked test scenarios** on the branch
  (regression tests exist) — if a capture shows hiss at idle or broken
  filter restore after a core-rate switch, that is a known class of bug,
  not expected behavior.
- **The wave ROM path must resolve server-side** — a renamed/moved
  `data/rom/opl4/` breaks wave synthesis while FM still plays; check the
  `MOONSOUND=` path in the model config before suspecting the engine.
