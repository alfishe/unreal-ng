# Recipe: Covox / SoundDrive DACs

The cheap sample-playback cards: 8-bit resistor DACs hanging off a port.
Three config toggles in `[SOUND]` (per model config), all served by one
4-channel class ([covox.h](../../core/src/emulator/sound/covox.h)):

| Key | Default | Effect |
|:--|:--|:--|
| `SD=1` | on | SoundDrive quad DAC: `#F1` Left A, `#F3` Left B, `#F9` Right A, `#FB` Right B (`#FB` doubles as mono Covox) |
| `CovoxFB=1` | on | with `SD=0`: mono Covox at `#FB` only; with `SD=1` the quad already owns `#FB` |
| `CovoxDD=0` | off | parsed but currently **inert** — no `#DD` device is wired on `master` |

All are **config-file toggles** — changing them needs a new instance.
On the `profi` branch the Profi gets its own Covox/SoundDrive at `#5F/#3F`
(see [machines/profi.md](../machines/profi.md)).

Port-map ground truth: VELESOFT DAC-for-ZX database, SoundDrive 1.05
mode 2 (<https://velesoft.speccy.cz/da_for_zx-cz.htm>) and the BC Info
Guide #4 port table ([zx-ports-full-table.md](../../docs/ports/zx-ports-full-table.md),
pattern `1111B0A1`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `capture_media audio_capture` is the only reliable verification (the DACs
> have no introspection endpoint yet). Use [WebAPI](#webapi) only inside
> host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PENTAGON"}   # CovoxFB=1, SD=1

# Run anything that plays samples through the DAC (digital music, speech,
# SFX demos), then:
capture_media {"action":"audio_capture","seconds":2}
#   → left/right {peak, rms}, dominant_hz, zero_crossing_rate
#   flat zero peak = the card is muted/absent or the software never wrote it

# Cross-check the channel mix the sound manager actually produced:
inspect_state {"aspects":["audio_ay"]}          # AY silent while DAC plays?
invoke_api   {"method":"GET","path":"/emulator/{id}/state/audio/channels"}
```

There is no `/state/audio/covox` data yet — on `master` the route answers
`{"status":"not_implemented"}` (reserved). Plan around capture analysis,
port traces on the DAC ports, and `/state/audio/channels`.

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PENTAGON"}' | jq '{id, model}'

curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" \
     -H 'Content-Type: application/json' -d '{"action": "start", "seconds": 2}' | jq .
# ...run ~100 frames of the sample player...
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result" \
     | jq '{dominant_hz, left: {peak: .left.peak, rms: .left.rms}, right: .right.peak}'

# Channel-level view of what mixed into the frame
curl -s "$BASE/emulator/$EMU_ID/state/audio/channels" | jq .
```

For driver work, trace the DAC writes directly — a `port_trace` filtered on
`#FB` (or any of the four SoundDrive ports) gives the sample stream the
software pushed, with PC attribution for who wrote it
([port-trace.md](../analysis/port-trace.md)).

Cheap fitment check: `GET /emulator/{id}/ports` shows the row the wiring
actually installed — `SoundDrive quad DAC (#F1 L-A, #F3 L-B, #F9 R-A, #FB
R-B...)` when `SD=1`, `Covox (mono #FB)` when only `CovoxFB=1`, and no row
at all when neither flag is set.

## Pitfalls

- **`CovoxDD` does nothing yet** — it is parsed but no `#DD` device exists
  on `master`; Scorpion Covox software using `#DD` stays silent on every
  build. Clone configs ship `SD=1` + `CovoxFB=1`, so `#FB` (mono Covox
  path) works everywhere — prefer `#FB` variants of the software.
- **`SD=1` wins `#FB`** — with `SD=1` all four ports are wired to the quad
  DAC; `CovoxFB=1` alone (with `SD=0`) wires only the mono `#FB` Covox. The
  class auto-centers the output when only `#FB` is ever written
  (mono-Covox compatibility), so plain Covox software sounds centered on a
  fitted SoundDrive — no separate card needed.
- **No register state exists for a DAC** — it's a latch, not a chip; don't
  look for per-register endpoints. The write value *is* the output level.
- **Profi port arbitration silences the DAC by design** (branch) — during
  FDC/CMOS activity on `#3F/#5F` the Profi DAC goes quiet; don't file that
  as a regression mid-disk-operation.
- **Silence proves nothing until software actually writes the port** —
  combine the capture with a port trace before concluding the card is dead.
