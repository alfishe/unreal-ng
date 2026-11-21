# POC 053 measurements, 2026-11-19 (round 1)

Host .238: GNOME Shell 50.1 (Mutter) Wayland, panel 1920x1200 @ 59.950 Hz (fixed refresh), Intel UHD (TGL GT1),
Mesa ANV 26.0.8, Vulkan 1.4.335. Window 1280x720 xdg toplevel, swapchain 3 images, BGRA8, clear-based picture.
Frame stream 50 Hz on CLOCK_MONOTONIC, 3000 frames per run (first 50 skipped). Raw CSV and logs:
`scratch/2026-11-19-poc-053/` on the host. Summaries: `python/summarize.py`, `python/deadline.py`.

**Caveat:** the host was loaded by other work (load average 19-27 on 16 CPUs: ffmpeg, compilers, localsearch) during
every run. Counts (lost, doubled frames) are robust; the ms spreads are an upper bound and must be re-run on an idle
host before a decision on numbers.

Compositor globals: `wp_presentation` v2 (clock 1 = CLOCK_MONOTONIC), `wp_commit_timing_manager_v1` v1,
`wp_fifo_manager_v1` v1. Present modes offered: FIFO, MAILBOX (no IMMEDIATE, no FIFO_RELAXED).
Device extensions used: `VK_KHR_present_id`, `VK_KHR_present_wait`.

## Q1. Screen times: go

| Run | frames with a `wp_presentation` time | `present_wait` return - screen time p1 / p50 / p99 |
|---|---|---|
| a FIFO lead 4 ms | 2950 / 2950 | -0.36 / -0.01 / +0.35 ms |
| a MAILBOX lead 4 ms | 2925 / 2950 (25 discarded) | -0.33 / -0.14 / +0.18 ms |

- `wp_presentation` feedback requested by the client on the surface Mesa's WSI commits works: every FIFO frame has a
  time, flags 7 (VSYNC | HW_CLOCK | HW_COMPLETION), refresh 16.681 ms.
- `vkWaitForPresentKHR` returns within ±0.4 ms of the compositor's time (Mesa implements it with its own
  `wp_presentation` feedback), so either can drive the presenter; `wp_presentation` also gives the refresh period.

## Q2. Present at a time

### a-timed-thread (present at target - lead)

| Run | on the first refresh after the target | late (a refresh missed) | early | error p50 / p99 | lost / doubled |
|---|---|---|---|---|---|
| FIFO lead 1 ms | 61.6 % | 1132 | 0 | 15.2 / 30.3 ms | 0 / 0 |
| FIFO lead 2 ms | 63.8 % | 1069 | 0 | 14.9 / 25.9 ms | 0 / 0 |
| FIFO lead 4 ms | 90.0 % | 293 | 2 | 10.3 / 24.1 ms | 0 / 0 |
| FIFO lead 6 ms | 91.0 % | 228 | 38 | 9.4 / 25.1 ms | 0 / 0 |
| FIFO lead 8 ms | 85.1 % | 143 | 297 | 6.9 / 24.7 ms | 0 / 0 |
| MAILBOX lead 4 ms | 51.3 % | 1424 | 0 | 17.0 / 28.5 ms | 25 lost |
| MAILBOX adaptive lead (→ 14 ms) | — | — | — | 2.2 / 16.5 ms | 30 lost |

"Lost / doubled": refresh slots beyond the 50-on-60 cadence (repeated slots 586 expected ≈ 2950/5; MAILBOX 611-617).

- FIFO never loses or doubles a frame; MAILBOX drops 25-30 per minute and adds a refresh of latency: MAILBOX is out.
- The make-or-miss of a refresh is not a sharp deadline (`deadline.py`): with 0-1 ms headroom 9-24 % of frames
  still make it, with 12-15 ms headroom 6-14 % still miss. Mutter's latch moment wanders by several ms (dynamic max
  render time, the host's load).
- **grid** variant (present at the predicted first refresh after the target minus a margin, the grid from the last
  screen time): margin 7 ms 77 % on time, 3-5 ms nearly all one refresh late, 9 ms a 100 ms tail. No better than a
  fixed lead.
- With the best constant delay per run, 70-74 % of frames land on the predicted refresh in every FIFO variant
  (`summarize.py --consistency`): about one frame in four takes the neighbour refresh, i.e. the 2-refresh hold of the
  50-on-60 cadence moves by one frame. Re-measure on an idle host.

### b-commit-timing on the WSI's surface: no-go (protocol error)

`wp_commit_timing_manager_v1.get_timer` on the surface given to `vkCreateWaylandSurfaceKHR` kills the connection
when the swapchain is created: `error 0: Commit timing resource already exists on surface`. Mesa 26.0 WSI creates its
own timer on every surface and, in FIFO, sends `wp_commit_timer_v1.set_timestamp` plus `wp_fifo_v1.set_barrier /
wait_barrier` on every present (WAYLAND_DEBUG trace), and commits the surface twice per present.

### c-commit-timing on a parent surface (Vulkan in a synchronized subsurface): no-go

Parent toplevel with a black shm buffer, our timer on the parent, Vulkan on a `wl_subsurface` in sync mode, the
parent committed with `set_timestamp(target)` after each `vkQueuePresentKHR`. Every feedback, on the child (Mesa's
second empty commit replaces the first in the subsurface cache) and on the parent (with or without damage, with or
without the timer, FIFO or MAILBOX), comes back `discarded`; `present_wait` returns at once. Mesa's own FIFO barrier
and timer do not compose with a synchronized subsurface.

### The supported way: VK_EXT_present_timing (Mesa 26.1)

Mesa 26.1 merged `VK_EXT_present_timing` for X11 and Wayland: present at an absolute time through Mesa's own commit
timer, plus past presentation timings. The host's Mesa 26.0.8 does not offer it. This is the path for Q2 on Wayland;
the client-side commit timer is closed to us while Mesa owns the surface.

## Not measured yet

- Q3 GL -> Vulkan (external memory fd), Q4 inside a Qt `QWindow`, llvmpipe.
- Q5 VRR: the panel is fixed 60 Hz; `wp_presentation` has no VRR flag (flags seen: 7 only), so a client can only
  infer VRR from a varying presented interval. No readable signal so far.

## Interim conclusion

Go on Q1: a Vulkan presenter on Wayland gets exact screen times (the main gap of the GL/EGL path today).
Q2 is a partial no-go on Mesa 26.0: the timed thread with FIFO and a 4-6 ms lead is the best available (no lost or
doubled frames, 90-91 % on the first refresh after the target under load); exact timing needs
`VK_EXT_present_timing` (Mesa 26.1+). Next: re-run Q2 on an idle host, then Q3 and Q4.
