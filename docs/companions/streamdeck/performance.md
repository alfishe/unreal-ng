# Stream Deck performance

> **Note:** these numbers were measured with **4:2:0** JPEG, which the Plus XL keys turned out to display as garbage. The library now encodes **4:4:4**, so key frames are roughly a third larger; re-run `sdctl bench matrix` / `bench lcd` to refresh the tables. The transaction cost (~0.4 ms per report, ~2,400 reports/s) does not depend on the encoding.

Everything here was measured with `sdctl bench ...` / `sdctl lcd-scene --animate` unless marked **(user-observed)** or **(structural)**.

**Setup**: Stream Deck + XL (PID 0x00C6, FW 1.0.2.2), USB 2.0 high-speed, macOS (Darwin 24.6, Apple silicon), hidapi 0.15 (IOHID backend), libjpeg-turbo 3.2 (since 2026-10-20 the library encodes with its own `sdb::jpeg`: turbojpeg's scan data, 1.37x its encode time, [POC 032](../../../tools/poc/032-streamdeck-jpeg-encoder/measurements-2026-10-20-optimised.md)), Release build. No other Stream Deck software was running.

**Method**: the loop calls blocking `hid_write()`; frames are pre-encoded so JPEG cost is excluded unless stated. Images are 144×144 source art resized to the key's 120×120. "Content": *flat* = one solid colour per frame, *gradient* = smooth colour field with a moving bar and a counter (typical UI art), *noise* = random pixels (worst case for JPEG). One cell = one second, so values carry about ±5 % jitter.

**Reading the numbers**: `hid_write` returns when the USB stack has handed the report to the device, so sustained throughput approximates what the device accepts. It does **not** prove the panel visibly redraws every frame (see Caveats).

## 1. Headline

| | `sdctl` |
|---|---|
| Animated key rate | up to **~860 img/s** for 1-report frames; **~24 fps on all 36 keys** at once |
| Whole 36-key panel, typical art | **~21-24 fps** every key (q60 gradient) |
| Full 1200x100 strip | **~128 updates/s** pre-rendered; **54-60 fps** rendering scenes live |
| Cold start: upload a complete 36-key theme | **57 ms** upload + 7 ms encode = 64 ms |
| Code path per frame | render in C++ -> JPEG encode -> `hid_write` |

The device is the limit: it accepted ~800 key images per second in the loop.

## 2. Device-side limits

| Quantity | Measured |
|---|---|
| Cost of one USB transaction, output report | ~0.39-0.45 ms (p50), nearly independent of size |
| Cost of one USB transaction, feature report | 0.41 ms mean (p99 0.60 ms, max 0.66 ms) |
| Transactions per second | **~2,400** (both types) |
| Sustained bandwidth | **~2.3-2.4 MB/s** once reports are full (about 1 KB each) |
| Brightness commands/s | 2,424 |

**Rule of thumb**: `frames/s = 2400 / (reports per frame x frames per tick)`; reports per frame = `ceil(JPEG bytes / 1016)` for keys (1008 for LCD regions).

The 0.4 ms floor may be a macOS IOHID scheduling cost rather than the firmware. Repeat on Linux/Windows before quoting it as hardware.

## 3. Key images: full matrix

Frame size in bytes, reports per image, images/s summed over keys written, and fps per key.

| Content | JPEG q | Keys | Frame B | Reports | img/s total | fps/key | MB/s | p50 write us |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| flat | 30 | 1 | 883 | 1.0 | 882.8 | 882.8 | 0.90 | 393 |
| flat | 30 | 8 | 883 | 1.0 | 754.9 | 94.4 | 0.77 | 444 |
| flat | 30 | 36 | 883 | 1.0 | 762.6 | 21.2 | 0.78 | 447 |
| flat | 60 | 1 | 884 | 1.0 | 839.0 | 839.0 | 0.86 | 393 |
| flat | 60 | 8 | 884 | 1.0 | 779.9 | 97.5 | 0.80 | 428 |
| flat | 60 | 36 | 884 | 1.0 | 781.3 | 21.7 | 0.80 | 421 |
| flat | 90 | 1 | 885 | 1.0 | 804.0 | 804.0 | 0.82 | 404 |
| flat | 90 | 8 | 885 | 1.0 | 814.8 | 101.9 | 0.83 | 394 |
| flat | 90 | 36 | 885 | 1.0 | 804.8 | 22.4 | 0.82 | 407 |
| gradient | 30 | 1 | 1636 | 2.0 | 767.6 | 767.6 | 1.57 | 391 |
| gradient | 30 | 8 | 1636 | 2.0 | 779.3 | 97.4 | 1.60 | 397 |
| gradient | 30 | 36 | 1636 | 2.0 | 781.8 | 21.7 | 1.60 | 390 |
| gradient | 60 | 1 | 2007 | 2.6 | 920.7 | 920.7 | 2.47 | 380 |
| gradient | 60 | 8 | 2007 | 2.6 | 629.9 | 78.7 | 1.69 | 398 |
| gradient | 60 | 36 | 2007 | 2.6 | 834.6 | 23.2 | 2.24 | 400 |
| gradient | 90 | 1 | 3228 | 3.9 | 555.2 | 555.2 | 2.24 | 404 |
| gradient | 90 | 8 | 3228 | 3.9 | 556.9 | 69.6 | 2.25 | 406 |
| gradient | 90 | 36 | 3228 | 3.9 | 590.1 | 16.4 | 2.38 | 386 |
| noise | 30 | 1 | 3164 | 4.0 | 550.6 | 550.6 | 2.26 | 397 |
| noise | 30 | 8 | 3164 | 4.0 | 564.7 | 70.6 | 2.31 | 395 |
| noise | 30 | 36 | 3164 | 4.0 | 585.9 | 16.3 | 2.40 | 383 |
| noise | 60 | 1 | 5208 | 6.0 | 388.9 | 388.9 | 2.39 | 391 |
| noise | 60 | 8 | 5208 | 6.0 | 386.2 | 48.3 | 2.37 | 393 |
| noise | 60 | 36 | 5208 | 6.0 | 375.9 | 10.4 | 2.31 | 390 |
| noise | 90 | 1 | 11378 | 12.0 | 187.5 | 187.5 | 2.30 | 398 |
| noise | 90 | 8 | 11378 | 12.0 | 183.1 | 22.9 | 2.25 | 409 |
| noise | 90 | 36 | 11378 | 12.0 | 182.4 | 5.1 | 2.24 | 402 |

Observation: p50 per-report latency stays ~0.39-0.45 ms in every row. Throughput follows the report count, not the key count and not the byte size per report. (The 8-key gradient q60 cell, 629.9, is the one low outlier in a 1-second sample.)

### Full-panel frame budget (all 36 keys, repeated 2-s runs)

| Content | JPEG q | Frame B | Reports/img | img/s | **fps per key** |
|---|---:|---:|---:|---:|---:|
| flat | 60 | 884 | 1.0 | 858.9 | **23.9** |
| gradient | 60 | 2007 | 2.0 | 748.2 | **20.8** |
| gradient | 90 | 3228 | 3.9 | 589.1 | **16.4** |
| noise | 90 | 11378 | 12.0 | 191.4 | **5.3** |

## 4. Horizontal display (1200x100, device space 100x1200)

Pre-rendered images, 3 s per cell (`sdctl bench lcd`).

| Operation | Content | Updates/s | MB/s | Reports/update | Mean write time |
|---|---|---:|---:|---:|---:|
| Full strip | gradient | 127.7 | 2.09 | 16.0 | 7.0 ms |
| Full strip | noise | 19.0 | 2.20 | 113.1 | 51.3 ms |
| One 200x100 dial tile | gradient | 262.1 | 1.07 | 4.0 | 3.6 ms |
| One 200x100 dial tile | noise | 105.6 | 2.16 | 20.0 | 9.2 ms |
| 100x100 block | gradient | 270.6 | 0.83 | 3.0 | 3.6 ms |
| 100x100 block | noise | 176.6 | 1.99 | 11.0 | 5.5 ms |
| 50x50 block | gradient | 1027.7 | 2.10 | 2.0 | 0.9 ms |
| 50x50 block | noise | 523.5 | 2.14 | 4.0 | 1.8 ms |

Region writes pay only for the pixels they touch, so a 50x50 meter update is ~8x cheaper than redrawing the strip.

## 5. Live-rendered scenes (render + rotate + JPEG + write, in one process)

`sdctl lcd-scene <scene> --animate 3 --fps 60`. "Late" = frames that missed their 16.7 ms slot.

| Scene | Target fps | Achieved fps | Late frames / 180 | Notes |
|---|---:|---:|---:|---|
| loading (tape stripes, random widths) | 60 | 54.3 | 180 | heaviest: noisy stripes -> big JPEGs |
| marquee (rainbow scroll) | 60 | 60.0 | 82 | |
| status banner (flag stripes) | 60 | 60.0 | 65 | |
| ttd timeline | 60 | 60.0 | 22 | |
| volume, single tile | 60 | 60.0 | 0 | 200x100 region write |
| loading | 20 | 20.0 | 0 | the rate the demo uses |
| status / ttd | 20 | 20.0 | 0 | |
| marquee / volume tile | 30 | 30.0 | 0 | |

The whole strip sustains 54-60 fps of live-rendered content.

## 6. Host cost per key frame

| Stage | Cost |
|---|---|
| Resize 144 -> 120 px + JPEG encode (q60-q90) | 0.38-0.57 ms per key |
| Same for a whole 36-key panel | ~14-20 ms |
| Whole-panel USB time (gradient q60) | ~48 ms (~20.8 fps) |

Encoding is not the bottleneck; USB transactions are. Pre-baking frames still removes the 14-20 ms for static content.

## 7. Responsiveness (`sdctl bench mixed`, 6 s)

| Metric | Reader alone | Reader while a second thread streams all 36 keys |
|---|---:|---:|
| Read-call gap p50 (10 ms timeout) | 11.99 ms | 11.82 ms |
| p99 | 13.13 ms | 12.91 ms |
| Max | 15.03 ms | 14.92 ms |
| Writer rate during concurrent reads | - | 786 img/s (21.8 fps/key) |

Concurrent `hid_read_timeout` + `hid_write` on one handle from two threads did not stall either side on macOS. Events seen: 0 (no one pressed keys during the run), so **press-to-host latency is not measured**.

## 8. Caveats

- Host timings, not photodiode timings: a device that accepts a frame and then drops or coalesces it would still look fast here. Check with `sdctl bench keys --keys 1 --fps 30` (the key shows a frame counter).
- Single device, single OS, single firmware. The 0.4 ms/transaction floor may differ elsewhere.
- Matrix cells are 1 s samples. Treat differences under ~10 % as noise.
