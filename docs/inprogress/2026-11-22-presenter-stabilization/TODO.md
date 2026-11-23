# TODO — Presenter stabilization

Plan and findings: [README.md](README.md). A step is done when its findings are reproduced by a test or a measurement,
fixed, and checked on macOS, Linux (.238), Windows and the Steam Deck where it applies.

- [x] 0. Video Synchronization window: tiles, only what judges the mode in effect (`0d30e77ea`)
- [ ] 1. One VRR verdict for every presenter (V1-V5 in code, `9c45c7c79`; hardware checks: G-Sync on .101, ProMotion)
- [ ] 2. A waiting present is replaced: the timed GL swap done (a pile of two); Vulkan and Metal need back-pressure
  in the window, not built (no pile reproduced: README 1.2). Found on the way and fixed: K10, K7
- [x] 3. Smooth motion: S1-S5 (counters `present.smooth.behind` / `starved`)
- [ ] 4. Vulkan: K1-K3, K5-K7, K9, K10 done; open: K4, K8, V6 (README 1.4)
- [ ] 5. Display events: the screen's rate followed, smooth motion starts over when it comes on again; open: the
  timed GL swap's wait without a deadline, sleep / wake, V7 (README 1.5)
- [ ] 6. Content and displays. Done: interpolation too slow for the refresh blends (`too_slow`), gamescope's screen
  time offset measured (no false lates), smooth motion by hand only where the display beats. Open: scenes for the
  classes not covered, 75-240 Hz and hi-res measured, the interpolation's cost on the Deck's GPU (README 1.6)
