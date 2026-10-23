# Recipe: Record the GUI window (CRT effects, on-screen keyboard) to a video

Goal: a demo video of what the unreal-qt window shows, CRT shader and all, with the machine's on-screen keyboard
(OSD) next to it lighting the keys the program holds and the machine's sound, at the machine's speed with every
frame, whatever the host's load; then, if wanted, a 4K file YouTube keeps sharp.

Reference: `POST/GET /api/v1/emulator/{id}/video/window/record`, `.../record/frames`, `.../record/stop`
(`video_window_api.cpp`), `unreal-qt/src/widgets/windowrecorder.cpp`, the demo script
[tools/recording/pentagon_osd_demo.py](../../tools/recording/pentagon_osd_demo.py). Verified 2026-10-20 on macOS
(a GUI unreal-qt, a Pentagon 128, the pentagon2plus keyboard, crt-lottes): every MCP and WebAPI call below;
2026-10-22 the CLI, Lua and Python calls.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side pipelines
> (a scripted demo that presses keys on given frames) or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## Which recording

| Want | Use |
|:--|:--|
| The emulated frame (no CRT, no OSD), with sound, GIF / H.264 / HEVC | [video-recording.md](video-recording.md) (`record_start`, `POST /video/record`): unchanged |
| A still of the window as drawn | `screenshot source=window` ([crt-effects.md](crt-effects.md)) |
| **The window as drawn + the on-screen keyboard + sound, every frame** | this recipe (`window_record_*`, `/video/window/record*`) |

## The clock is the machine's

The file's time is emulated time, not the host's. Every emulated frame is one video frame at the machine's own rate
(3.5 MHz / t-states per frame: 3500000/71680 = 48.828 fps on a Pentagon) and carries the samples
the sound mixed in that frame (the frame audio tap, taken before mute like `video/record`'s). A frame the window did
not draw (a busy GUI, a running machine under load, turbo) repeats the last picture; its sound is still its own. So
the picture and the sound stay together however slow or fast the host runs the machine, stepped or running.
A muted instance (`audio/mixer/master muted`) still records its sound.

The window shows the frame latched the present delay ago (2 frames for the A/V sync, 7 with ZX DLSS
`PUT /video/temporal {"algorithm":"mod-tpgwafsd"}`, which looks ahead); the recording delays the sound and the
keyboard by the same frames, so a key lights, clicks and draws its character in one video frame. Switch ZX DLSS on
before the start: a delay changed while recording inserts silence (longer) or drops sound (shorter) for the difference.

## What there is

- **GUI only**, the instance the window shows; headless: 409 `no-window`. Needs `ffmpeg` on the host (PATH,
  `/opt/homebrew/bin`, `/usr/local/bin`).
- **Start** (`video/window/record`): `path` (server side, required), `keyboard` (`"default"` = the keyboard the OSD
  shows, a picture id of the machine such as `pentagon2plus`, another machine's picture whose keys drive the same
  matrix such as `zxnext` on a Pentagon (for the recording only, the window's own comes back at stop), or left out =
  the picture only; a wrong id lists the offered ones), `codec` (`prores` = ProRes 4444 `.mov`, the default; `h264` = H.264 4:4:4 `.mp4`: full colour, so a
  mask a pixel wide survives), `audio` (default true: the sound track, PCM in `.mov`, AAC 320k in `.mp4`; false =
  picture only), `fps` (left out = the machine's rate; 1..100 only with `audio: false`, else 400), `scale` (1..4: the picture doubled in blocks, the default 1),
  `picture_height` (0..4320: the CRT drawn at that height instead of the window's, GPU only), `width` + `height` (the
  file's frame; left out = the picture with the keyboard under it), `layout` (`below` or `side`, with a frame).
- **Frames** (`video/window/record/frames {frames}`): the machine must be paused (409 `not-paused`). Runs `frames` frames, each one drawn
  by the window and written before the next runs. No time limit on the emulator's side; an MCP client may have its
  own per-call timeout, so a few hundred frames per call keeps progress visible.
- **Status** (`GET video/window/record`) / **stop** (`video/window/record/stop`): `{recording, path, keyboard, frames,
  width, height, fps, audio, sample_rate, audio_samples}`. Stop waits for ffmpeg, then muxes the sound in (the
  picture copied, not re-encoded: seconds). Meanwhile the picture is in `.<name>.video.<ext>` and the sound in
  `.<name>.sound.s16le` beside the file; a failed mux leaves both and names them.
- **The HUD** ("Remote control" and the like) is never in the file; the keys the program holds light up on the
  keyboard (a combination such as CS+6 lights its own key, the Down arrow, when the picture has one).
- **Running instead of stepping**: a running machine records too, the frames the window skipped repeat the picture
  before. Step with `record/frames` for a file where every picture is its own frame.

## MCP (preferred)

```text
emulator_manage {"action":"start","model":"PENTAGON"}             # or the instance the window already shows
capture_media {"action":"window_display_set","display":{"gpu":true,"crt_profile":"crt-lottes"}}
control_execution {"action":"take_control"}                       # the GUI shows who drives it and locks
control_execution {"action":"pause"}
capture_media {"action":"window_record_start","filename":"/abs/path/scratch/demo.mov","keyboard":"pentagon2plus","scale":2}
  -> window recording to /abs/path/scratch/demo.mov: 1406x1828 at 48.828 fps, sound 44100 Hz, 0 frames, keyboard
     pentagon2plus; pause, then
     window_record_frames {frames:N} records every frame (keys in between: type_input / keyboard press-release),
     window_record_stop to finish [target ...]
capture_media {"action":"window_record_frames","frames":100}
  -> ran 100 frames, window recording to ...: 1406x1828 at 48.828 fps, sound 44100 Hz, 100 frames, keyboard ... [target ...]
type_input {"action":"type","text":"10 PRINT \"HI\"\n"}         # queued: the keys go down while frames run
capture_media {"action":"window_record_frames","frames":300}
capture_media {"action":"window_record_stop"}
  -> window recording off, last /abs/path/scratch/demo.mov: 1406x1828 at 48.828 fps, sound 44100 Hz, 400 frames, ...
control_execution {"action":"resume"}
control_execution {"action":"release_control"}
```

Mute the instance first (`PUT /audio/mixer/master {"muted":true}`); put the user's window settings back after.

Errors (`isError: true`, the HTTP status in the text):

```text
window_record_frames on a running machine  -> HTTP 409: Pause the machine first: it runs frame by frame here
window_record_frames without a recording   -> HTTP 409: The window is not being recorded: video/window/record first
"keyboard":"pcat" on a Pentagon             -> HTTP 409: Keyboard 'pcat': keyboard 'pcat' drives the pc matrix, this
                                               machine the zx one (this machine's: pentagon, pentagon2plus, or another
                                               with its matrix)
"scale":4,"width":640,"height":480          -> HTTP 409: The picture (2812x2304) does not fit the frame
window_record_frames {"frames":0}           -> HTTP 400: Body {frames}: 1..100000
```

## WebAPI

```bash
B=http://localhost:8090/api/v1
ID=$(curl -s $B/emulator | jq -r '.emulators[0].id'); E=$B/emulator/$ID
S=$(curl -s -X POST $E/control/session -H 'Content-Type: application/json' -d '{"client":"demo"}' | jq -r .session)
c() { curl -s -H "X-Unreal-Session: $S" -H 'Content-Type: application/json' "$@"; }   # every call as the session's
c -X PUT $E/audio/mixer/master -d '{"muted":true}' >/dev/null
c -X PUT $E/video/window -d '{"gpu":true,"crt":true,"crt_profile":"crt-lottes"}' >/dev/null
c -X POST $E/pause >/dev/null
c -X POST $E/video/window/record -d "{\"path\":\"$PWD/scratch/demo.mov\",\"keyboard\":\"pentagon2plus\",\"scale\":2}"
#  {"recording":true,"path":".../demo.mov","keyboard":"pentagon2plus","frames":0,"width":1406,"height":1828,
#   "fps":48.828125,"audio":true,"sample_rate":44100,"audio_samples":0}
c -X POST $E/keyboard/press -d '{"key":"down"}' >/dev/null
c -X POST $E/video/window/record/frames -d '{"frames":6}'      # the key held 6 frames, all recorded
c -X POST $E/keyboard/release -d '{"key":"down"}' >/dev/null
c -X POST $E/video/window/record/frames -d '{"frames":100}'
#  {"recording":true,...,"frames":106,"ran":100}
c -X POST $E/video/window/record/stop
c -X POST $E/resume >/dev/null
c -X DELETE $E/control/session                                 # the GUI unlocks
```

A scripted demo plans the key changes on frame numbers, then alternates `record/frames` (up to the next change) and
`keyboard/press` / `release`. [tools/recording/pentagon_osd_demo.py](../../tools/recording/pentagon_osd_demo.py)
does it for a 31-second Pentagon 128 demo: the boot menu (Down, Down, Down, Up, Up, Enter), 128 BASIC, a three-line
program typed key by key (symbol shift for `= : ; "`), `RUN`:

```bash
python3 tools/recording/pentagon_osd_demo.py --port 8090 --out $PWD/scratch/demo.mov
```

Key names: `/keyboard/keys` (`cs`, `ss`, `enter`, `space`, `up`, `down`, letters, digits).

## CLI, Lua, Python

The same checks and the same report (`WindowRecording`, [command-interface.md](../../docs/emulator/design/control-interfaces/command-interface.md#gui-window-recording)):

```text
video window record /abs/path/scratch/demo.mp4 keyboard=default codec=h264
video window record frames 100
video window record stop
```

```lua
-- global functions on the selected instance (POST /api/v1/lua/exec, the Lua console)
window_record("/abs/path/scratch/demo.mp4", {keyboard = "default", codec = "h264"})
window_record_frames(100)
window_record_stop()
```

```python
import unreal_emulator as unreal
emu = unreal.emu_get_selected()
emu.window_record('/abs/path/scratch/demo.mp4', keyboard='default', codec='h264')
emu.window_record_frames(100)
emu.window_record_stop()
```

## A 4K file for YouTube

YouTube re-encodes to 4:2:0 (a colour sample per 2x2 pixels): a CRT mask a pixel wide turns to grey moiré at the
window's size. Draw the CRT at 1080 lines and double it in blocks: every mask pixel then has its own colour sample.

```text
capture_media {"action":"window_record_start","filename":"/abs/path/scratch/demo-4k.mov","keyboard":"pentagon2plus",
               "picture_height":1080,"scale":2,"width":3840,"height":2160,"layout":"side"}
```

The script: `--scale 2 --picture-height 1080 --frame 3840x2160 --layout side`. ProRes 4444 at 4K is large (~10 GB
for 30 s); convert for the upload and delete the master:

```bash
ffmpeg -i scratch/demo-4k.mov -c:v hevc_videotoolbox -b:v 70M -tag:v hvc1 -pix_fmt yuv420p -c:a aac -b:a 320k \
       scratch/demo-4k.mp4
# or software: -c:v libx265 -crf 16 -preset slow -tag:v hvc1 -pix_fmt yuv420p
ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames,duration -of compact scratch/demo-4k.mp4
#  stream|codec_name=hevc|width=3840|height=2160|r_frame_rate=3125/64|nb_frames=1544|duration=31.621...
#  stream|codec_name=aac|...|duration=31.62...                 # the same length: the same frames
```

## Pitfalls

- **The 128 editor drops keys right after Enter** (it is entering the line): wait ~40 frames after each Enter, or
  the next line's number is lost and the line runs as a direct command.
- **The Pentagon 128 boot menu starts on Tape Loader**: 128 BASIC is one Down.
- **`picture_height` needs the GPU** (`gpu: true`); the software screen records at the window's size.
- **Switching `gpu` while recording** re-creates the screen the recording draws from: set the display first.
- **A frame does not fit** (`width` / `height` smaller than picture × scale): 409 with the sizes; lower `scale` or
  `picture_height`.
- **Speed is the host's**: each frame waits for the window to draw it and ffmpeg to take it (measured on a busy
  M-series Mac: 5..20 fps at 1406x1828, ~1.3 fps at 4K under a load of 300); the file plays at the machine's rate regardless.
- **Take control for the whole demo** (`POST /control/session`, MCP `control_execution take_control`) and release it
  after. Sessionless calls open an implicit session per client; another session holding control answers 409
  `held_by_other` to `record/frames`.
- **The file is the window's picture**: resize the window (or use `picture_height`) before starting, not during.
