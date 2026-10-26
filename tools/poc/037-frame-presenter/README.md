# 037 — frame-presenter

Feature: [`docs/inprogress/2026-10-26-frame-presenter`](../../../docs/inprogress/2026-10-26-frame-presenter/README.md).
Lab: [`design.md`](design.md). Results: [`results.md`](results.md).

The emulated machines run at 48.83 Hz (Pentagon), 50.08 Hz (48K / 128K) and, on the ZX Next's 60 Hz timing, at 58.15 /
59.36 / 61.13 Hz (VGA 0 / 1 / 2 clocks). The host displays run at a fixed 60 Hz, at an adaptive 24-120 Hz (ProMotion) or
at a variable 1-144 Hz (G-Sync). Today the Qt app paints a frame when the emulator says one is ready, through a
`QOpenGLWindow` with swap interval 1; nothing ties the choice of frame to the display's refresh. This POC measures what
reaches the screen with today's path and with three replacements, in Qt 6 and in SDL3, on the three hosts.

## Goals

1. **Baseline.** On each host, what does today's pattern (paint on the frame-ready signal, `update()`, swap interval 1)
   put on the screen: how many frames are shown twice or never, how far from the unavoidable beat pattern, at what
   latency?
2. **Vsync presenter.** Does presenting the newest complete frame on every refresh (picked by its timestamp) give exactly
   the arithmetic beat pattern (50.08 Hz on 60 Hz: one repeat every 5 frames, evenly spaced), and nothing worse?
3. **VRR.** Can an application put each frame on the screen at its own time (one refresh per frame, at 48.83-61.13 Hz) on
   ProMotion (macOS, Metal) and on G-Sync (Windows, DXGI flip model)? Through which API, from a Qt window and from an
   SDL3 window? How close is the on-screen duration of each frame to 1 / rate?
4. **Display-locked.** When the machine's rate is close to the display's (Next VGA 0-2 on 60 Hz: -3.1 % / -1.1 % / +1.9 %),
   can the display's refresh drive the emulation (one frame per refresh) steadily, and what audio rate range does the
   DRC need then?
5. **Qt 6 vs SDL3.** Which shell gives the control goals 2-4 need (refresh callbacks with timestamps, present timing,
   presentation feedback) with less code, and what does each cost per frame?

## Requirements

- Hosts: the Mac Studio (M1 Ultra, macOS 15.8, DELL U4320Q 60 Hz), the MacBook Pro (ProMotion, up to 120 Hz), the
  Windows host (RTX 4090, G-Sync display, up to 144 Hz).
- Source rates: 48.828, 50.080, 58.15, 59.36, 61.13 and 60.00 Hz (a control), each from the machine's T-states per frame
  and its clock, the way `CalculateFrameDurationUs` / `PortDecoder_Next::FrameDurationUs` give them.
- A synthetic producer that behaves like `MainLoop`: an absolute-deadline clock at the source rate, a latch into a ring
  with the latch time and the frame number per slot, a small random emulation time per frame. No emulator core: the
  POC is about the display side.
- The picture: a horizontal scroller moving a fixed step per frame (judder is visible to the eye) and the frame number as
  a block code in a corner.
- Ground truth for every presented frame: the time the frame reached the screen.
  - macOS / Metal: `MTLDrawable.presentedTime` (`addPresentedHandler`).
  - Windows / DXGI: `IDXGISwapChain::GetFrameStatistics` (`SyncQPCTime` per present count), cross-checked with PresentMon
    where it is available.
  - OpenGL (today's path): no presentation feedback exists; the swap's return time is the proxy, marked as such.
- Variants, the same names in every file:
  - `a-signal`: today's pattern, paint on the producer's signal.
  - `b-vsync`: a refresh callback (display link) presents the newest complete frame.
  - `c-vrr`: each frame is presented at its latch time plus a fixed delay; the display follows.
  - `d-locked`: the refresh callback drives the producer (one frame per refresh); the rate ratio goes to a DRC model.
- Shells: Qt 6.9 (`QOpenGLWindow` for `a-signal` / `b-vsync` as in the app; a `QWindow` with a native Metal or DXGI
  presenter for `b`-`d`) and SDL3 3.4 (`SDL_GPU` for `b`; the same native presenter on the SDL window for `b`-`d`).

## Constraints

- No change to the emulator or the apps. POC code stays in this folder; adopting means re-implementing in the feature.
- No change of the user's display modes (the Mac Studio's displays offer 50 Hz only at lower resolutions; a 50 Hz panel
  mode is out of scope here).
- Windowed and borderless fullscreen only (the Qt app is windowed); exclusive fullscreen is noted, not measured.
- Runs of 60 s per (host, shell, variant, rate); the machines are shared, so CPU load is logged with each run.
- Raw logs in `scratch/2026-10-26-poc-037/` (per host), summaries in the measurement reports.

## Acceptance criteria / expected results

Metrics per run (definitions in [`design.md`](design.md)):

- **Excess judder**: frames whose time on screen differs from the ideal beat pattern for the two rates (per minute).
- **On-screen duration error** (VRR): |time on screen - 1 / source rate|, p50 / p99.
- **Latency**: latch to the frame reaching the screen, p50 / p99.
- **Tick jitter** (`d-locked`): refresh interval p99 - median.
- **Presenter cost**: CPU time per presented frame.

Expected:

| Variant | Fixed 60 Hz | ProMotion | G-Sync |
|---|---|---|---|
| `a-signal` | excess judder > 0 (the frame picked when Qt paints, not at the refresh) | same as 60 Hz or worse | same |
| `b-vsync` | excess judder 0; the beat pattern only | the beat pattern of the refresh the system chose | the beat pattern at 144 Hz (small: 2-3 refreshes per frame) |
| `c-vrr` | not applicable (falls back to `b`) | duration error p99 < 1 ms if the panel accepts arbitrary intervals; if ProMotion only offers 120 / n, the error is a refresh (8.3 ms) | duration error p99 < 1 ms in the G-Sync range (48.83 Hz may sit near its floor) |
| `d-locked` | tick jitter p99 < 0.5 ms; DRC range ±3.2 % for Next VGA 0-2 | — | — |

Decision rules:

- `b-vsync` replaces `a-signal` if its excess judder is 0 and the latency is no worse than `a-signal`'s by more than
  half a refresh.
- `c-vrr` is the default on a VRR display if the duration error p99 is under 1 ms; otherwise `b-vsync` stays the
  default there.
- `d-locked` becomes an option if the tick jitter is under 0.5 ms and the DRC model holds the audio ring within its
  band at the needed ratio.
- Qt vs SDL3: the shell that reaches `b`-`d` on all three hosts wins; if both do, the one with less platform code.
