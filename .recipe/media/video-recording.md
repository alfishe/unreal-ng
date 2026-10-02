# Recipe: Record video (with sound) from automation

Goal: record the emulated screen to a file, optionally with the emulated sound track, from MCP,
the WebAPI, the CLI, Lua or Python, and check the file.

Reference: endpoint `POST /video/record` (`recording_api.cpp`), the shared rules in
`core/recording/src/recordingrequest.h`, the A/V checks in
[crash-fb-overflow.md](../../docs/inprogress/2026-09-28-sprinter/crash-fb-overflow.md#stability-verification).
Verified 2026-10-02 on macOS GUI builds: WebAPI, MCP, CLI, Lua and Python (`tools/verification/webapi/src/test_api_recording.py`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi)
> inside host-side pipelines or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## What you can ask for

- **Format** (`format`): `gif` (default, native, no sound), `h264`, `h265`/`hevc`, `vp9`, `rawvideo`.
  The container is the file's extension (`.mp4`, `.mov`, `.mkv`, `.webm`, `.avi`, `.gif`).
- **Sound** (`audio`): leave it out for video only (the default everywhere). `"aac"` (or `true`)
  adds the emulated sound. The codec must fit the container:

  | container | audio codecs |
  |:--|:--|
  | `.mp4` | aac, mp3, opus, flac |
  | `.mov` | aac, mp3, pcm_s16le |
  | `.mkv` | aac, mp3, opus, vorbis, flac, pcm_s16le |
  | `.webm` | opus, vorbis |
  | `.avi` | aac, mp3, pcm_s16le |
  | `.gif` | none: `gif` + `audio` is refused (HTTP 400) |

  On macOS, h264/hevc + aac in `.mp4`/`.mov` uses the native encoder (VideoToolbox +
  AVAssetWriter). Every other combination needs ffmpeg on the PATH.
- **Bitrates** (optional, kbps, 0 = encoder default): `video_bitrate` 100..200000,
  `audio_bitrate` 32..512 (needs `audio`).
- **Picture**: `scale` 1..4 (integer upscale; 2 keeps color detail through H.264 chroma
  subsampling), `region` `full` (with border) or `screen`, `fps` 1..100.

Timestamps are emulated time: a recording made in turbo mode plays at normal speed, and the
sound track has exactly the frames' length.

## MCP (preferred)

```text
capture_media {"action":"record_start","format":"h264","filename":"/abs/path/scratch/run.mp4",
               "scale":2,"audio":"aac"}
#  -> Recording started → /abs/path/scratch/run.mp4 (h264 + aac audio 44100 Hz); run the emulator, then record_stop
capture_media {"action":"record_status"}      # frames_recorded, audio_samples_recorded, audio_duration ...
capture_media {"action":"record_stop"}        # -> Recorded N frame(s) + S s of aac audio → file (bytes)

# Bounded: N frames, then stop by itself (runs the frames for you)
capture_media {"action":"record_start","format":"gif","frames":100,"filename":"/abs/path/scratch/clip.gif"}
```

Without `frames` the session stays open until `record_stop`; let the emulator run (or drive it
with `type_input` / `mouse_input`) in between.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
curl -s -X POST $BASE/emulator/$EMU_ID/video/record -H 'Content-Type: application/json' \
     -d "{\"action\":\"start\",\"format\":\"h264\",\"audio\":\"aac\",\"scale\":2,\"filename\":\"$PWD/scratch/run.mp4\"}" \
     | jq -c '{recording, audio, audio_codec, audio_sample_rate, audio_channels, output}'
#  {"recording":true,"audio":true,"audio_codec":"aac","audio_sample_rate":44100,"audio_channels":2,"output":".../run.mp4"}

curl -s $BASE/emulator/$EMU_ID/video/record/status | jq -c '{frames_recorded, emulated_duration, audio_duration}'
curl -s -X POST $BASE/emulator/$EMU_ID/video/record -H 'Content-Type: application/json' -d '{"action":"stop"}' \
     | jq -c '{frames_recorded, emulated_duration, audio_duration, file_size}'

# Refused before anything changes:
curl -s -X POST $BASE/emulator/$EMU_ID/video/record -H 'Content-Type: application/json' \
     -d '{"action":"start","format":"gif","audio":"aac"}' | jq -r .message
#  GIF has no audio track. Record h264/hevc into .mp4 or .mov to include audio, or leave audio out.
```

## CLI, Lua, Python

```text
videorecord start h264 scratch/run.mp4 --scale 2 --audio aac [--audio-bitrate 192] [--video-bitrate 8000]
videorecord status          # ... Audio: aac, 44100 Hz, 2 ch / Audio samples: N (S s)
videorecord stop
```

```lua
video_record("start", {format = "h264", filename = "scratch/run.mp4", scale = 2, audio = "aac"})
video_record_status().audio_duration
video_record("stop")
```

```python
emu.video_record("start", {"format": "h264", "filename": "scratch/run.mp4", "scale": 2, "audio": "aac"})
emu.video_record("stop")
```

## Assert on (ffprobe)

```bash
ffprobe -v error -show_entries stream=codec_type,codec_name,width,height,sample_rate,channels,duration \
        -of compact scratch/run.mp4
ffmpeg -v info -i scratch/run.mp4 -map 0:a -af astats=measure_perchannel=none -f null - 2>&1 | grep 'RMS level'
```

- Video size = framebuffer x `scale` (Pentagon 352x288 -> 704x576 at 2x; Sprinter 736x288 is
  stored at half height, so 1472x1152; TS-Conf 720x288 -> 1440x1152).
- Frame count = emulated seconds / frame length (Pentagon 20.48 ms, 48.83 fps).
- Audio: AAC 48 kHz stereo in the file (the core rate, 44.1 kHz by default, is converted).
- `duration` of the audio and video streams agree within a millisecond or two; the sound starts
  with the picture (no AAC priming offset).

## Pitfalls

- **A GIF has 1/100 s frame delays**: 20.48 ms frames are written as 20 ms, so a GIF plays about
  2% fast. Use h264 when timing matters.
- **The sound is what the machine plays**: a silent program records a silent track (RMS -inf).
  Sprinter DSS programs that use the Covox-Blaster are silent until that card exists.
- **GUI on macOS reading `~/Downloads`**: a freshly built app may wait for a privacy prompt the
  first time it opens a file there (the request hangs). Keep media and recordings under the
  project's `scratch/`.
- **The CLI's default file for h264 is `.mkv`** (written by ffmpeg); name a `.mp4` file for the
  native macOS encoder.
