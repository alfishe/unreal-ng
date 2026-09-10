# 14 — MiSTer Reference Model (Implemented)

**Date:** 2026-08-10
**Status:** Implemented
**Decision:** MiSTer ZX-Spectrum core (ula.sv) is the timing reference for Pentagon.
Frame anchor: variant A — paper stays at emulator line 80; framebuffer is the exact
MiSTer visible frame 340×284.

---

## 1. Why the old values didn't work

The old `intstart=71619` was a MiSTer coordinate transplanted with an incomplete
conversion: the vertical origin was converted (+80 lines), the horizontal was not.

Three independent error sources were being compensated by hand-tuning (71625 + fix1):

1. **24T horizontal origin mismatch.** MiSTer `hc=0` is the first paper pixel;
   the old LUT treated t-in-line 0 as the first *border* pixel and ignored hblank
   entirely (`framebufferX = (t % 224) * 2`).
2. **6T MiSTer output pipeline.** INT is generated from raw hc/vc counters, but
   pixels leave the fetch/shift pipeline 12 px (6T) later (paper output window is
   hc 12..267, border override `hc<12 | hc>267` in ula.sv).
3. **Sub-instruction effects** (INT sampled at instruction boundary, OUT applied
   at +7T of OUT (n),A) — the residual ±2T covered by the demo "fix" ladder.

## 2. The new coordinate system (Pentagon)

Single source of truth: `rasterDescriptors[M_PENTAGON128K]` in `screen.h`
and the rasterState derived from it. LUT, transforms, contention and floating bus
all use the same origins now.

### Line (224T = 448 px @ 7MHz)

| t-in-line | Content | MiSTer hc |
|---|---|---|
| 0..53 | hsync+hblank (invisible) | 312..419 |
| 54..73 | left border (40 px) | 420..447, 0..11 |
| 74..201 | paper (256 px) | 12..267 |
| 202..223 | right border (44 px) | 268..311 |

### Frame (320 lines)

| lines | Content | MiSTer vc |
|---|---|---|
| 0..31 | blank (VSync at 8..15) | 240..271 |
| 32..79 | top border (48) | 272..319 |
| 80..271 | paper (192) | 0..191 |
| 272..315 | bottom border (44) | 192..235 |
| 316..319 | trailing blank (`vBlankBottomLines=4`) | 236..239 |

### Framebuffer

**340×284**, paper rectangle at (40, 48)..(295, 239).
This is pixel-for-pixel the MiSTer visible output (no padding, no loss).

### INT

MiSTer: vc=239, hc=326, 64 hc = 32T. In emulator coordinates:

```
emulatorLine = (239 + 81) mod 320 = 0     (hc=326 is inside hblank, which STARTS
                                           the line carrying paper of vc+1)
tInLine      = (326 - 312) / 2 = 7
intstart     = 7,  intlen = 32
```

INT → first visible paper pixel: `(80*224 + 74) - 7 = 17987 T`
(= MiSTer counter distance 17981 + 6T pipeline).

Cross-check with other emulators (INT→paper): original Unreal Speccy `Paper=17989`,
ZXMAK2 `80*224+65 = 17985`, MiSTer visible 17987 — all within ±2T; our value is
exactly the reference (MiSTer).

Note: intstart=7 effectively restores the original Unreal Speccy convention
(frame starts ~at INT; US used intstart≈0..13 with t=0 = INT).

## 3. ZX-48K / 128K (interim)

Their descriptors/LUT now also use the hblank-first line origin, which shifted all
video events +48T in the t-domain. To preserve their previous INT-vs-picture
alignment, intstart got the same +48T: 48K `1794 → 1842`, 128K/+3 `2056 → 2104`.
A proper MiSTer derivation for mZX modes (visible window, pipeline, INT at vc=248
hc=4/8) is still pending.

## 4. Files changed

| File | Change |
|---|---|
| `core/src/emulator/video/screen.h` | `vBlankBottomLines` field; Pentagon descriptor `{340,284,256,192,40,48,448,32,76,8,24,4}` |
| `core/src/emulator/video/screen.cpp` | maxFrameTiming includes trailing blank lines |
| `core/src/emulator/video/zx/screenzx.cpp` | LUT + transforms account for hblank (framebuffer X origin after blank); trailing blank lines are silent RT_BLANK |
| `core/src/emulator/config.cpp` | intstart defaults: Pentagon 7, 48K 1842, 128K/+3 2104; legacy values (13/1794/2056/71619/71623/71625) treated as placeholders |
| `data/configs/*/unreal.ini` | intstart updated accordingly |
| `core/tests/emulator/video/int_timing_test.cpp` | new expectations + geometry tests (paper at (40,48), INT→paper=17987, border widths, trailing blank) |
| `core/tests/emulator/video/screenzx_test.cpp` | Pentagon line/frame layout, transform tests rewritten for hblank-first origin |

## 4a. Sub-instruction IO timing fix (2026-08-10, follow-up)

Demo calibration ("Across The Edge") revealed that INT acceptance quantizes to
**4T** (HALT executes 4T NOP cycles; /INT is sampled once per instruction), so
`intstart` can only steer alignment in 4T cells — the demo's own 2T "fix" ladder
exists precisely for the intra-cell residue. A persistent 2T (4 px) residue in
the emulator was traced to port-write timing: `out()` was applied at the START
of the IO machine cycle (instruction T+7 for OUT (n),A) while the real Z80
asserts IORQ+WR at **T2 of the IO cycle** (T+9), which is when the ULA latches
the border color. Fixed in all OUT paths (`op_D3`, all `OUT (C),r`,
OUTI/OUTD/OTIR/OTDR): the IO cycle is now split `cputact(2); out(); cputact(2);`.
Total instruction length is unchanged. IN paths (floating bus sample point) are
still applied at IO-cycle start — see follow-ups.

Negative `intstart` is now supported end-to-end (GUI, INI, core): `-N` means
"N T-states before frame start", normalized to `frame-N`; the INT window then
wraps the frame boundary correctly (tail `[intstart; frame)` asserts, gap
clears, `[0; int_end)` continues after the wrap).

## 5. Known follow-ups

- **Floating bus fetch anchor:** `ulacontention.cpp` models fetch at
  `screenLineAreaStart - 4`; MiSTer counter-truth is display − 6T (fetch window
  t-in-line 68..195). 2T refinement pending its own verification pass.
- **mZX (48K/128K) MiSTer derivation** (see §3).
- **Qt cosmetics:** `devicescreen.h` sizeHint/aspect still 352×288 (display-only);
  video recording benchmark uses synthetic 352×288 (emulator-independent).
- **Verification on demos:** "Across The Edge" fix0 should now align without
  fix-ladder compensation; border multicolor demos should be stable.
