# Video Mode Decode and Renderers (ATM710 / ATM3)

How an OUT to the mode ports maps to a `VideoModeEnum`, and how each mode is
rendered. Implementation: `Screen::DetectModeATM3` (`core/src/emulator/video/screen.cpp`)
and `ScreenZX::DrawATMMode` / `ScreenZX::DrawAlcoMode`
(`core/src/emulator/video/zx/screenzx.cpp`).

## The two mode sources

| Register | Bits | Written by | Meaning |
|----------|------|-----------|---------|
| `pFF77[2:0]` | ATM mode | `OUT (xx77),A` data bits 0-2 | selects the mode family (EGA / HWM / TX / TL / ZX base) |
| `pEFF7` bit 0 (`z0`), bit 5 (`z5`) | Pentagon z-modes | `OUT (#EFF7),A` | 16-color AlCo (`z0`) / hardware multicolor (`z5`) overlay on the ZX raster |

Both latches live on ATM3; the FPGA counterparts are `atm_scr_mode`
(`z80/zports.v`, `atm_scr_mode <= din[2:0]` on an xx77 write) and `peff7_int`.

## Hierarchical decode (FPGA `video/video_modedecode.v`) - what unreal-ng implements

The FF77 mode family is consulted FIRST. Only when it is the ZX base mode do
the EFF7 z-bits matter at all:

```
atm_vmode = pFF77 & 7          // mode family
pent_vmode = { pEFF7.0, pEFF7.5 }   // z0, z5 - ONLY used when atm_vmode == 3

atm_vmode:                          pent_vmode (atm_vmode == 3):
  0 -> EGA  320x200 16c  (M_ATM16)    2'b10 (z0 only) -> AlCo 16c (M_P16)
  2 -> HWM  640x200     (M_ATMHR)     2'b01 (z5 only) -> HWMC     (M_PMC)
  6 -> TX   80x25 text  (M_ATMTX)     2'b00 and 2'b11 -> plain ZX (M_ZX)
  7 -> TL   80x25 linear (M_ATMTL)
  3 -> ZX base -> consult pent_vmode  (2'b11 is undefined on hardware and
  1, 4, 5 -> undefined -> no mode     falls back to ZX, matching the FPGA's
                                        one-hot decode where no z-mode fires)
```

Key consequences, all FPGA-faithful and all covered by
`ModeMatrix_ATM3_EFF7ZBits_HierarchicalDecode`:

- An extended FF77 mode (0/2/6/7) **ignores** the z-bits completely; e.g.
  `FF77=0, EFF7=0x21` renders EGA, not AlCo.
- `z0 && z5` together (`EFF7 & 0x21 == 0x21`) is undefined on hardware and
  renders plain ZX.
- FF77 modes 1/4/5 produce no video mode at all (M_NUL fallback).

A change of the z-bits (`EFF7 & 0x21`, i.e. `EFF7_4BPP | EFF7_HWMC`) triggers a
raster re-init exactly like an xx77 mode change; control-only EFF7 writes do
not (`PortEFF7_ControlBitsStored_ZBitsTriggerRedetection`). xpeccy behaves the
same (`evoOutEFF7` -> `evoSetVideoMode` unconditionally; harmless there because
its decode ignores collisions).

## The xpeccy `<< 1` bug (a port difference found)

`pentevo.c:33` composes one flat mode byte:

```c
int mode = (comp->pEFF7 & 0x20) | ((comp->pEFF7 & 0x01) << 1) | (comp->prt2 & 0x07);
// z5.z0.0.b2.b1.b0     <-- the comment says z0 sits at bit 4
```

The code shifts `z0` to **bit 1**, colliding with FF77 mode bit 1. The intended
layout (per its own comment) was bit 4. Effects:

- `case 0x13` (AlCo over ZX base) and `case 0x23` (HWMC over ZX base) are
  **dead code** - `0x13 = z5|mode 3` would require `z5` AND mode 3 with z0
  clear, which the switch never reaches as AlCo.
- `z0` over the ZX base actually lands in `case 0x01`/`0x03` territory and is
  mis-decoded.

Verified present in **both** the xpeccy-plus fork and upstream
samstyle/Xpeccy master, so it is an upstream bug, not a fork regression.
Original Unreal 0.39.0 never wired EFF7 into ATM3 mode detection at all (EFF7
is only used for Pentagon 384/HWMC attribute tables there), which is why the
bug went unnoticed: the BaseConf FPGA is the only self-consistent reference.
unreal-ng therefore implements the **hierarchical FPGA decode**, not xpeccy's
flat composition.

## Timing: ATM3 keeps 312 lines in every mode

All ATM modes share ZX timing: 224 T-states/line, 312 lines/frame,
`maxFrameTiming == 69888`. The ALCO/HWMC mode ids (`M_P16`, `M_PMC`) carry
Pentagon-class raster descriptors (320 lines / 71680 T) because on Pentagon
hardware those modes genuinely run on that timing. On MM_ATM3 the emulator
overrides the descriptor at all three timing sites:

- `Screen::SetVideoMode`
- `ScreenZX::CreateTstateLUT`
- `ScreenZX::TransformTstateToFramebufferCoords`

using the ZX48 descriptor (`M_ZX48`, 224T x 312) for `M_P16`/`M_PMC`
(`Timing_ATM3ZModes_KeepAtm312LineFrame`). Without the override the beam
mapping of the ALCO/HWMC renderers stretches past the framebuffer bottom.

## Renderer details

### Shared conventions (DrawATMMode: EGA / HWM / TX / TL)

- Screen window: T-states 32..191 of each line; 200 screen rows starting at
  framebuffer row 44 (`screenOffsetTop`); 320-px modes emit 2 px/T
  (columns 64..383 of the 448-wide frame), 640-px modes 4 px/T
  (columns 32..671 of the 704-wide frame).
- Video planes are LINEAR 8 KB with 40 bytes per line (8000 bytes per plane) -
  not the ZX 32-byte stride - except TL (below).
- Video page from `7FFD bit 3` (page 7/5); the plane pair lives in the page
  4 below it (3/1): `vp = RAMPage(videoPage)`, `ap = RAMPage(videoPage - 4)`.
- **Every color, including the border, goes through the 16-cell #FF palette
  RAM** (`state.atmPalette`); the border's 4-bit cell pointer is
  `(border_attr & 7) | (atmBorderBright << 3)` (see
  [port-differences.md](port-differences.md)).
- **Attribute decode (xpeccy `vidATMDoubleDot`, shared by HWM/TX/TL): bit 6 =
  ink bright, bit 7 = PAPER bright - there is no flash.**
  `ink  = palette[(attr & 0x07) | ((attr & 0x40) >> 3)]`,
  `paper = palette[((attr & 0x38) >> 3) | ((attr & 0x80) >> 4)]`.

### EGA 16-color 320x200 (M_ATM16, FF77 mode 0)

Each plane byte holds TWO adjacent pixels as 4-bit palette indices,
bit-interleaved ZX-attribute-style (unrealspeccy `dxr_atm0.cpp`
`line_atm0_16`): left pixel = `{b6, b2, b1, b0}`, right pixel =
`{b7, b5, b4, b3}`; bit 3 of the index is the bright flag (carried by byte
bits 6/7). Plane k serves pixel pair `(8j+2k, 8j+2k+1)` of byte group j:
ega0 = `ap+0`, ega1 = `vp+0`, ega2 = `ap+0x2000`, ega3 = `vp+0x2000`.

### Hardware Multicolor 640x200 (M_ATMHR, FF77 mode 2)

1 bpp pixel planes + ZX-attr planes. Pixel bytes alternate planes every 8 px:
even byte groups `+0`, odd `+0x2000` (`dxr_atm2.cpp` `line_atm2_8/16`;
ZXMAK2 `Atm640Renderer` `+0x2000*((y*80+x)&1)`); attr planes pair with the
same parity. Bits are MSB-first (bit 7 = leftmost pixel).

### Text 80x25 (M_ATMTX, FF77 mode 6)

Text rows at plane byte `0x1C0 + 64*row` (screen starts at ray line 56 ->
row 0 at 0x1C0; `draw.cpp` `PrepareFrameATM2`). All 8 scanlines of a row read
the same 40 bytes per plane; the font line (`screenY % 8`) selects the glyph
row. Char codes: p0 = `vp+0`, p1 = `vp+0x2000`; attrs: a1 = `ap+0x2000` pairs
with p0 chars, a0 = **`ap+1`** pairs with p1 chars - the +1 offset is a
hardware quirk kept faithfully from `dxr_atm6.cpp`. Font: built-in 2 KB table,
row-major `[row * 256 + code]`, MSB-first bits.

### Text Linear 80x25 (M_ATMTL, FF77 mode 7 - ATM3/ZX-Evo only)

Reads a DEDICATED page instead of the vp/ap pair: `videoPage == 5` -> RAM page
8, else page 10 (ZXMAK2 `UlaAtm450.UpdateVideoPage`). Text rows are linear
64-byte blocks (ZXMAK2 `EvoTxtRenderer`): codes at `0x01C0/0x11C0 + 64*r`
(even/odd char column), attrs at the complement-parity `0x31C0/0x21C0 + 64*r`.
Font and bit order identical to TX.

### EFF7 z-modes over the ZX raster (DrawAlcoMode: M_P16 / M_PMC)

LUT-driven (same `TstateCoordLUT` as the ULA path); 256x192 inside the
352x288 border frame, one T-state = one pixel pair.

- **M_P16 AlCo 16-color** (xpeccy `vidDrawAlco`): four planes at
  `{vidPage ^ 1, vidPage} x {+0, +0x2000}` with ZX screen addressing - note
  the pair pages are `^1`, not the `-4` of the extended modes. Byte = two
  4-bit palette indices in the same packing as EGA (left `{b6,b2,b1,b0}`,
  right `{b7,b5,b4,b3}`).
- **M_PMC hardware multicolor** (xpeccy `vidDrawHwmc`): the bitmap byte AND
  the attribute byte are both fetched from the PIXEL address of the video page
  (xpeccy reads `MADR(vidPage, pixAdr)` for both - faithful to the reference).
  Attr decode: ink = bits 0-2, paper = bits 3-5, **bit 6 brights both**, and
  **bit 7 = flash** - it inverts the bitmap on the 16-frame flash phase
  (`_vid.flash`), the only flash in the ATM family.
- Border and all colors through the #FF palette (Pentagon has no #FF port, but
  the cells default to the ZX colors so stock software sees stock colors -
  xpeccy `vid_zx_palette` presets).

### Batch rendering (ScreenHQ = OFF)

`RenderFrameBatch` has no batch renderer for the ATM/ALCO modes
(`RenderScreen_Batch8` assumes ZX geometry) and instead runs the per-T-state
renderer across the frame; `RenderFrameBatch_EquivalentToPerTstate_AllModes`
asserts equivalence.
