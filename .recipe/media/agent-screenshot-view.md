# Recipe: Viewing a Screenshot as the Agent (not just returning metadata)

Goal: actually **look at** the emulator's screen from inside an agent session
(Claude Code, Antigravity, or other LLM agents) — for visual bug triage,
comparing frames, or confirming a fix rendered correctly.

The trick in every variant below: make the **server** write the PNG to disk,
then open the file with the IDE's image viewer (`view_file`-style tools).
Base64 image data routed through the model's text transcript is slow,
token-expensive and corruption-prone — never do it if a file save exists.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `capture_media` with `filename` saves the binary server-side. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines (the
> base64-decode fallback for older servers) or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

> **Prefer the cheapest report that answers your question, in this order:**
> 1. `screen_digest` (`inspect_state` aspect `screen_digest`) — a hash of
>    screen RAM, for change detection ("did anything change since last
>    frame?") with no pixel data at all.
> 2. `screen_ocr` (`inspect_state` aspect `screen_ocr`) — extracted text,
>    when you need what a text-mode screen says.
> 3. `screen_attributes` (`inspect_state` aspect `screen_attributes`, or
>    `GET /state/screen/attributes`) — per-cell ink/paper/bright/flash
>    decoded from the classic ZX attribute layout (32x24 cells), when you
>    need the color/attribute layout but not actual pixels.
> 4. A screenshot (this recipe) — last resort, when you actually need pixel
>    data (graphics, font rendering, precise layout).

## What a screenshot is

One implementation (the core `Screenshotter`) sits behind every surface
(WebAPI, MCP, CLI, Lua, Python, the Qt window). Design:
[2026-10-03-screenshotter/design.md](../../docs/inprogress/2026-10-03-screenshotter/design.md).

- It takes the **presented frame**: the finished, tear-free frame the Qt
  window shows. It lags the machine by the present delay (2 frames by
  default, more with ZX DLSS). It is not the half-drawn live buffer.
- `area=full` (**default**) is the whole frame including the border:
  352x288 on a Spectrum or Pentagon, 384x304 Pentagon overscan, 736x288
  Sprinter, 720x288 TS-Conf, 1024x768 for the FT812 picture of the
  TS-Conf VDAC2 card.
- `area=screen` is the working picture named by the frame's own geometry:
  the paper (256x192) of a Spectrum, 640x256 Sprinter, 640x200 ATM hires,
  the graphics window of a TS-Conf (512x192, 640x200, 640x240 or 720x288
  pixels, taken from `V_CONFIG`), the whole picture of an FT812 card. On a
  hires mode the working picture can be the whole frame; then `full` and
  `screen` are the same image.
- `format=png` (**default**, lossless) or `gif` (256 colors, only on
  request).
- `mode=full|screen` still works as a **deprecated** alias of `area`.
  Unknown words are HTTP 400 (the message lists the allowed words).

Worked example: on a 48K, `area=full` returns a 352x288 PNG, `area=screen`
returns the 256x192 paper. The answer's `crop` member says where the
returned image sits inside the frame, and `screen_window` where the
working picture sits inside the frame.

### Presented or live

- `source=presented` (default) is the finished frame the window shows, a couple of frames behind the machine
  (more with ZX DLSS). After a pause or a single step the last frames may still be the ones before the stop.
- `source=live` is the frame as drawn **right now**, with no delay and no ZX DLSS processing. On a running
  machine the emulation thread copies it at the end of its next frame (about one frame time; HTTP 409
  `no-frame` if none arrives within 1 s). On a paused machine it is read directly and the answer says where the
  beam stopped: `frame.beam.line`, `frame.beam.tstate`, and `frame.partial` (true: the beam is inside a frame
  drawn per T-state, so the lines it has not reached are still the previous frame's).
- Worked example: pause on a breakpoint in the middle of a frame, then
  `capture_media {"action":"screenshot","source":"live"}`: the summary ends with "beam stopped at line 120 T 26880
  (half-drawn frame)", and the image shows the lines above line 120 as the machine drew them now.

### ZX-Poly

The master of a ZX-Poly group answers with the group's composed display frame, the frame the window shows: 2x
the master's frame (704x576 for a Pentagon), the four modules' paper 512x384 at (96, 96) (`frame.source` is
`composed`, `area=screen` cuts that paper). A slave answers with its own native frame.

### Which call do I want

| I want | Call |
| :--- | :--- |
| To look at the whole picture, border included | `area=full` (the default) |
| Only the paper / working picture (OCR, pixel comparison of the game area) | `area=screen` |
| Smallest file for a still | `format=png` (default) is lossless; `gif` only if 256 colors are enough |
| The same pixels unencoded, to process in code | `GET /capture/framebuffer?format=rgba` (same presented frame, whole) |
| What the Qt window shows, in the clipboard | Tools > Take Screenshot |
| The whole frame as a file from the Qt window | Tools > Save Screenshot As... (PNG or GIF) |
| A frame guaranteed to be a specific recorded moment | Seek with TTD first: [ttd-visual-inspection.md](../analysis/ttd-visual-inspection.md) |

## MCP (preferred)

```text
capture_media {"action":"screenshot","area":"full","format":"png",
              "filename":"scratch/screen.png"}
#   -> structuredContent: {status, format:"png", area:"full", width, height, size,
#        crop:{x,y,width,height}, screen_window:{x,y,width,height},
#        frame:{width,height,mode,source,frame_number}, saved:true, file:"scratch/screen.png"}
#     no "data" member: the bytes went straight to disk, not through the transcript
```

Then `view_file` on `scratch/screen.png`.

- `area` and `format` default to `full` and `png`; the example spells them
  out for clarity.
- `filename` (alias `path`) resolves on the **emulator host**, relative to
  the server process working directory, not the agent's CWD. Missing parent
  directories are created.
- Without a path the result is metadata only; add `include_image:true` to
  get the base64 image.

## WebAPI

The curl walkthrough below is the right choice when a shell/Python pipeline
drives the capture.

### Server-side save (path query parameter)

```bash
curl -s "$BASE/emulator/$EMU_ID/capture/screen?area=full&format=png&path=scratch/screen.png" | jq .
#   -> {"status":"success","format":"png","area":"full","width":352,"height":288,"size":12450,
#       "crop":{...},"screen_window":{...},"frame":{...},"saved":true,"file":"scratch/screen.png"}
```

`path` (or `filename`) is accepted; when present, the response omits the
base64 `data` member entirely, same as the MCP form above.

Errors are JSON `{error, message, kind}`: 400 `bad-parameter` (unknown
`area`/`mode`/`format`, or `area` and `mode` disagree), 404 `not-found`,
409 `no-frame` (nothing presented yet), 500 `bad-geometry` /
`encode-failed` / `io-failed`, 503 shutting down.

### Fallback: fetch base64, decode host-side

To keep base64 out of the transcript without a server-side save, decode
inside the same shell command:

```bash
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png" \
  | python3 -c "
import json, sys, base64
obj = json.load(sys.stdin)
with open('scratch/shot.png', 'wb') as f:
    f.write(base64.b64decode(obj['data']))
print('saved', obj.get('width'), obj.get('height'))
"
```

### Other surfaces

```text
CLI:    capture screen --area=screen --format=png scratch/screen.png   # acts on the session's emulator
Lua:    screenshot{area="full", format="png", path="scratch/screen.png"}
Python: emu.capture_screen(format="png", area="screen", path="scratch/screen.png")
        # full=True/False is deprecated: True = area full, False = area screen
```

Without a file the CLI prints a data URI.

### Metadata only (no pixels at all)

Without `path`, MCP `capture_media` omits the image unless
`include_image:true`: width/height/size come back for cheap existence and
dimension checks with zero image tokens.

## Presented frame vs. what the Qt window draws

Both the window and the screenshot now read the same presented frame, so a
GUI screenshot and `capture/screen?area=full` agree frame for frame (for
the same machine state). What still differs is what the window does on top:

| Aspect | Qt window | Screenshot / `capture_media` / `capture/framebuffer` |
| :--- | :--- | :--- |
| **Source** | Presented frame (`CopyPresentedFramebuffer()`) | The same presented frame |
| **Mid-frame pause** | Completed frame, lagging by the present delay | Same: completed frame, not the beam position |
| **Post-processing** | Temporal blending (Gigascreen flicker), CRT scanlines, viewport cropping | None: 1:1 pixels of the frame |

Tools > Take Screenshot (clipboard) copies what the window shows
(viewport-cropped); Tools > Save Screenshot As... writes the whole frame.

### Diagnostic Guidance

- **Beam/raster timing profiling** (mid-frame beam positions while
  stepping) is **not** visible in a screenshot any more, because it shows
  finished frames. Use the raster/beam state aspects instead.
- **Need a guaranteed-stable, byte-identical image** at an exact frame:
  seek with TTD ([ttd-visual-inspection.md](../analysis/ttd-visual-inspection.md)).

## Pitfalls

- **Defaults changed:** the default is now `png` and `area=full`. Older
  notes and scripts that assumed GIF and a 256x192 crop must pass
  `format=gif&area=screen` to get that result.
- **The path is server-side.** Relative paths resolve against the emulator
  process CWD; agents driving a remote emulator must not assume their own
  filesystem layout. Stick to `scratch/` paths.
- **Base64 size math**: a full-border PNG is tens of KB — roughly 4/3 of
  that as base64 characters in your context window. Acceptable once,
  terrible in a polling loop; save to files and compare digests
  (`inspect_state {"aspects":["screen_digest"]}`) instead.
- **A live screenshot is the presented frame when the call lands** (a
  couple of frames behind the machine), so for a guaranteed-stable image of a specific recorded frame, seek there
  first with TTD:
  [ttd-visual-inspection.md](../analysis/ttd-visual-inspection.md).
