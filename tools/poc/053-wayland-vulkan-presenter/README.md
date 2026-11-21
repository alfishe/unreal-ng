# 053 — wayland-vulkan-presenter

Feature: [frame presenter](../../../docs/inprogress/2026-10-26-frame-presenter/README.md), section 6.2 ("Not
built: a Vulkan presenter") and section 9 ("Linux, level 1 limits"). On native Wayland the window has no screen times
today: the GL path swaps through EGL and cannot read `wp_presentation`, so there is no refresh grid for smooth motion,
no adaptive lead, the beat counts stay 0, and `c-vrr` can only swap from a timed thread with a fixed 8 ms lead.

## Goals

1. **Screen times on Wayland.** Can a Vulkan swapchain on the window's `wl_surface` tell when each frame reached the
   screen, and how exactly: `VK_KHR_present_wait2` (when it returns) against `wp_presentation` feedback (the
   compositor's timestamp, the refresh period, the flags)? Which of them can the presenter use while Mesa's WSI owns
   the surface's commits?
2. **Present at a time.** Can a frame be shown at its audio time:
   - **a-timed-thread**: a present thread waits until the frame's time minus a lead and calls `vkQueuePresentKHR`
     (FIFO and MAILBOX), as `gl-timed` does today;
   - **b-commit-timing**: `wp_commit_timer_v1.set_timestamp` on the surface before the present, so the compositor
     holds the commit until the time (Mutter 50 implements `commit-timing-v1` and `fifo-v1`). Does that work next to
     Mesa's WSI (one commit timer per surface; does Mesa make its own)?
   What error (screen time minus the frame's target) does each give, and how many frames are lost or doubled?
3. **GL draws, Vulkan shows.** The owner's rule for every native presenter (GL keeps the whole renderer): what does it
   cost to share the GL picture with Vulkan per frame (`GL_EXT_memory_object_fd` + `GL_EXT_semaphore_fd` against a
   `VkImage` exported with `VK_KHR_external_memory_fd`), and is the picture right (rows, colours)?
4. **In a Qt window.** Does all of it work on the `wl_surface` of a Qt 6.9 `QWindow` embedded the way the screen
   window is (`createWindowContainer`), or only on a top-level window / a subsurface?
5. **VRR on Wayland.** Is there anything a client can read (`wp_presentation` refresh 0 or a changing period, a
   flag) to tell a variable refresh, or does it stay `unknown` as on GL today?

## Requirements

- Linux, native Wayland: GNOME 50 / Mutter on host .238 (Intel TGL, Mesa ANV, Vulkan 1.4; also Mesa llvmpipe as the
  software case). XWayland is out of scope (GLX already gives times there).
- A frame stream like the emulator's: a frame every 20 ms (50 Hz) with a target time on `steady_clock`
  (CLOCK_MONOTONIC), on a display of another rate (the panel's 60 Hz), with and without the timed variants.
- A picture drawn by GL each frame (a moving pattern that shows tearing, doubling and row order).
- Every number from at least 3000 frames (one minute), reported as p50 / p99 / max and counts.

## Constraints

- POC code only in this folder (`cpp/`, `CMakeLists.txt`); nothing linked into `core/` or `unreal-qt/`. Adopting means
  a `VulkanPresenter` behind the existing `FramePresenter` interface, with its own tests.
- Built and run on .238 in a worktree of its own (`scratch/wt-vulkan-presenter` there), against the system's Vulkan
  and Wayland headers and `~/Qt/6.9.3/gcc_64`; protocol XML from `/usr/share/wayland-protocols` through
  `wayland-scanner`. Nothing installed on the host.
- The window opens on .238's own session (`WAYLAND_DISPLAY=wayland-0`), never on the owner's Mac.
- Raw logs in `scratch/YYYY-MM-DD-poc-053/` on the host; summaries in the measurement reports.

## Acceptance criteria / expected results

| Question | Go | Expected |
|---|---|---|
| 1. Screen times | a time for ≥ 99 % of presented frames, agreeing with the refresh grid within 1 ms | `wp_presentation` works on the WSI's commits; `present_wait2` returns a few ms after the flip |
| 2. Present at a time | error p50 ≤ 2 ms, p99 ≤ one refresh (16.7 ms), no frame lost or doubled beyond the 50-on-60 cadence | a-timed-thread works with a lead learned from the screen times; b-commit-timing may clash with Mesa's own timer |
| 3. GL -> Vulkan | ≤ 0.5 ms per 1080p frame on ANV, the picture identical | a GPU copy, well under a millisecond |
| 4. Qt window | works in an embedded `QWindow` | works; a container may need a subsurface |
| 5. VRR | a readable signal, or a clear "none" | none: stays `unknown` (no client query exists) |

No-go on 1 or 2 leaves Wayland on the GL path (`b-vsync` without times) and closes the deferred item with the reason.
