# POC 032 measurements, 2026-10-20: JPEG encoders for the Stream Deck companion

## Setup

| | |
|---|---|
| Harness | `cpp/jpegbench.cpp`, target `poc032-jpegbench` (`-DUNREAL_STREAMDECK=ON -DBUILD_POC=ON`), tables by `python/summarize.py` |
| Build | Release: `-O3 -DNDEBUG -g -flto=thin`, `-mmacosx-version-min=12.0`, Apple clang 17, arm64 |
| Timing host | **M3 Ultra** (32 cores, macOS), load average 3.8-4.8 during the run: wall clock (`steady_clock`). Cross-check on the **M1 Ultra** (20 cores, the deck's host), load average 50-270: thread CPU time (`--cpu`) |
| Variants | `a-turbojpeg` libjpeg-turbo 3.2.0 (Homebrew; a handle per call, as `image.cpp` did); `b-jpge` jpeg-compressor v1.05 (aeb7d3b463aa) H1V1, one pass, its default mozjpeg-style tables; `b-jpge-2pass` the same, optimized Huffman tables; `b-jpge-std` one pass, Annex K tables; `c-stb` stb_image_write v1.16 (a7d4c2175630) patched to 4:4:4; `d-toojpeg` TooJpeg (341de5c295e4) `downsample = false`. All at 4:4:4 |
| Images, 151 | keys: the 27 icons of `data/streamdeck/themes/unrealng` fitted (Contain, 144 -> key) at 72 (MK.2), 96 (XL), 112 (+ XL, rotated 270), 120 (+), plus one noise key per size; strips: 10 libsdb scenes (4 `StatusScene`, 2 `LoadingScene`, `TtdScene`, `Marquee`, `Volume`, the theme's strip) at 1200x100 (+ XL, rotated to 100x1200), 800x100 (+), 248x58 (Neo, rotated 180), plus noise; full screens: a panel composite (icons at the model's key positions + a status strip) and noise at 1024x600 (XL), 800x480 (+), 1280x800 (+ XL, rotated) |
| Method | device-space images built before timing; encoders interleaved (one call of each per round), 3 warm-up calls; 400 rounds per key, 150 per strip, 30 per screen; median and p99 per image, averaged over the group. PSNR over RGB after decoding with stb_image; every stream also decoded with libjpeg-turbo. Quality sweep q = 50..98 for the equal-PSNR comparison |
| Raw data | `scratch/2026-10-20-poc-032/m3u-wall`, `m3u-cpu` (M3 Ultra), `m1u-cpu` (M1 Ultra): `q90.csv`, `sweep.csv`, `frame.csv`, `tables.md` |

## 1. Stream structure (`poc032-jpegbench inspect`)

| Variant | SOF | Sampling | Huffman tables | Markers |
|---|---|---|---|---|
| a-turbojpeg | C0 (baseline) | 11 11 11 | Annex K | SOI APP0 DQT DQT SOF0 DHT x4 SOS |
| b-jpge, b-jpge-std | C0 | 11 11 11 | Annex K | SOI APP0 DQT DQT SOF0 DHT x4 SOS |
| b-jpge-2pass | C0 | 11 11 11 | **optimized** | SOI APP0 DQT DQT SOF0 DHT x4 SOS |
| c-stb, d-toojpeg | C0 | 11 11 11 | Annex K | SOI APP0 DQT SOF0 DHT SOS (tables merged in one segment each) |

All 906 q = 90 streams (151 images x 6) decode with both stb_image and libjpeg-turbo; the PSNR seen by the two
decoders differs by at most 0.10 dB.

## 2. A whole + XL frame: 36 keys (112 px) + the 1200x100 strip, q = 90, one thread

| Content | Variant | M3 Ultra median / p99 us | x tj | M1 Ultra CPU median / p99 us | x tj | Bytes | HID reports |
|---|---|---:|---:|---:|---:|---:|---:|
| real | a-turbojpeg | 1810 / 2074 | 1.00 | 2574 / 3681 | 1.00 | 207880 | 221 |
| real | b-jpge | 6146 / 7193 | 3.39 | 8793 / 11034 | 3.42 | 224179 | 241 |
| real | b-jpge-2pass | 10946 / 12301 | 6.05 | 15864 / 19223 | 6.16 | 201375 | 216 |
| real | b-jpge-std | 5936 / 6734 | 3.28 | 8628 / 12565 | 3.35 | 209889 | 223 |
| real | c-stb | 4219 / 4595 | 2.33 | 6051 / 9388 | 2.35 | 206491 | 217 |
| real | **d-toojpeg** | **3782 / 4130** | **2.09** | **5350 / 6987** | **2.08** | 206491 | 217 |
| noise | a-turbojpeg | 4004 / 4432 | 1.00 | 5624 / 7424 | 1.00 | 1099526 | 1095 |
| noise | b-jpge | 20878 / 23332 | 5.21 | 30585 / 33577 | 5.44 | 1145473 | 1141 |
| noise | b-jpge-2pass | 36495 / 40176 | 9.11 | 51290 / 56264 | 9.12 | 1017286 | 1009 |
| noise | b-jpge-std | 20731 / 23077 | 5.18 | 30025 / 34628 | 5.34 | 1103025 | 1096 |
| noise | c-stb | 14058 / 15754 | 3.51 | 20404 / 24364 | 3.63 | 1098163 | 1095 |
| noise | d-toojpeg | 12697 / 14803 | 3.17 | 17630 / 23723 | 3.13 | 1098163 | 1095 |

For scale: the USB side of the real frame, 217-221 reports at ~0.4 ms each (`docs/companions/streamdeck/performance.md`),
is about 90 ms; the noise frame (1095 reports) about 450 ms.

## 3. Encode time per image, q = 90 (M3 Ultra, wall clock)

Median / p99 in microseconds (mean over the group's images), ratio of the median to turbojpeg. (n) = images.

| Image group | a-turbojpeg | b-jpge | b-jpge-2pass | b-jpge-std | c-stb | d-toojpeg |
|---|---:|---:|---:|---:|---:|---:|
| key-real mk2-72 (27) | 17.6 / 19.5 (1.00x) | 41.2 / 45.8 (2.34x) | 72.4 / 79.8 (4.11x) | 39.0 / 42.7 (2.21x) | 31.8 / 34.4 (1.81x) | 29.7 / 32.1 (1.69x) |
| key-noise mk2-72 (1) | 37.8 / 41.5 (1.00x) | 135 / 149 (3.57x) | 208 / 230 (5.51x) | 135 / 153 (3.56x) | 88.3 / 97.7 (2.34x) | 79.1 / 86.5 (2.09x) |
| key-real xl-96 (27) | 28.2 / 39.3 (1.00x) | 69.1 / 91.6 (2.45x) | 124 / 175 (4.39x) | 66.2 / 86.2 (2.35x) | 55.9 / 68.3 (1.98x) | 50.7 / 69.1 (1.80x) |
| key-noise xl-96 (1) | 71.5 / 73.8 (1.00x) | 291 / 301 (4.08x) | 476 / 493 (6.67x) | 321 / 330 (4.50x) | 209 / 212 (2.92x) | 166 / 169 (2.32x) |
| key-real plusxl-112 (27) | 36.2 / 44.6 (1.00x) | 93.5 / 115.4 (2.58x) | 165 / 202 (4.54x) | 88.0 / 109.7 (2.43x) | 73.4 / 88.1 (2.03x) | 65.8 / 80.4 (1.82x) |
| key-noise plusxl-112 (1) | 86.0 / 102.5 (1.00x) | 376 / 447 (4.37x) | 639 / 730 (7.44x) | 408 / 462 (4.74x) | 287 / 323 (3.34x) | 259 / 294 (3.02x) |
| key-real plus-120 (27) | 40.0 / 51.8 (1.00x) | 104 / 135 (2.59x) | 183 / 233 (4.58x) | 98.4 / 128.1 (2.46x) | 82.3 / 103.7 (2.06x) | 73.3 / 93.6 (1.83x) |
| key-noise plus-120 (1) | 95.5 / 122.3 (1.00x) | 427 / 594 (4.47x) | 720 / 1007 (7.54x) | 462 / 596 (4.83x) | 328 / 408 (3.43x) | 295 / 384 (3.09x) |
| strip-real plusxl-1200x100 (10) | 402 / 452 (1.00x) | 1088 / 1233 (2.71x) | 1924 / 2148 (4.79x) | 1113 / 1236 (2.77x) | 930 / 1030 (2.32x) | 824 / 932 (2.05x) |
| strip-noise plusxl-1200x100 (1) | 829 / 946 (1.00x) | 4472 / 5021 (5.39x) | 7753 / 8698 (9.35x) | 4360 / 4902 (5.26x) | 3040 / 3421 (3.67x) | 2691 / 3052 (3.25x) |
| strip-real plus-800x100 (10) | 250 / 283 (1.00x) | 771 / 870 (3.09x) | 1334 / 1494 (5.34x) | 788 / 874 (3.15x) | 650 / 722 (2.60x) | 582 / 669 (2.33x) |
| strip-noise plus-800x100 (1) | 533 / 588 (1.00x) | 2962 / 3048 (5.56x) | 5096 / 5337 (9.57x) | 2926 / 3204 (5.49x) | 2010 / 2134 (3.77x) | 1765 / 1932 (3.31x) |
| strip-real neo-248x58 (10) | 61.2 / 71.5 (1.00x) | 193 / 218 (3.14x) | 326 / 363 (5.32x) | 205 / 233 (3.35x) | 159 / 179 (2.60x) | 142 / 161 (2.31x) |
| strip-noise neo-248x58 (1) | 105 / 139 (1.00x) | 482 / 659 (4.60x) | 813 / 1047 (7.76x) | 526 / 638 (5.02x) | 365 / 440 (3.48x) | 325 / 401 (3.10x) |
| screen-real xl-1024x600 (1) | 1523 / 1782 (1.00x) | 5212 / 6146 (3.42x) | 9484 / 10628 (6.23x) | 5055 / 5695 (3.32x) | 3877 / 4490 (2.55x) | 3328 / 3930 (2.18x) |
| screen-noise xl-1024x600 (1) | 4119 / 4563 (1.00x) | 22784 / 24557 (5.53x) | 39855 / 42993 (9.68x) | 22176 / 24283 (5.38x) | 15216 / 16606 (3.69x) | 13679 / 14701 (3.32x) |
| screen-real plus-800x480 (1) | 1053 / 1182 (1.00x) | 3526 / 3601 (3.35x) | 6259 / 6401 (5.94x) | 3457 / 3550 (3.28x) | 2710 / 2774 (2.57x) | 2347 / 2393 (2.23x) |
| screen-noise plus-800x480 (1) | 2719 / 2885 (1.00x) | 14673 / 15661 (5.40x) | 24712 / 26830 (9.09x) | 14453 / 15268 (5.32x) | 10228 / 10425 (3.76x) | 9085 / 9274 (3.34x) |
| screen-real plusxl-1280x800 (1) | 2568 / 3020 (1.00x) | 8666 / 9410 (3.37x) | 15732 / 17599 (6.13x) | 8408 / 9228 (3.27x) | 6377 / 6970 (2.48x) | 5535 / 6047 (2.16x) |
| screen-noise plusxl-1280x800 (1) | 6971 / 7627 (1.00x) | 37318 / 42601 (5.35x) | 64211 / 71536 (9.21x) | 36109 / 40600 (5.18x) | 24709 / 27628 (3.54x) | 21767 / 24496 (3.12x) |

## 4. Output size and quality

### 4.1 Bytes at equal PSNR (each image at turbojpeg q90's PSNR, interpolated on the variant's q sweep), and at q = 90

| Category | Variant | x tj at equal PSNR | x tj at q90 |
|---|---|---:|---:|
| key-real (108) | b-jpge | 0.998 | 1.086 |
| | b-jpge-2pass | 0.872 | 0.952 |
| | b-jpge-std | 1.006 | 1.007 |
| | c-stb | 0.997 | 0.994 |
| | d-toojpeg | 0.997 | 0.994 |
| key-noise (4) | b-jpge / 2pass / std | 1.188 / 1.025 / 1.003 | 1.041 / 0.919 / 1.003 |
| | c-stb, d-toojpeg | 0.998 | 0.999 |
| strip-real (30) | b-jpge / 2pass / std | 1.133 / 0.997 / 1.013 | 1.052 / 0.926 / 1.011 |
| | c-stb, d-toojpeg | 0.994 | 0.995 |
| strip-noise (3) | b-jpge / 2pass / std | 1.192 / 1.042 / 1.003 | 1.043 / 0.935 / 1.003 |
| | c-stb, d-toojpeg | 0.999 | 0.999 |
| screen-real (3) | b-jpge / 2pass / std | 1.069 / 0.970 / 1.007 | 1.100 / 0.994 / 1.007 |
| | c-stb, d-toojpeg | 0.998 | 0.997 |
| screen-noise (3) | b-jpge / 2pass / std | 1.192 / 1.040 / 1.003 | 1.042 / 0.933 / 1.003 |
| | c-stb, d-toojpeg | 0.999 | 0.999 |

stb and TooJpeg emit the same entropy-coded sizes almost everywhere (both descend from Jon Olick's jo_jpeg: Annex K
tables, the same float AAN DCT).

### 4.2 PSNR at q = 90 (mean dB; difference to turbojpeg)

| Group | a-turbojpeg | b-jpge | b-jpge-std | c-stb | d-toojpeg |
|---|---:|---:|---:|---:|---:|
| key-real 72 / 96 / 112 / 120 | 38.00 / 39.62 / 39.99 / 39.97 | +1.28 / +1.36 / +1.39 / +1.29 | -0.02 / +0.01 / +0.03 / +0.03 | -0.01 / +0.01 / +0.05 / +0.05 | -0.01 / +0.01 / +0.05 / +0.05 |
| key-noise | 30.0 | -3.1 ... -3.3 | ~0 | ~0 | ~0 |
| strip-real 1200 / 800 / Neo | 39.65 / 38.80 / 36.36 | -1.20 / -0.99 / -1.07 | +0.01 / -0.03 / -0.09 | 0.00 / +0.01 / -0.06 | 0.00 / +0.01 / -0.06 |
| strip-noise | 30.0 | -3.0 ... -3.1 | ~0 | ~0 | ~0 |

(b-jpge-2pass has the PSNR of b-jpge: only the Huffman tables differ.)

### 4.3 + XL key (112 px, the 27 theme icons) by quality: mean bytes, HID reports, PSNR

| q | turbojpeg bytes / reports / PSNR (min) | TooJpeg bytes / reports / PSNR (min) |
|---:|---|---|
| 85 | 3632 / 4.07 / 38.35 (36.19) | 3615 / 4.04 / 38.33 (36.26) |
| 90 | 4261 / 4.63 / 40.44 (38.43) | 4239 / 4.56 / 40.39 (38.42) |
| 92 | 4584 / 5.11 / 41.67 (39.75) | 4557 / 5.11 / 41.60 (39.57) |
| 95 | 5539 / 5.93 / 44.37 (42.47) | 5509 / 5.89 / 44.20 (42.23) |
| 98 | 7371 / 7.78 / 49.74 (47.22) | 7320 / 7.74 / 49.74 (47.12) |

## 5. Binary size and dependencies (M1 Ultra, Release, LTO)

| | turbojpeg (before) | TooJpeg (after) | Delta |
|---|---:|---:|---:|
| `unreal-streamdeck` | 2,059,648 B | 2,077,184 B | +17,536 B |
| stripped | 1,430,760 B | 1,446,616 B | +15,856 B |
| `__TEXT` | 1,327,104 B | 1,343,488 B | +16 KB |
| `sdctl` | 824,968 B | 842,680 B | +17,712 B |
| dynamic libraries | + `libturbojpeg.0.dylib` (734,624 B, built for macOS 15) | system frameworks and libc++ only | -1 dylib |
| `ld: warning: building for macOS-12.0, but linking with dylib ... built for newer version 15.0` | on every link of the companion | gone (only the POC harness, which links turbojpeg on purpose, still prints it) | |

## 6. Hardware: Stream Deck + XL (PID 0x00C6, FW 1.0.2.2, serial AD4MA62512RPEP), M1 Ultra host

| Step | Command | What the deck got | Result |
|---|---|---|---|
| H1 | `poc032-jpegbench device --hold 5` | full screen (0x08, 800x1280 rotated panel) from each of the 6 variants in turn (238,945-262,716 B); then two rounds of 4 key rows (rows: turbojpeg / jpge-2pass / jpge-std / jpge, then turbojpeg / jpge / stb / TooJpeg, row label in column 0) and the six 200x100 strip tiles of the loading scene from alternating variants (labelled), all q90 4:4:4 | 0 HID write failures |
| H2 | `sdctl theme data/streamdeck/themes/unrealng` (product build, TooJpeg) | 36 keys, 239 reports, encode 59 ms (incl. PNG fit), upload 136 ms | ok |
| H3 | `sdctl lcd-scene loading --animate 3 --fps 30`, `lcd-scene status` (TooJpeg) | strip, 30 fps | 29.6 fps; status 43 reports, 18.8 ms |
| H4 | `sdctl bench keys --keys all --content gradient --seconds 2` (TooJpeg q90) | bench pattern on all 36 keys | 385.8 img/s, 4126 B / 5.0 reports per frame, host encode 607 us per 36-key frame |
| H5 | loading scene A/B, `scratch/.../baseline/sdctl` (turbojpeg) vs the new `sdctl`, twice, interleaved, load average ~80 | strip | 28.7 / 29.4 fps (turbojpeg) vs 29.3 / 30.0 fps (TooJpeg) at a 30 fps target: no regression visible through the load noise |
| **V1** | the owner looked at the deck after H4/H5 | keys: **visible JPEG artifacts**; strip: fine | **failed** visual check, see below |
| H6 | `keys-compare --rows d-toojpeg:90,d-toojpeg:95,d-toojpeg:98,d-toojpeg:100` | rows 0-3 = TooJpeg q90 / q95 / q98 / q100, 4:4:4, the same 8 icons (bug ... eye) in columns 1-8 | 4494 / 5881 / 7874 / 10428 B per key; shown about a second, then replaced by H7 |
| H7 | `keys-compare --rows a-turbojpeg:90,d-toojpeg:90,a-turbojpeg:95,d-toojpeg:95` (left on the deck) | **row 0 = keys 0-8: turbojpeg q90; row 1 = keys 9-17: TooJpeg q90; row 2 = keys 18-26: turbojpeg q95; row 3 = keys 27-35: TooJpeg q95**; key 0 / 9 / 18 / 27 = the label key, columns 1-8 the icons bug, camera, check, chip, cross, disasm, disk, eye; all 4:4:4, product key path (FitImage 144 -> 112 triangle filter, rotate 270) | 4522 / 4494 / 5908 / 5881 B per key; 0 HID write failures |
| **V2** | the owner looked at the deck showing H7 | "the keys now decode and look fine; the earlier artifacts are gone" | **passed** visual check for all four rows: turbojpeg and TooJpeg at q90 and q95, 4:4:4, on the theme icons |

### V1: what the keys showed and the cause

The last key write before V1 was H4, `sdctl bench keys --content gradient` with the new TooJpeg build: **TooJpeg, q90,
4:4:4** (subsampling checked: SOF0, all components 1x1), each frame drawn at 144x144 and resized to 112 (triangle
filter), then rotated 270, the same quality as the strip (q90; the key path does not use a lower quality). That bench
pattern is a synthetic stress image (`sdctl/bench.cpp` `MakeFrame`): its red ramp wraps from 255 to 0 (`& 0xff`)
across the key, a hard-edged white bar, and small black frame-number digits: hard, saturated colour edges, which ring
at q90 in any baseline JPEG. Before H4 the deck had also shown H1-H3 (all q90 4:4:4); which of them the owner saw is
not certain, but H4's pattern stayed on the keys the longest.

What changed between V1 and V2: only the **content** (the theme icons instead of the bench pattern) and, for rows 2-3,
the quality (q95). Encoder (TooJpeg and turbojpeg both on screen), subsampling (4:4:4), resize and rotation were the
same. So the artifacts came from the bench's synthetic pattern at q90, not from TooJpeg and not from a decoder
problem: the owner found the q90 TooJpeg row (keys 9-17) as clean as the turbojpeg rows. Both encoders produce the same
bytes and PSNR within 1 % on these keys (section 4.3).

Not separated by the owner's look: whether q90 and q95 differ visibly on the icons (all four rows were judged together).
