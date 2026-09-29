# TDD — video

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): renderer and INT source are chosen by the PLD configuration module (§1) |
| **Hardware** | [hardware-reference.md](hardware-reference.md) §6 |
| **Index** | [technical-design.md](technical-design.md) |

## 1. Classes

| Class | File | Job |
|---|---|---|
| `SprinterVideoRam` | `core/src/emulator/video/sprinter/sprintervideoram.{h,cpp}` | 256 KB array (1 024 × 256 lines), `Write(addr, value)`, palette cache, "mode byte changed" notification |
| `ScreenSprinter : Screen` | `core/src/emulator/video/sprinter/screensprinter.{h,cpp}` | per-line renderer, frame geometry (320/312 lines), border, flash, HOLD |
| `SprinterIntSource : IInterruptSource` | `core/src/emulator/video/sprinter/sprinterintsource.{h,cpp}` | INT times from the mode table; also merges the keyboard and Covox-Blaster requests |
| `SprinterVideoMapper : IVideoMapper` | `core/src/emulator/video/sprinter/` | beam ↔ square ↔ source address, for the debugger (PLAN #42) |

`ScreenSprinter` is a real `Screen` subclass chosen by `VideoController::CreateScreen`
(built by PLAN #60(e), 2026-09-29: one `case MM_SPRINTER` next to `MM_TSL`) for `MM_SPRINTER`, not a helper inside
`ScreenZX`: Sprinter modes are per square and cannot be expressed as a single `VideoModeEnum`.
The per-model `Screen` selection itself is shared infrastructure (PLAN #60), in place before the
Sprinter starts.

**Selection through the configuration module.** Which renderer and which INT source run is decided
by the active PLD configuration module ([tdd-ports-memory.md](tdd-ports-memory.md) §6.1, hook 3).
The Standard module supplies `ScreenSprinter` and `SprinterIntSource`; a later module (for
example Game) can return its own renderer and INT source, and anything it does not override stays
Standard. The switch happens when a module is activated, which is always followed by a CPU reset,
so a frame is never drawn half by one renderer and half by another. Both `ScreenSprinter` and
`SprinterIntSource` read VRAM through `SprinterVideoRam`, so a new module reuses the storage and
only replaces the drawing and INT rules.

## 2. Geometry

| Quantity | Value | Why |
|---|---|---|
| Pixel clock | 14 MHz = 4 pixels per base T-state (3.5 MHz) | HW §6.1; same density as the Profi hi-res renderer (4 px/T) |
| Line | 224 T = 896 pixels = 56 squares of 16 pixels | HW §6.1 |
| Frame | 320 or 312 lines; 71 680 or 69 888 base T | HW §6.1 |
| Output raster | 736 × 288: 48 + 640 + 48 horizontally, 16 + 256 + 16 vertically (MAME's visible area, `sprinter.cpp:180-190`) | a new `RasterDescriptor` row `R_736_288`; 320-pixel content is doubled horizontally, so the framebuffer is always 736 wide |
| Visible squares | a = 0..39 (plus the right border 40-42 and left border 53-55), b = 0..31 (bottom border 32-33, top border 38-39 in 320-line mode) | MAME `sprinter.cpp:413-416` |
| Turbo | the renderer counts **base** T-states; at ratio 6 the beam still takes 224 base T per line | frames are time-based, the CPU just runs more instructions |

Line and square for a base T-state `t` in the frame: `line = t / 224`, `x = (t mod 224) × 4`,
square column `a = ((x − 48 − holdX) mod 896) / 16`, square row `b = ((line − 16 − holdY) mod lines) / 8`.

## 3. Rendering a line

The renderer catches up by line segments, like the other renderers (`Screen::DrawRange`,
`core/src/emulator/video/screen.h:668-673`), so a mid-line palette or mode change shows where it
happened (MAME calls `update_now()` before every VRAM or RGMOD write, `sprinter.cpp:1226`,
`:859`).

```text
RenderSpan(line, x0, x1):                       // x in output pixels of the 896-pixel line
  for x in x0..x1 step to the next square edge:
    a, b      = square of (x, line) after HOLD
    m         = ModeBytes(a, b, RGMOD bit 0)     // VRAM[(1 + 2a + #80·PM) · 1024 + #300 + 4b .. +3]
    if m0 bits 7-4 == %1111 (border / sync square):
        color = (m0 bits 3-2 == %11) ? blank (black) : TextPalette0[(border · 9)]
    elif m0 bit 4 == 1:                          // text / Spectrum square
        attr  = VRAM[m2 << 10 | (m0 & #0F) << 6 | (#7FFD bit 3) << 5 | %11000 | m0 >> 6]
        bits  = VRAM[m1 << 10 | (m0 & #0F) << 6 | (#7FFD bit 3) << 5 | (m0 >> 6) << 3 | (line mod 8)]
        640 mode (m0 bit 5 = 0): the right half uses the Line2 bytes (a second fetch)
        pen   = TextPalette[flash·2 + bit] [attr]   // paper / ink / flash paper / flash ink
    else:                                         // graphics square
        pal   = m0 >> 6
        col   = (m0 & #0F) << 6 | (m1 & 7) << 3;  row = (m1 >> 3) << 3
        byte  = VRAM[(row + line mod 8) · 1024 + col + pixel]
        320 mode: one byte per 2 output pixels; 640 mode: low nibble then high nibble
        pen   = GraphicsPalette[pal][byte or nibble]
    write pens to the framebuffer (only the 736-pixel window)
```

The formulas are MAME's (`draw_symbol` `sprinter.cpp:453-497`, `draw_tile` `:430-451`); MAN §4.6-4.7
is the prose version. Border color: text palette 0 at index `border × 9` (INC `SP2000.inc:403-420`,
MAME `:478-479`). Flash: frame counter bit 4 (MAME `:409`).

Performance note (not v1 work): the naive loop reads 4 mode bytes per square. MAME caches decoded
tiles; the idea goes to the optimization backlog with a benchmark (`SIMD-CANDIDATE` is not
relevant here; it is a caching question).

## 4. Palettes

- 8 palettes × 256 entries, each 3 bytes at VRAM `(n · 1024) + #3E0 + 4k` (MAN §4.8).
- `SprinterVideoRam::Write` updates a 2 048-entry RGBA cache when the column is ≥ `#3E0`
  (MAME `:1238-1243`).
- Byte order: **Blue, Green, Red** per MAN and the BIOS palette data; MAME reads offset 0 as red.
  Decided by the first S2 test (HW §4.5): render the BIOS setup screen, check that CGA entry 1
  ("blue" = `#A8,#00,#00` in memory) is blue.

## 5. INT source

`SprinterIntSource` implements the shared `IInterruptSource` (TSConf technical design §3.4):

1. **Frame INT list.** Walk squares in beam order; a square whose `Mode0 & #FD == #FD`
   (blank + INT bit) arms; the first following square without it fires an INT at the **last line**
   of that square row, at that square's x (MAME `update_int`, `sprinter.cpp:1278-1313`; MAN §4.6:
   "on the eighth line of the square"). Convert each position to a base T-state.
2. **Recompute** only when a VRAM write changes a mode byte to or from the `#FC` pattern, when RGMOD
   bit 0 changes, or on a frame-length change (MAME `:1228`, `:861`, `:394`).
3. **Pulse**: 32 T at 3.5 MHz (scaled by the clock ratio) (MAME `:1736`).
4. **Other requests** merged into the same `/INT`: keyboard (ALL_MODE bit 0 and bit 3, after each
   received scan-code frame) and Covox-Blaster half-buffer; all acknowledge with `#FF` and are
   cleared by the acknowledge (MAME `:1962-1964`).

Worked example: the BIOS Pentagon setting puts the blank+INT run at the start of the bottom
border; the test in the test plan (§2.4) measures the INT T-state after `FN_SINC` Pentagon and
compares with MAME and with the Pentagon's own `intstart` (`core/src/emulator/config.cpp:905-1057`).

## 6. Mode changes the CPU can make

| Change | Where | Effect |
|---|---|---|
| RGMOD bit 0 | code `#C5` | switches the mode page (the whole screen) at the current beam position |
| PORT_Y | code `#C4` | only affects where CPU writes go, not the display |
| HOLD | code `#CB` | picture offset: x = `(7 − (v & #0F)) × 2`, y = `7 − (v >> 4)` (MAME `:850-852`) |
| frame 320/312 | codes `#2C`/`#2D` | next frame |
| border | code `#C2` | border squares |

## 7. Surfaces and debugging

- `GET /state/screen/mode` reports `sprinter` with the frame length, RGMOD page and a summary of
  the square modes on screen (counts of text/graphics/320/640).
- Debugger views (Qt): VRAM as an image of 1 024 × 256 bytes; the mode table as a 56 × 40 grid
  (mode, palette, source); the 8 palettes. Specified in [tdd-integration.md](tdd-integration.md) §5.
- `SprinterVideoMapper` answers "which VRAM bytes make this pixel" and "which pixels show this VRAM
  byte" for the beam widget and the pixel inspector of PLAN #42.
- Screenshots: the 736 × 288 framebuffer; `SaveZXSpectrumNativeScreen` is not applicable and
  returns an error for this model.

## 8. Tests

See [test-plan.md](test-plan.md) §2.4: mode byte decoding per mode (golden 16-pixel strips
from hand-built VRAM), palette order, transparency and video-only writes, Spectrum shadow address,
INT list from a hand-built mode page, RGMOD switch mid-frame, 312/320 frame length, BIOS logo
golden image (ROM-gated); a stub configuration module's renderer override is picked up after a
simulated load (test plan §2.3, T-PLDM).
