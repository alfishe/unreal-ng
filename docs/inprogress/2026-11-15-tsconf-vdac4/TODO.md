# TODO — VDAC4 for TS-Conf

Status 2026-11-24: on master. `TSL-VDAC4` runs DOOM-VDAC4 and R-Type VDAC4 and the VDAC2 programs: the Tang FT812
core (eve-emu VDAC4 profile), the ESP32-S3 (`unreal-xtensa` with a block JIT, the firmware ABI as HLE, the app as
jobs on a worker thread), the board state on every automation plane and time travel
([ttd-design.md](ttd-design.md)). Design and phase status:
[design.md](design.md) §11. Speed: [eve-async-drawing.md](eve-async-drawing.md) §6.

## Open

- [ ] HMM2-VDAC4 to the adventure map with music (OGG module): needs the game's `HMM2.DAT` from the owner.
- [ ] DEMO3D (BL616 3D commands A0-AF): implemented in `eve-vdac4-3d.cpp`, no program to check it with.
- [ ] TTD tuning, one item at a time with the seek check and an A/B: [ttd-design.md](ttd-design.md) §10.
- [ ] x86 speed of Zuma: its rounds on .238 ran under foreign load (below); a quiet repeat.
- [ ] Metal Slug frame cost, in the owner's order (2026-11-26), every step with an A/B and no regression: the FT812
  (done, below), then the Xtensa JIT (regions chained across CALL / ENTRY / RETW: as many interpreter entries as
  native ones today), then the TS-Conf side (DramWait, the arbiter, DrawRange, the interrupt catch-up).

- [ ] Quake II: time travel for the `q2` personality (the core's registers, 8 MB SDRAM and on-chip tables per
  keyframe); today TTD is unavailable while it runs, with the reason ([quake2.md](quake2.md) §8).
- [ ] Quake II PSRAM: Qcommon_Init takes ~220 KB more PSRAM here than in the author's emulator, so the reserve
  stays at 1440 KB (4336 KB free before init, board 4192); at 1584 KB base2 runs out of memory ([quake2.md](quake2.md) §8).
- [x] Quake II: the firmware's LINK watchdog (tang_link.cpp): the `0x5C564434` mark in SCRATCH, checked every 2 s; a
  foreign value with ID answering logs "LINK: Tang reloaded" and writes the mark again. Q2's `link_diag` at core open
  still logs `SCRATCH 00000000 BAD` (the fresh bitstream, as on the board); later checks see the mark.

## Not planned (design §5.2, §7.1)

- Xtensa PIE / SIMD `EE.*`, MAC16, `RECIP0.S` / `RSQRT0.S` / `DIV0.S` / `SQRT0.S`, privileged instructions: the
  apps do not use them; the interpreter stops the task with the PC if one is met.
- The Tang's renderer omissions, HDMI 1366×768, dithering, frame drops: eve-emu draws the full FT812; cosmetic.

## Done

- [x] Quake II for ZX Evolution VDAC4 (2026-11-27, [quake2.md](quake2.md)): firmware string
  `vdac4-0.1.174-unreal-ng-hle`, ABI 1.21 (`data_open` / `data_read` / `data_close` over 0x6E / 0x6F through the Z80
  program, `link_read_checked`), MOD_QUERY flags 2 (cores into the BL616 cache) and 4 (data files), realloc in
  place, `core_load` / `core_back`, the bomzh-3D core (vendored C model, `3rdparty/bomzh`) as the Tang personality
  `q2` with its own 424x240 picture, PSRAM reserve `UNREAL_VDAC4_PSRAM_RESERVE_KB`, state fields on every plane.
  The demo runs (base2) at the game's own 30-35 fps figure.

- [x] FT812 drawing for Metal Slug (2026-11-26): LinePlan bands and history, index signatures, LineRepeat, palette
  hits; single-thread replay of a Metal Slug capture 1.73 -> 0.67 s in `EveAdvance`, the same picture hashes for
  1-4 line threads. `FtDrawThreads` defaults to 1 (owner, after the 1-4 thread A/B: no faster frame, 20-50 % more
  CPU with more threads); `threads.ft_draw_threads` on every plane. Captures start and stop with the CPU held and the
  ESP job done with the chip (`Vdac2Control`, `WaitChipIdle`), and the frame a capture starts in is recorded as not
  drawn: live Metal Slug captures replay with 0 answer, frame-count and picture mismatches.

- [x] Phases 1a-5 of design §11 (R-Type, the image decoders and 3D in code; HMM2 and DEMO3D unchecked, above).
- [x] Automation: `vdac4 state` / `log`, the ESP app memory, thread counters on CLI, Python, Lua, MCP, REST and
  WebSocket; the docs chain; the MCP block `test_vdac4.py`.
- [x] Speed: the ESP as jobs on a worker thread, the FT812 drawing on the eve-emu renderer thread, the Xtensa
  block JIT (POC 053), FT812 bus bursts and the frames barrier ([eve-async-drawing.md](eve-async-drawing.md)).
- [x] **TTD** (design §10.1): the devices `vdac4-tang`, `vdac4-esp-link` and `vdac4-esp-cpu` with their own
  histories; a seek inside DOOM gameplay reproduces the frames; zero cost with the board absent or recording off
  (A/B). Before the work: measure what an ESP history costs (the pages the app dirties per frame, the state's
  size, the checkpoint time), so the design fits the numbers.
  - Measured 2026-11-23 (DOOM demo after 6000 frames, ESP memory read through `GET /vdac4/esp/memory`, 4 KB
    pieces, zstd of the XOR with the previous version as the piece store does): 887 of 2152 pieces non-zero
    (3.5 MB); a frame changes 7-95 pieces (90 on a game tick, about every third frame), 45-62 KB/frame
    compressed, about 2.3-3 MB/s = 140-190 MB per minute at a checkpoint per frame. The set is hot: 125 pieces
    (500 KB) over 250 frames, so a checkpoint every 50 frames costs 1.4 KB/frame (about 4 MB per minute).
    zstd of the changed pieces: 0.8-0.9 ms/frame on a loaded M1 Ultra (load 100; DOOM itself 3.7 ms/frame on
    an idle M3 Ultra). For comparison the Z80 side of this recording stores 5-10 KB/frame (session heap growth).
  - Today a recording on `TSL-VDAC4` starts and is silently incomplete: no VDAC4 device in its peripherals,
    `not_recorded` empty, and after a seek back 100 frames the ESP memory is neither the recorded one nor
    the present one. Until the devices exist, a recording with the board must be refused (or list the board in
    `not_recorded`).
  - Landed 2026-11-24 (`1f0154632`, `293d25062`): sparse ESP keyframes and an input journal,
    [ttd-design.md](ttd-design.md); DOOM recorded at 2.80 ms per frame (1.94 off), 18.7 KB per frame, 55 of 55 seeks
    restore the ESP memory exactly (§9). Open items there (§10).
  - Direction: the ESP app is deterministic from its inputs (the LINK traffic is in the port journal), so
    sparse ESP keyframes (every 25-50 frames, a sync point with the worker only there) plus re-running the ESP
    from the keyframe on a seek; the Tang / FT812 state (RAM_G 3 MB, mostly uniform) recorded per frame like
    VDAC2's RAM_G.
- [x] x86 speed (2026-11-24, .238 i7-11850H, gcc 15, Release + LTO, 5 ABBA rounds per game on pinned cores,
  84fb17657 vs 79116d336, ms per frame): R-Type VDAC2 5.72 -> 4.97 (-15 %), R-Type VDAC4 4.30 -> 3.85 (-9.4 %),
  DOOM 3.71 -> 3.74 (noise), Heroes II 4.73 -> 4.90 (paired -1.4 %, neutral); pictures bit-exact in every round.
  A/A spread on a quiet host about 10 % (the first run of a pair slower, hence ABBA).
