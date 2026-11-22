# Presenter stabilization: VRR, display rate, interpolation on every platform and content

Started 2026-11-22 on the branch `vulkan-presenter`, from a revision of everything the frame presenter does
([design](../2026-10-26-frame-presenter/README.md), on master since 2026-11-08) and of the Vulkan renderer the branch
adds ([design](../2026-11-19-vulkan-renderer/README.md)). Goal: VRR, the display rate switch and smooth motion steady
on macOS, Windows, Linux (X11, Wayland, GL and Vulkan) and the Steam Deck, on every kind of picture a machine makes.

The revision read the code (three passes: the Vulkan presenter, the sync state machine with the four presenters,
smooth motion with its tests and scenes). A finding below is from the code until its step reproduces it; the status
column says what was done. Steps and their state: [TODO.md](TODO.md).

## 1. Findings

`win` = `unreal-qt/src/widgets/devicescreenglwindow.cpp`, `dxgi` / `glt` / `metal` = the presenters under
`unreal-qt/src/platform/`, `vk` = `unreal-qt/src/gpu/vulkanpresenter.cpp`, `SM` = `core/src/emulator/video/smoothmotion.cpp`.

### 1.1 VRR decided wrongly (step 1)

| # | Finding | Where | Status |
|---|---------|-------|--------|
| V1 | DXGI started as "VRR" and left it only when the frames were more than 10 % off the display's rate: a 60 Hz machine on 60 Hz, 50.08 / 48.83 Hz on 50 Hz kept tearing presents on a fixed display for good | `dxgi` `CheckVrr` | fixed: starts on the grid, VRR when the vblanks are seen to follow (`vrrprobe.h`) |
| V2 | Every `setFrameSource` set the policy again, and DXGI / the timed GL swap took VRR anew: two seconds of tearing presents, smooth motion off, and with `display_rate=match` the mode restored and switched again (two blankings) on any settings or guest video mode change | `win` `setFrameSource`, `dxgi` / `glt` `SetPolicy` | fixed: the same policy again changes nothing |
| V3 | `display_rate=match` without smooth motion `auto` never saw the presenter's VRR change (its check sat behind the `auto` return, the timer did not run) | `win` `updateVideoSync` | fixed: watched whenever the display rate is `match` |
| V4 | A display mode switched while the presenter still measured VRR was switched back a second later on a VRR display | `win` `updateDisplayRate` | fixed: the switch waits for `vrrPending` |
| V5 | macOS takes a refresh range of the screen as VRR; ProMotion reports one and keeps its 120 Hz grid (frame presenter README, section 9: 16.7 / 25 ms, 7-12 % of frames lost) | `metal` `Vrr` | fixed in code: the steps between screen times are judged (`JudgeSteps`), a grid ends VRR; not measured (no ProMotion machine at hand) |
| V6 | Vulkan: policy `auto` never takes VRR (FIFO), `vrr_support` unknown; `x11::DecideWindowVrr` is not asked | `vk` `WantedMode`, stats | open (step 4) |
| V7 | Wayland, gamescope: no VRR query at all | — | open (step 5) |

Policy `vrr` forces each frame at its own time on every presenter (the user says the display follows): tearing on a
fixed display under it is the setting's meaning, not a defect.

### 1.2 A waiting present is not replaced (step 2)

Only DXGI drops a queued present when a later one is due. The timed GL swap keeps "at once" presents, Vulkan keeps
all, Metal queues drawables: a machine faster than a fixed display (50.08 Hz on 50 Hz after a rate switch, a 60 Hz
Next on 59.81 Hz), most of all with low latency, fills the queue: the GUI thread blocks a refresh per frame, two
refreshes of delay stay, the lead climbs to its bound. Open.

### 1.3 Smooth motion (step 3)

| # | Finding | Where |
|---|---------|-------|
| S1 | After a `Reset` (settings, VRR on / off, focus lost with a display rate switch) or a GUI stall the newest frame fed is three behind the one due: `Plan` answers with it, a step back for a refresh | `win` `feedSmooth`, `SM` `Plan` |
| S2 | Smooth motion stays on in turbo (the latch clock): eight frame copies under the frame source's mutex and eight flicker searches a refresh, for a picture that is always the frame | `win` `smoothActive` |
| S3 | No count of "the next frame was not there": a long lead (Wayland / Vulkan, a grown adaptive lead, the lip-sync trim, the core's temporal delay) eats the look-ahead and the judder returns unseen | `SM` `Plan` |
| S4 | `_smoothGpuFailed` is never reset: after one failed `Create` the steps run on the CPU until a restart | `win` |
| S5 | `_smoothDrained` / `_smoothLag` are not reset when the presenter is made anew | `win` |

### 1.4 Vulkan (step 4)

| # | Finding |
|---|---------|
| K1 | Restarts used up: `fallBackToGl` leaves the Vulkan renderer in place and `paintGL` returns early: an empty window for good (`dropVulkan` is the way down) |
| K2 | `PresentFrame` fails silently (acquire timeout, every image held): 100-200 ms of the GUI thread per frame, no restart, no way back to GL |
| K3 | The timing deque and the calibration fields are read by the idle present thread while `MakeSwapchain` rewrites them (a resize on the timed path) |
| K4 | `VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT` is a plain drop: nothing drains the queue of a window that is not shown |
| K5 | The refresh period does not follow a mode change of the same screen (a display rate switch, gamescope's 49 Hz) |
| K6 | The Wayland lead is applied in IMMEDIATE too (policy `vrr`): 10 ms early |
| K7 | `WantedMode` looks at "the surface offers present timing", not at "the swapchain has it": FIFO with the timing flags when the set-up failed |
| K8 | Acquire waits 100 ms under the swapchain mutex; `vkWaitForPresentKHR` 100 ms per present while the queue is busy (a locked screen) |
| K9 | The calibration takes one sample with the call's own delay in it; the time domain counter is not watched |

### 1.5 Display events nobody handles (step 5)

The OS changing the rate of the same screen (no `QScreen::refreshRateChanged`); full screen to windowed and back;
low latency switched at run time (smooth motion and the sync step are not started over); a monitor unplugged while
its mode is switched; sleep and wake; the timed GL swap's `glXWaitForSbcOML` without a timeout (a window hidden with
a swap in flight).

### 1.6 Content and displays not covered (step 6)

Everything measured so far is 50 (48.83) Hz on 60 Hz at 352x288.

- Scrolls faster than 8 pixels a frame (the search range): no interpolation, no scene.
- 60 Hz machines and hi-res / hi-colour modes (Next, TSConf, Sprinter): the cover rule tells 32 colours, 640x512 on the
  GPU takes 27 ms; one test (`SizeChangeStartsOver`).
- Displays of 75, 120, 144, 165, 240 Hz: 1.5 to 3.3 pictures a frame in 6-13 ms, not measured; on 144 and 240 Hz
  `auto` is off and the beat stays (the 5 % bound of `PickVideoSync`).
- Border effects, static multicolour, interlace, fades; the beat of a held 25 Hz flicker with 60 Hz.
- Turbo, pause, single step.
- Explicit `blend` / `interpolate` check no rates: on 50 Hz a blend ghosts for good.

## 2. Platform by mode, from the code (before the steps)

| | Fixed grid | VRR | Display rate switch | Smooth motion | Low latency |
|---|---|---|---|---|---|
| macOS Metal | yes | by the screen's range, unchecked (V5) | yes | yes | drawables queue (1.2) |
| Windows DXGI | yes | V1, V2 | V2-V4 | yes | yes |
| Linux X11, timed GL | yes | full screen + `vrr_capable` only | yes (RandR) | yes | no replacement (1.2) |
| Linux Wayland, GL | yes, no screen times | policy `vrr` only, blind | no | by the paint's time, no grid | yes |
| Linux Vulkan | yes | policy `vrr` only (V6) | X11 (K5); Wayland no | K5 | no replacement (1.2) |
| Steam Deck, gamescope | yes | no query (V7) | yes (`gamescope_control`) | yes, on gamescope's composition times | no replacement (1.2) |

## 3. The VRR verdict (step 1)

`unreal-qt/src/platform/vrrprobe.h`: one answer for every presenter under policy `auto`, from what the screen did.
A range the system reports, a swap mode the driver offers or tearing being supported do not say that the display
follows this window now.

- `Judge`: the vblanks' interval against the frames' and the mode's period (DXGI, the timed GL swap). Follows: with
  the frames, off the mode's grid. Grid: on a steady grid the frames are not on.
- `JudgeSteps`: the same from the steps between the screen times of the frames shown, where no vblank is heard
  between them (Metal): on a grid a frame stays a whole number of refreshes.
- Frames at the display's own rate tell nothing, and nothing changes then.
- `Probe`: the state. Windows starts on the grid (tearing supported says nothing) and is `Pending` until the first
  verdict; macOS starts in VRR where the screen has a range, until a grid is seen. A new screen, range, full screen
  state or policy starts it over.

Checked: unit tests (`VrrProbe_Test`); builds without warnings and the Qt tests on macOS, Linux (.238) and Windows
(MSVC 2022). To check on hardware: the G-Sync monitor of .101 in a console session (the engage after about two
seconds, no tearing presents on its fixed displays), a ProMotion Mac.
