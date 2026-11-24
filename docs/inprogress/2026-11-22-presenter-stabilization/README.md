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
refreshes of delay stay, the lead climbs to its bound.

Tried on the Steam Deck (gamescope, low latency, 2026-11-22), GL and Vulkan, per-present traces:

| Case | Result |
|------|--------|
| ZX 48K (50.08 Hz), `display_rate=match` (gamescope at 50 Hz), 90 s | no queue: the error steady (GL 9-10 / 17 ms), 0 dropped; the display shows 50.1 frames a second, not fewer than the machine makes |
| Turbo, 30 s | no queue: the window hands over one frame a refresh (59 a second) |

So the pile is not reached in what a user can set today; a machine clearly faster than its display (a 60 Hz mode on
59.81 Hz) is the case left, with no machine to make it here.

- Done, the timed GL swap: a frame at once gives way to a pile of two or more frames at once behind it
  (`linuxpresent::NextForPlan`). One frame waiting is the usual case and stays: giving way to it lost 0.4 frames a
  second on the Deck (48.83 Hz on 60 Hz); with the pile rule 0 dropped in the measured 20 s (5 during the start and
  the snapshot's load).
- Open, Vulkan and Metal: a present handed over cannot be taken back (an acquired image, a drawable). The way is
  back-pressure in the window: while a present waits for its refresh the next frame is not handed over, the newest one
  goes when the presenter is free. Not built: nothing reproduces the pile there yet.

### 1.3 Smooth motion (step 3)

| # | Finding | Where |
|---|---------|-------|
| S1 | After a `Reset` (settings, VRR on / off, focus lost with a display rate switch) or a GUI stall the newest frame fed is three behind the one due: `Plan` answers with it, a step back for a refresh | `win` `feedSmooth`, `SM` `Plan` |
| S2 | Smooth motion stays on in turbo (the latch clock): eight frame copies under the frame source's mutex and eight flicker searches a refresh, for a picture that is always the frame | `win` `smoothActive` |
| S3 | No count of "the next frame was not there": a long lead (Wayland / Vulkan, a grown adaptive lead, the lip-sync trim, the core's temporal delay) eats the look-ahead and the judder returns unseen | `SM` `Plan` |
| S4 | `_smoothGpuFailed` is never reset: after one failed `Create` the steps run on the CPU until a restart | `win` |
| S5 | `_smoothDrained` / `_smoothLag` are not reset when the presenter is made anew | `win` |

Done (2026-11-22):

- S1: a feed after a `Reset` starts four frames before the one due, and a plan whose newest frame is two or more
  behind the frame due is not used (the frame due is shown as it is; `present.smooth.behind` counts them).
- S2: no smooth motion on the latch clock (turbo, stepping).
- S3: `present.smooth.starved` counts the frames due with no next frame fed yet.
- S4: the GPU passes are tried again with another context or device. S5: the drain's counters start over with a new
  presenter.

Steam Deck, gamescope, 60 Hz, Across the Edge, `smooth_motion=interpolate`, GL and Vulkan: after six mode switches
(a `Reset` each) `behind` 0 on both; under turbo the mode in effect is off, and on again after it (one refresh
`behind` at the change, the frame due shown); `starved` 15-22 of ~1100 frames on GL, 1 on Vulkan.

### 1.4 Vulkan (step 4)

| # | Finding |
|---|---------|
| K1 | Restarts used up: `fallBackToGl` leaves the Vulkan renderer in place and `paintGL` returns early: an empty window for good (`dropVulkan` is the way down) |
| K2 | `PresentFrame` fails silently (acquire timeout, every image held): 100-200 ms of the GUI thread per frame, no restart, no way back to GL |
| K3 | The timing deque and the calibration fields are read by the idle present thread while `MakeSwapchain` rewrites them (a resize on the timed path) |
| K4 | `VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT` is a plain drop: nothing drains the queue of a window that is not shown |
| K5 | The refresh period does not follow a mode change of the same screen (a display rate switch, gamescope's 49 Hz) |
| K6 | The Wayland lead is applied in IMMEDIATE too (policy `vrr`): 10 ms early |
| K7 | `WantedMode` looks at "the surface offers present timing", not at "the swapchain has it": FIFO with the timing flags when the set-up failed. Fixed: a failed set-up makes the swapchain again without present timing |
| K10 | gamescope's WSI layer completes every timing result with no time while the window does not bypass Xwayland: no screen times for 15 s after each start on the Steam Deck, and whenever something is over the window. Fixed: 32 such results in a row end present timing for the swapchain (`VK_KHR_present_wait`'s times), until the surface says it changed ([Vulkan renderer, section 2.6](../2026-11-19-vulkan-renderer/README.md)) |
| K8 | Acquire waits 100 ms under the swapchain mutex; `vkWaitForPresentKHR` 100 ms per present while the queue is busy (a locked screen) |
| K9 | The calibration takes one sample with the call's own delay in it; the time domain counter is not watched. Half done: a change of the swapchain's time domains asks them again (`PickTimeDomain`) |

Done (2026-11-22):

- K1: restarts used up with the Vulkan renderer end in `dropVulkan` (GL draws and tries its own presenter). Checked
  on .238 under Xvfb with `UNREAL_TEST_PRESENT_FAIL` (a build with `UNREAL_TEST_HOOKS`): four restarts, then "the
  Vulkan renderer stopped presenting", `renderer_active` gl, the window's capture shows the picture.
- K2: `NativePresenter::PresentFailed` (Vulkan: no image came, every image held): the window counts it as a picture
  not presented, the same recovery as a missing framebuffer.
- K3: the idle poll does not run while `BeginTexture` makes the swapchain anew, and is waited for when under way.
- K5: the screen's `refreshRateChanged` is followed (the period measured again, the swapchain's refresh asked again).
- K6: the Wayland lead only on a swapchain that shows a present at a refresh.
- K7, K10: step 2. K9: the time domains followed (step 2); a calibration call that took over 0.5 ms keeps the offset
  before it.

Steam Deck after these (gamescope with its layer, Across the Edge, 20 s; error p50 / p99): at once GL 9.2 / 17.3 ms,
Vulkan 12.3 / 20.4 ms on a timed swapchain (its submit is taken before the GPU work: Vulkan renderer README 2.6); at
the frame's time GL 10.0 / 18.1 ms, Vulkan 1.9 / 10.3 ms. The whole cycle was seen in one run: present timing, no
times without the bypass, `VK_KHR_present_wait`, present timing again when the bypass came back.

Open: K4 (a full timing queue: not seen since the poll runs before every present and K10 ends a timed swapchain
without times), K8 (the 100 ms waits under the swapchain mutex and per present for a window that is exposed and not
shown), V6 (VRR under policy `auto` on X11: `x11::DecideWindowVrr` with the probe; no VRR display on a Linux host here).

### 1.5 Display events nobody handles (step 5)

The OS changing the rate of the same screen (no `QScreen::refreshRateChanged`); full screen to windowed and back;
low latency switched at run time (smooth motion and the sync step are not started over); a monitor unplugged while
its mode is switched; sleep and wake; the timed GL swap's `glXWaitForSbcOML` without a timeout (a window hidden with
a swap in flight).

Done (2026-11-22):

- The rate of the screen under the window is followed (`QScreen::refreshRateChanged`, `watchScreenRate`): the display
  rate step and the sync step are taken again. Not exercised: gamescope's XWayland does not send it (the presenter's
  own vblanks tell the rate there), and no desktop session was at hand to change a mode in.
- Smooth motion that comes on again (low latency or temporal blending off, turbo ended) starts over from the frames
  that come now. The Deck: `behind` 0 after turbo (it was 1).
- Full screen to windowed and back: the sync timer runs whenever smooth motion is `auto` or the display rate is
  `match` (step 1), and takes the presenter's VRR and its mode again twice a second.
- A monitor unplugged while its mode is switched: `Restore` on a display that is gone fails without harm on the three
  platforms (the calls' errors are dropped; X11 under its own error handler). Read, not exercised.

Tried and taken back: the timed GL swap's wait for its swap by polling `glXGetSyncValuesOML` with a deadline, in
place of `glXWaitForSbcOML` (which has none). On the Deck it moved the screen times (at once p50 9.2 -> 10.1-11.0 ms)
and after turbo smooth motion drew half its pictures (`starved` +29 to +51 in 5 s). The wait without a deadline stays;
the hang it can cause (a window hidden with a swap in flight, then closed) was not reproduced.

Open: the hang above; sleep and wake (the frame clock's sequence break and the presenters' own recovery, not
exercised); V7 (a VRR query on Wayland and under gamescope: no such display here).

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

Done (2026-11-22), each found by measuring smooth motion on the Steam Deck (gamescope, 60 Hz, GL and Vulkan, the
effects on the GPU and on the CPU):

| Before | Cause | After |
|--------|-------|-------|
| `interpolate`: 13 pictures a second on screen, 98 presents a refresh late in 10 s | a picture takes 18.9 ms on the Deck's GPU at 16.7 ms a refresh | the window blends instead when 4 of the last 20 interpolated pictures came a refresh late (`present.smooth.too_slow`, `mode` says what draws), until the settings are set again: 59.5 pictures a second, 0 late, each on its refresh (error 8.3 ms, half a refresh, as asked) |
| `blend`: 30 pictures a second after an `interpolate` | the lates above left every other refresh out to drain the queue, and widened the lead to its 20 ms bound | 59.4 a second |
| At the frame's time: 56-62 "late" of 1300 on GL, the lead at 20 ms | gamescope's screen times come 1.5-2 ms after the vblank, a frame at the first refresh after its time read up to 18.7 ms at 16.7 ms a refresh | `PresentLead` measures that constant offset (the 5th percentile of the errors on time, up to 3 ms): 0 late, lead 6 ms |
| `blend` / `interpolate` set by hand drew on any display | no rates were looked at: on a display that shows each frame for whole refreshes a blend is a ghost that comes and goes | `SmoothMotionHelps`: drawn only where the display beats with the machine (a ZX 48K: on at 60 Hz, off at gamescope's 50 Hz, on again) |

Still not covered (no display or machine here to measure them on, and the layers' rules are frozen without a scene
set to judge them by):

- Displays of 75, 120, 144, 165, 240 Hz: more pictures a frame. The guard above covers a machine that cannot draw
  them in time (it blends); on 144 and 240 Hz `auto` stays off within the 5 % bound, the same beat the display rate
  switch accepts for 48.83 Hz on 50 Hz.
- Hi-res and hi-colour modes: the guard covers their cost; their picture quality under interpolation is not judged.
- Scrolls over 8 pixels a frame, fades, border effects, static multicolour, interlace: scenes to write.

### 1.7 What an interpolated picture costs (2026-11-22)

The Deck's GPU takes 18.9 ms a picture; its CPU was measured with the window's own frames: a smooth motion trace
of Across the Edge taken on the Deck (`UNREAL_SMOOTH_TRACE`, 300 refreshes, 352x288, 31.8 vectors a picture: the
cap) replayed through the core by `tools/verification/smoothmotion/live/replay --time`, which times every `Draw` and
sums the pictures' pixels (the same sum before and after every change below: the same bits).

| Deck CPU, a picture | 1 thread p50 / p95 | 4 threads p50 / p95 |
|---------------------|--------------------|---------------------|
| before | 64.3 / 88.0 ms | 22.7 / 30.0 ms |
| after | 29.8 / 42.6 ms | 14.2 / 17.6 ms |

What was found, by a profile (callgrind) and by timing `Compose`'s stages on the Deck:

- `MotionField::Estimate` read every pixel through `Picture::At` (two clamps): 30 % of a picture. Blocks whose two
  warped copies lie inside the pictures compare whole rows through pointers now.
- `LayerFlow::Compose`'s per-vector pixel loop stored 32-bit compares as bytes: a byte store may alias any of the
  nine rows read, so the loop stayed scalar (14 of 32 ms). It writes a chunk of 64 pixels into arrays of its own
  scope and narrows them after; the same for the `usable` pass. `BlockCounts` called `std::popcount` through the
  library (no popcnt in the build's baseline): the bytes' sum by a multiply.
- Instruction counts mislead here: a first rewrite of the pixel loop that only split the loops by width changed
  nothing, and reordering `CpuEvidence::Own`'s loops for the cache did not either. Each step was kept only for the
  time it took off on the Deck.

Where the 4-thread picture's time is now (ms): the vectors' pass 5.4 (the pixel loop and two window sums per
vector, 32 vectors), `Decide` 3.2 (one thread), the field 2.3, the pixels' pass 1.3, the rest 2. Still over the
8 ms a picture has before its refresh on a 60 Hz display, so the Deck keeps the guard's blend; the Deck's own way
to a smooth picture is its display at the machine's rate (`display_rate=match`, gamescope's 50 Hz: frame for frame).
Next, should the Deck interpolate: `Decide` split over the pool or its per-block reads laid out by block, the
window sums only over the rows a vector is allowed in, and the GPU path's read-backs.

### 1.8 The cheap interpolation (2026-11-23)

[POC 055](../../../tools/poc/055-interpolation-low-end/README.md) answered section 1.7's question another way: not
LayerFlow made cheaper, an interpolation of its own for the machines LayerFlow is too slow on. In the code:

- `core/src/emulator/video/smoothlite.{h,cpp}`: `LiteFlow`, the POC's form `lite19`. Frames as bit planes of colour
  indices, a block field kept from pair to pair, a pixel's layer picked by masks of whole words. One thread. Its
  pictures are the POC's byte for byte (the POC binary's `core` filter against `lite19`: 12 suite scenes and 2 game
  traces), and the same on x86-64 and ARM64. Not its picture: over 256 colours in a pair of frames, a size that is
  not whole 8 x 8 blocks or over 2048 wide; LayerFlow draws those.
- `smooth_motion = interpolate-lite` (`[VIDEO] SmoothMotion`, every plane's `smooth_motion`): chosen outright.
- The step down. `SmoothGuard` (`displayrate.h`) judges every interpolated picture drawn: the presenter's late
  presents, and the drawing itself taking over 3/4 of a refresh (the software screen and the plain GL swap have no
  presenter to count lates, and a machine with no GPU is where it matters). 4 of 20: `interpolate` goes on by
  `LiteFlow` (`present.smooth.stepped_down`, `mode` says `interpolate-lite`); 4 of 20 again, or the mode was
  `interpolate-lite` already: the blend (`too_slow`). Until the settings are set again. Both screen widgets
  (`DeviceScreenGLWindow`, `DeviceScreen`).
- `present.smooth.lite`: the interpolated pictures `LiteFlow` drew.

Cost and quality (POC 055): 0.42-0.81 ms a picture on one quiet Steam Deck thread by trace where LayerFlow takes
10-36 ms; 1.3 times LayerFlow's error on the suite (58.7 against 43.8, no interpolation 278.8).

In the live window (2026-11-23, Across the Edge, low latency off, 10 s a case after the mode settled):

| Host | Asked | Effects | Draws | Pictures a second | Late presents | A picture on the CPU's clock, p50 / max |
|------|-------|---------|-------|-------------------|---------------|------------------------------------------|
| Steam Deck, gamescope 60 Hz, `gl-timed` | `interpolate-lite` | cpu | `interpolate-lite` | 57.0 | 0 | 1.34 / 2.68 ms |
| | `interpolate` | cpu | `interpolate-lite` (stepped down) | 56.2 | 0 | 1.37 / 2.57 ms |
| | `interpolate` | gpu | `interpolate-lite` (stepped down) | 56.0 | 0 | 1.38 / 2.54 ms |
| | `blend` | cpu | `blend` | 57.0 | 0 | 0.51 / 0.86 ms |
| .238 under load, Xvfb, the software screen | `interpolate-lite` | cpu | `interpolate-lite` | 92.0 | — | — |
| | `interpolate` | cpu | `interpolate-lite` (stepped down) | 90.9 (11.3 before the guard judged the drawing time) | — | — |

On the Deck `interpolate` used to end as the blend (`too_slow`, section 1.6: 13-30 pictures a second before the
guard). Now it ends as the cheap interpolation at the refresh's rate with no late present, on the CPU, whichever
device the effects have; the step's log line: 4 of 20 pictures a refresh late on the CPU, 12 of 20 on the GPU.
The live MCP scripts on .238: 40 of 41 (`test_gs_debugger.py` fails as it did before these changes).

Not in it: a game profile's cadence modes (`video.smooth` cadence / carry) still draw by `MotionField`; the video
wall (its tiles have no timed frames yet: per tile, not for the whole wall's picture).

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
