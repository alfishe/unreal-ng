# DONE — Frame presenter: video timed by the sound, with and without VRR

Status: **Done 2026-11-08.** Phases 1-8 of the plan are on master in one piece (`29382cdde`, the merge of the
`frame-presenter` branch), as the owner decided: every frame carries the time its sound plays, the window presents it
at that time on a VRR display and picks the frame of each refresh on a fixed one; the display never paces the
emulation. Design and measurements: [README.md](README.md); the POCs:
[037](../../../tools/poc/037-frame-presenter/README.md) (presenters), [047](../../../tools/poc/047-smoothing-filters/README.md) (smoothing layers),
[049](../../../tools/poc/049-motion-interpolation/README.md) (motion interpolation). Automation recipe:
[.recipe/media/frame-presenter.md](../../../.recipe/media/frame-presenter.md).

Checked before the landing (2026-11-08, the merge commit): macOS build without warnings, core-tests and 200 Qt tests;
Linux gcc in the CI image (`docker/linux/build.sh --test --qt-gl`) 0 warnings, core-tests 11060 of 11061 and 201 Qt
tests; Windows MSVC 0 warnings, core-tests 11055 of 11056 and 214 Qt tests on the real platform; the Steam Deck
bundle in Game Mode (README section 4.7). The two red tests are not the presenter's:
`HostActionsMachine_Test.RecordStartAndStopAreTheRecAndStopKeys` needs ffmpeg, which the Linux image lacks;
`GSCardTarget_Test.TheMachinesCardStepsThroughRunControl` failed once in a Windows shard and passes alone.

## Phases (README section 8)

- [x] 1. Audio clock and frame times: `AudioClock`, `FrameClock` (audio, emulated and latch clocks, the carry),
  `Screen::CopyFrameAt` / `CopyNextFrame`, `[VIDEO] LipSyncOffsetMs` (replaces `AVSyncDelayFrames`)
- [x] 2. The GL path picks frames by time; screenshots, recording and the video wall read by time too
- [x] 3. Metal presenter (macOS): `c-vrr` present at time, `presentedTime` stats, occlusion, the adaptive lead;
  `b-vsync` on a display link on macOS 14+ (`UNREAL_PRESENT_DISPLAY_LINK`), the window's timer on macOS 12-13
- [x] 4. DXGI presenter (Windows): the interop ring, the presenter and vblank threads, the VRR check and its
  fallback, recovery from a GPU reset (`UNREAL_TEST_GPU_HANG`)
- [x] 5. Smooth motion: blend, interpolation (`LayerFlow`, POC 047), the game cadence modes from the profile's
  `video:` section; integer, the same bits on CPU and GPU (`SmoothMotion_Test`, `SmoothMotionGPU_Test`)
- [x] 6. Automation planes: `smooth_motion`, `effects_device`, `present_policy`, `display_rate`,
  `lip_sync_offset_ms`, the `present` report and the `video.present` topic on CLI, Python, Lua, MCP, REST and
  WebSocket
- [x] 7. Display rate switch (`[VIDEO] DisplayRate`): macOS, Windows, X11 RandR, gamescope (Steam Deck Game Mode)
- [x] 8. Video Synchronization: View > Video Synchronization, `present.sync`, Tools > Video Synchronization
- [x] Linux level 1: `gl-timed` on `GLX_OML_sync_control`, VRR detection on X11, `docker/linux/build.sh --qt-gl`

## For the owner to check on a monitor

The agent's shell has no window server on these hosts, and windows are not opened on the owner's Mac unasked.

- [ ] View > Video Synchronization on the G-Sync monitor of the Windows host (.101), windowed and full screen
- [ ] The same on the Mac (Metal, display link on the fixed display)

## Deferred (README section 8 item 9 and section 9)

- [ ] A Vulkan / Wayland presenter (`VK_KHR_present_wait`, `wp_presentation`, `wp_commit_timing`): screen times,
  the refresh grid and a VRR query on Wayland
- [ ] The iOS host on `FrameClock` and the Metal presenter
- [ ] Late input sampling in `c-vrr` (input-latency doc, option B)
- [ ] A hint naming the driver setting when G-Sync stays off in a window (the fallback to `b-vsync` is built)
- [ ] The audio clock's own target interval error, isolated (measured so far only inside the present errors)
- [ ] ProMotion on macOS 14+ and a GPU reset on Metal: not measured (no such machine at hand)
- [ ] `display_rate=match` on KDE Wayland (`kde_output_management_v2`)
- [ ] Smooth motion: `Decide` on the GPU (one read-back per refresh instead of four); the profile field
  `picture_ready`
- [ ] The on-screen tail under G-Sync (p99 1.7-3.5 ms): an optical measurement would tell the kernel from the panel
