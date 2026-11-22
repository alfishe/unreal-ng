# TODO — Presenter stabilization

Plan and findings: [README.md](README.md). A step is done when its findings are reproduced by a test or a measurement,
fixed, and checked on macOS, Linux (.238), Windows and the Steam Deck where it applies.

- [x] 0. Video Synchronization window: tiles, only what judges the mode in effect (`0d30e77ea`)
- [ ] 1. One VRR verdict for every presenter (V1-V5 in code; hardware checks: G-Sync on .101, ProMotion)
- [ ] 2. A waiting present is replaced on the timed GL swap, Vulkan and Metal
- [ ] 3. Smooth motion: S1-S5
- [ ] 4. Vulkan: K1-K9, V6
- [ ] 5. Display events; V7
- [ ] 6. Content and displays: scenes for the classes not covered, 75-240 Hz, hi-res modes
