# 037 — results

Measured 2026-10-26. Metric definitions: [`design.md`](design.md) section 4. Raw logs:
`scratch/2026-10-26-poc-037/{mac,win,smoke*,diag}` (not in git). Rerun:
`python3 -I analyze.py --detail <csv...>`.

Deviation from the charter: runs are 20 s (Mac) and 30 s (Windows), not 60 s. The machines were shared, and these
lengths already give 900-1700 frames per run. The MacBook Pro (ProMotion) was not available, so its column is open.

## 1. Mac Studio, DELL U4320Q, fixed 60 Hz

Qt 6.9.3, SDL 3.4.18, macOS 15.8, M1 Ultra, load average 26-57 during the runs.

| run | time | rate | refresh | frames | never/min | irregular/min | back | \|dur-1/R\| p50/p99 ms | latency p50/p99 ms | latency span ms (V) | producer jitter p99 ms | cpu ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| qt-gl-a (today) | proxy:swap | 50.080 | 59.85 | 905 | 0.0 | 0.0 | 0 | 3.36 / 14.34 | 9.2 / 17.8 | 16.7 (16.7) | 1.17 | 0.51 |
| qt-gl-b | proxy:swap | 50.080 | 60.01 | 881 | 0.0 | 0.0 | 0 | 3.36 / 14.40 | 9.2 / 17.7 | 16.8 (16.7) | 0.89 | 0.48 |
| sdl-gpu-b | proxy:swap | 50.080 | 59.96 | 901 | 0.0 | 0.0 | 0 | 3.36 / 14.77 | 8.3 / 16.8 | 16.5 (16.7) | 0.90 | 0.18 |
| sdl-native-b, CADisplayLink | presented | 50.080 | 60.00 | 899 | 6.7 | 3.3 | 0 | 3.30 / 13.37 | 67.8 / 96.8 | 62.6 (16.7) | 0.90 | 0.18 |
| sdl-native-c-at | presented | 50.080 | 60.00 | 903 | 0.0 | 3.3 | 0 | 3.30 / 13.37 | 28.1 / 45.9 | 26.2 (16.7) | 0.92 | 0.18 |
| sdl-native-c-at | presented | 48.828 | 60.00 | 881 | 0.0 | 0.0 | 0 | 3.81 / 12.86 | 28.7 / 46.5 | 26.5 (16.7) | 0.92 | 0.18 |
| qt-native-c-at | presented | 50.080 | 60.00 | 896 | 0.0 | 10.1 | 0 | 3.30 / 13.37 | 28.4 / 46.0 | 26.3 (16.7) | 1.76 | 0.21 |
| sdl-native-d, CADisplayLink | presented | 58.147 | 60.00 | 1080 | 0.0 | 0.0 | 0 | 0.53 / 0.53 | 32.7 / 49.5 | 17.6 (16.7) | 1.51 | 0.20 |
| sdl-native-d, CADisplayLink | presented | 59.359 | 60.00 | 1081 | 0.0 | 0.0 | 0 | 0.18 / 0.18 | 65.7 / 66.2 | 3.5 (16.7) | 1.66 | 0.22 |
| sdl-native-d, CADisplayLink | presented | 61.133 | 60.00 | 1079 | 0.0 | 3.3 | 0 | 0.31 / 0.31 | 49.3 / 66.1 | 17.6 (16.7) | 1.12 | 0.20 |

Display-locked, the DRC model on the real ticks:

| source | ticks | tick jitter p99 | machine speed | ring error, trim ±0.5 % | ring error, trim ±4 % (trim reached) |
|---|---|---|---|---|---|
| 58.147 (VGA 0) | 59.996 Hz | 0.267 ms | +3.18 % | 480 ms | 1.4 ms (-3.08 %) |
| 59.359 (VGA 1) | 59.990 Hz | 1.144 ms | +1.06 % | 102 ms | 6.4 ms (-1.04 %) |
| 61.133 (VGA 2) | 59.996 Hz | 0.117 ms | -1.86 % | 247 ms | 1.4 ms (+1.90 %) |

Earlier 6 s runs at a higher load (load average 50-90), for the API choices only:

| run | latency p50/p99 ms | note |
|---|---|---|
| sdl-native-b, CAMetalDisplayLink | 59.9 / 81.8 | the link's target is 3 refreshes ahead, even with `preferredFrameLatency = 1` |
| sdl-native-b, CADisplayLink | 41.7 / 49.6 | |
| sdl-native-c-link (CAMetalDisplayLink, `preferredFrameRateRange` = the source rate) | 58.1 / 66.5 | on a fixed display the range changes nothing |
| sdl-native-c-min (`afterMinimumDuration:` one period) | 122.9 / 133.9 | 30 fps on 60 Hz: 1202 frames never shown per minute. Wrong API |
| sdl-native-d, CAMetalDisplayLink, 61.133 | 48.9 / 49.4 | tick jitter p99 0.056 ms |

Reading:

- On a fixed 60 Hz display every variant shows the beat: a 50.08 Hz frame stays for one or two refreshes,
  |dur - 1/R| p99 = 13.4 ms. No presenter removes it (README of the feature, section 1). None made it worse: irregular
  0-10 per minute, never 0, back 0.
- `c-at` has the lowest latency of the variants with a real screen time (28 ms p50, a period plus 8 ms).
- `d-locked` is the only variant without judder on a fixed display: |dur - 1/R| ≤ 0.5 ms, one frame per refresh. Its
  ticks are steady (0.06-1.1 ms p99). The DRC holds the ring within 1.4-6.4 ms with a ±4 % trim, and runs away with
  today's ±0.5 %.
- The macOS display links add depth: 2-4 refreshes between a tick and the screen.
- The GL and `SDL_GPU` rows have proxy times only. Their latency is the time to the swap's return, not to the screen.

## 2. Windows host, Dell AW3821DW, 144 Hz, G-Sync off for windows

Qt 6.9.3 MinGW, SDL 3.4.18, D3D11 on an RTX 4090 (driver 610.88), session 1 on the physical console. The screen time
is the first kernel vblank after the present (design.md section 3). In every run below the kernel's vblank interval was
6.943-6.945 ms p50, 6.74-7.14 ms p1-p99: a fixed 144 Hz grid.

| run | rate | frames | never/min | irregular/min | back | \|dur-1/R\| p50/p99 ms | latency p50/p99 ms | latency span ms (V) | refreshes per frame |
|---|---|---|---|---|---|---|---|---|---|
| qt-gl-a (today) | 50.080 | 1402 | 0.0 | 38.6 | 0 | 0.87 / 7.65 | 5.6 / 9.6 | 7.6 (6.9) | 2: 193, 3: 1190, 4: 18 |
| qt-gl-a (today) | 48.828 | 1368 | 0.0 | 102.9 | 0 | 0.36 / 7.30 | 5.7 / 9.8 | 7.8 (6.9) | 2: 117, 3: 1202, 4: 48 |
| qt-gl-b | 50.080 | 1402 | 0.0 | 4.3 | 0 | 0.87 / 6.23 | 17.1 / 20.5 | 6.8 (6.9) | 2: 177, 3: 1222, 4: 2 |
| sdl-gpu-b | 50.080 | 1402 | 0.0 | 32.2 | 0 | 0.87 / 7.66 | 9.6 / 13.1 | 7.2 (6.9) | 2: 189, 3: 1197, 4: 15 |
| sdl-native-b (waitable) | 50.080 | 1402 | 0.0 | 10.7 | 0 | 0.87 / 6.23 | 10.4 / 13.8 | 6.8 (6.9) | 2: 179, 3: 1217, 4: 5 |
| sdl-native-b (waitable) | 48.828 | 1369 | 0.0 | 42.8 | 0 | 0.35 / 7.28 | 10.4 / 13.7 | 6.8 (6.9) | 2: 90, 3: 1258, 4: 20 |
| sdl-native-b (waitable) | 61.133 | 1713 | 0.0 | 6.4 | 0 | 2.49 / 4.63 | 10.4 / 13.8 | 6.8 (6.9) | 1: 2, 2: 1101, 3: 608, 4: 1 |
| qt-native-b (waitable) | 50.080 | 1402 | 0.0 | 15.0 | 0 | 0.87 / 6.23 | 10.3 / 13.8 | 6.9 (6.9) | 1: 2, 2: 176, 3: 1218, 4: 5 |
| sdl-native-c-at (ALLOW_TEARING) | 50.080 | 1403 | 0.0 | 40.7 | 0 | 0.87 / 7.79 | 17.7 / 22.6 | 8.7 (6.9) | 2: 194, 3: 1189, 4: 19 |

The ideal for 50.08 Hz on 144 Hz is 2.875 refreshes per frame: 7 frames for 3 refreshes and 1 for 2, so 12.5 % twos
(175 of 1402) and no fours.

Reading:

- The waitable-object presenter (`b`) is closest to the arithmetic: 176-179 twos and 2-5 fours. Today's pattern
  (`a`) has 3.6 times as many frames outside the pattern at 50.08 Hz and 2.4 times at 48.83 Hz.
- `c-at` on a fixed 144 Hz display is a `b` with a worse pick: a present between refreshes waits for the next one.
- At 144 Hz the beat's amplitude is a 7 ms refresh, a third of what it is on 60 Hz. The latency through DXGI is
  10.4 ms p50 to the first vblank; DWM may add one refresh on top.

VRR checks before the driver setting was changed (10 s each, the same 6.944 ms grid in all of them):

| mode | result |
|---|---|
| window, `c-at`, `Present(0, ALLOW_TEARING)` | the fixed 144 Hz grid; `GetFrameStatistics` all zeros (composed by DWM) |
| borderless fullscreen (SDL), `c-at` and `b` | the fixed grid; `GetFrameStatistics` works (independent flip): `PresentCount` follows `GetLastPresentCount`, `PresentRefreshCount` = `SyncRefreshCount` |
| exclusive fullscreen (`SetFullscreenState`) | refused when started from the scheduled task (the window is not in the foreground); ran as borderless |

The app's side of VRR was complete; the driver allowed G-Sync for exclusive fullscreen only. The owner enabled it for
windowed mode, and section 2a was measured after that.

## 2a. Windows host, G-Sync on for windowed and fullscreen

The same host, 20 s per run. The screen time is the kernel's vblank after the present. Under VRR the panel scans out
at the 144 Hz speed (6.94 ms) and then holds the vblank until the next flip. So the vblank comes 6.94 ms after the
flip, and the intervals between vblanks are the frames' times on screen. DXGI's statistics now work in a window too
(independent flip), but their `SyncQPCTime` is the start of the vblank the flip ended. That is 8-13 ms before the
present, so it is not used.

Who gets VRR (the kernel's vblank interval p50):

| run | vblank interval p50 | the panel runs at |
|---|---|---|
| qt-gl-a (today), qt-gl-b, sdl-gpu-b, sdl-native-b | 6.944-6.948 ms | 144 Hz: a vsync present every refresh, or a GL window composed by DWM |
| sdl-native-c-at / qt-native-c-at, 50.080 | 19.94-19.98 ms | 50.1 Hz |
| sdl-native-c-at / qt-native-c-at, 48.828 | 20.44-20.47 ms | 48.9 Hz |
| sdl-native-c-at, 49.360 (Next 50 Hz) | 20.25 ms | 49.4 Hz |
| sdl-native-c-at, 61.133 (Next VGA 2) | 16.27-16.32 ms | 61.3 Hz |

Today's Qt GL path does not get VRR even with G-Sync on for windows: the panel stays at 144 Hz.

The error of each frame's time against the source period (`vrr_err.py`). The present intervals are
the presenter's own timing; the screen intervals are measured by the vblanks:

| run | target | present interval error p50 / p99 / max ms | on-screen error p50 / p99 / max ms | frames off by > 1.5 ms |
|---|---|---|---|---|
| sdl-native-c-at, 50.080 | latch + period | 0.34 / 1.24 / 1.65 | 0.53 / 3.15 / 5.04 | 86 of 902 |
| qt-native-c-at, 50.080 | latch + period | 0.31 / 1.16 / 1.73 | 0.48 / 3.45 / 8.74 | 52 of 901 |
| sdl-native-c-at, 50.080, borderless | latch + period | 0.32 / 1.16 / 1.72 | 0.47 / 2.34 / 5.06 | 55 of 903 |
| sdl-native-c-at, 48.828 | latch + period | 0.39 / 1.24 / 1.64 | 0.57 / 3.58 / 5.80 | 77 of 878 |
| sdl-native-c-at, 49.360 | latch + period | 0.31 / 1.26 / 2.19 | 0.50 / 3.16 / 4.66 | 92 of 888 |
| sdl-native-c-at, 61.133 | latch + period | 0.35 / 1.18 / 2.38 | 0.88 / 3.43 / 7.57 | 299 of 1102 |
| sdl-native-c-at, 50.080 | source clock | 0.05 / 0.54 / 0.65 | 0.22 / 1.72 / 5.08 | 26 of 902 |
| sdl-native-c-at, 50.080, vblank thread time-critical | source clock | 0.05 / 0.49 / 0.61 | 0.20 / 1.83 / 3.81 | 19 of 902 |
| sdl-native-c-at, 48.828 | source clock | 0.13 / 0.53 / 0.67 | 0.30 / 2.95 / 3.60 | 57 of 880 |
| sdl-native-c-at, 61.133 | source clock | 0.16 / 0.53 / 0.61 | 0.52 / 3.46 / 7.44 | 170 of 1102 |
| sdl-native-c-at, 61.133, vblank thread time-critical | source clock | 0.15 / 0.53 / 0.74 | 0.55 / 2.63 / 4.12 | 184 of 1102 |

For comparison, the beat on a fixed display: |dur - 1/R| p99 = 6.2-7.8 ms at 144 Hz, 13.4 ms at 60 Hz.

Reading:

- VRR works from a plain window: one refresh per frame, at the machine's rate, Pentagon and the Next included.
- Targets on the source clock (the feature's `FrameClock`) beat latch-based ones: the present intervals go from 1.2
  to 0.5 ms p99, the median screen error from ~0.5 to ~0.2 ms.
- The on-screen tail (p99 1.7-3.5 ms) is larger than the presents' (0.5 ms). It did not shrink with the measuring
  thread at time-critical priority. So it is either the kernel's delivery of the vblank event or the panel itself.
  Telling the two apart takes an optical measurement (a photodiode or a high-speed camera on the scroller).
- Latency, latch to the end of scan-out: 27.4-28.8 ms = the period (D = one period in the POC) + 6.9 ms of
  scan-out + ~0.5 ms.

Lab fixes found on the way (all in the code now):

- MinGW's `sleep_for` granularity: producer jitter 24.6 ms p99, until the high-resolution waitable timer.
- The frame-latency waitable object is lost if it is consumed without a present. The first `b` runs had 0 presents.
- A sleeping monitor has no vblanks, and DWM composes on a ~60 Hz timer (DWM's `qpcVBlank` intervals: 16 ms p50).
  `KeepDisplayOn()` fixed it.
- `IDXGIOutput::WaitForVBlank` returns at once in a window (0.8 µs intervals); the kernel's vblank event works.

## 3. Against the charter

| Goal | Result |
|---|---|
| 1. Baseline | Mac 60 Hz: on par with `b` (proxy timing). Windows 144 Hz: 39-103 frames per minute outside the beat pattern, against 11-43 for `b` |
| 2. Vsync presenter | the arithmetic beat, nothing worse: Mac irregular 0-3 / min, Windows near the ideal histogram. `CADisplayLink` over `CAMetalDisplayLink` for latency |
| 3. VRR | G-Sync: a window gets one refresh per frame at 48.8-61.3 Hz with `Present(0, ALLOW_TEARING)` at the frame's time; on-screen error p50 0.2-0.55 ms, p99 1.7-3.5 ms (source-clock targets). On a fixed display it degrades to `b` (Mac `presentDrawable:atTime:`). Today's GL path stays at 144 Hz under G-Sync. ProMotion not measured |
| 4. Display-locked | tick jitter 0.06-1.1 ms p99 (criterion < 0.5 ms met for VGA 0 and VGA 2, not for VGA 1 at 1.14 ms); the DRC needs ±4 % for Next 60 Hz on 60 Hz and holds the ring within 6.4 ms then. Latency through the macOS links is 2-4 refreshes |
| 5. Qt 6 vs SDL3 | equal with the same native presenter (Mac `c-at` 28.4 / 28.1 ms, Windows `b` 10.3 / 10.4 ms). `SDL_GPU` has neither present-at-time nor feedback. Qt stays; the presenter is native under either shell |

Decision rules of the charter:

- `b` replaces `a`: excess judder is lower (Windows, 3.6x) or equal (Mac). The latency rule is not met formally on
  Windows: `b` is 4.8 ms behind `a` (10.4 against 5.6 ms p50), the rule allows half a refresh (3.5 ms at 144 Hz). On
  the Mac the latencies are not comparable (GL has proxy times only). Proposed anyway, because the judder is what the
  user sees and 4.8 ms is under one 144 Hz refresh; the owner decides.
- `c` as the VRR default: the rule (duration error p99 < 1 ms) is met by the presents (0.5 ms) and by the median on
  screen (0.2-0.55 ms), not by the on-screen p99 as measured through the vblank events (1.7-3.5 ms). That is a
  quarter of the fixed 60 Hz beat and half of the 144 Hz one. Proposed as the VRR default; an optical measurement
  would settle the tail.
- `d` as an option: the jitter rule (< 0.5 ms) is met for VGA 0 and VGA 2, not for VGA 1 (1.14 ms, one run); the DRC
  rule is met with the limit widened to ±4 % in that mode. Proposed as an opt-in, VGA 1 to be re-measured.
- Shell: Qt.
