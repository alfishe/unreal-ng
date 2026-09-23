# ATM Extended-Mode Video: Border Geometry and Framebuffer Addressing

Investigation date: 2026-09-22/23. Triggered by a visual bug report on ATM2/710
hires modes ("2-3 pixel stripe on left/right sides, top 15-20% of screen") that
turned into a full audit of border geometry and video-RAM addressing across
four reference emulators.

## Summary

Two separate things were checked. One was a real, previously-undiscovered
architecture bug (confirmed, fixed). The other was a well-founded suspicion
that turned out **not** to be a bug (addressing is correct, verified two
ways). Both are documented here so the dead end doesn't get re-walked.

| Question | Verdict | Evidence |
|---|---|---|
| Do ATM hires modes have a real left/right border? | **No — bug, now fixed** | 3 independent ZXMAK2 renderer classes |
| Is video-page selection `p7FFD bit 3` only, or bits 3+6? | **Bit 3 only — code was already correct** | unreal-speccy `dxr_atm0.cpp` + ZXMAK2 `MemoryAtm710.cs`, cross-checked, plus a live memory-poke experiment |
| Is `p7FFD` latched once per frame or read live? | **Read live — code was already correct, and period-accurate** | ZXMAK2 port-write handlers set the register directly, no frame-boundary latch |

## 1. The bug: ATM hires modes have no side border

### What the code assumed

`core/src/emulator/video/screen.h`'s `rasterDescriptors[]` table modeled
every ATM extended mode (`M_ATM16` EGA 320×200, `M_ATMHR` hardware
multicolor 640×200, `M_ATMTX` text 80×25, `M_ATMTL` ZX-Evo linear text) the
same way as a plain ZX48/128/Pentagon screen: a scanned border margin on
all four sides, active picture centered inside a wider/taller frame buffer.

```
Old model (WRONG for ATM hires modes):

  ┌────────────────────────────────────────┐  ← fullFrameWidth = 704
  │           border (32px)                │
  │   ┌─────────────────────────────┐      │
  │   │                             │      │  screenOffsetTop = 44
  │   │      active picture         │      │
  │ B │      640 × 200              │  B   │
  │ o │                             │  o   │
  │ r │                             │  r   │
  │ d │                             │  d   │
  │ e │                             │  e   │
  │ r │                             │  r   │
  │   └─────────────────────────────┘      │
  │           border (32px)                │
  └────────────────────────────────────────┘
        screenOffsetLeft = 32, screenWidth = 640
```

The side-border area was painted by a beam-scan fallback using a
hard-coded 2 px/T-state pixel clock (correct for the 320-wide `M_ATM16`
EGA-adjacent geometry, wrong for the 704-wide hires storage) — this was
the first thing found and fixed this session (`screenatm.cpp`, the
`pxPerT` computation), and it *did* eliminate the originally-reported
2-3px stripe. But it was a correct fix to the wrong model: real ATM
hardware doesn't scan a side border there at all.

### What real hardware / reference emulators do

`/Volumes/TB4-4Tb/Projects/emulators/github/ZXMAK2/src/ZXMAK2.Hardware/Atm/`
has three independent renderer classes for these exact modes, each with an
explicit, named border-width parameter:

```csharp
// Atm320Renderer.cs (EGA 320x200) — CreateParams()
timing.c_ulaBorderTop = 28;
timing.c_ulaBorderBottom = 28;
timing.c_ulaBorderLeftT = 0;      // ← zero
timing.c_ulaBorderRightT = 0;     // ← zero
timing.c_ulaWidth = (0 + 160 + 0) * 2;   // = 320, exactly screenWidth

// Atm640Renderer.cs (HW multicolor 640x200) — identical pattern
// AtmTxtRenderer.cs (text 80x25) — identical pattern
```

Three separate, independently-implemented renderer classes agree: **zero
side border, `fullFrameWidth == screenWidth`, only a real top/bottom
border remains.**

```
Correct model (now implemented):

  ┌──────────────────────────────────┐  ← fullFrameWidth = screenWidth = 640
  │         border (top)             │
  ├──────────────────────────────────┤
  │                                  │
  │        active picture            │
  │        640 × 200                 │  edge-to-edge, no side margin
  │                                  │
  ├──────────────────────────────────┤
  │        border (bottom)           │
  └──────────────────────────────────┘
```

### The fix

`core/src/emulator/video/screen.h`, `rasterDescriptors[]`:

```cpp
// Before
{448, 288, 320, 200, 64, 44, 448, 64, 32, 16, 8},  // M_ATM16
{704, 288, 640, 200, 32, 44, 448, 64, 32, 16, 8},  // M_ATMHR
{704, 288, 640, 200, 32, 44, 448, 64, 32, 16, 8},  // M_ATMTX
{704, 288, 640, 200, 32, 44, 448, 64, 32, 16, 8},  // M_ATMTL

// After
{320, 288, 320, 200, 0, 44, 448, 64, 32, 16, 8},   // M_ATM16
{640, 288, 640, 200, 0, 44, 448, 64, 32, 16, 8},   // M_ATMHR
{640, 288, 640, 200, 0, 44, 448, 64, 32, 16, 8},   // M_ATMTX
{640, 288, 640, 200, 0, 44, 448, 64, 32, 16, 8},   // M_ATMTL
```

(`pixelsPerLine=448` is unchanged — that's the shared ZX-compatible beam
*timing*, not framebuffer storage width; only the two geometry fields
change.) No changes needed in `ScreenAtm::Draw` itself: with
`screenOffsetLeft=0`, the generalized `pxPerT = screenOffsetLeft /
SCREEN_START_T` side-border fill computed this session naturally becomes
`0`, so the side-border loop writes nothing — the code degrades correctly
to "no side border" without special-casing.

Vertical border geometry (`screenOffsetTop=44`, `fullFrameHeight=288`) was
**not** touched — the evidence above is specifically about the horizontal
axis; ZXMAK2's `c_ulaBorderTop/Bottom=28` differs numerically from our 44
because of a different vsync/vblank accounting convention, not because our
vertical border is wrong. That's a separate question this investigation
didn't need to open.

## 2. Not a bug: video-page addressing

A large chunk of this investigation chased two competing claims about how
ATM selects which physical RAM page holds hires video data:

- A pasted note claimed ATM2 EGA mode uses **fixed** physical pages 4 and 6,
  no `#7FFD` dependency at all.
- This repo's own `atm-video-crossanalysis.md` claimed video page comes
  from **bits 3 and 6** of `#7FFD` (not just bit 3).

Neither held up. Tracing the actual reference sources:

- `unreal-speccy/dxr_atm0.cpp`: plane offsets `ega0_ofs = -4*PAGE`,
  `ega1_ofs = 0`, `ega2_ofs = -4*PAGE+0x2000`, `ega3_ofs = 0x2000` relative
  to a base page pointer — structurally identical to this codebase's
  `vp`/`ap = RAMPageAddress(videoPage)` / `RAMPageAddress(videoPage-4)`.
- `ZXMAK2/src/ZXMAK2.Hardware/Atm/MemoryAtm710.cs`:
  ```csharp
  protected virtual void BusWritePort7FFD_128(ushort addr, byte value, ref bool handled)
  {
      if (m_lock) return;
      CMR0 = value;              // CMR0 IS the raw #7FFD byte
  }
  ...
  int videoPage = (CMR0 & 0x08) == 0 ? 5 : 7;   // bit 3 only
  ```
- `Xpeccy/src/libxpeccy/hardware/atm2.c`: `comp->vid->curscr = (val & 0x08)
  ? 7 : 5;` — set directly and synchronously inside the `#7FFD` port-write
  handler, then read live (per-dot) by `vidDrawATMega`/`vidDrawATMhwmc` in
  `video.c`. No frame-boundary latch, no bit 6.

Then verified empirically against the actual running emulator (not just
reference source): wrote `0xFF` into RAM page 5 and `0x47` into page 1 at
the exact byte address the formula predicts for a specific screen column,
advanced one frame, and the predicted 8-pixel-wide white block appeared at
exactly that column. Setting page 5 to `0x00` and page 7 to `0xFF` at the
same address turned the pixel black — confirming page 5 (not 7) was the
live plane, matching `p7FFD` bit 3 = 0 at that moment.

**Conclusion:** `videoPage = (p7FFD & 0x08) ? 7 : 5`, read fresh on every
`Draw()` call (not latched once per frame), is correct and matches three
independent reference implementations plus live empirical testing. The
stale "bits 3,6" line in `atm-video-crossanalysis.md` has been corrected.

## Dead ends worth remembering

- **TTD-seek artifacts**: ruled out by comparing a direct `ttd/seek` to a
  frame against reaching the same frame via 100 linear `step-forward`
  calls — byte-identical framebuffers. TTD replay is not the source of any
  visual discrepancy seen this session.
- **GUI vs. WebAPI capture-endpoint divergence**: observed once (GUI
  showed different content than `/capture/screen` for a supposedly-paused
  instant) but never isolated to a reproducible cause. Flagged, not
  resolved — worth an independent look if it recurs.
- **"Equalizer bars" / isometric box shading**: several observed visual
  patterns (tapered gradient bands, a small highlight-dot at a shape's
  corner) that looked suspicious turned out, on precise pixel-boundary
  measurement, to be internally consistent — most likely legitimate
  hand-drawn pixel art, not corruption. Worth remembering before assuming
  every odd-looking gradient in ATM demo graphics is a rendering bug.
