# TODO — Presenter stabilization

Plan and findings: [README.md](README.md). A step is done when its findings are reproduced by a test or a measurement,
fixed, and checked on macOS, Linux (.238), Windows and the Steam Deck where it applies.

- [x] 0. Video Synchronization window: tiles, only what judges the mode in effect (`0d30e77ea`)
- [ ] 1. One VRR verdict for every presenter (V1-V5 in code, `9c45c7c79`; hardware checks: G-Sync on .101, ProMotion)
- [ ] 2. A waiting present is replaced: the timed GL swap done (a pile of two); Vulkan and Metal: back-pressure in
  the window (`PresentSlot`, README 1.2), Metal's pool full held too and its lead no longer widened by the pool's
  waits; unit tests positive and negative, live on Metal and on Vulkan FIFO on the Deck (README 1.2). Found on the
  way and fixed: K10, K7. Open, found on the way: the Deck bundle has no Vulkan renderer (its build image lacks the
  Vulkan headers); smooth motion stays off under Vulkan on the Deck
- [x] 3. Smooth motion: S1-S5 (counters `present.smooth.behind` / `starved`)
- [ ] 4. Vulkan: K1-K10 done (K8 measured on .238 with the session locked); V6 in code, open: a Linux VRR display
  (README 1.4)
- [ ] 5. Display events: the screen's rate followed, smooth motion starts over when it comes on again; sleep / wake
  from the code (a lead held by the gap, a failure run across a hidden window: fixed); V7 for gamescope (its root
  atoms); the GL window where the swap waits for nothing draws only what is new (remote desktop: ~250 a second for
  49 Hz frames). Open: the timed GL swap's wait without a deadline, V7 for a Wayland client, a sleep on hardware
  (README 1.5)
- [ ] 6. Content and displays. Done: interpolation too slow for the refresh blends (`too_slow`), gamescope's screen
  time offset measured (no false lates), smooth motion by hand only where the display beats. Scenes for the classes
  not covered written; the two LayerFlow / LiteFlow defects on border effects fixed (README 1.6). Open:
  75-240 Hz and hi-res measured, the interpolation's cost on the Deck's GPU (README 1.6)
- [ ] 7. The cheap interpolation (README 1.8, POC 055 `47ef92056`). Done: `LiteFlow` in the core, `interpolate-lite`,
  the two-step guard (also by drawing time: the software screen), `present.smooth.lite` / `stepped_down`. Measured in the live window on the Steam Deck: 56-57
  pictures a second, no late present, 1.3 ms a picture (README 1.8). The flicker search of
  `Push` on local bit planes (README 1.8: 6.3 / 16.2 ms to 2.1 / 3.5 ms a frame on the worst content). The video
  wall per tile with its two guards, measured on the Deck, off at no cost (README 1.9). The flicker search four
  times faster with the same masks on a corpus of 26 clips; the wall of 12 Across the Edge keeps 9-10 tiles smooth, 3-5 before (README 1.10). Open: a real low-end
  machine, Windows and gcc builds of it, the game cadence modes by it, the wall's software grid
- [ ] 8. The seams of the temporal filters (README 1.11): D1-D9 fixed with a test each, the frame copy skipped for
  the version held, the wall's auto per tile; each test fails with its fix undone (gcc, .74). D2b: the smooth guard
  steps down after three bad windows in a row, counts nothing in a window's first 3 s, retries after 10 s doubling to
  5 min. D2c: the Video Synchronization box shows the setting, not the step in effect. The live check of ZX DLSS with
  smooth motion (2026-11-27, unreal-qt on .20, `interpolate`, `interpolate-lite`, `blend`, `auto`, 30 s each plus DLSS
  toggled and modes switched mid-stream): no frame raw, missed or repeated; it found D10 (the read ahead cut by the
  whole delay: half the frames starved) and D11 (the delay's step carried: ~3 s starved after each switch on), both
  fixed with a test each (README 1.11). On Metal (the owner's 60 Hz Mac, 2026-11-27) it found D12 (starved 85 %:
  the presenter draws ~40 ms ahead), fixed, and D13 (whole runs every present a refresh late, evenly: the compositor's,
  not the drawing's, with or without ZX DLSS; the guard stepped down to a blend as late), fixed: the guard counts
  steps of the lag, not lates. The earlier ~5 % lates were the window hidden under a full-screen remote desktop client
