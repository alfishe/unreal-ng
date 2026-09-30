# P0a — Plane B: What the Renderer Drew

**Created:** 2026-09-28
**Status:** implemented (branch `worktree-zxdlss-planeb`), measured
**Requirements:** [R-2, R-24, R-25, R-27](requirements.md) · **Rollout:** [P0a](rollout.md)

## 1. Why

Color mixing decisions need the ZX meaning of every pixel, not only its
color: which bitmap bit it came from and which attribute the beam actually
used. A memory snapshot is not enough - in multicolor parts the attributes
change while the beam draws (on *Across the Edge* a memory-decoded screen
matches the displayed picture only 77–92 % in those parts, 100 % elsewhere).

## 2. What it is

With the feature **`zxdlss`** on, the ZX renderer writes, in the same pass as
each RGBA pixel, one `uint16` into a buffer of the framebuffer's size and
layout (`Screen::GetPlaneB`):

| Bits | Content |
|---|---|
| 0–7 | attribute byte the beam used for this pixel (0 on the border) |
| 8–11 | color index 0–15 (bright × 8 + color) |
| 12 | ink (1) / paper (0) |
| 13–14 | role: 0 not drawn, 1 screen, 2 border |

- Filled by the per-T ZX renderer (`ScreenZX::DrawRangeZXImpl<true>`); other
  video modes and the ScreenHQ-off batch path leave it at 0.
- At the end of a frame the buffer describes the whole frame, border changes
  and multicolor included.

## 3. Zero cost when off

- The buffer is allocated only while the feature is on (`Screen::SetPlaneBEnabled`,
  applied at the next frame start, see §5.1).
- `DrawRangeZX` is a template with two instantiations; `SelectRangeRenderer`
  picks the plain one when plane B is off, so the disabled path runs exactly
  the previous code - no per-pixel check.

## 4. TTD

Plane B is not stored in recordings. `ComposeDisplay` replays the frame
through the renderer for the picture anyway, so plane B is re-rendered in the
same replay; the sandbox saves and restores it with the pixels, and it is
cleared for the static-decode base of the session's first frame. After any
seek or step, plane B describes exactly the displayed position.

## 5. Access

`GET /api/v1/emulator/{id}/capture/planeb` → `application/octet-stream`,
width × height little-endian `uint16`; geometry in `X-PlaneB-Width` /
`X-PlaneB-Height`, format in `X-PlaneB-Format`; `409` while the feature is
off. Documented in `openapi_capture.inc`.

### 5.1 Threads

The live buffer belongs to the thread that renders: the emulation thread, or
a TTD replay (compose, export) while the emulation thread is paused. Two
paths used to touch it from other threads:

| Path | Was | Now |
|---|---|---|
| `zxdlss` toggled (WebAPI / UI thread) | `UpdateFeatureCache` resized or freed the buffer and swapped the renderer while the emulation thread drew into it | `UpdateFeatureCache` stores the wish in an atomic; `InitFrame` applies it at the next frame start on the rendering thread |
| `GET capture/planeb` (WebAPI thread, emulator running) | read the live buffer: a torn frame, or freed memory after a toggle | copies the plane B latched with the presented frame (`CopyPresentedPlaneB`, same slot and present delay as `CopyPresentedFramebuffer`, under `_presentMutex`) |

The latch copies 200 KB per frame only while the feature is on; off, it is
one flag check. No locks on the render path.

## 6. Measurements (M-series Mac, Release)

Renderer only (`BM_DrawFrame_Range16`: full 48K frame, `DrawRange` in 16-T
catch-ups, random screen memory), median of 9:

| Variant | CPU per frame | vs. before |
|---|---|---|
| before the change | 90.2 µs | — |
| plane B off | 84.8–88.6 µs | noise (zero cost) |
| plane B on, per-T table lookup | 126.6 µs | +38 µs (+42 %) |
| plane B on, lookup once per cell (current) | 118.4 µs | **+34 µs (+40 %)** |

Full emulated frame (`BM_Frame_NoTTD` vs `BM_Frame_NoTTD_PlaneB`, action.sna):
both ≈ 2.1 ms CPU on a loaded machine; the difference is inside the noise
(±40–90 µs). At 50 frames/s the renderer cost is ≈ 1.7 ms/s, about 0.2 % of
one core.

The remaining cost is the second store stream (a separate 16-bit array), not
the lookup. Candidates in [optimization-ideas.md](optimization-ideas.md):
per-cell (8-pixel) stores and SIMD for the plane B write (tagged
`SIMD-CANDIDATE(O-13)` in `screenzx.cpp`).

## 7. Tests

| Test | Checks |
|---|---|
| `ScreenZX_Test.PlaneB_OffByDefault_NoBuffer` | no buffer, null pointer while off |
| `ScreenZX_Test.PlaneB_PixelsIdenticalOnAndOff` | the picture is bit-identical with plane B on and off |
| `ScreenZX_Test.PlaneB_DescribesEveryDrawnPixel` | border color changed every 16 T and attributes rewritten mid-frame: every screen pixel's color index and RGBA follow its recorded attribute and ink bit, every border pixel its recorded color; all 49 152 paper pixels described, all 8 border colors recorded |
| `TTD_Display_Test.SeekByFrame_PlaneBMatchesLive` | after a TTD seek plane B equals the live frame's, border stripes included |
| `PresentLatch_Test.PlaneB_FeatureToggle_AppliesAtFrameStart` | a toggle neither allocates nor frees on the caller's thread; the next `InitFrame` applies it (fails with the old immediate resize) |
| `PresentLatch_Test.PlaneB_PresentedCopyFollowsPresentedFrame` | other threads see the latched plane B of the presented frame: not the in-progress render, same present delay as the pixels, none once off |

## 8. Clip export

`TimeTravelManager::ExportClip` (`ttdclipexport.cpp`), WebAPI
`POST /api/v1/emulator/{id}/ttd/export-clip {"from", "to", "path", "chunk"}`:
walks a TTD range inside the core - internal positioning by frame number +
`ComposeDisplay`, no publication per frame - and writes

| File | Content |
|---|---|
| `rgba_NNNN.zst` | RGBA8 frames (final beam-rendered picture), zstd, `chunk` frames per file |
| `planeb_NNNN.zst` | plane B frames (`uint16` LE), when the zxdlss feature is on |
| `meta.jsonl` | per frame: frame, `#7FFD`, displayed screen, border at the frame's start |
| `clip.json` | format `unreal-ng-clip` v2, geometry, encodings |

The machine is left positioned and displayed at `to`.

**Whole *Across the Edge* recording** (frames 25–16595, 16 571 frames):
105 s in one call (≈ 158 frames/s; one WebAPI round trip per frame took
≈ 10 minutes), 93 MB. Verified frame by frame
(`tools/poc/019-zxdlss-gigascreen/capture/verify_clip_v2.py`):

- plane B explains the picture on every frame: each drawn pixel's color index
  through the ZX palette equals its RGBA, screen colors follow attribute + ink
  bit, border pixels carry attribute 0, all 49 152 paper pixels described -
  0 failures;
- RGBA equals the clip extracted earlier over WebAPI - 0 differing frames;
- meta consecutive, displayed screen matches - 0 problems.

Tests: `ttdclipexport_test.cpp` (exported frames equal the live frames,
RGBA and plane B, across full and partial chunks; one meta line per frame;
refused while recording and outside the session).

## 9. Next

- Python POC v5 on plane B: split mode, sprite vs. GigaScreen, multicolor.
