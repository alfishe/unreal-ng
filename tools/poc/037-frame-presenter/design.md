# 037 — design of the lab

## 1. Parts

| Part | Files | Role |
|---|---|---|
| Producer | `common/lab.cpp` (`Producer`) | an absolute-deadline clock at the source rate (as `MainLoop`), on a thread with `MainLoop`'s real-time policy; per frame a random 0.5-1.5x `--work-ms` (default 1 ms) of "emulation", then a latch into the ring. Ticked mode (`d-locked`): the presenter's refresh releases one frame |
| Ring | `common/lab.cpp` (`FrameRing`) | 8 slots, each with the frame number and the latch time (`NowNs`) |
| Picture | `common/lab.cpp` | 384x288: a scroller 4 px per frame, the frame number as a 16-bit block code |
| Native presenters | `common/presenter_metal.mm`, `common/presenter_dxgi.cpp` | own thread; the variants of the charter; presentation feedback |
| Shells | `qt/main.cpp`, `sdl/main.cpp` | Qt 6: `QOpenGLWindow` (`a`, `b` as the app does) or a `QWindow` handed to the native presenter; SDL3: `SDL_GPU` (`a`, `b`) or the native presenter on the SDL window |
| Log | `common/lab.cpp` (`PresentLog`) | one CSV row per present, `# key=value` meta lines (host, display, load, presenter, feedback source) |
| Analysis | `analyze.py` | the metrics below, one markdown row per run |

## 2. Clocks

All timestamps are one clock per host: `mach_absolute_time` (= `CLOCK_UPTIME_RAW` = `CACurrentMediaTime`, the base of
`presentedTime` and the display links) on macOS, QPC on Windows (the base of `SyncQPCTime`). Sleeps on Windows use a
high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) and spin the last 0.7 ms: MinGW's
`sleep_for` has a ~15.6 ms granularity, which gave the producer a 24.6 ms p99 jitter in the first Windows run.

## 3. The screen time of a present

| Source | When | Marked |
|---|---|---|
| `MTLDrawable.presentedTime` via `addPresentedHandler` (0 = never shown) | Metal | `presented` |
| `IDXGISwapChain::GetFrameStatistics`: `PresentCount` -> `SyncQPCTime` | DXGI in independent flip (fullscreen) | `presented` / `vblank` |
| the first kernel vblank after the present returned (`D3DKMTWaitForVerticalBlankEvent` on a thread); a present followed by another one before that vblank is counted as dropped | Windows, every shell (DXGI's statistics are all zeros for a composed window) | `vblank` |
| the swap's return (`frameSwapped`) / the acquire's return | macOS GL / `SDL_GPU`: no feedback | `proxy:swap`, `proxy:acquire` |

The vblank source is a lower bound for a composed window: DWM may show the frame one refresh later. Windows runs call
`KeepDisplayOn()` first (`ES_DISPLAY_REQUIRED` and a zero mouse move): with the monitor asleep there are no vblanks,
and DWM composes on a ~60 Hz timer.

## 4. Metrics

Per run, after a 2 s settle. A frame's time on screen is the gap between its first screen time and the next distinct
frame's.

| Metric | Definition |
|---|---|
| never / min | frame numbers between the first and the last shown that were never shown |
| irregular / min | frames on screen for a refresh count outside floor / ceil of (display period / source period): worse than the beat pattern |
| back | a frame shown after a newer one |
| dropped / late | presents with screen time 0; presents shown more than half a refresh after their target (`c`, `d`; on a fixed display `c`'s targets fall between refreshes, so late is expected there) |
| \|dur - 1/R\| p50 / p99 | a frame's time on screen against the source period: the judder itself (13.4 ms on 60 Hz = a frame shown for two refreshes) |
| latency p50 / p99 | latch to screen |
| latency span (V) | latency p99 - p1 against the refresh period V: a span of V is the beat, more is jitter |
| producer jitter p99 | the latch interval against the source period |
| cpu | presenter thread CPU per present (`thread_info` / `GetThreadTimes`; the latter has a 15.6 ms granularity, so Windows values are not usable) |
| `d-locked` | the tick rate, its jitter (p99 of \|interval - median\|), the machine's speed against its own rate, and a model of the DRC (target 40 ms, KP 0.08, KI 0.0008, EMA 0.05, deadband 0.02, as `SoundManager`) fed with the real ticks: the worst ring error after the settle, at trim limits ±0.5 % and ±4 % |
