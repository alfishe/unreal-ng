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
| V6 | Vulkan: policy `auto` never takes VRR (FIFO), `vrr_support` unknown; `x11::DecideWindowVrr` is not asked | `vk` `WantedMode`, stats | fixed in code: on X11 the window opts in and `DecideWindowVrr` is asked every second; a capable window is judged by the probe (`JudgeSteps` on the screen times), a grid ends VRR; not measured (no VRR display on a Linux host) |
| V7 | Wayland, gamescope: no VRR query at all | — | gamescope fixed: its root atoms on Steam's `:0` (`GAMESCOPE_VRR_CAPABLE`, `GAMESCOPE_VRR_ENABLED`) give the answer, the probe judges it as on X11 (Steam Deck LCD read 2026-11-25: capable 0, so `no`); a Wayland client: open, no protocol to ask |

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

Done (2026-11-25):

- K4: presents with timing stages are counted until their results come back complete (`timingHeld`), and the poll
  and the idle wait (20 ms, not 1 s) run while any is held, not only while the deque has entries. A refused present
  polls at once; eight refusals in a row end present timing for the swapchain (`VK_KHR_present_wait`, as K10). Not
  reproduced: .238's Mesa 26.0 has no present timing.
- K8: the acquire tries for 2 ms at a time with the swapchain mutex released between tries (100 ms in all); after a
  timeout it only looks (timeout 0) until an image comes. `vkWaitForPresentKHR` after a timeout looks with timeout 0
  until a present is seen; such presents are recorded without a screen time. Counted in the close report ("waits
  timed out"). .238, GNOME Wayland, Intel, MAILBOX with `VK_KHR_present_wait`, 25 s with the session locked: before,
  38 presents and none shown, the presents stopped for 21.7 s; after, 1022 presents, 874 shown, the longest gap
  2.8 s (the window not exposed), presenting again 0.4 / 0.7 s after two acquire timeouts.
- V6: see table 1.1 (code only, no VRR display on a Linux host). The close report says "VRR (the display follows)"
  or "VRR capable, on a grid".

Open: V6 on a Linux VRR display (X11, FreeSync or G-Sync).

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

Done (2026-11-25), sleep and wake, from a pass over the code (no machine here may be put to sleep): steady clocks go
on in sleep on macOS (libc++ CLOCK_MONOTONIC_RAW) and Windows (QPC) and stop on Linux, so a sleep is a gap in time on
two platforms and a display off without sleep a gap on all. Handled already: the frame clock cuts at a gap of 4
periods, the audio clock fits anew after 250 ms without callbacks, MainLoop's deadline anchors again, the GLX vblank
clock drops steps over 100 ms and takes an MSC that went back, the probes' percentile windows are not moved by one
step, Metal maps Core Animation's time (stopped in sleep) through an offset read now. Fixed:

- A present shown seconds after its time (held through the gap) widened the adaptive lead a step, two or three queued
  ones two or three, each step 600 presents on time to give back (DXGI, the timed GL swap, Vulkan's timing paths).
  Over `PresentLead::kGapNs` (100 ms) it is counted late and changes nothing else (`presentlead_test.cpp`).
- A run of failures that began before the window was hidden (a display going to sleep) restarted the presenter at the
  first failure after the wake. A hidden window ends the run (`PresenterRecovery::Hidden`).

Found 2026-11-25 on a remote desktop (headless mutter, NVIDIA EGL, the RDP server's log): the GL window repaints on
every refresh while frames come, paced by the swap that waits for the display; there the swap waits for nothing and
the window drew and swapped ~250 times a second for 49 Hz frames (102 % CPU), each a new buffer for the compositor
and the encoder. Now a window whose swaps come more than half again as often as its display refreshes (at least 90 a
second, then for its life) draws and swaps only for something new (another frame, smooth motion's next picture, a
repaint asked) and times its next look by the next frame's target. Xvfb on .238 (llvmpipe, the same kind of swap):
99-128 swaps a second before the switch, 486 in 10 s after it (a gdb count on `QOpenGLContext::swapBuffers`): the
machine's 48.8 Hz. A display whose swap waits is not touched.

Open: the hang above (the swap wait is skipped for a window not exposed; a display off with the window still exposed
not reproduced); a stuck present on a visible window restarts the presenter every 2 s and the fourth restart in a
minute falls back to GL: not seen (K8 on .238 with the session locked kept presenting about once a second, which ends
each run); MoltenVK's `actualPresentTime` may be Core Animation's time (stopped in sleep) on the Vulkan build for
macOS; V7 for a Wayland client.

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
- Scrolls over 8 pixels a frame, fades, border effects, static multicolour, interlace: scenes written 2026-11-25
  (below).

Scenes (2026-11-25, `core/tests/emulator/video/smoothscenes_test.cpp`, generators in `smoothscenes.h` for the GPU
tests too; 128x96 ZX pictures, Blend, Interpolate (LayerFlow) and InterpolateLite (LiteFlow)). "Invented" is an
output pixel that is neither frame's nor their mix at the weight drawn.

| Scene | What a correct picture keeps | Result |
|-------|------------------------------|--------|
| Scroll of 12 / 16 / 24 px a frame under a still status bar | nothing invented, the bar as it is | kept |
| ZX fade (7 -> 6 with bright going, 3 -> 2) | the frames or their blend | kept |
| Border stripes moving 3 lines a frame around still paper | the paper as it is; nothing invented in the border | kept, one pair and a 12-frame stream (was: LayerFlow moved 3-9 paper pixels at x = 16, and in a stream up to 11 at the paper's top rows and 94 border pixels at rows 15 / 80; LiteFlow up to 80 border pixels; fixed 2026-11-26) |
| Loader border (random red / cyan lines each frame) | the paper as it is; the border the frames or their blend | kept, one pair and a 12-frame stream (was: LayerFlow invented 1272-1384 of 6144 border pixels, up to 590 a pair in a stream; LiteFlow 160, up to 128 in a stream; fixed 2026-11-26, confirmed vectors) |
| Static multicolour (8x1 attributes) | passed through | kept |
| Multicolour with a changing counter | pixels outside it as they are | kept |
| Multicolour colour cycling | the frames or their blend | kept |
| Interlace (two pictures' lines swapped; one picture a line lower) | the frame due | kept |
| A held 25 Hz flicker, 50 Hz frames on 60 Hz | the flicker the frame due at every refresh | kept: 5 repeats in 30 refreshes (a 10 Hz beat, as without smooth motion), no blend of the two fields |

The two defects were `DISABLED_` tests; fixed on the owner's go (2026-11-26), the tests run, with a stream variant
(every pair of 12 frames in order, as the window draws them) and the border asserted too:
- stripes fit every sideways shift, and the cover rule took one, (4, 3), whose path crosses the paper's edge column.
  The cover rule now leaves a sample to the slowest vector fitting it (`slowest`), asks the sample to arrive where a
  had another color (`landing`: a still pixel of the same color by chance is not the layer's), and leaves the pixel
  when the layer's pixel a would show there is seen in k - 1 and b at its places (`behind`: the layer is opaque
  there, the still pixel in front of it);
- `first` (carryFirst) carried the nearer frame's sample from the paper into the border row next to it (82 pixels
  at w = 171): it takes the other frame's sample where the nearer one's stood still over the three frames in a
  block decided still and the other's did not (a still layer the moving one goes under). LiteFlow's carried step
  the same;
- a line of one color across a block holds 8 values, not 64, and the field matches within `kAccept`: random lines
  match some vector by chance in every pair. A moving vector is now confirmed by a block matched to it that has
  detail in both directions (`kDetail`), or whose pixels it carries exactly to b and that came exactly from k - 1
  (by the vector, or in place: a motion from rest). Never exact over three frames by chance. LayerFlow
  `KeepConfirmed` in `Prepare` (CPU for both paths), LiteFlow `Confirm` (the frame before a by its planes still
  kept; none: detail alone). An unconfirmed vector moves only detailed blocks in LiteFlow, none in LayerFlow.

All four rules on the CPU and in the GPU's picture pass, the same bits (`BorderEffectsOnTheGpuAreTheCpus`).
`SameBitsOnEveryCompiler`'s two LayerFlow hashes changed: w = 128 by `behind` (116 -> 117 pixels unlike the sprite
moved), w = 85 by `behind`, `landing` and `first` (131 -> 128).

POC 055 `score.sh` (12 scenes, the final code):

| Scene | LayerFlow before | after | LiteFlow before | after |
|-------|-----------------:|------:|----------------:|------:|
| strips | 1.07 | 1.03 | 2.23 | 2.17 |
| vertical | 2.04 | 1.94 | 3.06 | 2.89 |
| diagonal | 0.48 | 0.27 | 0.58 | 0.57 |
| overlay | 1.04 | 1.04 | 1.04 | 1.00 |
| textlines | 0.58 | 0.48 | 0.92 | 0.86 |
| parallax | 4.08 | 4.01 | 5.42 | 5.38 |
| sprites | 7.36 | 7.64 | 8.03 | 8.02 |
| cadence | 3.33 | 3.16 | 6.58 | 6.54 |
| dotmatrix | 4.77 | 4.57 | 7.32 | 7.20 |
| thin | 1.43 | 1.27 | 1.75 | 1.38 |
| flux | 3.02 | 2.73 | 4.13 | 4.07 |
| animated | 14.58 | 14.79 | 17.60 | 17.57 |
| sum | 43.78 | 42.93 | 58.66 | 57.65 |

The cost, one thread, the best of 3 interleaved rounds on sprites / parallax / cadence: LayerFlow 3.99 / 7.19 / 5.18
-> 3.79 / 5.61 / 4.10 ms (fewer vectors to try), LiteFlow 0.416 / 0.575 / 0.425 -> 0.439 / 0.609 / 0.450 ms (+6 %).

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
  presenter to count lates, and a machine with no GPU is where it matters). 4 of 20, three windows of 20 in a row:
  `interpolate` goes on by `LiteFlow` (`present.smooth.stepped_down`, `mode` says `interpolate-lite`); the same
  again, or the mode was `interpolate-lite` already: the blend (`too_slow`). Both screen widgets
  (`DeviceScreenGLWindow`, `DeviceScreen`). In `DeviceScreenGLWindow` (2026-11-27) a step down is for a while: the
  level above is tried again after 10 s, the wait doubling with each step down up to 5 minutes, and the settings set
  again start the waits over; no lates count in the first 3 s of a window shown, exposed or given a presenter.
  The software screen (`DeviceScreen`) and a wall tile (`smoothmotion::Tile`, its own guard; the wall's step stays
  for good) recover the same way (2026-11-27); from the blend the cheap interpolation is tried first, then the full one
  after the wait as it stands (`SmoothGuard::RetryLater`: before, the full one was never tried again). One
  window was not enough: on a 60 Hz Mac a single stall (another app's GPU work, a start) made 4 presents late at once
  and stepped down for good an interpolation drawn in 1 ms; a start made every present late for its first seconds.
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

What a tile of the video wall would cost (2026-11-23): one `SmoothMotion` engine fed a trace at 50 frames a
second and asked for a picture at every refresh of 60 Hz, one Steam Deck thread, no pool:

| Trace | Mode | `Push`, a frame, p50 / p95 | Plan + Draw, a refresh, p50 / p95 | Of one thread |
|-------|------|----------------------------|------------------------------------|---------------|
| Across the Edge | `interpolate-lite` | 6.27 / 16.23 ms | 1.35 / 1.44 ms | 43.6 % |
| | `blend` | 6.27 / 16.58 ms | 0.50 / 0.57 ms | 39.6 % |
| action | `interpolate-lite` | 0.22 / 0.29 ms | 0.72 / 0.97 ms | 5.1 % |
| Aleste | `interpolate-lite` | 0.20 / 0.23 ms | 0.74 / 0.85 ms | 4.8 % |
| scroll | `interpolate-lite` | 0.21 / 0.23 ms | 0.58 / 0.64 ms | 4.1 % |
| action / Aleste / scroll | `blend` | 0.19 / 0.23 ms | 0.25 / 0.27 ms | 2.3 % |

A game is 4-5 % of a thread a tile: about 20 tiles a Deck thread. A full-screen demo is 44 %, and nearly all of
it is `Push`: the flicker search over five frames, which runs whatever the mode (the blend pays it too) and takes
16 ms at p95, a frame's whole 20 ms on a core a third of the Deck's. The cheap interpolation moved the cost there:
before a wall of tiles, and before a low-end machine is called served, the flicker search needs the same bit-plane
form.

Done the same day. The time was `MovesBy` (the flicker's motion look-alike test: 288 shifts a block, a pixel loop
each). Now the 24 x 24 pixels round a block are bit planes of the colours met there (up to 16, 64 since 1.10; more: the pixel
loop) and a shift is a few word operations; the same answers, so the same pictures (the sums of every picture of
the Across the Edge and action traces, blend and interpolate-lite, before and after). One Deck thread:

| Trace | `Push` before, p50 / p95 | After | A tile, `interpolate-lite`, of one thread: before | After |
|-------|--------------------------|-------|---------------------------------------------------|-------|
| Across the Edge | 6.27 / 16.23 ms | 2.07 / 3.49 ms | 43.6 % | 17.5 % |
| action | 0.22 / 0.29 ms | 0.27 / 0.35 ms | 5.1 % | 6.5 % |
| Aleste | 0.20 / 0.23 ms | 0.27 / 0.37 ms | 4.8 % | 7.2 % |
| scroll | 0.21 / 0.23 ms | 0.28 / 0.36 ms | 4.1 % | 5.5 % |

The games' rows are the Deck's run-to-run spread (section 1.7's quarter), not a cost of the change: their frames
have no block that asks `MovesBy`. A memory of the answer two frames back (flicker repeats) was tried first and
kept: it halves the calls where a picture flickers.

### 1.9 The video wall (2026-11-24)

Smooth motion per tile, in the GPU grid (`TileGridGL`); the software grid draws every frame as it is.

- `smoothmotion::Tile` (`core/src/emulator/video/smoothtile.{h,cpp}`): one stream of frames among many. Pulls the
  machine's frames in order with their times (`Screen::CopyNextFrame`), gives the picture for a refresh, steps down
  when its pictures take too long. No threads of its own (`SmoothMotion::SetOwnThreads(false)`): the wall spreads
  the tiles over one pool, half the cores and at most 8.
- The wall draws a refresh while a tile is between frames and rests otherwise.
- Two guards. A tile's own (`SmoothGuard` on its share of the refresh: the threads' time over the tiles that
  draw). The wall's: all the tiles together took over 3/4 of a refresh in 4 refreshes of 20, so the costliest
  quarter steps one level down: the full interpolation, the cheap one, the blend, the frames as they are.
- Set by View > Smooth Motion, `tilegrid.smooth_motion {mode}` on the GUI plane, `UNREAL_SMOOTH_MOTION` at start.
  State `smooth_motion` and `smooth {...}` on `main/tilegrid`.
- Off is the default and the wall as it was: no tile engine, no pool, no state timer, a redraw only on a frame's
  notification. Switching back to off drops all of it.

Measured on the Steam Deck (gamescope, GL, a 60 Hz panel, 4 cores / 8 threads; Pentagon 128K tiles, the wall's
process, one core = 100 %; `tools/verification/videowall/src/videowall_smooth.py`):

| Tiles, content | Mode | Tiles in the mode at the end | A refresh's work | Wall CPU |
|---|---|---|---|---|
| 12, a game | off | - | - | 148-153 % |
| | blend | 12 | 4.5 ms | 203 % |
| | interpolate-lite | 12 | 6.4 ms | 244 % |
| | interpolate | all 12 stepped down to the cheap one by their own guards | 6.3 ms | 257 % |
| 24, a game | off | - | - | 317-333 % |
| | blend | 24 | 9.2 ms | 424 % |
| | interpolate-lite | 23, one on frames as they are (the wall's guard, 2 steps) | 8.3 ms | 441 % |
| 12, Across the Edge (the flicker search's worst content) | off | - | - | 166-177 % |
| | blend | 3, 9 on frames as they are (the wall's guard, 3 steps) | 12.0 ms | 237 % |
| | interpolate-lite | 5, 7 on frames as they are | 6.2 ms | 261 % |

- A wall of games fits the cheap interpolation on every tile at 12 tiles and nearly at 24; on the worst content the
  wall's guard takes tiles down to frames as they are and the rest stay smooth. What is left there is the flicker
  search of `Push` (1.8), paid in every mode.
- Off costs nothing. Interleaved runs of the wall before this work and after it, 12 tiles of Across the Edge, 20 s
  each: 172.1 / 167.8 / 166.1 % before, 164.7 / 171.3 / 165.7 % after, the same thread count; the state reports no
  engine. The single window with smooth motion off: the engine is given no frame (`frames`, `renders` 0 a second).

Not in it: a game profile's cadence modes (`video.smooth` cadence / carry) still draw by `MotionField`; the software
grid of the wall; a default (the wall starts with smooth motion off).

### 1.10 The flicker search, four times faster (2026-11-24)

`Push`'s flicker search (`SmoothMotion::FindFlicker`) was still most of a flickering tile's time (1.9). The same
decisions, faster. Its answer for a block depends on the five frames' pixels round it only, so every shortcut below
gives the same yes / no as before:

- The counts (changed, alternating) in one pass, eight pixels at a time without branches; the pass also notes the
  blocks that are as two frames before.
- The motion look-alike test (`MovesBy`) only where its answer matters: a block whose 3 x 3 neighbourhood flickers
  is held whatever it says, so it is asked only when the neighbourhood does not.
- The memory of 1.8 (the answer two frames back, kept when the 3 x 3 blocks of both frames are as two frames
  before) reads the blocks' sameness from the counting pass instead of comparing the pixels again.
- `BlockShifts` (the bit planes of 1.8): the colours round a block numbered by a small hash, not a search; the
  planes made from a byte a pixel by one multiply per 8 pixels; a plane's shifted bytes made only when a shift
  reaches it (most shifts fail on the first plane); no validity planes for a block away from the edges; up to 64
  colours round a block (16 before: the pixel loop is rarer, its answer is the same).

The check: the flicker mask and the held count of every frame of 26 clips (21 recorded: games, demos, Across the
Edge, two Deck traces; 5 synthetic: a Gigascreen picture over a scroller, flickering sprites on a scroll, dots a
scroll step apart that alternate like flicker but move, a half flickering / half panning picture with a sprite
across both, 64 colours), hashed: the same hash before and after on the Mac and on the Deck. The unit tests of the
flicker decisions pass unchanged.

`Push` a frame, the mean, one thread (`flickbench`, the best of 3 runs a frame; the Deck in two interleaved rounds,
the first in brackets: its CPU clock was still settling):

| Clip | Mac before | Mac after | Deck before | Deck after |
|------|-----------|-----------|-------------|------------|
| Across the Edge | 1.156 ms | 0.308 ms | 1.510 (2.150) ms | 0.617 ms |
| Across the Edge, Deck trace | 1.013 ms | 0.311 ms | 1.372 (1.949) ms | 0.627 ms |
| earshaver | 0.271 ms | 0.064 ms | 0.451 (0.620) ms | 0.150 ms |
| synthetic Gigascreen | 1.286 ms | 0.083 ms | 1.468 ms | 0.170 ms |
| synthetic, 64 colours | 1.239 ms | 0.074 ms | 1.781 ms | 0.159 ms |
| synthetic, mixed | 0.806 ms | 0.138 ms | 0.956 ms | 0.255 ms |
| synthetic sprites | 0.547 ms | 0.048 ms | 0.604 ms | 0.117 ms |
| a game (action, Dizzy X) | 0.08-0.09 ms | 0.05 ms | 0.17-0.18 ms | 0.12 ms |
| all clips, the sum | 8.074 ms (26) | 1.978 ms | 8.847 ms (10) | 2.481 ms |

The wall of 1.9 again on the Deck, 12 tiles of Across the Edge, two runs:

| Mode | Smooth tiles at the end, before (1.9) | After | Wall CPU before | After |
|------|----------------------------------------|-------|-----------------|-------|
| blend | 3, 9 on frames as they are (3 steps) | 9, 3 on frames as they are (1 step) | 237 % | 315 % |
| interpolate-lite | 5, 7 on frames as they are | 9-10, 2-3 on frames as they are | 261 % | 267-270 % |

The wall CPU grows because more tiles stay smooth: the guard now takes a quarter of the tiles down where it took
three quarters. Off: 148-180 %, as in 1.9.

#### A profiling round of a tile (2026-11-24)

`perf` on the Deck over a tile of one thread (`tilecost`: `Push` a frame, a picture every 60 Hz refresh), gcc -O3
as the release builds. Four plain costs found, each fixed with the same pictures (the hash of every picture of
action, Aleste, Dizzy X and Across the Edge, blend and `interpolate-lite`, the flicker masks of `flickbench`):

- `LiteFlow`'s scratch rows are `kMaxWidth` (2048 pixels: 32 words) long and were zeroed whole per pixel row of
  every vector tried (`rep stos`, a fifth of `KeptField::Score`); only the picture's words are zeroed now.
- `std::popcount` is a library call (`__popcountdi2`) in a gcc build for x86-64 without a popcnt instruction in
  its baseline: 7 % of a flickering tile, in `BlockShifts::Explains`. A few word operations there instead
  (`Ones`). The project sets no `-march`: every gcc x86-64 build has this cost wherever it counts bits.
- The held flicker pixels were written back with a branch per pixel, mispredicted where flicker and motion mix (a
  quarter of a blend picture's time): a mask now (`KeepHeld`).
- `Planes::Pack` read eight pixels of one colour (paper, border) one at a time: one test of the eight now.

A tile, the share of one Deck thread, the median of 5-6 interleaved runs after a warm-up run:

| Clip | Mode | Before | After |
|------|------|--------|-------|
| Across the Edge | blend | 5.2 % | 4.3 % |
| | interpolate-lite | 8.1 % | 6.6 % |
| action | blend | 2.0 % | 2.0 % |
| | interpolate-lite | 4.25 % | 3.7 % |
| Aleste | interpolate-lite | 4.6 % (after the first three) | 4.3 % |
| Dizzy X | interpolate-lite | 2.2 % (after the first three) | 2.1 % |

What is left is the work itself, spread with no hot spot: `MovesBy` on flickering content, the field search
(`KeptField::Score`), `Credible`, the drawing and the flicker counts on games.

### 1.11 The seams of the temporal filters (2026-11-26)

A review of where smooth motion, ZX DLSS (the core's temporal effect) and the video wall meet. Each found in the code,
each with a test that fails without its fix (`core/tests/emulator/video/`). `screen` =
`core/src/emulator/video/screen.cpp`, `tile` = `core/src/emulator/video/smoothtile.cpp`.

| # | Before | Cause | After | Test |
|---|--------|-------|-------|------|
| D1 | With ZX DLSS on, smooth motion's read ahead sometimes got a frame raw: the flicker drawn and counted nowhere (`shownRaw` 0) | `CopyNextFrame` cut the slots by the effect's delay only; a worker one or two frames behind (up to `TemporalEffects::kMaxQueuedJobs`) had not written the output yet, and the reader kept the frame by its serial | a slot whose output is still to come for the worker's current generation is waited for (`SlotAwaitsOutputLocked`); after a restart (new generation) nothing is waited for | `FramesByTime_Test.NextFrameWaitsForTheTemporalOutputToCome` |
| D2 | Smooth motion on again after turbo, low latency or another sync step could step down at once | `SmoothGuard` counted the presenter's late presents from while it was off | `SmoothGuard::Track` every refresh starts over while smooth motion does not draw; reset on a refresh rate change, a display switch and a sync step change | `DisplayRate_Test.SmoothGuardCountsNoLatesFromWhileSmoothMotionWasOff` |
| D2b | On a 60 Hz Mac, interpolation fell to the blend for good: at a start, and after any stall of a few presents | one window of 20 pictures with 4 lates stepped down, with no way back but the settings; the first seconds of a window count every present late | three bad windows in a row, no count in the first 3 s of a window shown, a retry after 10 s doubling to 5 min | `DisplayRate_Test.SmoothGuardStepsDownOnLateOrSlowPictures`, `DisplayRate_Test.SmoothGuardTriesTheLevelAboveAgainAfterAWaitThatGrows` |
| D2c | The Video Synchronization box unticked itself a refresh after a click when the window's mode was not `auto` (`UNREAL_SMOOTH_MOTION`, or `interpolate` set through the API) | the box showed the sync step in effect, which is `off` for any mode not `auto` | the box shows the setting (smooth motion `auto`) | `VideoSyncWindow_Test.TheBoxSaysTheSettingNotTheStep` |
| D3 | A video mode switch between the look at the frame size and the read: one frame drawn or pushed with the wrong row width | the window and the wall checked the byte size only | a frame whose geometry is not the size expected is left out (window `updateTexture` / `feedSmooth`, `tile` `FitsFrame`) | `SmoothTile_Test.FrameOfAnotherSizeIsLeftOut` |
| D4 | ZX DLSS switched on mid-stream: the picture went back up to six frames | the delay grew from 1 to 7 frames; `ChooseSlotLocked` cut off the frame on screen | the frame on screen is held until the look-ahead has passed the ones after it (a freeze of about six frames, the delay itself) | `FramesByTime_Test.TemporalEffectSwitchedOnNeverGoesBack` |
| D5 | The frame clock cut back (turbo on, a latch-timed frame before the last one's time): a blend of a later and an earlier frame | `SmoothMotion::Push` took frames out of time order | a target at or before the last one's starts the engine over; a serial already pushed is ignored | `SmoothMotion_Test.TargetBeforeTheLastStartsOver` |
| D6 | A reader that missed frames: a picture between two frames that are not consecutive (twice the motion, a blink read as motion) | `Push` took any next serial | a serial gap starts the engine over | `SmoothMotion_Test.SerialGapStartsOver` |
| D7 | After a pause, the first frame's picture (weight 255 / 256) shown a refresh early, and the pause counted as cadence periods | `Plan` paired the frames across the pause | `Plan` shows the frame before when two frames are more than `kGapPeriods` (4) periods apart; `Push` adds no periods for such a step | `SmoothMotion_Test.NoMotionAcrossAPause` |
| D8 | Wall tiles in turbo / stepping pushed every frame (copies and flicker searches, up to eight a refresh) for a picture that is always the frame | `Tile::Advance` treated latch-timed frames as paced | latch-timed or low-latency frames go to no engine, the newest is shown as it is; paced again, the engine starts over | `SmoothTile_Test.TurboFramesGoToNoEngine` |
| D9 | The effects readout (the dialog's LED) flipped to raw while a screenshot or the shared-memory export read | `MarkServedLocked` set it for any served frame | only the frame the window shows sets it; a served frame sets it only when no window shows one | `FramesByTime_Test.ScreenshotLeavesTheTemporalReadout` |
| D10 | With ZX DLSS on, smooth motion had no next frame for about half the frames (live: `starved` 165 of 339, 183 interpolated against 398 with DLSS off) | `CopyNextFrame` still cut the newest frames by the effect's whole delay (look-ahead + 1 for the worker), on top of D1's wait: a frame came a latch after its output was in, the frame after the one due only when that one was due | the cut dropped, each frame waits for its own output (`SlotAwaitsOutputLocked`) | `FramesByTime_Test.NextFrameServedOnceItsTemporalOutputIsIn` |
| D11 | ZX DLSS switched on (and at the start with it on): smooth motion starved for about 3 s (live: 43-49 of 49 frames a second), the picture ahead of its sound meanwhile | the delay steps by the look-ahead (6 frames, the sound with it at the same latch); `FrameClock` carried the step and paid it back at 0.2 ms a frame (10 s for 120 ms) | a change of the effect's delay cuts the frame clock (`FrameTimingInput::cut`): the targets take the new delay at once, a freeze of the step's length | `FramesByTime_Test.TemporalEffectSwitchCutsTheFrameClock` |
| D12 | ZX DLSS with smooth motion on the Metal presenter: no next frame for 85 % of the pictures (starved 798 of 943 on a 60 Hz Mac; 0 on GL) | Metal draws a picture ~40 ms ahead of its refresh; with the effect's delay alone the next frame's output came only as the frame before was due, less the worker's time | one frame more of video delay (and sound) while a temporal effect runs and smooth motion is set: starved 3 of 1277, +20 ms only in that case | `FramesByTime_Test.SmoothMotionGetsAFrameMoreWithATemporalEffect` |

The check (Linux, gcc 13, .74): with every fix undone at once, all eleven new tests fail, each at the assertion
for its defect (frame 5 handed out raw; the picture back from frame 10 to 4; the readout flipped to raw by the
screenshot; the same version copied again; the guard stepping down for lates from while it was off; a blend across
the target cut back, an interpolation across the serial gap and the pause; the misfit frame pushed; turbo frames
pushed to the engine; `auto` drawing on 50 Hz). With the fixes: the 232 tests of smooth motion, the screen's frames,
the display rate and the temporal effect pass in both tiers; the screen and tile tests 50 times over.

The live check (2026-11-27, unreal-qt on .20, a 60 Hz display, the host's screen locked: the Metal presenter rests
while occluded, so `UNREAL_PRESENTER=gl`; driven through the WebAPI by a script in the worktree's `scratch/`, the
window's `present` and `/video/temporal` sampled every second, every instance muted). Across the Edge (flicker and motion) in `interpolate`, `interpolate-lite`, `blend` and
`auto`, and Aleste in `interpolate` and `auto`; per run 30 s steady, 30 s with ZX DLSS off / on every 5 s, 40 s
with the modes switched every 5 s. With D10 and D11 fixed, in every run: `shown_raw`, `late`, `restarts` and
`present.smooth.behind` 0; every processed frame written; no serial gap among the ~740 frames traced
(`UNREAL_SMOOTH_TRACE`, 900 refreshes); `starved` 0 over the 30 s steady (before D10: half the frames, before D11:
92, all in the first 3 s), 6 over the three switches on (two frames each, the delay's step); 1450-1470 interpolated
(Across the Edge) and 1716-1724 (Aleste) of ~1550 / ~1720 refreshes; the effect processed 48.8-49.8 frames a
second throughout (Pentagon 48.83 Hz), the samples never more than 1.05 s apart. The frames traced with no color
outside the 16 ZX ones among mixed ones are the same serials in every run and mode: the content, not a frame
handed out raw.

Cheap optimizations:

- `Plan`'s "the two frames are the same" test reads `Stored::changed` (counted in `Push`) instead of a `memcmp` of
  the two frames each refresh.
- `CopyFrameAt` with the version the reader holds (`PresentFrameInfo::content`, a stamp each slot write gets): the same
  version is neither copied again nor pinned (`FramesByTime_Test.FrameHeldAlreadyIsNotCopiedAgain`).
- The window: a smooth plan that is the frame latched already uses the latched texture, no CPU / GPU pass.
- The wall's `auto`: per tile, the cheap interpolation where the display beats with the machine, off (no engine)
  where it does not (`WallTileMode`, `SmoothTile_Test.WallAutoDrawsOnlyWhereTheDisplayBeats`).
- Not done: skipping the flicker search on frames ZX DLSS already processed. Its output can still flicker (the border,
  content it leaves), so the same masks are not certain.

### 1.12 LayerFlow: tests, hardening, a page flip, the cost (2026-11-26)

A review of LayerFlow (`core/src/emulator/video/smoothlayers.{h,cpp}`, `MotionField::Estimate`, `FieldFor`,
`ComposeLayers`) and its GPU mirror (`unreal-qt/src/widgets/smoothmotiongpu.cpp`) asked for tests where the old ones
did not reach, three hardening points and six optimizations. All of it below; every optimization kept gives the
same bits.

New tests (about 10 ms each; the screen's size behind `TestTier::Long()`):

| Test | What it holds |
|------|---------------|
| `SmoothMotion_Test.LayersOnAPoolAtAnOddSizeAreTheCallersBits` | 101 x 109 (partial blocks, the last band partial): a pool's picture is the caller's |
| `SmoothMotion_Test.LayersAtTheEndWeightsAreTheFrames` | w 1 and 15 give a, w 241, 255 and 256 give b; w -50 and 400 clamped |
| `SmoothMotion_Test.LayersConfirmAMovingVectorByDetailOrByThreeFrames` | `KeepConfirmed` through `CommonVectors`: detail, exact over three frames, never by chance |
| `SmoothMotion_Test.LayersScrollByTheWholeRange` | a scroll of +-8 on both axes at 101 x 77, content at every edge: the interior exact |
| `SmoothMotion_Test.TheFieldIsTheSameWithAnyHint` | `Estimate`'s hint (below): the pair after, another scene, another size, none: the same field |
| `SmoothMotion_Test.SameBitsOnEveryCompiler` | 4 more hashes: dots past the cap, the cover's worst content, the layer scene at 101 x 77 at w 128 and 213 |
| `SmoothScenes_Test.PageFlipInventsNothing` | a page of text flipped (letters a cell apart), held or a page every frame, LayerFlow and the engine's modes |
| `SmoothMotionGPU_Test.LayerFlowAtTheEndWeightsIsTheCpus` | the end weights on the GPU, three scenes |
| `SmoothMotionGPU_Test.LayerFlowScrollingByTheWholeRangeIsTheCpus` | the +-8 scrolls at 101 x 77, w 1, 128, 255 |
| `SmoothMotionGPU_Test.LayerFlowOnAPageFlipIsTheCpus` | the page flip on the GPU |

`LayerFlowOnTheGpuIsTheCpus` runs at 101 x 77 now (its comment said a partial column, its size was 120 x 74). The
test scenes moved to `smoothscenes.h` (`LayerScene`, `TextPage`, `DotsOverLayers`).

Hardening:

- `Compose` clamps the weight to 0..256: past the pair a sample reads beyond the padded copies' margins (two
  vectors out). The GPU path clamps the same.
- `FieldFor` keeps a field the caller still holds: `ComposeLayers` asks for p -> a, then a -> b; the second could
  overwrite the first's slot of two. `FieldFor(..., keep)` takes the other slot then.
- `kLayersReads` (a fragment's texture reads, which cut a draw into stripes) was 1 % short: counted term by term
  now (`kLayersAllowed`, `kLayersWindow`), 5375 a fragment.

The page flip (the test above found it): 493 invented pixels at w = 128 on a held page, 580 with a page every frame,
392 through the engine. A letter of the new page is often an old one a cell over, matched exactly, with detail: the
eight cell vectors get confirmed and common, and 171 of 192 blocks take one. No single vector explains much: on a
flipped page the three vectors matched most hold 0.45-0.58 of the moving blocks; on every pair of the POC 055 suite
that changes a quarter of the picture (scrolls, parallax, strips) 0.80 or more. So `Prepare` calls a pair a cut when
a quarter of the blocks or more changed and the three most matched vectors hold under two thirds of the moving ones
(`kCutChanged`, `kCutLayers`, `kCutShare` / `kCutShareOf`): every block zero alone, `Decide` zero or nothing, the
nearer frame where zero does not fit. Both paths take it from `Prepare` and `Decide`. 0 invented in every case, POC
055 the same 42.93 (no pair of the suite is a cut). The dots scene that reached the cap had a vector a block (a cut
by this rule): `DotsOverLayers` keeps three layers over three quarters of the blocks and a vector of its own on every
fourth (still past the cap). `LiteFlow`'s tests keep the old `Dots`. The first four LayerFlow hashes stay; the four
new ones are recorded with the cut, the same on clang (.20) and gcc (.74).

The optimizations. Measured on .74 (Xeon E5-2696 v4, gcc -O3, a loaded machine: timings swing up to 2x between
rounds, so the instruction counts of callgrind are the steady measure), `layerbench` over 352 x 288 scenes, the same
hash of every field and picture before and after:

| Item | Done | Why |
|------|------|-----|
| (2) aliases over the pool | yes | `CpuEvidence::Ask` works out every own(...) the rule can ask (s slower than c: `Slower` is an order, so the chosen vector only gets slower), each ask its block, split over the pool; `Own` looks them up. Top two per pixel instead of three (the one that matters is the first that is not c; s fits none of the pixels counted), only the vectors fitting the block |
| (5) `Estimate` seeded | yes | the other cached field's moving vector bounds pass 1's fewest mismatches: the search before it stops each count early, and finds the same first vector of the fewest (the hint's vector in the range, its count known when reached) |
| (4) both window sums in one pass | no | the paired slide (padded columns, two chains of adds) was 2-3 % fewer instructions, the time the same or worse (flux -2.5 %, parallax +2.5 %, sprites +3 %) |
| (6) the cover's slower test, early out | no | the same instruction count: few still pixels reach it |
| (2) the regions loop | no | order-independent (the votes read only blocks decided alone, never a region's), but `Continues` is under 1 % |
| (1) vector-to-index tables | no | `Prepare` is 1-2 % of a picture |
| (3) window sums on needed rows only | no | paper fits every vector: every row is needed |

Instructions a call, one thread (callgrind, the mean of 2 triples):

| Scene | Compose before | after | Estimate before | after |
|-------|---------------:|------:|----------------:|------:|
| dots past the cap | 330.9 M | 290.3 M (-12 %) | 289.2 M | 272.8 M (-6 %) |
| flux | 140.2 M | 138.8 M | 20.7 M | 19.3 M (-7 %) |
| parallax | 94.5 M | 93.8 M | 23.7 M | 23.0 M |

Time, the best of 5 interleaved rounds, 8 triples (ms):

| Scene | Compose 1 thread before / after | 4 threads before / after |
|-------|---------------------------------|--------------------------|
| dots past the cap | 69.8 / 64.1 | 57.1 / 29.3 |
| flux | 29.9 / 22.0 (noise) | 11.4 / 10.9 |
| parallax | 16.0 / 13.0 (noise) | 8.6 / 7.9 |
| sprites | 12.8 / 10.1 (noise) | 6.9 / 8.7 (noise) |

The one large gain is the cap's worst case on a pool: `Decide`'s aliases ran on one thread (1.7's 3.2 ms on the
Deck). What is left in a picture is the vectors' pass (the fits and two window sums a vector), the pixels' pass and
the field; none of them has a cheap cut left.

Checks: core-tests and unreal-qt-tests with zero warnings on .20 (clang), core-tests on .74 (gcc); the smooth tests
in both tiers on both; the GPU's tests on GL (cocoa) on .20 in both tiers; POC 055 `score.sh` 42.93 before and
after.

### 1.13 LiteFlow: the palette, refused and revisited pairs, the cost (2026-11-26)

A review of `LiteFlow` (`core/src/emulator/video/smoothlite.{h,cpp}`) found four suspects; each checked by a test
first (the new tests run on the old code: which failed there), fixed where it was real, the pictures of POC 055's
12 scenes the same bits throughout (the hash of every picture, clang on .20 and gcc on .74, before and after).

| Suspect | Before | Cause | After | Test |
|---------|--------|-------|-------|------|
| A fading palette pops | the palette only grew (`Planes()` up to 8 for frames of 16 colours or fewer); when it filled it started over with the pair's two frames: the frame before a went with it, so a motion confirmed only by the frames before (a box of one colour, no detail) was not drawn at that pair, about every 16 frames of a fade (TSConf, Next, ULAplus); 8 planes cost twice 4 | `Palette` restarted instead of compacting | the palette is compacted to the colours of the frames kept (their planes re-indexed, ids and the frame before kept) when it fills, and before a frame is packed when it holds two planes' worth more than the kept frames need. A picture asks only which pixels are equal, never an index | `AFadingPaletteDrawsAsASteadyOne` (failed: pair 14 differed), `ThePicturesDoNotDependOnTheColoursIndices` (the invariance held on the old code too; it failed only on asking for a compaction), `ThePaletteIsCompactedWhenItFills` (packed 25 frames once each) |
| A refused pair costs every refresh | a pair of over 256 colours was packed again at every refresh, and each try started the palette over: the frames kept for the pair after went with it | no memory of a refusal | a refused pair (by its ids) is refused at once when asked again; a picture that cannot fit with the pair's other frame is found out before anything is dropped | `ARefusedPairIsNotTriedAgain` (failed: palette restarted, the good pair estimated and packed again) |
| Thick loader stripes | a block inside a stripe 8-16 lines high is of one colour, and a vector may carry it exactly over three frames by chance | — | not real: nothing invented in 12 frames of such stripes, engine and LiteFlow alone, both tiers | `ThickLoaderStripesInventNothing` (passed on the old code too) |
| A pair asked again | only the last pair was kept: (a, b), (k, a), (a, b) estimated (a, b) again from a field that had moved on: other bits | one pair of state | the last two pairs are kept (vectors, masks, the frame before); a pair asked again is drawn as the first time, and the kept field moves on only with a pair new to it. Four frames cached (two pairs and the frame before each), the least recently used packed over | `APairAskedAgainIsTheSamePicture` (failed) |

Also: `ResetAndAnotherSizeStartFresh` (Reset, and a stream of another size first, give the pictures of a fresh
instance) and `SmoothTile_Test.LiteOverItsColoursIsDrawnByTheLayersAndStillGuarded` (an `interpolate-lite` tile of
frames over 256 colours: LayerFlow draws, a slow picture still steps the tile down to the blend).

The cost, the same bits (`ThePicturesAreTheSameEverywhere`'s hash and every picture of the 12 scenes unchanged):

- `Confirm` asks only what it needs: per vector the blocks the field matched to it (`own` before `Credible`),
  detail and `unlike` per word when first asked, the block that confirmed a vector at the pair before first,
  nothing when no vector moves (`Credible` neither); `unlike` is the pair before's `a ^ b`, kept by its `Credible`.
- `Credible` computes `a == b` once for all vectors, and keeps each vector's `onward | from` for `Draw` (it does not
  depend on the weight: `Draw` runs at every refresh, more often than a pair is estimated); `Draw` takes `same` from
  it too.
- `Pack`: an open-addressing table for the colours (no `unordered_map`), the last two colours remembered (ink and
  paper), a word of each plane written once (no zero fill of all eight planes), only the planes the eight indices
  have.

POC 055 `score.sh` (12 scenes): 57.65, every scene as before (strips 2.17, vertical 2.89, diagonal 0.57, overlay 1.00,
textlines 0.86, parallax 5.38, sprites 8.02, cadence 6.54, dotmatrix 7.20, thin 1.38, flux 4.07, animated 17.57).

A/B, one thread, the 12 scenes' sources (352 x 288, 300 frames, 60 Hz refreshes) through `Compose` as the POC's
harness asks them, best of 5 interleaved rounds (A and B in turn per scene and round), ms a refresh:

| Scene | Before | After | |
|-------|--------|-------|---|
| strips | 1.33 | 1.04 | -22 % |
| vertical | 1.39 | 0.96 | -31 % |
| diagonal | 0.94 | 0.79 | -16 % |
| overlay | 0.89 | 0.66 | -26 % |
| textlines | 0.77 | 0.54 | -29 % |
| parallax | 1.07 | 0.84 | -22 % |
| sprites | 1.07 | 0.60 | -44 % |
| cadence | 0.80 | 0.63 | -21 % |
| dotmatrix | 0.75 | 0.64 | -15 % |
| thin | 1.23 | 0.68 | -45 % |
| flux | 1.12 | 0.92 | -17 % |
| animated | 0.90 | 0.71 | -21 % |

gcc -O3 on .74 (x86-64, a shared host under load: single runs spread up to 2x, dotmatrix and sprites best of 12).
The profile of what is left (sprites): the field search, `Pack`, `Draw`, `Credible` in about equal parts;
`Confirm` is under 1 %.

### 1.14 Metal: what "a refresh late" counts (2026-11-27)

The owner's Mac (60 Hz, display link, Pentagon 128K on `aleste1.sna`, 48.83 Hz, a World of Warcraft window open)
showed 1313 of 23187 presents a refresh late in steady state (5.7 %), in blend too (about 1 ms a picture), and in
interpolate in bursts of 4-5 with seconds of none between. The question: real missed refreshes, an artefact of the
count, or our own scheduling. Nothing was changed but this section.

**What is counted** (`MetalPresenter::PresentFrame`, `PresentLead::Shown`). On the display link the window asks
for each present half a refresh before the refresh it is for (`at = pacedRefresh - period / 2`, the refresh taken
from the link's `targetTimestamp` grid, a lead of a refresh plus the margin ahead). The presented handler compares
`MTLDrawable.presentedTime` (turned to the steady clock) with that `at`: on time is `period / 2` = 8.33 ms, a refresh
late 25.0 ms. Late is an error over `period + offset + 1 ms`, the offset being the 5th percentile of the on-time
errors capped at 3 ms: on this path always the cap (the on-time errors are 8.33 ms), so the line is at 20.67 ms,
12 ms above on time and 4 ms below a refresh late. No rounding of the 48.83 Hz targets enters it: every present (off
too) asks for a refresh on the link's grid, not for the frame's own time, and presentedTime jitter of a few ms cannot
cross the line. A present counted late was shown at least one refresh after the one it asked for.

**The bursts are one hold each.** Metal shows the queued presents in order. A present is handed over 1.3-2.3
refreshes before its refresh, so when the compositor holds one a refresh, the two queued behind it and the one or two
handed over before its presented handler reports (at its screen time) are shown a refresh late too: 4-5 counted for
one hold. The window then leaves a refresh out (`LastLatePresent`, section 1.3 and the frame presenter README,
section 6.2) and the presents after it are on time again. Each of those presents really was shown a refresh after
its time (the count is right), but the screen missed one refresh, not five. 1313 lates at 4-5 a hold are about 300
holds in 386 s, one every 1.3 s.

**Our own scheduling is not short of lead** (.20, link-paced presents from `UNREAL_PRESENT_TRACE`, blend, 1744
presents): the times asked step by exactly one refresh (1739 of 1744; the rest around a 1 s stall), at a constant
phase; each present handed over p5 20.1 ms, p50 21.8 ms before its time asked, 28-30 ms before its refresh;
`nextDrawable` p50 0.07 ms. Off (1422 presents, 48.83 Hz on 60): steps of 1 and 2 refreshes, handed over p5 23.2 ms
before. A late present handed over this far ahead was held by the system, and the lead rightly does not widen for it
(`aheadNs >= period`). The earlier measurement on the loaded M1 Ultra (frame presenter README, section 6.2) saw the
same: the compositor a refresh behind every 3-4 s for 0.1-0.6 s, presents handed over 20-35 ms ahead.

**.20 cannot show screen times now.** Its only display is the Screen Sharing virtual display (3840 x 2160, 60.00 Hz,
fixed), the console locked and the display asleep: the presenter reports the window occluded and draws nothing.
With the occlusion check forced off (a local patch, not kept) every present came back with presentedTime 0: 30 s each,
off 1419 presented / 1419 dropped, blend 1741 / 1741, interpolate 1426 / 1429, 0 shown, so 0 late means nothing
there. The display link ticks only while the display is awake (`caffeinate -u`, about 25 s), then the timer path
takes over (interpolate then has no refresh grid and draws nothing). A 1 s `nextDrawable` wait about 5 s after each
`smooth_motion` change is the asleep display holding the drawables, not seen on a real one.

**Verdict.** No false late is counted and our scheduling hands every present over more than a refresh and a half
before its refresh: the lates on the owner's Mac are holds of the system (most likely the other GPU load), counted
4-5 times each by the queue. To confirm: 30 s of `UNREAL_PRESENT_TRACE` per mode on that Mac with the game open and
closed; the lates' `asked - submitted` (all above a refresh: held) and their run lengths (one hold per run) settle it.

## 2. Platform by mode, from the code (before the steps)

| | Fixed grid | VRR | Display rate switch | Smooth motion | Low latency |
|---|---|---|---|---|---|
| macOS Metal | yes | by the screen's range, unchecked (V5) | yes | yes | drawables queue (1.2) |
| Windows DXGI | yes | V1, V2 | V2-V4 | yes | yes |
| Linux X11, timed GL | yes | full screen + `vrr_capable` only | yes (RandR) | yes | no replacement (1.2) |
| Linux Wayland, GL | yes, no screen times | policy `vrr` only, blind | no | by the paint's time, no grid | yes |
| Linux Vulkan | yes | X11: `vrr_capable` + probe (V6, unmeasured); Wayland: policy `vrr` only | X11 (K5); Wayland no | K5 | no replacement (1.2) |
| Steam Deck, gamescope | yes | gamescope's root atoms (V7) | yes (`gamescope_control`) | yes, on gamescope's composition times | no replacement (1.2) |

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
