# Screenshotter: one screenshot that always shows the screen

Status: design draft, decisions 1-7 taken by the owner (2026-10-03), no open questions (see the end).

## The problem in one paragraph

Ask the emulator for a screenshot over the WebAPI, MCP, the CLI or Python and you get a
**crop** of the picture, not the picture. The crop is a fixed 256x192 window cut out of the
frame ("the Spectrum screen"). That is right for a 48K and wrong for almost everything else:
on a TS-Conf with the FT812 card (VDAC2) the picture is e.g. 1024x768 and the default
screenshot returns 256x192 from the middle of it. Only `mode=full` returns the whole frame,
and nobody finds out. On top of that, the screenshot reads a different buffer than the one the
user sees on screen, the crop code exists twice (screenshots and video recording, with
different fallbacks), and nothing tests any of it.

## Glossary

- **Frame buffer**: the array of pixels (RGBA, 4 bytes each) the renderer draws one picture into.
  Border included.
- **Raster descriptor** (`RasterDescriptor`, `core/src/emulator/video/screen.h`): a static table,
  one row per video mode, that says how big the whole frame is and where the working picture
  ("screen window": the paper on a Spectrum, the graphics area on others) sits inside it.
- **Live frame**: the buffer the emulation thread is drawing right now. Can be half drawn.
- **Presented frame**: a finished, tear-free copy made at the end of each frame, shown to the user
  (the Qt window, the video wall). Lags the live frame by a couple of frames, more with ZX DLSS.
- **External picture**: a picture that does not come from the machine's own renderer. Today: the
  FT812 graphics card on the TS-Conf IDE connector. It has its own size and no row in the table.

## Decisions taken

1. **Source: the presented frame.** A screenshot shows what the user sees: complete, no tearing,
   with ZX DLSS and other post-processing applied. The price is the lag, which is handled in
   "Pause and single-step" below, not by reading the live buffer.
2. **Default area: everything; `area=screen` on request.** The default is the whole frame, border
   included (border effects are content, demos draw in it). The screen window is returned as
   **data next to the pixels** (rectangle inside the frame), and `area=screen` cuts it out. On a
   hires mode the screen window can be the whole frame, and then `full` and `screen` are the same
   picture: that is fine, no special handling. One rule for every machine, including FT812: no
   special case. No arbitrary-rectangle option (considered and dropped as excess).

3. **`screen` means the working picture of the machine**: the paper on a Spectrum, the graphics
   window on TS-Conf (from the V_CONFIG-derived state), the whole picture on FT812. The table is
   fixed to say so (it is wrong today for TS-Conf and P384); the borderless picture is what a
   caller gets from `area=screen`.

4. **ZX-Poly: the screenshot is the frame the user sees** (the group's composed 2x display frame,
   with its own geometry and `source: composed`), not the master's native 256x192 downsample.

5. **`source=live` exists** next to the default `source=presented`, for cycle-accurate
   debugging (the frame as drawn up to now, no present delay, no post-processing). It never reads
   the live buffer from the API thread (see "Live source" below).

6. **Default format: PNG.** Lossless, full colour. GIF (256 colors) stays available as an explicit
   `format=gif`; callers that relied on the GIF default get PNG and are updated in phase 4.

7. **Index frames (`format=index`, Sprinter pens) stay only on `/capture/framebuffer`** (and the
   `framebuffer` call of every module). The screenshotter produces pictures (PNG, opt-in GIF).

## What the survey found (facts, file:line from the survey; re-verify when implementing)

All surfaces except Qt and Lua share one function, `ScreenCapture::captureScreen`
(`core/src/emulator/video/screencapture.cpp`):

| Surface | Reads | Default today |
|---|---|---|
| WebAPI `GET /capture/screen` | live buffer | GIF, crop to descriptor screen window (256x192 fallback) |
| MCP `capture_media screenshot` | same, through the WebAPI | same; schema says "256x192" |
| CLI `capture screen` | same | same; prints a base64 URI; always the *selected* emulator |
| Python `capture_screen` | same | same |
| WebAPI `GET /capture/framebuffer` (+ MCP/CLI/Lua/Python `framebuffer`) | presented frame | whole frame, raw RGBA |
| Qt "Take Screenshot" | presented frame | viewport crop, **clipboard only** |
| Video recording | live buffer at frame end | whole frame, optional descriptor crop (own copy of the crop code) |

Problems that this design removes:

1. Default crop is wrong outside 48K-class machines. Sprinter is 640x256, ATM hires 640x200,
   Profi hires 512x240; the docs and the MCP schema still say 256x192.
2. FT812: the external picture has `videoMode = M_NUL`, whose table row is all zeros, so the
   crop falls back to 256x192 from the centre (and underflows when the picture is smaller).
   Recording takes the whole picture, so recording and screenshot disagree.
3. Screenshot reads the **live** buffer without a lock: it can be half drawn, never contains
   DLSS output, and a mode switch that reallocates the buffer frees memory under the reader
   (use-after-free risk, found by reading the code, not reproduced).
4. The table does not describe everything the renderer produces:
   TS-Conf (descriptor says the screen window is the whole 720x288, the real graphics window
   is set by a register); ZX-Poly (the user sees a 2x frame, the screenshot gets the 256x192
   downsample of it). The survey also suspected P384 (paper at y=64 against the table's 48);
   **measured 2026-10-03: the table is right** (paper at (48,48) in the 384x304 frame), so there is
   nothing to fix there. The Qt viewport presets assume y=56, which does not match: noted, out of
   scope here.
5. `mode` accepts only the word `full`; any other value silently means "crop".
   Any `format` except `png` silently becomes GIF (256 colors, lossy for true-colour pictures),
   written through a fixed `/tmp/unreal_capture_<address>.gif` name.
6. Every failure is HTTP 500, including "emulator not found".
7. Two copies of the crop code, no tests for the crop, FT812, the WebAPI endpoint, MCP, CLI or
   Python.

## Design

### Principle

A screenshot is a **pure function of one frame snapshot**: pixels and geometry taken together,
atomically, from the same frame. Everything else (crop, encode, transport) is derived from the
snapshot and knows nothing about machines.

### 1. `FrameSnapshot`: pixels and geometry, always a pair

```
struct PictureGeometry {
    uint32_t width, height;          // the buffer, pixels
    uint32_t stride;                 // bytes per line
    PixelFormat format;              // RGBA8888 today; index (u16 pens) for Sprinter
    Rect screenWindow;               // the working picture inside the frame (x, y, w, h)
    VideoMode mode;                  // for humans and tests ("ZX48", "TS-256", "FT812", ...)
    FrameSource source;              // Native | External | Composed (ZX-Poly)
    uint64_t frameNumber;            // which emulated frame this is
};
struct FrameSnapshot { std::vector<uint8_t> pixels; PictureGeometry geometry; };
```

`Screen::SnapshotPresented(FrameSnapshot&)` copies the presented slot **and** its geometry under
the same mutex that already guards `CopyPresentedFramebuffer`. The geometry is latched with the
frame (in `LatchFramebuffer` / `LatchExternalFrame`), so a mode switch or a resize between the
copy and the read cannot produce a width/height that disagrees with the pixels. This removes
problem 3 for screenshots by construction.

### 2. The raster descriptor becomes the truth, once

The geometry is computed in **one** place, at latch time, by one function
`Screen::DescribeFrame(...)`, from three kinds of source:

- **Native**: the table row for the current mode, except where the window moves: TS-Conf's
  graphics window comes from V_CONFIG (a virtual `Screen::WorkingWindow()`); every other mode is
  checked against its renderer by a test that paints a marker and finds it.
- **External (FT812)**: size from the picture itself; the screen window is the whole picture.
  No more `M_NUL` row of zeros: the missing descriptor is replaced by data the picture owns.
- **Composed (ZX-Poly)**: the display frame the user sees (2x), with its own window.

Recording's crop and the Qt viewport read the same geometry, so the second copy of the crop code
disappears (a follow-up phase, listed below, not a prerequisite).

### 3. `Screenshotter`: one class, no machine knowledge

```
Result Screenshotter::Take(emulator, Options) ;
Options { Area area = Full;          // Full | Screen
          Format format = Png;       // Png | Gif (opt-in, lossy) ; Raw handled by /capture/framebuffer
          Source source = Presented; // Presented | Live
          std::optional<Path> saveTo; }
Result  { std::vector<uint8_t> bytes; PictureGeometry geometry /* of what was returned */;
          Rect cropInFrame; std::string error; ErrorKind kind; }
```

- `Area::Full` returns the snapshot as is. `Area::Screen` cuts `geometry.screenWindow`.
  A window that does not fit inside the frame is an **error with a reason** (it names the frame
  size), never a silent centred 256x192.
- Encoders: PNG (default), GIF (opt-in; unique temp name from `GetUniqueScratchPath`-style helper
  or in-memory, no fixed `/tmp` name). Unknown format or area is an error, not a silent GIF.
- Error kinds map to HTTP statuses: not found 404, bad parameter 400, no frame yet 409/503,
  encode/IO failure 500.
- `ScreenCapture::captureScreen` becomes a thin shim over `Screenshotter` until all callers move.

### 4. Every surface returns the same thing

The response always carries the geometry, so a caller never has to guess what it got:

```
{ "format":"png", "width":1024, "height":768, "area":"full",   // or "screen"
  "frame":{"width":1024,"height":768,"mode":"FT812","source":"external","frame_number":4711},
  "screen_window":{"x":0,"y":0,"w":1024,"h":768}, "crop":null, "data":"<base64>" }
```

Surfaces (feature parity rule: all of them, each with its docs):

- **WebAPI** `GET /capture/screen`: params `area=full|screen` (default `full`), `format=png|gif`
  (default `png`), `path`/`filename`; strict validation; OpenAPI
  (`openapi_capture.inc`) updated with the real meaning of each value. `/capture/framebuffer`
  keeps raw RGBA/index and gains the same geometry headers/fields.
- **MCP** `capture_media screenshot`: same parameters, schema text fixed, `path` URL-encoded.
- **CLI** `capture screen [--area=...] [--format=...] [file]`: honours the session's emulator,
  can save to a file.
- **Lua**: `screenshot{area=,format=}` added (today only `framebuffer`).
- **Python**: `capture_screen(area="full", format="png")`; `full=` kept as a deprecated alias.
- **Qt**: "Take Screenshot" saves a file as well as the clipboard, whole frame by default
  (viewport crop becomes an explicit option), same `Screenshotter`.

### 5. Pause and single-step (the price of decision 1)

The presented frame lags. For an agent that pauses, steps one frame and takes a screenshot this
would be a trap. Rule: **whenever emulation stops (pause, single step, breakpoint hit, TTD seek)
the current frame is published immediately**, as TTD seek already does through
`FlushAndPresentFramebuffer`. Then "screenshot after stop" shows the frame where emulation
stopped. Needs a test per stop reason. If the stop is mid-frame, the published frame is the
partly drawn one, and `geometry.frameNumber` plus a `partial:true` flag say so;
`source=live` gives the same partial frame plus the beam position.

### 6. Live source (`source=live`)

The live buffer belongs to the emulation thread, and today's screenshot reads it from an API
thread with no lock (problem 3). The live source never does that:

- **Emulation stopped or paused** (including a debugger stop in the middle of a frame): the
  emulation thread is idle, so the live buffer is copied directly, together with its geometry.
  Pixels the beam has not reached yet still hold the previous frame; the result says so:
  `partial: true` and the beam position (`line`, `t`) next to `frame_number`.
- **Emulation running**: the API thread posts a one-shot request; the emulation thread serves it at
  the next frame end, right before the frame is latched, and wakes the caller. No lock on the hot
  path, only one atomic flag read per frame. The result is the finished current frame: no present
  delay (2 frames, up to 7 with ZX DLSS) and no post-processing. A mid-frame snapshot of a running
  machine is not offered: it would not mean anything.
- A mode switch or resize cannot tear the result: the request is served by the thread that makes
  the switch.
- A timeout (emulation stuck, no frames) returns an error kind `no-frame` instead of hanging.

## Phases

1. **Foundation**: `PictureGeometry`, `SnapshotPresented` with geometry latched per present slot,
   the TS-Conf window; tests that find a marker pattern in every video mode (landed for the ZX family,
   P384, TS-Conf and the FT812 picture; ATM, Profi, Sprinter and ZX-Poly markers still to add).
2. **`Screenshotter`** with PNG/GIF encoders, error kinds, `ScreenCapture` shim; unit tests for
   crop, FT812 (smaller and larger than 256x192), error cases, mode switch during capture.
3. **Surfaces**: WebAPI + OpenAPI, MCP, CLI, Lua, Python, Qt; endpoint tests (none exist today).
4. **Docs and recipes** (the full checklist is in "Documentation deliverables"): `.recipe/media/agent-screenshot-view.md` and the ~15 recipes and design
   docs that quote "256x192" or `mode=full` (list in the survey; each reviewed, not search and
   replaced); scripts that call `/capture/screen` with no parameters (they hash the result, so
   their expected values change).
5. **Recording and viewport**: move the recording crop and the Qt viewport onto the same geometry.

## Tests (non-negotiables from `core/tests/README.md` apply)

- Per video mode and per machine: render a known marker pattern, take a screenshot, assert size,
  window rectangle and that the marker is where the geometry says.
- FT812 smaller and larger than 256x192; resize between two screenshots.
- Pause, step, breakpoint, TTD seek: screenshot equals the frame at the stop.
- ZX DLSS on: screenshot equals the presented (processed) frame.
- Mode switch while a screenshot is taken on another thread (sanitizer run).
- Parameters: bad `area`/`format` give 400 with a reason (a screen window that does not fit
  the frame is an error naming the frame size); missing emulator gives 404; on a hires mode where the
  window is the whole frame, `full` and `screen` return identical pixels.
- Every surface returns identical pixels and geometry for the same frame.

## Compatibility

Callers that relied on the 256x192 default get the whole frame plus the window rectangle.
`area=screen` restores the old picture for machines whose window was right. Scripts under
`tools/` that hash `/capture/screen` output need new expected values, and are updated in
phase 4.

## Documentation deliverables (part of "done", checked per phase)

Each item is updated in the same change as the code it describes, never later:

- **Command reference** (`docs/emulator/design/control-interfaces/command-interface.md`): the
  capture/screenshot rows of every surface table (line ~761 and the planned `screenshot` /
  `capture screen` rows near lines ~3381 and ~3654, which today describe 320x240, 256x192 and
  page 5/7 and are stale); the `area`, `source`, `format` parameters, the response fields
  (frame, screen window, crop, partial, beam position), error kinds, and the pause/step rule.
- **Per-module documentation**, each with the new parameters, defaults, response shape and an
  example:
  - WebAPI: `docs/emulator/design/control-interfaces/webapi-interface.md`, and
    `core/automation/webapi/OPENAPI_MAINTENANCE.md` if the maintenance rules change;
  - CLI: `core/automation/cli/README.md`, `docs/emulator/design/control-interfaces/cli-interface.md`
    and the `capture` help text in code;
  - MCP: `core/automation/mcp/README.md`, `docs/features/mcp/README.md`, and the tool schema text
    in `mcp-media.cpp`;
  - Lua: `docs/emulator/design/control-interfaces/lua-interface.md` (new `screenshot` call);
  - Python: `docs/emulator/design/control-interfaces/python-interface.md` and the docstring of
    `capture_screen` (the `full=` alias documented as deprecated);
  - the other modules (`gdb`, `dezog`, `zesarux`) are checked for capture commands; if none
    exist, the doc says nothing and the check is recorded in `TODO.md`.
- **OpenAPI manifest** (`core/automation/webapi/src/openapi/openapi_capture.inc`): `/capture/screen`
  gets `area`, `source`, `format` enums with the real meaning of each value and their defaults,
  the new response schema, the 400/404/409/503 error responses; `/capture/framebuffer` gets the
  geometry fields. Validated against `core/automation/webapi/OPENAPI_MAINTENANCE.md` and its
  `AGENTS.md` (every parameter described, examples present, no stale "256x192").
- **Recipes** (`.recipe/`): the primary `.recipe/media/agent-screenshot-view.md` rewritten around
  the new model (presented vs live, full vs screen, geometry in the answer), plus every recipe
  that quotes the old default (list in phase 4).
- **Design docs** that still say "Take Screenshot [TODO]" or describe the old crop
  (`docs/emulator/design/video/screen-rendering-pipeline.md`, the Qt menu docs, the recording
  design) are corrected.
- After editing: `python3 tools/fix-absolute-paths.py` dry run is clean and every link resolves.

## Open questions

None at the moment: all questions raised by the survey were decided (decisions 1-7). New ones go
here, one at a time.
