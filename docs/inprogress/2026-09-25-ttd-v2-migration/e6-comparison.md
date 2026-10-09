# Engine streams against the E6 model, and the state registry's Size and Variability

Status: measured 2026-10-09 on branch `ttd-engine` (engine backend, the controller), host load moderate (bytes are deterministic, no timings used).

## Method

- **BM-9, per region.** The benchmark matrix reports every region of the engine (`TTDBench`, `bm9_<region>_*`):
  - its size;
  - the bytes stored after the first frame (`first_bytes`: the first capture stores every piece once);
  - the steady rate after that, in bytes per frame and versions per frame (`steady_bpf`, `steady_vpf`).
  - Device states are regions named `device.<instance>`.
- **The registry columns.** The Size and Variability columns of [state-registry.md](state-registry.md) come from the full matrix (46 cases, 600 frames each):
  ```
  UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_ENGINE=controller UNREAL_TTD_BENCH_SEEKS=0 UNREAL_TTD_BENCH_OVERHEAD=0 UNREAL_TTD_BENCH_FRAMES=600
  ```
- **The E6 comparison.** It uses the six E6 sessions' counterparts in the matrix, at their own length of 3,000 frames (one minute): Pentagon idle / game / 7th Reality / Across the Edge / Eye Ache, and ZX-Evo (ATM3) idle. Figures are MB per minute (10^6 bytes, as E6).
  - Memory pieces: machine RAM plus the memory regions.
  - Device state: the `device.*` regions.
  - Coverage: the index's memory (E6's working set).

## Per stream, MB per minute

| Session | Stream | E6 v2 memory | E6 v2 file | Engine | Engine / E6 memory |
|---|---|---:|---:|---:|---:|
| Pentagon 128, BASIC prompt | memory pieces | 0.20 | 0.16 | 0.17 | 0.87 |
| Pentagon 128, BASIC prompt | reference table | 0.40 | 0.43 | 0.65 | 1.62 |
| Pentagon 128, BASIC prompt | device state | 0.80 | 0.81 | 0.47 | 0.58 |
| Pentagon 128, BASIC prompt | CPU + chipset | 0.50 | 0.50 | 1.18 | 2.36 |
| Pentagon 128, BASIC prompt | write journal | 0.10 | 0.06 | 0.08 | 0.79 |
| Pentagon 128, BASIC prompt | coverage | 3.20 | 0.02 | 3.20 | 1.00 |
| Pentagon 128, BASIC prompt | **total** | **5.20** | **1.98** | **5.75** | **1.11** |
| Pentagon 128, BASIC prompt | session file (bm7) | | 1.98 | 0.58 | file / E6 file 0.29 |
| Pentagon 128, game with input | memory pieces | 3.40 | 3.43 | 3.17 | 0.93 |
| Pentagon 128, game with input | reference table | 0.40 | 0.43 | 1.24 | 3.11 |
| Pentagon 128, game with input | device state | 0.90 | 0.93 | 0.58 | 0.65 |
| Pentagon 128, game with input | CPU + chipset | 0.50 | 0.50 | 1.21 | 2.42 |
| Pentagon 128, game with input | write journal | 22.50 | 22.49 | 22.15 | 0.98 |
| Pentagon 128, game with input | coverage | 6.10 | 1.04 | 6.14 | 1.01 |
| Pentagon 128, game with input | **total** | **33.80** | **28.82** | **34.50** | **1.02** |
| Pentagon 128, game with input | session file (bm7) | | 28.82 | 10.23 | file / E6 file 0.35 |
| Pentagon 128, 7th Reality | memory pieces | 2.10 | 2.09 | 1.95 | 0.93 |
| Pentagon 128, 7th Reality | reference table | 0.40 | 0.43 | 1.24 | 3.10 |
| Pentagon 128, 7th Reality | device state | 0.90 | 0.92 | 0.57 | 0.63 |
| Pentagon 128, 7th Reality | CPU + chipset | 0.50 | 0.50 | 1.19 | 2.38 |
| Pentagon 128, 7th Reality | write journal | 6.70 | 6.70 | 6.59 | 0.98 |
| Pentagon 128, 7th Reality | coverage | 3.90 | 0.39 | 3.91 | 1.00 |
| Pentagon 128, 7th Reality | **total** | **14.50** | **11.03** | **15.44** | **1.06** |
| Pentagon 128, 7th Reality | session file (bm7) | | 11.03 | 4.59 | file / E6 file 0.42 |
| Pentagon 128, Across the Edge | memory pieces | 2.90 | 2.94 | 2.57 | 0.89 |
| Pentagon 128, Across the Edge | reference table | 0.40 | 0.43 | 1.24 | 3.10 |
| Pentagon 128, Across the Edge | device state | 0.90 | 0.92 | 0.57 | 0.63 |
| Pentagon 128, Across the Edge | CPU + chipset | 0.50 | 0.50 | 1.19 | 2.38 |
| Pentagon 128, Across the Edge | write journal | 21.50 | 21.47 | 13.68 | 0.64 |
| Pentagon 128, Across the Edge | coverage | 4.90 | 0.37 | 4.93 | 1.01 |
| Pentagon 128, Across the Edge | **total** | **31.10** | **26.63** | **24.17** | **0.78** |
| Pentagon 128, Across the Edge | session file (bm7) | | 26.63 | 5.94 | file / E6 file 0.22 |
| Pentagon 128, Eye Ache | memory pieces | 2.10 | 2.10 | 1.97 | 0.94 |
| Pentagon 128, Eye Ache | reference table | 0.40 | 0.43 | 1.24 | 3.10 |
| Pentagon 128, Eye Ache | device state | 0.90 | 0.93 | 0.58 | 0.64 |
| Pentagon 128, Eye Ache | CPU + chipset | 0.50 | 0.50 | 1.19 | 2.38 |
| Pentagon 128, Eye Ache | write journal | 31.50 | 31.55 | 30.53 | 0.97 |
| Pentagon 128, Eye Ache | coverage | 4.80 | 0.20 | 4.77 | 0.99 |
| Pentagon 128, Eye Ache | **total** | **40.20** | **35.71** | **40.27** | **1.00** |
| Pentagon 128, Eye Ache | session file (bm7) | | 35.71 | 4.88 | file / E6 file 0.14 |
| ZX-Evo, BASIC prompt | memory pieces | 1.00 | 1.03 | 1.31 | 1.31 |
| ZX-Evo, BASIC prompt | reference table | 1.60 | 0.84 | 1.30 | 0.81 |
| ZX-Evo, BASIC prompt | device state | 0.80 | 0.82 | 0.47 | 0.59 |
| ZX-Evo, BASIC prompt | CPU + chipset | 0.50 | 0.50 | 1.47 | 2.94 |
| ZX-Evo, BASIC prompt | write journal | 1.40 | 1.40 | 1.10 | 0.78 |
| ZX-Evo, BASIC prompt | coverage | 3.30 | 0.04 | 3.30 | 1.00 |
| ZX-Evo, BASIC prompt | **total** | **8.60** | **4.63** | **8.95** | **1.04** |
| ZX-Evo, BASIC prompt | session file (bm7) | | 4.64 | 1.97 | file / E6 file 0.42 |

## What it says

- **The engine holds what E6 predicted.**
  - In memory, the totals are 0.78–1.11 of the model.
  - Memory pieces, the write journal and coverage are within 0.9–1.0, except Across the Edge's journal at 0.64: the matrix's snapshot plays another part of the demo than E6's session.
- **Device state is below the model** (0.58–0.65). Phase 2's time fields and changed-ranges encoding did better than E6's "changed blob, XOR" assumption.
- **Two streams are above the model, both fixed costs per checkpoint:**
  - Reference tables: 3.1× on 128K machines, 413 B per frame against 143.
  - CPU + chipset: 2.4–2.9×, about 400 B per frame against E6's 168 B, which was not delta-coded either.
  - They are 1.2–1.7 MB per minute: small next to the journal on active content, but most of an idle recording. Open in the [TODO](TODO.md).
- **The session file is 0.14–0.42 of E6's file model.** The file stores the journal's raw column blocks inside the container's zstd stream, which compresses across blocks. Memory keeps one zstd frame per 2,048 records, which is E6's model.
- **Device states that change in every frame at an idle prompt** (Variability "every frame"):
  - the AY / TSFM chips and the classic GS (Phase 2's Q1: real generator state);
  - the NeoGS (27–43 B/frame) and MoonSound (29–39 B/frame) cards;
  - the ATM2 and Profi keyboard controllers' MCUs (42–58 B/frame);
  - the TS-Conf state (25 B/frame);
  - the ZX-Evo AVR volatile bytes (6.5 B/frame);
  - the Sprinter PLD (8 B/frame);
  - the VDAC2 card (13 B/frame);
  - the ZiFi UART (6 B/frame).

  The cards and the TS-Conf, AVR and Sprinter entries are candidates for time fields: what changes in them at rest has not been traced field by field yet.
- **The Sprinter at its idle boot screen** rewrites 9.7 pieces of video RAM per frame (223 B/frame). That is the BIOS screen at work, not a capture artifact. The region is compared at each capture and stores only the pieces that changed.
