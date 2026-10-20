# POC 032 proposal

**Superseded by stage 2 (below the line): adopt `sdb::jpeg`, not the vendored TooJpeg.** The stage 1 text is kept
as written.

Adopt TooJpeg in the Stream Deck companion (`docs/inprogress/2026-10-19-streamdeck-integration`, SD2, L-6). Prepared in
the same worktree as this POC, uncommitted:

- `companions/streamdeck/3rdparty/toojpeg/` (toojpeg.cpp, toojpeg.h, LICENSE; upstream 341de5c295e4, unmodified),
  target `sd-toojpeg` compiled with `-w` / `/w`, linked privately into `sdb`;
- `lib/src/image.cpp`: `EncodeJpeg` = TooJpeg, 4:4:4, quality clamped to 1..100, a `thread_local` byte sink;
  `DecodeJpeg` = stb_image (already compiled in with `STBI_ONLY_JPEG`);
- `pkg_check_modules(libturbojpeg)` removed from `companions/streamdeck/CMakeLists.txt`; the libjpeg-turbo row of
  `THIRD_PARTY_NOTICES.md` replaced by TooJpeg;
- tests: the stream is SOF0 with three 1x1 components, decodes with error under 2 levels at q90, and four threads
  encoding at once give the same bytes (849 checks, was 838).

Keep q = 90 as the default. Raising keys to q95 costs +30 % bytes (4239 -> 5509 B, 4.6 -> 5.9 reports per + XL key)
and the owner saw no difference between the q90 and q95 rows that would need it; synthetic patterns with hard
saturated edges (the `sdctl bench` gradient) do ring at q90 in either encoder: a theme can set `"quality"` higher for
such art.

Not proposed now:

- jpge two-pass (5-13 % fewer bytes on keys, 6x slower, optimized Huffman tables): only if USB bandwidth becomes the
  limit; re-measure then, and have the owner judge it on the deck first.
- Speeding TooJpeg up (buffered output instead of a call per byte, integer DCT): it would be a local patch of the
  vendored file for a cost that is 4-6 % of a frame.

Open for SD2: a full companion build on Linux (gcc) and Windows (MSVC, MinGW); this POC compiled the changed files one
by one on those compilers.

---

## Stage 2: adopt `sdb::jpeg`

Prepared in the worktree, uncommitted, replacing the stage 1 change:

- `companions/streamdeck/lib/src/jpeg.cpp` + `lib/include/sdb/jpeg.h`: our encoder, derived from TooJpeg (its zlib
  notice kept in the source, marked as altered) with libjpeg's integer arithmetic; `EncodeJpeg` calls
  `jpeg::Encode`; `DecodeJpeg` stays on stb_image;
- `companions/streamdeck/3rdparty/toojpeg` and target `sd-toojpeg` removed (TooJpeg lives on in the POC as the
  reference); no `-w` for the encoder, it is compiled with the companion's warnings;
- `THIRD_PARTY_NOTICES.md`: the TooJpeg row now names the derived file;
- tests (855 checks): golden bytes of two images (the same on clang, gcc 16, MSVC, MinGW), PSNR > 40 dB on a gradient,
  the stb_image round trip, SOF0 / 4:4:4 structure, four concurrent encoders.

Not proposed: S3 (NEON / SSE2 DCT), see the conclusions. If it is ever wanted, the first target is the DCT on 16-bit
lanes as libjpeg-turbo's `jfdctint-neon.c`; the golden-bytes test pins the output, so a SIMD path must reproduce it.
