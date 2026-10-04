# Recipe: Audio Mixer, Per-Device Capture and Capture Analysis

Scope: hearing and measuring one sound device at a time. The per-device
mixer (list the fitted devices, mute, solo, set the gain), recording the
master mix or a single device into a capture buffer, and reading what the
capture result says (levels per side, dominant pitch, WAV export), plus how
stereo placement shows up in those numbers. It does not cover what a
particular chip does with its registers: for that read the device recipe
([turbosound.md](turbosound.md), [generalsound.md](generalsound.md),
[neogs.md](neogs.md), [moonsound.md](moonsound.md),
[covox-sounddrive.md](covox-sounddrive.md)). The Sprinter's own DAC and ring
are in [machines/sprinter-sound.md](../machines/sprinter-sound.md).

Ground truth: mixer core
[audiomixer.h](../../core/src/emulator/sound/audiomixer.h) /
[audiomixer.cpp](../../core/src/emulator/sound/audiomixer.cpp), WebAPI handlers
`getAudioMixer` / `setAudioMixer` in
[state_audio_api.cpp](../../core/automation/webapi/src/api/state_audio_api.cpp)
and `audioCapture` / `audioCaptureStatus` / `audioCaptureResult` in
[analyzers_api.cpp](../../core/automation/webapi/src/api/analyzers_api.cpp),
MCP `capture_media` audio actions in
[mcp-media.cpp](../../core/automation/mcp/src/mcp-media.cpp), MCP aspect
`audio_mixer` in [mcp-tools.cpp](../../core/automation/mcp/src/mcp-tools.cpp).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `inspect_state` reads the mixer, `capture_media` captures and analyzes in
> one call. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines
> or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## Source keys

One set of names on every surface (WebAPI, CLI `mixer`/`audiocapture`, Lua
`audio_mixer*`/`audio_capture_start`, Python the same, MCP): `master`,
`beeper`, `ay1`, `ay2`, `fm1`, `fm2`, `covox` (the Covox / SoundDrive, or the
machine's own DAC: the Sprinter's Covox-Blaster), `gs`, `gs_mp3`,
`moonsound_fm`, `moonsound_pcm`, `cd0`..`cd3` (the CD drive on IDE unit
0..3). Only the devices the machine has are listed; naming one that is not
fitted is a `400` whose text lists the fitted keys.

## MCP (preferred)

```text
# 1. What is fitted, what is audible, what is making noise right now
inspect_state {"aspects":["audio_mixer"]}
#   → master {muted, sample_rate_hz} and devices[] {source, name, muted, solo, audible,
#     volume, gain_db, peak, active, capturable}

# 2. Change one device (same path as the WebAPI; MCP has no dedicated mixer action)
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/audio/mixer/ay1","body":{"muted":true}}
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/audio/mixer/covox","body":{"gain_db":-6}}
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/audio/mixer/beeper","body":{"solo":true}}
invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/audio/mixer/master","body":{"muted":false}}

# 3. Capture and analyze in one call (arms, runs the frames, fetches the result)
capture_media {"action":"audio_capture","seconds":2}                       # the master mix
capture_media {"action":"audio_capture","seconds":2,"source":"ay1"}        # one device
capture_media {"action":"audio_capture","seconds":5,"source":"gs_mp3","wav":true}   # + a WAV file

# 4. The same in steps (for a capture you armed yourself)
capture_media {"action":"audio_status"}
capture_media {"action":"audio_result","wav":true}
```

`audio_capture` takes `seconds` 0.01-30 (default 1), runs about 50 frames
per emulated second plus two of margin, and returns the analysis in
`structuredContent` (the arming reply is attached as `armed`). If the machine
is not running it says so and leaves the capture armed — run frames and poll
`audio_status`. The `wav` flag adds `wav_path`. `source` here means the audio
device; on `screenshot` the same argument means something else.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1

# Mixer: list, then set one device (PUT or POST; body keys are all optional)
curl -s "$BASE/emulator/$EMU_ID/audio/mixer" | jq '.devices[] | {source, muted, solo, gain_db, peak, active, capturable}'
curl -s -X PUT "$BASE/emulator/$EMU_ID/audio/mixer/ay1" -H 'Content-Type: application/json' \
     -d '{"muted":true}' | jq '.devices[] | select(.source=="ay1") | {muted, audible}'
curl -s -X PUT "$BASE/emulator/$EMU_ID/audio/mixer/covox" -H 'Content-Type: application/json' \
     -d '{"gain_db":-6}' | jq '.devices[] | select(.source=="covox") | {volume, gain_db}'

# Capture: arm, run frames, poll, read
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" -H 'Content-Type: application/json' \
     -d '{"action":"start","seconds":2,"source":"ay1"}' | jq '{armed, target_samples, sample_rate, source}'
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' \
     -d '{"frames":105}' > /dev/null          # 2 s * 50 + margin; or resume and wait
curl -s "$BASE/emulator/$EMU_ID/audio/capture/status" | jq '{complete, progress, captured_frames}'
curl -s "$BASE/emulator/$EMU_ID/audio/capture/result?wav=true" | jq '{duration_seconds, left, right, dominant_hz, wav_path}'

# Stop early (buffer is kept) or drop the data
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" -d '{"action":"stop"}' -H 'Content-Type: application/json' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" -d '{"action":"clear"}' -H 'Content-Type: application/json' | jq .
```

Mixer fields and ranges (from the handler):

| Field | Range | Notes |
|:--|:--|:--|
| `muted`, `solo` | `true`/`false` or `0`/`1` | `solo`: when any device is soloed only soloed devices are heard (`audible` shows the result) |
| `volume` | `0`..`1` linear | |
| `gain_db` | `-120`..`0` | instead of `volume`; 0 dB = 1.0; cannot boost above 0 dB |
| `master` | `muted` only | any other field is a `400` |

The reply to a `PUT` is the whole mixer report, so one call both sets and
verifies. Per-frame `peak` and `active` (`activeRecently`) are the quick
"is this device making sound" test without capturing anything.

## Is anything reaching the speakers? (`host_output`)

The mixer report also says what the host audio output got. A run that is not
paced to real time holds it: the speakers get nothing while every device keeps
computing its samples (captures and recordings still get them). Three things
hold it, each for exactly its own span: a direct run (`run_frames`,
`run_tstates`, `step`... - `direct_run`), a TTD seek or replay (`ttd_replay`)
and turbo mode (`turbo`). None of them touches the user's master mute.

| Field | Meaning |
|:--|:--|
| `held` | `true` while any hold is active |
| `holders` | active holds by reason: `direct_run`, `ttd_replay`, `turbo` |
| `holds_taken` | holds ever taken, by reason |
| `stale_holds_cleared` | holds a `resume` found without their reason and dropped (a leak; stays `0`) |
| `frames_delivered` / `frames_audible` / `frames_held` | emulated frames given to the speakers / of those, not silent / withheld |

"I resumed and hear nothing" - read it twice, a second apart:

```bash
curl -s "$BASE/emulator/$EMU_ID/audio/mixer" | jq '{master, host_output}'
```

A playing, resumed machine shows `held: false`, every `holders` entry `0`,
and `frames_delivered` / `frames_audible` growing by about 50 a second. If
`frames_audible` stays put while `frames_delivered` grows, the machine plays
silence (check `master.muted` and the devices' `peak`). If a holder is not `0`
after `resume`, something still runs fast (turbo on, a TTD replay); `resume`
itself drops holds whose reason is gone.

## Reading a capture result

`GET /audio/capture/result` (MCP `audio_result`) returns:

| Field | Meaning |
|:--|:--|
| `sample_rate`, `channels` (always 2), `frames`, `duration_seconds`, `complete` | the buffer's shape; the rate is the core audio rate |
| `left` / `right` `{peak, rms}` | normalized 0..1 per side |
| `zero_crossing_rate`, `dominant_hz` | crossings per second and half of it: a zero-crossing estimate on the mono mixdown |
| `wav_path`, `wav_bytes` (with `wav=true`) | a WAV file the server wrote; `wav_error` if it could not |

How to use them:

- **Is it silent?** Both `rms` near 0 means nothing reached the buffer:
  muted device, wrong source, or the device never played. A capture of one
  device takes its own buffer *before mute and volume*, so a muted device
  still records — that is what makes "mute it in the mix, still capture it"
  work.
- **Which note?** `dominant_hz` is one number for the whole buffer. It is
  right for one clean tone (capture one `source`), meaningless for a chord
  or a mix. For per-side pitch or a spectrum take the WAV and run an FFT on
  it (the Python one-liner is in [sprinter-sound.md](../machines/sprinter-sound.md)).
- **Stereo placement:** compare `left.rms` with `right.rms` for a single
  device. Equal means centered or mono; a lopsided pair means the device is
  panned. The placement itself is set elsewhere: the AY pair by the
  `[AY] Stereo=` preset in `unreal.ini` (`STEREO.ABC=100,10,66,66,10,100`
  is the six levels A-left, A-right, B-left, B-right, C-left, C-right as
  percent), the NeoGS DAC channels by `gs_stereo_mode`
  (`separated` | `gs` | `mono`, see [neogs.md](neogs.md)). Mono mode must
  give equal sides; `separated` on a one-channel-per-side test tune must not.
  The folder `docs/inprogress/2026-09-28-stereo-panning/` currently holds
  test tunes only (`materials/ABC_test.*`), no write-up.
- **Compare before and after a change:** capture the same `source` for the
  same number of frames twice and diff `left`/`right` `rms`.

## Pitfalls

- **The machine must run to fill the buffer.** Arming on a paused machine
  collects nothing; `audio/capture/result` answers `409` while a capture is
  armed but incomplete ("poll /audio/capture/status first") and `409` when
  there is no captured audio at all.
- **One capture buffer per instance.** A new `start` replaces the previous
  one; read or export the result first. `seconds` outside 0.01-30 is a `400`.
- **`source` is validated at arming time.** An unknown key, or a device that
  is not fitted, is a `400` listing the valid keys. Omit `source` for the
  master mix.
- **Capture is taken at the core rate, interleaved stereo.** `sample_rate`
  in the result is the truth for converting frames to seconds; do not
  assume 44100.
- **ZX DLSS delays audio too.** With a temporal video algorithm on, audio is
  delayed by the matching extra frames: add them to the run length or
  switch it off for audio checks.
- **Mixer changes are live and unsaved.** They do not write `unreal.ini` and
  are gone with the instance.
- **`gain_db` above 0 is a `400`**, and `volume` outside 0..1 too; there is
  no boost, use the device's own volume setting in the config for that.
