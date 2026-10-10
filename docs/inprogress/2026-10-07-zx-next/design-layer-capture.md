# ZX Spectrum Next: per-layer capture and the pixel inspector

**Date:** 2026-10-10 · part of [README.md](README.md) · follows [design-automation-coverage.md](design-automation-coverage.md) (open question 1; the one
item of phase B that was left out) · renderer: `core/src/emulator/video/next/nextvideorenderer.{h,cpp}`, `screennext.cpp`

The composed frame is all the automation planes can capture today. The questions a Next developer asks are per layer: *"is this tile from the
tilemap or from Layer 2?"*, *"why is my sprite hidden - behind which layer?"*, *"what does the ULA look like without the tilemap on top?"*. This
document designs the capture that answers them **exactly as the frame was drawn**, copper effects included.

## 1. Why not "render again from the current registers"

The first idea - an on-demand call that re-runs the layer renderers from the register file when the request arrives - is wrong for the Next,
and the reason is visible in `ScreenNext::RenderLinesUpTo`:

- the picture is drawn **line by line, and inside a line in segments**: the copper writes registers at 28 MHz resolution and
  `copper.SetBeforeWrite` flushes the pixels drawn so far with the state *before* the write (`draw(toX)`), then the rest of the line is drawn with the
  new state;
- port writes (`#FF`, `#7FFD`, the palette and tilemap registers) and the 8K MMU slots change the picture the same way;
- `RenderLine(in, y, out)` is a pure function of the `NextVideoInputs` snapshot (`nr[256]`, `portFf`, `shadowScreen`, the RAM) - the
  snapshot at the end of the frame is not the snapshot the line was drawn with.

A re-render from "now" would show every copper bar, split screen and palette trick wrong - precisely the cases where a developer needs the tool.
So the capture has to be taken **while the frame is drawn**.

## 2. Requirements

| # | Requirement |
|:--|:--|
| R1 | Exact: the per-layer images of a captured frame are what the compositor was given: per segment, with the registers of that segment |
| R2 | Layers: `ula` (with the border and LoRes), `layer2`, `sprites`, `tilemap`, plus `composed` and `ids` (which layer won each pixel) |
| R3 | Transparent pixels are transparent: PNG with alpha (alpha 0 where the layer's pixel is not opaque or is the global transparency colour), `format=rgba` for raw bytes |
| R4 | Off by default; when off, the cost on the render path is one predictable pointer test per `RenderLine` call and per segment; no allocation, no extra copies |
| R5 | A request covers exactly one frame: armed at the next frame start, delivered when that frame is complete (no half-frame tearing); the request times out (500 ms x the frame time multiple, 503) when no frame completes (a paused machine: the capture of a paused machine returns the **last drawn** frame if it was captured, else 409) |
| R6 | Same geometry as `capture/screen` (the framebuffer: 640 sub-pixels x 256 grid lines, lines doubled) so a layer image overlays the composed screenshot pixel for pixel |
| R7 | **Every plane**: WebAPI + OpenAPI, MCP (`capture_media`), CLI, Lua, Python; the pixel inspector on the same planes |
| R8 | Docs and tests as in [design-automation-coverage.md](design-automation-coverage.md) section 7, plus a recipe "which layer draws this pixel" in `.recipe/machines/next.md` |

## 3. Design

### 3.1 Core: a sink the renderer fills

```cpp
struct NextLayerFrame            // owned by ScreenNext, allocated when a capture is armed, freed after delivery
{
    static constexpr unsigned kLayers = 5;                // ula, layer2, tilemap, sprites, border-as-ula flag lives in ids
    std::vector<uint32_t> layer[4];                       // RGBA per layer, kWidth * kHeight (grid lines, not doubled)
    std::vector<uint8_t>  ids;                            // winner per pixel: 0 fallback, 1 ula, 2 border, 3 layer2, 4 tilemap, 5 sprite, 6 blend (modes 110/111), 7 stencil
    std::vector<uint32_t> composed;                       // the same frame the screen got
    std::vector<uint8_t>  info;                           // per pixel flags: tilemap 'below', layer-2 priority bit, sprite relative, ...
};
```

`NextVideoInputs` gets one member, `NextLayerLine* layerOut = nullptr`. When it is non-null `RenderLine` copies, right where it already
holds them, the four `Pixel` rows (`ula`, `layer2`, `tiles`, `sprites`, after `ApplyUlaClip` and the LoRes replacement) into the sink as RGBA (`Rgba(colour)`,
alpha 0 when `!visible(p)`) and writes the compositor's decision per pixel into `ids` and `info` (the compositor already computes `ue`, `lv`, `sv`, `tv`, `l2Priority`,
`borderException` per pixel - recording the winner is one store inside the existing branches, executed only when the pointer is set).

`ScreenNext::RenderLinesUpTo` owns the frame buffers. Its `draw(toX)` lambda already does `memcpy(row + fromX*2, segment + fromX*2, ...)` for the composed
row; with the sink armed it passes the line sink to `RenderLine` and **copies the same x-range** of each layer row, `ids` and `info` into the frame buffers.
Segments are therefore handled exactly like the composed row - this is what makes R1 hold.

Cost when armed: five more 640 x 256 x 4 byte planes (~0.65 MB each, 3.4 MB in all) for one frame; when off: nothing (R4).

### 3.2 Arming and delivery (R5)

`Emulator::RunAtCoherentMoment`-style command (the NextREG write already uses it): the API thread posts "arm layer capture" and waits on a condition variable; the
screen sets `armed` at the **start** of the next frame (`BeginFrame`), the last `RenderLinesUpTo` of that frame completes the buffers and notifies. A
paused machine has no next frame: a paused request is answered from the last captured frame if one exists (kept until the next armed capture), else 409 with
"resume or step a frame". Single-step users get a frame by `step_frame` + capture.

### 3.3 Planes

| Plane | Call |
|:--|:--|
| WebAPI | `GET /api/v1/emulator/{id}/capture/screen?layer=composed\|ula\|layer2\|sprites\|tilemap\|ids` (PNG, base64 JSON like today; `&format=rgba` raw; `&alpha=false` flattens transparent pixels onto the fallback colour); `GET .../capture/next-layers` all layers of one frame in one JSON (one frame, consistent); `GET .../state/next/pixel?x=&y=` |
| OpenAPI | the parameters (`layer` enum) on `capture/screen`, the new paths, response schemas (`NextPixel`) in `openapi_next.inc` / `openapi_next_schemas.inc` |
| MCP | `capture_media` action `screen` with `layer`; `inspect_state` aspect `next_pixel` (`nr_x`, `nr_y`) |
| CLI | `capture screen layer=tilemap [file=...]`; `state next pixel <x> <y>` |
| Lua / Python | `capture_screen_layer("tilemap")` (table: width, height, rgba string / bytes), `next_pixel(x, y)` / `emu.capture_screen_layer(...)`, `emu.next_pixel(x, y)` |

### 3.4 The pixel inspector

`next_pixel(x, y)` (x 0-319 pixel, 0-255 line; sub-pixel `sx` optional) arms one capture and answers from it: `winner` (`ula|border|layer2|tilemap|sprite|blend|fallback`), `colour` (9-bit and RGB), and per layer
`{opaque, rgb9, why}` - why a layer lost: *"transparent (index 15)"*, *"below the ULA (tile attr bit 0)"*, *"clipped (NR #1A window)"*, *"layer order LSU: sprites above"*; plus the **source address** of the pixel byte where it
is known (ULA bitmap / attribute byte in bank 5, tile number and definition byte in the tilemap bank, Layer 2 byte, sprite number and pattern byte) - the step from "wrong colour" to "which memory".

## 4. Tests (written first)

| Test | What |
|:--|:--|
| `NextLayerCapture_Test.IdsExplainTheComposedFrame` | for each of the six layer orders, stencil, blend modes 110/111, ULA clip, tilemap-below and the border exception: for every pixel `composed[x] == layer[ids[x]][x]` (blend and stencil pixels are checked against their own formula), so the capture and the compositor cannot disagree |
| `NextLayerCapture_Test.CopperChangesAreCapturedPerSegment` | a copper list that changes NR #15 (layer order) and the palette in the middle of a line and between lines: the layer images and `ids` change at the same x / line as the composed frame; a re-render from the end-of-frame registers is **not** equal (the case that rules out the "render again" design) |
| `NextLayerCapture_Test.TransparencyIsAlpha` | tilemap index 15, Layer 2 / ULA global transparency, sprite transparency index: alpha 0 in the layer image, 255 elsewhere |
| `NextLayerCapture_Test.OffCostsNothing` | `layerOut == nullptr`: the renderer test suite's frames are byte-identical to before; a counting allocator sees no allocation |
| benchmark | A/B per `docs/guidelines/performance-guidelines.md`: `BM_NextFrame` with the sink off against the baseline, +-1 %; armed frame cost reported |
| `NextPixel_Test` | `winner`, `why` and the source address for each layer on a built-up frame (ULA byte, tile, Layer 2, sprite) |
| arming | armed capture waits for the next frame start; two requests in one frame get the same frame; a paused machine answers from the last captured frame or 409 |
| surfaces | `tools/verification/webapi/src/test_api_next.py`: the routes, `layer` values, PNG alpha, 400 for an unknown layer, 409 on a non-Next machine; Lua and Python bindings through `exec_lua` / `exec_python` (verification tests, not core tests: the interpreters are not linked into `core-tests`); the OpenAPI route test of `core/tests/automation/webapi/openapi-next-routes_test.cpp` picks the new paths up |
| golden | one golden `.txt` for `next_pixel` and the `ids` legend; images are checked by hash of a fixed synthetic frame |

## 5. Phases

| Phase | Content | Exit |
|:--|:--|:--|
| L1 | `NextLayerFrame`, the sink in `RenderLine` / `ScreenNext`, `ids` + oracle test, copper-exactness test, off-cost test and benchmark | core tests; no regression in the renderer suite; benchmark within 1 % |
| L2 | arming / delivery, `capture/screen?layer=`, `capture/next-layers`, OpenAPI, MCP, CLI, Lua, Python, docs, recipe | verification pytest green against a live app; full `core-tests`; gcc-16 -O3 clean |
| L3 | `next_pixel` with `why` and source addresses | tests above |
| L4 | Qt: per-layer view and layer toggles (the jnext per-layer screenshots are the model), a pixel inspector under the mouse | later (N12) |

## 6. Open questions

1. `ids` vocabulary for the blend / stencil modes: one `blend` id, or an id per source pair? Proposal: one id, the formula's inputs in `info`.
2. Layer 2 is drawn in three resolutions; the layer image is always the 640-wide grid (doubling the 256-wide mode), like the screen. A raw-resolution option is cheap to add later.
3. Sprites: one image of all sprites composed with each other, or per-sprite attribution in `info`? Proposal: composed sprites in the image, the winning sprite number in `info` for the inspector.
4. 28 MHz / turbo: the frame is the same; nothing special (the renderer is per frame, not per CPU clock).
5. Should the capture be allowed while the TTD is replaying? The replay renders frames through the same screen path; proposal: yes, nothing to add.
