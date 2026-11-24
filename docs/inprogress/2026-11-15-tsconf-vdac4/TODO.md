# TODO — VDAC4 for TS-Conf

Status 2026-11-23: on master. `TSL-VDAC4` runs DOOM-VDAC4 and R-Type VDAC4 and the VDAC2 programs: the Tang FT812
core (eve-emu VDAC4 profile), the ESP32-S3 (`unreal-xtensa` with a block JIT, the firmware ABI as HLE, the app as
jobs on a worker thread) and the board state on every automation plane. Design and phase status:
[design.md](design.md) §11. Speed: [eve-async-drawing.md](eve-async-drawing.md) §6.

## Open

- [ ] **TTD** (design §10.1): the devices `vdac4-tang`, `vdac4-esp-link` and `vdac4-esp-cpu` with their own
  histories; a seek inside DOOM gameplay reproduces the frames; zero cost with the board absent or recording off
  (A/B). Before the work: measure what an ESP history costs (the pages the app dirties per frame, the state's
  size, the checkpoint time), so the design fits the numbers.
- [ ] HMM2-VDAC4 to the adventure map with music (OGG module): needs the game's `HMM2.DAT` from the owner.
- [ ] DEMO3D (BL616 3D commands A0-AF): implemented in `eve-vdac4-3d.cpp`, no program to check it with.
- [ ] x86 speed: the A/B numbers of the 2026-11-22 / 23 rounds on a quiet x86 host (the Linux host was loaded,
  the run-to-run noise there is about 2 %).

## Not planned (design §5.2, §7.1)

- Xtensa PIE / SIMD `EE.*`, MAC16, `RECIP0.S` / `RSQRT0.S` / `DIV0.S` / `SQRT0.S`, privileged instructions: the
  apps do not use them; the interpreter stops the task with the PC if one is met.
- The Tang's renderer omissions, HDMI 1366×768, dithering, frame drops: eve-emu draws the full FT812; cosmetic.

## Done

- [x] Phases 1a-5 of design §11 (R-Type, the image decoders and 3D in code; HMM2 and DEMO3D unchecked, above).
- [x] Automation: `vdac4 state` / `log`, the ESP app memory, thread counters on CLI, Python, Lua, MCP, REST and
  WebSocket; the docs chain; the MCP block `test_vdac4.py`.
- [x] Speed: the ESP as jobs on a worker thread, the FT812 drawing on the eve-emu renderer thread, the Xtensa
  block JIT (POC 053), FT812 bus bursts and the frames barrier ([eve-async-drawing.md](eve-async-drawing.md)).
