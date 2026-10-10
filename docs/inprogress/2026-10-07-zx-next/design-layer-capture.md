# ZX Spectrum Next: per-layer capture and the pixel inspector

**Date:** 2026-10-10 (rev. 2: one-frame buffers, always-on if the overhead is within 5 %) · part of [README.md](README.md) · follows
[design-automation-coverage.md](design-automation-coverage.md) (open question 1; the one item of phase B that was left out) · renderer:
`core/src/emulator/video/next/nextvideorenderer.{h,cpp}`, `screennext.cpp`

The composed frame is all the automation planes can capture today. The questions a Next developer asks are per layer: *"is this tile from the
tilemap or from Layer 2?"*, *"why is my sprite hidden - behind which layer?"*, *"what does the ULA look like without the tilemap on top?"*. This
document designs the capture that answers them **exactly as the frame was drawn**, copper effects included, with buffers for **one frame only**
and - if it costs no more than 5 % - **on by default**.

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
So the capture is taken **while the frame is drawn**, and because it is cheap enough to be always there, a request just reads the last frame.

## 2. Requirements

| # | Requirement |
|:--|:--|
| R1 | Exact: the per-layer images of a captured frame are what the compositor was given: per segment, with the registers of that segment |
| R2 | Layers: `ula` (with the border and LoRes), `layer2`, `sprites`, `tilemap`, plus `composed` and `ids` (which layer won each pixel) |
| R3 | Transparent pixels are transparent: PNG with alpha (alpha 0 where the layer's pixel is not opaque or is the global transparency colour), `format=rgba` for raw bytes |
| R4 | **One frame of buffers**, two sets that swap at the frame end (the one being drawn and the last completed one): a request reads the last completed frame, never a half-drawn one, with no waiting and no locking of the renderer |
| R5 | **Always on if the measured overhead is within 5 %** of the frame cost (section 5, phase L0); then the setting is `on` by default and can be switched off. If it is not, the default is `off` and a request **arms** the capture for the next frame (the v1 behaviour, section 3.4). Either way the surfaces and answers are the same |
| R6 | Same geometry as `capture/screen` (the framebuffer: 640 sub-pixels x 256 grid lines, lines doubled) so a layer image overlays the composed screenshot pixel for pixel |
| R7 | **Every plane**: WebAPI + OpenAPI, MCP (`capture_media`), CLI, Lua, Python for the capture, the pixel inspector **and the switch**; the report `state/next/video` shows `layer_capture {enabled, frames, last_frame, bytes}` |
| R8 | Docs and tests as in [design-automation-coverage.md](design-automation-coverage.md) section 7, plus a recipe "which layer draws this pixel" in `.recipe/machines/next.md`; the switch's ini key and environment variable (if any) in `docs/emulator/environment-variables.md` |

## 3. Design

### 3.1 Core: a compact sink the renderer fills

The cost is the memory traffic of recording, so the record is **compact** and the conversion to RGBA happens on the reader's thread, when a request asks for an image:

```cpp
struct NextLayerFrame            // two of them in ScreenNext: `draw` and `last`; swapped at the frame end (a pointer swap)
{
    // 16 bits per pixel per layer: bits 0-8 the 9-bit colour, bit 9 Layer 2 priority, bit 10 opaque, bit 11 border (ULA),
    // bit 12 tilemap 'below', bit 13 text-mode tile (the fields of Pixel)
    std::vector<uint16_t> layer[4];   // ula, layer2, tilemap, sprites; kWidth * kHeight (grid lines, not doubled)
    std::vector<uint8_t>  ids;        // winner per pixel: 0 fallback, 1 ula, 2 border, 3 layer2, 4 tilemap, 5 sprite, 6 blend (modes 110/111), 7 stencil
    std::vector<uint8_t>  info;       // flags: sprite relative / winning sprite index low bits, tile index low bits, ...
    // + the composed row is the frame the screen already has (the framebuffer), not copied again
};
```

9 bytes per pixel (4 x 2 + 1), 640 x 256 pixels = **1.5 MB per set, 3 MB for the two** (against 3.4 MB for one set of RGBA planes in rev. 1). The
`Pixel` rows are already in registers / the line's stack arrays in `RenderLine`; the record is a straight 16-bit pack and a store.

`NextVideoInputs` gets one member, `NextLayerLine* layerOut`. When it is non-null `RenderLine` stores, right where it already holds them, the four
`Pixel` rows (`ula`, `layer2`, `tiles`, `sprites`, after `ApplyUlaClip` and the LoRes replacement) packed, and the compositor's decision per pixel into
`ids` and `info` (the compositor already computes `ue`, `lv`, `sv`, `tv`, `l2Priority`, `borderException` per pixel - recording the winner is one store inside the existing branches).

`ScreenNext::RenderLinesUpTo` owns the sets. Its `draw(toX)` lambda already does `memcpy(row + fromX*2, segment + fromX*2, ...)` for the composed row; the
sink writes the **same x-range** of each plane (the renderer fills a line buffer for the segment, the caller copies the range - one more `memcpy` per plane per segment). Segments
are therefore handled exactly like the composed row, which is what makes R1 hold. At the frame end the pointers `draw` and `last` swap and `last_frame` is set.

The reader converts `last` to RGBA / PNG with the 9-bit palette conversion (`Rgba(colour9)`) on its own thread; the swap guarantees the set it reads is not written again until
**two** frames later - a reader that needs longer takes a copy first (the conversion is a one-pass copy anyway; the planes are read under a `shared_ptr` so a slow reader keeps its set alive and the renderer allocates a fresh one).

### 3.2 The overhead budget (R5)

Recording adds, per frame, ~1.5 MB of sequential stores plus the per-pixel winner store. The frame cost it is compared with is the whole `RenderLinesUpTo` of a typical Next frame
(measured, section 5, phase L0). The decision rule is written down before the measurement: **recording on costs <= 5 % of the frame-render time and of the full emulated-frame time
of `BM_NextFrame` (a program with all layers on) -> default `on`; otherwise default `off`.** The measurement is interleaved A/B rounds (sink off / sink on, many rounds, no waiting for quiet load), median and spread reported.

If the first cut is over budget, in this order: (1) compact records as above (already in); (2) record per **segment** only the layers whose inputs changed since the previous segment and
line (most lines record nothing new: a plane row is copied from the line above when no register / palette / sprite input changed - the renderer knows `refresh()` changes); (3) write `ids` only (the winner) and recompute the layer images lazily from `ids` + the RAM state - rejected
as soon as the copper makes it inexact, so only for lines without copper writes; (4) otherwise default `off` and armed mode.

### 3.3 Settings and planes

The switch: ini `[NEXT] LayerCapture = on|off` (default by the measurement), runtime `POST /api/v1/emulator/{id}/next/layer-capture {"enabled":true}` on every plane, the Qt machine menu later.

| Plane | Calls |
|:--|:--|
| WebAPI | `GET /api/v1/emulator/{id}/capture/screen?layer=composed\|ula\|layer2\|sprites\|tilemap\|ids` (PNG, base64 JSON like today; `&format=rgba` raw; `&alpha=false` flattens transparent pixels onto the fallback colour); `GET .../capture/next-layers` all layers of the same frame in one JSON; `GET .../state/next/pixel?x=&y=`; `POST .../next/layer-capture` |
| OpenAPI | the parameters (`layer` enum) on `capture/screen`, the new paths, response schemas (`NextPixel`, `NextLayers`) in `openapi_next.inc` / `openapi_next_schemas.inc` |
| MCP | `capture_media` action `screen` with `layer`; `inspect_state` aspect `next_pixel` (`nr_x`, `nr_y`); `invoke_api` for the POST |
| CLI | `capture screen layer=tilemap [file=...]`; `state next pixel <x> <y>`; `next layer-capture on\|off` |
| Lua | `capture_screen_layer("tilemap")` (table: width, height, rgba string), `next_pixel(x, y)`, `next_layer_capture_control({enabled=true})` |
| Python | `emu.capture_screen_layer("tilemap")`, `emu.next_pixel(x, y)`, `emu.next_layer_capture_control(enabled=True)` |

### 3.4 Armed mode (the fallback when the default is off)

With the switch off a request **arms** a capture: the screen allocates one `draw` set at the next frame start (`BeginFrame`), records that frame, and the request is answered when the frame completes (a condition variable
with a 500 ms x frame-multiple timeout, 503). Then the sink pointer is cleared again. A paused machine has no next frame: the request is answered from the last captured frame if there is one, else 409 "resume or step a frame". Always-on mode has none of
this - the last completed frame is always there, a paused machine included.

### 3.5 The pixel inspector

`next_pixel(x, y)` (x 0-319 pixel, 0-255 line; sub-pixel `sx` optional) answers from `last`: `winner` (`ula|border|layer2|tilemap|sprite|blend|fallback`), `colour` (9-bit and RGB), and per layer
`{opaque, rgb9, why}` - why a layer lost: *"transparent (index 15)"*, *"below the ULA (tile attr bit 0)"*, *"clipped (NR #1A window)"*, *"layer order LSU: sprites above"*; plus the **source address** of the pixel byte where it
is known (ULA bitmap / attribute byte in bank 5, tile number and definition byte in the tilemap bank, Layer 2 byte, sprite number and pattern byte) - the step from "wrong colour" to "which memory". The source address is
recomputed by the inspector from the pixel position and the register state the line used (stored per line in `info`'s line table: the copper-relevant registers of each segment, a few bytes per segment, not per pixel).

## 4. Tests (written first)

| Test | What |
|:--|:--|
| `NextLayerCapture_Test.IdsExplainTheComposedFrame` | for each of the six layer orders, stencil, blend modes 110/111, ULA clip, tilemap-below and the border exception: for every pixel `composed[x] == layer[ids[x]][x]` (blend and stencil pixels are checked against their own formula), so the capture and the compositor cannot disagree |
| `NextLayerCapture_Test.CopperChangesAreCapturedPerSegment` | a copper list that changes NR #15 (layer order) and the palette in the middle of a line and between lines: the layer planes and `ids` change at the same x / line as the composed frame; a re-render from the end-of-frame registers is **not** equal (the case that rules out the "render again" design) |
| `NextLayerCapture_Test.TransparencyIsAlpha` | tilemap index 15, Layer 2 / ULA global transparency, sprite transparency index: alpha 0 in the layer image, 255 elsewhere |
| `NextLayerCapture_Test.TwoSetsSwapAtTheFrameEnd` | a reader taking the last frame while the next one is drawn sees all of frame N and none of N+1 (two frames with different content; a stress test with a reader thread); a slow reader keeps its set |
| `NextLayerCapture_Test.PausedMachineAnswersFromTheLastFrame` | always-on: after pause the request returns the last drawn frame; switched off: 409 until a frame is stepped; armed mode times out with 503 on a stalled machine |
| `NextLayerCapture_Test.OffCostsNothing` | `layerOut == nullptr`: the renderer suite's frames are byte-identical to before; no allocation |
| benchmark `BM_NextFrame_LayerCapture` | the gate of section 3.2: interleaved A/B of sink off / on over `BM_NextFrame` (all layers, a copper list, sprites) and of the bare `RenderLinesUpTo`; median, spread, overhead % recorded in this document; **<= 5 % -> default on** |
| `NextPixel_Test` | `winner`, `why` and the source address for each layer on a built-up frame (ULA byte, tile, Layer 2, sprite) |
| surfaces | `tools/verification/webapi/src/test_api_next.py`: the routes, `layer` values, PNG alpha, 400 for an unknown layer, 409 on a non-Next machine, the switch; Lua and Python bindings through `exec_lua` / `exec_python` (verification tests, not core tests: the interpreters are not linked into `core-tests`); the OpenAPI route test of `core/tests/automation/webapi/openapi-next-routes_test.cpp` picks the new paths up |
| golden | one golden `.txt` for `next_pixel` and the `ids` legend; images are checked by hash of a fixed synthetic frame |

## 5. Phases

| Phase | Content | Exit |
|:--|:--|:--|
| **L0** | spike: the sink in `RenderLine` / `ScreenNext` (compact planes, two sets, the swap), the benchmark; **measure** and write the numbers into this document | overhead number; the default (on / off) decided by the 5 % rule; nothing else started until then |
| L1 | the real sink with `ids`, `info`, the line table; oracle, copper, transparency, swap, off-cost tests | core tests; no regression in the renderer suite |
| L2 | the settings (ini + runtime), the surfaces: `capture/screen?layer=`, `capture/next-layers`, `next/layer-capture`, OpenAPI, MCP, CLI, Lua, Python, the `video` report field; armed mode if the default is off; docs and recipe | verification pytest green against a live app; full `core-tests`; gcc-16 -O3 clean; `environment-variables.md` row if an env switch is added |
| L3 | `next_pixel` with `why` and source addresses | tests above |
| L4 | Qt: per-layer view and layer toggles (the jnext per-layer screenshots are the model), a pixel inspector under the mouse, the machine-menu switch | later (N12) |

## 6. Open questions

1. `ids` vocabulary for the blend / stencil modes: one `blend` id, or an id per source pair? Proposal: one id, the formula's inputs in `info`.
2. Layer 2 is drawn in three resolutions; the layer image is always the 640-wide grid (doubling the 256-wide mode), like the screen. A raw-resolution option is cheap to add later.
3. Sprites: one image of all sprites composed with each other, or per-sprite attribution? Proposal: composed sprites in the image, the winning sprite number in `info`.
4. The 5 % budget: of the frame-render time, or of the emulated frame including the CPU? Proposal: both are measured and reported; the gate is the larger (stricter) of the two. Say so if the owner prefers one.
5. Memory: 3 MB for two sets is fixed; with the shared-set rule for slow readers it can briefly be 4.5 MB. Acceptable? (An embedded / Steam Deck build may want the switch off.)
6. Capture while the TTD replays: the replay renders through the same screen path; proposal: yes, nothing to add.
