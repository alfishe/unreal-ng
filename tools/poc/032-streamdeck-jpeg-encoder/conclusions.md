# POC 032 conclusions

Stage 1 chose TooJpeg; stage 2 ([below](#stage-2-sdbjpeg-2026-10-20)) replaced it with our own `sdb::jpeg`, which is
what the product now uses.

## Answers

1. **Yes.** TooJpeg (Stephan Brumme, zlib, one .cpp + .h) replaces libjpeg-turbo for every image libsdb sends: the
   same bytes on the wire (0.994-0.999x at equal PSNR, 217 vs 221 reports per + XL frame), the same quality (PSNR
   within 0.1 dB at q90), baseline 4:4:4 streams the deck renders (the owner's visual check passed on the keys and
   the strip), no system dependency.
2. **TooJpeg** over stb_image_write (identical output, 10-15 % slower, needs a patch for 4:4:4 at q <= 90) and over
   jpge (2.2-5.6x turbojpeg; its fast mode is 13-19 % larger at equal PSNR, its small mode 6x slower).
3. Cost: encoding a whole + XL frame takes 2.1x longer, 1.8 -> 3.8 ms (M3 Ultra) / 2.6 -> 5.35 ms CPU (M1 Ultra),
   against ~90 ms of USB for the same frame. Wire bytes: about 1 % fewer. Binary: `unreal-streamdeck` +17.5 KB
   (+15.9 KB stripped), one dylib (735 KB) and the macOS 15 linker warning gone.

## Criteria

| # | Criterion | TooJpeg | Met |
|---|---|---|---|
| A1 | <= 2x turbojpeg per image; frame <= 5 ms | keys 1.69-1.83x; strips 2.05-2.33x; screens 2.16-2.23x; noise 2.1-3.3x. Frame 3.8 ms (M3 Ultra), 5.35 ms CPU (M1 Ultra, load 50-270) | **Partly**: keys yes, the rest a near miss; the frame budget it protects is met (encode = 4-6 % of the frame's USB time, live strip fps unchanged) |
| A2 | bytes within +15 % at equal PSNR and in reports at q90 | 0.994-0.999x; 217 vs 221 reports | Yes |
| A3 | PSNR at q90 not below -1 dB | -0.06 ... +0.05 dB | Yes |
| A4 | SOF0, 4:4:4, both decoders, renders on the + XL | SOF0, 11 11 11, 0 / 906 decode failures; deck: 0 write failures; owner's look V2 passed (keys, q90 and q95, side by side with turbojpeg; strip fine) | Yes |
| A5 | clang, gcc, MSVC, MinGW, zero warnings in our code | our code clean on Apple clang 17 (full build), gcc 16, MinGW gcc 15, MSVC 2019 /W3 /WX (changed files compiled one by one, not a full companion build on Linux / Windows); TooJpeg needs `/w` on MSVC (C4244), set | Yes, with the per-file limit |
| A6 | one or two files, no dependency, <= ~50 KB code | 2 files, ~16 KB code | Yes |

Hardware: one failed look (V1) on the `sdctl bench keys --content gradient` pattern at q90 (TooJpeg, 4:4:4): the
pattern's hard saturated edges ring in baseline JPEG; not encoder-specific (turbojpeg gives the same bytes and PSNR).
Not done: the owner did not compare q90 against q95 separately, and did not judge the jpge-2pass / stb screens.

**Decision proposed: go with TooJpeg** ([proposal.md](proposal.md)).

## Analysis

Numbers: [measurements-2026-10-20-encoders.md](measurements-2026-10-20-encoders.md).

| Expectation (charter) | Outcome |
|---|---|
| turbojpeg is the fastest, the scalar candidates 2-4x slower | Right. TooJpeg 1.7-1.8x on real keys, 2.0-2.3x on strips and screens, 2.1-3.3x on noise; stb 10-15 % slower than TooJpeg; jpge 2.2-5.6x; jpge two-pass 4-9.7x |
| every variant well under 1 ms per key | Right: TooJpeg 30-73 us per real key, 79-295 us per noise key |
| jpge smallest at equal PSNR | Only with two-pass Huffman (-13 % on keys, ~0 on strips). One-pass jpge with its default tables (mozjpeg-style, one table for luma and chroma) is +13 to +19 % at equal PSNR on strips and noise and 1-3 dB lower at q90 there: its quality scale is not libjpeg's. With Annex K tables +0.3 to +1.3 % |
| stb / TooJpeg within a few % of turbojpeg | Better: -0.6 to -0.1 % at equal PSNR, PSNR within 0.1 dB at the same q. Both are the jo_jpeg algorithm and give the same sizes |
| winner b-jpge | No: TooJpeg. jpge is the slowest family; its fast mode is larger, its small mode 6x turbojpeg |

**Time (A1).** A whole + XL frame: TooJpeg 3.8 ms (M3 Ultra, quiet) / 5.35 ms CPU (M1 Ultra), turbojpeg 1.8 / 2.6 ms.
The thresholds chosen before measuring (2x per image, 5 ms per frame) are missed narrowly outside the keys. What they
protect is the frame budget, and there the margin is large: the same frame's USB transfer is ~90 ms (217 reports at
~0.4 ms), so encoding is 4-6 % of it (2-3 % with turbojpeg); the live strip at 30 fps ran as fast with TooJpeg as with
turbojpeg in an interleaved A/B; a noise frame encodes in 12.7-17.6 ms against ~450 ms on the wire. Only a SIMD
encoder (libjpeg-turbo itself) would fix the ratio, and L-6 keeps that for "much worse"; 2.1x on a 4-6 % share is not.

**Size and quality (A2, A3).** TooJpeg and stb: 0.994-0.999x of turbojpeg's bytes at equal PSNR in every category,
217 vs 221 reports per + XL frame, PSNR equal within 0.1 dB. jpge-2pass alone saves bandwidth (5-13 % on keys), at 6x
turbojpeg and with optimized Huffman tables nobody else sends (the deck rendered them in H1; the owner did not judge
that screen).

**Compatibility (A4).** All variants: SOF0, 4:4:4, 8-bit tables; 906 / 906 streams decode with stb_image and
libjpeg-turbo; no HID write failures. The owner's look V2 passed turbojpeg and TooJpeg at q90 and q95 on the keys,
strip fine. V1 (failed) showed the `sdctl bench` synthetic pattern at q90 (red ramp wrapping 255 -> 0, white bar, small
digits: hard saturated edges, which ring in any baseline JPEG at that quality); encoder, subsampling, quality, resize
and rotation were the same in V1 and V2.

**Portability, simplicity (A5, A6).** TooJpeg: one .cpp (32 KB source, ~16 KB of code) and a header, no includes,
zlib, no patch (4:4:4 is a parameter). No warnings with gcc 16 / MinGW gcc 15 at -Wall -Wextra; MSVC 2019 /W3 reports
C4244 (int -> float, lines 637-638), hence `/w` on MSVC and `-w` elsewhere. The callback is a plain function pointer
with no context, one call per byte: libsdb uses a `thread_local` sink, and a test checks four concurrent encoders. stb
would be the alternative (same output, 10-15 % slower, one header, MIT / PD), but it needs a patch: upstream subsamples
at quality <= 90, and libsdb's default 90 is exactly what the + XL keys garble.

## Stage 2: `sdb::jpeg` (2026-10-20)

Numbers: [measurements-2026-10-20-optimised.md](measurements-2026-10-20-optimised.md).

| # | Criterion | Result | Met |
|---|---|---|---|
| B1 | S1 byte-identical to TooJpeg | 2114 / 2114 streams (151 images x 14 qualities) | Yes |
| B2 | S2: PSNR within 0.1 dB of turbojpeg at q90, bytes within 1 % | the entropy-coded scan equals turbojpeg's on all 151 images at every quality: PSNR identical, bytes -16 per image (headers) | Yes |
| B3 | a step is kept if it gains >= 15 % on the + XL frame; S3 only if S2 >= 1.5x | S1 -23 % (2.09x -> 1.62x), S2 -15 % (-> 1.37x; M1 Ultra 1.33x). 1.37x < 1.5x: **S3 not started** | S1, S2 kept |
| B4 | renders on the + XL | 0 write failures; rows turbojpeg / sdb::jpeg / TooJpeg / sdb::jpeg q95 left on the deck | owner's look pending |

Answers:

4. S1 (buffered output, 64-bit bit buffer, bit-mask zero runs) closes 23 % of the frame time, S2 (libjpeg's integer
   arithmetic, auto-vectorised) another 15 %: `sdb::jpeg` encodes a whole + XL frame in 2.37 ms against turbojpeg's
   1.73 ms (1.37x; per real key 1.15-1.33x, per strip 1.41-1.53x), with turbojpeg's exact output.
5. The gain stops at S2. What remains is 0.64 ms per frame, 0.7 % of the frame's ~90 ms USB transfer; the profile puts
   it in the DCT (turbojpeg's NEON islow uses 16-bit lanes). Closing it means NEON and SSE2 intrinsics for the DCT,
   ~150-250 more lines in two instruction sets with a scalar path kept and tested, to save under 1 % of a frame. Not
   worth it now; worth it only if the encoder ever runs per frame on a slow host or the strip runs at 60 fps on many
   decks at once.

The result: our code (468 lines with the header, ~60 of them tables), no third-party encoder, no system dependency,
turbojpeg's bytes and pixels, 1.37x its time, the same binary size as TooJpeg (`unreal-streamdeck` +17.5 KB against
the turbojpeg build, which also needed a 735 KB dylib).
