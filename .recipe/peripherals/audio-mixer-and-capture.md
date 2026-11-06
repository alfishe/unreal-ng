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
0..3), and a ZX-MultiSound's `ms_ssg1`, `ms_ssg2`, `ms_fm1`, `ms_fm2` (its
YM2203 pair per chip, like the TurboSound FM's `ay1` / `ay2` / `fm1` / `fm2`),
`ms_saa`, `ms_pcm` (the GS + SounDrive DACs), `ms_midi`, a Fuller Box's `ay3`, and the ZX Spectrum Next's
`next_ay1`..`next_ay3` and `next_dac` (its three AYs and its DAC, one row each). Only the devices the machine has are
listed; naming one that is not fitted is a `400` whose text lists the fitted keys.

Channels: `<source>.<channel>` names one channel of a row - `ay1.a` / `.b` / `.c`, `covox.la` / `lb` / `ra` / `rb` (SounDrive; a mono Covox - `#FB`, the Scorpion's `#DD` - has `covox.mono` alone),
`gs.1`..`gs.4` (NeoGS `.8`), `fm1.1`..`fm1.3`, `moonsound_fm.1`..`.18`, `moonsound_pcm.1`..`.24` (metered),
`ms_ssg1.a`, `ms_fm1.2`, `ms_midi.1`..`.16` (mute and solo only), `next_dac.a`..`.d`. A channel takes muted, solo
(within its row: the row's other channels go silent) and volume / gain_db, applied at the next frame start
([mixer-channels-and-settings.md](../../docs/emulator/design/audio/mixer-channels-and-settings.md)).

## MCP (preferred)

```text
# 1. What is fitted, what is audible, what is making noise right now
inspect_state {"aspects":["audio_mixer"]}
#   → master {muted, sample_rate_hz} and devices[] {source, name, muted, solo, audible,
#     volume, gain_db, peak, active, capturable}

# 2. Change one device or channel (the audio_mixer tool; the same PUT /audio/mixer/{source} underneath)
audio_mixer {"action":"set","source":"ay1","muted":true}
audio_mixer {"action":"set","source":"covox","gain_db":-6}
audio_mixer {"action":"set","source":"ay1.b","solo":true}          # only channel B of AY 1
audio_mixer {"action":"set","source":"master","muted":false}
audio_mixer {"action":"mute"}                                      # the master (or "source"); "unmute" undoes it
audio_mixer {"action":"get","source":"moonsound_fm"}               # a row and its channels (peaks)
audio_mixer {"action":"levels"}                                     # every row's and channel's level and mute / solo
audio_mixer {"action":"set_setting","name":"ay_stereo","value":"acb"}

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
computing its samples (captures and recordings still get them). Four things
hold it, each for exactly its own span: a direct run (`run_frames`,
`run_tstates`, `step`... - `direct_run`), a TTD seek or replay (`ttd_replay`),
turbo mode (`turbo`) and script hooks of the `unpaced` policy that keep the
frames over their period (`unpaced`, [script callbacks](../../docs/features/script-callbacks.md)).
None of them touches the user's master mute.

| Field | Meaning |
|:--|:--|
| `held` | `true` while any hold is active |
| `holders` | active holds by reason: `direct_run`, `ttd_replay`, `turbo`, `unpaced` |
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

## Sound or AY writes of a recorded (TTD) span: rewind, drop the TTD, free-run with a capture

A replay cannot record these. The device histories (`ay-socket`, the AY log) write nothing while TTD replays
(`ttdReplayActive`: a seek re-executes what a history already holds), and a `seek` fast-forwards from the nearest
checkpoint without rendering sound, so only the last frame of the jump reaches the audio capture. The way that
works is a plain run from the point you want:

```bash
# 1. The session back from disk (a fresh instance of the same model is fine); it comes up idle
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/load" -H 'Content-Type: application/json' -d '{"path":"scratch/session-001.ttd"}'
# 2. Rewind to the start of the span
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/seek" -H 'Content-Type: application/json' -d '{"frame": 12000}'
# 3. Drop the TTD: stop it, then forget the history
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop"
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/invalidate"
# 4. Arm what you want while the machine is still paused at that point
curl -s -X POST "$BASE/emulator/$EMU_ID/audio/capture" -H 'Content-Type: application/json' \
     -d '{"action":"start","seconds":600,"to_file":true,"path":"scratch/span.wav"}'
curl -s -X POST "$BASE/emulator/$EMU_ID/history/ay-socket/config" -H 'Content-Type: application/json' \
     -d '{"mode":"capture","limit":0,"stream":"scratch/span-ay.jsonl"}'
# 5. Free-run (resume) for the length of the span; the capture closes itself at its target.
#    The FIRST resume after the TTD stop is followed by a pause (seen 5 of 5 times, also after 2 s of waiting):
#    ask GET /emulator for is_paused and POST /resume again until it is false (the second one holds)
curl -s -X POST "$BASE/emulator/$EMU_ID/resume"
# 6. Read /audio/capture/status until file_closed, then switch the history off (closes its stream)
curl -s -X POST "$BASE/emulator/$EMU_ID/history/ay-socket/config" -H 'Content-Type: application/json' -d '{"mode":"off"}'
```

Notes:

- **Measured end to end** (Pentagon, a loop writing AY registers and toggling the beeper; dump, load, seek, stop,
  invalidate, both captures armed, resume): the WAV is exactly 1.0 s (88 200 samples, 176 444 bytes, header patched),
  the AY history holds all 123 988 writes of that second with none dropped, its stream file has the same number of
  lines. Without the second resume the same run collected 3 614 samples in 2 s: the machine stood paused.
- **Both captures together.** The audio capture and the AY history see the same frames, so one pass gives the sound
  and the register writes of a span (`frame` / `tacts` of each write against the sample position:
  `samples = frames * samples_per_frame + tacts * rate / cpu_clock`).
- **The AY history has no 65 536 ceiling**, unlike `/ay/log` (a ring clamped to 65 536, 128 194 entries lost in 2 s
  of a busy loop). `capture` with `limit` 0 and a `stream` kept all 294 469 writes of 3 s with none dropped. The
  device is `ay-socket` on a machine with a TurboSound / TSFM card, else `ay`: `GET /history` lists the name.
- **Input of the original session is not fed again by a free run.** Keys, the mouse and tape / disk actions
  journaled in the recording replay during `seek` / `step-forward` only. For a span that needs them (a menu to get
  through, a tape to load), drive them yourself (`keyboard/type`, `tape/...`) after the rewind. Not measured here:
  check a span with input against the original before trusting its audio.
- **Short spans without leaving the replay.** `step-forward` renders the sound of each frame it replays, so a few
  dozen frames can be captured in place (see the TTD recipe, "Audio during a replay"); the AY history stays silent
  there.

## Pitfalls

- **The machine must run to fill the buffer.** Arming on a paused machine
  collects nothing; `audio/capture/result` answers `409` while a capture is
  armed but incomplete ("poll /audio/capture/status first") and `409` when
  there is no captured audio at all.
- **Long captures go to a file.** The memory capture holds 30 s at most. `POST /audio/capture
  {"action":"start","seconds":600,"to_file":true,"path":"..."}` streams a WAV through a writer thread (up to
  24 h, only the disk limits it; the same on every plane: CLI `audiocapture start 600 file out.wav`, Lua
  `audio_capture_start(600, "", "out.wav")`, Python `audio_capture_start(600, file="out.wav")`, MCP `audio_capture`
  with `to_file` / `path`, which arms and returns at once); `stop` closes the file, `status` shows `wav_path` / `file_closed`, `result` gives
  the path and length (no level analysis: the samples are not in memory). The `cap.audio_capture` action always
  streams, so it has no 30 s ceiling.
- **A stopped capture holds only what came.** `stop` cuts the buffer to the captured samples; the result
  and the WAV never carry a silent tail up to the target.
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
