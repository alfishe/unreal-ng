# POC 032 stage 2 measurements, 2026-10-20: `sdb::jpeg`, TooJpeg with libjpeg-turbo's techniques

Charter: [README.md](README.md#stage-2-our-own-encoder-from-toojpeg-with-libjpeg-turbos-techniques-charter-addition-2026-10-20-before-measuring).
Stage 1: [measurements-2026-10-20-encoders.md](measurements-2026-10-20-encoders.md).

## Setup

Same harness, inputs (151 images), build flags and method as stage 1; `bench --only a-turbojpeg,d-toojpeg,e-step1,f-sdbjpeg`
(the four interleaved in every round). M3 Ultra wall clock (load average 3.4-4.1), M1 Ultra thread CPU time (load
average 180-210). Raw data: `scratch/2026-10-20-poc-032/s2-m3u-wall`, `s2-m1u-cpu`.

| Variant | Code | Lines (all / without comments and blank) |
|---|---|---:|
| `d-toojpeg` | TooJpeg as upstream (`cpp/3rdparty/toojpeg/toojpeg.cpp`) | 665 / 410 |
| `e-step1` (S1) | `cpp/step1/toojpegstep1.cpp`: output into a vector, 64-bit bit buffer emitted 32 bits at a time with a no-0xFF fast path, value length by `std::countl_zero`, zero runs from a bit mask by `std::countr_zero` (jchuff.c), Huffman tables built at compile time; TooJpeg's float arithmetic | 376 / 337 |
| `f-sdbjpeg` (S1 + S2) | `companions/streamdeck/lib/src/jpeg.cpp` + `sdb/jpeg.h`: S1, plus fixed-point RGB -> YCbCr per 8-row strip (jccolor.c constants), the islow integer DCT (jfdctint.c) with the 8 rows / columns of a block as vector lanes, quantisation by an exact 32-bit reciprocal multiply; the tables are ~60 of the lines | 452 + 16 / 384 + 7 |

Auto-vectorisation (Apple clang 17, -O3, arm64), checked in the object code: the colour conversion uses `ld3` (3 x 16
pixels per load), the DCT passes `mul.4s` / `mla.4s` (72 of them), the quantisation `umull` (46); `-Rpass` reports no
loop-vectoriser success because the 8-lane loops are fully unrolled and packed by the SLP vectoriser.

## 1. Correctness

`poc032-jpegbench identity`, all 151 images at q = 50, 60, 70, 75, 80, 85, 88, 90, 92, 94, 95, 96, 98, 100:

| Check | Result |
|---|---|
| B1: `e-step1` byte-identical to `d-toojpeg` | **2114 / 2114** streams |
| `f-sdbjpeg` entropy-coded scan identical to libjpeg-turbo's | **151 / 151 images at every quality** (14 x 151): the quantised coefficients are turbojpeg's |
| `f-sdbjpeg` total size against turbojpeg | -16 B per image at every quality: only the headers differ (one DQT and one DHT segment instead of two and four) |
| golden bytes (`streamdeck-tests`): 112x112 gradient q90 -> 2696 B, FNV-1a 3336051733; 13x5 odd image q75 -> 762 B, 953815062 | the same on Apple clang arm64, gcc 16 arm64 (Linux), MSVC 2019 x64 and MinGW gcc 15 x64 (integer arithmetic only) |

## 2. Speed

### 2.1 A whole + XL frame (36 keys 112 px + strip 1200x100, q90, one thread)

| Content | Variant | M3 Ultra median / p99 us | x tj | M1 Ultra CPU median / p99 us | x tj | Bytes | Reports |
|---|---|---:|---:|---:|---:|---:|---:|
| real | a-turbojpeg | 1726 / 1988 | 1.00 | 2521 / 4322 | 1.00 | 207880 | 221 |
| real | d-toojpeg | 3608 / 4012 | 2.09 | 5213 / 8202 | 2.07 | 206491 | 217 |
| real | e-step1 (S1) | 2794 / 3150 | **1.62** | 4034 / 6609 | 1.60 | 206491 | 217 |
| real | f-sdbjpeg (S1+S2) | 2370 / 2748 | **1.37** | 3347 / 6453 | 1.33 | 207288 | 218 |
| noise | a-turbojpeg | 3921 / 4158 | 1.00 | 5561 / 8629 | 1.00 | 1099526 | 1095 |
| noise | d-toojpeg | 12101 / 13085 | 3.09 | 17062 / 23323 | 3.07 | 1098163 | 1095 |
| noise | e-step1 (S1) | 7050 / 7709 | 1.80 | 10118 / 12918 | 1.82 | 1098163 | 1095 |
| noise | f-sdbjpeg (S1+S2) | 6516 / 6968 | 1.66 | 9097 / 12794 | 1.64 | 1098934 | 1095 |

Per step on the real frame (M3 Ultra): S1 -814 us (-23 %), S2 -424 us more (-15 %); together -34 %, from 2.09x to
1.37x turbojpeg. The remaining gap: +644 us per frame, against ~90 ms of USB for the same frame (0.7 %).

### 2.2 Per image, q90 (M3 Ultra, wall clock): median / p99 us (ratio to turbojpeg); (n) = images

| Image group | a-turbojpeg | d-toojpeg | e-step1 | f-sdbjpeg |
|---|---:|---:|---:|---:|
| key-real mk2-72 (27) | 16.5 / 18.9 (1.00x) | 28.4 / 34.1 (1.73x) | 22.9 / 26.5 (1.39x) | 19.0 / 21.6 (1.15x) |
| key-noise mk2-72 (1) | 34.5 / 38.3 (1.00x) | 63.0 / 71.9 (1.83x) | 56.5 / 66.5 (1.64x) | 49.3 / 60.5 (1.43x) |
| key-real xl-96 (27) | 25.1 / 29.5 (1.00x) | 45.2 / 53.0 (1.80x) | 38.3 / 45.5 (1.53x) | 31.7 / 37.2 (1.26x) |
| key-noise xl-96 (1) | 59.9 / 71.8 (1.00x) | 123 / 140 (2.06x) | 102 / 115 (1.70x) | 89.9 / 102.2 (1.50x) |
| key-real plusxl-112 (27) | 32.5 / 37.2 (1.00x) | 59.7 / 67.9 (1.84x) | 51.6 / 58.9 (1.59x) | 42.8 / 49.4 (1.32x) |
| key-noise plusxl-112 (1) | 81.5 / 91.2 (1.00x) | 182 / 206 (2.23x) | 141 / 162 (1.73x) | 125 / 139 (1.53x) |
| key-real plus-120 (27) | 36.5 / 42.1 (1.00x) | 67.1 / 75.3 (1.84x) | 58.5 / 66.1 (1.60x) | 48.6 / 54.4 (1.33x) |
| key-noise plus-120 (1) | 93.2 / 105.5 (1.00x) | 216 / 238 (2.31x) | 162 / 180 (1.74x) | 144 / 162 (1.55x) |
| strip-real plusxl-1200x100 (10) | 352 / 399 (1.00x) | 681 / 764 (1.93x) | 603 / 659 (1.71x) | 497 / 543 (1.41x) |
| strip-noise plusxl-1200x100 (1) | 819 / 897 (1.00x) | 2543 / 2710 (3.11x) | 1519 / 1599 (1.86x) | 1397 / 1475 (1.71x) |
| strip-real plus-800x100 (10) | 226 / 255 (1.00x) | 484 / 545 (2.14x) | 418 / 458 (1.85x) | 345 / 386 (1.53x) |
| strip-noise plus-800x100 (1) | 530 / 579 (1.00x) | 1666 / 1781 (3.14x) | 970 / 1126 (1.83x) | 896 / 984 (1.69x) |
| strip-real neo-248x58 (10) | 55.8 / 66.3 (1.00x) | 118 / 134 (2.12x) | 95.9 / 109.6 (1.72x) | 79.8 / 91.0 (1.43x) |
| strip-noise neo-248x58 (1) | 100 / 117 (1.00x) | 234 / 274 (2.33x) | 184 / 224 (1.84x) | 163 / 190 (1.63x) |
| screen-real xl-1024x600 (1) | 1472 / 1500 (1.00x) | 3164 / 3186 (2.15x) | 2636 / 2686 (1.79x) | 2245 / 2275 (1.53x) |
| screen-noise xl-1024x600 (1) | 4131 / 4476 (1.00x) | 12828 / 13216 (3.11x) | 7472 / 7759 (1.81x) | 6923 / 7470 (1.68x) |
| screen-real plus-800x480 (1) | 940 / 966 (1.00x) | 2023 / 2092 (2.15x) | 1689 / 1720 (1.80x) | 1423 / 1490 (1.51x) |
| screen-noise plus-800x480 (1) | 2444 / 2995 (1.00x) | 7962 / 9197 (3.26x) | 4684 / 5352 (1.92x) | 4316 / 5181 (1.77x) |
| screen-real plusxl-1280x800 (1) | 2411 / 2442 (1.00x) | 5297 / 5354 (2.20x) | 4461 / 4588 (1.85x) | 3746 / 3888 (1.55x) |
| screen-noise plusxl-1280x800 (1) | 6847 / 7940 (1.00x) | 21234 / 23054 (3.10x) | 12508 / 13692 (1.83x) | 11417 / 12489 (1.67x) |

In ns: a + XL key (112 px, real) 32.5 us turbojpeg / 59.7 TooJpeg / 51.6 S1 / **42.8 us** S2; the 1200x100 strip 352 /
681 / 603 / **497 us**; the frame 1.73 / 3.61 / 2.79 / **2.37 ms**.

### 2.3 Where the remaining time goes (`sample`, M3 Ultra, 8 s of `bench --only a-turbojpeg,f-sdbjpeg`, samples)

| libjpeg-turbo (NEON) | samples | sdb::jpeg | samples |
|---|---:|---|---:|
| `jsimd_huff_encode_one_block_neon` | 939 | `EncodeBlock` (Huffman) + `Writer::Bits` | 683 + part of 1091 merged |
| `jsimd_fdct_islow_neon` + `jsimd_quantize_neon` + `jsimd_convsamp_neon` | 461 | `Transform` (gather, two 32-bit DCT passes, transposes, quantisation) | 831 |
| `jsimd_extrgb_ycc_convert_neon` | 124 | colour conversion (inlined in `Encode` / merged) | ~160 |

The gap is the DCT: libjpeg-turbo's NEON islow works on 16-bit lanes (a whole 8-sample row per register, `vqdmulh`),
the auto-vectorised one on 32-bit lanes with two transposes. The Huffman coding is roughly on par (part of it is in the
merged symbol, which the profile cannot split).

## 3. Bytes and quality at q90 (M3 Ultra run; the same on M1 Ultra)

| Category | Variant | Bytes x tj | Mean PSNR dB | max per-image dPSNR vs tj |
|---|---|---:|---:|---:|
| key-real (108) | d-toojpeg = e-step1 | 0.9943 | 39.77 | 0.708 |
| | **f-sdbjpeg** | **0.9957** | **39.82** (tj 39.82) | **0.000** |
| key-noise (4) | d / e | 0.9988 | 30.00 | 0.014 |
| | f-sdbjpeg | 0.9992 | 30.00 | 0.000 |
| strip-real (30) | d / e | 0.9946 | 38.25 | 0.495 |
| | f-sdbjpeg | 0.9995 | 38.27 (tj 38.27) | 0.000 |
| strip-noise (3) | d / e | 0.9993 | 29.98 | 0.019 |
| | f-sdbjpeg | 0.9999 | 29.97 | 0.000 |
| screen-real (3) | d / e | 0.9966 | 41.48 | 0.032 |
| | f-sdbjpeg | 0.9999 | 41.49 (tj 41.49) | 0.000 |
| screen-noise (3) | d / e | 0.9993 | 29.97 | 0.012 |
| | f-sdbjpeg | 1.0000 | 29.96 | 0.000 |

B2 (PSNR within 0.1 dB of turbojpeg, bytes within 1 %): met exactly; the decoded pixels are turbojpeg's.

## 4. Binary size (M1 Ultra, Release, LTO)

| `unreal-streamdeck` | Bytes | Stripped | `__TEXT` | Encoder object `__text` (-O3, alone) |
|---|---:|---:|---:|---:|
| turbojpeg (before POC 032) | 2,059,648 | 1,430,760 | 1,327,104 | (dylib, 734,624 B) |
| TooJpeg (stage 1) | 2,077,184 | 1,446,616 | 1,343,488 | 10,120 |
| **sdb::jpeg (stage 2)** | **2,077,152** | **1,446,520** | 1,343,488 | 11,904 |

## 5. Warnings (our code, no `-w`)

| Compiler | Flags | jpeg.cpp, image.cpp, tests.cpp |
|---|---|---|
| Apple clang 17 (project build) | companion's `-Wall -Wextra -Werror` | 0 |
| gcc 16.2 (docker, arm64) | companion's gcc flags (`-Wall -Wextra -Werror ...`); jpeg.cpp also `-Wpedantic -Wconversion -Wshadow` | 0 |
| MinGW gcc 15.2 (Windows host) | companion's gcc flags | 0 |
| MSVC 2019 16.11 x64 | `/W4 /WX` (stricter than the companion's default) | 0, after two fixes (a shift in a not-taken `constexpr` branch, two shadowed test locals) |

## 6. Hardware (+ XL), stage 2

Before showing: theme (product `sdctl`, sdb::jpeg q90) briefly; then the strip = sdb::jpeg q90 status scene
(`sdctl lcd-scene status`, 44 reports); then `keys-compare --rows a-turbojpeg:90,f-sdbjpeg:90,d-toojpeg:90,f-sdbjpeg:95`:
**row 0 = keys 0-8 turbojpeg q90, row 1 = keys 9-17 sdb::jpeg q90, row 2 = keys 18-26 TooJpeg q90, row 3 = keys 27-35
sdb::jpeg q95**; key 0 / 9 / 18 / 27 the label key, columns 1-8 the icons bug ... eye; all 4:4:4, FitImage 144 -> 112,
rotate 270. 4522 / 4506 / 4494 / 5892 B per key; 0 HID write failures. Left on the deck for the owner's look; **not
judged yet** at the time of writing.
