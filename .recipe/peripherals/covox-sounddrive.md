# Recipe: Covox / SoundDrive DACs

The cheap sample-playback cards: 8-bit resistor DACs hanging off a port.
Three config toggles in `[SOUND]` (per model config), all served by one
4-channel class ([covox.h](../../core/src/emulator/sound/covox.h)):

| Key | Default | Effect |
|:--|:--|:--|
| `SD=1` | on | SoundDrive quad DAC on **both** port schemes for the same 4 channels: mode 2 mirror set `#F1` Left A, `#F3` Left B, `#F9` Right A, `#FB` Right B, and mode 1 primary set `#0F` Left A, `#1F` Left B, `#4F` Right A, `#5F` Right B (`#FB` doubles as mono Covox) |
| `CovoxFB=1` | on | with `SD=0`: mono Covox at `#FB` only; with `SD=1` the quad already owns `#FB` |
| `CovoxDD=0` | off | parsed but currently **inert** — no `#DD` device is wired on `master` |

Mode 1 (`#0F/#1F/#4F/#5F`) physically aliases into the Beta128 FDC's wide
mirror decode, so a Pentagon/Scorpion machine can only have one peripheral
answering there at a time: **Beta128 wins while TR-DOS is paged in**,
SoundDrive claims the same addresses the rest of the time when `SD=1`
(matches the reference decoders — pentevo/Unreal `io.cpp` and Xpeccy
`soundrive.c` SDRV_105_1 — so this isn't emulator-specific behavior).

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
`#FB` (or any of the eight SoundDrive addresses — mode 1 + mode 2) gives the sample stream the
software pushed, with PC attribution for who wrote it
([port-trace.md](../analysis/port-trace.md)).

Cheap fitment check: `GET /emulator/{id}/ports` shows the rows the wiring
actually installed — with `SD=1`, two rows: `SoundDrive quad DAC mode 2
(#F1 L-A, #F3 L-B, #F9 R-A, #FB R-B...)` and `SoundDrive quad DAC mode 1
(#0F L-A, #1F L-B, #4F R-A, #5F R-B)` (the mode-1 row's `gate` field states
the TR-DOS precedence); `Covox (mono #FB)` when only `CovoxFB=1`; no row at
all when neither flag is set.

## Pitfalls

- **`CovoxDD` does nothing yet** — it is parsed but no `#DD` device exists
  on `master`; Scorpion Covox software using `#DD` stays silent on every
  build. Clone configs ship `SD=1` + `CovoxFB=1`, so `#FB` (mono Covox
  path) works everywhere — prefer `#FB` variants of the software.
- **`SD=1` wins `#FB`** — with `SD=1` all eight addresses (mode 1 + mode 2)
  are wired to the same 4-channel quad DAC; `CovoxFB=1` alone (with `SD=0`)
  wires only the mono `#FB` Covox. The class auto-centers the output
  whenever exactly one of the four channels is driven — not just `#FB` —
  so plain mono digi/Covox software sounds centered regardless of which of
  the eight addresses it happens to use.
- **Switching player modes mid-song can click once** — mode 1 and mode 2
  are two bus schemes for the *same* 4 latches, so a demo that switches
  from driving e.g. mode-1 Left B (`#1F`) to mode-2 Right B (`#FB`) leaves
  Left B's old value frozen (nothing rewrote it). A channel untouched for
  `Covox::STALE_CHANNEL_FRAMES` (3) whole frames decays back to the 0x80
  midpoint automatically, restoring mono-centering — expect at most one
  brief click right at the switch, not a persistent imbalance.
- **No register state exists for a DAC** — it's a latch, not a chip; don't
  look for per-register endpoints. The write value *is* the output level.
- **Profi port arbitration silences the DAC by design** (branch) — during
  FDC/CMOS activity on `#3F/#5F` the Profi DAC goes quiet; don't file that
  as a regression mid-disk-operation.
- **Silence proves nothing until software actually writes the port** —
  combine the capture with a port trace before concluding the card is dead.
