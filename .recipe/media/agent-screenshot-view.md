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

## MCP (preferred)

```text
capture_media {"action":"screenshot","format":"png","mode":"full",
              "filename":"scratch/screen.png"}
#   → structuredContent: {format:"png", width, height, size, saved:true, file:"scratch/screen.png"}
#     no "data" member — the bytes went straight to disk, not through the transcript
```

Then `view_file` on `scratch/screen.png`.

- `format` defaults to **gif** — pass `"png"` explicitly for lossless stills.
- `mode:"full"` includes the border (320x240 / 352x288 depending on timing
  mode); the default crops to the active screen area (256x192).
- `filename` (alias `path`) resolves on the **emulator host**, relative to
  the server process working directory — not the agent's CWD. Missing parent
  directories are created.

## WebAPI

The curl walkthrough below — right choice when a shell/Python pipeline
drives the capture.

### Server-side save (path query parameter)

```bash
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png&mode=full&path=scratch/screen.png" | jq .
#   → {"format":"png","width":320,"height":240,"size":12450,"saved":true,"file":"scratch/screen.png"}
```

`path` (or `filename`) is accepted; when present, the response omits the
base64 `data` member entirely — same behavior as the MCP form above.

### Fallback: fetch base64, decode host-side (older servers)

If the server build predates the `path` parameter, keep base64 out of the
transcript by decoding inside the same shell command:

```bash
curl -s "$BASE/emulator/$EMU_ID/capture/screen?format=png&mode=full" \
  | python3 -c "
import json, sys, base64
obj = json.load(sys.stdin)
with open('scratch/shot.png', 'wb') as f:
    f.write(base64.b64decode(obj['data']))
print('saved', obj.get('width'), obj.get('height'))
"
```

### Metadata only (no pixels at all)

Without `path`, MCP `capture_media` omits the image unless
`include_image:true` — width/height/size come back for cheap existence and
dimension checks with zero image tokens.

## Framebuffer Architecture: GUI Display vs. WebAPI Capture Divergence

When the emulator is paused (e.g. at a breakpoint, instruction step, or manual pause), there is a structural difference between what the Qt GUI window displays and what `capture_media` / `GET /capture/screen` returns:

| Buffer / Aspect | Qt Desktop GUI (`unreal-qt`) | WebAPI / MCP Capture (`capture_media`) |
| :--- | :--- | :--- |
| **Buffer Source** | **Latched Presentation Buffer** (`_presentSlots` via `CopyPresentedFramebuffer()`). | **Live Active Framebuffer** (`_framebuffer.memoryBuffer` via `GetFramebuffer()`). |
| **Update Cycle** | Updated at V-SYNC (`MainLoop::OnFrameEnd()`) via `LatchFramebuffer()`. | Live work-in-progress raster buffer, updated continuously per CPU instruction step. |
| **Mid-Frame Pause** | Shows the **completed frame from the previous V-SYNC**. | Shows **mid-frame beam progress** up to the paused T-state (top half updated, bottom half from previous frame). |
| **Post-Processing** | Applies **Temporal Blending** (`_frameHistory` for 50 Hz Gigascreen flicker), **CRT Scanlines** (`_crtFilter`), and **Viewport Cropping** (`_displayViewport`). | Returns **pure raw 1:1 RGBA pixels** directly from the video rasterizer without post-processing or CRT shaders. |

### Diagnostic Guidance

- **Use WebAPI / MCP `capture_media`**: When you need pure raw 1:1 pixel accuracy, or when performing **beam/raster timing profiling** (inspecting mid-frame beam positions during CPU stepping).
- **Use Qt GUI / `grabFramebuffer`**: When inspecting post-processed composite rendering (CRT effects, temporal Gigascreen blending, or viewport-cropped output).
- **Use TTD Seeking**: When you need a **guaranteed-stable, 100% byte-identical screenshot** at exact frame boundaries without mid-frame beam artifacts ([ttd-visual-inspection.md](../analysis/ttd-visual-inspection.md)).

## Pitfalls

- **Default format is gif** on both transports — always pass `format=png`
  explicitly when the pixels will be compared or viewed as a still.
- **The path is server-side.** Relative paths resolve against the emulator
  process CWD; agents driving a remote emulator must not assume their own
  filesystem layout. Stick to `scratch/` paths.
- **Base64 size math**: a full-border PNG is tens of KB — roughly 4/3 of
  that as base64 characters in your context window. Acceptable once,
  terrible in a polling loop; save to files and compare digests
  (`inspect_state {"aspects":["screen_digest"]}`) instead.
- **A live screenshot is "whatever the machine shows when the call lands"**
  — for a guaranteed-stable image of a specific recorded frame, seek there
  first with TTD:
  [ttd-visual-inspection.md](../analysis/ttd-visual-inspection.md).
